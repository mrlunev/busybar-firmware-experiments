#include "../js_modules.h"

#include <storage/storage.h>
#include <string.h>

static bool js_fs_extra_resolve_path(JsRunner* runner, const char* rel_path, char* out, size_t out_size) {
    if(!rel_path || rel_path[0] == '\0' || strstr(rel_path, "..")) {
        return false;
    }

    const int written =
        snprintf(out, out_size, "%s/%s/%s", EXT_PATH("apps"), runner->app_id, rel_path);
    return written > 0 && (size_t)written < out_size;
}

static bool js_fs_extra_get_path(
    JSContext* ctx,
    JsRunner* runner,
    JSValueConst value,
    char* out,
    size_t out_size,
    const char* error_text) {
    if(!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "%s", error_text);
        return false;
    }

    const char* rel_path = JS_ToCString(ctx, value);
    if(!rel_path) return false;

    bool ok = js_fs_extra_resolve_path(runner, rel_path, out, out_size);
    JS_FreeCString(ctx, rel_path);

    if(!ok) {
        JS_ThrowTypeError(ctx, "invalid fs_extra path");
    }

    return ok;
}

static JSValue js_fs_extra_mkdir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "mkdir expects a relative path");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    char full_path[192];
    if(!js_fs_extra_get_path(ctx, runner, argv[0], full_path, sizeof(full_path), "mkdir expects a relative path")) {
        return JS_EXCEPTION;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error err = storage_common_mkdir(storage, full_path);
    furi_record_close(RECORD_STORAGE);
    return JS_NewBool(ctx, err == FSE_OK || err == FSE_EXIST);
}

static JSValue js_fs_extra_rename(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2) {
        return JS_ThrowTypeError(ctx, "rename expects old and new relative paths");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    char old_path[192];
    char new_path[192];
    if(!js_fs_extra_get_path(
           ctx, runner, argv[0], old_path, sizeof(old_path), "rename expects old path")) {
        return JS_EXCEPTION;
    }
    if(!js_fs_extra_get_path(
           ctx, runner, argv[1], new_path, sizeof(new_path), "rename expects new path")) {
        return JS_EXCEPTION;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error err = storage_common_rename(storage, old_path, new_path);
    furi_record_close(RECORD_STORAGE);
    return JS_NewBool(ctx, err == FSE_OK);
}

static JSValue js_fs_extra_stat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "stat expects a relative path");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    char full_path[192];
    if(!js_fs_extra_get_path(ctx, runner, argv[0], full_path, sizeof(full_path), "stat expects a relative path")) {
        return JS_EXCEPTION;
    }

    FileInfo info;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error err = storage_common_stat(storage, full_path, &info);
    furi_record_close(RECORD_STORAGE);

    if(err != FSE_OK) {
        return JS_UNDEFINED;
    }

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "isDirectory", JS_NewBool(ctx, file_info_is_dir(&info)));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewFloat64(ctx, info.size));
    return obj;
}

static JSValue js_fs_extra_readdir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    char full_path[192];

    if(argc >= 1 && JS_IsString(argv[0])) {
        if(!js_fs_extra_get_path(
               ctx, runner, argv[0], full_path, sizeof(full_path), "readdir expects a relative path")) {
            return JS_EXCEPTION;
        }
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", EXT_PATH("apps"), runner->app_id);
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);

    if(!storage_dir_open(dir, full_path)) {
        storage_file_free(dir);
        furi_record_close(RECORD_STORAGE);
        return JS_NewArray(ctx);
    }

    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    FileInfo info;
    char name[256];

    while(storage_dir_read(dir, &info, name, sizeof(name))) {
        JSValue entry = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, entry, "name", JS_NewString(ctx, name));
        JS_SetPropertyStr(ctx, entry, "isDirectory", JS_NewBool(ctx, file_info_is_dir(&info)));
        JS_SetPropertyStr(ctx, entry, "size", JS_NewFloat64(ctx, info.size));
        JS_SetPropertyUint32(ctx, arr, idx++, entry);
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return arr;
}

JSValue js_module_fs_extra_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "mkdir", JS_NewCFunction(ctx, js_fs_extra_mkdir, "mkdir", 1));
    JS_SetPropertyStr(ctx, obj, "rename", JS_NewCFunction(ctx, js_fs_extra_rename, "rename", 2));
    JS_SetPropertyStr(ctx, obj, "stat", JS_NewCFunction(ctx, js_fs_extra_stat, "stat", 1));
    JS_SetPropertyStr(ctx, obj, "readdir", JS_NewCFunction(ctx, js_fs_extra_readdir, "readdir", 1));
    return obj;
}

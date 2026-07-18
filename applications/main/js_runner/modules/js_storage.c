#include "../js_modules.h"

#include <storage/storage.h>

static bool js_storage_resolve_path(JsRunner* runner, const char* rel_path, char* out, size_t out_size) {
    if(!rel_path || rel_path[0] == '\0' || strstr(rel_path, "..")) {
        return false;
    }

    const int written =
        snprintf(out, out_size, "%s/%s/%s", EXT_PATH("apps"), runner->app_id, rel_path);
    return written > 0 && (size_t)written < out_size;
}

static JSValue js_storage_exists(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "exists expects a relative path");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* rel_path = JS_ToCString(ctx, argv[0]);
    if(!rel_path) return JS_EXCEPTION;

    char full_path[192];
    if(!js_storage_resolve_path(runner, rel_path, full_path, sizeof(full_path))) {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "invalid storage path");
    }
    JS_FreeCString(ctx, rel_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, full_path) || storage_dir_exists(storage, full_path);
    furi_record_close(RECORD_STORAGE);

    return JS_NewBool(ctx, exists);
}

static JSValue js_storage_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "read expects a relative path");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* rel_path = JS_ToCString(ctx, argv[0]);
    if(!rel_path) return JS_EXCEPTION;

    char full_path[192];
    if(!js_storage_resolve_path(runner, rel_path, full_path, sizeof(full_path))) {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "invalid storage path");
    }
    JS_FreeCString(ctx, rel_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage_file_exists(storage, full_path)) {
        furi_record_close(RECORD_STORAGE);
        return JS_UNDEFINED;
    }

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, full_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return JS_ThrowInternalError(ctx, "cannot open file");
    }

    uint64_t size = storage_file_size(file);
    if(size > 64 * 1024) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return JS_ThrowRangeError(ctx, "file too large");
    }

    char* buffer = malloc((size_t)size + 1);
    if(!buffer) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return JS_ThrowInternalError(ctx, "out of memory");
    }
    size_t read = storage_file_read(file, buffer, (size_t)size);
    buffer[read] = '\0';

    JSValue result = JS_NewStringLen(ctx, buffer, read);

    free(buffer);
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return result;
}

static JSValue js_storage_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2 || !JS_IsString(argv[0]) || !JS_IsString(argv[1])) {
        return JS_ThrowTypeError(ctx, "write expects path and string data");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* rel_path = JS_ToCString(ctx, argv[0]);
    if(!rel_path) return JS_EXCEPTION;

    char full_path[192];
    if(!js_storage_resolve_path(runner, rel_path, full_path, sizeof(full_path))) {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "invalid storage path");
    }
    JS_FreeCString(ctx, rel_path);

    size_t data_len = 0;
    const char* data = JS_ToCStringLen(ctx, &data_len, argv[1]);
    if(!data) return JS_EXCEPTION;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, full_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, data, data_len) == data_len;
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    JS_FreeCString(ctx, data);
    return JS_NewBool(ctx, ok);
}

static JSValue js_storage_remove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "remove expects a relative path");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* rel_path = JS_ToCString(ctx, argv[0]);
    if(!rel_path) return JS_EXCEPTION;

    char full_path[192];
    if(!js_storage_resolve_path(runner, rel_path, full_path, sizeof(full_path))) {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "invalid storage path");
    }
    JS_FreeCString(ctx, rel_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error err = storage_common_remove(storage, full_path);
    furi_record_close(RECORD_STORAGE);
    return JS_NewBool(ctx, err == FSE_OK);
}

static JSValue js_storage_list(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    JsRunner* runner = JS_GetContextOpaque(ctx);
    char full_path[192];

    if(argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if(!JS_IsString(argv[0])) {
            return JS_ThrowTypeError(ctx, "list expects a relative path");
        }

        const char* rel_path = JS_ToCString(ctx, argv[0]);
        if(!rel_path) return JS_EXCEPTION;

        if(!js_storage_resolve_path(runner, rel_path, full_path, sizeof(full_path))) {
            JS_FreeCString(ctx, rel_path);
            return JS_ThrowTypeError(ctx, "invalid storage path");
        }
        JS_FreeCString(ctx, rel_path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", EXT_PATH("apps"), runner->app_id);
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);
    JSValue result = JS_NewArray(ctx);
    uint32_t idx = 0;

    if(storage_dir_open(dir, full_path)) {
        FileInfo info;
        char name[128];
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(name[0] == '\0') continue;
            JSValue item = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, name));
            JS_SetPropertyStr(ctx, item, "isDirectory", JS_NewBool(ctx, file_info_is_dir(&info)));
            JS_SetPropertyStr(ctx, item, "size", JS_NewFloat64(ctx, info.size));
            JS_SetPropertyUint32(ctx, result, idx++, item);
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return result;
}

JSValue js_module_storage_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "exists", JS_NewCFunction(ctx, js_storage_exists, "exists", 1));
    JS_SetPropertyStr(ctx, obj, "read", JS_NewCFunction(ctx, js_storage_read, "read", 1));
    JS_SetPropertyStr(ctx, obj, "write", JS_NewCFunction(ctx, js_storage_write, "write", 2));
    JS_SetPropertyStr(ctx, obj, "remove", JS_NewCFunction(ctx, js_storage_remove, "remove", 1));
    JS_SetPropertyStr(ctx, obj, "list", JS_NewCFunction(ctx, js_storage_list, "list", 0));
    return obj;
}

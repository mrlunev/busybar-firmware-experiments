#include "js_modules.h"

#include <storage/storage.h>
#include <toolbox/path.h>

typedef struct {
    char key[64];
    JSValue value;
} JsBuiltinModule;

typedef struct {
    char key[192];
    JSValue value;
} JsLocalModule;

struct JsModules {
    JSContext* ctx;
    JsRunner* runner;
    JsBuiltinModule builtins[JS_RUNNER_MAX_MODULE_CACHE];
    size_t builtin_count;
    JsLocalModule locals[JS_RUNNER_MAX_LOCAL_MODULES];
    size_t local_count;
    char active_dir[128];
    char loading_stack[JS_RUNNER_MAX_LOCAL_MODULES][192];
    size_t loading_depth;
};

extern JSValue js_module_display_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_input_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_audio_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_fs_extra_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_fetch_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_json_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_settings_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_status_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_storage_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_system_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_time_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_timer_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_radio_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_config_create(JSContext* ctx, JsRunner* runner);
extern JSValue js_module_wifi_create(JSContext* ctx, JsRunner* runner);

static const JsModuleDescriptor js_builtin_modules[] = {
    {"audio", js_module_audio_create},
    {"config", js_module_config_create},
    {"display", js_module_display_create},
    {"fetch", js_module_fetch_create},
    {"fs_extra", js_module_fs_extra_create},
    {"input", js_module_input_create},
    {"json", js_module_json_create},
    {"radio", js_module_radio_create},
    {"settings", js_module_settings_create},
    {"status", js_module_status_create},
    {"storage", js_module_storage_create},
    {"system", js_module_system_create},
    {"time", js_module_time_create},
    {"timer", js_module_timer_create},
    {"wifi", js_module_wifi_create},
};

static JsBuiltinModule* js_modules_find_builtin(JsBuiltinModule* items, size_t count, const char* key) {
    for(size_t i = 0; i < count; i++) {
        if(strcmp(items[i].key, key) == 0) {
            return &items[i];
        }
    }
    return NULL;
}

static JsLocalModule* js_modules_find_local(JsLocalModule* items, size_t count, const char* key) {
    for(size_t i = 0; i < count; i++) {
        if(strcmp(items[i].key, key) == 0) {
            return &items[i];
        }
    }
    return NULL;
}

static bool js_modules_is_loading(JsModules* modules, const char* path) {
    for(size_t i = 0; i < modules->loading_depth; i++) {
        if(strcmp(modules->loading_stack[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static bool js_module_has_js_suffix(const char* name) {
    size_t len = strlen(name);
    return len >= 3 && strcmp(name + len - 3, ".js") == 0;
}

static bool
    js_module_resolve_local(JsModules* modules, const char* name, char* out_path, size_t out_size) {
    const char* base_dir = modules->active_dir[0] ? modules->active_dir : modules->runner->script_dir;
    const char* local_name = name;
    if(strncmp(local_name, "./", 2) == 0) {
        local_name += 2;
    }

    if(local_name[0] == '\0' || local_name[0] == '/' || strchr(local_name, '\\') || strstr(local_name, "..")) {
        return false;
    }

    if(js_module_has_js_suffix(local_name)) {
        snprintf(out_path, out_size, "%s/%s", base_dir, local_name);
    } else {
        snprintf(out_path, out_size, "%s/%s.js", base_dir, local_name);
    }

    return strlen(out_path) < out_size - 1;
}

static JSValue js_module_load_builtin(JsModules* modules, const char* name) {
    JsBuiltinModule* cached = js_modules_find_builtin(modules->builtins, modules->builtin_count, name);
    if(cached) {
        return JS_DupValue(modules->ctx, cached->value);
    }

    for(size_t i = 0; i < COUNT_OF(js_builtin_modules); i++) {
        if(strcmp(js_builtin_modules[i].name, name) != 0) continue;
        if(modules->builtin_count >= JS_RUNNER_MAX_MODULE_CACHE) {
            return JS_ThrowRangeError(modules->ctx, "built-in module cache is full");
        }

        JSValue value = js_builtin_modules[i].create(modules->ctx, modules->runner);
        if(JS_IsException(value)) {
            return value;
        }

        JsBuiltinModule* slot = &modules->builtins[modules->builtin_count++];
        strlcpy(slot->key, name, sizeof(slot->key));
        slot->value = JS_DupValue(modules->ctx, value);
        return value;
    }

    return JS_UNDEFINED;
}

static JSValue js_module_load_local(JsModules* modules, const char* name) {
    char path[192];
    if(!js_module_resolve_local(modules, name, path, sizeof(path))) {
        return JS_ThrowTypeError(modules->ctx, "invalid local module path \"%s\"", name);
    }

    JsLocalModule* cached = js_modules_find_local(modules->locals, modules->local_count, path);
    if(cached) {
        return JS_DupValue(modules->ctx, cached->value);
    }

    if(js_modules_is_loading(modules, path)) {
        return JS_ThrowReferenceError(modules->ctx, "cyclic local module import: %s", name);
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, path);
    furi_record_close(RECORD_STORAGE);

    if(!exists) {
        return JS_ThrowReferenceError(modules->ctx, "module \"%s\" not found", name);
    }

    if(modules->local_count >= JS_RUNNER_MAX_LOCAL_MODULES) {
        return JS_ThrowRangeError(modules->ctx, "local module cache is full");
    }

    if(modules->loading_depth >= JS_RUNNER_MAX_LOCAL_MODULES) {
        return JS_ThrowRangeError(modules->ctx, "local module loading stack is full");
    }

    char previous_dir[sizeof(modules->active_dir)];
    strlcpy(previous_dir, modules->active_dir, sizeof(previous_dir));
    strlcpy(modules->loading_stack[modules->loading_depth++], path, sizeof(modules->loading_stack[0]));

    FuriString* dir = furi_string_alloc();
    path_extract_dirname(path, dir);
    strlcpy(modules->active_dir, furi_string_get_cstr(dir), sizeof(modules->active_dir));
    furi_string_free(dir);

    JSValue value = js_runner_exec_file(modules->runner, path);

    if(modules->loading_depth > 0) {
        modules->loading_depth--;
        modules->loading_stack[modules->loading_depth][0] = '\0';
    }
    strlcpy(modules->active_dir, previous_dir, sizeof(modules->active_dir));

    if(JS_IsException(value)) {
        return value;
    }

    JsLocalModule* slot = &modules->locals[modules->local_count++];
    strlcpy(slot->key, path, sizeof(slot->key));
    slot->value = JS_DupValue(modules->ctx, value);

    return value;
}

JsModules* js_modules_create(JSContext* ctx, JsRunner* runner) {
    JsModules* modules = calloc(1, sizeof(JsModules));
    if(!modules) return NULL;
    modules->ctx = ctx;
    modules->runner = runner;
    return modules;
}

void js_modules_destroy(JsModules* modules) {
    if(!modules) return;

    for(size_t i = 0; i < modules->builtin_count; i++) {
        JS_FreeValue(modules->ctx, modules->builtins[i].value);
    }

    for(size_t i = 0; i < modules->local_count; i++) {
        JS_FreeValue(modules->ctx, modules->locals[i].value);
    }

    free(modules);
}

JSValue js_module_require(JsModules* modules, const char* name, size_t name_len) {
    if(name_len == 0 || !name) {
        return JS_ThrowTypeError(modules->ctx, "module name is empty");
    }

    char module_name[64];
    if(name_len >= sizeof(module_name)) {
        return JS_ThrowRangeError(modules->ctx, "module name is too long");
    }

    size_t copy_len = name_len;
    memcpy(module_name, name, copy_len);
    module_name[copy_len] = '\0';

    JSValue builtin = js_module_load_builtin(modules, module_name);
    if(!JS_IsUndefined(builtin)) {
        return builtin;
    }

    return js_module_load_local(modules, module_name);
}

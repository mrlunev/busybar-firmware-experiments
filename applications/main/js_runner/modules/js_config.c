#include "../js_modules.h"

#include <cjson/cJSON.h>
#include <storage/storage.h>

#define CONFIG_FILE     "config.json"
#define SCHEMA_FILE     "config.schema.json"
#define CONFIG_MAX_SIZE (16 * 1024)

typedef struct {
    cJSON* cached_config;
    cJSON* cached_defaults;
    char app_dir[192];
} JsConfigState;

static bool js_config_read_file(const char* path, char** out_buf, size_t* out_len) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage_file_exists(storage, path)) {
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    uint64_t size = storage_file_size(file);
    if(size > CONFIG_MAX_SIZE) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    char* buf = malloc((size_t)size + 1);
    size_t read = storage_file_read(file, buf, (size_t)size);
    buf[read] = '\0';

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    *out_buf = buf;
    if(out_len) *out_len = read;
    return true;
}

static bool js_config_write_file(const char* path, const char* data, size_t len) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, data, len) == len;
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static cJSON* js_config_parse_defaults(const char* schema_path) {
    char* buf = NULL;
    if(!js_config_read_file(schema_path, &buf, NULL)) {
        return NULL;
    }

    cJSON* schema = cJSON_Parse(buf);
    free(buf);
    if(!schema) return NULL;

    cJSON* fields = cJSON_GetObjectItemCaseSensitive(schema, "fields");
    if(!cJSON_IsArray(fields)) {
        cJSON_Delete(schema);
        return NULL;
    }

    cJSON* defaults = cJSON_CreateObject();
    for(cJSON* field = fields->child; field; field = field->next) {
        cJSON* key = cJSON_GetObjectItemCaseSensitive(field, "key");
        cJSON* def = cJSON_GetObjectItemCaseSensitive(field, "default");
        if(cJSON_IsString(key) && def) {
            cJSON_AddItemToObject(defaults, key->valuestring, cJSON_Duplicate(def, true));
        }
    }

    cJSON_Delete(schema);
    return defaults;
}

static cJSON* js_config_merge(const cJSON* defaults, const cJSON* config) {
    cJSON* result = cJSON_CreateObject();

    if(defaults) {
        for(cJSON* item = defaults->child; item; item = item->next) {
            cJSON_AddItemToObject(result, item->string, cJSON_Duplicate(item, true));
        }
    }

    if(config) {
        for(cJSON* item = config->child; item; item = item->next) {
            cJSON_DeleteItemFromObjectCaseSensitive(result, item->string);
            cJSON_AddItemToObject(result, item->string, cJSON_Duplicate(item, true));
        }
    }

    return result;
}

static JsConfigState* js_config_get_state(JsRunner* runner) {
    if(!runner->config_state) {
        JsConfigState* state = malloc(sizeof(JsConfigState));
        memset(state, 0, sizeof(JsConfigState));
        snprintf(state->app_dir, sizeof(state->app_dir), "%s/%s", EXT_PATH("apps"), runner->app_id);
        runner->config_state = state;
    }
    return runner->config_state;
}

static cJSON* js_config_ensure_loaded(JsRunner* runner) {
    JsConfigState* state = js_config_get_state(runner);

    if(state->cached_config) {
        return state->cached_config;
    }

    if(!state->cached_defaults) {
        char schema_path[256];
        snprintf(schema_path, sizeof(schema_path), "%s/%s", state->app_dir, SCHEMA_FILE);
        state->cached_defaults = js_config_parse_defaults(schema_path);
    }

    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/%s", state->app_dir, CONFIG_FILE);

    cJSON* file_config = NULL;
    char* buf = NULL;
    if(js_config_read_file(config_path, &buf, NULL)) {
        file_config = cJSON_Parse(buf);
        free(buf);
    }

    state->cached_config = js_config_merge(state->cached_defaults, file_config);

    if(file_config) cJSON_Delete(file_config);

    return state->cached_config;
}

static bool js_config_persist(JsRunner* runner, cJSON* obj) {
    JsConfigState* state = js_config_get_state(runner);

    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/%s", state->app_dir, CONFIG_FILE);

    char* str = cJSON_PrintUnformatted(obj);
    if(!str) return false;

    bool ok = js_config_write_file(config_path, str, strlen(str));
    free(str);
    return ok;
}

/* cJSON -> JSValue conversion (self-contained to avoid coupling with js_json.c) */

#define CJSON_TO_JS_MAX_DEPTH 16

static JSValue js_config_cjson_to_js(JSContext* ctx, const cJSON* item, uint8_t depth) {
    if(depth > CJSON_TO_JS_MAX_DEPTH) {
        return JS_ThrowRangeError(ctx, "config nesting too deep");
    }

    if(!item || cJSON_IsNull(item)) {
        return bsb_jerry_make_null();
    }
    if(cJSON_IsBool(item)) {
        return JS_NewBool(ctx, cJSON_IsTrue(item));
    }
    if(cJSON_IsNumber(item)) {
        return JS_NewFloat64(ctx, cJSON_GetNumberValue(item));
    }
    if(cJSON_IsString(item)) {
        const char* val = cJSON_GetStringValue(item);
        return JS_NewString(ctx, val ? val : "");
    }
    if(cJSON_IsArray(item)) {
        JSValue arr = JS_NewArray(ctx);
        uint32_t idx = 0;
        for(const cJSON* child = item->child; child; child = child->next, idx++) {
            JSValue v = js_config_cjson_to_js(ctx, child, depth + 1);
            if(JS_IsException(v)) {
                JS_FreeValue(ctx, arr);
                return v;
            }
            JS_SetPropertyUint32(ctx, arr, idx, v);
        }
        return arr;
    }
    if(cJSON_IsObject(item)) {
        JSValue obj = JS_NewObject(ctx);
        for(const cJSON* child = item->child; child; child = child->next) {
            JSValue v = js_config_cjson_to_js(ctx, child, depth + 1);
            if(JS_IsException(v)) {
                JS_FreeValue(ctx, obj);
                return v;
            }
            JS_SetPropertyStr(ctx, obj, child->string ? child->string : "", v);
        }
        return obj;
    }

    return JS_UNDEFINED;
}

/* JSValue -> cJSON conversion */

static cJSON* js_config_js_to_cjson(JSContext* ctx, JSValueConst value, uint8_t depth) {
    if(depth > CJSON_TO_JS_MAX_DEPTH) return NULL;

    if(JS_IsNull(value) || JS_IsUndefined(value)) {
        return cJSON_CreateNull();
    }
    if(JS_IsBool(value)) {
        return cJSON_CreateBool(JS_ToBool(ctx, value));
    }
    if(JS_IsNumber(value)) {
        double n = 0;
        JS_ToFloat64(ctx, &n, value);
        return cJSON_CreateNumber(n);
    }
    if(JS_IsString(value)) {
        const char* s = JS_ToCString(ctx, value);
        if(!s) return NULL;
        cJSON* item = cJSON_CreateString(s);
        JS_FreeCString(ctx, s);
        return item;
    }
    if(!JS_IsObject(value)) {
        return cJSON_CreateNull();
    }

    bool is_array = false;
    if(bsb_jerry_value_is_array(value, &is_array) && is_array) {
        cJSON* arr = cJSON_CreateArray();
        uint32_t len = 0;
        bsb_jerry_array_length(value, &len);
        for(uint32_t i = 0; i < len; i++) {
            JSValue child = JS_GetPropertyUint32(ctx, value, i);
            cJSON* cchild = js_config_js_to_cjson(ctx, child, depth + 1);
            JS_FreeValue(ctx, child);
            if(cchild) cJSON_AddItemToArray(arr, cchild);
        }
        return arr;
    }

    cJSON* obj = cJSON_CreateObject();
    JSValue keys = bsb_jerry_object_keys(value);
    if(JS_IsException(keys)) {
        cJSON_Delete(obj);
        return NULL;
    }
    uint32_t kc = 0;
    bsb_jerry_array_length(keys, &kc);
    for(uint32_t i = 0; i < kc; i++) {
        JSValue kv = JS_GetPropertyUint32(ctx, keys, i);
        const char* key = JS_ToCString(ctx, kv);
        JS_FreeValue(ctx, kv);
        if(!key) continue;
        JSValue child = JS_GetPropertyStr(ctx, value, key);
        cJSON* cchild = js_config_js_to_cjson(ctx, child, depth + 1);
        JS_FreeValue(ctx, child);
        if(cchild) cJSON_AddItemToObject(obj, key, cchild);
        JS_FreeCString(ctx, key);
    }
    JS_FreeValue(ctx, keys);
    return obj;
}

/* --- JS API functions --- */

static JSValue js_config_load(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    cJSON* merged = js_config_ensure_loaded(runner);

    if(!merged) {
        return JS_NewObject(ctx);
    }

    return js_config_cjson_to_js(ctx, merged, 0);
}

static JSValue js_config_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);

    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "config.get expects a key string");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* key = JS_ToCString(ctx, argv[0]);
    if(!key) return JS_EXCEPTION;

    cJSON* merged = js_config_ensure_loaded(runner);
    if(!merged) {
        JS_FreeCString(ctx, key);
        if(argc >= 2) return JS_DupValue(ctx, argv[1]);
        return JS_UNDEFINED;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(merged, key);
    JS_FreeCString(ctx, key);

    if(!item) {
        if(argc >= 2) return JS_DupValue(ctx, argv[1]);
        return JS_UNDEFINED;
    }

    return js_config_cjson_to_js(ctx, item, 0);
}

static JSValue js_config_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);

    if(argc < 2 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "config.set expects (key, value)");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* key = JS_ToCString(ctx, argv[0]);
    if(!key) return JS_EXCEPTION;

    cJSON* merged = js_config_ensure_loaded(runner);
    if(!merged) {
        JS_FreeCString(ctx, key);
        return JS_NewBool(ctx, false);
    }

    cJSON* new_val = js_config_js_to_cjson(ctx, argv[1], 0);
    if(!new_val) {
        JS_FreeCString(ctx, key);
        return JS_NewBool(ctx, false);
    }

    cJSON_DeleteItemFromObjectCaseSensitive(merged, key);
    cJSON_AddItemToObject(merged, key, new_val);
    JS_FreeCString(ctx, key);

    bool ok = js_config_persist(runner, merged);
    return JS_NewBool(ctx, ok);
}

static JSValue js_config_save(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);

    if(argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "config.save expects an object");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsConfigState* state = js_config_get_state(runner);

    cJSON* new_config = js_config_js_to_cjson(ctx, argv[0], 0);
    if(!new_config) {
        return JS_NewBool(ctx, false);
    }

    if(state->cached_config) {
        cJSON_Delete(state->cached_config);
    }
    state->cached_config = new_config;

    bool ok = js_config_persist(runner, new_config);
    return JS_NewBool(ctx, ok);
}

static JSValue
    js_config_reload(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsConfigState* state = js_config_get_state(runner);

    if(state->cached_config) {
        cJSON_Delete(state->cached_config);
        state->cached_config = NULL;
    }

    cJSON* merged = js_config_ensure_loaded(runner);
    if(!merged) {
        return JS_NewObject(ctx);
    }

    return js_config_cjson_to_js(ctx, merged, 0);
}

/* --- Module lifecycle --- */

void js_config_cleanup(JsRunner* runner) {
    if(!runner->config_state) return;
    JsConfigState* state = runner->config_state;

    if(state->cached_config) cJSON_Delete(state->cached_config);
    if(state->cached_defaults) cJSON_Delete(state->cached_defaults);

    free(state);
    runner->config_state = NULL;
}

JSValue js_module_config_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "load", JS_NewCFunction(ctx, js_config_load, "load", 0));
    JS_SetPropertyStr(ctx, obj, "get", JS_NewCFunction(ctx, js_config_get, "get", 1));
    JS_SetPropertyStr(ctx, obj, "set", JS_NewCFunction(ctx, js_config_set, "set", 2));
    JS_SetPropertyStr(ctx, obj, "save", JS_NewCFunction(ctx, js_config_save, "save", 1));
    JS_SetPropertyStr(ctx, obj, "reload", JS_NewCFunction(ctx, js_config_reload, "reload", 0));
    return obj;
}

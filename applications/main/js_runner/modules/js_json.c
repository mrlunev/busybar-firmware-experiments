#include "../js_modules.h"

#include <cjson/cJSON.h>

#define JS_JSON_MAX_DEPTH 32

static JSValue js_json_from_cjson(JSContext* ctx, const cJSON* item, uint8_t depth);
static cJSON* js_json_to_cjson(JSContext* ctx, JSValueConst value, uint8_t depth);

static JSValue js_json_from_cjson(JSContext* ctx, const cJSON* item, uint8_t depth) {
    if(depth > JS_JSON_MAX_DEPTH) {
        return JS_ThrowRangeError(ctx, "JSON nesting is too deep");
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
        const char* value = cJSON_GetStringValue(item);
        return JS_NewString(ctx, value ? value : "");
    }

    if(cJSON_IsArray(item)) {
        JSValue array = JS_NewArray(ctx);
        if(JS_IsException(array)) {
            return array;
        }

        uint32_t index = 0;
        for(const cJSON* child = item->child; child; child = child->next, index++) {
            JSValue value = js_json_from_cjson(ctx, child, depth + 1);
            if(JS_IsException(value)) {
                JS_FreeValue(ctx, array);
                return value;
            }

            if(JS_SetPropertyUint32(ctx, array, index, value) != 0) {
                JS_FreeValue(ctx, array);
                return JS_ThrowInternalError(ctx, "failed to populate JSON array");
            }
        }

        return array;
    }

    if(cJSON_IsObject(item)) {
        JSValue obj = JS_NewObject(ctx);
        if(JS_IsException(obj)) {
            return obj;
        }

        for(const cJSON* child = item->child; child; child = child->next) {
            JSValue value = js_json_from_cjson(ctx, child, depth + 1);
            if(JS_IsException(value)) {
                JS_FreeValue(ctx, obj);
                return value;
            }

            if(JS_SetPropertyStr(ctx, obj, child->string ? child->string : "", value) != 0) {
                JS_FreeValue(ctx, obj);
                return JS_ThrowInternalError(ctx, "failed to populate JSON object");
            }
        }

        return obj;
    }

    return JS_ThrowTypeError(ctx, "unsupported JSON value");
}

static cJSON* js_json_to_cjson(JSContext* ctx, JSValueConst value, uint8_t depth) {
    if(depth > JS_JSON_MAX_DEPTH) {
        JS_ThrowRangeError(ctx, "JSON nesting is too deep");
        return NULL;
    }

    if(JS_IsUndefined(value)) {
        JS_ThrowTypeError(ctx, "cannot stringify undefined");
        return NULL;
    }

    if(JS_IsNull(value)) {
        return cJSON_CreateNull();
    }

    if(JS_IsBool(value)) {
        return cJSON_CreateBool(JS_ToBool(ctx, value));
    }

    if(JS_IsNumber(value)) {
        double number = 0;
        JS_ToFloat64(ctx, &number, value);
        return cJSON_CreateNumber(number);
    }

    if(JS_IsString(value)) {
        const char* str = JS_ToCString(ctx, value);
        if(!str) return NULL;
        cJSON* item = cJSON_CreateString(str);
        JS_FreeCString(ctx, str);
        return item;
    }

    if(!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "cannot stringify this value");
        return NULL;
    }

    bool is_array = false;
    if(!bsb_jerry_value_is_array(value, &is_array)) {
        JS_ThrowInternalError(ctx, "failed to inspect JSON value");
        return NULL;
    }

    if(is_array) {
        cJSON* array = cJSON_CreateArray();
        if(!array) {
            JS_ThrowInternalError(ctx, "failed to allocate JSON array");
            return NULL;
        }

        uint32_t length = 0;
        if(!bsb_jerry_array_length(value, &length)) {
            cJSON_Delete(array);
            JS_ThrowInternalError(ctx, "failed to read array length");
            return NULL;
        }

        for(uint32_t i = 0; i < length; i++) {
            JSValue child = JS_GetPropertyUint32(ctx, value, i);
            cJSON* child_json = js_json_to_cjson(ctx, child, depth + 1);
            JS_FreeValue(ctx, child);
            if(!child_json) {
                cJSON_Delete(array);
                return NULL;
            }

            if(!cJSON_AddItemToArray(array, child_json)) {
                cJSON_Delete(child_json);
                cJSON_Delete(array);
                JS_ThrowInternalError(ctx, "failed to append JSON array item");
                return NULL;
            }
        }

        return array;
    }

    cJSON* object = cJSON_CreateObject();
    if(!object) {
        JS_ThrowInternalError(ctx, "failed to allocate JSON object");
        return NULL;
    }

    JSValue keys = bsb_jerry_object_keys(value);
    if(JS_IsException(keys)) {
        cJSON_Delete(object);
        JS_ThrowInternalError(ctx, "failed to enumerate JSON object");
        return NULL;
    }

    uint32_t key_count = 0;
    if(!bsb_jerry_array_length(keys, &key_count)) {
        JS_FreeValue(ctx, keys);
        cJSON_Delete(object);
        JS_ThrowInternalError(ctx, "failed to inspect JSON object keys");
        return NULL;
    }

    for(uint32_t i = 0; i < key_count; i++) {
        JSValue key_value = JS_GetPropertyUint32(ctx, keys, i);
        size_t key_len = 0;
        const char* key = JS_ToCStringLen(ctx, &key_len, key_value);
        JS_FreeValue(ctx, key_value);
        if(!key) {
            JS_FreeValue(ctx, keys);
            cJSON_Delete(object);
            return NULL;
        }

        JSValue child = JS_GetPropertyStr(ctx, value, key);
        cJSON* child_json = js_json_to_cjson(ctx, child, depth + 1);
        JS_FreeValue(ctx, child);
        if(!child_json) {
            JS_FreeCString(ctx, key);
            JS_FreeValue(ctx, keys);
            cJSON_Delete(object);
            return NULL;
        }

        if(!cJSON_AddItemToObject(object, key, child_json)) {
            cJSON_Delete(child_json);
            JS_FreeCString(ctx, key);
            JS_FreeValue(ctx, keys);
            cJSON_Delete(object);
            JS_ThrowInternalError(ctx, "failed to append JSON object property");
            return NULL;
        }

        JS_FreeCString(ctx, key);
    }

    JS_FreeValue(ctx, keys);
    return object;
}

static JSValue js_json_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "parse expects a JSON string");
    }

    size_t len = 0;
    const char* input = JS_ToCStringLen(ctx, &len, argv[0]);
    if(!input) {
        return JS_EXCEPTION;
    }

    cJSON* root = cJSON_ParseWithLength(input, len);

    JS_FreeCString(ctx, input);
    if(!root) {
        return JS_ThrowTypeError(ctx, "invalid JSON");
    }

    JSValue result = js_json_from_cjson(ctx, root, 0);
    cJSON_Delete(root);
    return result;
}

static JSValue js_json_stringify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "stringify expects a value");
    }

    cJSON* root = js_json_to_cjson(ctx, argv[0], 0);
    if(!root) {
        return JS_EXCEPTION;
    }

    char* encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!encoded) {
        return JS_ThrowInternalError(ctx, "failed to stringify JSON");
    }

    JSValue result = JS_NewString(ctx, encoded);
    free(encoded);
    return result;
}

JSValue js_module_json_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "parse", JS_NewCFunction(ctx, js_json_parse, "parse", 1));
    JS_SetPropertyStr(
        ctx, obj, "stringify", JS_NewCFunction(ctx, js_json_stringify, "stringify", 1));
    return obj;
}

#include "js_quickjs_compat.h"

#include <furi.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    JSContext* ctx;
    JSCFunction fn;
} JsCompatFunctionData;

static void js_compat_enter(JSContext* ctx, BsbJerryScope* scope) {
    bsb_jerry_scope_enter(ctx->jerry, scope);
}

static void js_compat_leave(BsbJerryScope* scope) {
    bsb_jerry_scope_leave(scope);
}

static void js_compat_function_free(void* native_p, struct jerry_object_native_info_t* info_p) {
    UNUSED(info_p);
    free(native_p);
}

static const jerry_object_native_info_t js_compat_function_info = {
    .free_cb = js_compat_function_free,
    .number_of_references = 0,
    .offset_of_references = 0,
};

static jerry_value_t js_compat_function_dispatch(
    const jerry_call_info_t* call_info_p,
    const jerry_value_t args_p[],
    const jerry_length_t args_count) {
    JsCompatFunctionData* data =
        jerry_object_get_native_ptr(call_info_p->function, &js_compat_function_info);

    if(!data || !data->fn) {
        return bsb_jerry_throw_type_error("Missing function binding");
    }

    return data->fn(
        data->ctx, call_info_p->this_value, (int)args_count, (JSValueConst*)args_p);
}

JSRuntime* JS_NewRuntime(void) {
    JSRuntime* rt = calloc(1, sizeof(JSRuntime));
    if(!rt) return NULL;
    rt->memory_limit = 256 * 1024;
    rt->stack_limit = 16 * 1024;
    return rt;
}

void JS_FreeRuntime(JSRuntime* rt) {
    free(rt);
}

void JS_SetMemoryLimit(JSRuntime* rt, size_t limit) {
    if(rt) rt->memory_limit = limit;
}

void JS_SetMaxStackSize(JSRuntime* rt, size_t limit) {
    if(rt) rt->stack_limit = limit;
}

void JS_SetInterruptHandler(JSRuntime* rt, JSInterruptHandler cb, void* opaque) {
    if(!rt) return;
    rt->interrupt_handler = cb;
    rt->interrupt_opaque = opaque;
}

JSContext* JS_NewContext(JSRuntime* rt) {
    JSContext* ctx = calloc(1, sizeof(JSContext));
    if(!ctx) return NULL;
    ctx->runtime = rt;
    ctx->jerry =
        bsb_jerry_context_create(rt ? (rt->memory_limit / 1024u) : 0, "JsRunner");
    if(!ctx->jerry) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void JS_FreeContext(JSContext* ctx) {
    if(!ctx) return;
    bsb_jerry_context_destroy(ctx->jerry);
    free(ctx);
}

void JS_SetContextOpaque(JSContext* ctx, void* opaque) {
    if(ctx) ctx->opaque = opaque;
}

void* JS_GetContextOpaque(JSContext* ctx) {
    return ctx ? ctx->opaque : NULL;
}

JSValue JS_GetGlobalObject(JSContext* ctx) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_current_realm();
    js_compat_leave(&scope);
    return value;
}

JSValue JS_GetException(JSContext* ctx) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = bsb_jerry_get_exception();
    js_compat_leave(&scope);
    return value;
}

JSValue JS_Eval(JSContext* ctx, const char* input, size_t input_len, const char* filename, int flags) {
    UNUSED(filename);
    UNUSED(flags);
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_eval((const jerry_char_t*)input, input_len, 0);
    js_compat_leave(&scope);
    return value;
}

JSValue JS_Call(
    JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj, int argc, JSValueConst* argv) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = bsb_jerry_call(func_obj, this_obj, argv, (jerry_length_t)argc);
    js_compat_leave(&scope);
    return value;
}

JSValue JS_NewObject(JSContext* ctx) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_object();
    js_compat_leave(&scope);
    return value;
}

JSValue JS_NewArray(JSContext* ctx) {
    static const char kArrayLiteral[] = "[]";
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_eval((const jerry_char_t*)kArrayLiteral, sizeof(kArrayLiteral) - 1, 0);
    js_compat_leave(&scope);
    return value;
}

JSValue JS_NewString(JSContext* ctx, const char* str) {
    return JS_NewStringLen(ctx, str, str ? strlen(str) : 0);
}

JSValue JS_NewStringLen(JSContext* ctx, const char* str, size_t len) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_string((const jerry_char_t*)(str ? str : ""), len, JERRY_ENCODING_UTF8);
    js_compat_leave(&scope);
    return value;
}

JSValue JS_NewInt32(JSContext* ctx, int32_t value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue result = jerry_number((float)value);
    js_compat_leave(&scope);
    return result;
}

JSValue JS_NewFloat64(JSContext* ctx, double value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue result = bsb_jerry_make_number(value);
    js_compat_leave(&scope);
    return result;
}

JSValue JS_NewBool(JSContext* ctx, bool value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue result = jerry_boolean(value);
    js_compat_leave(&scope);
    return result;
}

JSValue JS_NewCFunction(JSContext* ctx, JSCFunction fn, const char* name, int length) {
    UNUSED(name);
    UNUSED(length);
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);

    JSValue function = jerry_function_external(js_compat_function_dispatch);
    JsCompatFunctionData* data = malloc(sizeof(JsCompatFunctionData));
    furi_check(data);
    data->ctx = ctx;
    data->fn = fn;
    jerry_object_set_native_ptr(function, &js_compat_function_info, data);

    js_compat_leave(&scope);
    return function;
}

JSValue JS_GetPropertyStr(JSContext* ctx, JSValueConst obj, const char* prop) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_object_get_sz(obj, prop);
    js_compat_leave(&scope);
    return value;
}

JSValue JS_GetPropertyUint32(JSContext* ctx, JSValueConst obj, uint32_t idx) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue value = jerry_object_get_index(obj, idx);
    js_compat_leave(&scope);
    return value;
}

int JS_SetPropertyStr(JSContext* ctx, JSValueConst obj, const char* prop, JSValue value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue status = jerry_object_set_sz(obj, prop, value);
    jerry_value_free(value);
    bool ok = !jerry_value_is_exception(status);
    jerry_value_free(status);
    js_compat_leave(&scope);
    return ok ? 0 : -1;
}

int JS_SetPropertyUint32(JSContext* ctx, JSValueConst obj, uint32_t idx, JSValue value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue status = jerry_object_set_index(obj, idx, value);
    jerry_value_free(value);
    bool ok = !jerry_value_is_exception(status);
    jerry_value_free(status);
    js_compat_leave(&scope);
    return ok ? 0 : -1;
}

JSValue JS_DupValue(JSContext* ctx, JSValueConst value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue result = bsb_jerry_value_dup(value);
    js_compat_leave(&scope);
    return result;
}

void JS_FreeValue(JSContext* ctx, JSValue value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    jerry_value_free(value);
    js_compat_leave(&scope);
}

const char* JS_ToCString(JSContext* ctx, JSValueConst value) {
    return JS_ToCStringLen(ctx, NULL, value);
}

const char* JS_ToCStringLen(JSContext* ctx, size_t* len, JSValueConst value) {
    BsbJerryScope scope;
    js_compat_enter(ctx, &scope);
    JSValue str_value = jerry_value_to_string(value);
    if(jerry_value_is_exception(str_value)) {
        js_compat_leave(&scope);
        return NULL;
    }

    /* jerry_string_length is the ECMA character count, not UTF-8 byte size. JERRY_ENCODING_UTF8
     * output can be larger (e.g. one U+2019 is 1 char but 3 UTF-8 bytes). A buffer sized to the
     * character count truncates JSON and can hang or corrupt cJSON. Reserve 4 bytes per char + NUL. */
    jerry_size_t str_len = jerry_string_length(str_value);
    if(str_len > 0 && (size_t)str_len > (SIZE_MAX / 4u)) {
        jerry_value_free(str_value);
        js_compat_leave(&scope);
        return NULL;
    }
    size_t buf_bytes = (size_t)str_len * 4u + 1u;
    char* buffer = malloc(buf_bytes);
    if(!buffer) {
        jerry_value_free(str_value);
        js_compat_leave(&scope);
        return NULL;
    }
    jerry_size_t written = jerry_string_to_buffer(
        str_value,
        JERRY_ENCODING_UTF8,
        (jerry_char_t*)buffer,
        (jerry_size_t)(buf_bytes - 1u));
    buffer[written] = '\0';
    if(len) *len = written;
    jerry_value_free(str_value);
    js_compat_leave(&scope);
    return buffer;
}

void JS_FreeCString(JSContext* ctx, const char* str) {
    UNUSED(ctx);
    free((void*)str);
}

int JS_ToInt32(JSContext* ctx, int32_t* out, JSValueConst value) {
    UNUSED(ctx);
    if(!out) return -1;
    *out = jerry_value_as_int32(value);
    return 0;
}

int JS_ToFloat64(JSContext* ctx, double* out, JSValueConst value) {
    UNUSED(ctx);
    if(!out) return -1;
    *out = (double)jerry_value_as_number(value);
    return 0;
}

int JS_ToBool(JSContext* ctx, JSValueConst value) {
    UNUSED(ctx);
    return bsb_jerry_value_to_bool(value) ? 1 : 0;
}

JSValue JS_ThrowFormattedError(JSContext* ctx, jerry_error_t type, const char* fmt, ...) {
    UNUSED(ctx);
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return bsb_jerry_throw_error(type, buffer);
}

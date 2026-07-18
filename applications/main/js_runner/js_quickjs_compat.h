#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <furi.h>
#include <jerry_runtime_manager.h>

typedef jerry_value_t JSValue;
typedef jerry_value_t JSValueConst;

typedef struct JSRuntime JSRuntime;
typedef struct JSContext JSContext;

typedef JSValue (*JSCFunction)(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
typedef int (*JSInterruptHandler)(JSRuntime* rt, void* opaque);

struct JSRuntime {
    size_t memory_limit;
    size_t stack_limit;
    JSInterruptHandler interrupt_handler;
    void* interrupt_opaque;
};

struct JSContext {
    JSRuntime* runtime;
    BsbJerryContext* jerry;
    void* opaque;
};

#define JS_EVAL_TYPE_GLOBAL 0
#define JS_UNDEFINED jerry_undefined()
#define JS_NULL jerry_null()
#define JS_EXCEPTION bsb_jerry_throw_error(JERRY_ERROR_COMMON, "Operation failed")

JSRuntime* JS_NewRuntime(void);
void JS_FreeRuntime(JSRuntime* rt);
void JS_SetMemoryLimit(JSRuntime* rt, size_t limit);
void JS_SetMaxStackSize(JSRuntime* rt, size_t limit);
void JS_SetInterruptHandler(JSRuntime* rt, JSInterruptHandler cb, void* opaque);

JSContext* JS_NewContext(JSRuntime* rt);
void JS_FreeContext(JSContext* ctx);
void JS_SetContextOpaque(JSContext* ctx, void* opaque);
void* JS_GetContextOpaque(JSContext* ctx);

JSValue JS_GetGlobalObject(JSContext* ctx);
JSValue JS_GetException(JSContext* ctx);
JSValue JS_Eval(JSContext* ctx, const char* input, size_t input_len, const char* filename, int flags);
JSValue JS_Call(
    JSContext* ctx, JSValueConst func_obj, JSValueConst this_obj, int argc, JSValueConst* argv);

JSValue JS_NewObject(JSContext* ctx);
JSValue JS_NewArray(JSContext* ctx);
JSValue JS_NewString(JSContext* ctx, const char* str);
JSValue JS_NewStringLen(JSContext* ctx, const char* str, size_t len);
JSValue JS_NewInt32(JSContext* ctx, int32_t value);
JSValue JS_NewFloat64(JSContext* ctx, double value);
JSValue JS_NewBool(JSContext* ctx, bool value);
JSValue JS_NewCFunction(JSContext* ctx, JSCFunction fn, const char* name, int length);

JSValue JS_GetPropertyStr(JSContext* ctx, JSValueConst obj, const char* prop);
JSValue JS_GetPropertyUint32(JSContext* ctx, JSValueConst obj, uint32_t idx);
int JS_SetPropertyStr(JSContext* ctx, JSValueConst obj, const char* prop, JSValue value);
int JS_SetPropertyUint32(JSContext* ctx, JSValueConst obj, uint32_t idx, JSValue value);

JSValue JS_DupValue(JSContext* ctx, JSValueConst value);
void JS_FreeValue(JSContext* ctx, JSValue value);

const char* JS_ToCString(JSContext* ctx, JSValueConst value);
const char* JS_ToCStringLen(JSContext* ctx, size_t* len, JSValueConst value);
void JS_FreeCString(JSContext* ctx, const char* str);
int JS_ToInt32(JSContext* ctx, int32_t* out, JSValueConst value);
int JS_ToFloat64(JSContext* ctx, double* out, JSValueConst value);
int JS_ToBool(JSContext* ctx, JSValueConst value);

JSValue JS_ThrowFormattedError(JSContext* ctx, jerry_error_t type, const char* fmt, ...);

#define JS_ThrowTypeError(ctx, ...) JS_ThrowFormattedError((ctx), JERRY_ERROR_TYPE, __VA_ARGS__)
#define JS_ThrowRangeError(ctx, ...) JS_ThrowFormattedError((ctx), JERRY_ERROR_RANGE, __VA_ARGS__)
#define JS_ThrowReferenceError(ctx, ...) \
    JS_ThrowFormattedError((ctx), JERRY_ERROR_REFERENCE, __VA_ARGS__)
#define JS_ThrowInternalError(ctx, ...) JS_ThrowFormattedError((ctx), JERRY_ERROR_COMMON, __VA_ARGS__)

static inline bool JS_IsUndefined(JSValueConst value) {
    return jerry_value_is_undefined(value);
}

static inline bool JS_IsNull(JSValueConst value) {
    return bsb_jerry_value_is_null(value);
}

static inline bool JS_IsException(JSValueConst value) {
    return jerry_value_is_exception(value);
}

static inline bool JS_IsNumber(JSValueConst value) {
    return jerry_value_is_number(value);
}

static inline bool JS_IsString(JSValueConst value) {
    return jerry_value_is_string(value);
}

static inline bool JS_IsObject(JSValueConst value) {
    return jerry_value_is_object(value);
}

static inline bool JS_IsBool(JSValueConst value) {
    return bsb_jerry_value_is_boolean(value);
}

static inline bool JS_IsFunction(JSContext* ctx, JSValueConst value) {
    UNUSED(ctx);
    return bsb_jerry_value_is_function(value);
}

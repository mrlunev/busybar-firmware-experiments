#include "../js_modules.h"

#include <time/time.h>

static JSValue js_time_now(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    return JS_NewFloat64(ctx, time_get_timestamp());
}

static JSValue js_time_now_ms(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    return JS_NewFloat64(ctx, time_get_timestamp_ms());
}

static JSValue js_time_local(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    Time* time = furi_record_open(RECORD_TIME);
    LocalTime lt = time_get_local_time(time);
    furi_record_close(RECORD_TIME);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "year", JS_NewInt32(ctx, lt.dt.year));
    JS_SetPropertyStr(ctx, obj, "month", JS_NewInt32(ctx, lt.dt.month));
    JS_SetPropertyStr(ctx, obj, "day", JS_NewInt32(ctx, lt.dt.dayofmonth));
    JS_SetPropertyStr(ctx, obj, "hour", JS_NewInt32(ctx, lt.dt.hour));
    JS_SetPropertyStr(ctx, obj, "minute", JS_NewInt32(ctx, lt.dt.minute));
    JS_SetPropertyStr(ctx, obj, "second", JS_NewInt32(ctx, lt.dt.second));
    JS_SetPropertyStr(ctx, obj, "weekday", JS_NewInt32(ctx, lt.dt.dayofweek));
    return obj;
}

JSValue js_module_time_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "now", JS_NewCFunction(ctx, js_time_now, "now", 0));
    JS_SetPropertyStr(ctx, obj, "nowMs", JS_NewCFunction(ctx, js_time_now_ms, "nowMs", 0));
    JS_SetPropertyStr(ctx, obj, "localTime", JS_NewCFunction(ctx, js_time_local, "localTime", 0));
    return obj;
}

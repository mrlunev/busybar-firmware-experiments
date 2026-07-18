#include "../js_modules.h"

#include <math.h>
#include <time/time.h>
#include <toolbox/tzutil.h>

static int32_t js_time_offset_minutes(const utz_offset_t* offset) {
    return (int32_t)offset->hours * 60 + offset->minutes;
}

static JSValue js_time_create_local_object(
    JSContext* ctx,
    const DateTime* dt,
    const char* timezone,
    const char* abbreviation,
    const utz_offset_t* offset) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "year", JS_NewInt32(ctx, dt->year));
    JS_SetPropertyStr(ctx, obj, "month", JS_NewInt32(ctx, dt->month));
    JS_SetPropertyStr(ctx, obj, "day", JS_NewInt32(ctx, dt->dayofmonth));
    JS_SetPropertyStr(ctx, obj, "hour", JS_NewInt32(ctx, dt->hour));
    JS_SetPropertyStr(ctx, obj, "minute", JS_NewInt32(ctx, dt->minute));
    JS_SetPropertyStr(ctx, obj, "second", JS_NewInt32(ctx, dt->second));
    JS_SetPropertyStr(ctx, obj, "weekday", JS_NewInt32(ctx, dt->dayofweek));
    JS_SetPropertyStr(
        ctx, obj, "offsetMinutes", JS_NewInt32(ctx, js_time_offset_minutes(offset)));
    JS_SetPropertyStr(ctx, obj, "timezone", JS_NewString(ctx, timezone ? timezone : ""));
    JS_SetPropertyStr(
        ctx, obj, "abbreviation", JS_NewString(ctx, abbreviation ? abbreviation : ""));
    return obj;
}

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
    TimeSettings settings;
    time_get_settings(time, &settings);
    furi_record_close(RECORD_TIME);

    const DateTime utc = datetime_timestamp_to_datetime(time_get_timestamp());
    TzutilTzInfo info;
    char abbreviation[TZUTIL_MAX_ABBR_LEN + 1] = "";
    if(tzutil_get_info_by_name(settings.timezone.name, &utc, &info)) {
        tzutil_get_abbr(&info, abbreviation, sizeof(abbreviation));
    }
    return js_time_create_local_object(
        ctx, &lt.dt, settings.timezone.name, abbreviation, &lt.offset);
}

static JSValue js_time_timezone(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    Time* time = furi_record_open(RECORD_TIME);
    TimeSettings settings;
    time_get_settings(time, &settings);
    furi_record_close(RECORD_TIME);
    return JS_NewString(ctx, settings.timezone.name);
}

static JSValue js_time_in_timezone(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2 || !JS_IsNumber(argv[0]) || !JS_IsString(argv[1])) {
        return JS_ThrowTypeError(ctx, "inTimezone expects timestamp and timezone name");
    }

    double timestamp_value = 0;
    if(JS_ToFloat64(ctx, &timestamp_value, argv[0])) return JS_EXCEPTION;
    if(!isfinite(timestamp_value) || timestamp_value < 946684800.0 ||
       timestamp_value >= 9025257600.0) {
        return JS_ThrowRangeError(ctx, "timestamp is outside timezone database range");
    }

    const char* timezone = JS_ToCString(ctx, argv[1]);
    if(!timezone) return JS_EXCEPTION;

    const DateTime utc = datetime_timestamp_to_datetime((time_t)timestamp_value);
    if(utc.year < UTZ_YEAR_OFFSET || utc.year > UTZ_YEAR_OFFSET + UINT8_MAX) {
        JS_FreeCString(ctx, timezone);
        return JS_ThrowRangeError(ctx, "timestamp is outside timezone database range");
    }

    TzutilTzInfo info;
    if(!tzutil_get_info_by_name(timezone, &utc, &info)) {
        JS_FreeCString(ctx, timezone);
        return JS_ThrowRangeError(ctx, "unknown timezone");
    }

    const DateTime local = utz_udatetime_add(&utc, &info.offset);
    char abbreviation[TZUTIL_MAX_ABBR_LEN + 1] = "";
    tzutil_get_abbr(&info, abbreviation, sizeof(abbreviation));
    JSValue result =
        js_time_create_local_object(ctx, &local, timezone, abbreviation, &info.offset);
    JS_FreeCString(ctx, timezone);
    return result;
}

JSValue js_module_time_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "now", JS_NewCFunction(ctx, js_time_now, "now", 0));
    JS_SetPropertyStr(ctx, obj, "nowMs", JS_NewCFunction(ctx, js_time_now_ms, "nowMs", 0));
    JS_SetPropertyStr(ctx, obj, "localTime", JS_NewCFunction(ctx, js_time_local, "localTime", 0));
    JS_SetPropertyStr(
        ctx, obj, "timezone", JS_NewCFunction(ctx, js_time_timezone, "timezone", 0));
    JS_SetPropertyStr(
        ctx,
        obj,
        "inTimezone",
        JS_NewCFunction(ctx, js_time_in_timezone, "inTimezone", 2));
    return obj;
}

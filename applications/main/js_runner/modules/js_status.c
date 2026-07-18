#include "../js_modules.h"

#include <stdio.h>
#include <status_lights/status_lights.h>
#include <string.h>

static bool js_status_parse_hex_color(const char* hex, Color* out_color) {
    if(!hex || !out_color) return false;
    if(hex[0] != '#' || strlen(hex) != 7) return false;

    unsigned int value = 0;
    if(sscanf(hex + 1, "%06x", &value) != 1) {
        return false;
    }

    *out_color = (Color)COLOR_MAKE_RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return true;
}

static JSValue js_status_run(JSContext* ctx, StatusLightsPreset preset, JSValueConst arg) {
    const char* color_str = NULL;
    Color color = (Color)COLOR_MAKE_RGB(255, 255, 255);

    if(!JS_IsUndefined(arg)) {
        if(!JS_IsString(arg)) {
            return JS_ThrowTypeError(ctx, "status color must be a string like #RRGGBB");
        }

        color_str = JS_ToCString(ctx, arg);
        if(!color_str) return JS_EXCEPTION;

        if(!js_status_parse_hex_color(color_str, &color)) {
            JS_FreeCString(ctx, color_str);
            return JS_ThrowTypeError(ctx, "invalid status color");
        }
    }

    StatusLights* status = furi_record_open(RECORD_STATUS_LIGHTS);
    status_lights_run_preset(status, preset, color);
    furi_record_close(RECORD_STATUS_LIGHTS);

    if(color_str) {
        JS_FreeCString(ctx, color_str);
    }

    return JS_UNDEFINED;
}

static JSValue js_status_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "set expects a color string");
    }

    return js_status_run(ctx, StatusLightsPresetStaticColor, argv[0]);
}

static JSValue js_status_blink(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "blink expects a color string");
    }

    return js_status_run(ctx, StatusLightsPresetBlink, argv[0]);
}

static JSValue js_status_off(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    return js_status_run(ctx, StatusLightsPresetOff, JS_UNDEFINED);
}

static JSValue js_status_fade(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "fade expects a color string");
    }
    return js_status_run(ctx, StatusLightsPresetFade, argv[0]);
}

static JSValue js_status_rainbow(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    return js_status_run(ctx, StatusLightsPresetRainbowGradient, JS_UNDEFINED);
}

static JSValue js_status_set_brightness(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "setBrightness expects a number 0-100");
    }

    int32_t val;
    JS_ToInt32(ctx, &val, argv[0]);
    if(val < 0) val = 0;
    if(val > 100) val = 100;

    StatusLights* status = furi_record_open(RECORD_STATUS_LIGHTS);
    StatusLightsBrightness brightness = {.val = (uint8_t)val};
    status_lights_set_brightness(status, brightness);
    furi_record_close(RECORD_STATUS_LIGHTS);

    return JS_UNDEFINED;
}

static JSValue js_status_get_brightness(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    StatusLights* status = furi_record_open(RECORD_STATUS_LIGHTS);
    StatusLightsBrightness brightness = {0};
    status_lights_get_brightness(status, &brightness);
    furi_record_close(RECORD_STATUS_LIGHTS);

    return JS_NewInt32(ctx, brightness.val);
}

JSValue js_module_status_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "set", JS_NewCFunction(ctx, js_status_set, "set", 1));
    JS_SetPropertyStr(ctx, obj, "blink", JS_NewCFunction(ctx, js_status_blink, "blink", 1));
    JS_SetPropertyStr(ctx, obj, "off", JS_NewCFunction(ctx, js_status_off, "off", 0));
    JS_SetPropertyStr(ctx, obj, "fade", JS_NewCFunction(ctx, js_status_fade, "fade", 1));
    JS_SetPropertyStr(ctx, obj, "rainbow", JS_NewCFunction(ctx, js_status_rainbow, "rainbow", 0));
    JS_SetPropertyStr(ctx, obj, "setBrightness", JS_NewCFunction(ctx, js_status_set_brightness, "setBrightness", 1));
    JS_SetPropertyStr(ctx, obj, "getBrightness", JS_NewCFunction(ctx, js_status_get_brightness, "getBrightness", 0));
    return obj;
}

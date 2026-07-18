#include "../js_modules.h"

#include <audio/audio.h>
#include <storage/storage.h>
#include <string.h>

static bool js_audio_resolve_path(JsRunner* runner, const char* rel_path, char* out, size_t out_size) {
    if(!rel_path || rel_path[0] == '\0' || strstr(rel_path, "..")) {
        return false;
    }

    snprintf(out, out_size, "%s/%s/%s", EXT_PATH("apps"), runner->app_id, rel_path);
    return strlen(out) < out_size - 1;
}

static JSValue js_audio_play_impl(JSContext* ctx, JSValueConst arg) {
    if(!JS_IsString(arg)) {
        return JS_ThrowTypeError(ctx, "play expects a relative path");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    const char* rel_path = JS_ToCString(ctx, arg);
    if(!rel_path) return JS_EXCEPTION;

    char full_path[192];
    if(!js_audio_resolve_path(runner, rel_path, full_path, sizeof(full_path))) {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "invalid audio path");
    }
    JS_FreeCString(ctx, rel_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, full_path);
    furi_record_close(RECORD_STORAGE);
    if(!exists) {
        return JS_ThrowReferenceError(ctx, "audio file not found");
    }

    Audio* audio = furi_record_open(RECORD_AUDIO);
    bool ok = audio_play_file(audio, full_path);
    furi_record_close(RECORD_AUDIO);
    return JS_NewBool(ctx, ok);
}

static JSValue js_audio_play(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "play expects a relative path");
    }
    return js_audio_play_impl(ctx, argv[0]);
}

static JSValue js_audio_beep(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "beep expects a relative path");
    }
    return js_audio_play_impl(ctx, argv[0]);
}

static JSValue js_audio_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(ctx);
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    Audio* audio = furi_record_open(RECORD_AUDIO);
    audio_stop(audio);
    furi_record_close(RECORD_AUDIO);
    return JS_UNDEFINED;
}

static JSValue js_audio_set_volume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "setVolume expects a number from 0.0 to 1.0");
    }

    double value = 0.0;
    JS_ToFloat64(ctx, &value, argv[0]);
    if(value < 0.0 || value > 1.0) {
        return JS_ThrowRangeError(ctx, "volume must be in range 0.0..1.0");
    }

    Audio* audio = furi_record_open(RECORD_AUDIO);
    audio_set_volume(audio, (float)value);
    furi_record_close(RECORD_AUDIO);
    return JS_UNDEFINED;
}

static JSValue js_audio_get_volume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    Audio* audio = furi_record_open(RECORD_AUDIO);
    float value = audio_get_volume(audio);
    furi_record_close(RECORD_AUDIO);
    return JS_NewFloat64(ctx, value);
}

static bool js_audio_enabled = false;

JSValue js_module_audio_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);

    if(!js_audio_enabled) {
        Audio* audio = furi_record_open(RECORD_AUDIO);
        audio_enable(audio);
        furi_record_close(RECORD_AUDIO);
        js_audio_enabled = true;
    }

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "play", JS_NewCFunction(ctx, js_audio_play, "play", 1));
    JS_SetPropertyStr(ctx, obj, "beep", JS_NewCFunction(ctx, js_audio_beep, "beep", 1));
    JS_SetPropertyStr(ctx, obj, "stop", JS_NewCFunction(ctx, js_audio_stop, "stop", 0));
    JS_SetPropertyStr(ctx, obj, "setVolume", JS_NewCFunction(ctx, js_audio_set_volume, "setVolume", 1));
    JS_SetPropertyStr(ctx, obj, "getVolume", JS_NewCFunction(ctx, js_audio_get_volume, "getVolume", 0));
    return obj;
}

void js_audio_cleanup(JsRunner* runner) {
    UNUSED(runner);
    if(js_audio_enabled) {
        Audio* audio = furi_record_open(RECORD_AUDIO);
        audio_stop(audio);
        audio_disable(audio);
        furi_record_close(RECORD_AUDIO);
        js_audio_enabled = false;
    }
}

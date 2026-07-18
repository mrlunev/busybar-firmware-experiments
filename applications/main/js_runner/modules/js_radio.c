#include "../js_modules.h"

#include <radio_stream/radio_stream.h>

static RadioStream* radio_instance = NULL;

static RadioStream* js_radio_get_instance(void) {
    if(!radio_instance) {
        radio_instance = radio_stream_alloc();
    }
    return radio_instance;
}

static JSValue js_radio_play(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "play expects a URL string");
    }

    const char* url = JS_ToCString(ctx, argv[0]);
    if(!url) return JS_EXCEPTION;

    RadioStream* rs = js_radio_get_instance();
    if(!rs) {
        JS_FreeCString(ctx, url);
        return JS_ThrowInternalError(ctx, "radio: out of memory");
    }

    bool ok = radio_stream_play(rs, url);
    JS_FreeCString(ctx, url);
    return JS_NewBool(ctx, ok);
}

static JSValue js_radio_play_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "playFile expects a path string");
    }

    const char* path = JS_ToCString(ctx, argv[0]);
    if(!path) return JS_EXCEPTION;

    RadioStream* rs = js_radio_get_instance();
    if(!rs) {
        JS_FreeCString(ctx, path);
        return JS_ThrowInternalError(ctx, "radio: out of memory");
    }

    bool ok = radio_stream_play_file(rs, path);
    JS_FreeCString(ctx, path);
    return JS_NewBool(ctx, ok);
}

static JSValue js_radio_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(ctx);
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    if(radio_instance) {
        radio_stream_stop(radio_instance);
    }
    return JS_UNDEFINED;
}

static JSValue js_radio_is_playing(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    if(!radio_instance) return JS_NewBool(ctx, false);
    return JS_NewBool(ctx, radio_stream_is_playing(radio_instance));
}

static JSValue js_radio_get_title(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    if(!radio_instance) return JS_NewString(ctx, "");
    const char* title = radio_stream_get_title(radio_instance);
    return JS_NewString(ctx, title ? title : "");
}

static JSValue js_radio_get_stream_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    if(!radio_instance) return JS_NewString(ctx, "");
    const char* name = radio_stream_get_stream_name(radio_instance);
    return JS_NewString(ctx, name ? name : "");
}

static JSValue js_radio_set_volume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "setVolume expects a number from 0 to 1");
    }

    double volume = 0.0;
    if(JS_ToFloat64(ctx, &volume, argv[0])) return JS_EXCEPTION;
    if(volume < 0.0) volume = 0.0;
    if(volume > 1.0) volume = 1.0;

    RadioStream* rs = js_radio_get_instance();
    if(!rs) {
        return JS_ThrowInternalError(ctx, "radio: out of memory");
    }

    radio_stream_set_volume(rs, (float)volume);
    return JS_UNDEFINED;
}

JSValue js_module_radio_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "play", JS_NewCFunction(ctx, js_radio_play, "play", 1));
    JS_SetPropertyStr(ctx, obj, "playFile", JS_NewCFunction(ctx, js_radio_play_file, "playFile", 1));
    JS_SetPropertyStr(ctx, obj, "stop", JS_NewCFunction(ctx, js_radio_stop, "stop", 0));
    JS_SetPropertyStr(ctx, obj, "isPlaying", JS_NewCFunction(ctx, js_radio_is_playing, "isPlaying", 0));
    JS_SetPropertyStr(ctx, obj, "setVolume", JS_NewCFunction(ctx, js_radio_set_volume, "setVolume", 1));
    JS_SetPropertyStr(ctx, obj, "getTitle", JS_NewCFunction(ctx, js_radio_get_title, "getTitle", 0));
    JS_SetPropertyStr(
        ctx, obj, "getStreamName", JS_NewCFunction(ctx, js_radio_get_stream_name, "getStreamName", 0));
    return obj;
}

void js_radio_cleanup(JsRunner* runner) {
    UNUSED(runner);
    if(radio_instance) {
        radio_stream_free(radio_instance);
        radio_instance = NULL;
    }
}

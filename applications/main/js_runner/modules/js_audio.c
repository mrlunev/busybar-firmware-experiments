#include "../js_modules.h"

#include <audio/audio.h>
#include <stdatomic.h>
#include <storage/storage.h>

#define JS_AUDIO_END_CALLBACK_ID 1

typedef struct {
    JsRunner* runner;
    Audio* audio;
    FuriPubSubSubscription* subscription;
    FuriMessageQueue* event_queue;
    JSValue end_callback;
    _Atomic bool end_subscribed;
    bool enabled;
} JsAudioState;

static JsAudioState* js_audio_get_state(JsRunner* runner) {
    return runner ? runner->audio_state : NULL;
}

static bool js_audio_resolve_path(
    JsRunner* runner,
    const char* rel_path,
    char* out,
    size_t out_size) {
    if(!rel_path || rel_path[0] == '\0' || strstr(rel_path, "..")) {
        return false;
    }

    const int written =
        snprintf(out, out_size, "%s/%s/%s", EXT_PATH("apps"), runner->app_id, rel_path);
    return written >= 0 && (size_t)written < out_size;
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
    const bool exists = storage_file_exists(storage, full_path);
    furi_record_close(RECORD_STORAGE);
    if(!exists) {
        return JS_ThrowReferenceError(ctx, "audio file not found");
    }

    JsAudioState* state = js_audio_get_state(runner);
    if(!state || !state->audio) {
        return JS_ThrowInternalError(ctx, "audio service is unavailable");
    }
    return JS_NewBool(ctx, audio_play_file(state->audio, full_path));
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
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsAudioState* state = js_audio_get_state(runner);
    return JS_NewBool(ctx, state && state->audio && audio_stop(state->audio));
}

static JSValue js_audio_set_volume(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "setVolume expects a number from 0 to 1");
    }

    double value = 0.0;
    if(JS_ToFloat64(ctx, &value, argv[0])) return JS_EXCEPTION;
    if(value < 0.0 || value > 1.0) {
        return JS_ThrowRangeError(ctx, "volume must be in range 0..1");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsAudioState* state = js_audio_get_state(runner);
    if(!state || !state->audio) {
        return JS_ThrowInternalError(ctx, "audio service is unavailable");
    }
    audio_set_volume(state->audio, (float)value);
    return JS_UNDEFINED;
}

static JSValue js_audio_get_volume(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsAudioState* state = js_audio_get_state(runner);
    if(!state || !state->audio) {
        return JS_ThrowInternalError(ctx, "audio service is unavailable");
    }
    return JS_NewFloat64(ctx, audio_get_volume(state->audio));
}

static JSValue js_audio_on(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "on expects event name and callback");
    }

    const char* event_name = JS_ToCString(ctx, argv[0]);
    if(!event_name) return JS_EXCEPTION;
    const bool is_end = strcmp(event_name, "end") == 0;
    JS_FreeCString(ctx, event_name);
    if(!is_end) {
        return JS_ThrowTypeError(ctx, "unsupported audio event");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsAudioState* state = js_audio_get_state(runner);
    if(!state) return JS_ThrowInternalError(ctx, "audio state is unavailable");
    if(!JS_IsUndefined(state->end_callback)) {
        return JS_ThrowRangeError(ctx, "audio end callback is already registered");
    }

    state->end_callback = JS_DupValue(ctx, argv[1]);
    furi_message_queue_reset(state->event_queue);
    atomic_store_explicit(&state->end_subscribed, true, memory_order_release);
    return JS_NewInt32(ctx, JS_AUDIO_END_CALLBACK_ID);
}

static JSValue js_audio_off(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "off expects a callback id");
    }

    int32_t callback_id = 0;
    if(JS_ToInt32(ctx, &callback_id, argv[0])) return JS_EXCEPTION;

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsAudioState* state = js_audio_get_state(runner);
    if(!state || callback_id != JS_AUDIO_END_CALLBACK_ID ||
       JS_IsUndefined(state->end_callback)) {
        return JS_NewBool(ctx, false);
    }

    atomic_store_explicit(&state->end_subscribed, false, memory_order_release);
    furi_message_queue_reset(state->event_queue);
    JS_FreeValue(ctx, state->end_callback);
    state->end_callback = JS_UNDEFINED;
    return JS_NewBool(ctx, true);
}

static void js_audio_event_queue_callback(FuriEventLoopObject* object, void* context) {
    JsAudioState* state = context;
    if(!state || object != state->event_queue) return;

    uint8_t event = 0;
    if(furi_message_queue_get(state->event_queue, &event, 0) != FuriStatusOk ||
       event != AudioEventPlayEnd ||
       !atomic_load_explicit(&state->end_subscribed, memory_order_acquire) ||
       !state->runner->running || JS_IsUndefined(state->end_callback)) {
        return;
    }

    JSValue callback = JS_DupValue(state->runner->ctx, state->end_callback);
    js_runner_call_callback(state->runner, callback, 0, NULL, "audio end callback");
    JS_FreeValue(state->runner->ctx, callback);
}

static void js_audio_event_callback(const void* message, void* context) {
    const AudioEvent* event = message;
    JsAudioState* state = context;
    if(!state || !event || event->type != AudioEventPlayEnd ||
       !atomic_load_explicit(&state->end_subscribed, memory_order_acquire)) {
        return;
    }

    const uint8_t event_type = AudioEventPlayEnd;
    if(furi_message_queue_put(state->event_queue, &event_type, 0) != FuriStatusOk) {
        FURI_LOG_W("JsAudio", "Audio event queue full");
    }
}

bool js_audio_has_subscribers(JsRunner* runner) {
    JsAudioState* state = js_audio_get_state(runner);
    return state && atomic_load_explicit(&state->end_subscribed, memory_order_acquire);
}

JSValue js_module_audio_create(JSContext* ctx, JsRunner* runner) {
    JsAudioState* state = calloc(1, sizeof(JsAudioState));
    if(!state) return JS_ThrowInternalError(ctx, "out of memory");

    state->runner = runner;
    state->end_callback = JS_UNDEFINED;
    state->audio = furi_record_open(RECORD_AUDIO);
    if(!state->audio) {
        free(state);
        return JS_ThrowInternalError(ctx, "audio service is unavailable");
    }

    state->event_queue = furi_message_queue_alloc(4, sizeof(uint8_t));
    if(!state->event_queue) {
        furi_record_close(RECORD_AUDIO);
        free(state);
        return JS_ThrowInternalError(ctx, "failed to allocate audio event queue");
    }
    furi_event_loop_subscribe_message_queue(
        runner->event_loop,
        state->event_queue,
        FuriEventLoopEventIn,
        js_audio_event_queue_callback,
        state);

    audio_enable(state->audio);
    state->enabled = true;
    state->subscription =
        furi_pubsub_subscribe(audio_get_pubsub(state->audio), js_audio_event_callback, state);
    runner->audio_state = state;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "play", JS_NewCFunction(ctx, js_audio_play, "play", 1));
    JS_SetPropertyStr(ctx, obj, "beep", JS_NewCFunction(ctx, js_audio_beep, "beep", 1));
    JS_SetPropertyStr(ctx, obj, "stop", JS_NewCFunction(ctx, js_audio_stop, "stop", 0));
    JS_SetPropertyStr(
        ctx, obj, "setVolume", JS_NewCFunction(ctx, js_audio_set_volume, "setVolume", 1));
    JS_SetPropertyStr(
        ctx, obj, "getVolume", JS_NewCFunction(ctx, js_audio_get_volume, "getVolume", 0));
    JS_SetPropertyStr(ctx, obj, "on", JS_NewCFunction(ctx, js_audio_on, "on", 2));
    JS_SetPropertyStr(ctx, obj, "off", JS_NewCFunction(ctx, js_audio_off, "off", 1));
    return obj;
}

void js_audio_cleanup(JsRunner* runner) {
    JsAudioState* state = js_audio_get_state(runner);
    if(!state) return;

    atomic_store_explicit(&state->end_subscribed, false, memory_order_release);
    if(state->subscription) {
        furi_pubsub_unsubscribe(audio_get_pubsub(state->audio), state->subscription);
    }
    if(state->event_queue) {
        furi_event_loop_unsubscribe(runner->event_loop, state->event_queue);
        furi_message_queue_free(state->event_queue);
    }
    if(runner->ctx && !JS_IsUndefined(state->end_callback)) {
        JS_FreeValue(runner->ctx, state->end_callback);
    }
    if(state->enabled) {
        audio_stop(state->audio);
        audio_disable(state->audio);
    }
    if(state->audio) {
        furi_record_close(RECORD_AUDIO);
    }

    free(state);
    runner->audio_state = NULL;
}

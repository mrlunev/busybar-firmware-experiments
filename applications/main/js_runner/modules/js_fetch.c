#include "../js_modules.h"

#include <fetch/fetch.h>
#include <stdatomic.h>

#define JS_FETCH_MAX_BUFFER        (64 * 1024)
#define JS_FETCH_THREAD_STACK_SIZE (12 * 1024)
#define JS_FETCH_POLL_INTERVAL_MS  50

typedef struct {
    JsRunner* runner;
    Fetch* fetch;
    FuriThread* thread;
    FuriEventLoopTimer* poll_timer;
    JSValue callback;
    char* url;
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    char* error_message;
    _Atomic bool done;
    _Atomic bool active;
    bool timer_started;
    bool had_error;
} JsFetchRequest;

typedef struct {
    JsFetchRequest* request;
} JsFetchState;

static JsFetchState* js_fetch_get_state(JsRunner* runner) {
    if(!runner->fetch_state) {
        runner->fetch_state = calloc(1, sizeof(JsFetchState));
    }
    return runner->fetch_state;
}

static void js_fetch_set_error(JsFetchRequest* request, const char* message) {
    if(!request || request->had_error) return;
    request->had_error = true;
    request->error_message = strdup(message ? message : "fetch error");
}

static void js_fetch_rx_callback(const void* data, size_t data_size, void* context) {
    JsFetchRequest* request = context;
    if(!request || !request->active || request->had_error || data_size == 0) return;

    if(data_size > JS_FETCH_MAX_BUFFER - request->buffer_size) {
        js_fetch_set_error(request, "fetch response exceeds 64 KB");
        fetch_stop(request->fetch);
        return;
    }

    const size_t required = request->buffer_size + data_size;
    if(required > request->buffer_capacity) {
        size_t capacity = request->buffer_capacity ? request->buffer_capacity * 2 : 4096;
        while(capacity < required) capacity *= 2;

        uint8_t* buffer = realloc(request->buffer, capacity);
        if(!buffer) {
            js_fetch_set_error(request, "out of memory while buffering fetch response");
            fetch_stop(request->fetch);
            return;
        }

        request->buffer = buffer;
        request->buffer_capacity = capacity;
    }

    memcpy(request->buffer + request->buffer_size, data, data_size);
    request->buffer_size += data_size;
}

static void js_fetch_error_callback(const char* error, void* context) {
    js_fetch_set_error(context, error);
}

static int32_t js_fetch_thread(void* context) {
    JsFetchRequest* request = context;
    const FetchRequest fetch_request = {
        .url = request->url,
    };

    const FetchStatus status = fetch_run(request->fetch, &fetch_request);
    if(status != FetchStatusOk && !request->had_error && request->active) {
        js_fetch_set_error(
            request, status == FetchStatusAborted ? "fetch aborted" : "fetch failed");
    }

    atomic_store_explicit(&request->done, true, memory_order_release);
    return 0;
}

static void js_fetch_request_free(JsFetchRequest* request, bool stop) {
    if(!request) return;

    atomic_store(&request->active, false);

    if(request->poll_timer) {
        if(request->timer_started) {
            furi_event_loop_timer_stop(request->poll_timer);
        }
        furi_event_loop_timer_free(request->poll_timer);
    }

    if(request->thread) {
        if(stop && !atomic_load_explicit(&request->done, memory_order_acquire)) {
            fetch_stop(request->fetch);
        }
        furi_thread_join(request->thread);
        furi_thread_free(request->thread);
    }

    if(request->fetch) fetch_free(request->fetch);
    if(request->runner && request->runner->ctx && !JS_IsUndefined(request->callback)) {
        JS_FreeValue(request->runner->ctx, request->callback);
    }

    free(request->url);
    free(request->buffer);
    free(request->error_message);
    free(request);
}

static void js_fetch_poll(void* context) {
    JsFetchRequest* request = context;
    if(!request || !atomic_load_explicit(&request->done, memory_order_acquire)) return;

    JsRunner* runner = request->runner;
    JsFetchState* state = runner->fetch_state;
    if(state) state->request = NULL;

    if(request->timer_started) {
        furi_event_loop_timer_stop(request->poll_timer);
        request->timer_started = false;
    }

    JSValue args[2];
    int argc;
    if(request->had_error) {
        args[0] = JS_UNDEFINED;
        args[1] = JS_NewString(
            runner->ctx, request->error_message ? request->error_message : "fetch error");
        argc = 2;
    } else {
        args[0] = JS_NewStringLen(
            runner->ctx,
            request->buffer ? (const char*)request->buffer : "",
            request->buffer_size);
        args[1] = JS_UNDEFINED;
        argc = 1;
    }

    js_runner_call_callback(runner, request->callback, argc, args, "fetch callback");
    JS_FreeValue(runner->ctx, args[0]);
    if(argc == 2) JS_FreeValue(runner->ctx, args[1]);

    js_fetch_request_free(request, false);
}

static void js_fetch_start_poll(void* context) {
    JsFetchRequest* request = context;
    if(!request || !request->active || request->timer_started) return;
    furi_event_loop_timer_start(
        request->poll_timer, furi_ms_to_ticks(JS_FETCH_POLL_INTERVAL_MS));
    request->timer_started = true;
}

static JSValue js_fetch_get(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);

    if(argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "get expects url and callback");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsFetchState* state = js_fetch_get_state(runner);
    if(!state) return JS_ThrowInternalError(ctx, "out of memory");
    if(state->request) return JS_ThrowRangeError(ctx, "a fetch request is already running");

    const char* url = JS_ToCString(ctx, argv[0]);
    if(!url) return JS_EXCEPTION;

    JsFetchRequest* request = calloc(1, sizeof(JsFetchRequest));
    if(!request) {
        JS_FreeCString(ctx, url);
        return JS_ThrowInternalError(ctx, "out of memory");
    }

    request->runner = runner;
    request->callback = JS_DupValue(ctx, argv[1]);
    request->url = strdup(url);
    request->fetch = fetch_alloc();
    JS_FreeCString(ctx, url);

    if(!request->url || !request->fetch) {
        js_fetch_request_free(request, false);
        return JS_ThrowInternalError(ctx, "out of memory");
    }

    fetch_set_callback_context(request->fetch, request);
    fetch_set_rx_data_callback(request->fetch, js_fetch_rx_callback);
    fetch_set_header_callback(request->fetch, NULL);
    fetch_set_progress_callback(request->fetch, NULL);
    fetch_set_error_callback(request->fetch, js_fetch_error_callback);

    request->poll_timer = furi_event_loop_timer_alloc(
        runner->event_loop, js_fetch_poll, FuriEventLoopTimerTypePeriodic, request);
    request->thread = furi_thread_alloc_ex(
        "JsFetch", JS_FETCH_THREAD_STACK_SIZE, js_fetch_thread, request);
    if(!request->poll_timer || !request->thread) {
        js_fetch_request_free(request, false);
        return JS_ThrowInternalError(ctx, "failed to allocate fetch worker");
    }

    atomic_store(&request->active, true);
    state->request = request;
    furi_thread_start(request->thread);

    if(runner->event_loop_active) {
        js_fetch_start_poll(request);
    } else {
        furi_event_loop_pend_callback(runner->event_loop, js_fetch_start_poll, request);
    }

    return JS_NewInt32(ctx, 0);
}

void js_fetch_cleanup(JsRunner* runner) {
    if(!runner || !runner->fetch_state) return;

    JsFetchState* state = runner->fetch_state;
    if(state->request) {
        JsFetchRequest* request = state->request;
        state->request = NULL;
        js_fetch_request_free(request, true);
    }

    free(state);
    runner->fetch_state = NULL;
}

JSValue js_module_fetch_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue module = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, module, "get", JS_NewCFunction(ctx, js_fetch_get, "get", 2));
    return module;
}

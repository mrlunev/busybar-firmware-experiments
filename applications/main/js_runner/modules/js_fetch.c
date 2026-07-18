#include "../js_modules.h"

#include <ctype.h>
#include <fetch/fetch.h>
#include <stdatomic.h>

#define JS_FETCH_MAX_HEADERS       FETCH_HEADERS_COUNT_MAX
#define JS_FETCH_MAX_HEADER_BYTES  (16 * 1024u)
#define JS_FETCH_THREAD_STACK_SIZE (12 * 1024)
#define JS_FETCH_POLL_INTERVAL_MS  50

typedef struct {
    JsRunner* runner;
    Fetch* fetch;
    FuriThread* thread;
    FuriEventLoopTimer* poll_timer;
    JSValue callback;
    JSValue progress_callback;
    int32_t handle;
    char* url;
    char* method;
    char* request_headers[JS_FETCH_MAX_HEADERS];
    uint32_t request_header_count;
    uint8_t* request_body;
    size_t request_body_size;
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    char* response_headers;
    size_t response_headers_size;
    char* error_message;
    _Atomic size_t progress_total;
    _Atomic size_t progress_received;
    _Atomic uint32_t progress_speed;
    _Atomic bool progress_dirty;
    _Atomic bool done;
    _Atomic bool active;
    _Atomic bool cancel_requested;
    bool timer_started;
    bool had_error;
    bool legacy_callback;
} JsFetchRequest;

typedef struct {
    JsFetchRequest* request;
    int32_t next_handle;
} JsFetchState;

static JsFetchState* js_fetch_get_state(JsRunner* runner) {
    if(!runner->fetch_state) {
        JsFetchState* state = calloc(1, sizeof(JsFetchState));
        if(!state) return NULL;
        state->next_handle = 1;
        runner->fetch_state = state;
    }
    return runner->fetch_state;
}

static void js_fetch_set_error(JsFetchRequest* request, const char* message) {
    if(!request || request->had_error) return;
    request->had_error = true;
    request->error_message = strdup(message ? message : "fetch error");
}

static bool js_fetch_append_bytes(
    uint8_t** buffer,
    size_t* size,
    size_t* capacity,
    const void* data,
    size_t data_size,
    size_t max_size) {
    if(data_size > max_size - *size) return false;

    const size_t required = *size + data_size;
    if(required > *capacity) {
        size_t next_capacity = *capacity ? *capacity * 2 : 4096;
        while(next_capacity < required) {
            if(next_capacity > max_size / 2) {
                next_capacity = max_size;
                break;
            }
            next_capacity *= 2;
        }

        uint8_t* next_buffer = realloc(*buffer, next_capacity);
        if(!next_buffer) return false;
        *buffer = next_buffer;
        *capacity = next_capacity;
    }

    memcpy(*buffer + *size, data, data_size);
    *size += data_size;
    return true;
}

static void js_fetch_rx_callback(const void* data, size_t data_size, void* context) {
    JsFetchRequest* request = context;
    if(!request || !request->active || request->had_error || data_size == 0) return;

    if(!js_fetch_append_bytes(
           &request->buffer,
           &request->buffer_size,
           &request->buffer_capacity,
           data,
           data_size,
           JS_RUNNER_FETCH_MAX_BYTES)) {
        js_fetch_set_error(request, "fetch response exceeds 64 KB or cannot be buffered");
        fetch_stop(request->fetch);
    }
}

static void js_fetch_header_callback(const void* data, size_t data_size, void* context) {
    JsFetchRequest* request = context;
    if(!request || !request->active || request->had_error || data_size == 0) return;
    if(data_size > JS_FETCH_MAX_HEADER_BYTES) {
        js_fetch_set_error(request, "fetch response headers exceed 16 KB");
        fetch_stop(request->fetch);
        return;
    }

    char* headers = malloc(data_size + 1);
    if(!headers) {
        js_fetch_set_error(request, "out of memory while buffering fetch headers");
        fetch_stop(request->fetch);
        return;
    }

    memcpy(headers, data, data_size);
    headers[data_size] = '\0';
    free(request->response_headers);
    request->response_headers = headers;
    request->response_headers_size = data_size;
}

static void js_fetch_progress_callback(const FetchProgress* progress, void* context) {
    JsFetchRequest* request = context;
    if(!request || !request->active || !progress) return;

    atomic_store_explicit(
        &request->progress_total, progress->total_download_size, memory_order_relaxed);
    atomic_store_explicit(
        &request->progress_received, progress->received_download_size, memory_order_relaxed);
    atomic_store_explicit(
        &request->progress_speed, progress->speed_bytes_per_sec, memory_order_relaxed);
    atomic_store_explicit(&request->progress_dirty, true, memory_order_release);
}

static void js_fetch_error_callback(const char* error, void* context) {
    js_fetch_set_error(context, error);
}

static int32_t js_fetch_thread(void* context) {
    JsFetchRequest* request = context;
    FetchRequest fetch_request = {
        .url = request->url,
        .method = request->method,
        .body =
            {
                .data = request->request_body,
                .length = (uint32_t)request->request_body_size,
            },
    };

    fetch_request.headers.count = request->request_header_count;
    for(uint32_t i = 0; i < request->request_header_count; i++) {
        fetch_request.headers.data[i] = request->request_headers[i];
    }

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

    atomic_store_explicit(&request->active, false, memory_order_relaxed);

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
    if(request->runner && request->runner->ctx) {
        if(!JS_IsUndefined(request->callback)) {
            JS_FreeValue(request->runner->ctx, request->callback);
        }
        if(!JS_IsUndefined(request->progress_callback)) {
            JS_FreeValue(request->runner->ctx, request->progress_callback);
        }
    }

    for(uint32_t i = 0; i < request->request_header_count; i++) {
        free(request->request_headers[i]);
    }
    free(request->url);
    free(request->method);
    free(request->request_body);
    free(request->buffer);
    free(request->response_headers);
    free(request->error_message);
    free(request);
}

static int32_t js_fetch_parse_status(const char* headers) {
    if(!headers || strncmp(headers, "HTTP/", 5) != 0) return 0;
    const char* separator = strchr(headers, ' ');
    if(!separator) return 0;
    const long status = strtol(separator + 1, NULL, 10);
    return status >= 100 && status <= 999 ? (int32_t)status : 0;
}

static JSValue js_fetch_create_headers_object(JSContext* ctx, const char* raw_headers) {
    JSValue headers = JS_NewObject(ctx);
    if(!raw_headers) return headers;

    char* copy = strdup(raw_headers);
    if(!copy) return headers;

    char* save = NULL;
    char* line = strtok_r(copy, "\r\n", &save);
    while((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char* colon = strchr(line, ':');
        if(!colon) continue;
        *colon = '\0';

        char* value = colon + 1;
        while(*value == ' ' || *value == '\t') value++;
        char* value_end = value + strlen(value);
        while(value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            *--value_end = '\0';
        }

        for(char* p = line; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        if(line[0] != '\0') {
            JS_SetPropertyStr(ctx, headers, line, JS_NewString(ctx, value));
        }
    }

    free(copy);
    return headers;
}

static JSValue js_fetch_create_response(JSContext* ctx, JsFetchRequest* request) {
    const int32_t status = js_fetch_parse_status(request->response_headers);
    JSValue response = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, response, "status", JS_NewInt32(ctx, status));
    JS_SetPropertyStr(
        ctx, response, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
    JS_SetPropertyStr(
        ctx,
        response,
        "headers",
        js_fetch_create_headers_object(ctx, request->response_headers));
    JS_SetPropertyStr(
        ctx,
        response,
        "rawHeaders",
        JS_NewStringLen(
            ctx,
            request->response_headers ? request->response_headers : "",
            request->response_headers_size));
    JS_SetPropertyStr(
        ctx,
        response,
        "body",
        JS_NewStringLen(
            ctx,
            request->buffer ? (const char*)request->buffer : "",
            request->buffer_size));
    return response;
}

static bool js_fetch_dispatch_progress(JsFetchRequest* request) {
    if(JS_IsUndefined(request->progress_callback) ||
       !atomic_exchange_explicit(&request->progress_dirty, false, memory_order_acq_rel)) {
        return true;
    }

    JSContext* ctx = request->runner->ctx;
    JSValue progress = JS_NewObject(ctx);
    JS_SetPropertyStr(
        ctx,
        progress,
        "totalBytes",
        JS_NewInt32(
            ctx, atomic_load_explicit(&request->progress_total, memory_order_relaxed)));
    JS_SetPropertyStr(
        ctx,
        progress,
        "receivedBytes",
        JS_NewInt32(
            ctx, atomic_load_explicit(&request->progress_received, memory_order_relaxed)));
    JS_SetPropertyStr(
        ctx,
        progress,
        "bytesPerSecond",
        JS_NewInt32(
            ctx, atomic_load_explicit(&request->progress_speed, memory_order_relaxed)));

    const bool running = js_runner_call_callback(
        request->runner, request->progress_callback, 1, &progress, "fetch progress callback");
    JS_FreeValue(ctx, progress);
    return running;
}

static void js_fetch_poll(void* context) {
    JsFetchRequest* request = context;
    if(!request || !request->active) return;
    if(atomic_load_explicit(&request->cancel_requested, memory_order_acquire) &&
       !atomic_load_explicit(&request->done, memory_order_acquire)) {
        fetch_stop(request->fetch);
    }
    if(!js_fetch_dispatch_progress(request)) return;
    if(!atomic_load_explicit(&request->done, memory_order_acquire)) return;

    JsRunner* runner = request->runner;
    JsFetchState* state = runner->fetch_state;
    if(state && state->request == request) state->request = NULL;

    if(request->timer_started) {
        furi_event_loop_timer_stop(request->poll_timer);
        request->timer_started = false;
    }

    JSValue args[2];
    int argc = 2;
    if(request->had_error) {
        args[0] = JS_UNDEFINED;
        args[1] = JS_NewString(
            runner->ctx, request->error_message ? request->error_message : "fetch error");
    } else if(request->legacy_callback) {
        args[0] = JS_NewStringLen(
            runner->ctx,
            request->buffer ? (const char*)request->buffer : "",
            request->buffer_size);
        args[1] = JS_UNDEFINED;
        argc = 1;
    } else {
        args[0] = js_fetch_create_response(runner->ctx, request);
        args[1] = JS_UNDEFINED;
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

static JSValue js_fetch_start(
    JSContext* ctx,
    JsFetchRequest* request,
    JSValueConst callback,
    JSValueConst progress_callback,
    bool legacy_callback) {
    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsFetchState* state = js_fetch_get_state(runner);
    if(!state) {
        js_fetch_request_free(request, false);
        return JS_ThrowInternalError(ctx, "out of memory");
    }
    if(state->request) {
        js_fetch_request_free(request, false);
        return JS_ThrowRangeError(ctx, "a fetch request is already running");
    }

    request->runner = runner;
    request->callback = JS_DupValue(ctx, callback);
    request->progress_callback = JS_IsFunction(ctx, progress_callback) ?
                                     JS_DupValue(ctx, progress_callback) :
                                     JS_UNDEFINED;
    request->legacy_callback = legacy_callback;
    request->handle = state->next_handle++;
    if(state->next_handle <= 0) state->next_handle = 1;
    request->fetch = fetch_alloc();
    if(!request->fetch) {
        js_fetch_request_free(request, false);
        return JS_ThrowInternalError(ctx, "out of memory");
    }

    fetch_set_callback_context(request->fetch, request);
    fetch_set_rx_data_callback(request->fetch, js_fetch_rx_callback);
    fetch_set_header_callback(request->fetch, js_fetch_header_callback);
    fetch_set_progress_callback(request->fetch, js_fetch_progress_callback);
    fetch_set_error_callback(request->fetch, js_fetch_error_callback);

    request->poll_timer = furi_event_loop_timer_alloc(
        runner->event_loop, js_fetch_poll, FuriEventLoopTimerTypePeriodic, request);
    request->thread = furi_thread_alloc_ex(
        "JsFetch", JS_FETCH_THREAD_STACK_SIZE, js_fetch_thread, request);
    if(!request->poll_timer || !request->thread) {
        js_fetch_request_free(request, false);
        return JS_ThrowInternalError(ctx, "failed to allocate fetch worker");
    }

    atomic_store_explicit(&request->active, true, memory_order_relaxed);
    state->request = request;
    furi_thread_start(request->thread);

    if(runner->event_loop_active) {
        js_fetch_start_poll(request);
    } else {
        furi_event_loop_pend_callback(runner->event_loop, js_fetch_start_poll, request);
    }

    return JS_NewInt32(ctx, request->handle);
}

static bool js_fetch_header_is_safe(const char* name, const char* value) {
    if(!name || name[0] == '\0' || !value) return false;
    return !strchr(name, '\r') && !strchr(name, '\n') && !strchr(name, ':') &&
           !strchr(value, '\r') && !strchr(value, '\n');
}

static bool js_fetch_request_line_is_safe(const char* value, bool allow_spaces) {
    if(!value || value[0] == '\0' || strchr(value, '\r') || strchr(value, '\n')) return false;
    return allow_spaces || (!strchr(value, ' ') && !strchr(value, '\t'));
}

static bool js_fetch_parse_request_headers(
    JSContext* ctx,
    JSValueConst headers,
    JsFetchRequest* request) {
    if(JS_IsUndefined(headers) || JS_IsNull(headers)) return true;
    if(!JS_IsObject(headers)) return false;

    JSValue keys = bsb_jerry_object_keys(headers);
    if(JS_IsException(keys)) return false;

    uint32_t count = 0;
    if(!bsb_jerry_array_length(keys, &count) || count > JS_FETCH_MAX_HEADERS) {
        JS_FreeValue(ctx, keys);
        return false;
    }

    for(uint32_t i = 0; i < count; i++) {
        JSValue key_value = JS_GetPropertyUint32(ctx, keys, i);
        const char* key = JS_ToCString(ctx, key_value);
        JS_FreeValue(ctx, key_value);
        if(!key) {
            JS_FreeValue(ctx, keys);
            return false;
        }

        JSValue value_value = JS_GetPropertyStr(ctx, headers, key);
        const char* value = JS_ToCString(ctx, value_value);
        JS_FreeValue(ctx, value_value);
        if(!value || !js_fetch_header_is_safe(key, value)) {
            JS_FreeCString(ctx, key);
            JS_FreeCString(ctx, value);
            JS_FreeValue(ctx, keys);
            return false;
        }

        const size_t line_size = strlen(key) + strlen(value) + 3;
        char* line = malloc(line_size);
        if(line) snprintf(line, line_size, "%s: %s", key, value);
        JS_FreeCString(ctx, key);
        JS_FreeCString(ctx, value);
        if(!line) {
            JS_FreeValue(ctx, keys);
            return false;
        }

        request->request_headers[request->request_header_count++] = line;
    }

    JS_FreeValue(ctx, keys);
    return true;
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

    const char* url = JS_ToCString(ctx, argv[0]);
    if(!url) return JS_EXCEPTION;

    JsFetchRequest* request = calloc(1, sizeof(JsFetchRequest));
    if(request) {
        request->callback = JS_UNDEFINED;
        request->progress_callback = JS_UNDEFINED;
        request->url = strdup(url);
        request->method = strdup("GET");
    }
    JS_FreeCString(ctx, url);
    if(!request || !request->url || !request->method) {
        js_fetch_request_free(request, false);
        return JS_ThrowInternalError(ctx, "out of memory");
    }

    return js_fetch_start(ctx, request, argv[1], JS_UNDEFINED, true);
}

static JSValue js_fetch_request(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2 || !JS_IsObject(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "request expects options and callback");
    }

    JsFetchRequest* request = calloc(1, sizeof(JsFetchRequest));
    if(!request) return JS_ThrowInternalError(ctx, "out of memory");
    request->callback = JS_UNDEFINED;
    request->progress_callback = JS_UNDEFINED;

    JSValue url_value = JS_GetPropertyStr(ctx, argv[0], "url");
    if(!JS_IsString(url_value)) {
        JS_FreeValue(ctx, url_value);
        js_fetch_request_free(request, false);
        return JS_ThrowTypeError(ctx, "request.url must be a string");
    }
    const char* url = JS_ToCString(ctx, url_value);
    JS_FreeValue(ctx, url_value);
    request->url = url ? strdup(url) : NULL;
    JS_FreeCString(ctx, url);

    JSValue method_value = JS_GetPropertyStr(ctx, argv[0], "method");
    if(JS_IsUndefined(method_value)) {
        request->method = strdup("GET");
    } else if(JS_IsString(method_value)) {
        const char* method = JS_ToCString(ctx, method_value);
        request->method = method ? strdup(method) : NULL;
        JS_FreeCString(ctx, method);
    }
    JS_FreeValue(ctx, method_value);

    JSValue body_value = JS_GetPropertyStr(ctx, argv[0], "body");
    const bool has_body = !JS_IsUndefined(body_value) && !JS_IsNull(body_value);
    bool body_ok = true;
    if(has_body) {
        if(!JS_IsString(body_value)) {
            JS_FreeValue(ctx, body_value);
            js_fetch_request_free(request, false);
            return JS_ThrowTypeError(ctx, "request.body must be a string");
        }
        size_t body_size = 0;
        const char* body = JS_ToCStringLen(ctx, &body_size, body_value);
        if(body) {
            request->request_body = body_size ? malloc(body_size) : NULL;
            if(body_size == 0 || request->request_body) {
                if(body_size > 0) {
                    memcpy(request->request_body, body, body_size);
                }
                request->request_body_size = body_size;
            } else {
                body_ok = false;
            }
        } else {
            body_ok = false;
        }
        JS_FreeCString(ctx, body);
    }
    JS_FreeValue(ctx, body_value);

    JSValue headers_value = JS_GetPropertyStr(ctx, argv[0], "headers");
    const bool headers_ok = js_fetch_parse_request_headers(ctx, headers_value, request);
    JS_FreeValue(ctx, headers_value);

    JSValue progress_value = JS_GetPropertyStr(ctx, argv[0], "onProgress");
    const bool progress_ok =
        JS_IsUndefined(progress_value) || JS_IsFunction(ctx, progress_value);

    if(!js_fetch_request_line_is_safe(request->url, true) ||
       !js_fetch_request_line_is_safe(request->method, false) || !body_ok || !headers_ok ||
       !progress_ok) {
        JS_FreeValue(ctx, progress_value);
        js_fetch_request_free(request, false);
        return JS_ThrowTypeError(ctx, "invalid fetch request options");
    }

    JSValue result =
        js_fetch_start(ctx, request, argv[1], progress_value, false);
    JS_FreeValue(ctx, progress_value);
    return result;
}

static JSValue js_fetch_cancel(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "cancel expects a request handle");
    }

    int32_t handle = 0;
    if(JS_ToInt32(ctx, &handle, argv[0])) return JS_EXCEPTION;

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsFetchState* state = runner->fetch_state;
    if(!state || !state->request || state->request->handle != handle ||
       atomic_load_explicit(&state->request->done, memory_order_acquire)) {
        return JS_NewBool(ctx, false);
    }

    atomic_store_explicit(&state->request->cancel_requested, true, memory_order_release);
    fetch_stop(state->request->fetch);
    return JS_NewBool(ctx, true);
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
    JS_SetPropertyStr(
        ctx, module, "request", JS_NewCFunction(ctx, js_fetch_request, "request", 2));
    JS_SetPropertyStr(
        ctx, module, "cancel", JS_NewCFunction(ctx, js_fetch_cancel, "cancel", 1));
    return module;
}

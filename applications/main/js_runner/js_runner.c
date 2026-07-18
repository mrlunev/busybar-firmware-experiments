#include "js_runner_i.h"
#include "js_modules.h"

#include <status_lights/status_lights.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

static void js_runner_free(JsRunner* runner);

void js_runner_log_exception(JsRunner* runner, const char* context) {
    JSValue exception = JS_GetException(runner->ctx);
    const char* msg = JS_ToCString(runner->ctx, exception);
    FURI_LOG_E(TAG, "%s: %s", context, msg ? msg : "unknown error");

    JSValue stack = JS_GetPropertyStr(runner->ctx, exception, "stack");
    if(!JS_IsUndefined(stack)) {
        const char* stack_str = JS_ToCString(runner->ctx, stack);
        if(stack_str && stack_str[0] != '\0') {
            FURI_LOG_E(TAG, "Stack: %s", stack_str);
        }
        JS_FreeCString(runner->ctx, stack_str);
    }
    JS_FreeValue(runner->ctx, stack);

    JS_FreeCString(runner->ctx, msg);
    JS_FreeValue(runner->ctx, exception);
}

static char* js_runner_read_file(const char* path, size_t* out_size) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    char* data = NULL;

    if(!file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "Cannot open %s", path);
    } else {
        size_t size = stream_size(stream);
        data = malloc(size + 1);
        if(data) {
            stream_rewind(stream);
            if(stream_read(stream, (uint8_t*)data, size) != size) {
                free(data);
                data = NULL;
            } else {
                data[size] = '\0';
                *out_size = size;
            }
        }
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return data;
}

JSValue js_runner_exec_file(JsRunner* runner, const char* path) {
    size_t size = 0;
    char* source = js_runner_read_file(path, &size);
    if(!source) {
        return JS_ThrowReferenceError(runner->ctx, "cannot read file: %s", path);
    }

    JSValue global = JS_GetGlobalObject(runner->ctx);
    JSValue prev_filename = JS_GetPropertyStr(runner->ctx, global, "__filename");
    JSValue prev_dirname = JS_GetPropertyStr(runner->ctx, global, "__dirname");
    JS_SetPropertyStr(
        runner->ctx, global, "__filename", JS_NewString(runner->ctx, path));

    FuriString* dir = furi_string_alloc();
    path_extract_dirname(path, dir);
    JS_SetPropertyStr(
        runner->ctx,
        global,
        "__dirname",
        JS_NewString(runner->ctx, furi_string_get_cstr(dir)));
    furi_string_free(dir);
    JSValue result = JS_Eval(runner->ctx, source, size, path, JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(runner->ctx, global, "__filename", prev_filename);
    JS_SetPropertyStr(runner->ctx, global, "__dirname", prev_dirname);
    JS_FreeValue(runner->ctx, global);
    free(source);
    return result;
}

static int js_interrupt_handler(JSRuntime* rt, void* opaque) {
    JsRunner* runner = opaque;
    UNUSED(rt);
    return runner && !runner->running;
}

static JSValue js_print(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    FuriString* out = furi_string_alloc();
    for(int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if(str) {
            if(i > 0) furi_string_push_back(out, ' ');
            furi_string_cat_str(out, str);
            JS_FreeCString(ctx, str);
        }
    }
    FURI_LOG_I(TAG, "%s", furi_string_get_cstr(out));
    furi_string_free(out);
    return JS_UNDEFINED;
}

static JSValue js_console_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_print(ctx, this_val, argc, argv);
}

static JSValue js_console_warn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    FuriString* out = furi_string_alloc();
    for(int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if(str) {
            if(i > 0) furi_string_push_back(out, ' ');
            furi_string_cat_str(out, str);
            JS_FreeCString(ctx, str);
        }
    }
    FURI_LOG_W(TAG, "%s", furi_string_get_cstr(out));
    furi_string_free(out);
    return JS_UNDEFINED;
}

static JSValue js_console_error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    FuriString* out = furi_string_alloc();
    for(int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if(str) {
            if(i > 0) furi_string_push_back(out, ' ');
            furi_string_cat_str(out, str);
            JS_FreeCString(ctx, str);
        }
    }
    FURI_LOG_E(TAG, "%s", furi_string_get_cstr(out));
    furi_string_free(out);
    return JS_UNDEFINED;
}

static JSValue js_str(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    FuriString* out = furi_string_alloc();
    for(int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if(str) {
            if(i > 0) furi_string_push_back(out, ' ');
            furi_string_cat_str(out, str);
            JS_FreeCString(ctx, str);
        }
    }
    JSValue result = JS_NewStringLen(ctx, furi_string_get_cstr(out), furi_string_size(out));
    furi_string_free(out);
    return result;
}

static JSValue js_delay(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1) {
        return JS_ThrowTypeError(ctx, "delay expects milliseconds");
    }

    int32_t ms;
    if(JS_ToInt32(ctx, &ms, argv[0])) {
        return JS_EXCEPTION;
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    uint32_t remaining_ms = (uint32_t)MAX(0, ms);

    while(runner->running && remaining_ms > 0) {
        uint32_t chunk = MIN(remaining_ms, 20u);
        furi_delay_ms(chunk);
        remaining_ms -= chunk;
    }

    return JS_UNDEFINED;
}

static JSValue js_require(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "require expects a string");
    }

    size_t len = 0;
    const char* name = JS_ToCStringLen(ctx, &len, argv[0]);
    if(!name) return JS_EXCEPTION;

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JSValue result = js_module_require(runner->modules, name, len);
    JS_FreeCString(ctx, name);
    return result;
}

static bool js_runner_has_async_work(JsRunner* runner) {
    if(!runner) return false;

    for(size_t i = 0; i < runner->input_cb_count; i++) {
        if(!JS_IsUndefined(runner->input_callbacks[i].callback)) {
            return true;
        }
    }

    if(runner->fetch_state) {
        return true;
    }

    if(js_audio_has_subscribers(runner)) {
        return true;
    }

    for(size_t i = 0; i < runner->timer_count; i++) {
        if(runner->timers[i] != NULL && !JS_IsUndefined(runner->timer_callbacks[i])) {
            return true;
        }
    }

    return false;
}

static void js_runner_start_pending_timers(void* context) {
    js_timer_start_pending(context);
}

static JSValue js_run_loop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    JsRunner* runner = JS_GetContextOpaque(ctx);
    if(runner->running) {
        if(!js_runner_has_async_work(runner) && !runner->display_state && !runner->widget_active) {
            FURI_LOG_D(TAG, "__runLoop skipped");
            return JS_UNDEFINED;
        }

        {
            size_t heap_total_kb, heap_used;
            bsb_jerry_heap_info(&heap_total_kb, &heap_used);
            FURI_LOG_I(
                TAG,
                "JS heap: %u / %u KB (%u%% used)",
                (unsigned)(heap_used / 1024),
                (unsigned)heap_total_kb,
                (unsigned)(heap_used * 100 / (heap_total_kb * 1024)));
        }

        runner->event_loop_active = true;
        furi_event_loop_pend_callback(
            runner->event_loop, js_runner_start_pending_timers, runner);
        FURI_LOG_D(TAG, "__runLoop begin");
        furi_event_loop_run(runner->event_loop);
        runner->event_loop_active = false;
        FURI_LOG_D(TAG, "__runLoop end");
    }
    return JS_UNDEFINED;
}

static bool js_runner_signal_callback(uint32_t signal, void* argument, void* context) {
    UNUSED(argument);
    JsRunner* runner = context;

    if(signal == FuriSignalExit) {
        runner->running = false;
        furi_event_loop_stop(runner->event_loop);
        return true;
    }

    return false;
}

bool js_runner_call_callback(
    JsRunner* runner,
    JSValue callback,
    int argc,
    JSValueConst* argv,
    const char* origin) {
    // FURI_LOG_D(TAG, "callback begin: %s", origin);
    runner->callback_depth++;
    JSValue result = JS_Call(runner->ctx, callback, JS_UNDEFINED, argc, argv);
    if(runner->callback_depth > 0) {
        runner->callback_depth--;
    }
    // FURI_LOG_D(TAG, "callback end: %s", origin);
    if(JS_IsException(result)) {
        runner->callback_exception_count++;
        strlcpy(
            runner->last_callback_exception,
            origin ? origin : "callback",
            sizeof(runner->last_callback_exception));
        js_runner_log_exception(runner, origin);
        runner->running = false;
        furi_event_loop_stop(runner->event_loop);
    }
    JS_FreeValue(runner->ctx, result);
    if(runner->callback_depth == 0 && runner->display_flush_pending) {
        js_display_flush(runner);
    }
    return runner->running;
}

void js_runner_invoke_callback(JsRunner* runner, JSValue callback, const char* origin) {
    js_runner_call_callback(runner, callback, 0, NULL, origin);
}

static void js_runner_input_queue_callback(FuriEventLoopObject* object, void* context) {
    UNUSED(object);
    JsRunner* runner = context;

    InputEvent event;
    while(furi_message_queue_get(runner->input_queue, &event, 0) == FuriStatusOk) {
        // FURI_LOG_D(TAG, "input event key=%u type=%u", event.key, event.type);
        if(runner->widget_active) {
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                js_settings_close(runner);
            }
            continue;
        }
        for(size_t i = 0; i < runner->input_cb_count; i++) {
            if(JS_IsUndefined(runner->input_callbacks[i].callback)) continue;
            if(runner->input_callbacks[i].key == event.key &&
               runner->input_callbacks[i].type == event.type) {
                js_runner_invoke_callback(
                    runner, runner->input_callbacks[i].callback, "input callback");
                if(!runner->running) return;
            }
        }
    }
}

static bool js_runner_enqueue_input(JsRunner* runner, const InputEvent* event) {
    if(furi_message_queue_put(runner->input_queue, event, 0) == FuriStatusOk) {
        return true;
    }

    const uint32_t dropped =
        atomic_fetch_add_explicit(&runner->input_drop_count, 1, memory_order_relaxed) + 1;
    if(dropped == 1 || (dropped & (dropped - 1)) == 0) {
        FURI_LOG_W(TAG, "Input queue full, dropped %lu event(s)", (unsigned long)dropped);
    }
    return false;
}

static void js_runner_input_pubsub_callback(const void* message, void* context) {
    JsRunner* runner = context;
    const InputEvent* event = message;
    if(runner->widget_active && event->key == InputKeyBack) {
        return;
    }
    js_runner_enqueue_input(runner, event);
}

static bool js_runner_gui_input_callback(const InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);
    JsRunner* runner = context;

    if(runner->widget_active && event->key == InputKeyBack && event->type == InputTypeShort) {
        js_runner_enqueue_input(runner, event);
        return true;
    }

    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        FURI_LOG_D(TAG, "gui back: stopping event loop");
        runner->running = false;
        furi_event_loop_stop(runner->event_loop);
        desktop_replace_current_app(runner->desktop, "apps_menu", runner->app_id);
        return true;
    }

    return false;
}

static JsRunner* js_runner_alloc(const char* script_path) {
    JsRunner* runner = calloc(1, sizeof(JsRunner));
    if(!runner) return NULL;

    for(size_t i = 0; i < JS_RUNNER_MAX_TIMERS; i++) {
        runner->timer_callbacks[i] = JS_UNDEFINED;
    }
    for(size_t i = 0; i < JS_RUNNER_MAX_INPUT_CBS; i++) {
        runner->input_callbacks[i].callback = JS_UNDEFINED;
    }

    runner->event_loop = furi_event_loop_alloc();
    runner->input_queue =
        furi_message_queue_alloc(JS_RUNNER_INPUT_QUEUE_SIZE, sizeof(InputEvent));
    if(!runner->event_loop || !runner->input_queue) {
        js_runner_free(runner);
        return NULL;
    }

    strlcpy(runner->script_path, script_path, sizeof(runner->script_path));

    FuriString* dir = furi_string_alloc();
    path_extract_dirname(script_path, dir);
    strlcpy(runner->script_dir, furi_string_get_cstr(dir), sizeof(runner->script_dir));
    FuriString* name = furi_string_alloc();
    path_extract_filename(dir, name, false);
    strlcpy(runner->app_id, furi_string_get_cstr(name), sizeof(runner->app_id));
    furi_string_free(name);
    furi_string_free(dir);

    if(runner->app_id[0] == '\0') {
        strlcpy(runner->app_id, "js_app", sizeof(runner->app_id));
    }

    runner->gui = furi_record_open(RECORD_GUI);
    runner->desktop = furi_record_open(RECORD_DESKTOP);

    with_gui(runner->gui, {
        runner->gui_layer = gui_get_layer(runner->gui, GuiLayerIdMain);
        gui_layer_add_input_callback(runner->gui_layer, js_runner_gui_input_callback, runner);
    });

    runner->input_pubsub = furi_record_open(RECORD_INPUT_EVENTS);
    runner->input_subscription =
        furi_pubsub_subscribe(runner->input_pubsub, js_runner_input_pubsub_callback, runner);

    furi_event_loop_subscribe_message_queue(
        runner->event_loop,
        runner->input_queue,
        FuriEventLoopEventIn,
        js_runner_input_queue_callback,
        runner);

    runner->running = true;
    return runner;
}

static void js_runner_free(JsRunner* runner) {
    if(!runner) return;

    StatusLights* sl = furi_record_open(RECORD_STATUS_LIGHTS);
    status_lights_run_preset(sl, StatusLightsPresetOff, (Color)COLOR_MAKE_RGB(0, 0, 0));
    furi_record_close(RECORD_STATUS_LIGHTS);

    for(size_t i = 0; i < runner->timer_count; i++) {
        if(runner->timers[i]) {
            if(runner->timer_started[i]) {
                furi_event_loop_timer_stop(runner->timers[i]);
            }
            furi_event_loop_timer_free(runner->timers[i]);
            runner->timers[i] = NULL;
        }
        if(runner->timer_contexts[i]) {
            free(runner->timer_contexts[i]);
            runner->timer_contexts[i] = NULL;
        }
        if(runner->ctx && !JS_IsUndefined(runner->timer_callbacks[i])) {
            JS_FreeValue(runner->ctx, runner->timer_callbacks[i]);
            runner->timer_callbacks[i] = JS_UNDEFINED;
        }
    }

    if(runner->ctx) {
        for(size_t i = 0; i < runner->input_cb_count; i++) {
            if(!JS_IsUndefined(runner->input_callbacks[i].callback)) {
                JS_FreeValue(runner->ctx, runner->input_callbacks[i].callback);
                runner->input_callbacks[i].callback = JS_UNDEFINED;
            }
        }
    }

    js_audio_cleanup(runner);
    js_radio_cleanup(runner);
    js_settings_cleanup(runner);
    js_config_cleanup(runner);
    js_fetch_cleanup(runner);
    js_display_cleanup(runner);

    if(runner->modules) {
        js_modules_destroy(runner->modules);
        runner->modules = NULL;
    }

    if(runner->ctx) {
        JS_FreeContext(runner->ctx);
        runner->ctx = NULL;
    }

    if(runner->rt) {
        JS_FreeRuntime(runner->rt);
        runner->rt = NULL;
    }

    if(runner->input_subscription) {
        furi_pubsub_unsubscribe(runner->input_pubsub, runner->input_subscription);
    }
    if(runner->input_pubsub) {
        furi_record_close(RECORD_INPUT_EVENTS);
    }

    if(runner->event_loop && runner->input_queue) {
        furi_event_loop_unsubscribe(runner->event_loop, runner->input_queue);
    }
    if(runner->input_queue) {
        furi_message_queue_free(runner->input_queue);
    }

    if(runner->gui && runner->gui_layer) {
        with_gui(runner->gui, {
            gui_layer_remove_input_callback(runner->gui_layer, js_runner_gui_input_callback);
        });
    }

    if(runner->gui) furi_record_close(RECORD_GUI);
    if(runner->desktop) furi_record_close(RECORD_DESKTOP);
    if(runner->event_loop) furi_event_loop_free(runner->event_loop);

    free(runner);
}

static void js_runner_register_globals(JsRunner* runner) {
    JSContext* ctx = runner->ctx;
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "print", JS_NewCFunction(ctx, js_print, "print", 1));
    JS_SetPropertyStr(ctx, global, "str", JS_NewCFunction(ctx, js_str, "str", 1));
    JS_SetPropertyStr(ctx, global, "delay", JS_NewCFunction(ctx, js_delay, "delay", 1));
    JS_SetPropertyStr(ctx, global, "require", JS_NewCFunction(ctx, js_require, "require", 1));
    JS_SetPropertyStr(ctx, global, "__runLoop", JS_NewCFunction(ctx, js_run_loop, "__runLoop", 0));

    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, js_console_warn, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_console_error, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", console);

    JS_FreeValue(ctx, global);
}

int32_t js_runner_app(void* arg) {
    const char* script_path = arg;
    if(!script_path || script_path[0] == '\0') {
        FURI_LOG_E(TAG, "No script path provided");
        return -1;
    }

    JsRunner* runner = js_runner_alloc(script_path);
    if(!runner) {
        FURI_LOG_E(TAG, "Failed to allocate runner");
        return -1;
    }

    runner->rt = JS_NewRuntime();
    if(!runner->rt) {
        FURI_LOG_E(TAG, "Failed to create JerryScript runtime");
        js_runner_free(runner);
        return -1;
    }

    JS_SetMemoryLimit(runner->rt, JS_RUNNER_HEAP_SIZE_BYTES);
    JS_SetInterruptHandler(runner->rt, js_interrupt_handler, runner);

    runner->ctx = JS_NewContext(runner->rt);
    if(!runner->ctx) {
        FURI_LOG_E(TAG, "Failed to create JerryScript context");
        js_runner_free(runner);
        return -1;
    }

    JS_SetContextOpaque(runner->ctx, runner);

    runner->modules = js_modules_create(runner->ctx, runner);
    if(!runner->modules) {
        FURI_LOG_E(TAG, "Failed to allocate module registry");
        js_runner_free(runner);
        return -1;
    }
    js_runner_register_globals(runner);

    FuriThread* thread = furi_thread_get_current();
    furi_thread_set_signal_callback(thread, js_runner_signal_callback, runner);

    JSValue result = js_runner_exec_file(runner, script_path);
    if(JS_IsException(result)) {
        js_runner_log_exception(runner, "script execution failed");
        runner->running = false;
    }
    JS_FreeValue(runner->ctx, result);

    furi_thread_set_signal_callback(thread, NULL, NULL);
    js_runner_free(runner);
    return 0;
}

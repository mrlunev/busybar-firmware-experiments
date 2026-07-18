#include "../js_modules.h"

#include <version.h>

static JSValue js_system_exit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    runner->running = false;
    furi_event_loop_stop(runner->event_loop);
    desktop_replace_current_app(runner->desktop, "apps_menu", runner->app_id);
    return JS_UNDEFINED;
}

static JSValue js_system_app_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    JsRunner* runner = JS_GetContextOpaque(ctx);
    return JS_NewString(ctx, runner->app_id);
}

static JSValue js_system_script_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    JsRunner* runner = JS_GetContextOpaque(ctx);
    return JS_NewString(ctx, runner->script_path);
}

static JSValue js_system_version(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    const Version* version = version_get();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "version", JS_NewString(ctx, version_get_version(version)));
    JS_SetPropertyStr(ctx, obj, "branch", JS_NewString(ctx, version_get_gitbranch(version)));
    JS_SetPropertyStr(ctx, obj, "commit", JS_NewString(ctx, version_get_githash(version)));
    JS_SetPropertyStr(ctx, obj, "target", JS_NewInt32(ctx, version_get_target(version)));
    JS_SetPropertyStr(ctx, obj, "dirty", JS_NewBool(ctx, version_get_dirty_flag(version)));
    return obj;
}

static JSValue js_system_runtime_stats(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    JsRunner* runner = JS_GetContextOpaque(ctx);
    size_t active_timers = 0;
    for(size_t i = 0; i < runner->timer_count; i++) {
        if(runner->timers[i] && !JS_IsUndefined(runner->timer_callbacks[i])) {
            active_timers++;
        }
    }

    size_t active_input_callbacks = 0;
    for(size_t i = 0; i < runner->input_cb_count; i++) {
        if(!JS_IsUndefined(runner->input_callbacks[i].callback)) {
            active_input_callbacks++;
        }
    }

    size_t heap_total_kb = 0;
    size_t heap_used_bytes = 0;
    bsb_jerry_heap_info(&heap_total_kb, &heap_used_bytes);
    const size_t heap_total_bytes = heap_total_kb * 1024u;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "heapTotalBytes", JS_NewInt32(ctx, heap_total_bytes));
    JS_SetPropertyStr(ctx, obj, "heapUsedBytes", JS_NewInt32(ctx, heap_used_bytes));
    JS_SetPropertyStr(
        ctx,
        obj,
        "heapFreeBytes",
        JS_NewInt32(ctx, heap_total_bytes > heap_used_bytes ? heap_total_bytes - heap_used_bytes : 0));
    JS_SetPropertyStr(ctx, obj, "activeTimers", JS_NewInt32(ctx, active_timers));
    JS_SetPropertyStr(ctx, obj, "maxTimers", JS_NewInt32(ctx, JS_RUNNER_MAX_TIMERS));
    JS_SetPropertyStr(
        ctx, obj, "activeInputCallbacks", JS_NewInt32(ctx, active_input_callbacks));
    JS_SetPropertyStr(
        ctx, obj, "maxInputCallbacks", JS_NewInt32(ctx, JS_RUNNER_MAX_INPUT_CBS));
    JS_SetPropertyStr(
        ctx, obj, "inputQueueCapacity", JS_NewInt32(ctx, JS_RUNNER_INPUT_QUEUE_SIZE));
    JS_SetPropertyStr(
        ctx,
        obj,
        "droppedInputEvents",
        JS_NewInt32(
            ctx, atomic_load_explicit(&runner->input_drop_count, memory_order_relaxed)));
    JS_SetPropertyStr(ctx, obj, "callbackDepth", JS_NewInt32(ctx, runner->callback_depth));
    JS_SetPropertyStr(
        ctx,
        obj,
        "callbackExceptionCount",
        JS_NewInt32(ctx, runner->callback_exception_count));
    JS_SetPropertyStr(
        ctx,
        obj,
        "lastCallbackException",
        JS_NewString(ctx, runner->last_callback_exception));
    JS_SetPropertyStr(ctx, obj, "stackLimitEnforced", JS_NewBool(ctx, false));
    JS_SetPropertyStr(ctx, obj, "fatalCallbackExceptions", JS_NewBool(ctx, true));
    JS_SetPropertyStr(ctx, obj, "fetchConcurrencyLimit", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(
        ctx, obj, "fetchMaxResponseBytes", JS_NewInt32(ctx, JS_RUNNER_FETCH_MAX_BYTES));
    return obj;
}

JSValue js_module_system_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "exit", JS_NewCFunction(ctx, js_system_exit, "exit", 0));
    JS_SetPropertyStr(ctx, obj, "appId", JS_NewCFunction(ctx, js_system_app_id, "appId", 0));
    JS_SetPropertyStr(ctx, obj, "scriptPath", JS_NewCFunction(ctx, js_system_script_path, "scriptPath", 0));
    JS_SetPropertyStr(ctx, obj, "version", JS_NewCFunction(ctx, js_system_version, "version", 0));
    JS_SetPropertyStr(
        ctx,
        obj,
        "runtimeStats",
        JS_NewCFunction(ctx, js_system_runtime_stats, "runtimeStats", 0));
    return obj;
}

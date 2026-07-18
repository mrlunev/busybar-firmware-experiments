#include "../js_modules.h"

static void js_timer_release_slot(JsRunner* runner, size_t index) {
    if(!runner || index >= JS_RUNNER_MAX_TIMERS) return;

    if(runner->timers[index]) {
        if(runner->timer_started[index]) {
            furi_event_loop_timer_stop(runner->timers[index]);
        }
        furi_event_loop_timer_free(runner->timers[index]);
        runner->timers[index] = NULL;
    }

    runner->timer_started[index] = false;
    runner->timer_periodic[index] = false;
    runner->timer_intervals_ms[index] = 0;

    if(runner->timer_contexts[index]) {
        ((JsTimerContext*)runner->timer_contexts[index])->active = false;
        free(runner->timer_contexts[index]);
        runner->timer_contexts[index] = NULL;
    }

    if(runner->ctx && !JS_IsUndefined(runner->timer_callbacks[index])) {
        JS_FreeValue(runner->ctx, runner->timer_callbacks[index]);
        runner->timer_callbacks[index] = JS_UNDEFINED;
    }
}

static void js_timer_dispatch(void* context) {
    JsTimerContext* timer_context = context;
    if(!timer_context || !timer_context->active) return;

    JsRunner* runner = timer_context->runner;
    size_t index = timer_context->index;

    // FURI_LOG_D(TAG, "timer dispatch begin idx=%zu", index);
    if(index >= runner->timer_count) return;
    if(runner->timers[index] == NULL) return;
    if(JS_IsUndefined(runner->timer_callbacks[index])) return;

    const bool periodic = runner->timer_periodic[index];
    runner->timer_dispatch_active = true;
    runner->timer_dispatch_index = index;
    runner->timer_dispatch_cancelled = false;

    runner->callback_depth++;
    JSValue result = JS_Call(runner->ctx, runner->timer_callbacks[index], JS_UNDEFINED, 0, NULL);
    if(runner->callback_depth > 0) {
        runner->callback_depth--;
    }
    const bool release_after_dispatch = !periodic || runner->timer_dispatch_cancelled;
    runner->timer_dispatch_active = false;
    runner->timer_dispatch_cancelled = false;

    // FURI_LOG_D(TAG, "timer dispatch end idx=%zu", index);
    if(JS_IsException(result)) {
        js_runner_log_exception(runner, "timer callback");
        runner->running = false;
        furi_event_loop_stop(runner->event_loop);
    }
    JS_FreeValue(runner->ctx, result);
    if(runner->callback_depth == 0 && runner->display_flush_pending) {
        js_display_flush(runner);
    }

    if(release_after_dispatch) {
        js_timer_release_slot(runner, index);
    }
}

static void js_timer_start_pending_callback(void* context) {
    js_timer_start_pending(context);
}

void js_timer_start_pending(JsRunner* runner) {
    if(!runner) return;

    for(size_t i = 0; i < runner->timer_count; i++) {
        if(!runner->timers[i] || runner->timer_started[i]) continue;
        FURI_LOG_D(
            TAG,
            "timer start idx=%zu interval=%lu",
            i,
            (unsigned long)runner->timer_intervals_ms[i]);
        furi_event_loop_timer_start(
            runner->timers[i], furi_ms_to_ticks(runner->timer_intervals_ms[i]));
        runner->timer_started[i] = true;
    }
}

static JSValue js_timer_create_impl(JSContext* ctx, int argc, JSValueConst* argv, FuriEventLoopTimerType type) {
    if(argc < 2 || !JS_IsNumber(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "timer expects seconds and callback");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    size_t index = runner->timer_count;
    for(size_t i = 0; i < runner->timer_count; i++) {
        if(runner->timers[i] == NULL) {
            index = i;
            break;
        }
    }

    if(index >= JS_RUNNER_MAX_TIMERS) {
        return JS_ThrowRangeError(ctx, "too many timers");
    }

    JsTimerContext* timer_context = malloc(sizeof(JsTimerContext));
    if(!timer_context) {
        return JS_ThrowInternalError(ctx, "out of memory");
    }
    timer_context->runner = runner;
    timer_context->index = index;
    timer_context->active = true;

    runner->timer_contexts[index] = timer_context;
    runner->timer_callbacks[index] = JS_DupValue(ctx, argv[1]);

    runner->timers[index] =
        furi_event_loop_timer_alloc(runner->event_loop, js_timer_dispatch, type, timer_context);
    if(!runner->timers[index]) {
        JS_FreeValue(ctx, runner->timer_callbacks[index]);
        runner->timer_callbacks[index] = JS_UNDEFINED;
        runner->timer_contexts[index] = NULL;
        free(timer_context);
        return JS_ThrowInternalError(ctx, "failed to allocate timer");
    }

    double seconds;
    JS_ToFloat64(ctx, &seconds, argv[0]);
    uint32_t interval_ms = (uint32_t)(seconds * 1000.0 + 0.5);
    if(interval_ms < 10) interval_ms = 10;
    runner->timer_intervals_ms[index] = interval_ms;
    runner->timer_started[index] = false;
    runner->timer_periodic[index] = type == FuriEventLoopTimerTypePeriodic;
    FURI_LOG_D(TAG, "timer create idx=%zu interval=%lu", index, (unsigned long)interval_ms);
    if(runner->event_loop_active) {
        furi_event_loop_pend_callback(runner->event_loop, js_timer_start_pending_callback, runner);
    }

    if(index == runner->timer_count) {
        runner->timer_count++;
    }

    return JS_NewInt32(ctx, (int32_t)index);
}

static JSValue js_timer_every(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    return js_timer_create_impl(ctx, argc, argv, FuriEventLoopTimerTypePeriodic);
}

static JSValue js_timer_once(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    return js_timer_create_impl(ctx, argc, argv, FuriEventLoopTimerTypeOnce);
}

static JSValue js_timer_cancel(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "cancel expects a timer id");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    int32_t index;
    JS_ToInt32(ctx, &index, argv[0]);
    if(index < 0 || (size_t)index >= runner->timer_count) {
        return JS_ThrowRangeError(ctx, "invalid timer id");
    }

    if(runner->timer_dispatch_active && runner->timer_dispatch_index == (size_t)index) {
        runner->timer_dispatch_cancelled = true;
        return JS_UNDEFINED;
    }

    js_timer_release_slot(runner, (size_t)index);

    return JS_UNDEFINED;
}

JSValue js_module_timer_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "every", JS_NewCFunction(ctx, js_timer_every, "every", 2));
    JS_SetPropertyStr(ctx, obj, "once", JS_NewCFunction(ctx, js_timer_once, "once", 2));
    JS_SetPropertyStr(ctx, obj, "cancel", JS_NewCFunction(ctx, js_timer_cancel, "cancel", 1));
    return obj;
}

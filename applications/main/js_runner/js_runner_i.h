#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <gui/gui.h>
#include <gui/modules/label.h>
#include <gui/modules/image.h>
#include <input/input.h>
#include <desktop/desktop.h>
#include <gui/modules/var_item_list.h>

#include "js_quickjs_compat.h"

#define TAG "JsRunner"

#define JS_RUNNER_MAX_TIMERS        8
#define JS_RUNNER_MAX_INPUT_CBS     16
#define JS_RUNNER_MAX_MODULE_CACHE  16
#define JS_RUNNER_MAX_LOCAL_MODULES 16

typedef struct JsRunner JsRunner;
typedef struct JsModules JsModules;

typedef struct {
    JSValue callback;
    uint8_t key;
    uint8_t type;
} JsInputCallback;

typedef struct {
    JsRunner* runner;
    size_t index;
    bool active;
} JsTimerContext;

struct JsRunner {
    JSRuntime* rt;
    JSContext* ctx;
    JsModules* modules;

    FuriEventLoop* event_loop;

    FuriEventLoopTimer* timers[JS_RUNNER_MAX_TIMERS];
    void* timer_contexts[JS_RUNNER_MAX_TIMERS];
    JSValue timer_callbacks[JS_RUNNER_MAX_TIMERS];
    uint32_t timer_intervals_ms[JS_RUNNER_MAX_TIMERS];
    bool timer_started[JS_RUNNER_MAX_TIMERS];
    bool timer_periodic[JS_RUNNER_MAX_TIMERS];
    size_t timer_count;
    bool timer_dispatch_active;
    size_t timer_dispatch_index;
    bool timer_dispatch_cancelled;

    FuriPubSub* input_pubsub;
    FuriPubSubSubscription* input_subscription;
    FuriMessageQueue* input_queue;
    JsInputCallback input_callbacks[JS_RUNNER_MAX_INPUT_CBS];
    size_t input_cb_count;

    Gui* gui;
    GuiLayer* gui_layer;
    Desktop* desktop;

    void* display_state;
    void* fetch_state;
    void* anim_state;
    void* settings_state;
    void* config_state;
    uint8_t callback_depth;
    bool display_flush_pending;
    bool event_loop_active;
    bool widget_active;

    char app_id[32];
    char script_dir[128];
    char script_path[128];
    bool running;
};

void js_display_cleanup(JsRunner* runner);
void js_display_flush(JsRunner* runner);
void js_timer_start_pending(JsRunner* runner);
void js_fetch_cleanup(JsRunner* runner);
void js_anim_cleanup(JsRunner* runner);
void js_settings_cleanup(JsRunner* runner);
void js_settings_close(JsRunner* runner);
bool js_settings_handle_input(JsRunner* runner, const InputEvent* event);
void js_audio_cleanup(JsRunner* runner);
void js_radio_cleanup(JsRunner* runner);
void js_config_cleanup(JsRunner* runner);

JSValue js_runner_exec_file(JsRunner* runner, const char* path);
bool js_runner_call_callback(
    JsRunner* runner,
    JSValue callback,
    int argc,
    JSValueConst* argv,
    const char* origin);
void js_runner_log_exception(JsRunner* runner, const char* context);
void js_runner_invoke_callback(JsRunner* runner, JSValue callback, const char* origin);

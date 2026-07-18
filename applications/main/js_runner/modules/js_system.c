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

JSValue js_module_system_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "exit", JS_NewCFunction(ctx, js_system_exit, "exit", 0));
    JS_SetPropertyStr(ctx, obj, "appId", JS_NewCFunction(ctx, js_system_app_id, "appId", 0));
    JS_SetPropertyStr(ctx, obj, "scriptPath", JS_NewCFunction(ctx, js_system_script_path, "scriptPath", 0));
    JS_SetPropertyStr(ctx, obj, "version", JS_NewCFunction(ctx, js_system_version, "version", 0));
    return obj;
}

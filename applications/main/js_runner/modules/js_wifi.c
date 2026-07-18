#include "../js_modules.h"

#include <wifi/wifi.h>

static const char* js_wifi_state_name(WifiState state) {
    switch(state) {
    case WifiStateDisconnected:
        return "disconnected";
    case WifiStateConnected:
        return "connected";
    case WifiStateConnecting:
        return "connecting";
    case WifiStateDisconnecting:
        return "disconnecting";
    case WifiStateReconnecting:
        return "reconnecting";
    case WifiStateUnknown:
    default:
        return "unknown";
    }
}

static JSValue js_wifi_status(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);

    WifiInfo info = {0};
    Wifi* wifi = furi_record_open(RECORD_WIFI);
    const WifiStatus status = wifi_get_info(wifi, &info);
    furi_record_close(RECORD_WIFI);

    const bool available = status == WifiStatusOk;
    const bool connected = available && info.state == WifiStateConnected;
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "available", JS_NewBool(ctx, available));
    JS_SetPropertyStr(ctx, result, "connected", JS_NewBool(ctx, connected));
    JS_SetPropertyStr(
        ctx,
        result,
        "state",
        JS_NewString(ctx, available ? js_wifi_state_name(info.state) : "unavailable"));
    JS_SetPropertyStr(
        ctx, result, "ssid", JS_NewString(ctx, connected ? info.ssid : ""));
    JS_SetPropertyStr(
        ctx, result, "rssi", connected ? JS_NewInt32(ctx, info.rssi) : JS_NULL);
    JS_SetPropertyStr(
        ctx, result, "channel", connected ? JS_NewInt32(ctx, info.channel) : JS_NULL);
    return result;
}

JSValue js_module_wifi_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue module = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, module, "status", JS_NewCFunction(ctx, js_wifi_status, "status", 0));
    return module;
}

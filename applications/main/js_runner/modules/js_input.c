#include "../js_modules.h"

static uint8_t js_input_parse_key(const char* name) {
    if(strcmp(name, "ok") == 0) return InputKeyOk;
    if(strcmp(name, "up") == 0) return InputKeyUp;
    if(strcmp(name, "down") == 0) return InputKeyDown;
    if(strcmp(name, "left") == 0) return InputKeyLeft;
    if(strcmp(name, "right") == 0) return InputKeyRight;
    if(strcmp(name, "back") == 0) return InputKeyBack;
    if(strcmp(name, "start") == 0) return InputKeyStart;
    return InputKeyMAX;
}

static uint8_t js_input_parse_type(const char* name) {
    if(strcmp(name, "press") == 0) return InputTypePress;
    if(strcmp(name, "release") == 0) return InputTypeRelease;
    if(strcmp(name, "short") == 0) return InputTypeShort;
    if(strcmp(name, "long") == 0) return InputTypeLong;
    if(strcmp(name, "repeat") == 0) return InputTypeRepeat;
    return InputTypeMAX;
}

static JSValue js_input_on(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 3) {
        return JS_ThrowTypeError(ctx, "on expects key, type and callback");
    }

    if(!JS_IsString(argv[0]) || !JS_IsString(argv[1]) || !JS_IsFunction(ctx, argv[2])) {
        return JS_ThrowTypeError(ctx, "invalid arguments for input.on");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    if(runner->input_cb_count >= JS_RUNNER_MAX_INPUT_CBS) {
        return JS_ThrowRangeError(ctx, "too many input callbacks");
    }

    const char* key_str = JS_ToCString(ctx, argv[0]);
    const char* type_str = JS_ToCString(ctx, argv[1]);
    if(!key_str || !type_str) {
        JS_FreeCString(ctx, key_str);
        JS_FreeCString(ctx, type_str);
        return JS_EXCEPTION;
    }

    uint8_t key = js_input_parse_key(key_str);
    uint8_t type = js_input_parse_type(type_str);
    JS_FreeCString(ctx, key_str);
    JS_FreeCString(ctx, type_str);

    if(key == InputKeyMAX || type == InputTypeMAX) {
        return JS_ThrowTypeError(ctx, "unknown key or input type");
    }

    JsInputCallback* slot = &runner->input_callbacks[runner->input_cb_count++];
    slot->key = key;
    slot->type = type;
    slot->callback = JS_DupValue(ctx, argv[2]);

    return JS_NewInt32(ctx, (int32_t)(runner->input_cb_count - 1));
}

JSValue js_module_input_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "on", JS_NewCFunction(ctx, js_input_on, "on", 3));
    return obj;
}

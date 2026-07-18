#include "../js_runner_i.h"
#include <gui/gui.h>
#include <gui/modules/var_item_list.h>

#define SETTINGS_MAX_ITEMS   8
#define SETTINGS_MAX_OPTIONS 16

typedef struct {
    JsRunner* runner;
    int item_index;
} JsSettingsItemCtx;

typedef struct {
    VarItemList* front_list;
    VarItemList* back_list;
    VarItem* front_items[SETTINGS_MAX_ITEMS];
    VarItem* back_items[SETTINGS_MAX_ITEMS];
    JsSettingsItemCtx* item_ctx[SETTINGS_MAX_ITEMS];
    int32_t values[SETTINGS_MAX_ITEMS];
    size_t item_count;
    JSValue callback;
    const char** option_ptrs[SETTINGS_MAX_ITEMS];
    size_t option_counts[SETTINGS_MAX_ITEMS];
} JsSettingsState;

static void settings_item_changed(VarItem* item, void* context) {
    JsSettingsItemCtx* ctx = context;
    if(!ctx || !ctx->runner) return;
    JsSettingsState* state = ctx->runner->settings_state;
    if(!state || !state->back_list) return;
    int i = ctx->item_index;
    if(i < 0 || i >= (int)state->item_count || !state->back_items[i]) return;
    var_item_set_value(state->back_items[i], var_item_get_value(item));
}

void js_settings_close(JsRunner* runner) {
    JsSettingsState* state = runner->settings_state;
    if(!state) return;
    if(!state->front_list) return;

    for(size_t i = 0; i < state->item_count; i++) {
        state->values[i] = var_item_get_value(state->front_items[i]);
    }

    with_gui(runner->gui, {
        if(state->front_list) {
            var_item_list_free(state->front_list);
            state->front_list = NULL;
        }
        if(state->back_list) {
            var_item_list_free(state->back_list);
            state->back_list = NULL;
        }
    });

    for(size_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if(state->item_ctx[i]) {
            free(state->item_ctx[i]);
            state->item_ctx[i] = NULL;
        }
        state->back_items[i] = NULL;
    }

    runner->widget_active = false;

    if(!JS_IsUndefined(state->callback) && runner->ctx) {
        JSContext* ctx = runner->ctx;
        JSValue arr = JS_NewArray(ctx);
        for(size_t i = 0; i < state->item_count; i++) {
            JS_SetPropertyUint32(ctx, arr, (uint32_t)i, JS_NewInt32(ctx, state->values[i] + 1));
        }
        JSValue argv[1] = {arr};
        js_runner_call_callback(runner, state->callback, 1, argv, "settings callback");
        JS_FreeValue(ctx, arr);
        JS_FreeValue(ctx, state->callback);
        state->callback = JS_UNDEFINED;
    }

    for(size_t i = 0; i < state->item_count; i++) {
        if(state->option_ptrs[i]) {
            for(size_t j = 0; j < state->option_counts[i]; j++) {
                free((void*)state->option_ptrs[i][j]);
            }
            free(state->option_ptrs[i]);
            state->option_ptrs[i] = NULL;
        }
    }
    state->item_count = 0;
}

void js_settings_cleanup(JsRunner* runner) {
    JsSettingsState* state = runner->settings_state;
    if(!state) return;

    with_gui(runner->gui, {
        if(state->front_list) {
            var_item_list_free(state->front_list);
            state->front_list = NULL;
        }
        if(state->back_list) {
            var_item_list_free(state->back_list);
            state->back_list = NULL;
        }
    });

    for(size_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if(state->item_ctx[i]) {
            free(state->item_ctx[i]);
            state->item_ctx[i] = NULL;
        }
    }

    if(!JS_IsUndefined(state->callback) && runner->ctx) {
        JS_FreeValue(runner->ctx, state->callback);
    }

    for(size_t i = 0; i < state->item_count; i++) {
        if(state->option_ptrs[i]) {
            for(size_t j = 0; j < state->option_counts[i]; j++) {
                free((void*)state->option_ptrs[i][j]);
            }
            free(state->option_ptrs[i]);
        }
    }

    free(state);
    runner->settings_state = NULL;
    runner->widget_active = false;
}

bool js_settings_handle_input(JsRunner* runner, const InputEvent* event) {
    if(!runner->widget_active || !runner->settings_state) return false;

    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        return true;
    }

    return false;
}

static JSValue js_settings_show(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv) {
    UNUSED(this_val);

    if(argc < 2) {
        return JS_ThrowTypeError(ctx, "settings.show(items, callback)");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);

    if(runner->widget_active) {
        return JS_ThrowTypeError(ctx, "a widget is already active");
    }

    if(!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "settings.show expects (array, function)");
    }

    JsSettingsState* state = runner->settings_state;
    if(!state) {
        state = malloc(sizeof(JsSettingsState));
        memset(state, 0, sizeof(JsSettingsState));
        state->callback = JS_UNDEFINED;
        runner->settings_state = state;
    }

    state->callback = JS_DupValue(ctx, argv[1]);

    JSValue len_val = JS_GetPropertyStr(ctx, argv[0], "length");
    int32_t item_count = 0;
    JS_ToInt32(ctx, &item_count, len_val);
    JS_FreeValue(ctx, len_val);
    if(item_count > SETTINGS_MAX_ITEMS) item_count = SETTINGS_MAX_ITEMS;
    state->item_count = (size_t)item_count;

    for(size_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if(state->item_ctx[i]) {
            free(state->item_ctx[i]);
            state->item_ctx[i] = NULL;
        }
        state->front_items[i] = NULL;
        state->back_items[i] = NULL;
    }

    char labels[SETTINGS_MAX_ITEMS][64];
    int32_t initial_values[SETTINGS_MAX_ITEMS];

    for(int i = 0; i < item_count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);

        JSValue label_val = JS_GetPropertyStr(ctx, item, "label");
        const char* label = JS_ToCString(ctx, label_val);
        strlcpy(labels[i], label ? label : "?", sizeof(labels[i]));
        if(label) JS_FreeCString(ctx, label);
        JS_FreeValue(ctx, label_val);

        JSValue value_val = JS_GetPropertyStr(ctx, item, "value");
        int32_t val = 0;
        JS_ToInt32(ctx, &val, value_val);
        initial_values[i] = val > 0 ? val - 1 : 0;
        JS_FreeValue(ctx, value_val);

        JSValue options_val = JS_GetPropertyStr(ctx, item, "options");
        JSValue opt_len_val = JS_GetPropertyStr(ctx, options_val, "length");
        int32_t opt_count = 0;
        JS_ToInt32(ctx, &opt_count, opt_len_val);
        JS_FreeValue(ctx, opt_len_val);
        if(opt_count > SETTINGS_MAX_OPTIONS) opt_count = SETTINGS_MAX_OPTIONS;
        state->option_counts[i] = (size_t)opt_count;

        state->option_ptrs[i] = malloc(sizeof(const char*) * (size_t)opt_count);
        for(int j = 0; j < opt_count; j++) {
            JSValue opt_val = JS_GetPropertyUint32(ctx, options_val, (uint32_t)j);
            const char* opt = JS_ToCString(ctx, opt_val);
            const char* s = opt ? opt : "?";
            size_t slen = strlen(s);
            char* dup = malloc(slen + 1);
            memcpy(dup, s, slen + 1);
            state->option_ptrs[i][j] = dup;
            if(opt) JS_FreeCString(ctx, opt);
            JS_FreeValue(ctx, opt_val);
        }
        JS_FreeValue(ctx, options_val);
        JS_FreeValue(ctx, item);
    }

    with_gui(runner->gui, {
        Widget* front_root = gui_layer_get_root_widget(runner->gui_layer, GuiDisplayIdFront);
        state->front_list = var_item_list_alloc(front_root);

        Widget* back_root = gui_layer_get_root_widget(runner->gui_layer, GuiDisplayIdBack);
        state->back_list = var_item_list_alloc(back_root);

        for(int i = 0; i < item_count; i++) {
            JsSettingsItemCtx* ictx = malloc(sizeof(JsSettingsItemCtx));
            furi_check(ictx);
            ictx->runner = runner;
            ictx->item_index = i;
            state->item_ctx[i] = ictx;

            state->front_items[i] = var_item_list_add_selector(
                state->front_list,
                labels[i],
                NULL,
                state->option_ptrs[i],
                (uint32_t)state->option_counts[i],
                settings_item_changed,
                ictx);
            var_item_set_value(state->front_items[i], initial_values[i]);
            state->values[i] = initial_values[i];

            state->back_items[i] = var_item_list_add_selector(
                state->back_list,
                labels[i],
                NULL,
                state->option_ptrs[i],
                (uint32_t)state->option_counts[i],
                NULL,
                NULL);
            var_item_set_value(state->back_items[i], initial_values[i]);
        }
    });

    runner->widget_active = true;
    return JS_UNDEFINED;
}

JSValue js_module_settings_create(JSContext* ctx, JsRunner* runner) {
    UNUSED(runner);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "show", JS_NewCFunction(ctx, js_settings_show, "show", 2));
    return obj;
}

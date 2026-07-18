#include "../js_modules.h"

#include <font_registry/fonts.h>
#include <gui/gui.h>
#include <gui/widget_i.h>
#include <gui/modules/flex_layout.h>
#include <gui/modules/anim_player.h>
#include <gui/modules/image.h>
#include <gui/modules/label.h>
#include <lvgl.h>
#include <lvgl_addons/themes/lv_theme_common.h>
#include <math.h>
#include <toolbox/color.h>

typedef struct {
    char* text;
    const char* font;
    char custom_font_path[96];
    Color color;
    int16_t x;
    int16_t y;
    Align align;
    GuiDisplayId display;
    size_t width;
    int16_t rotation;
    int16_t z;
    uint8_t opacity;
    bool has_clip;
    int16_t clip_x, clip_y, clip_w, clip_h;
    bool long_mode_set;
    LabelLongContentMode long_mode;
    uint32_t scroll_duration_ms;
} JsPendingText;

typedef struct {
    char* path;
    int16_t x;
    int16_t y;
    GuiDisplayId display;
    bool background;
    int16_t z;
    uint8_t opacity;
    int32_t scale;
    bool has_clip;
    int16_t clip_x, clip_y, clip_w, clip_h;
} JsPendingImage;

typedef enum {
    JsGfxTypePixel,
    JsGfxTypeRect,
    JsGfxTypeLine,
    JsGfxTypeCircle,
} JsGfxType;

typedef struct {
    JsGfxType type;
    float x1;
    float y1;
    float x2;
    float y2;
    int16_t w;
    int16_t h;
    int16_t r;
    uint8_t cr;
    uint8_t cg;
    uint8_t cb;
    bool filled;
    GuiDisplayId display;
} JsPendingGfx;

#define JS_DISPLAY_MAX_PENDING        32
#define JS_DISPLAY_MAX_PENDING_GFX    256
#define JS_DISPLAY_MAX_PENDING_IMAGES 32
#define JS_DISPLAY_MAX_LABELS         32
#define JS_DISPLAY_MAX_IMAGES         32
#define JS_DISPLAY_MAX_FLEX           16
#define JS_FLEX_MAX_CHILDREN          32
#define JS_GRAPH_MAX_VALUES           72
#define JS_DISPLAY_MAX_CLIP_CONTAINERS 32

typedef enum {
    JsFlexChildText,
    JsFlexChildImage,
} JsFlexChildType;

typedef struct {
    JsFlexChildType type;
    uint8_t grow;
    char* text;
    const char* font;
    char custom_font_path[96];
    Color color;
    size_t width;
    char* path;
} JsFlexChild;

typedef struct {
    int16_t x, y;
    int16_t width, height;
    GuiDisplayId display;
    int16_t z;
    uint8_t flow;
    int32_t spacing;
    uint8_t main_align;
    uint8_t cross_align;
    int16_t padding;
    int16_t parent_flex_idx;
    JsFlexChild children[JS_FLEX_MAX_CHILDREN];
    size_t child_count;
} JsPendingFlex;

typedef struct {
    float values[JS_GRAPH_MAX_VALUES];
    uint8_t count;
    int16_t x, y, width, height;
    uint8_t color_top_r, color_top_g, color_top_b;
    uint8_t color_bot_r, color_bot_g, color_bot_b;
    int16_t cursor_x;
    uint8_t cursor_r, cursor_g, cursor_b;
    uint8_t border_r, border_g, border_b;
    bool has_border;
    int16_t min_curve_y;
    float temp_min;
    float temp_max;
    GuiDisplayId display;
    int16_t z;
    bool active;
    float reveal_x;
    int16_t reveal_fade;
    float reveal_slide_y;
} JsPendingGraph;

typedef struct {
    int16_t z;
    uint8_t type_order;
    uint8_t index;
} JsZEntry;

typedef struct {
    Label* label;
    GuiDisplayId display;
    lv_obj_t* clip_container;
    char* prev_text;
    char prev_font_path[96];
    Color prev_color;
    Align prev_align;
    bool prev_has_clip;
    int16_t prev_clip_x, prev_clip_y, prev_clip_w, prev_clip_h;
    bool prev_long_mode_set;
    LabelLongContentMode prev_long_mode;
    uint32_t prev_scroll_duration_ms;
    size_t prev_fixed_width;
} JsActiveLabel;

typedef struct {
    Image* image;
    GuiDisplayId display;
    lv_obj_t* clip_container;
    char* prev_path;
    bool prev_has_clip;
    int16_t prev_clip_x, prev_clip_y, prev_clip_w, prev_clip_h;
} JsActiveImage;

typedef struct {
    JsPendingText items[JS_DISPLAY_MAX_PENDING];
    size_t count;
    JsPendingGfx gfx[JS_DISPLAY_MAX_PENDING_GFX];
    size_t gfx_count;
    JsPendingImage images[JS_DISPLAY_MAX_PENDING_IMAGES];
    size_t image_count;

    JsActiveLabel active_labels[JS_DISPLAY_MAX_LABELS];
    size_t active_label_count;

    JsActiveImage active_images[JS_DISPLAY_MAX_IMAGES];
    size_t active_image_count;

    struct {
        Image* image;
        GuiDisplayId display;
        lv_obj_t* clip_container;
    } active_bg_images[JS_DISPLAY_MAX_IMAGES];
    size_t active_bg_image_count;

    JsPendingFlex flexes[JS_DISPLAY_MAX_FLEX];
    size_t flex_count;

    struct {
        FlexLayout* layout;
        GuiDisplayId display;
    } active_flexes[JS_DISPLAY_MAX_FLEX];
    size_t active_flex_count;

    JsPendingGraph graph;

    struct {
        char* path;
        int16_t x, y, z;
        GuiDisplayId display;
        bool set;
        uint8_t opacity;
        int32_t scale;
    } pending_anim;

    AnimPlayer* active_anim;
    char* active_anim_path;
    GuiDisplayId active_anim_display;

    lv_obj_t* canvas_front;
    lv_obj_t* canvas_back;
    lv_draw_buf_t* canvas_front_buf;
    lv_draw_buf_t* canvas_back_buf;

    float cached_curve[JS_GRAPH_MAX_VALUES];
    float cached_values[JS_GRAPH_MAX_VALUES];
    uint8_t cached_values_count;
    int16_t cached_graph_width;
    bool curve_cache_valid;
    uint32_t active_order_hash;

    int16_t graph_border_y[JS_GRAPH_MAX_VALUES];
    uint8_t graph_col_r[JS_GRAPH_MAX_VALUES];
    uint8_t graph_col_g[JS_GRAPH_MAX_VALUES];
    uint8_t graph_col_b[JS_GRAPH_MAX_VALUES];
    float graph_reveal_opa[JS_GRAPH_MAX_VALUES];
    float graph_reveal_dy[JS_GRAPH_MAX_VALUES];
} JsDisplayState;

static JsDisplayState* js_display_get_state(JsRunner* runner) {
    if(!runner->display_state) {
        runner->display_state = malloc(sizeof(JsDisplayState));
        memset(runner->display_state, 0, sizeof(JsDisplayState));
    }
    return runner->display_state;
}

static void js_display_pending_clear(JsDisplayState* state) {
    for(size_t i = 0; i < state->count; i++) {
        free(state->items[i].text);
        state->items[i].text = NULL;
    }
    state->count = 0;
    state->gfx_count = 0;

    for(size_t i = 0; i < state->image_count; i++) {
        free(state->images[i].path);
        state->images[i].path = NULL;
    }
    state->image_count = 0;

    for(size_t i = 0; i < state->flex_count; i++) {
        JsPendingFlex* flex = &state->flexes[i];
        for(size_t j = 0; j < flex->child_count; j++) {
            free(flex->children[j].text);
            free(flex->children[j].path);
        }
        flex->child_count = 0;
    }
    state->flex_count = 0;
    state->graph.active = false;

    if(state->pending_anim.path) {
        free(state->pending_anim.path);
        state->pending_anim.path = NULL;
    }
    state->pending_anim.set = false;
}

// Reset Label fonts to baked before LVGL tree deletion (label.c destructor omits unload)
static void js_display_reset_child_label_fonts(lv_obj_t* parent) {
    uint32_t count = lv_obj_get_child_count(parent);
    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if(lv_obj_check_type(child, &label_lvgl_class)) {
            label_set_font((Label*)child, FONT_BUSY_REGULAR_5);
        } else {
            js_display_reset_child_label_fonts(child);
        }
    }
}

static void js_display_free_active_label(JsActiveLabel* al) {
    // Reset to baked font before free — label.c destructor omits font_registry_unload
    label_set_font(al->label, FONT_BUSY_REGULAR_5);
    label_free(al->label);
    if(al->clip_container) {
        lv_obj_delete(al->clip_container);
    }
    free(al->prev_text);
    memset(al, 0, sizeof(JsActiveLabel));
}

static void js_display_free_active_image(JsActiveImage* ai) {
    image_free(ai->image);
    if(ai->clip_container) {
        lv_obj_delete(ai->clip_container);
    }
    free(ai->prev_path);
    memset(ai, 0, sizeof(JsActiveImage));
}

static void js_apply_label_long_mode(Label* label, Widget* base, JsPendingText* text) {
    if(text->long_mode_set) {
        if(text->width > 0) {
            widget_set_width(base, (int32_t)text->width);
            label_set_long_content_mode(label, text->long_mode);
        } else {
            label_set_long_content_mode(label, LabelLongContentModeWrap);
            widget_set_width(base, LV_SIZE_CONTENT);
        }
    } else {
        label_set_long_content_mode(label, LabelLongContentModeWrap);
        if(text->width > 0) {
            widget_set_width(base, (int32_t)text->width);
        } else {
            widget_set_width(base, LV_SIZE_CONTENT);
        }
    }
}

static void js_display_create_active_label(
    JsRunner* runner,
    JsActiveLabel* al,
    JsPendingText* text) {
    Widget* root = gui_layer_get_root_widget(runner->gui_layer, text->display);
    Widget* parent = root;
    lv_obj_t* clip_cont = NULL;

    if(text->has_clip) {
        clip_cont = lv_obj_create(TO_LV_OBJ(root));
        lv_obj_set_style_bg_opa(clip_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(clip_cont, 0, 0);
        lv_obj_set_style_pad_all(clip_cont, 0, 0);
        lv_obj_set_pos(clip_cont, text->clip_x, text->clip_y);
        lv_obj_set_size(clip_cont, text->clip_w, text->clip_h);
        lv_obj_clear_flag(clip_cont, LV_OBJ_FLAG_SCROLLABLE);
        parent = (Widget*)clip_cont;
    }

    Label* label = label_alloc(parent);
    if(!label) {
        if(clip_cont) lv_obj_delete(clip_cont);
        memset(al, 0, sizeof(JsActiveLabel));
        return;
    }
    label_set_text(label, text->text);
    label_set_font(label, text->font);
    label_set_text_color(label, text->color);

    Widget* base = label_get_base(label);
    widget_set_align(base, text->align);
    if(text->has_clip) {
        widget_set_pos(base, text->x - text->clip_x, text->y - text->clip_y);
    } else {
        if(text->x || text->y) widget_set_pos(base, text->x, text->y);
    }

    js_apply_label_long_mode(label, base, text);

    if(text->rotation != 0) {
        lv_obj_t* obj = TO_LV_OBJ(base);
        lv_obj_set_style_transform_pivot_x(obj, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(obj, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(obj, text->rotation, 0);
    }

    if(text->opacity < 255) {
        lv_obj_set_style_opa(TO_LV_OBJ(base), text->opacity, 0);
    }

    al->label = label;
    al->display = text->display;
    al->clip_container = clip_cont;
    al->prev_text = strdup(text->text);
    strncpy(al->prev_font_path, text->font, sizeof(al->prev_font_path) - 1);
    al->prev_font_path[sizeof(al->prev_font_path) - 1] = '\0';
    al->prev_color = text->color;
    al->prev_align = text->align;
    al->prev_has_clip = text->has_clip;
    al->prev_clip_x = text->clip_x;
    al->prev_clip_y = text->clip_y;
    al->prev_clip_w = text->clip_w;
    al->prev_clip_h = text->clip_h;
    al->prev_long_mode_set = text->long_mode_set;
    al->prev_long_mode = text->long_mode;
    al->prev_scroll_duration_ms = text->scroll_duration_ms;
    al->prev_fixed_width = text->width;
}

static void js_display_update_active_label(JsActiveLabel* al, JsPendingText* text) {
    bool text_changed = !al->prev_text || strcmp(al->prev_text, text->text) != 0;
    if(text_changed) {
        label_set_text(al->label, text->text);
        free(al->prev_text);
        al->prev_text = strdup(text->text);
    }

    bool font_changed = strcmp(al->prev_font_path, text->font) != 0;
    if(font_changed) {
        label_set_font(al->label, text->font);
        strncpy(al->prev_font_path, text->font, sizeof(al->prev_font_path) - 1);
        al->prev_font_path[sizeof(al->prev_font_path) - 1] = '\0';
    }

    if(memcmp(&al->prev_color, &text->color, sizeof(Color)) != 0) {
        label_set_text_color(al->label, text->color);
        al->prev_color = text->color;
    }

    Widget* base = label_get_base(al->label);

    if(al->prev_align != text->align) {
        widget_set_align(base, text->align);
        al->prev_align = text->align;
    }

    if(text->has_clip) {
        widget_set_pos(base, text->x - text->clip_x, text->y - text->clip_y);
    } else {
        widget_set_pos(base, text->x, text->y);
    }

    bool long_changed = al->prev_long_mode_set != text->long_mode_set ||
        al->prev_long_mode != text->long_mode ||
        al->prev_scroll_duration_ms != text->scroll_duration_ms ||
        al->prev_fixed_width != text->width;

    if(text_changed || font_changed || long_changed) {
        js_apply_label_long_mode(al->label, base, text);
        al->prev_long_mode_set = text->long_mode_set;
        al->prev_long_mode = text->long_mode;
        al->prev_scroll_duration_ms = text->scroll_duration_ms;
        al->prev_fixed_width = text->width;
    }

    lv_obj_t* obj = TO_LV_OBJ(base);
    lv_obj_set_style_transform_rotation(obj, text->rotation, 0);
    if(text->rotation != 0) {
        lv_obj_set_style_transform_pivot_x(obj, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(obj, LV_PCT(50), 0);
    }

    lv_obj_set_style_opa(obj, text->opacity < 255 ? text->opacity : LV_OPA_COVER, 0);
}

static void js_display_create_active_image(
    JsRunner* runner,
    JsActiveImage* ai,
    JsPendingImage* img) {
    Widget* root = gui_layer_get_root_widget(runner->gui_layer, img->display);
    Widget* parent = root;
    lv_obj_t* clip_cont = NULL;

    if(img->has_clip) {
        clip_cont = lv_obj_create(TO_LV_OBJ(root));
        lv_obj_set_style_bg_opa(clip_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(clip_cont, 0, 0);
        lv_obj_set_style_pad_all(clip_cont, 0, 0);
        lv_obj_set_pos(clip_cont, img->clip_x, img->clip_y);
        lv_obj_set_size(clip_cont, img->clip_w, img->clip_h);
        lv_obj_clear_flag(clip_cont, LV_OBJ_FLAG_SCROLLABLE);
        parent = (Widget*)clip_cont;
    }

    Image* widget = image_alloc(parent);
    if(!widget) {
        if(clip_cont) lv_obj_delete(clip_cont);
        memset(ai, 0, sizeof(JsActiveImage));
        return;
    }
    image_set_source(widget, img->path);
    Widget* base = image_get_base(widget);
    widget_set_align(base, AlignTopLeft);
    if(img->has_clip) {
        widget_set_pos(base, img->x - img->clip_x, img->y - img->clip_y);
    } else {
        if(img->x || img->y) widget_set_pos(base, img->x, img->y);
    }
    if(img->opacity < 255) {
        lv_obj_set_style_opa(TO_LV_OBJ(base), img->opacity, 0);
    }
    if(img->scale != 256) {
        image_set_scale(widget, (uint32_t)img->scale);
    }

    ai->image = widget;
    ai->display = img->display;
    ai->clip_container = clip_cont;
    ai->prev_path = strdup(img->path);
    ai->prev_has_clip = img->has_clip;
    ai->prev_clip_x = img->clip_x;
    ai->prev_clip_y = img->clip_y;
    ai->prev_clip_w = img->clip_w;
    ai->prev_clip_h = img->clip_h;
}

static void js_display_update_active_image(JsActiveImage* ai, JsPendingImage* img) {
    if(!ai->prev_path || strcmp(ai->prev_path, img->path) != 0) {
        image_set_source(ai->image, img->path);
        free(ai->prev_path);
        ai->prev_path = strdup(img->path);
    }

    Widget* base = image_get_base(ai->image);
    if(img->has_clip) {
        widget_set_pos(base, img->x - img->clip_x, img->y - img->clip_y);
    } else {
        widget_set_pos(base, img->x, img->y);
    }

    lv_obj_set_style_opa(
        TO_LV_OBJ(base), img->opacity < 255 ? img->opacity : LV_OPA_COVER, 0);

    if(img->scale != 256) {
        image_set_scale(ai->image, (uint32_t)img->scale);
    } else {
        image_set_scale(ai->image, 256);
    }
}

static const char* js_display_get_str_prop(JSContext* ctx, JSValueConst obj, const char* prop) {
    JSValue val = JS_GetPropertyStr(ctx, obj, prop);
    if(JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        JS_FreeValue(ctx, val);
        return str;
    }
    JS_FreeValue(ctx, val);
    return NULL;
}

static uint32_t js_display_hash_u32(uint32_t hash, uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

static GuiDisplayId js_display_parse_display(JSContext* ctx, JSValueConst options) {
    GuiDisplayId display = GuiDisplayIdFront;
    if(!JS_IsObject(options)) return display;
    const char* name = js_display_get_str_prop(ctx, options, "display");
    if(name) {
        if(strcmp(name, "back") == 0) display = GuiDisplayIdBack;
        JS_FreeCString(ctx, name);
    }
    return display;
}

static void js_display_parse_color(JSContext* ctx, JSValueConst options, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = 255;
    *g = 255;
    *b = 255;
    if(!JS_IsObject(options)) return;

    const char* hex = js_display_get_str_prop(ctx, options, "color");
    if(!hex) return;

    if(hex[0] == '#' && strlen(hex) >= 7) {
        unsigned int value = 0;
        if(sscanf(hex + 1, "%06x", &value) == 1) {
            *r = (value >> 16) & 0xFF;
            *g = (value >> 8) & 0xFF;
            *b = value & 0xFF;
        }
    }
    JS_FreeCString(ctx, hex);
}

static const char* js_display_parse_font(
    JSContext* ctx,
    JSValueConst options,
    char* custom_buf,
    size_t buf_size) {
    UNUSED(custom_buf);
    UNUSED(buf_size);
    const char* font = FONT_BUSY_REGULAR_5;
    if(!JS_IsObject(options)) return font;

    const char* name = js_display_get_str_prop(ctx, options, "font");
    if(!name) return font;

    if(strcmp(name, "small") == 0) font = FONT_BUSY_REGULAR_5;
    else if(strcmp(name, "medium") == 0) font = FONT_BUSY_REGULAR_7;
    else if(strcmp(name, "big") == 0) font = FONT_BUSY_REGULAR_9;
    else if(strcmp(name, "bold_7") == 0) font = FONT_BUSY_BOLD_7;
    else if(strcmp(name, "bold_10") == 0) font = FONT_BUSY_BOLD_10;
    else if(strcmp(name, "condensed_7") == 0) font = FONT_BUSY_CONDENSED_7;
    else if(strcmp(name, "regular_14") == 0) font = FONT_BUSY_REGULAR_14;
    else if(strcmp(name, "superscript_7") == 0) font = FONT_BUSY_SUPERSCRIPT_7;

    JS_FreeCString(ctx, name);
    return font;
}

static Align js_display_parse_align(JSContext* ctx, JSValueConst options) {
    Align align = AlignCenter;
    if(!JS_IsObject(options)) return align;

    const char* name = js_display_get_str_prop(ctx, options, "align");
    if(!name) return align;

    if(strcmp(name, "left") == 0) align = AlignTopLeft;
    else if(strcmp(name, "right") == 0) align = AlignTopRight;
    JS_FreeCString(ctx, name);
    return align;
}

static int16_t js_display_get_i16(JSContext* ctx, JSValueConst object, const char* field_name, int16_t default_value) {
    if(!JS_IsObject(object)) return default_value;
    JSValue field = JS_GetPropertyStr(ctx, object, field_name);
    if(JS_IsNumber(field)) {
        int32_t val;
        JS_ToInt32(ctx, &val, field);
        JS_FreeValue(ctx, field);
        return (int16_t)val;
    }
    JS_FreeValue(ctx, field);
    return default_value;
}

static size_t js_display_get_size_t(JSContext* ctx, JSValueConst object, const char* field_name, size_t default_value) {
    if(!JS_IsObject(object)) return default_value;
    JSValue field = JS_GetPropertyStr(ctx, object, field_name);
    if(JS_IsNumber(field)) {
        int32_t val;
        JS_ToInt32(ctx, &val, field);
        JS_FreeValue(ctx, field);
        return (size_t)val;
    }
    JS_FreeValue(ctx, field);
    return default_value;
}

static bool js_display_get_bool(JSContext* ctx, JSValueConst object, const char* field_name, bool default_value) {
    if(!JS_IsObject(object)) return default_value;
    JSValue field = JS_GetPropertyStr(ctx, object, field_name);
    if(JS_IsBool(field)) {
        bool val = JS_ToBool(ctx, field) != 0;
        JS_FreeValue(ctx, field);
        return val;
    }
    JS_FreeValue(ctx, field);
    return default_value;
}

static double js_display_get_double(JSContext* ctx, JSValueConst val) {
    double d = 0;
    JS_ToFloat64(ctx, &d, val);
    return d;
}

static float js_display_get_float(JSContext* ctx, JSValueConst object, const char* field_name, float default_value) {
    if(!JS_IsObject(object)) return default_value;
    JSValue field = JS_GetPropertyStr(ctx, object, field_name);
    if(JS_IsNumber(field)) {
        double val;
        JS_ToFloat64(ctx, &val, field);
        JS_FreeValue(ctx, field);
        return (float)val;
    }
    JS_FreeValue(ctx, field);
    return default_value;
}

static void js_display_parse_clip(JSContext* ctx, JSValueConst options, bool* has_clip, int16_t* cx, int16_t* cy, int16_t* cw, int16_t* ch) {
    *has_clip = false;
    if(!JS_IsObject(options)) return;
    JSValue clip = JS_GetPropertyStr(ctx, options, "clip");
    if(JS_IsObject(clip)) {
        *has_clip = true;
        *cx = js_display_get_i16(ctx, clip, "x", 0);
        *cy = js_display_get_i16(ctx, clip, "y", 0);
        *cw = js_display_get_i16(ctx, clip, "w", 72);
        *ch = js_display_get_i16(ctx, clip, "h", 16);
    }
    JS_FreeValue(ctx, clip);
}

static void js_display_parse_long_mode(JSContext* ctx, JSValueConst options, JsPendingText* item) {
    item->long_mode_set = false;
    item->long_mode = LabelLongContentModeScroll;
    item->scroll_duration_ms = 4000;
    if(!JS_IsObject(options)) return;

    const char* lm = js_display_get_str_prop(ctx, options, "longMode");
    if(!lm) return;

    if(strcmp(lm, "scroll") == 0) {
        item->long_mode_set = true;
        item->long_mode = LabelLongContentModeScroll;
    } else if(strcmp(lm, "scroll_circular") == 0) {
        item->long_mode_set = true;
        item->long_mode = LabelLongContentModeScrollCircular;
    } else if(strcmp(lm, "dots") == 0) {
        item->long_mode_set = true;
        item->long_mode = LabelLongContentModeDots;
    } else if(strcmp(lm, "clip") == 0) {
        item->long_mode_set = true;
        item->long_mode = LabelLongContentModeClip;
    }
    JS_FreeCString(ctx, lm);

    if(item->long_mode_set) {
        JSValue d = JS_GetPropertyStr(ctx, options, "scrollDurationMs");
        if(JS_IsNumber(d)) {
            int32_t dur = 0;
            JS_ToInt32(ctx, &dur, d);
            if(dur > 0) item->scroll_duration_ms = (uint32_t)dur;
        }
        JS_FreeValue(ctx, d);
    }
}

static JSValue js_display_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "text expects a string");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->count >= JS_DISPLAY_MAX_PENDING) {
        return JS_ThrowRangeError(ctx, "too many text elements");
    }

    size_t text_len = 0;
    const char* text = JS_ToCStringLen(ctx, &text_len, argv[0]);
    if(!text) return JS_EXCEPTION;

    JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;

    JsPendingText* item = &state->items[state->count++];
    item->text = malloc(text_len + 1);
    memcpy(item->text, text, text_len);
    item->text[text_len] = '\0';
    JS_FreeCString(ctx, text);

    item->font = js_display_parse_font(ctx, options, item->custom_font_path, sizeof(item->custom_font_path));
    item->color = (Color)COLOR_MAKE_HEXA(0xFFFFFFFF);
    item->x = js_display_get_i16(ctx, options, "x", 0);
    item->y = js_display_get_i16(ctx, options, "y", 0);
    item->align = js_display_parse_align(ctx, options);
    item->display = js_display_parse_display(ctx, options);
    item->width = js_display_get_size_t(ctx, options, "width", 0);
    item->rotation = (int16_t)(js_display_get_i16(ctx, options, "rotation", 0) * 10);
    item->z = js_display_get_i16(ctx, options, "z", 0);

    float opa_f = js_display_get_float(ctx, options, "opacity", 1.0f);
    if(opa_f < 0.0f) opa_f = 0.0f;
    if(opa_f > 1.0f) opa_f = 1.0f;
    item->opacity = (uint8_t)(opa_f * 255.0f);

    js_display_parse_clip(ctx, options, &item->has_clip, &item->clip_x, &item->clip_y, &item->clip_w, &item->clip_h);

    uint8_t r, g, b;
    js_display_parse_color(ctx, options, &r, &g, &b);
    item->color = (Color)COLOR_MAKE_RGB(r, g, b);

    js_display_parse_long_mode(ctx, options, item);

    return JS_UNDEFINED;
}

static JSValue js_display_pixel(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2 || !JS_IsNumber(argv[0]) || !JS_IsNumber(argv[1])) {
        return JS_ThrowTypeError(ctx, "pixel expects x and y");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->gfx_count >= JS_DISPLAY_MAX_PENDING_GFX) {
        return JS_ThrowRangeError(ctx, "too many graphics");
    }

    JSValueConst options = argc > 2 ? argv[2] : JS_UNDEFINED;
    JsPendingGfx* gfx = &state->gfx[state->gfx_count++];
    memset(gfx, 0, sizeof(JsPendingGfx));
    gfx->type = JsGfxTypePixel;
    gfx->x1 = (float)js_display_get_double(ctx, argv[0]);
    gfx->y1 = (float)js_display_get_double(ctx, argv[1]);
    gfx->display = js_display_parse_display(ctx, options);
    js_display_parse_color(ctx, options, &gfx->cr, &gfx->cg, &gfx->cb);
    return JS_UNDEFINED;
}

static JSValue js_display_rect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 4) {
        return JS_ThrowTypeError(ctx, "rect expects x, y, w, h");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->gfx_count >= JS_DISPLAY_MAX_PENDING_GFX) {
        return JS_ThrowRangeError(ctx, "too many graphics");
    }

    JSValueConst options = argc > 4 ? argv[4] : JS_UNDEFINED;
    JsPendingGfx* gfx = &state->gfx[state->gfx_count++];
    memset(gfx, 0, sizeof(JsPendingGfx));
    gfx->type = JsGfxTypeRect;
    gfx->x1 = (float)js_display_get_double(ctx, argv[0]);
    gfx->y1 = (float)js_display_get_double(ctx, argv[1]);
    int32_t w, h;
    JS_ToInt32(ctx, &w, argv[2]);
    JS_ToInt32(ctx, &h, argv[3]);
    gfx->w = (int16_t)w;
    gfx->h = (int16_t)h;
    gfx->filled = js_display_get_bool(ctx, options, "filled", true);
    gfx->display = js_display_parse_display(ctx, options);
    js_display_parse_color(ctx, options, &gfx->cr, &gfx->cg, &gfx->cb);
    return JS_UNDEFINED;
}

static JSValue js_display_line(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 4) {
        return JS_ThrowTypeError(ctx, "line expects x1, y1, x2, y2");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->gfx_count >= JS_DISPLAY_MAX_PENDING_GFX) {
        return JS_ThrowRangeError(ctx, "too many graphics");
    }

    JSValueConst options = argc > 4 ? argv[4] : JS_UNDEFINED;
    JsPendingGfx* gfx = &state->gfx[state->gfx_count++];
    memset(gfx, 0, sizeof(JsPendingGfx));
    gfx->type = JsGfxTypeLine;
    gfx->x1 = (float)js_display_get_double(ctx, argv[0]);
    gfx->y1 = (float)js_display_get_double(ctx, argv[1]);
    gfx->x2 = (float)js_display_get_double(ctx, argv[2]);
    gfx->y2 = (float)js_display_get_double(ctx, argv[3]);
    gfx->display = js_display_parse_display(ctx, options);
    js_display_parse_color(ctx, options, &gfx->cr, &gfx->cg, &gfx->cb);
    return JS_UNDEFINED;
}

static JSValue js_display_circle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 3) {
        return JS_ThrowTypeError(ctx, "circle expects x, y, radius");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->gfx_count >= JS_DISPLAY_MAX_PENDING_GFX) {
        return JS_ThrowRangeError(ctx, "too many graphics");
    }

    JSValueConst options = argc > 3 ? argv[3] : JS_UNDEFINED;
    JsPendingGfx* gfx = &state->gfx[state->gfx_count++];
    memset(gfx, 0, sizeof(JsPendingGfx));
    gfx->type = JsGfxTypeCircle;
    gfx->x1 = (float)js_display_get_double(ctx, argv[0]);
    gfx->y1 = (float)js_display_get_double(ctx, argv[1]);
    int32_t r;
    JS_ToInt32(ctx, &r, argv[2]);
    gfx->r = (int16_t)r;
    gfx->filled = js_display_get_bool(ctx, options, "filled", true);
    gfx->display = js_display_parse_display(ctx, options);
    js_display_parse_color(ctx, options, &gfx->cr, &gfx->cg, &gfx->cb);
    return JS_UNDEFINED;
}

static JSValue js_display_image(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "image expects a relative file name");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->image_count >= JS_DISPLAY_MAX_PENDING_IMAGES) {
        return JS_ThrowRangeError(ctx, "too many images");
    }

    size_t name_len = 0;
    const char* file_name = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if(!file_name) return JS_EXCEPTION;

    JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%.*s", runner->script_dir, (int)name_len, file_name);
    JS_FreeCString(ctx, file_name);

    JsPendingImage* image = &state->images[state->image_count++];
    image->path = strdup(full_path);
    image->x = js_display_get_i16(ctx, options, "x", 0);
    image->y = js_display_get_i16(ctx, options, "y", 0);
    image->display = js_display_parse_display(ctx, options);
    image->background = js_display_get_bool(ctx, options, "background", false);
    image->z = js_display_get_i16(ctx, options, "z", 0);

    float opa_f = js_display_get_float(ctx, options, "opacity", 1.0f);
    if(opa_f < 0.0f) opa_f = 0.0f;
    if(opa_f > 1.0f) opa_f = 1.0f;
    image->opacity = (uint8_t)(opa_f * 255.0f);

    float scale_f = js_display_get_float(ctx, options, "scale", 1.0f);
    image->scale = (int32_t)(scale_f * 256.0f);

    js_display_parse_clip(ctx, options, &image->has_clip, &image->clip_x, &image->clip_y, &image->clip_w, &image->clip_h);

    return JS_UNDEFINED;
}

static JSValue js_display_anim(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "anim expects a relative file name");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);

    size_t name_len = 0;
    const char* file_name = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if(!file_name) return JS_EXCEPTION;

    JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%.*s", runner->script_dir, (int)name_len, file_name);
    JS_FreeCString(ctx, file_name);

    if(state->pending_anim.path) free(state->pending_anim.path);
    state->pending_anim.path = strdup(full_path);
    state->pending_anim.x = js_display_get_i16(ctx, options, "x", 0);
    state->pending_anim.y = js_display_get_i16(ctx, options, "y", 0);
    state->pending_anim.z = js_display_get_i16(ctx, options, "z", 0);
    state->pending_anim.display = js_display_parse_display(ctx, options);
    state->pending_anim.set = true;

    float opa_f = js_display_get_float(ctx, options, "opacity", 1.0f);
    if(opa_f < 0.0f) opa_f = 0.0f;
    if(opa_f > 1.0f) opa_f = 1.0f;
    state->pending_anim.opacity = (uint8_t)(opa_f * 255.0f);

    float scale_f = js_display_get_float(ctx, options, "scale", 1.0f);
    state->pending_anim.scale = (int32_t)(scale_f * 256.0f);

    return JS_UNDEFINED;
}

static uint8_t js_display_parse_flex_align(JSContext* ctx, JSValueConst options, const char* prop, uint8_t def) {
    const char* name = js_display_get_str_prop(ctx, options, prop);
    if(!name) return def;
    uint8_t result = def;
    if(strcmp(name, "start") == 0) result = FlexLayoutAlignStart;
    else if(strcmp(name, "end") == 0) result = FlexLayoutAlignEnd;
    else if(strcmp(name, "center") == 0) result = FlexLayoutAlignCenter;
    else if(strcmp(name, "space-evenly") == 0) result = FlexLayoutAlignSpaceEvenly;
    else if(strcmp(name, "space-around") == 0) result = FlexLayoutAlignSpaceAround;
    else if(strcmp(name, "space-between") == 0) result = FlexLayoutAlignSpaceBetween;
    JS_FreeCString(ctx, name);
    return result;
}

static JSValue js_flex_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "flex.text expects a string");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);

    JSValue idx_val = JS_GetPropertyStr(ctx, this_val, "__idx");
    int32_t idx;
    JS_ToInt32(ctx, &idx, idx_val);
    JS_FreeValue(ctx, idx_val);

    if(idx < 0 || idx >= (int32_t)state->flex_count) {
        return JS_ThrowRangeError(ctx, "invalid flex index");
    }
    JsPendingFlex* flex = &state->flexes[idx];
    if(flex->child_count >= JS_FLEX_MAX_CHILDREN) {
        return JS_ThrowRangeError(ctx, "too many flex children");
    }

    size_t text_len = 0;
    const char* text = JS_ToCStringLen(ctx, &text_len, argv[0]);
    if(!text) return JS_EXCEPTION;

    JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;

    JsFlexChild* child = &flex->children[flex->child_count++];
    memset(child, 0, sizeof(JsFlexChild));
    child->type = JsFlexChildText;
    child->text = malloc(text_len + 1);
    memcpy(child->text, text, text_len);
    child->text[text_len] = '\0';
    JS_FreeCString(ctx, text);

    child->font = js_display_parse_font(ctx, options, child->custom_font_path, sizeof(child->custom_font_path));
    child->width = js_display_get_size_t(ctx, options, "width", 0);
    child->grow = (uint8_t)js_display_get_i16(ctx, options, "grow", 0);

    uint8_t r, g, b;
    js_display_parse_color(ctx, options, &r, &g, &b);
    child->color = (Color)COLOR_MAKE_RGB(r, g, b);

    return JS_UNDEFINED;
}

static JSValue js_flex_image(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if(argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "flex.image expects a file name");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);

    JSValue idx_val = JS_GetPropertyStr(ctx, this_val, "__idx");
    int32_t idx;
    JS_ToInt32(ctx, &idx, idx_val);
    JS_FreeValue(ctx, idx_val);

    if(idx < 0 || idx >= (int32_t)state->flex_count) {
        return JS_ThrowRangeError(ctx, "invalid flex index");
    }
    JsPendingFlex* flex = &state->flexes[idx];
    if(flex->child_count >= JS_FLEX_MAX_CHILDREN) {
        return JS_ThrowRangeError(ctx, "too many flex children");
    }

    size_t name_len = 0;
    const char* file_name = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if(!file_name) return JS_EXCEPTION;

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%.*s", runner->script_dir, (int)name_len, file_name);
    JS_FreeCString(ctx, file_name);

    JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;

    JsFlexChild* child = &flex->children[flex->child_count++];
    memset(child, 0, sizeof(JsFlexChild));
    child->type = JsFlexChildImage;
    child->path = strdup(full_path);
    child->grow = (uint8_t)js_display_get_i16(ctx, options, "grow", 0);

    return JS_UNDEFINED;
}

static JSValue js_flex_flex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static JSValue js_flex_create_pending(JSContext* ctx, JSValueConst options, int16_t parent_idx) {
    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    if(state->flex_count >= JS_DISPLAY_MAX_FLEX) {
        return JS_ThrowRangeError(ctx, "too many flex containers");
    }

    JsPendingFlex* flex = &state->flexes[state->flex_count];
    memset(flex, 0, sizeof(JsPendingFlex));
    flex->parent_flex_idx = parent_idx;

    const char* flow_str = js_display_get_str_prop(ctx, options, "flow");
    flex->flow = 0;
    if(flow_str) {
        if(strcmp(flow_str, "column") == 0) flex->flow = 1;
        JS_FreeCString(ctx, flow_str);
    }

    flex->x = js_display_get_i16(ctx, options, "x", 0);
    flex->y = js_display_get_i16(ctx, options, "y", 0);
    flex->width = js_display_get_i16(ctx, options, "width", 0);
    flex->height = js_display_get_i16(ctx, options, "height", 0);
    flex->spacing = (int32_t)js_display_get_i16(ctx, options, "spacing", 0);
    flex->padding = js_display_get_i16(ctx, options, "padding", 0);
    flex->z = js_display_get_i16(ctx, options, "z", 0);
    flex->main_align = js_display_parse_flex_align(ctx, options, "align", FlexLayoutAlignStart);
    flex->cross_align = js_display_parse_flex_align(ctx, options, "crossAlign", FlexLayoutAlignStart);

    if(parent_idx < 0) {
        flex->display = js_display_parse_display(ctx, options);
    } else {
        flex->display = state->flexes[parent_idx].display;
    }

    int32_t flex_idx = (int32_t)state->flex_count;
    state->flex_count++;

    JSValue flex_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, flex_obj, "__idx", JS_NewInt32(ctx, flex_idx));
    JS_SetPropertyStr(ctx, flex_obj, "text", JS_NewCFunction(ctx, js_flex_text, "text", 1));
    JS_SetPropertyStr(ctx, flex_obj, "image", JS_NewCFunction(ctx, js_flex_image, "image", 1));
    JS_SetPropertyStr(ctx, flex_obj, "flex", JS_NewCFunction(ctx, js_flex_flex, "flex", 1));
    return flex_obj;
}

static JSValue js_flex_flex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);

    JSValue idx_val = JS_GetPropertyStr(ctx, this_val, "__idx");
    int32_t parent_idx;
    JS_ToInt32(ctx, &parent_idx, idx_val);
    JS_FreeValue(ctx, idx_val);

    if(parent_idx < 0 || parent_idx >= (int32_t)state->flex_count) {
        return JS_ThrowRangeError(ctx, "invalid flex index");
    }

    JSValueConst options = argc > 0 ? argv[0] : JS_UNDEFINED;
    return js_flex_create_pending(ctx, options, (int16_t)parent_idx);
}

static JSValue js_display_flex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    JSValueConst options = argc > 0 ? argv[0] : JS_UNDEFINED;
    return js_flex_create_pending(ctx, options, -1);
}

static void js_display_parse_named_color(
    JSContext* ctx,
    JSValueConst options,
    const char* prop,
    uint8_t* r,
    uint8_t* g,
    uint8_t* b) {
    *r = 0;
    *g = 0;
    *b = 0;
    if(!JS_IsObject(options)) return;
    const char* hex = js_display_get_str_prop(ctx, options, prop);
    if(!hex) return;
    if(hex[0] == '#' && strlen(hex) >= 7) {
        unsigned int value = 0;
        if(sscanf(hex + 1, "%06x", &value) == 1) {
            *r = (value >> 16) & 0xFF;
            *g = (value >> 8) & 0xFF;
            *b = value & 0xFF;
        }
    }
    JS_FreeCString(ctx, hex);
}

static JSValue js_display_graph(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    if(argc < 2 || !JS_IsObject(argv[0]) || !JS_IsObject(argv[1])) {
        return JS_ThrowTypeError(ctx, "graph expects (values[], options)");
    }

    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    JsPendingGraph* g = &state->graph;
    memset(g, 0, sizeof(JsPendingGraph));

    JSValue len_val = JS_GetPropertyStr(ctx, argv[0], "length");
    int32_t len = 0;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    if(len > JS_GRAPH_MAX_VALUES) len = JS_GRAPH_MAX_VALUES;
    if(len < 2) return JS_ThrowRangeError(ctx, "graph needs at least 2 values");

    for(int32_t i = 0; i < len; i++) {
        JSValue el = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
        double v = 0;
        JS_ToFloat64(ctx, &v, el);
        JS_FreeValue(ctx, el);
        g->values[i] = (float)v;
    }
    g->count = (uint8_t)len;

    JSValueConst opts = argv[1];
    g->x = js_display_get_i16(ctx, opts, "x", 0);
    g->y = js_display_get_i16(ctx, opts, "y", 6);
    g->width = js_display_get_i16(ctx, opts, "width", 72);
    g->height = js_display_get_i16(ctx, opts, "height", 10);
    g->cursor_x = js_display_get_i16(ctx, opts, "cursorX", -1);
    g->min_curve_y = js_display_get_i16(ctx, opts, "minCurveY", 8);

    double tmin_d = 0, tmax_d = 0;
    JSValue v_tmin = JS_GetPropertyStr(ctx, opts, "tempMin");
    if(JS_IsNumber(v_tmin)) JS_ToFloat64(ctx, &tmin_d, v_tmin);
    JS_FreeValue(ctx, v_tmin);
    JSValue v_tmax = JS_GetPropertyStr(ctx, opts, "tempMax");
    if(JS_IsNumber(v_tmax)) JS_ToFloat64(ctx, &tmax_d, v_tmax);
    JS_FreeValue(ctx, v_tmax);
    g->temp_min = (float)tmin_d;
    g->temp_max = (float)tmax_d;

    g->display = js_display_parse_display(ctx, opts);
    g->z = js_display_get_i16(ctx, opts, "z", 0);

    js_display_parse_named_color(ctx, opts, "colorTop", &g->color_top_r, &g->color_top_g, &g->color_top_b);
    js_display_parse_named_color(ctx, opts, "colorBottom", &g->color_bot_r, &g->color_bot_g, &g->color_bot_b);
    js_display_parse_named_color(ctx, opts, "cursorColor", &g->cursor_r, &g->cursor_g, &g->cursor_b);

    const char* border_hex = js_display_get_str_prop(ctx, opts, "borderColor");
    if(border_hex) {
        g->has_border = true;
        if(border_hex[0] == '#' && strlen(border_hex) >= 7) {
            unsigned int bval = 0;
            if(sscanf(border_hex + 1, "%06x", &bval) == 1) {
                g->border_r = (bval >> 16) & 0xFF;
                g->border_g = (bval >> 8) & 0xFF;
                g->border_b = bval & 0xFF;
            }
        }
        JS_FreeCString(ctx, border_hex);
    }

    g->reveal_x = js_display_get_float(ctx, opts, "revealX", -1.0f);
    g->reveal_fade = js_display_get_i16(ctx, opts, "revealFade", 0);
    g->reveal_slide_y = js_display_get_float(ctx, opts, "revealSlideY", 0.0f);

    g->active = true;
    return JS_UNDEFINED;
}

static JSValue js_display_clear(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    JsRunner* runner = JS_GetContextOpaque(ctx);
    JsDisplayState* state = js_display_get_state(runner);
    js_display_pending_clear(state);
    return JS_UNDEFINED;
}

static void js_display_destroy_canvas(JsDisplayState* state) {
    if(state->canvas_front) {
        lv_obj_delete(state->canvas_front);
        state->canvas_front = NULL;
    }
    if(state->canvas_back) {
        lv_obj_delete(state->canvas_back);
        state->canvas_back = NULL;
    }
    if(state->canvas_front_buf) {
        lv_draw_buf_destroy(state->canvas_front_buf);
        state->canvas_front_buf = NULL;
    }
    if(state->canvas_back_buf) {
        lv_draw_buf_destroy(state->canvas_back_buf);
        state->canvas_back_buf = NULL;
    }
    state->curve_cache_valid = false;
}

static void js_display_clear_canvas_pixels(JsDisplayState* state) {
    if(state->canvas_front) {
        lv_canvas_fill_bg(state->canvas_front, lv_color_black(), LV_OPA_TRANSP);
    }
    if(state->canvas_back) {
        lv_canvas_fill_bg(state->canvas_back, lv_color_black(), LV_OPA_TRANSP);
    }
}

static lv_obj_t* js_display_ensure_canvas(
    JsRunner* runner,
    JsDisplayState* state,
    GuiDisplayId display,
    int16_t width,
    int16_t height) {
    lv_obj_t** canvas = (display == GuiDisplayIdFront) ? &state->canvas_front : &state->canvas_back;
    lv_draw_buf_t** buffer =
        (display == GuiDisplayIdFront) ? &state->canvas_front_buf : &state->canvas_back_buf;

    if(*canvas) return *canvas;

    Widget* root = gui_layer_get_root_widget(runner->gui_layer, display);
    lv_obj_t* parent = TO_LV_OBJ(root);
    *buffer = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if(!*buffer) return NULL;

    *canvas = lv_canvas_create(parent);
    if(!*canvas) {
        lv_draw_buf_destroy(*buffer);
        *buffer = NULL;
        return NULL;
    }
    lv_canvas_set_draw_buf(*canvas, *buffer);
    lv_canvas_fill_bg(*canvas, lv_color_black(), LV_OPA_TRANSP);
    return *canvas;
}

static void js_display_render_subpixel_rect(
    lv_obj_t* canvas,
    float fx,
    float fy,
    int w,
    int h,
    lv_color_t color) {
    int ix = (int)floorf(fx);
    int iy = (int)floorf(fy);
    float frac_x = fx - (float)ix;
    float frac_y = fy - (float)iy;
    bool has_fx = frac_x > 0.001f;
    bool has_fy = frac_y > 0.001f;

    int body_x = has_fx ? ix + 1 : ix;
    int body_y = has_fy ? iy + 1 : iy;
    int body_w = has_fx ? w - 1 : w;
    int body_h = has_fy ? h - 1 : h;

    if(body_w > 0 && body_h > 0) {
        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);
        lv_draw_fill_dsc_t fill;
        lv_draw_fill_dsc_init(&fill);
        fill.color = color;
        fill.opa = LV_OPA_COVER;
        lv_area_t area = {body_x, body_y, body_x + body_w - 1, body_y + body_h - 1};
        lv_draw_fill(&layer, &fill, &area);
        lv_canvas_finish_layer(canvas, &layer);
    }

    uint8_t opa_l = (uint8_t)((1.0f - frac_x) * 255);
    uint8_t opa_r = (uint8_t)(frac_x * 255);
    uint8_t opa_t = (uint8_t)((1.0f - frac_y) * 255);
    uint8_t opa_b = (uint8_t)(frac_y * 255);

    if(has_fx) {
        for(int y = body_y; y < body_y + body_h; y++) lv_canvas_set_px(canvas, ix, y, color, opa_l);
        for(int y = body_y; y < body_y + body_h; y++) lv_canvas_set_px(canvas, ix + w, y, color, opa_r);
    }

    if(has_fy) {
        for(int x = body_x; x < body_x + body_w; x++) lv_canvas_set_px(canvas, x, iy, color, opa_t);
        for(int x = body_x; x < body_x + body_w; x++) lv_canvas_set_px(canvas, x, iy + h, color, opa_b);
    }

    if(has_fx && has_fy) {
        lv_canvas_set_px(canvas, ix, iy, color, (uint8_t)((1.0f - frac_x) * (1.0f - frac_y) * 255));
        lv_canvas_set_px(canvas, ix + w, iy, color, (uint8_t)(frac_x * (1.0f - frac_y) * 255));
        lv_canvas_set_px(canvas, ix, iy + h, color, (uint8_t)((1.0f - frac_x) * frac_y * 255));
        lv_canvas_set_px(canvas, ix + w, iy + h, color, (uint8_t)(frac_x * frac_y * 255));
    }
}

static float js_graph_catmull_rom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

static void js_graph_temp_color(float temp_c, uint8_t* r, uint8_t* g, uint8_t* b) {
    float t = (temp_c + 30.0f) / 60.0f;
    if(t < 0.0f) t = 0.0f;
    if(t > 1.0f) t = 1.0f;

    static const float stops[] = {0.0f, 0.35f, 0.50f, 0.65f, 0.80f, 1.0f};
    static const uint8_t colors[][3] = {
        {0x20, 0x00, 0x8A},
        {0x67, 0xA8, 0xF5},
        {0xCF, 0xCF, 0xCF},
        {0xFF, 0xC8, 0x00},
        {0xFF, 0x88, 0x19},
        {0xFF, 0x00, 0x00},
    };

    int seg = 4;
    for(int i = 0; i < 5; i++) {
        if(t < stops[i + 1]) { seg = i; break; }
    }

    float local = (t - stops[seg]) / (stops[seg + 1] - stops[seg]);
    *r = (uint8_t)((float)colors[seg][0] + local * ((float)colors[seg + 1][0] - (float)colors[seg][0]));
    *g = (uint8_t)((float)colors[seg][1] + local * ((float)colors[seg + 1][1] - (float)colors[seg][1]));
    *b = (uint8_t)((float)colors[seg][2] + local * ((float)colors[seg + 1][2] - (float)colors[seg][2]));
}

static void js_display_render_graph(JsRunner* runner, JsDisplayState* state) {
    JsPendingGraph* g = &state->graph;
    if(!g->active || g->count < 2) return;

    int16_t w = g->width;
    int16_t h = g->height;
    if(w <= 0 || h <= 0) return;

    js_display_ensure_canvas(
        runner,
        state,
        g->display,
        (g->display == GuiDisplayIdFront) ? 72 : 160,
        (g->display == GuiDisplayIdFront) ? 16 : 80);

    lv_obj_t* canvas =
        (g->display == GuiDisplayIdFront) ? state->canvas_front : state->canvas_back;
    if(!canvas) return;

    bool cache_hit = state->curve_cache_valid &&
        state->cached_values_count == g->count &&
        state->cached_graph_width == w &&
        memcmp(state->cached_values, g->values, g->count * sizeof(float)) == 0;

    float* curve = state->cached_curve;
    if(!cache_hit) {
        int n = g->count;
        for(int x = 0; x < w; x++) {
            float pos = (float)x / (float)(w - 1) * (float)(n - 1);
            int idx = (int)pos;
            float t = pos - (float)idx;
            if(idx >= n - 1) {
                curve[x] = g->values[n - 1];
            } else {
                float p0 = g->values[(idx > 0) ? idx - 1 : 0];
                float p1 = g->values[idx];
                float p2 = g->values[idx + 1];
                float p3 = g->values[(idx + 2 < n) ? idx + 2 : n - 1];
                curve[x] = js_graph_catmull_rom(p0, p1, p2, p3, t);
            }
            if(curve[x] < 0.0f) curve[x] = 0.0f;
            if(curve[x] > 1.0f) curve[x] = 1.0f;
        }
        memcpy(state->cached_values, g->values, g->count * sizeof(float));
        state->cached_values_count = g->count;
        state->cached_graph_width = w;
        state->curve_cache_valid = true;
    }

    int max_curve_offset = h - 1;
    if(g->min_curve_y > 0 && g->min_curve_y < max_curve_offset) {
        max_curve_offset = g->min_curve_y;
    }

    int16_t* border_y_arr = state->graph_border_y;

    bool use_temp_color = (g->temp_max - g->temp_min) > 0.01f;
    uint8_t* col_r = state->graph_col_r;
    uint8_t* col_g_arr = state->graph_col_g;
    uint8_t* col_b_arr = state->graph_col_b;

    bool has_reveal = g->reveal_x >= 0.0f;
    float* reveal_col_opa = state->graph_reveal_opa;
    float* reveal_col_dy = state->graph_reveal_dy;
    if(has_reveal) {
        for(int x = 0; x < w; x++) {
            float fx = (float)x;
            if(fx >= g->reveal_x) {
                reveal_col_opa[x] = 0.0f;
                reveal_col_dy[x] = g->reveal_slide_y;
            } else if(g->reveal_fade > 0 && fx >= g->reveal_x - (float)g->reveal_fade) {
                float t = (g->reveal_x - fx) / (float)g->reveal_fade;
                reveal_col_opa[x] = t;
                reveal_col_dy[x] = g->reveal_slide_y * (1.0f - t);
            } else {
                reveal_col_opa[x] = 1.0f;
                reveal_col_dy[x] = 0.0f;
            }
        }
    }

    for(int x = 0; x < w; x++) {
        if(has_reveal && reveal_col_opa[x] <= 0.0f) continue;
        float col_opa = has_reveal ? reveal_col_opa[x] : 1.0f;
        float col_dy = has_reveal ? reveal_col_dy[x] : 0.0f;

        float v = curve[x];
        float curve_offset = (1.0f - v) * (float)max_curve_offset;
        float curve_y = (float)g->y + curve_offset + col_dy;

        int top_y = (int)curve_y;
        float frac = curve_y - (float)top_y;
        int fill_start = (frac > 0.001f) ? top_y + 1 : top_y;
        int bottom = g->y + h - 1;
        int sx = g->x + x;

        border_y_arr[x] = fill_start;

        uint8_t cr, cg, cb;
        if(use_temp_color) {
            float temp = g->temp_min + v * (g->temp_max - g->temp_min);
            js_graph_temp_color(temp, &cr, &cg, &cb);
        } else {
            cr = g->color_top_r;
            cg = g->color_top_g;
            cb = g->color_top_b;
        }
        col_r[x] = cr;
        col_g_arr[x] = cg;
        col_b_arr[x] = cb;
        lv_color_t col = lv_color_make(cr, cg, cb);

        if(frac > 0.001f && top_y >= g->y && top_y <= bottom) {
            float row_t = ((float)(top_y - g->y)) / (float)(h - 1);
            uint8_t opa = (uint8_t)((204.0f - row_t * 166.0f) * (1.0f - frac) * col_opa);
            lv_canvas_set_px(canvas, sx, top_y, col, opa);
        }

        for(int y = fill_start; y <= bottom; y++) {
            float row_t = ((float)(y - g->y)) / (float)(h - 1);
            uint8_t opa = (uint8_t)((204.0f - row_t * 166.0f) * col_opa);
            lv_canvas_set_px(canvas, sx, y, col, opa);
        }
    }

    for(int x = 0; x < w - 1; x++) {
        if(has_reveal && (reveal_col_opa[x] <= 0.0f || reveal_col_opa[x + 1] <= 0.0f)) continue;
        int y1 = border_y_arr[x];
        int y2 = border_y_arr[x + 1];
        if(y1 == y2) continue;
        int lo = (y1 < y2) ? y1 : y2;
        int hi = (y1 > y2) ? y1 : y2;
        int gap_col = (y1 > y2) ? x : x + 1;
        float gap_opa = has_reveal ? reveal_col_opa[gap_col] : 1.0f;
        int sx = g->x + gap_col;
        lv_color_t gc = lv_color_make(col_r[gap_col], col_g_arr[gap_col], col_b_arr[gap_col]);
        for(int y = lo; y < hi; y++) {
            float row_t = ((float)(y - g->y)) / (float)(h - 1);
            uint8_t opa = (uint8_t)((204.0f - row_t * 166.0f) * 0.5f * gap_opa);
            lv_canvas_set_px(canvas, sx, y, gc, opa);
        }
    }

    {
        int bottom = g->y + h - 1;
        for(int x = 0; x < w; x++) {
            if(has_reveal && reveal_col_opa[x] <= 0.0f) continue;
            float col_opa = has_reveal ? reveal_col_opa[x] : 1.0f;
            float col_dy = has_reveal ? reveal_col_dy[x] : 0.0f;

            float v = curve[x];
            float curve_offset = (1.0f - v) * (float)max_curve_offset;
            float curve_y = (float)g->y + curve_offset + col_dy;
            int top_y = (int)curve_y;
            float frac = curve_y - (float)top_y;
            int sx = g->x + x;

            uint8_t br = (uint8_t)(((int)col_r[x] * (255 - 102) + 255 * 102) / 255);
            uint8_t bg_c = (uint8_t)(((int)col_g_arr[x] * (255 - 102) + 255 * 102) / 255);
            uint8_t bb = (uint8_t)(((int)col_b_arr[x] * (255 - 102) + 255 * 102) / 255);
            lv_color_t bc = lv_color_make(br, bg_c, bb);

            if(top_y >= g->y && top_y <= bottom) {
                float row_t = ((float)(top_y - g->y)) / (float)(h - 1);
                uint8_t opa = (uint8_t)((204.0f - row_t * 166.0f) * (1.0f - frac) * col_opa);
                lv_canvas_set_px(canvas, sx, top_y, bc, opa);
            }
            int by1 = top_y + 1;
            if(frac > 0.001f && by1 >= g->y && by1 <= bottom) {
                float row_t = ((float)(by1 - g->y)) / (float)(h - 1);
                uint8_t fill_opa = (uint8_t)((204.0f - row_t * 166.0f) * col_opa);
                float blend = frac * (102.0f / 255.0f);
                uint8_t mr = (uint8_t)((float)col_r[x] * (1.0f - blend) + 255.0f * blend);
                uint8_t mg = (uint8_t)((float)col_g_arr[x] * (1.0f - blend) + 255.0f * blend);
                uint8_t mb = (uint8_t)((float)col_b_arr[x] * (1.0f - blend) + 255.0f * blend);
                lv_canvas_set_px(canvas, sx, by1, lv_color_make(mr, mg, mb), fill_opa);
            }
        }
    }

    if(g->cursor_x >= 0 && g->cursor_x < w) {
        if(!has_reveal || reveal_col_opa[g->cursor_x] > 0.0f) {
            float cursor_opa = has_reveal ? reveal_col_opa[g->cursor_x] : 1.0f;
            float cursor_dy = has_reveal ? reveal_col_dy[g->cursor_x] : 0.0f;
            int cx = g->x + g->cursor_x;
            float v = curve[g->cursor_x];
            float co = (1.0f - v) * (float)max_curve_offset;
            int dot_y = g->y + (int)(co + cursor_dy + 0.5f);
            int bottom = g->y + h - 1;

            lv_color_t cc = lv_color_make(g->cursor_r, g->cursor_g, g->cursor_b);
            for(int y = dot_y + 2; y <= bottom; y++) {
                lv_canvas_set_px(canvas, cx, y, cc, (uint8_t)(100.0f * cursor_opa));
            }

            uint8_t dot_opa = (uint8_t)(255.0f * cursor_opa);
            lv_canvas_set_px(canvas, cx, dot_y, cc, dot_opa);
            if(cx - 1 >= g->x) lv_canvas_set_px(canvas, cx - 1, dot_y, cc, dot_opa);
            if(cx + 1 < g->x + w) lv_canvas_set_px(canvas, cx + 1, dot_y, cc, dot_opa);
            if(dot_y - 1 >= g->y) lv_canvas_set_px(canvas, cx, dot_y - 1, cc, dot_opa);
            if(dot_y + 1 <= bottom) lv_canvas_set_px(canvas, cx, dot_y + 1, cc, dot_opa);

            uint8_t corner_opa = (uint8_t)(128.0f * cursor_opa);
            if(cx - 1 >= g->x && dot_y - 1 >= g->y)
                lv_canvas_set_px(canvas, cx - 1, dot_y - 1, cc, corner_opa);
            if(cx + 1 < g->x + w && dot_y - 1 >= g->y)
                lv_canvas_set_px(canvas, cx + 1, dot_y - 1, cc, corner_opa);
            if(cx - 1 >= g->x && dot_y + 1 <= bottom)
                lv_canvas_set_px(canvas, cx - 1, dot_y + 1, cc, corner_opa);
            if(cx + 1 < g->x + w && dot_y + 1 <= bottom)
                lv_canvas_set_px(canvas, cx + 1, dot_y + 1, cc, corner_opa);
        }
    }
}

static void js_display_render_gfx(JsRunner* runner, JsDisplayState* state) {
    bool need_front = false;
    bool need_back = false;
    for(size_t i = 0; i < state->gfx_count; i++) {
        if(state->gfx[i].display == GuiDisplayIdFront) {
            need_front = true;
        } else {
            need_back = true;
        }
    }

    if(need_front) js_display_ensure_canvas(runner, state, GuiDisplayIdFront, 72, 16);
    if(need_back) js_display_ensure_canvas(runner, state, GuiDisplayIdBack, 160, 80);

    for(size_t i = 0; i < state->gfx_count; i++) {
        JsPendingGfx* gfx = &state->gfx[i];
        lv_obj_t* canvas =
            (gfx->display == GuiDisplayIdFront) ? state->canvas_front : state->canvas_back;
        if(!canvas) continue;

        lv_color_t color = lv_color_make(gfx->cr, gfx->cg, gfx->cb);
        switch(gfx->type) {
        case JsGfxTypePixel:
            lv_canvas_set_px(canvas, (int)gfx->x1, (int)gfx->y1, color, LV_OPA_COVER);
            break;
        case JsGfxTypeRect: {
            float frac_x = gfx->x1 - floorf(gfx->x1);
            float frac_y = gfx->y1 - floorf(gfx->y1);
            bool subpixel = (frac_x > 0.001f) || (frac_y > 0.001f);

            if(gfx->filled && subpixel) {
                js_display_render_subpixel_rect(canvas, gfx->x1, gfx->y1, gfx->w, gfx->h, color);
            } else {
                lv_layer_t layer;
                lv_canvas_init_layer(canvas, &layer);
                int ix = (int)gfx->x1;
                int iy = (int)gfx->y1;

                if(gfx->filled) {
                    lv_draw_fill_dsc_t fill;
                    lv_draw_fill_dsc_init(&fill);
                    fill.color = color;
                    fill.opa = LV_OPA_COVER;
                    lv_area_t area = {ix, iy, ix + gfx->w - 1, iy + gfx->h - 1};
                    lv_draw_fill(&layer, &fill, &area);
                } else {
                    lv_draw_border_dsc_t border;
                    lv_draw_border_dsc_init(&border);
                    border.color = color;
                    border.opa = LV_OPA_COVER;
                    border.width = 1;
                    border.side = LV_BORDER_SIDE_FULL;
                    lv_area_t area = {ix, iy, ix + gfx->w - 1, iy + gfx->h - 1};
                    lv_draw_border(&layer, &border, &area);
                }
                lv_canvas_finish_layer(canvas, &layer);
            }
            break;
        }
        case JsGfxTypeLine: {
            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);
            lv_draw_line_dsc_t line;
            lv_draw_line_dsc_init(&line);
            line.color = color;
            line.width = 1;
            line.opa = LV_OPA_COVER;
            line.p1.x = (int)gfx->x1;
            line.p1.y = (int)gfx->y1;
            line.p2.x = (int)gfx->x2;
            line.p2.y = (int)gfx->y2;
            lv_draw_line(&layer, &line);
            lv_canvas_finish_layer(canvas, &layer);
            break;
        }
        case JsGfxTypeCircle: {
            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);
            lv_draw_arc_dsc_t arc;
            lv_draw_arc_dsc_init(&arc);
            arc.color = color;
            arc.width = gfx->filled ? (int32_t)gfx->r : 1;
            arc.opa = LV_OPA_COVER;
            arc.center.x = (int)gfx->x1;
            arc.center.y = (int)gfx->y1;
            arc.radius = (uint16_t)gfx->r;
            arc.start_angle = 0;
            arc.end_angle = 360;
            lv_draw_arc(&layer, &arc);
            lv_canvas_finish_layer(canvas, &layer);
            break;
        }
        }
    }
}

void js_display_flush(JsRunner* runner) {
    if(!runner || !runner->display_state) return;
    JsDisplayState* state = runner->display_state;
    bool has_bg = false;
    for(size_t i = 0; i < state->image_count; i++) {
        if(state->images[i].background) {
            has_bg = true;
            break;
        }
    }

    with_gui(runner->gui, {
        for(size_t i = 0; i < state->active_bg_image_count; i++) {
            image_free(state->active_bg_images[i].image);
            if(state->active_bg_images[i].clip_container) {
                lv_obj_delete(state->active_bg_images[i].clip_container);
            }
        }
        state->active_bg_image_count = 0;

        for(size_t i = 0; i < state->active_flex_count; i++) {
            js_display_reset_child_label_fonts(
                TO_LV_OBJ(flex_layout_get_base(state->active_flexes[i].layout)));
            flex_layout_free(state->active_flexes[i].layout);
        }
        state->active_flex_count = 0;

        if(state->pending_anim.set) {
            bool need_recreate = !state->active_anim ||
                !state->active_anim_path ||
                strcmp(state->active_anim_path, state->pending_anim.path) != 0 ||
                state->active_anim_display != state->pending_anim.display;

            if(need_recreate) {
                if(state->active_anim) {
                    anim_player_free(state->active_anim);
                    state->active_anim = NULL;
                }
                if(state->active_anim_path) {
                    free(state->active_anim_path);
                    state->active_anim_path = NULL;
                }

                Widget* root = gui_layer_get_root_widget(runner->gui_layer, state->pending_anim.display);
                state->active_anim = anim_player_alloc(root);
                anim_player_set_source(state->active_anim, state->pending_anim.path);
                Widget* abase = anim_player_get_base(state->active_anim);
                widget_set_align(abase, AlignTopLeft);
                if(state->pending_anim.x || state->pending_anim.y) {
                    widget_set_pos(abase, state->pending_anim.x, state->pending_anim.y);
                }
                state->active_anim_path = strdup(state->pending_anim.path);
                state->active_anim_display = state->pending_anim.display;
            } else {
                Widget* abase = anim_player_get_base(state->active_anim);
                widget_set_pos(abase, state->pending_anim.x, state->pending_anim.y);
            }

            {
                Widget* abase = anim_player_get_base(state->active_anim);
                lv_obj_t* aobj = TO_LV_OBJ(abase);
                if(state->pending_anim.opacity < 255) {
                    lv_obj_set_style_opa(aobj, state->pending_anim.opacity, 0);
                } else {
                    lv_obj_set_style_opa(aobj, LV_OPA_COVER, 0);
                }
                if(state->pending_anim.scale != 256) {
                    lv_obj_set_style_transform_scale(aobj, state->pending_anim.scale, 0);
                    lv_obj_set_style_transform_pivot_x(aobj, LV_PCT(50), 0);
                    lv_obj_set_style_transform_pivot_y(aobj, LV_PCT(50), 0);
                } else {
                    lv_obj_set_style_transform_scale(aobj, 256, 0);
                }
            }
        } else {
            if(state->active_anim) {
                anim_player_free(state->active_anim);
                state->active_anim = NULL;
                free(state->active_anim_path);
                state->active_anim_path = NULL;
            }
        }

        if(has_bg) {
            js_display_destroy_canvas(state);
        }

        if(state->gfx_count > 0 || state->graph.active) {
            js_display_clear_canvas_pixels(state);
        } else {
            js_display_destroy_canvas(state);
        }

        for(size_t i = 0; i < state->image_count && i < JS_DISPLAY_MAX_IMAGES; i++) {
            JsPendingImage* image = &state->images[i];
            if(!image->background) continue;
            Widget* root = gui_layer_get_root_widget(runner->gui_layer, image->display);
            Widget* parent = root;
            lv_obj_t* bg_clip = NULL;
            if(image->has_clip) {
                bg_clip = lv_obj_create(TO_LV_OBJ(root));
                lv_obj_set_style_bg_opa(bg_clip, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(bg_clip, 0, 0);
                lv_obj_set_style_pad_all(bg_clip, 0, 0);
                lv_obj_set_pos(bg_clip, image->clip_x, image->clip_y);
                lv_obj_set_size(bg_clip, image->clip_w, image->clip_h);
                lv_obj_clear_flag(bg_clip, LV_OBJ_FLAG_SCROLLABLE);
                parent = (Widget*)bg_clip;
            }
            Image* widget = image_alloc(parent);
            image_set_source(widget, image->path);
            Widget* base = image_get_base(widget);
            widget_set_align(base, AlignTopLeft);
            if(image->has_clip) {
                widget_set_pos(base, image->x - image->clip_x, image->y - image->clip_y);
            } else {
                if(image->x || image->y) widget_set_pos(base, image->x, image->y);
            }
            if(image->opacity < 255) {
                lv_obj_set_style_opa(TO_LV_OBJ(base), image->opacity, 0);
            }
            if(image->scale != 256) {
                image_set_scale(widget, (uint32_t)image->scale);
            }
            state->active_bg_images[state->active_bg_image_count].image = widget;
            state->active_bg_images[state->active_bg_image_count].display = image->display;
            state->active_bg_images[state->active_bg_image_count].clip_container = bg_clip;
            state->active_bg_image_count++;
        }

        if(state->gfx_count > 0) {
            js_display_render_gfx(runner, state);
        }

        if(state->graph.active) {
            js_display_render_graph(runner, state);
        }

        if(has_bg) {
            if(state->canvas_front) lv_obj_move_to_index(state->canvas_front, 0);
            if(state->canvas_back) lv_obj_move_to_index(state->canvas_back, 0);
            for(int i = (int)state->active_bg_image_count - 1; i >= 0; i--) {
                lv_obj_t* obj = state->active_bg_images[i].clip_container
                    ? state->active_bg_images[i].clip_container
                    : TO_LV_OBJ(image_get_base(state->active_bg_images[i].image));
                lv_obj_move_to_index(obj, 0);
            }
        }

        FlexLayout* created_flexes[JS_DISPLAY_MAX_FLEX];
        memset(created_flexes, 0, sizeof(created_flexes));

        for(size_t i = 0; i < state->flex_count && i < JS_DISPLAY_MAX_FLEX; i++) {
            JsPendingFlex* flex = &state->flexes[i];
            Widget* parent;
            if(flex->parent_flex_idx >= 0 && flex->parent_flex_idx < (int16_t)i &&
               created_flexes[flex->parent_flex_idx]) {
                parent = flex_layout_get_base(created_flexes[flex->parent_flex_idx]);
            } else {
                parent = gui_layer_get_root_widget(runner->gui_layer, flex->display);
            }

            FlexLayoutType type = flex->flow ? FlexLayoutTypeColumn : FlexLayoutTypeRow;
            FlexLayout* fl = flex_layout_alloc(parent, type);
            Widget* fl_base = flex_layout_get_base(fl);
            created_flexes[i] = fl;

            lv_obj_t* fl_obj = TO_LV_OBJ(fl_base);
            lv_obj_set_style_bg_opa(fl_obj, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(fl_obj, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(fl_obj, flex->padding, LV_PART_MAIN);

            widget_set_align(fl_base, AlignTopLeft);
            if(flex->x || flex->y) widget_set_pos(fl_base, flex->x, flex->y);

            if(flex->width > 0) lv_obj_set_width(fl_obj, flex->width);
            else lv_obj_set_width(fl_obj, LV_SIZE_CONTENT);
            if(flex->height > 0) lv_obj_set_height(fl_obj, flex->height);
            else lv_obj_set_height(fl_obj, LV_SIZE_CONTENT);

            if(flex->spacing) flex_layout_set_spacing(fl, flex->spacing);
            flex_layout_set_align(fl,
                (FlexLayoutAlign)flex->main_align,
                (FlexLayoutAlign)flex->cross_align,
                (FlexLayoutAlign)flex->cross_align);

            for(size_t j = 0; j < flex->child_count; j++) {
                JsFlexChild* child = &flex->children[j];
                if(child->type == JsFlexChildText) {
                    Label* label = label_alloc(fl_base);
                    label_set_text(label, child->text);
                    label_set_font(label, child->font);
                    label_set_text_color(label, child->color);
                    Widget* lbl_base = label_get_base(label);
                    if(child->width) widget_set_width(lbl_base, child->width);
                    if(child->grow) flex_layout_set_child_widget_grow(fl, lbl_base, child->grow);
                } else {
                    Image* img = image_alloc(fl_base);
                    image_set_source(img, child->path);
                    if(child->grow) {
                        Widget* img_base = image_get_base(img);
                        flex_layout_set_child_widget_grow(fl, img_base, child->grow);
                    }
                }
            }

            if(flex->parent_flex_idx < 0) {
                state->active_flexes[state->active_flex_count].layout = fl;
                state->active_flexes[state->active_flex_count].display = flex->display;
                state->active_flex_count++;
            }
        }

        JsZEntry entries[JS_DISPLAY_MAX_IMAGES + JS_DISPLAY_MAX_LABELS + JS_DISPLAY_MAX_FLEX];
        size_t entry_count = 0;

        for(size_t i = 0; i < state->image_count && i < JS_DISPLAY_MAX_IMAGES; i++) {
            if(state->images[i].background) continue;
            entries[entry_count].z = state->images[i].z;
            entries[entry_count].type_order = 0;
            entries[entry_count].index = (uint8_t)i;
            entry_count++;
        }
        for(size_t i = 0; i < state->count && i < JS_DISPLAY_MAX_LABELS; i++) {
            entries[entry_count].z = state->items[i].z;
            entries[entry_count].type_order = 1;
            entries[entry_count].index = (uint8_t)i;
            entry_count++;
        }

        for(size_t i = 1; i < entry_count; i++) {
            JsZEntry key = entries[i];
            int j = (int)i - 1;
            while(j >= 0 && (entries[j].z > key.z ||
                  (entries[j].z == key.z && entries[j].type_order > key.type_order))) {
                entries[j + 1] = entries[j];
                j--;
            }
            entries[j + 1] = key;
        }

        uint32_t order_hash = 2166136261u;
        for(size_t i = 0; i < entry_count; i++) {
            order_hash = js_display_hash_u32(order_hash, entries[i].type_order);
            order_hash = js_display_hash_u32(order_hash, (uint16_t)entries[i].z);
            if(entries[i].type_order == 0) {
                JsPendingImage* image = &state->images[entries[i].index];
                order_hash = js_display_hash_u32(order_hash, image->display);
            } else {
                JsPendingText* text = &state->items[entries[i].index];
                order_hash = js_display_hash_u32(order_hash, text->display);
            }
        }

        if(order_hash != state->active_order_hash) {
            for(size_t i = 0; i < state->active_image_count; i++) {
                js_display_free_active_image(&state->active_images[i]);
            }
            state->active_image_count = 0;

            for(size_t i = 0; i < state->active_label_count; i++) {
                js_display_free_active_label(&state->active_labels[i]);
            }
            state->active_label_count = 0;
        }

        size_t new_label_idx = 0;
        size_t new_image_idx = 0;

        for(size_t i = 0; i < entry_count; i++) {
            if(entries[i].type_order == 0) {
                JsPendingImage* image = &state->images[entries[i].index];
                if(new_image_idx < state->active_image_count) {
                    JsActiveImage* ai = &state->active_images[new_image_idx];
                    bool can_reuse =
                        ai->display == image->display &&
                        ai->prev_has_clip == image->has_clip &&
                        (!image->has_clip || (
                            ai->prev_clip_x == image->clip_x &&
                            ai->prev_clip_y == image->clip_y &&
                            ai->prev_clip_w == image->clip_w &&
                            ai->prev_clip_h == image->clip_h));
                    if(can_reuse) {
                        js_display_update_active_image(ai, image);
                    } else {
                        js_display_free_active_image(ai);
                        js_display_create_active_image(runner, ai, image);
                    }
                } else {
                    JsActiveImage* ai = &state->active_images[new_image_idx];
                    memset(ai, 0, sizeof(JsActiveImage));
                    js_display_create_active_image(runner, ai, image);
                }
                new_image_idx++;
            } else if(entries[i].type_order == 1) {
                JsPendingText* text = &state->items[entries[i].index];
                if(new_label_idx < state->active_label_count) {
                    JsActiveLabel* al = &state->active_labels[new_label_idx];
                    bool can_reuse =
                        al->display == text->display &&
                        al->prev_has_clip == text->has_clip &&
                        (!text->has_clip || (
                            al->prev_clip_x == text->clip_x &&
                            al->prev_clip_y == text->clip_y &&
                            al->prev_clip_w == text->clip_w &&
                            al->prev_clip_h == text->clip_h));
                    if(can_reuse) {
                        js_display_update_active_label(al, text);
                    } else {
                        js_display_free_active_label(al);
                        js_display_create_active_label(runner, al, text);
                    }
                } else {
                    JsActiveLabel* al = &state->active_labels[new_label_idx];
                    memset(al, 0, sizeof(JsActiveLabel));
                    js_display_create_active_label(runner, al, text);
                }
                new_label_idx++;
            }
        }

        for(size_t i = new_image_idx; i < state->active_image_count; i++) {
            js_display_free_active_image(&state->active_images[i]);
        }
        state->active_image_count = new_image_idx;

        for(size_t i = new_label_idx; i < state->active_label_count; i++) {
            js_display_free_active_label(&state->active_labels[i]);
        }
        state->active_label_count = new_label_idx;
        state->active_order_hash = order_hash;

    });

    js_display_pending_clear(state);
    runner->display_flush_pending = false;
}

static JSValue js_display_show(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    UNUSED(this_val);
    UNUSED(argc);
    UNUSED(argv);
    JsRunner* runner = JS_GetContextOpaque(ctx);
    js_display_get_state(runner);

    if(runner->callback_depth > 0) {
        runner->display_flush_pending = true;
        // FURI_LOG_D(TAG, "display.show deferred depth=%u", runner->callback_depth);
        return JS_UNDEFINED;
    }

    js_display_flush(runner);
    return JS_UNDEFINED;
}

void js_display_cleanup(JsRunner* runner) {
    if(!runner || !runner->display_state || !runner->gui) return;
    JsDisplayState* state = runner->display_state;

    with_gui(runner->gui, {
        for(size_t i = 0; i < state->active_bg_image_count; i++) {
            image_free(state->active_bg_images[i].image);
            if(state->active_bg_images[i].clip_container) {
                lv_obj_delete(state->active_bg_images[i].clip_container);
            }
        }
        state->active_bg_image_count = 0;

        for(size_t i = 0; i < state->active_image_count; i++) {
            js_display_free_active_image(&state->active_images[i]);
        }
        state->active_image_count = 0;

        for(size_t i = 0; i < state->active_label_count; i++) {
            js_display_free_active_label(&state->active_labels[i]);
        }
        state->active_label_count = 0;

        for(size_t i = 0; i < state->active_flex_count; i++) {
            js_display_reset_child_label_fonts(
                TO_LV_OBJ(flex_layout_get_base(state->active_flexes[i].layout)));
            flex_layout_free(state->active_flexes[i].layout);
        }
        state->active_flex_count = 0;

        if(state->active_anim) {
            anim_player_free(state->active_anim);
            state->active_anim = NULL;
        }

        js_display_destroy_canvas(state);
    });

    if(state->active_anim_path) {
        free(state->active_anim_path);
        state->active_anim_path = NULL;
    }

    js_display_pending_clear(state);
    free(state);
    runner->display_state = NULL;
}

JSValue js_module_display_create(JSContext* ctx, JsRunner* runner) {
    JsDisplayState* state = js_display_get_state(runner);
    UNUSED(state);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "text", JS_NewCFunction(ctx, js_display_text, "text", 1));
    JS_SetPropertyStr(ctx, obj, "pixel", JS_NewCFunction(ctx, js_display_pixel, "pixel", 2));
    JS_SetPropertyStr(ctx, obj, "rect", JS_NewCFunction(ctx, js_display_rect, "rect", 4));
    JS_SetPropertyStr(ctx, obj, "line", JS_NewCFunction(ctx, js_display_line, "line", 4));
    JS_SetPropertyStr(ctx, obj, "circle", JS_NewCFunction(ctx, js_display_circle, "circle", 3));
    JS_SetPropertyStr(ctx, obj, "image", JS_NewCFunction(ctx, js_display_image, "image", 1));
    JS_SetPropertyStr(ctx, obj, "anim", JS_NewCFunction(ctx, js_display_anim, "anim", 1));
    JS_SetPropertyStr(ctx, obj, "flex", JS_NewCFunction(ctx, js_display_flex, "flex", 1));
    JS_SetPropertyStr(ctx, obj, "graph", JS_NewCFunction(ctx, js_display_graph, "graph", 2));
    JS_SetPropertyStr(ctx, obj, "clear", JS_NewCFunction(ctx, js_display_clear, "clear", 0));
    JS_SetPropertyStr(ctx, obj, "show", JS_NewCFunction(ctx, js_display_show, "show", 0));
    return obj;
}

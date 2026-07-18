#include "../apps_menu_i.h"
#include "../storage_macros.h"
#include "apps_menu_scenes.h"
#include "../app_list.h"

#include <desktop/desktop.h>
#include <gui/modules/menu.h>
#include <storage/storage.h>
#include <cjson/cJSON.h>

#include <string.h>

#define SCRIPT_APP_FRONT_ICON APPS_MENU_IMG_PATH("clock_front_8x8.image")
#define SCRIPT_APP_BACK_ICON  APPS_MENU_IMG_PATH("clock_back_11x11.image")

typedef enum {
    SceneCustomEventMenuItemClicked = AppsMenuCustomEventSceneEventsStart,
} SceneCustomEvent;

typedef struct {
    char title[48];
    char application[APPS_MENU_ACTIVE_APPLICATION_MAX_SIZE];
    char args[160];
    char front_icon[160];
    char back_icon[160];
    bool persist_selection;
} AppsMenuLaunchEntry;

typedef struct {
    Menu* front_menu;
    Menu* back_menu;

    _Atomic uint32_t menu_idx;
    AppsMenuLaunchEntry entries[APPS_MENU_MAX_BUILTIN_APPS + APPS_MENU_MAX_JS_APPS];
    size_t entry_count;
} AppsMenuSceneMain;

static bool apps_menu_scene_main_add_entry(
    AppsMenuSceneMain* data,
    const char* title,
    const char* application,
    const char* args,
    const char* front_icon,
    const char* back_icon,
    bool persist_selection) {
    if(data->entry_count >= COUNT_OF(data->entries)) {
        return false;
    }

    AppsMenuLaunchEntry* entry = &data->entries[data->entry_count++];
    strlcpy(entry->title, title, sizeof(entry->title));
    strlcpy(entry->application, application, sizeof(entry->application));
    strlcpy(entry->args, args ? args : "", sizeof(entry->args));
    strlcpy(entry->front_icon, front_icon ? front_icon : "", sizeof(entry->front_icon));
    strlcpy(entry->back_icon, back_icon ? back_icon : "", sizeof(entry->back_icon));
    entry->persist_selection = persist_selection;
    return true;
}

static bool apps_menu_scene_main_read_manifest_name(
    Storage* storage,
    const char* manifest_path,
    char* out,
    size_t out_size) {
    bool found = false;
    File* file = storage_file_alloc(storage);
    char* buffer = NULL;

    do {
        if(!storage_file_open(file, manifest_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }

        const size_t size = (size_t)storage_file_size(file);
        if(size == 0 || size > 1024) break;

        buffer = malloc(size + 1);
        if(!buffer) break;

        const size_t read = storage_file_read(file, buffer, size);
        buffer[read] = '\0';
        if(read != size) break;

        cJSON* manifest = cJSON_ParseWithLength(buffer, read);
        if(!manifest) break;

        const cJSON* name = cJSON_GetObjectItemCaseSensitive(manifest, "name");
        if(cJSON_IsString(name) && name->valuestring && name->valuestring[0]) {
            strlcpy(out, name->valuestring, out_size);
            found = true;
        }
        cJSON_Delete(manifest);
    } while(false);

    free(buffer);
    storage_file_close(file);
    storage_file_free(file);
    return found;
}

static void apps_menu_scene_main_fill_script_icon(
    Storage* storage,
    const char* app_name,
    const char* icon_name,
    const char* fallback_path,
    char* out,
    size_t out_size) {
    furi_check(storage);
    furi_check(app_name);
    furi_check(icon_name);
    furi_check(fallback_path);
    furi_check(out);

    char icon_path[160];
    snprintf(icon_path, sizeof(icon_path), "%s/%s/%s", EXT_PATH("apps"), app_name, icon_name);
    if(storage_file_exists(storage, icon_path)) {
        strlcpy(out, icon_path, out_size);
    } else {
        strlcpy(out, fallback_path, out_size);
    }
}

static void apps_menu_scene_main_add_js_entries(AppsMenuSceneMain* data) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);

    if(storage_dir_open(dir, EXT_PATH("apps"))) {
        FileInfo info;
        char name[64];

        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(name[0] == '\0' || !file_info_is_dir(&info)) {
                continue;
            }

            char main_path[160];
            snprintf(main_path, sizeof(main_path), "%s/%s/main.js", EXT_PATH("apps"), name);
            if(!storage_file_exists(storage, main_path)) {
                continue;
            }

            char title[48];
            strlcpy(title, name, sizeof(title));

            char manifest_path[160];
            snprintf(manifest_path, sizeof(manifest_path), "%s/%s/app.json", EXT_PATH("apps"), name);
            apps_menu_scene_main_read_manifest_name(storage, manifest_path, title, sizeof(title));

            char front_icon[160];
            char back_icon[160];
            apps_menu_scene_main_fill_script_icon(
                storage,
                name,
                "icon_front.image",
                SCRIPT_APP_FRONT_ICON,
                front_icon,
                sizeof(front_icon));
            apps_menu_scene_main_fill_script_icon(
                storage,
                name,
                "icon_back.image",
                SCRIPT_APP_BACK_ICON,
                back_icon,
                sizeof(back_icon));

            if(!apps_menu_scene_main_add_entry(
                   data, title, "js_runner", main_path, front_icon, back_icon, false)) {
                break;
            }
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
}

static void apps_scene_setup_menu_callback(uint32_t index, void* context) {
    furi_assert(context);

    AppsMenu* instance = context;
    AppsMenuSceneMain* data =
        scene_manager_get_scene_data(instance->scene_manager, AppsMenuSceneIdMain);

    data->menu_idx = index;
    furi_message_queue_put(
        instance->event_queue, &(uint32_t){SceneCustomEventMenuItemClicked}, FuriWaitForever);
}

static void apps_menu_scene_main_on_enter(void* context) {
    furi_assert(context);
    AppsMenu* instance = context;
    AppsMenuSceneMain* data =
        scene_manager_get_scene_data(instance->scene_manager, AppsMenuSceneIdMain);
    data->entry_count = 0;

    with_gui(instance->gui, {
        // front:
        data->front_menu = menu_alloc(instance->front_scene_window);

        // back:
        data->back_menu = menu_alloc(instance->back_scene_window);
        for(size_t i = 0; i < apps_menu_builtin_entries_count; i++) {
            const AppsMenuBuiltinEntry* entry = &apps_menu_builtin_entries[i];
            apps_menu_scene_main_add_entry(
                data,
                entry->title,
                entry->application,
                NULL,
                entry->front_icon,
                entry->back_icon,
                entry->persist_selection);
        }

        apps_menu_scene_main_add_js_entries(data);

        for(size_t i = 0; i < data->entry_count; i++) {
            const AppsMenuLaunchEntry* entry = &data->entries[i];
            menu_add_item(
                data->front_menu,
                entry->title,
                "",
                entry->front_icon,
                i,
                apps_scene_setup_menu_callback,
                instance);
            menu_add_item(data->back_menu, entry->title, "", entry->back_icon, 0, NULL, NULL);
        }

        menu_set_selected_item_index(data->front_menu, data->menu_idx);
        menu_set_selected_item_index(data->back_menu, data->menu_idx);

        widget_set_scrollbar_enabled(menu_get_base(data->front_menu), true);
        widget_set_scrollbar_enabled(menu_get_base(data->back_menu), true);

        widget_set_visible(nav_bar_get_base(instance->back_nav_bar), true);
    });
}

static void apps_menu_scene_main_on_exit(void* context) {
    furi_assert(context);
    AppsMenu* instance = context;
    AppsMenuSceneMain* data =
        scene_manager_get_scene_data(instance->scene_manager, AppsMenuSceneIdMain);

    with_gui(instance->gui, {
        menu_free(data->front_menu);
        menu_free(data->back_menu);
    });
}

static bool apps_menu_scene_main_on_event(const SceneManagerEvent* event, void* context) {
    furi_assert(context);

    AppsMenu* instance = context;
    AppsMenuSceneMain* data =
        scene_manager_get_scene_data(instance->scene_manager, AppsMenuSceneIdMain);

    if(event->type == SceneManagerEventTypeCustom) {
        switch(event->event) {
        case SceneCustomEventMenuItemClicked:
            furi_check(data->menu_idx < data->entry_count);

            const AppsMenuLaunchEntry* target = &data->entries[data->menu_idx];

            if(target->persist_selection) {
                strlcpy(
                    instance->settings.active_application,
                    target->application,
                    sizeof(instance->settings.active_application));
            } else {
                instance->settings.active_application[0] = '\0';
            }
            apps_menu_settings_save(&instance->settings);

            Desktop* desktop = furi_record_open(RECORD_DESKTOP);
            desktop_replace_current_app(
                desktop, target->application, target->args[0] ? target->args : NULL);
            furi_record_close(RECORD_DESKTOP);
            return true;

        default:
            break;
        }
    }

    return false;
}

const Scene apps_menu_scene_main = {
    .enter_callback = apps_menu_scene_main_on_enter,
    .exit_callback = apps_menu_scene_main_on_exit,
    .event_callback = apps_menu_scene_main_on_event,
    .data_size = sizeof(AppsMenuSceneMain),
};

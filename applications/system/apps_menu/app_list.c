#include "app_list.h"
#include "storage_macros.h"

#include <furi.h>

const AppsMenuBuiltinEntry apps_menu_builtin_entries[] = {
    {
        .title = "Clock",
        .application = "clock",
        .front_icon = APPS_MENU_IMG_PATH("clock_front_8x8.image"),
        .back_icon = APPS_MENU_IMG_PATH("clock_back_11x11.image"),
        .persist_selection = true,
    },
};

const size_t apps_menu_builtin_entries_count = COUNT_OF(apps_menu_builtin_entries);

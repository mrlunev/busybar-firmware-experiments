#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char* title;
    const char* application;
    const char* front_icon;
    const char* back_icon;
    bool persist_selection;
} AppsMenuBuiltinEntry;

#define APPS_MENU_MAX_BUILTIN_APPS 4
#define APPS_MENU_MAX_JS_APPS      24

extern const AppsMenuBuiltinEntry apps_menu_builtin_entries[];
extern const size_t apps_menu_builtin_entries_count;

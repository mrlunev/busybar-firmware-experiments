#pragma once

#include "js_runner_i.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef JSValue (*JsModuleConstructor)(JSContext* ctx, JsRunner* runner);

typedef struct {
    const char* name;
    JsModuleConstructor create;
} JsModuleDescriptor;

JsModules* js_modules_create(JSContext* ctx, JsRunner* runner);
void js_modules_destroy(JsModules* modules);
JSValue js_module_require(JsModules* modules, const char* name, size_t name_len);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <jerryscript.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BsbJerryContext BsbJerryContext;

typedef struct {
    BsbJerryContext* previous;
} BsbJerryScope;

BsbJerryContext* bsb_jerry_context_create(size_t heap_size_kb, const char* tag);
void bsb_jerry_context_destroy(BsbJerryContext* context);

void bsb_jerry_scope_enter(BsbJerryContext* context, BsbJerryScope* scope);
void bsb_jerry_scope_leave(BsbJerryScope* scope);

BsbJerryContext* bsb_jerry_context_current(void);
const char* bsb_jerry_context_tag(const BsbJerryContext* context);

bool bsb_jerry_value_is_boolean(jerry_value_t value);
bool bsb_jerry_value_is_null(jerry_value_t value);
bool bsb_jerry_value_as_bool(jerry_value_t value);
bool bsb_jerry_value_to_bool(jerry_value_t value);
bool bsb_jerry_value_is_function(jerry_value_t value);
bool bsb_jerry_value_is_array(jerry_value_t value, bool* is_array_out);
bool bsb_jerry_array_length(jerry_value_t value, uint32_t* length_out);
jerry_value_t bsb_jerry_value_dup(jerry_value_t value);
jerry_value_t bsb_jerry_make_number(double number);
jerry_value_t bsb_jerry_make_null(void);
double bsb_jerry_value_as_number_full(jerry_value_t value);
jerry_value_t bsb_jerry_object_keys(jerry_value_t value);
jerry_value_t bsb_jerry_get_exception(void);
jerry_value_t bsb_jerry_call(
    jerry_value_t function, jerry_value_t this_value, const jerry_value_t* args, jerry_length_t argc);

jerry_value_t bsb_jerry_throw_error(jerry_error_t type, const char* message);

void bsb_jerry_heap_info(size_t* total_kb, size_t* used_bytes);
jerry_value_t bsb_jerry_throw_type_error(const char* message);
jerry_value_t bsb_jerry_throw_reference_error(const char* message);
jerry_value_t bsb_jerry_throw_range_error(const char* message);

#ifdef __cplusplus
}
#endif

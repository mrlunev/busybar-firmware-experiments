#include "jerry_runtime_manager.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <jerryscript-port.h>

#include <stdlib.h>
#include <string.h>

#include "jcontext.h"
#include "ecma-conversion.h"
#include "ecma-exceptions.h"
#include "ecma-function-object.h"
#include "ecma-helpers.h"
#include "ecma-objects.h"
#include "ecma-array-object.h"

struct BsbJerryContext {
    jerry_context_t* context_ptr;
    void* buffer;
    size_t buffer_size;
    size_t heap_size_kb;
    const char* tag;
    bool initialized;
};

static BsbJerryContext* bsb_jerry_active_context = NULL;
static BsbJerryContext* bsb_jerry_pending_context = NULL;

BsbJerryContext* bsb_jerry_context_create(size_t heap_size_kb, const char* tag) {
    BsbJerryContext* context = (BsbJerryContext*)calloc(1, sizeof(BsbJerryContext));
    if(!context) return NULL;

    context->heap_size_kb = heap_size_kb ? heap_size_kb : 256;
    context->tag = tag ? tag : "Jerry";

    BsbJerryContext* previous = bsb_jerry_active_context;
    bsb_jerry_pending_context = context;
    bsb_jerry_active_context = context;
    jerry_init(JERRY_INIT_EMPTY);
    context->initialized = true;
    bsb_jerry_pending_context = NULL;
    bsb_jerry_active_context = previous;

    return context;
}

void bsb_jerry_context_destroy(BsbJerryContext* context) {
    if(!context) return;

    if(context->initialized) {
        BsbJerryContext* previous = bsb_jerry_active_context;
        bsb_jerry_active_context = context;
        jerry_cleanup();
        context->initialized = false;
        bsb_jerry_active_context = previous;
    }

    free(context);
}

void bsb_jerry_scope_enter(BsbJerryContext* context, BsbJerryScope* scope) {
    furi_check(context);
    furi_check(scope);
    scope->previous = bsb_jerry_active_context;
    bsb_jerry_active_context = context;
}

void bsb_jerry_scope_leave(BsbJerryScope* scope) {
    furi_check(scope);
    bsb_jerry_active_context = scope->previous;
}

BsbJerryContext* bsb_jerry_context_current(void) {
    return bsb_jerry_active_context;
}

const char* bsb_jerry_context_tag(const BsbJerryContext* context) {
    return context ? context->tag : "Jerry";
}

bool bsb_jerry_value_is_boolean(jerry_value_t value) {
    return ecma_is_value_boolean(value);
}

bool bsb_jerry_value_is_null(jerry_value_t value) {
    return ecma_is_value_null(value);
}

bool bsb_jerry_value_as_bool(jerry_value_t value) {
    return ecma_is_value_true(value);
}

bool bsb_jerry_value_to_bool(jerry_value_t value) {
    return ecma_op_to_boolean(value);
}

bool bsb_jerry_value_is_function(jerry_value_t value) {
    return ecma_op_is_callable(value);
}

bool bsb_jerry_value_is_array(jerry_value_t value, bool* is_array_out) {
    if(!is_array_out) return false;

    ecma_value_t result = ecma_is_value_array(value);
    if(ECMA_IS_VALUE_ERROR(result)) {
        return false;
    }

    *is_array_out = ecma_is_value_true(result);
    return true;
}

bool bsb_jerry_array_length(jerry_value_t value, uint32_t* length_out) {
    if(!length_out || !ecma_is_value_object(value)) {
        return false;
    }

    ecma_length_t length = 0;
    ecma_value_t status = ecma_op_object_get_length(ecma_get_object_from_value(value), &length);
    if(ECMA_IS_VALUE_ERROR(status)) {
        return false;
    }

    ecma_free_value(status);
    *length_out = (uint32_t)length;
    return true;
}

jerry_value_t bsb_jerry_value_dup(jerry_value_t value) {
    return ecma_copy_value(value);
}

jerry_value_t bsb_jerry_make_number(double number) {
    return ecma_make_number_value((ecma_number_t)number);
}

jerry_value_t bsb_jerry_make_null(void) {
    return ECMA_VALUE_NULL;
}

double bsb_jerry_value_as_number_full(jerry_value_t value) {
    return (double)ecma_get_number_from_value(value);
}

jerry_value_t bsb_jerry_object_keys(jerry_value_t value) {
    if(!ecma_is_value_object(value)) {
        return ecma_raise_type_error(ECMA_ERR_ARGUMENT_THIS_NOT_OBJECT);
    }

    ecma_collection_t* keys = ecma_op_object_get_enumerable_property_names(
        ecma_get_object_from_value(value), ECMA_ENUMERABLE_PROPERTY_KEYS);
    if(!keys) {
        return ecma_raise_common_error(ECMA_ERR_EMPTY);
    }

    return ecma_op_new_array_object_from_collection(keys, false);
}

jerry_value_t bsb_jerry_get_exception(void) {
    return ecma_create_exception_from_context();
}

jerry_value_t bsb_jerry_call(
    jerry_value_t function, jerry_value_t this_value, const jerry_value_t* args, jerry_length_t argc) {
    if(!ecma_op_is_callable(function)) {
        return ecma_raise_type_error(ECMA_ERR_EXPECTED_A_FUNCTION);
    }

    return ecma_op_function_call(ecma_get_object_from_value(function), this_value, args, argc);
}

jerry_value_t bsb_jerry_throw_error(jerry_error_t type, const char* message) {
#if JERRY_ERROR_MESSAGES
    return ecma_raise_standard_error_with_format(type, "%s", message ? message : "Error");
#else
    UNUSED(message);
    switch(type) {
    case JERRY_ERROR_REFERENCE:
        return ecma_raise_reference_error(ECMA_ERR_EMPTY);
    case JERRY_ERROR_RANGE:
        return ecma_raise_range_error(ECMA_ERR_EMPTY);
    case JERRY_ERROR_TYPE:
        return ecma_raise_type_error(ECMA_ERR_EMPTY);
    default:
        return ecma_raise_common_error(ECMA_ERR_EMPTY);
    }
#endif
}

jerry_value_t bsb_jerry_throw_type_error(const char* message) {
    return bsb_jerry_throw_error(JERRY_ERROR_TYPE, message);
}

jerry_value_t bsb_jerry_throw_reference_error(const char* message) {
    return bsb_jerry_throw_error(JERRY_ERROR_REFERENCE, message);
}

jerry_value_t bsb_jerry_throw_range_error(const char* message) {
    return bsb_jerry_throw_error(JERRY_ERROR_RANGE, message);
}

void JERRY_ATTR_NORETURN jerry_port_fatal(jerry_fatal_code_t code) {
    FURI_LOG_E("Jerry", "Fatal error: %d", (int)code);
    furi_crash("JerryScript fatal");
}

void jerry_port_sleep(uint32_t sleep_time) {
    furi_delay_ms(sleep_time);
}

size_t jerry_port_context_alloc(size_t context_size) {
    furi_check(bsb_jerry_pending_context);

    size_t total_size = context_size + (bsb_jerry_pending_context->heap_size_kb * 1024u) + 16u;
    void* buffer = malloc(total_size);
    furi_check(buffer);

    bsb_jerry_pending_context->buffer = buffer;
    bsb_jerry_pending_context->buffer_size = total_size;
    bsb_jerry_pending_context->context_ptr = (jerry_context_t*)buffer;

    return total_size;
}

jerry_context_t* jerry_port_context_get(void) {
    furi_check(bsb_jerry_active_context);
    furi_check(bsb_jerry_active_context->context_ptr);
    return bsb_jerry_active_context->context_ptr;
}

void jerry_port_context_free(void) {
    if(!bsb_jerry_active_context) return;

    free(bsb_jerry_active_context->buffer);
    bsb_jerry_active_context->buffer = NULL;
    bsb_jerry_active_context->buffer_size = 0;
    bsb_jerry_active_context->context_ptr = NULL;
}

void jerry_port_log(const char* message_p) {
    if(message_p) {
        FURI_LOG_D("Jerry", "%s", message_p);
    }
}

void jerry_port_print_byte(jerry_char_t byte) {
    UNUSED(byte);
}

void jerry_port_print_buffer(const jerry_char_t* buffer_p, jerry_size_t buffer_size) {
    UNUSED(buffer_p);
    UNUSED(buffer_size);
}

int32_t jerry_port_local_tza(double unix_ms) {
    UNUSED(unix_ms);
    return 0;
}

double jerry_port_current_time(void) {
    return (double)furi_hal_rtc_get_timestamp_ms();
}

void bsb_jerry_heap_info(size_t* total_kb, size_t* used_bytes) {
    furi_check(total_kb);
    furi_check(used_bytes);

    if(!bsb_jerry_active_context || !bsb_jerry_active_context->context_ptr) {
        *total_kb = 0;
        *used_bytes = 0;
        return;
    }

    *total_kb = bsb_jerry_active_context->heap_size_kb;
    *used_bytes = JERRY_CONTEXT(jmem_heap_allocated_size);
}

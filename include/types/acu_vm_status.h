#pragma once
#include "common/panic.h"

#define ACU_VM_STATUS_LIST(X)                                                                      \
    X(ACU_VM_OK, "OK")                                                                             \
    X(ACU_VM_HALT, "Execution halted")                                                             \
    X(ACU_VM_ERR_PANIC, "Execution aborted due to explicit panic")                                 \
    X(ACU_VM_ERR_UNHANDLED_OPCODE, "Unhandled or invalid bytecode opcode")                         \
    X(ACU_VM_ERR_STACK_OVERFLOW, "Value stack overflow")                                           \
    X(ACU_VM_ERR_CALL_STACK_OVERFLOW, "Call frame stack overflow")                                 \
    X(ACU_VM_ERR_DIVISION_BY_ZERO, "Integer division or modulo by zero")                           \
    X(ACU_VM_ERR_INVALID_SYSCALL, "Invalid or unregistered syscall ID")                            \
    X(ACU_VM_ERR_IO, "Host I/O operation failed")

typedef enum {
#define X(name, str) name,
    ACU_VM_STATUS_LIST(X)
#undef X
} AcuVmStatus;

static inline const char *AcuVmStatus_String(AcuVmStatus status) {
    switch (status) {
#define X(name, str)                                                                               \
    case name:                                                                                     \
        return str;
        ACU_VM_STATUS_LIST(X)
#undef X
        default:
            return "Unknown VM status";
    }
}

static inline const char *AcuVmStatus_MnemonicString(AcuVmStatus status) {
    switch (status) {
#define X(name, str)                                                                               \
    case name:                                                                                     \
        return #name;
        ACU_VM_STATUS_LIST(X)
#undef X
        default:
            acu_unreachable_debug("unknown %d\n", status);
    }
}

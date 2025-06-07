// src/common.h
#ifndef COMMON_H
#define COMMON_H

// Memory-mapped registers
constexpr long PC_ADDR = 0;
constexpr long SP_ADDR = 1;
constexpr long CPU_OS_COMM_ADDR = 2;
constexpr long INSTR_COUNT_ADDR = 3;
constexpr long SAVED_TRAP_PC_ADDR = 4;
constexpr long SYSCALL_ARG1_PASS_ADDR = 5;
constexpr long SYSCALL_ARG2_PASS_ADDR = 6;
constexpr long REGISTERS_END_ADDR = 20;

// Try to include auto-generated symbols from assembler
#ifdef USE_ASSEMBLED_SYMBOLS
#include "os_and_threads_symbols.h"
#endif

// OS handler instruction addresses
constexpr long OS_BOOT_START_PC = 0;

// Use assembled symbols if available, otherwise fall back to hardcoded values
#ifdef OS_SYSCALL_DISPATCHER
constexpr long OS_SYSCALL_DISPATCHER_PC = OS_SYSCALL_DISPATCHER;
#else
constexpr long OS_SYSCALL_DISPATCHER_PC = 50; // Fallback value
#endif

#ifdef OS_MEMORY_FAULT_HANDLER_PC
// already defined by assembler
#else
constexpr long OS_MEMORY_FAULT_HANDLER_PC = 220; // Fallback value
#endif

#ifdef OS_ARITHMETIC_FAULT_HANDLER_PC  
// already defined by assembler
#else
constexpr long OS_ARITHMETIC_FAULT_HANDLER_PC = 230; // Fallback value
#endif

#ifdef OS_UNKNOWN_INSTRUCTION_HANDLER_PC
// already defined by assembler  
#else
constexpr long OS_UNKNOWN_INSTRUCTION_HANDLER_PC = 240; // Fallback value
#endif


// CPU events
enum class CpuEvent : long {
    NONE = 0,
    SYSCALL_PRN = 1,
    SYSCALL_HLT_THREAD = 2,
    SYSCALL_YIELD = 3,
    MEMORY_FAULT_USER = 4,
    UNKNOWN_INSTRUCTION_FAULT = 5,
    ARITHMETIC_FAULT = 6
};

// Memory layout
constexpr long OS_DATA_START_ADDR = REGISTERS_END_ADDR + 1; // Should be 21
constexpr long OS_DATA_END_ADDR = 999;
constexpr long USER_MEMORY_START_ADDR = 1000;

// Thread states - use values from assembly symbols if available
#ifndef THREAD_STATE_READY
// Fallback values if assembly symbols not available
constexpr int THREAD_STATE_INVALID = 0;
constexpr int THREAD_STATE_READY = 1;
constexpr int THREAD_STATE_RUNNING = 2;
constexpr int THREAD_STATE_BLOCKED = 3;
constexpr int THREAD_STATE_TERMINATED = 4;
#endif

#endif // COMMON_H
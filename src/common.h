// src/common.h
#ifndef COMMON_H
#define COMMON_H

// Memory-mapped "registers"
constexpr long PC_ADDR = 0;                // Program Counter
constexpr long SP_ADDR = 1;                // Stack Pointer
constexpr long CPU_OS_COMM_ADDR = 2;       // CPU-OS Communication (Syscall type, error codes, results)
constexpr long INSTR_COUNT_ADDR = 3;       // Total CPU instructions executed by CPU
constexpr long SAVED_TRAP_PC_ADDR = 4;     // CPU saves user PC here on syscall/trap
constexpr long SYSCALL_ARG1_PASS_ADDR = 5; // CPU passes first syscall argument (e.g., addr for PRN)
constexpr long SYSCALL_ARG2_PASS_ADDR = 6; // CPU passes second syscall argument (if any)
// Add more up to 20 if needed for general CPU-OS comm

// OS specific memory locations (known to CPU for traps/syscalls)
// These are INSTRUCTION ADDRESSES (i.e., PC values) for the OS handlers
constexpr long OS_BOOT_START_PC = 0;            // Default PC for OS startup if not specified otherwise
constexpr long OS_SYSCALL_DISPATCHER_PC = 50;   // Example: OS Syscall dispatcher routine starts at instruction 50
constexpr long OS_MEMORY_FAULT_HANDLER_PC = 60; // Example: OS Memory fault handler starts at instruction 60

// Codes for CPU_OS_COMM_ADDR (mem[2])
// CPU -> OS: What happened?
constexpr long CPU_EVENT_NONE = 0;
constexpr long CPU_EVENT_SYSCALL_PRN = 1;
constexpr long CPU_EVENT_SYSCALL_HLT_THREAD = 2;
constexpr long CPU_EVENT_SYSCALL_YIELD = 3;
constexpr long CPU_EVENT_MEMORY_FAULT_USER = 4; // User mode memory access violation
constexpr long CPU_EVENT_UNKNOWN_INSTRUCTION = 5;
// Add other CPU events/traps if necessary

// OS -> CPU: (Less common for this simple model, but could be used)
// Example: If OS needs to signal CPU about something specific.

// General constants
constexpr long USER_MODE_PROTECTED_MEMORY_START = 21; // Start of OS data only area
constexpr long USER_MODE_PROTECTED_MEMORY_END = 999;  // End of OS data only area
                                                      // Threads can access 0-20 (registers) and >=1000

// Thread states (for OS Thread Table, not directly used by CPU but good for context)
constexpr int THREAD_STATE_INVALID = 0;
constexpr int THREAD_STATE_READY = 1;
constexpr int THREAD_STATE_RUNNING = 2;
constexpr int THREAD_STATE_BLOCKED = 3;
constexpr int THREAD_STATE_TERMINATED = 4;

#endif // COMMON_H
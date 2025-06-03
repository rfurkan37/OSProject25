// src/common.h
#ifndef COMMON_H
#define COMMON_H

// Memory-mapped "registers"
constexpr long PC_ADDR = 0;                // Program Counter
constexpr long SP_ADDR = 1;                // Stack Pointer
constexpr long CPU_OS_COMM_ADDR = 2;       // CPU-OS Communication (Syscall type, error codes, results)
constexpr long INSTR_COUNT_ADDR = 3;       // Total CPU instructions executed by CPU
constexpr long SAVED_TRAP_PC_ADDR = 4;     // CPU saves user PC here on syscall/trap/fault
constexpr long SYSCALL_ARG1_PASS_ADDR = 5; // CPU passes first syscall/fault argument
constexpr long SYSCALL_ARG2_PASS_ADDR = 6; // CPU passes second syscall/fault argument (if any)
// Add more up to 20 if needed for general CPU-OS comm
constexpr long REGISTERS_END_ADDR = 20;    // Last address of memory-mapped registers

// OS specific memory locations (known to CPU for traps/syscalls)
// These are INSTRUCTION ADDRESSES (i.e., PC values) for the OS handlers
constexpr long OS_BOOT_START_PC = 0;               // Default PC for OS startup if not specified otherwise
constexpr long OS_SYSCALL_DISPATCHER_PC = 50;      // Example: OS Syscall dispatcher routine
constexpr long OS_MEMORY_FAULT_HANDLER_PC = 60;    // Example: OS Memory fault handler routine
constexpr long OS_ARITHMETIC_FAULT_HANDLER_PC = 70; // Example: OS Arithmetic fault handler routine (New)
constexpr long OS_UNKNOWN_INSTRUCTION_HANDLER_PC = OS_MEMORY_FAULT_HANDLER_PC; // Can reuse memory fault or have a new one

// Codes for CPU_OS_COMM_ADDR (mem[2]) - CPU -> OS: What happened?
enum class CpuEvent : long {
    NONE = 0,
    SYSCALL_PRN = 1,
    SYSCALL_HLT_THREAD = 2,
    SYSCALL_YIELD = 3,
    MEMORY_FAULT_USER = 4,       // User mode memory access violation
    UNKNOWN_INSTRUCTION_FAULT = 5, // Unknown instruction encountered
    ARITHMETIC_FAULT = 6           // Arithmetic overflow/error (New)
    // Add other CPU events/traps if necessary
};

// General constants for memory layout
constexpr long OS_DATA_START_ADDR = REGISTERS_END_ADDR + 1; // Start of OS data only area (e.g. 21)
constexpr long OS_DATA_END_ADDR = 999;                      // End of OS data only area
constexpr long USER_MEMORY_START_ADDR = 1000;               // Start of user-accessible general memory (and stacks)

// Thread states (for OS Thread Table, not directly used by CPU but good for context)
// These are conventions for the OS, not enforced by CPU hardware.
constexpr int THREAD_STATE_INVALID = 0;
constexpr int THREAD_STATE_READY = 1;
constexpr int THREAD_STATE_RUNNING = 2;
constexpr int THREAD_STATE_BLOCKED = 3;
constexpr int THREAD_STATE_TERMINATED = 4;

#endif // COMMON_H
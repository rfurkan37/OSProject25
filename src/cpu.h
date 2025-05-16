// src/cpu.h
#ifndef CPU_H
#define CPU_H

#include "memory.h"
#include <vector>
#include <string>
#include <functional> // For std::function (to handle PRN syscall output)
#include <stdexcept>  // For std::runtime_error

// Enum for operation codes
enum class OpCode {
    SET, CPY, CPYI, ADD, ADDI, SUBI, JIF,
    PUSH, POP, CALL, RET, HLT, USER,
    SYSCALL_PRN, 
    SYSCALL_HLT_THREAD, // To distinguish from HLT CPU instruction
    SYSCALL_YIELD,
    UNKNOWN // Placeholder for parsing errors or uninitialized instructions
};

// Structure to hold a parsed instruction
struct Instruction {
    OpCode opcode;
    long arg1; // For SET: B value. For CPY/CPYI/ADDI/SUBI: Address A1. For ADD: Address A. For JIF: Address A. For PUSH/POP: Address A. For SYSCALL_PRN: Address A. For CALL: Instr. Num C.
    long arg2; // For SET: Address A. For CPY/CPYI/ADDI/SUBI: Address A2. For ADD: B value. For JIF: Instr. Num C.
    int num_operands; // 0 for HLT, USER, RET. 1 for PUSH, POP, CALL, SYSCALLs. 2 for others.
    std::string original_line; // Store the original instruction string for debugging

    // Default constructor
    Instruction(OpCode op = OpCode::UNKNOWN, long a1 = 0, long a2 = 0, int num_ops = 0, std::string line = "")
        : opcode(op), arg1(a1), arg2(a2), num_operands(num_ops), original_line(std::move(line)) {}
};

// System call result codes written to memory location 2
constexpr long SYSCALL_STATUS_SUCCESS = 0;
constexpr long SYSCALL_STATUS_THREAD_HALT_REQUEST = 1;
constexpr long SYSCALL_STATUS_YIELD_REQUEST = 2;
constexpr long SYSCALL_STATUS_ERROR_GENERIC = -1; // Example error code

class CPU {
public:
    // Constructor
    CPU(Memory& mem, 
        const std::vector<Instruction>& instructions, 
        std::function<void(long)> prn_callback);

    // Executes a single instruction cycle
    void step();

    // Checks if the CPU has been halted by an HLT instruction
    bool isHalted() const { return halted_flag_; }

    // Resets CPU state (PC, SP from memory, flags, instruction count)
    void reset();
    
    // (Optional) Getters for CPU state, useful for debugging or OS
    bool isInUserMode() const { return user_mode_flag_; }
    long getCurrentProgramCounter() const; // Reads from memory_[0]

private:
    Memory& memory_; // Reference to the system memory
    const std::vector<Instruction>& program_instructions_; // Reference to parsed instructions
    std::function<void(long)> prn_system_call_handler_; // Callback for SYSCALL PRN

    bool halted_flag_;      // True if CPU HLT instruction has been executed
    bool user_mode_flag_;   // True if CPU is in user mode, false for kernel mode

    // Helper methods for memory access with user mode protection
    long privilegedRead(long address); // Internal read, bypasses user mode checks for registers
    long checkedRead(long address);
    void checkedWrite(long address, long value);
    void handleMemoryViolation(const std::string& operation_type, long address);

    // Helper methods for register access (which are memory-mapped)
    long getPC() const;
    void setPC(long new_pc);
    long getSP() const;
    void setSP(long new_sp);
    void incrementInstructionCounter();
    void setSystemCallResult(long result_code);
};

#endif // CPU_H
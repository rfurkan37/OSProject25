// src/cpu.h
#ifndef CPU_H
#define CPU_H

#include "memory.h"
#include "instruction.h" // For Instruction and OpCode definitions
#include "common.h"
#include <vector>
#include <string>
#include <functional> // For std::function (to handle PRN syscall output)
#include <stdexcept>  // For std::runtime_error

class CPU
{
public:
    // Constructor
    CPU(Memory &mem,
        const std::vector<Instruction> &instructions,
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
    Memory &memory_;                                       // Reference to the system memory
    const std::vector<Instruction> &program_instructions_; // Reference to parsed instructions
    std::function<void(long)> prn_system_call_handler_;    // Callback for SYSCALL PRN

    bool halted_flag_;    // True if CPU HLT instruction has been executed
    bool user_mode_flag_; // True if CPU is in user mode, false for kernel mode

    // Helper methods for memory access with user mode protection
    long privilegedRead(long address); // Internal read, bypasses user mode checks for registers
    long checkedRead(long address);
    void checkedWrite(long address, long value);

    // Helper methods for register access (which are memory-mapped)
    long getPC() const;
    void setPC(long new_pc);
    long getSP() const;
    void setSP(long new_sp);
    void incrementInstructionCounter();
    void setSystemCallResult(long result_code);
};

class UserMemoryFaultException : public std::runtime_error
{
public:
    long faulting_address;
    UserMemoryFaultException(const std::string &msg, long addr) : std::runtime_error(msg), faulting_address(addr) {}
};

#endif // CPU_H
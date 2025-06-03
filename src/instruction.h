// src/instruction.h
#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <string>
#include <vector> // For opcode to string mapping if needed

// Enum for operation codes
enum class OpCode
{
    SET,
    CPY,
    CPYI,
    CPYI2, // CPYI2 is optional
    ADD,
    ADDI,
    SUBI,
    JIF,
    PUSH,
    POP,
    CALL,
    RET,
    HLT,
    USER, // Will be USER A
    SYSCALL_PRN,
    SYSCALL_HLT_THREAD,
    SYSCALL_YIELD,
    UNKNOWN // Placeholder for parsing errors or uninitialized instructions
};

// Structure to hold a parsed instruction
struct Instruction
{
    OpCode opcode;
    long arg1;
    long arg2;
    int num_operands;
    std::string original_line; // Store the original instruction string for debugging

    // Default constructor
    Instruction(OpCode op = OpCode::UNKNOWN, long a1 = 0, long a2 = 0, int num_ops = 0, std::string line = "")
        : opcode(op), arg1(a1), arg2(a2), num_operands(num_ops), original_line(std::move(line)) {}
};

// Helper function to convert OpCode to string (optional, for debugging)
inline std::string opCodeToString(OpCode op)
{
    static const std::vector<std::string> opCodeStrings = {
        "SET", "CPY", "CPYI", "CPYI2",
        "ADD", "ADDI", "SUBI", "JIF",
        "PUSH", "POP", "CALL", "RET", "HLT",
        "USER",
        "SYSCALL_PRN", "SYSCALL_HLT_THREAD", "SYSCALL_YIELD",
        "UNKNOWN"};
    if (static_cast<size_t>(op) < opCodeStrings.size())
    {
        return opCodeStrings[static_cast<int>(op)];
    }
    return "INVALID_OPCODE";
}

#endif // INSTRUCTION_H
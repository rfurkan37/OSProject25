#include "instruction.h"
#include <array>   // For std::array
#include <string>  // For std::string
// #include <vector> // No longer needed

// Helper function to convert OpCode to string
std::string opCodeToString(OpCode op)
{
    // Using std::array as the size is fixed at compile time.
    static const std::array<std::string, 20> opCodeStrings = {{
        "SET", "CPY", "CPYI", "CPYI2",
        "ADD", "ADDI", "SUBI", "JIF",
        "PUSH", "POP", "CALL", "RET", "HLT",
        "USER", "STOREI", "LOADI",
        "SYSCALL_PRN", "SYSCALL_HLT_THREAD", "SYSCALL_YIELD",
        "UNKNOWN"}};
    
    // Cast OpCode to its underlying type (usually int), then to size_t for bounds checking.
    size_t op_index = static_cast<size_t>(static_cast<std::underlying_type_t<OpCode>>(op));

    if (op_index < opCodeStrings.size())
    {
        return opCodeStrings[op_index];
    }
    return "INVALID_OPCODE"; // Should ideally not be reached if OpCode::UNKNOWN is the last valid one
}
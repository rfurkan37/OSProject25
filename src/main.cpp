// src/sim.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept> // For std::runtime_error
#include <unordered_map>
#include <algorithm> // For std::transform (string to uppercase)
#include <limits>    // For std::numeric_limits

#include "memory.h"
#include "cpu.h" // This should also bring in OpCode and Instruction struct

// Global map for mnemonics to OpCode. Defined here for parseInstructionSection.
const std::unordered_map<std::string, OpCode> MNEMONIC_TO_OPCODE = {
    {"SET", OpCode::SET}, {"CPY", OpCode::CPY}, {"CPYI", OpCode::CPYI}, {"CPYI2", OpCode::CPYI2}, // Add if implementing
    {"ADD", OpCode::ADD},
    {"ADDI", OpCode::ADDI},
    {"SUBI", OpCode::SUBI},
    {"JIF", OpCode::JIF},
    {"PUSH", OpCode::PUSH},
    {"POP", OpCode::POP},
    {"CALL", OpCode::CALL},
    {"RET", OpCode::RET},
    {"HLT", OpCode::HLT},
    {"USER", OpCode::USER} // USER A is a 1-operand instruction
};

// Forward declaration for our instruction parser
std::vector<Instruction> parseInstructionSection(std::ifstream &fileStream, const std::string &filename);

// PRN system call handler
void handlePrnSyscall(long value)
{
    std::cout << value << std::endl;
}

// Function to dump memory (used by debug modes)
void dumpMemoryForDebug(const Memory &mem, int debug_mode, bool after_halt = false)
{
    if (debug_mode == 0 && after_halt)
    {                                        // D0: only after halt
        mem.dumpImportantRegions(std::cerr); // Or full dump if preferred
        // Or use mem.dumpMemoryRange(std::cerr, 0, mem.getSize() -1); for full dump
    }
    else if (debug_mode == 1)
    { // D1: after each instruction
        // "the contents of the memory will be sent to standard error stream
        // after each CPU instruction execution (memory address and the content for each adress)."
        // This implies a full dump or at least significant regions.
        // For simplicity, let's dump important regions. A full dump can be very verbose.
        // The spec: "(memory address and the content for each adress)" -> This implies full dump.
        mem.dumpMemoryRange(std::cerr, 0, mem.getSize() - 1);
    }
    else if (debug_mode == 2)
    { // D2: after each instruction + keypress
        mem.dumpMemoryRange(std::cerr, 0, mem.getSize() - 1);
        std::cerr << "--- Press ENTER to continue to next tick ---" << std::endl;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        // Or just std::cin.get(); if single char is enough and robust across platforms
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: ./sim <program_filename> [-D <0|1|2>]" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    int debug_mode = -1; // Default: no debug output beyond PRN

    if (argc == 3)
    {
        std::string debug_arg = argv[2];
        
        // Check if it starts with -D or -d
        if (debug_arg.length() < 3 || (debug_arg.substr(0, 2) != "-D" && debug_arg.substr(0, 2) != "-d"))
        {
            std::cerr << "Error: Invalid debug flag format. Use -D<0|1|2>." << std::endl;
            return 1;
        }
        
        // Extract the digit after -D
        std::string digit_str;
        if (debug_arg.length() == 3)
        {
            // Format: -D0, -D1, etc.
            digit_str = debug_arg.substr(2, 1);
        }
        else if (debug_arg.length() > 3 && debug_arg[2] == ' ')
        {
            // Format: -D 0, -D 1, etc.
            digit_str = debug_arg.substr(3);
        }
        else
        {
            std::cerr << "Error: Malformed debug flag. Use -D0, -D1, or -D2." << std::endl;
            return 1;
        }
        
        // Parse and validate the debug mode
        try
        {
            debug_mode = std::stoi(digit_str);
            if (debug_mode < 0 || debug_mode > 2)
            {
                std::cerr << "Error: Invalid debug mode. Must be 0, 1, or 2." << std::endl;
                return 1;
            }
        }
        catch (const std::exception&)
        {
            std::cerr << "Error: Invalid debug mode value. Must be an integer 0, 1, or 2." << std::endl;
            return 1;
        }
    }

    Memory systemMemory(11000); // Default size, can be configurable later
    std::vector<Instruction> programInstructions;

    std::ifstream programFile(filename);
    if (!programFile.is_open())
    {
        std::cerr << "Error: Could not open program file '" << filename << "'." << std::endl;
        return 1;
    }

    // 1. Load Data Section
    if (!systemMemory.loadDataSection(programFile))
    {
        std::cerr << "Error loading data section from '" << filename << "'. Ensure format is correct and markers exist." << std::endl;
        programFile.close();
        return 1;
    }
    // programFile stream is now positioned after "End Data Section"

    // 2. Parse Instruction Section
    try
    {
        programInstructions = parseInstructionSection(programFile, filename);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Error parsing instruction section: " << e.what() << std::endl;
        programFile.close();
        return 1;
    }
    programFile.close(); // Done with the file

    // Check if PC is initialized (memory[0] should have a value from data section)
    // If not, and there are instructions, it's an issue.
    // The problem spec implies BIOS sets PC from memory[0].
    // "BIOS will read both data segment and instruction segment, then it will start executing
    // starting from the PC which is stored in address zero."
    // So, the data section should define memory[0]. If it's 0 and instructions exist, it's okay.

    if (programInstructions.empty())
    {
        std::cout << "Warning: No instructions found or parsed in '" << filename << "'. Program may only contain data." << std::endl;
        // If PC is non-zero but no instructions, it's an error. If PC is 0 and no instructions, it might just halt or do nothing.
        // This might be fine for a data-only file, or an OS that's entirely data-driven for setup (unlikely here).
    }

    // 3. Create CPU
    CPU gtu_cpu(systemMemory, programInstructions, handlePrnSyscall);
    // gtu_cpu.reset(); // Constructor calls reset, so not strictly needed here unless re-running

    // 4. Run Simulation Loop
    try
    {
        while (!gtu_cpu.isHalted())
        {
            gtu_cpu.step();
            if (debug_mode == 1 || debug_mode == 2)
            {
                dumpMemoryForDebug(systemMemory, debug_mode);
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        // This can catch errors propagated from CPU::step (e.g., memory violations)
        std::cerr << "Simulation stopped due to runtime error: " << e.what() << std::endl;
        // Dump memory on error if not already covered by D1/D2, or if D0 is active
        if (debug_mode == 0 || debug_mode == -1)
        { // If D0 or no debug, dump on error.
            std::cerr << "--- Memory state at time of error ---" << std::endl;
            dumpMemoryForDebug(systemMemory, 0, true); // Use D0 dump logic
        }
        return 1; // Indicate error exit
    }

    // After HLT or if loop finishes (e.g. no instructions)
    // std::cout << "CPU Halted." << std::endl; // CPU HLT message is handled in CPU::step usually
    if (debug_mode == 0)
    {
        dumpMemoryForDebug(systemMemory, debug_mode, true);
    }

    // Final instruction count from memory
    // std::cout << "Total instructions executed: " << systemMemory.read(3) << std::endl;

    return 0;
}

// --- Instruction Parsing Implementation ---
std::vector<Instruction> parseInstructionSection(std::ifstream &fileStream, const std::string &filename_for_error_msg)
{
    std::vector<Instruction> instructions;
    std::string line;
    bool inInstructionSection = false;
    int line_number_for_errors = 0; // Track original line number for better errors

    // First, find "Begin Instruction Section"
    // Need to track lines from where data section ended.
    // This simple line counter might not be accurate if data section parsing also read lines.
    // A better way would be to count lines from the start of the file for error reporting,
    // but for now, this counts lines *within* this function's scope of reading.

    while (std::getline(fileStream, line))
    {
        line_number_for_errors++;
        // Trim whitespace and comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos)
        {
            line = line.substr(0, commentPos);
        }
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line == "Begin Instruction Section")
        {
            inInstructionSection = true;
            break;
        }
    }

    if (!inInstructionSection)
    {
        // This can happen if the file ends or "End Data Section" was the last thing.
        // It's not necessarily an error if no instructions are intended.
        // std::cerr << "Warning: 'Begin Instruction Section' marker not found in " << filename_for_error_msg << std::endl;
        return instructions; // Return empty vector
    }

    int instruction_index_within_section = 0;
    while (std::getline(fileStream, line))
    {
        line_number_for_errors++;
        std::string original_line_for_instr = line; // Keep original for Instruction struct

        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos)
        {
            line = line.substr(0, commentPos);
        }
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line == "End Instruction Section")
        {
            break; // End of instructions
        }

        if (line.empty())
        {
            continue; // Skip empty lines
        }

        // The first token is "line_number_in_section" which we ignore for parsing the instruction itself,
        // but it's good for validation or if we were to support labels that resolve to these numbers.
        // For now, the CPU PC is an index into the vector, so these explicit numbers are mostly for human readability.

        std::istringstream iss(line);
        int parsed_line_num_in_file; // The '0', '1', '2' at the start of instruction lines
        std::string mnemonic_str;

        iss >> parsed_line_num_in_file; // Read and discard/validate the instruction line number
        if (iss.fail() || parsed_line_num_in_file != instruction_index_within_section)
        {
            // Allow for flexible line numbering, or enforce strict sequential?
            // Spec "0 SET 10 50" implies it's part of the line.
            // For now, let's assume it's present but we primarily use vector index for PC.
            // If it's not the expected sequential number, it might be a format error.
            // Let's be lenient for now and just ensure we read it.
            // If it's crucial, add stricter check:
            // if (parsed_line_num_in_file != instruction_index_within_section) {
            //    throw std::runtime_error("Instruction line number mismatch at '" + original_line_for_instr + "'. Expected " + std::to_string(instruction_index_within_section));
            // }
        }

        iss >> mnemonic_str;
        std::transform(mnemonic_str.begin(), mnemonic_str.end(), mnemonic_str.begin(), ::toupper); // To uppercase

        Instruction instr;
        instr.original_line = original_line_for_instr; // Store full original line
        long arg1_val = 0, arg2_val = 0;

        // Temporary string stream for parsing operands for current line
        std::string operands_part_str;
        // iss is from the full line. We need to advance it past instr_line_idx and mnemonic.
        // Example line: "0 SET 10, 50"
        // iss already had "0" and "SET" extracted.
        // The rest of the line is in iss.
        std::getline(iss, operands_part_str); // Get " 10, 50" (with leading space)

        // Trim leading space from operands_part_str
        operands_part_str.erase(0, operands_part_str.find_first_not_of(" \t"));

        if (mnemonic_str == "SYSCALL")
        {
            std::istringstream ops_iss(operands_part_str);
            std::string syscall_type_str;
            ops_iss >> syscall_type_str; // PRN, HLT, YIELD
            std::transform(syscall_type_str.begin(), syscall_type_str.end(), syscall_type_str.begin(), ::toupper);

            if (syscall_type_str == "PRN")
            {
                instr.opcode = OpCode::SYSCALL_PRN;
                instr.num_operands = 1;
                if (!(ops_iss >> arg1_val))
                    throw std::runtime_error("SYSCALL PRN: Missing address argument. Line: " + original_line_for_instr);
                instr.arg1 = arg1_val;

                std::string junk;
                if (ops_iss >> junk && !junk.empty() && junk[0] != '#')
                {
                    throw std::runtime_error("SYSCALL PRN: Too many operands or trailing characters. Line: " + original_line_for_instr);
                }
            }
            else if (syscall_type_str == "HLT")
            {
                instr.opcode = OpCode::SYSCALL_HLT_THREAD;
                instr.num_operands = 0;
                std::string junk;
                if (ops_iss >> junk && !junk.empty() && junk[0] != '#')
                {
                    throw std::runtime_error("SYSCALL HLT: Too many operands or trailing characters. Line: " + original_line_for_instr);
                }
            }
            else if (syscall_type_str == "YIELD")
            {
                instr.opcode = OpCode::SYSCALL_YIELD;
                instr.num_operands = 0;
                std::string junk;
                if (ops_iss >> junk && !junk.empty() && junk[0] != '#')
                {
                    throw std::runtime_error("SYSCALL YIELD: Too many operands or trailing characters. Line: " + original_line_for_instr);
                }
            }
            else
            {
                throw std::runtime_error("Unknown SYSCALL type: " + syscall_type_str + ". Line: " + original_line_for_instr);
            }
        }
        else
        {
            auto it = MNEMONIC_TO_OPCODE.find(mnemonic_str);
            if (it == MNEMONIC_TO_OPCODE.end())
            {
                throw std::runtime_error("Unknown mnemonic: " + mnemonic_str + ". Line: " + original_line_for_instr);
            }
            instr.opcode = it->second;

            // Determine operands based on opcode more robustly
            std::vector<std::string> arg_tokens;
            std::istringstream ops_iss(operands_part_str);
            std::string temp_arg_token;

            while(ops_iss >> temp_arg_token) {
                arg_tokens.push_back(temp_arg_token);
            }

            // Determine operands based on opcode
            switch (instr.opcode)
            {
            case OpCode::HLT:
            case OpCode::RET:
                instr.num_operands = 0;
                // Check for extra operands
                if (!arg_tokens.empty())
                    throw std::runtime_error(mnemonic_str + ": Unexpected operand(s). Line: " + original_line_for_instr);
            case OpCode::USER: // USER A (arg1=A)
            case OpCode::PUSH:
            case OpCode::POP:
            case OpCode::CALL: // CALL C (arg1=C)
                instr.num_operands = 1;
                instr.num_operands = 1;
                if (arg_tokens.size() != 1)
                    throw std::runtime_error(mnemonic_str + ": Expects 1 operand. Got: " + std::to_string(arg_tokens.size()) + ". Line: " + original_line_for_instr);
                try
                {
                    instr.arg1 = std::stol(arg_tokens[0]);
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error(mnemonic_str + ": Invalid numeric operand '" + arg_tokens[0] + "'. Line: " + original_line_for_instr);
                }
                break;
            case OpCode::SET:  // SET B A (arg1=B, arg2=A)
            case OpCode::CPY:  // CPY A1 A2
            case OpCode::CPYI: // CPYI A1 A2
            case OpCode::CPYI2:
            case OpCode::ADD:  // ADD A B
            case OpCode::ADDI: // ADDI A1 A2
            case OpCode::SUBI: // SUBI A1 A2
            case OpCode::JIF:  // JIF A C
                instr.num_operands = 2;
                if (arg_tokens.size() != 2)
                    throw std::runtime_error(mnemonic_str + ": Expects 2 operands. Got: " + std::to_string(arg_tokens.size()) + ". Line: " + original_line_for_instr);
                try
                {
                    instr.arg1 = std::stol(arg_tokens[0]);
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error(mnemonic_str + ": Invalid numeric operand1 '" + arg_tokens[0] + "'. Line: " + original_line_for_instr);
                }
                try
                {
                    instr.arg2 = std::stol(arg_tokens[1]);
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error(mnemonic_str + ": Invalid numeric operand2 '" + arg_tokens[1] + "'. Line: " + original_line_for_instr);
                }
                break;
            default: // Should not happen if MNEMONIC_TO_OPCODE is complete
                throw std::runtime_error("Internal parser error: Unhandled opcode in switch. Mnemonic: " + mnemonic_str);
            }
        }

        instructions.push_back(instr);
        instruction_index_within_section++;
    }

    // Check if "End Instruction Section" was found
    if (inInstructionSection && line != "End Instruction Section" && !fileStream.eof())
    {
        // This means we broke loop for some other reason, or EOF hit mid-section.
        // If EOF is hit and we were in section, marker is missing.
        if (fileStream.eof())
        {
            throw std::runtime_error("'End Instruction Section' marker not found before EOF in " + filename_for_error_msg);
        }
    }

    return instructions;
}
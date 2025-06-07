#include "parser.h"
#include "instruction.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <vector>

// Global map for mnemonics to OpCode
const std::unordered_map<std::string, OpCode> MNEMONIC_TO_OPCODE = {
    {"SET", OpCode::SET}, {"CPY", OpCode::CPY}, {"CPYI", OpCode::CPYI}, 
    {"CPYI2", OpCode::CPYI2}, {"ADD", OpCode::ADD}, {"ADDI", OpCode::ADDI}, 
    {"SUBI", OpCode::SUBI}, {"JIF", OpCode::JIF}, {"PUSH", OpCode::PUSH}, 
    {"POP", OpCode::POP}, {"CALL", OpCode::CALL}, {"RET", OpCode::RET}, 
    {"HLT", OpCode::HLT}, {"USER", OpCode::USER}, {"STOREI", OpCode::STOREI},
    {"LOADI", OpCode::LOADI},
    // SYSCALL variants are handled separately
    {"SYSCALL", OpCode::SYSCALL_PRN} // Placeholder, specific type determined by operand
};

static bool parseOperands(const std::string& operands_str, long& arg1, long& arg2, int expected_count)
{
    std::string clean_operands = operands_str;
    clean_operands.erase(std::remove(clean_operands.begin(), clean_operands.end(), ','), clean_operands.end());
    
    std::istringstream iss(clean_operands);
    
    if (expected_count == 0) return true;
    if (expected_count == 1) return static_cast<bool>(iss >> arg1);
    if (expected_count == 2) return static_cast<bool>(iss >> arg1 >> arg2);
    
    return false;
}

static int getExpectedOperandCount(OpCode opcode, const std::string& syscall_type = "")
{
    if (opcode == OpCode::SYSCALL_PRN) { // This is our generic SYSCALL opcode now
        if (syscall_type == "PRN") return 1;
        if (syscall_type == "HLT" || syscall_type == "YIELD") return 0;
        return -1; // Unknown syscall type
    }

    switch (opcode) {
    case OpCode::HLT:
    case OpCode::RET:
        return 0;
    case OpCode::USER:
    case OpCode::PUSH:
    case OpCode::POP:
    case OpCode::CALL:
        return 1;
    case OpCode::SET:
    case OpCode::CPY:
    case OpCode::CPYI:
    case OpCode::CPYI2:
    case OpCode::ADD:
    case OpCode::ADDI:
    case OpCode::SUBI:
    case OpCode::JIF:
    case OpCode::STOREI:
    case OpCode::LOADI:
        return 2;
    default:
        return -1;
    }
}

std::vector<Instruction> parseInstructionSection(std::ifstream &fileStream, const std::string &filename, int &lineOffset)
{
    // This parser now only handles .img files (pre-assembled)
    // For .g312 files, use the standalone assembler first: tools/gtu_assembler file.g312 file.img
    
    bool is_img_file = filename.size() >= 4 && filename.substr(filename.size() - 4) == ".img";
    
    if (!is_img_file) {
        throw std::runtime_error("Parser now only supports .img files. For .g312 files, please assemble first using: tools/gtu_assembler " + filename + " " + filename.substr(0, filename.size()-5) + ".img");
    }

    std::vector<Instruction> instructions;
    bool inInstructionSection = false;
    std::string line;
    int current_line_num = 0;

    // Rewind to beginning
    fileStream.clear();
    fileStream.seekg(0, std::ios::beg);
    
    while (std::getline(fileStream, line)) {
        current_line_num++;
        lineOffset = current_line_num; // Keep track for error messages
        
        std::string upper_line = line;
        std::transform(upper_line.begin(), upper_line.end(), upper_line.begin(), 
                       [](unsigned char c){ return std::toupper(c); });

        if (upper_line.find("BEGIN INSTRUCTION SECTION") != std::string::npos) {
            inInstructionSection = true;
            continue;
        }
        if (upper_line.find("END INSTRUCTION SECTION") != std::string::npos) {
            inInstructionSection = false;
            break;
        }

        // Skip comment lines and empty lines
        std::string trimmed_line = line;
        size_t comment_pos = trimmed_line.find('#');
        if (comment_pos != std::string::npos) {
            trimmed_line = trimmed_line.substr(0, comment_pos);
        }
        // Trim whitespace
        trimmed_line.erase(0, trimmed_line.find_first_not_of(" \t\n\r\f\v"));
        trimmed_line.erase(trimmed_line.find_last_not_of(" \t\n\r\f\v") + 1);
        
        if (!inInstructionSection || trimmed_line.empty()) {
            continue;
        }

        // Parse instruction line (should be in format: "PC MNEMONIC [args...]")
        std::istringstream iss(trimmed_line);
        int parsedLineNum;
        std::string mnemonic_str;

        if (!(iss >> parsedLineNum)) {
            throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": Missing instruction line number.");
        }

        if (!(iss >> mnemonic_str)) {
            throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": Missing mnemonic.");
        }
        std::transform(mnemonic_str.begin(), mnemonic_str.end(), mnemonic_str.begin(), 
                       [](unsigned char c){ return std::toupper(c); });

        Instruction instr;
        instr.original_line = line;
        long arg1_val = 0, arg2_val = 0;
        std::string operands_part_str;
        std::getline(iss, operands_part_str);
        
        size_t first_char = operands_part_str.find_first_not_of(" \t");
        if(first_char != std::string::npos) {
            operands_part_str = operands_part_str.substr(first_char);
        }

        if (mnemonic_str == "SYSCALL") {
            std::string syscallType;
            std::istringstream ops_iss(operands_part_str);
            if (!(ops_iss >> syscallType))
                throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": SYSCALL missing type.");
            
            std::transform(syscallType.begin(), syscallType.end(), syscallType.begin(), 
                           [](unsigned char c){ return std::toupper(c); });

            if (syscallType == "PRN") {
                instr.opcode = OpCode::SYSCALL_PRN;
                if (!(ops_iss >> arg1_val)) throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": SYSCALL PRN missing argument.");
                instr.arg1 = arg1_val;
                instr.num_operands = 1;
            } else if (syscallType == "HLT") {
                instr.opcode = OpCode::SYSCALL_HLT_THREAD;
                instr.num_operands = 0;
            } else if (syscallType == "YIELD") {
                instr.opcode = OpCode::SYSCALL_YIELD;
                instr.num_operands = 0;
            } else {
                throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": Unknown SYSCALL type '" + syscallType + "'.");
            }
        } else {
            auto it = MNEMONIC_TO_OPCODE.find(mnemonic_str);
            if (it == MNEMONIC_TO_OPCODE.end()) {
                throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": Unknown mnemonic '" + mnemonic_str + "'.");
            }
            instr.opcode = it->second;

            int expected_operands = getExpectedOperandCount(instr.opcode);
            instr.num_operands = expected_operands;
            
            if (!parseOperands(operands_part_str, arg1_val, arg2_val, expected_operands)) {
                throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": " + mnemonic_str + " expects " + std::to_string(expected_operands) + " operand(s).");
            }
            
            instr.arg1 = arg1_val;
            instr.arg2 = arg2_val;
        }

        if (parsedLineNum < 0) {
            throw std::runtime_error("Error L" + std::to_string(current_line_num) + ": Instruction PC cannot be negative.");
        }
        
        if (static_cast<size_t>(parsedLineNum) >= instructions.size()) {
            instructions.resize(static_cast<size_t>(parsedLineNum) + 1);
        }
        instructions[static_cast<size_t>(parsedLineNum)] = instr;
    }

    return instructions;
} 
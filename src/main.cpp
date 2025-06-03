#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <algorithm> // For std::transform, std::remove
#include <limits>    // For std::numeric_limits
#include <iomanip>   // For std::setw (used in thread table dump)
#include <cstring>

#include "memory.h"
#include "cpu.h"    // This should also bring in OpCode and Instruction struct
#include "common.h" // For constants like CPU_OS_COMM_ADDR, OS_DATA_START_ADDR etc.

// Global map for mnemonics to OpCode. Defined here for parseInstructionSection.
const std::unordered_map<std::string, OpCode> MNEMONIC_TO_OPCODE = {
    {"SET", OpCode::SET}, {"CPY", OpCode::CPY}, {"CPYI", OpCode::CPYI}, {"CPYI2", OpCode::CPYI2}, {"ADD", OpCode::ADD}, {"ADDI", OpCode::ADDI}, {"SUBI", OpCode::SUBI}, {"JIF", OpCode::JIF}, {"PUSH", OpCode::PUSH}, {"POP", OpCode::POP}, {"CALL", OpCode::CALL}, {"RET", OpCode::RET}, {"HLT", OpCode::HLT}, {"USER", OpCode::USER}
    // SYSCALL variants are handled separately in parsing
};

// Forward declaration for our instruction parser
std::vector<Instruction> parseInstructionSection(std::ifstream &fileStream, const std::string &filename, int &lineOffset);

// PRN system call handler
void handlePrnSyscall(long value)
{
    std::cout << value << std::endl;
}

void dumpMemoryForDebug(const Memory &mem, int debug_mode, bool after_halt = false)
{
    if (debug_mode == 0 && after_halt)
    {
        mem.dumpImportantRegions(std::cerr);
    }
    else if (debug_mode == 1)
    {
        mem.dumpMemoryRange(std::cerr, 0, mem.getSize() - 1);
    }
    else if (debug_mode == 2)
    {
        mem.dumpMemoryRange(std::cerr, 0, mem.getSize() - 1);
        std::cerr << "--- Press ENTER to continue to next tick ---" << std::endl;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    // Debug mode 3 is handled separately for thread table dump
}

void dumpThreadTableForDebug3(const Memory &mem, std::ostream &out)
{
    out << "--- Thread Table Dump (OS Data Area: "
        << OS_DATA_START_ADDR << "-" << OS_DATA_END_ADDR << ") ---" << std::endl;

    out << "Addr  | Value     | Likely Meaning (Example OS Convention)" << std::endl;
    out << "----------------------------------------------------------" << std::endl;
    bool has_printed_anything = false;
    for (long addr = OS_DATA_START_ADDR; addr <= OS_DATA_END_ADDR; ++addr)
    {
        long value = mem.read(addr);
        if (addr < OS_DATA_START_ADDR + 50 || value != 0)
        { // Heuristic to print some data
            out << std::setw(5) << addr << " | " << std::setw(9) << value << " | ";
            // Example interpretation (highly dependent on OS design)
            if (addr == OS_DATA_START_ADDR)
                out << "OS Info / Current Thread ID Pointer";
            else if (addr > OS_DATA_START_ADDR && addr <= OS_DATA_START_ADDR + 100)
            {
            }
            out << std::endl;
            has_printed_anything = true;
        }
    }
    if (!has_printed_anything)
    {
        out << "(OS data area is all zeros or not fitting display heuristic)" << std::endl;
    }
    out << "----------------------------------------------------------" << std::endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: ./sim <program_filename> [-D<0|1|2|3>] [--memory-size <size_in_longs>]" << std::endl;
        return 1;
    }

    std::string filename = "";
    int debug_mode = -1;        // Default: no debug output beyond PRN
    size_t memory_size = 11000; // Default memory size

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg.rfind("-D", 0) == 0)
        { // Starts with -D
            std::string mode_str;
            if (arg.length() == 3 && isdigit(arg[2]))
            { // -D0, -D1, etc.
                mode_str = arg.substr(2, 1);
            }
            else if (arg.length() == 2 && (i + 1 < argc) && isdigit(argv[i + 1][0]) && strlen(argv[i + 1]) == 1)
            { // -D 0, -D 1
                mode_str = argv[i + 1];
                i++; // Consume next argument
            }
            else
            {
                std::cerr << "Error: Malformed debug flag. Use -D<0|1|2|3> or -D <0|1|2|3>." << std::endl;
                return 1;
            }
            try
            {
                debug_mode = std::stoi(mode_str);
                if (debug_mode < 0 || debug_mode > 3)
                { // Now includes 3
                    std::cerr << "Error: Invalid debug mode. Must be 0, 1, 2, or 3." << std::endl;
                    return 1;
                }
            }
            catch (const std::exception &)
            {
                std::cerr << "Error: Invalid debug mode value. Must be an integer 0, 1, 2, or 3." << std::endl;
                return 1;
            }
        }
        else if (arg == "--memory-size" || arg == "-m")
        {
            if (i + 1 < argc)
            {
                try
                {
                    memory_size = std::stoul(argv[++i]);
                    if (memory_size < USER_MEMORY_START_ADDR)
                    { // Minimum reasonable size
                        std::cerr << "Warning: Specified memory size " << memory_size << " is very small. Recommended >= " << USER_MEMORY_START_ADDR << "." << std::endl;
                    }
                    if (memory_size == 0)
                    {
                        std::cerr << "Error: Memory size cannot be zero." << std::endl;
                        return 1;
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error: Invalid memory size value. Must be a positive integer. " << e.what() << std::endl;
                    return 1;
                }
            }
            else
            {
                std::cerr << "Error: --memory-size or -m option requires a value." << std::endl;
                return 1;
            }
        }
        else if (filename.empty())
        {
            filename = arg;
        }
        else
        {
            std::cerr << "Error: Unknown argument or filename specified twice: " << arg << std::endl;
            std::cerr << "Usage: ./sim <program_filename> [-D<0|1|2|3>] [--memory-size <size_in_longs>]" << std::endl;
            return 1;
        }
    }

    if (filename.empty())
    {
        std::cerr << "Error: Program filename is required." << std::endl;
        std::cerr << "Usage: ./sim <program_filename> [-D<0|1|2|3>] [--memory-size <size_in_longs>]" << std::endl;
        return 1;
    }

    Memory systemMemory(memory_size); // Default size, can be configurable later
    std::vector<Instruction> programInstructions;

    std::ifstream programFile(filename);
    if (!programFile.is_open())
    {
        std::cerr << "Error: Could not open program file '" << filename << "'." << std::endl;
        return 1;
    }

    int total_lines_read_for_error = 0;
    if (!systemMemory.loadDataSection(programFile, total_lines_read_for_error))
    {
        programFile.close();
        return 1;
    }

    // 2. Parse Instruction Section
    try
    {
        programInstructions = parseInstructionSection(programFile, filename, total_lines_read_for_error);
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
        std::cout << "Warning: No instructions found or parsed in '" << filename << "'. Program may only contain data or OS is purely data-driven." << std::endl;
    }

    // 3. Create CPU
    CPU gtu_cpu(systemMemory, programInstructions, handlePrnSyscall);

    long last_cpu_os_comm_val = 0; // For D3 mode, to detect changes
    long last_pc_for_d3 = -1;      // For D3 mode, to detect jumps to OS handlers

    // 4. Run Simulation Loop
    try
    {
        while (!gtu_cpu.isHalted())
        {

            if (debug_mode == 3)
            {
                long current_cpu_os_comm = systemMemory.read(CPU_OS_COMM_ADDR);
                long current_pc = gtu_cpu.getCurrentProgramCounter(); // PC for the *next* instruction

                bool os_handler_jump = (current_pc == OS_SYSCALL_DISPATCHER_PC ||
                                        current_pc == OS_MEMORY_FAULT_HANDLER_PC ||
                                        current_pc == OS_ARITHMETIC_FAULT_HANDLER_PC ||
                                        current_pc == OS_UNKNOWN_INSTRUCTION_HANDLER_PC);
                if ((current_cpu_os_comm != 0 && current_cpu_os_comm != last_cpu_os_comm_val) ||
                    (os_handler_jump && current_pc != last_pc_for_d3))
                {
                    std::cerr << "--- Debug Mode 3: Context Switch or System Call Detected ---" << std::endl;
                    if (current_cpu_os_comm != 0)
                    {
                        std::cerr << "Event (mem[2]): " << current_cpu_os_comm
                                  << " (PC for next OS instruction: " << current_pc << ")" << std::endl;
                    }
                    else if (os_handler_jump)
                    {
                        std::cerr << "Jump to OS handler at PC: " << current_pc << std::endl;
                    }
                    dumpThreadTableForDebug3(systemMemory, std::cerr);
                    if (debug_mode == 3)
                    { // D3 implies keypress like D2 for this detailed dump
                        std::cerr << "--- Press ENTER to continue ---" << std::endl;
                        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                    }
                }
                last_cpu_os_comm_val = current_cpu_os_comm; // Update for next iteration
                if (current_cpu_os_comm == 0 && os_handler_jump)
                {
                    // If comm was cleared but we jumped to OS handler, it's still a notable event.
                }
                last_pc_for_d3 = current_pc; // Store PC for next check
            }

            gtu_cpu.step();

            if (debug_mode == 1 || debug_mode == 2)
            {
                dumpMemoryForDebug(systemMemory, debug_mode);
            }

            // After CPU step, if OS handled event & cleared CPU_OS_COMM_ADDR, update for D3
            if (debug_mode == 3)
            {
                last_cpu_os_comm_val = systemMemory.read(CPU_OS_COMM_ADDR);
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

    std::cout << "CPU Halted." << std::endl;
    if (debug_mode == 0)
    {
        dumpMemoryForDebug(systemMemory, debug_mode, true);
    }
    // Final D3 dump if halted cleanly, showing final state of thread table
    if (debug_mode == 3 && gtu_cpu.isHalted())
    {
        std::cerr << "--- Debug Mode 3: Final Thread Table State (CPU Halted) ---" << std::endl;
        dumpThreadTableForDebug3(systemMemory, std::cerr);
    }

    std::cout << "Total instructions executed (from mem[" << INSTR_COUNT_ADDR << "]): "
              << systemMemory.read(INSTR_COUNT_ADDR) << std::endl;

    return 0;
}

// --- Instruction Parsing Implementation ---
std::vector<Instruction> parseInstructionSection(std::ifstream &fileStream, const std::string &filename, int &lineOffset)
{
    std::vector<Instruction> instructions;
    std::string line;
    bool inInstructionSection = false;
    int instructionIndex = 0; // To track instruction index within the section

    while (std::getline(fileStream, line))
    {
        lineOffset++;
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);
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
        return instructions; // Return empty vector
    }

    while (std::getline(fileStream, line))
    {
        lineOffset++;
        std::string originalInstLine = line; // Keep original for Instruction struct

        size_t commentPos = line.find('#');

        if (commentPos != std::string::npos)
        {
            line = line.substr(0, commentPos);
        }

        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line == "End Instruction Section")
            break;
        if (line.empty())
            continue;

        // The first token is "line_number_in_section" which we ignore for parsing the instruction itself,
        // but it's good for validation or if we were to support labels that resolve to these numbers.
        // For now, the CPU PC is an index into the vector, so these explicit numbers are mostly for human readability.

        std::istringstream iss(line);
        int parsedLineNum; // The '0', '1', '2' at the start of instruction lines
        std::string mnemonic_str;

        if (!(iss >> parsedLineNum))
        {
            throw std::runtime_error("Missing instruction line number at file line " + std::to_string(lineOffset) + ". Content: '" + originalInstLine + "'");
        }

        if (parsedLineNum != instructionIndex)
        {
            std::cerr << "Warning: Instruction line number mismatch at file line " << std::to_string(lineOffset)
                      << ". Expected " << instructionIndex << ", got " << parsedLineNum
                      << ". Using vector index for PC. Line: '" << originalInstLine << "'" << std::endl;
        }

        if (!(iss >> mnemonic_str))
        {
            throw std::runtime_error("Missing mnemonic at file line " + std::to_string(lineOffset) + ". Content: '" + originalInstLine + "'");
        }
        std::transform(mnemonic_str.begin(), mnemonic_str.end(), mnemonic_str.begin(), ::toupper); // To uppercase

        Instruction instr;
        instr.original_line = originalInstLine; // Store full original line
        long arg1_val = 0, arg2_val = 0;

        // Temporary string stream for parsing operands for current line
        std::string operands_part_str;

        std::getline(iss, operands_part_str); // Get " 10, 50" (with leading space)

        // Trim leading space from operands_part_str
        operands_part_str.erase(0, operands_part_str.find_first_not_of(" \t"));
        operands_part_str.erase(std::remove(operands_part_str.begin(), operands_part_str.end(), ','), operands_part_str.end());

        std::istringstream ops_iss(operands_part_str);

        if (mnemonic_str == "SYSCALL")
        {
            std::string syscallType;
            if (!(ops_iss >> syscallType))
                throw std::runtime_error("SYSCALL: Missing type (PRN, HLT, YIELD). File line " + std::to_string(lineOffset) + ": " + originalInstLine);
            std::transform(syscallType.begin(), syscallType.end(), syscallType.begin(), ::toupper);

            if (syscallType == "PRN")
            {
                instr.opcode = OpCode::SYSCALL_PRN;
                instr.num_operands = 1;
                if (!(ops_iss >> arg1_val))
                    throw std::runtime_error("SYSCALL PRN: Missing address argument. File line " + std::to_string(lineOffset) + ": " + originalInstLine);
                instr.arg1 = arg1_val;
            }
            else if (syscallType == "HLT")
            {
                instr.opcode = OpCode::SYSCALL_HLT_THREAD;
                instr.num_operands = 0;
            }
            else if (syscallType == "YIELD")
            {
                instr.opcode = OpCode::SYSCALL_YIELD;
                instr.num_operands = 0;
            }
            else
            {
                throw std::runtime_error("Unknown SYSCALL type: " + syscallType + ". File line " + std::to_string(lineOffset) + ": " + originalInstLine);
            }
        }
        else
        {
            auto it = MNEMONIC_TO_OPCODE.find(mnemonic_str);
            if (it == MNEMONIC_TO_OPCODE.end())
            {
                throw std::runtime_error("Unknown mnemonic: " + mnemonic_str + ". File line " + std::to_string(lineOffset) + ": " + originalInstLine);
            }
            instr.opcode = it->second;

            // Determine operands based on opcode
            switch (instr.opcode)
            {
            case OpCode::HLT:
            case OpCode::RET:
                instr.num_operands = 0;
                break;
            case OpCode::USER: // USER A (arg1=A)
            case OpCode::PUSH:
            case OpCode::POP:
            case OpCode::CALL: // CALL C (arg1=C)
                instr.num_operands = 1;
                if (!(ops_iss >> arg1_val))
                    throw std::runtime_error(mnemonic_str + ": Expects 1 operand. File line " + std::to_string(lineOffset) + ": " + originalInstLine);
                instr.arg1 = arg1_val;
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
                if (!(ops_iss >> arg1_val >> arg2_val))
                    throw std::runtime_error(mnemonic_str + ": Expects 2 operands. File line " + std::to_string(lineOffset) + ": " + originalInstLine);
                instr.arg1 = arg1_val;
                instr.arg2 = arg2_val;
                break;
            default: // Should not happen if MNEMONIC_TO_OPCODE is complete
                throw std::runtime_error("Internal parser error: Unhandled opcode " + mnemonic_str + " in switch. File line " + std::to_string(lineOffset));
            }
        }

        std::string junk;
        if (ops_iss >> junk && !junk.empty())
        {
            size_t junk_comment_pos = junk.find('#');
            if (junk_comment_pos == 0 || (junk_comment_pos == std::string::npos && !junk.empty()))
                throw std::runtime_error(mnemonic_str + ": Too many operands or trailing characters. File line " + std::to_string(lineOffset) + ": '" + originalInstLine + "'. Junk: '" + junk + "'");
        }

        instructions.push_back(instr);
        instructionIndex++;
    }

    // Check if "End Instruction Section" was found
    if (inInstructionSection && line != "End Instruction Section" && !fileStream.eof())
    {
        if (fileStream.eof())
        {
            throw std::runtime_error("'End Instruction Section' marker not found before EOF in " + filename + " (last relevant file line: " + std::to_string(lineOffset) + ")");
        }
    }
    return instructions;
}
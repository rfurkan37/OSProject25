#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <limits>
#include <iomanip>
#include <cctype>
#include <cstring>

#include "memory.h"
#include "cpu.h"
#include "common.h"
#include "instruction.h"
#include "parser.h"

void handlePrnSyscall(long value)
{
    std::cout << value << std::endl;
}

// Dump for -D0 (after halt) or -D1/-D2 (after each step)
void dumpMemoryForDebug(const Memory &mem, int debug_mode, bool after_halt = false)
{
    if (debug_mode == 0 && after_halt)
    {
        std::cerr << "--- Memory Dump After Halt ---" << std::endl;
        mem.dumpImportantRegions(std::cerr);
    }
    else if (debug_mode == 1)
    {
        // std::cerr << "--- Memory Dump After Step ---" << std::endl; // Optional header
        mem.dumpMemoryRange(std::cerr, 0, mem.getSize() - 1);
    }
    else if (debug_mode == 2)
    {
        std::cerr << "--- Memory Dump After Step ---" << std::endl;
        mem.dumpMemoryRange(std::cerr, 0, mem.getSize() - 1);
        std::cerr << "--- Press ENTER to continue to next tick ---" << std::endl;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

// Enhanced dump for -D3
void dumpThreadTableForDebug3(const Memory &mem, std::ostream &out)
{
    out << "--- Thread Table Dump ---" << std::endl;
    out << "TID | PC   | SP   | State | StartT | ExecsU | BlockU" << std::endl;
    out << "---------------------------------------------------------" << std::endl;

    // Read state constant values from memory as defined by os.g312
    long state_ready_val = mem.read(THREAD_STATE_READY);      // THREAD_STATE_READY_CONST
    long state_running_val = mem.read(THREAD_STATE_RUNNING);    // THREAD_STATE_RUNNING_CONST
    long state_blocked_val = mem.read(THREAD_STATE_BLOCKED);    // THREAD_STATE_BLOCKED_CONST
    long state_terminated_val = mem.read(THREAD_STATE_TERMINATED); // THREAD_STATE_TERMINATED_CONST

    // Read TCB configuration from OS data in memory
    long tcb_base_addr = mem.read(TCB_TABLE_START); // TCB_TABLE_START_ADDR_CONST
    long num_threads = mem.read(TOTAL_THREADS);   // num_total_threads_addr
    long tcb_size = mem.read(TCB_SIZE);      // TCB_SIZE_CONST

    if (tcb_size == 0)
    { // Prevent division by zero or infinite loop if TCB_SIZE is not set
        out << "Error: TCB_SIZE_CONST in memory (address 27) is zero. Cannot dump TCB table." << std::endl;
        return;
    }

    for (long i = 0; i < num_threads; ++i)
    { // Use long for i if num_threads can be large, else int
        long current_tcb_start_addr = tcb_base_addr + i * tcb_size;

        // Check if TCB address is valid to prevent reading out of bounds if config is wrong
        if (static_cast<size_t>(current_tcb_start_addr + tcb_size - 1) >= mem.getSize())
        {
            out << "Error: TCB for thread " << (i + 1) << " would be out of memory bounds." << std::endl;
            break;
        }

        out << std::setw(3) << (i + 1) << " | "                               // Assuming thread IDs are 1-based for display
            << std::setw(4) << mem.read(current_tcb_start_addr + 0) << " | "  // TCB_PC_OFFSET
            << std::setw(4) << mem.read(current_tcb_start_addr + 1) << " | "; // TCB_SP_OFFSET

        long state_val = mem.read(current_tcb_start_addr + 2); // TCB_State_OFFSET
        std::string state_str = "UNK(" + std::to_string(state_val) + ")";
        if (state_val == state_ready_val)
            state_str = "READY";
        else if (state_val == state_running_val)
            state_str = "RUNNG";
        else if (state_val == state_blocked_val)
            state_str = "BLOCK";
        else if (state_val == state_terminated_val)
            state_str = "TERMD";

        out << std::setw(5) << state_str << " | "
            << std::setw(6) << mem.read(current_tcb_start_addr + 3) << " | " // TCB_StartTime_OFFSET
            << std::setw(6) << mem.read(current_tcb_start_addr + 4) << " | " // TCB_ExecsUsed_OFFSET
            << std::setw(6) << mem.read(current_tcb_start_addr + 5)          // TCB_BlockUntil_OFFSET
            << std::endl;
    }

    out << "OS Current Thread ID: " << mem.read(CURRENT_THREAD_ID) << std::endl; // current_thread_id_addr
    out << "OS Next to Schedule:  " << mem.read(NEXT_THREAD_TO_SCHEDULE) << std::endl; // next_thread_to_schedule_addr
    out << "CPU Total Instr:      " << mem.read(INSTR_COUNT_ADDR) << std::endl;
    out << "CPU Event Code:       " << mem.read(CPU_OS_COMM_ADDR) << std::endl;
    out << "CPU Saved Trap PC:    " << mem.read(SAVED_TRAP_PC_ADDR) << std::endl;
    out << "CPU Syscall Arg1:     " << mem.read(SYSCALL_ARG1_PASS_ADDR) << std::endl;

    out << "---------------------------------------------------------" << std::endl;
}

struct ProgramArgs
{
    std::string filename;
    int debug_mode = -1;        // Default to no debug mode explicitly set
    size_t memory_size = 11000; // Default memory size
};

ProgramArgs parseArguments(int argc, char *argv[])
{
    ProgramArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg_str = argv[i];

        if (arg_str == "-D")
        {
            if (i + 1 < argc && strlen(argv[i + 1]) == 1 && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])))
            {
                args.debug_mode = std::stoi(argv[++i]);
            }
            else
            {
                throw std::runtime_error("Debug flag -D requires a single digit mode (0-3) as the next argument.");
            }
        }
        else if (arg_str.rfind("-D", 0) == 0 && arg_str.length() == 3 && std::isdigit(static_cast<unsigned char>(arg_str[2])))
        {
            // Handles -D0, -D1, -D2, -D3
            args.debug_mode = std::stoi(arg_str.substr(2, 1));
        }
        else if (arg_str == "--memory-size" || arg_str == "-m")
        {
            if (i + 1 < argc)
            {
                try
                {
                    args.memory_size = std::stoul(argv[++i]);
                }
                catch (const std::exception &e)
                {
                    throw std::runtime_error("Invalid value for --memory-size: " + std::string(argv[i]));
                }
                if (args.memory_size < USER_MEMORY_START_ADDR)
                { // USER_MEMORY_START_ADDR is 1000
                    std::cerr << "Warning: Small memory size " << args.memory_size
                              << ". Recommended >= " << USER_MEMORY_START_ADDR
                              << " for OS and threads." << std::endl;
                }
                if (args.memory_size == 0)
                {
                    throw std::runtime_error("Memory size cannot be zero.");
                }
            }
            else
            {
                throw std::runtime_error("--memory-size option requires a value.");
            }
        }
        else if (args.filename.empty())
        {
            args.filename = arg_str;
        }
        else
        {
            throw std::runtime_error("Unknown or misplaced argument: " + arg_str);
        }
    }

    if (args.filename.empty())
    {
        throw std::runtime_error("Program filename is required.");
    }

    if (args.debug_mode != -1 && (args.debug_mode < 0 || args.debug_mode > 3))
    {
        throw std::runtime_error("Invalid debug mode specified. Must be 0, 1, 2, or 3.");
    }
    if (args.debug_mode == -1)
        args.debug_mode = 0; // Default to mode 0 if not specified

    return args;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: ./gtu_sim <program_filename> [-D<0|1|2|3>] [--memory-size <size_in_longs>]" << std::endl;
        return 1;
    }

    ProgramArgs args;
    try
    {
        args = parseArguments(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Argument Error: " << e.what() << std::endl;
        std::cerr << "Usage: ./gtu_sim <program_filename> [-D<0|1|2|3>] [--memory-size <size_in_longs>]" << std::endl;
        return 1;
    }

    Memory systemMemory(args.memory_size);
    std::vector<Instruction> programInstructions;

    std::ifstream programFile(args.filename);
    if (!programFile.is_open())
    {
        std::cerr << "Error: Could not open program file '" << args.filename << "'." << std::endl;
        return 1;
    }

    int total_lines_read_for_error = 0;
    if (!systemMemory.loadDataSection(programFile, total_lines_read_for_error))
    {
        // Error message already printed by loadDataSection
        return 1;
    }

    try
    {
        // Pass total_lines_read_for_error by reference so it's updated
        programInstructions = parseInstructionSection(programFile, args.filename, total_lines_read_for_error);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Error parsing instruction section: " << e.what() << std::endl;
        return 1;
    }
    programFile.close();

    long initial_pc = systemMemory.read(PC_ADDR);
    if (initial_pc == 0 && programInstructions.empty())
    {
        std::cerr << "Warning: PC is 0 and no instructions loaded. CPU will likely halt or fault immediately." << std::endl;
    }
    else if (initial_pc == 0 && !programInstructions.empty() && OS_BOOT_START_PC != 0)
    {
        std::cerr << "Warning: Initial PC is 0 from data section. OS boot is expected at "
                  << OS_BOOT_START_PC << " (or as per data section 0 value). "
                  << "If OS instructions start at PC 0, this might be fine." << std::endl;
    }

    CPU gtu_cpu(systemMemory, programInstructions, handlePrnSyscall);

    int cycle_count = 0;
    constexpr int MAX_CYCLES = 200000; // Increased max cycles for potentially longer OS runs

    bool prev_is_user_mode = gtu_cpu.isInUserMode(); // Initial state before first step

    while (!gtu_cpu.isHalted() && cycle_count < MAX_CYCLES)
    {
        // For -D3: condition check *before* step, based on state *about to be caused by OS* or *just caused by thread*
        // This is tricky. Let's try state change *after* step.


        /* long current_pc_for_debug = gtu_cpu.getCurrentProgramCounter();
        if (current_pc_for_debug >= 0 && static_cast<size_t>(current_pc_for_debug) < programInstructions.size())
        {
            // Ensure there's a valid instruction at the PC to prevent crashing the simulator
            const Instruction &instr_to_exec = programInstructions[static_cast<size_t>(current_pc_for_debug)];
            if (instr_to_exec.opcode != OpCode::UNKNOWN || !instr_to_exec.original_line.empty())
            {
                // Print to std::cerr so it doesn't interfere with the program's SYSCALL PRN output
                std::cerr << "Cycle " << std::setw(5) << cycle_count
                          << " | PC: " << std::setw(4) << current_pc_for_debug
                          << " | Executing: " << instr_to_exec.original_line << std::endl;
            }
        }
        else
        {
            std::cerr << "Cycle " << std::setw(5) << cycle_count
                      << " | PC: " << std::setw(4) << current_pc_for_debug
                      << " | NOTE: PC is out of instruction bounds. CPU will fault." << std::endl;
        }  */

        gtu_cpu.step();
        cycle_count++;

        bool current_is_user_mode = gtu_cpu.isInUserMode();
        CpuEvent current_event_code = static_cast<CpuEvent>(systemMemory.read(CPU_OS_COMM_ADDR));

        if (args.debug_mode == 3)
        {
            bool syscall_like_event_occurred = (current_event_code != CpuEvent::NONE);
            bool context_switch_to_user = !prev_is_user_mode && current_is_user_mode;
            bool syscall_trap_to_kernel = prev_is_user_mode && !current_is_user_mode && syscall_like_event_occurred;
            
            // For debug mode 3, we should trigger on any syscall or context switch
            // This includes: any non-NONE event, or mode transitions
            bool should_dump_thread_table = context_switch_to_user || syscall_trap_to_kernel || syscall_like_event_occurred;

            if (should_dump_thread_table)
            {
                std::cerr << "--- D3: Event Trigger (Cycle " << cycle_count << ") ---" << std::endl;
                if (context_switch_to_user)
                    std::cerr << "Context switch to USER detected." << std::endl;
                if (syscall_trap_to_kernel)
                    std::cerr << "Syscall/Trap to KERNEL detected. Event: " << static_cast<long>(current_event_code) << std::endl;
                if (syscall_like_event_occurred && !syscall_trap_to_kernel)
                    std::cerr << "System call event detected. Event: " << static_cast<long>(current_event_code) << std::endl;
                    
                dumpThreadTableForDebug3(systemMemory, std::cerr);
                
                // DEBUG MODE SHOULD ONLY OBSERVE, NOT MODIFY SYSTEM STATE
                // The OS itself will clear events when appropriate - we don't interfere
                std::cerr << "Event preserved for OS handling (not cleared by debug mode)." << std::endl;
                
                // Optional pause for mode 3 event
                std::cerr << "--- Press ENTER to continue after D3 event ---" << std::endl;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        // Dumps for -D1, -D2 happen after step
        if (args.debug_mode == 1)
        {
            dumpMemoryForDebug(systemMemory, args.debug_mode);
        }
        else if (args.debug_mode == 2)
        {
            dumpMemoryForDebug(systemMemory, args.debug_mode); // This includes its own "Press ENTER"
            // No need for "Cycle ... completed" here as dumpMemoryForDebug handles the pause
        }
        prev_is_user_mode = current_is_user_mode;
        if (current_event_code != CpuEvent::NONE && !gtu_cpu.isInUserMode())
        { // Clear event if OS is handling it
            // This assumes OS will handle it in its current timeslice.
            // Or OS can clear it by writing 0 to CPU_OS_COMM_ADDR
            // memory.write(CPU_OS_COMM_ADDR, static_cast<long>(CpuEvent::NONE)); // CPU might do this or OS does
        }
    }

    if (gtu_cpu.isHalted())
    {
        std::cout << "Program HLT instruction executed after " << cycle_count << " cycles." << std::endl;
    }
    else if (cycle_count >= MAX_CYCLES)
    {
        std::cerr << "Program terminated: Maximum cycle limit reached (" << MAX_CYCLES << ")." << std::endl;
    }
    else
    {
        std::cout << "Program ended for unknown reason after " << cycle_count << " cycles." << std::endl;
    }

    // Final dump for mode 0 (or always if desired)
    if (args.debug_mode == 0 || args.debug_mode == -1)
    {                                              // -1 was if not set, now defaults to 0
        dumpMemoryForDebug(systemMemory, 0, true); // Mode 0 dump after halt
    }
    else if (gtu_cpu.isHalted())
    { // If halted and was in D1, D2, D3, still good to see final state
        std::cerr << "--- Final Memory State After Halt ---" << std::endl;
        systemMemory.dumpImportantRegions(std::cerr);
    }

    return 0;
}
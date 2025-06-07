// src/cpu.cpp
#include "cpu.h"
#include "memory.h"      // Now included in implementation
#include "instruction.h" // Now included in implementation  
#include "common.h"
#include <iostream>  // For error messages, potentially for SYSCALL_PRN if callback not used externally
#include <stdexcept> // For runtime_error
#include <vector>    // For std::vector
#include <sstream>   // For std::ostringstream

// Constructor
CPU::CPU(Memory &mem,
         const std::vector<Instruction> &instructions,
         std::function<void(long)> prn_callback)
    : memory_(mem),
      program_instructions_(instructions),
      prn_system_call_handler_(prn_callback),
      halted_flag_(false),
      user_mode_flag_(false),// CPU starts in KERNEL mode
      pc_modified_by_data_operation_(false) 
{
    // Ensure memory has minimal size for registers
    if (memory_.getSize() < REGISTERS_END_ADDR + 1)
    { 
        throw std::runtime_error("Memory size too small for CPU registers.");
    }
    reset(); // Initialize registers from memory (or to defaults if memory is zeroed)
}

// Resets CPU state
void CPU::reset()
{
    halted_flag_ = false;
    user_mode_flag_ = false; // Start in kernel mode
    pc_modified_by_data_operation_ = false;
}

// --- Register Access Helper Methods ---
long CPU::getPC() const
{
    return memory_.read(PC_ADDR);
}

void CPU::setPC(long new_pc)
{
    memory_.write(PC_ADDR, new_pc);
}

long CPU::getSP() const
{
    return memory_.read(SP_ADDR);
}

void CPU::setSP(long new_sp)
{
    memory_.write(SP_ADDR, new_sp);
}

void CPU::incrementInstructionCounter()
{
    memory_.write(INSTR_COUNT_ADDR, memory_.read(INSTR_COUNT_ADDR) + 1);
}

void CPU::setCpuEvent(CpuEvent event)
{
    memory_.write(CPU_OS_COMM_ADDR, static_cast<long>(event));
}

long CPU::getCurrentProgramCounter() const
{
    return getPC();
}

// --- Memory Access with User Mode Protection ---

// Privileged read: for CPU internal use, e.g., reading PC, SP, which are always accessible
long CPU::privilegedRead(long address)
{
    try
    {
        return memory_.read(address);
    }
    catch (const std::out_of_range &e)
    {
        std::ostringstream oss;
        oss << "CPU privileged memory read out of bounds at address " << address << ". Details: " << e.what();
        throw std::runtime_error(oss.str());
    }
}

// Checked read: enforces user mode restrictions
long CPU::checkedRead(long address)
{
    if (user_mode_flag_ && address < USER_MEMORY_START_ADDR && address >= 0) // Check address >=0 to allow -1 like addresses to be caught by memory system
    {
        throw UserMemoryFaultException("User mode read access violation", address);
    }
    try
    {
        return memory_.read(address);
    }
    catch (const std::out_of_range &e)
    {
        std::ostringstream oss;
        oss << "CPU memory read out of bounds at address " << address << ". Details: " << e.what();
        throw std::runtime_error(oss.str()); // Re-throw as a more general CPU error or specific out of bounds
    }
}

// Checked write: enforces user mode restrictions
void CPU::checkedWrite(long address, long value)
{
    if (user_mode_flag_ && address < USER_MEMORY_START_ADDR && address >= 0)
    {
        throw UserMemoryFaultException("User mode write access violation", address);
    }
    try
    {
        memory_.write(address, value);
        if (address == PC_ADDR) 
            this->pc_modified_by_data_operation_ = true;
    }
    catch (const std::out_of_range &e)
    {
        std::ostringstream oss;
        oss << "CPU memory write out of bounds at address " << address << ". Details: " << e.what();
        throw std::runtime_error(oss.str()); // Re-throw
    }
}

// --- Main Execution Step ---
void CPU::step()
{
    if (halted_flag_)
    {
        return; // CPU is halted, do nothing
    }

    long current_pc = getPC();
    this->pc_modified_by_data_operation_ = false;

    Instruction instr_for_error_reporting;
    instr_for_error_reporting.opcode = OpCode::UNKNOWN; // Initially unknown

    long next_pc = current_pc + 1; // Default next PC
    bool pc_modified_by_instruction = false;
    
    try
    {
        if (current_pc < 0 || static_cast<size_t>(current_pc) >= program_instructions_.size())
        {
            std::ostringstream oss;
            oss << "Program Counter (" << current_pc
                << ") is out of instruction bounds (0-"
                << (program_instructions_.empty() ? 0 : program_instructions_.size() - 1) << ").";
            throw std::runtime_error(oss.str());
        }

        const Instruction &instr = program_instructions_[static_cast<size_t>(current_pc)];
        instr_for_error_reporting = instr;

        // Check for 'holes' in the instruction vector (parsedLineNum skipped)
        // These are default-constructed Instructions with UNKNOWN opcode and empty original_line
        if (instr.opcode == OpCode::UNKNOWN && instr.original_line.empty()) {
            std::cerr << "CPU WARNING: Encountered uninitialized instruction (hole) at PC " << current_pc
                      << ". Treating as HLT." << std::endl;
            halted_flag_ = true;
            next_pc = current_pc; // PC should point at this implicit HLT
            pc_modified_by_instruction = true;
        } else {
            // Normal instruction processing via switch
            switch (instr.opcode)
            {
            case OpCode::SET: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("SET: Invalid number of operands.");
                checkedWrite(instr.arg2, instr.arg1);
                break;

            case OpCode::CPY: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("CPY: Invalid number of operands.");
                {
                    long val_a1 = checkedRead(instr.arg1);
                    checkedWrite(instr.arg2, val_a1);
                }
                break;

            case OpCode::CPYI: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("CPYI: Invalid number of operands.");
                {
                    long addr_from_a1 = checkedRead(instr.arg1);          
                    long val_at_indirect_addr = checkedRead(addr_from_a1); 
                    checkedWrite(instr.arg2, val_at_indirect_addr);       
                }
                break;

            case OpCode::CPYI2:
                if (instr.num_operands != 2)
                    throw std::runtime_error("CPYI2: Invalid number of operands.");
                {
                    long address_X = checkedRead(instr.arg1);
                    long address_Y = checkedRead(instr.arg2);
                    checkedWrite(address_Y, checkedRead(address_X));
                }
                break;

            case OpCode::ADD: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("ADD: Invalid number of operands.");
                {
                    long val_a = checkedRead(instr.arg1);
                    checkedWrite(instr.arg1, val_a + instr.arg2);
                }
                break;

            case OpCode::ADDI: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("ADDI: Invalid number of operands.");
                {
                    long val_a1 = checkedRead(instr.arg1);
                    long val_a2 = checkedRead(instr.arg2);
                    checkedWrite(instr.arg1, val_a1 + val_a2);
                }
                break;

            case OpCode::SUBI: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("SUBI: Invalid number of operands.");
                {
                    long val_a1 = checkedRead(instr.arg1);
                    long val_a2 = checkedRead(instr.arg2);
                    checkedWrite(instr.arg2, val_a1 - val_a2); 
                }
                break;

            case OpCode::STOREI:
                if (instr.num_operands != 2)
                    throw std::runtime_error("STOREI: Invalid number of operands.");
                {
                    long src_value = checkedRead(instr.arg1);        // Get value from source address
                    long ptr_addr_value = checkedRead(instr.arg2);   // Get pointer address
                    checkedWrite(ptr_addr_value, src_value);         // mem[mem[Ptr_Addr]] = mem[Src_Addr]
                }
                break;

            case OpCode::LOADI:
                if (instr.num_operands != 2)
                    throw std::runtime_error("LOADI: Invalid number of operands.");
                {
                    long ptr_addr_value = checkedRead(instr.arg1);   // Get pointer address
                    long indirect_value = checkedRead(ptr_addr_value); // Get value from indirect address
                    checkedWrite(instr.arg2, indirect_value);        // mem[Dest_Addr] = mem[mem[Ptr_Addr]]
                }
                break;

            case OpCode::JIF: 
                if (instr.num_operands != 2)
                    throw std::runtime_error("JIF: Invalid number of operands.");
                {
                    long val_a = checkedRead(instr.arg1);
                    if (val_a <= 0)
                    {
                        next_pc = instr.arg2; 
                        pc_modified_by_instruction = true;
                    }
                }
                break;

            case OpCode::PUSH: 
                if (instr.num_operands != 1)
                    throw std::runtime_error("PUSH: Invalid number of operands.");
                {
                    long sp = getSP();
                    sp--; 
                    if (sp < 0) // Basic check, detailed bounds check by checkedWrite
                        throw std::runtime_error("Stack overflow during PUSH (SP would be negative).");
                    setSP(sp);
                    long val_a = checkedRead(instr.arg1);
                    checkedWrite(sp, val_a);
                }
                break;

            case OpCode::POP: 
                if (instr.num_operands != 1)
                    throw std::runtime_error("POP: Invalid number of operands.");
                {
                    long sp = getSP();
                    // checkedRead will throw if sp is out of memory bounds.
                    // No specific check like sp >= memory_.getSize() needed here if checkedRead is robust.
                    long val_from_stack = checkedRead(sp); // This checks if sp is valid address
                    setSP(sp + 1); 
                    checkedWrite(instr.arg1, val_from_stack);
                }
                break;

            case OpCode::CALL: 
                if (instr.num_operands != 1)
                    throw std::runtime_error("CALL: Invalid number of operands.");
                {
                    long sp = getSP();
                    sp--; 
                    if (sp < 0)
                        throw std::runtime_error("Stack overflow during CALL (SP would be negative).");
                    setSP(sp);
                    checkedWrite(sp, current_pc + 1); // Push return address (PC of instruction AFTER call)
                    next_pc = instr.arg1; 
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::RET: 
                if (instr.num_operands != 0)
                    throw std::runtime_error("RET: Invalid number of operands.");
                {
                    long sp = getSP();
                    long return_addr = checkedRead(sp); // Check if sp is valid address
                    setSP(sp + 1);
                    next_pc = return_addr;
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::HLT: 
                if (instr.num_operands != 0)
                    throw std::runtime_error("HLT: Invalid number of operands.");
                halted_flag_ = true;
                next_pc = current_pc; // PC remains at HLT instruction
                pc_modified_by_instruction = true; 
                break;

            case OpCode::USER: 
                if (instr.num_operands != 1)
                    throw std::runtime_error("USER: Invalid number of operands.");
                // No check for already in user mode, OS might use it to re-enter with a new PC.
                // CPU switches to kernel mode on syscall/fault. OS must use USER to return to thread.
                {
                    long target_pc_value = checkedRead(instr.arg1); 
                    next_pc = target_pc_value;                        
                    user_mode_flag_ = true;
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::SYSCALL_PRN: 
                if (instr.num_operands != 1)
                    throw std::runtime_error("SYSCALL PRN: Invalid number of operands.");
                {
                    user_mode_flag_ = false; // Enter Kernel mode for syscall

                    long val_to_print = checkedRead(instr.arg1); 
                    if (prn_system_call_handler_)
                    {
                        prn_system_call_handler_(val_to_print);
                    }
                    else
                    {
                        std::cout << val_to_print << std::endl;
                    }

                    memory_.write(SAVED_TRAP_PC_ADDR, current_pc + 1); // Save PC of *next* instruction
                    setCpuEvent(CpuEvent::SYSCALL_PRN);               
                    memory_.write(SYSCALL_ARG1_PASS_ADDR, instr.arg1); 
                    next_pc = OS_SYSCALL_DISPATCHER_PC;                  
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::SYSCALL_HLT_THREAD: 
                if (instr.num_operands != 0)
                    throw std::runtime_error("SYSCALL HLT_THREAD: Invalid number of operands.");
                {
                    user_mode_flag_ = false; 
                    memory_.write(SAVED_TRAP_PC_ADDR, current_pc + 1);
                    setCpuEvent(CpuEvent::SYSCALL_HLT_THREAD); 
                    next_pc = OS_SYSCALL_DISPATCHER_PC;
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::SYSCALL_YIELD: 
                if (instr.num_operands != 0)
                    throw std::runtime_error("SYSCALL YIELD: Invalid number of operands.");
                {
                    user_mode_flag_ = false; 
                    memory_.write(SAVED_TRAP_PC_ADDR, current_pc + 1);
                    setCpuEvent(CpuEvent::SYSCALL_YIELD); 
                    next_pc = OS_SYSCALL_DISPATCHER_PC;
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::UNKNOWN: // Genuine unknown/unimplemented instruction (not a hole)
            default:
                std::cerr << "CPU FAULT: Unknown or unimplemented opcode encountered at PC " << current_pc
                          << ". Instruction: " << instr.original_line << std::endl;
                if (user_mode_flag_)
                {
                    user_mode_flag_ = false;                          
                    memory_.write(SAVED_TRAP_PC_ADDR, current_pc);    
                    setCpuEvent(CpuEvent::UNKNOWN_INSTRUCTION_FAULT); 
                    next_pc = OS_UNKNOWN_INSTRUCTION_HANDLER_PC;              
                    pc_modified_by_instruction = true;
                }
                else // Kernel mode unknown instruction is fatal
                {
                    halted_flag_ = true; 
                    next_pc = current_pc; // PC points at the faulting unknown instruction
                    pc_modified_by_instruction = true;
                }
                break;
            }
        } // End of 'else' for hole check
    }
    catch (const UserMemoryFaultException &umf)
    {
        std::cerr << "CPU FAULT: User mode memory fault during execution of instruction at PC "
                  << current_pc;
        if (!instr_for_error_reporting.original_line.empty()) { // If instruction was fetched
            std::cerr << " (" << instr_for_error_reporting.original_line << ")";
        }
        std::cerr << ":\n" << "  " << umf.what() << " at address " << umf.faulting_address << std::endl;

        user_mode_flag_ = false; // Switch to Kernel mode
        memory_.write(SAVED_TRAP_PC_ADDR, current_pc); // Save faulting PC
        setCpuEvent(CpuEvent::MEMORY_FAULT_USER);      
        memory_.write(SYSCALL_ARG1_PASS_ADDR, umf.faulting_address); 
        next_pc = OS_MEMORY_FAULT_HANDLER_PC; 
        pc_modified_by_instruction = true; 
    }
    catch (const std::runtime_error &e)
    { 
        std::cerr << "CPU FAULT: Runtime error during execution of instruction at PC " << current_pc;
        if (!instr_for_error_reporting.original_line.empty() && instr_for_error_reporting.opcode != OpCode::UNKNOWN) {
            std::cerr << " (" << instr_for_error_reporting.original_line << ")";
        }
        std::cerr << ":\n  " << e.what() << std::endl;

        if (user_mode_flag_) { 
            user_mode_flag_ = false; 
            memory_.write(SAVED_TRAP_PC_ADDR, current_pc); 
            // Determine fault type. For now, assume arithmetic or generic.
            // This could be more specific if ArithmeticFaultException is thrown by ops.
            bool is_stack_issue = (std::string(e.what()).find("Stack overflow") != std::string::npos ||
                                   std::string(e.what()).find("Stack underflow") != std::string::npos);
            if (is_stack_issue) {
                 setCpuEvent(CpuEvent::MEMORY_FAULT_USER); // Treat stack issues as memory faults
                 next_pc = OS_MEMORY_FAULT_HANDLER_PC;
            } else if (std::string(e.what()).find("out of instruction bounds") != std::string::npos) {
                 setCpuEvent(CpuEvent::UNKNOWN_INSTRUCTION_FAULT); // PC out of bounds
                 next_pc = OS_UNKNOWN_INSTRUCTION_HANDLER_PC;
            }
            else {
                 setCpuEvent(CpuEvent::ARITHMETIC_FAULT); // Generic runtime error in user, assume arithmetic or similar
                 next_pc = OS_ARITHMETIC_FAULT_HANDLER_PC;
            }
        } else { // Kernel mode runtime error
            halted_flag_ = true; 
            next_pc = current_pc; // PC points at the faulting instruction
        }
        pc_modified_by_instruction = true; 
    }

    // Increment instruction counter for any processed instruction/attempt,
    // unless CPU was already halted before this step.
    // HLT executed in this step still counts. Faults also count.
    incrementInstructionCounter();

    if (!halted_flag_)
    {
        if (pc_modified_by_instruction) {
            // PC was determined by the instruction (JIF, CALL, RET, USER, SYSCALL, trap to OS)
            // or by HLT (next_pc = current_pc, but halted_flag_ will be true).
            // The value in 'next_pc' is the target.
            setPC(next_pc);
        } else if (this->pc_modified_by_data_operation_) {
            // PC was directly written by SET/CPY to PC_ADDR.
            // The new PC value is already in memory_[PC_ADDR].
            // No further setPC() needed here, as getPC() in the next cycle will read it.
        } else {
            // Normal instruction, PC not modified by instruction or data op.
            // next_pc here is still current_pc + 1.
            setPC(next_pc);
        }
    }
    // If halted_flag_ is true (either from HLT or kernel fault), PC will not be updated here,
    // preserving current_pc (or the PC of HLT/faulting instruction) in memory_[PC_ADDR].
}
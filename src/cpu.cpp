// src/cpu.cpp
#include "cpu.h"
#include "common.h"
#include "instruction.h"
#include <iostream>  // For error messages, potentially for SYSCALL_PRN if callback not used externally
#include <stdexcept> // For runtime_error
#include <sstream>   // For formatting error messages

// Constructor
CPU::CPU(Memory &mem,
         const std::vector<Instruction> &instructions,
         std::function<void(long)> prn_callback)
    : memory_(mem),
      program_instructions_(instructions),
      prn_system_call_handler_(prn_callback),
      halted_flag_(false),
      user_mode_flag_(false) // CPU starts in KERNEL mode
{
    // Ensure memory has minimal size for registers
    if (memory_.getSize() < 21)
    { // At least 0-20 for registers
        throw std::runtime_error("Memory size too small for CPU registers.");
    }
    reset(); // Initialize registers from memory (or to defaults if memory is zeroed)
}

// Resets CPU state
void CPU::reset()
{
    halted_flag_ = false;
    user_mode_flag_ = false; // Start in kernel mode
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
    long current_count = memory_.read(INSTR_COUNT_ADDR);
    memory_.write(INSTR_COUNT_ADDR, current_count + 1);
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
        std::cerr << "FATAL: CPU privileged memory read out of bounds at address "
                  << address << ". " << e.what() << std::endl;
        halted_flag_ = true;
        throw; // Re-throw to stop simulation
    }
}

// Checked read: enforces user mode restrictions
long CPU::checkedRead(long address)
{
    if (user_mode_flag_)
    {
        // Registers 0-20 are always accessible
        // Memory >= 1000 is accessible
        // Memory 21-999 is OS area, forbidden in user mode
        if (address >= USER_MEMORY_START_ADDR && address <= OS_DATA_END_ADDR)
        {
            throw UserMemoryFaultException("User mode read access violation", address);
        }
    }
    // If kernel mode, or user mode accessing allowed regions (0-20 or >=1000)
    try
    {
        return memory_.read(address);
    }
    catch (const std::out_of_range &e)
    {
        std::cerr << "FATAL: CPU memory read out of bounds at address "
                  << address << ". " << e.what() << std::endl;
        halted_flag_ = true;
        throw; // Re-throw to stop simulation
    }
}

// Checked write: enforces user mode restrictions
void CPU::checkedWrite(long address, long value)
{
    if (user_mode_flag_)
    {
        if (address >= USER_MEMORY_START_ADDR && address <= OS_DATA_END_ADDR)
        {
            throw UserMemoryFaultException("User mode write access violation", address);
        }
    }
    try
    {
        memory_.write(address, value);
    }
    catch (const std::out_of_range &e)
    {
        std::cerr << "FATAL: CPU memory write out of bounds at address "
                  << address << ". " << e.what() << std::endl;
        halted_flag_ = true;
        throw; // Re-throw to stop simulation
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

    if (current_pc < 0 || static_cast<size_t>(current_pc) >= program_instructions_.size())
    {
        std::cerr << "CPU FAULT: Program Counter (" << current_pc
                  << ") is out of instruction bounds (0-"
                  << (program_instructions_.empty() ? 0 : program_instructions_.size() - 1) << ")." << std::endl;
        if (user_mode_flag_)
        {
            user_mode_flag_ = false;                                                                 // Switch to Kernel
            memory_.write(SAVED_TRAP_PC_ADDR, current_pc);                                           // Save faulting PC
            memory_.write(CPU_OS_COMM_ADDR, static_cast<long>(CpuEvent::UNKNOWN_INSTRUCTION_FAULT)); // Or a specific PC_OUT_OF_BOUNDS event
            setPC(OS_MEMORY_FAULT_HANDLER_PC);                                                       // Trap to OS
        }
        else
        {
            halted_flag_ = true; // Kernel mode PC fault is fatal for the CPU
        }
        return; // End step here, do not increment instruction counter for this fault
    }

    const Instruction &instr = program_instructions_[static_cast<size_t>(current_pc)];
    long next_pc = current_pc + 1;
    bool pc_modified_by_instruction = false;
    bool increment_instr_counter_after_execution = true;

    // --- Execute Instruction ---
    // This will be a large switch statement
    // For now, let's just increment PC and instruction count
    // std::cout << "Executing: " << instr.original_line << " (PC=" << current_pc << ")" << std::endl; // Debug

    try
    {
        switch (instr.opcode)
        {
        case OpCode::SET: // SET B A (arg1=B, arg2=A)
            // Direct Set: Set the Ath memory location with number B.
            if (instr.num_operands != 2)
                throw std::runtime_error("SET: Invalid number of operands.");
            checkedWrite(instr.arg2, instr.arg1);
            break;

        case OpCode::CPY: // CPY A1 A2 (arg1=A1, arg2=A2)
            // Direct Copy: Copy the content of memory location A1 to memory A2.
            if (instr.num_operands != 2)
                throw std::runtime_error("CPY: Invalid number of operands.");
            {
                long val_a1 = checkedRead(instr.arg1);
                checkedWrite(instr.arg2, val_a1);
            }
            break;

        case OpCode::CPYI: // CPYI A1 A2 (arg1=A1, arg2=A2)
            // Indirect Copy: Copy the memory address indexed by A1 to memory address A2.
            // Example: CPYI 100, 102: if memory address 100 contains 200,
            // then this instruction copies the contents of memory address 200 to memory location 120. (Spec says 120, should be A2/102)
            if (instr.num_operands != 2)
                throw std::runtime_error("CPYI: Invalid number of operands.");
            {
                long addr_from_a1 = checkedRead(instr.arg1);           // Get the address (e.g., 200) stored at A1 (e.g., 100)
                long val_at_indirect_addr = checkedRead(addr_from_a1); // Get content of address 200
                checkedWrite(instr.arg2, val_at_indirect_addr);        // Write to A2 (e.g., 102 or 120 as per example confusion)
                                                                       // Assuming A2 from instruction: instr.arg2
            }
            break;

        case OpCode::CPYI2:
            if (instr.num_operands != 2)
                throw std::runtime_error("CPYI2: Invalid number of operands.");
            // CPYI2 A1 A2: if mem[A1] contains X, and mem[A2] contains Y,
            // copy contents of mem[X] to mem[Y].
            {
                long address_X_container = instr.arg1;
                long address_Y_container = instr.arg2;

                long address_X = checkedRead(address_X_container);
                long address_Y = checkedRead(address_Y_container);

                long value_from_X = checkedRead(address_X);
                checkedWrite(address_Y, value_from_X);
            }
            break;

        case OpCode::ADD: // ADD A B (arg1=A, arg2=B)
            // Add number B to memory location A
            if (instr.num_operands != 2)
                throw std::runtime_error("ADD: Invalid number of operands.");
            {
                long val_a = checkedRead(instr.arg1);
                checkedWrite(instr.arg1, val_a + instr.arg2);
            }
            break;

        case OpCode::ADDI: // ADDI A1 A2 (arg1=A1, arg2=A2)
            // Indirect Add: Add the contents of memory address A2 to address A1.
            if (instr.num_operands != 2)
                throw std::runtime_error("ADDI: Invalid number of operands.");
            {
                long val_a1 = checkedRead(instr.arg1);
                long val_a2 = checkedRead(instr.arg2);
                checkedWrite(instr.arg1, val_a1 + val_a2);
            }
            break;

        case OpCode::SUBI: // SUBI A1 A2 (arg1=A1, arg2=A2)
            // Indirect Subtraction: Subtract the contents of memory address A2 from address A1, put the result in A2
            if (instr.num_operands != 2)
                throw std::runtime_error("SUBI: Invalid number of operands.");
            {
                long val_a1 = checkedRead(instr.arg1);
                long val_a2 = checkedRead(instr.arg2);
                checkedWrite(instr.arg2, val_a1 - val_a2); // Result in A2
            }
            break;

        case OpCode::JIF: // JIF A C (arg1=A, arg2=C)
            // Set the CPU program counter with C if memory location A content is less than or equal to 0
            if (instr.num_operands != 2)
                throw std::runtime_error("JIF: Invalid number of operands.");
            {
                long val_a = checkedRead(instr.arg1);
                if (val_a <= 0)
                {
                    next_pc = instr.arg2; // Jump to instruction C
                    pc_modified_by_instruction = true;
                }
            }
            break;

        case OpCode::PUSH: // PUSH A (arg1=A)
            // Push memory A onto the stack. Stack grows downwards.
            if (instr.num_operands != 1)
                throw std::runtime_error("PUSH: Invalid number of operands.");
            {
                long sp = getSP();
                sp--; // Stack grows downwards, so decrement SP first
                if (sp < 0)
                    throw std::runtime_error("Stack overflow during PUSH.");
                setSP(sp);
                long val_a = checkedRead(instr.arg1);
                // Stack write should ideally bypass user mode checks if SP is in OS area
                // but for threads, SP will point to thread's stack.
                // Using checkedWrite for now. If stack is in OS area and user_mode is on, this might be an issue.
                // For simplicity: If stack is for a thread, it should be in thread memory >= 1000.
                // OS stack could be < 1000 but OS runs in kernel mode.
                checkedWrite(sp, val_a);
            }
            break;

        case OpCode::POP: // POP A (arg1=A)
            // Pop value from stack into memory A.
            if (instr.num_operands != 1)
                throw std::runtime_error("POP: Invalid number of operands.");
            {
                long sp = getSP();
                if (static_cast<size_t>(sp) >= memory_.getSize())
                    throw std::runtime_error("Stack underflow during POP (SP out of bounds).");
                // Similar to PUSH, stack read might need careful consideration of user mode
                long val_from_stack = checkedRead(sp);
                setSP(sp + 1); // Increment SP after pop
                checkedWrite(instr.arg1, val_from_stack);
            }
            break;

        case OpCode::CALL: // CALL C (arg1=C) (arg2 not used, but struct has it)
            // Call subroutine at instruction C, push return address.
            if (instr.num_operands != 1)
                throw std::runtime_error("CALL: Invalid number of operands.");
            {
                long sp = getSP();
                sp--; // Decrement SP for return address
                if (sp < 0)
                    throw std::runtime_error("Stack overflow during CALL.");
                setSP(sp);
                // Push current_pc + 1 (the address of the instruction AFTER the CALL)
                checkedWrite(sp, current_pc + 1);
                next_pc = instr.arg1; // Jump to instruction C
                pc_modified_by_instruction = true;
            }
            break;

        case OpCode::RET: // RET
            // Return from subroutine by popping return address.
            if (instr.num_operands != 0)
                throw std::runtime_error("RET: Invalid number of operands.");
            {
                long sp = getSP();
                if (static_cast<size_t>(sp) >= memory_.getSize())
                    throw std::runtime_error("Stack underflow during RET (SP out of bounds).");
                long return_addr = checkedRead(sp);
                setSP(sp + 1);
                next_pc = return_addr;
                pc_modified_by_instruction = true;
            }
            break;

        case OpCode::HLT: // CPU Halt
            if (instr.num_operands != 0)
                throw std::runtime_error("HLT: Invalid number of operands.");
            halted_flag_ = true;
            pc_modified_by_instruction = true; // PC effectively stops advancing

            break;

        case OpCode::USER: // USER A (arg1 = A, the memory address containing the target PC)
            if (instr.num_operands != 1)
                throw std::runtime_error("USER: Invalid number of operands.");
            if (user_mode_flag_)
            {
                // Already in user mode. This could be an error or a NOP.
                // For now, let it proceed, OS might use it for re-entry if PC changes.
                // Or throw std::runtime_error("USER: Already in user mode.");
            }
            else
            {
                // "Switch to user mode and jump to address contained at location Ard" (Ard is A -> instr.arg1)
                long target_pc_value = checkedRead(instr.arg1); // Read the value from address A
                setPC(target_pc_value);                         // Set PC to this new value
                user_mode_flag_ = true;
                pc_modified_by_instruction = true;
                // std::cout << "CPU: Switching to USER mode, PC=" << target_pc_value << std::endl; // Debug
            }
            break;

        case OpCode::SYSCALL_PRN: // SYSCALL PRN A (arg1=A)
            if (instr.num_operands != 1)
                throw std::runtime_error("SYSCALL PRN: Invalid number of operands.");
            {
                user_mode_flag_ = false; // Enter Kernel mode for syscall

                long val_to_print = checkedRead(instr.arg1); // Read happens in kernel mode context
                if (prn_system_call_handler_)
                {
                    prn_system_call_handler_(val_to_print);
                }
                else
                {
                    std::cout << val_to_print << std::endl;
                }

                memory_.write(SAVED_TRAP_PC_ADDR, next_pc);        // Save PC of *next* instruction for OS
                setCpuEvent(CpuEvent::SYSCALL_PRN);                // Notify OS of syscall
                memory_.write(SYSCALL_ARG1_PASS_ADDR, instr.arg1); // Pass original address A to OS
                setPC(OS_SYSCALL_DISPATCHER_PC);                   // OS will handle blocking & scheduling
                pc_modified_by_instruction = true;
            }
            break;

        case OpCode::SYSCALL_HLT_THREAD: // SYSCALL HLT (Thread Halt)
            if (instr.num_operands != 0)
                throw std::runtime_error("SYSCALL HLT_THREAD: Invalid number of operands.");
            {
                user_mode_flag_ = false; // Enter Kernel mode
                memory_.write(SAVED_TRAP_PC_ADDR, next_pc);
                setCpuEvent(CpuEvent::SYSCALL_HLT_THREAD); // Notify OS of syscall
                setPC(OS_SYSCALL_DISPATCHER_PC);
                pc_modified_by_instruction = true;
            }
            break;

        case OpCode::SYSCALL_YIELD: // SYSCALL YIELD
            if (instr.num_operands != 0)
                throw std::runtime_error("SYSCALL YIELD: Invalid number of operands.");
            {
                user_mode_flag_ = false; // Enter Kernel mode
                memory_.write(SAVED_TRAP_PC_ADDR, next_pc);
                setCpuEvent(CpuEvent::SYSCALL_YIELD); // Notify OS of syscall
                setPC(OS_SYSCALL_DISPATCHER_PC);
                pc_modified_by_instruction = true;
            }
            break;

        case OpCode::UNKNOWN:
        default:
            // Potentially trap to OS if in user mode, or halt if in kernel
            std::cerr << "FATAL: Unknown or unimplemented opcode encountered at PC " << current_pc
                      << ". Instruction: " << instr.original_line << std::endl;
            if (user_mode_flag_)
            {
                user_mode_flag_ = false;                          // Switch to Kernel
                memory_.write(SAVED_TRAP_PC_ADDR, current_pc);    // Save faulting PC
                setCpuEvent(CpuEvent::UNKNOWN_INSTRUCTION_FAULT); // Notify OS of unknown instruction
                setPC(OS_MEMORY_FAULT_HANDLER_PC);                // Or a generic fault handler
                pc_modified_by_instruction = true;
                increment_instr_counter_after_execution = false; // Don't count this fault as a full execution by user
            }
            else
            {
                halted_flag_ = true; // Kernel mode unknown instruction is fatal
                pc_modified_by_instruction = true;
            }
            break;
        }
    }
    catch (const UserMemoryFaultException &umf)
    {
        // Handle User Mode Memory Fault
        std::cerr << "FATAL: User mode memory fault during execution of instruction at PC "
                  << current_pc << " (" << instr.original_line << "): "
                  << umf.what() << " at address " << umf.faulting_address << std::endl;

        user_mode_flag_ = false; // Switch to Kernel mode

        memory_.write(SAVED_TRAP_PC_ADDR, current_pc); // Save faulting PC
        setCpuEvent(CpuEvent::MEMORY_FAULT_USER);      // Notify OS of user mode memory fault

        // pass the faulting address to OS for handling
        memory_.write(SYSCALL_ARG1_PASS_ADDR, umf.faulting_address);
        setPC(OS_MEMORY_FAULT_HANDLER_PC);
        pc_modified_by_instruction = true;
        increment_instr_counter_after_execution = false; // Fault is not a complete user instruction
    }
    catch (const std::runtime_error &e)
    { // General runtime errors, e.g., stack overflow, underflow, invalid operands
        std::cerr << "FATAL: Runtime error during execution of instruction at PC "
                  << current_pc << " (" << instr.original_line << "): " << e.what() << std::endl;
        halted_flag_ = true;
        pc_modified_by_instruction = true; // PC should not advance after a fatal error
    }
    catch (const std::out_of_range &e)
    {
        // This should ideally be caught by checkedRead/Write making it fatal there,
        // or if not, it means memory_ is being accessed directly bypassing checks.
        std::cerr << "Memory access out of range (caught in step) at PC " << current_pc
                  << " (" << instr.original_line << "): " << e.what() << std::endl;
        halted_flag_ = true;
        pc_modified_by_instruction = true;
    }

    if (increment_instr_counter_after_execution && !halted_flag_)
    {
        incrementInstructionCounter();
    }

    if (!halted_flag_)
    {
        if (!pc_modified_by_instruction)
        {
            setPC(next_pc);
        }
    }
}

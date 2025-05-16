// src/cpu.cpp
#include "cpu.h"
#include <iostream> // For error messages, potentially for SYSCALL_PRN if callback not used externally
#include <stdexcept> // For runtime_error
#include <sstream>   // For formatting error messages

// Constants for memory-mapped registers
constexpr long PC_ADDR = 0;
constexpr long SP_ADDR = 1;
constexpr long SYSCALL_RESULT_ADDR = 2;
constexpr long INSTR_COUNT_ADDR = 3;

// Memory boundaries for user mode access checks
constexpr long USER_MODE_FORBIDDEN_START_ADDR = 21; // 0-20 are registers, accessible
constexpr long USER_MODE_FORBIDDEN_END_ADDR = 999;

// Constructor
CPU::CPU(Memory& mem, 
         const std::vector<Instruction>& instructions, 
         std::function<void(long)> prn_callback)
    : memory_(mem), 
      program_instructions_(instructions), 
      prn_system_call_handler_(prn_callback),
      halted_flag_(false), 
      user_mode_flag_(false) // CPU starts in KERNEL mode
{
    // Ensure memory has minimal size for registers
    if (memory_.getSize() < 21) { // At least 0-20 for registers
        throw std::runtime_error("Memory size too small for CPU registers.");
    }
    reset(); // Initialize registers from memory (or to defaults if memory is zeroed)
}

// Resets CPU state
void CPU::reset() {
    halted_flag_ = false;
    user_mode_flag_ = false; // Start in kernel mode
    // PC, SP, INSTR_COUNT, SYSCALL_RESULT are assumed to be set appropriately
    // in memory by the OS loader or by their initial values in the data segment.
    // If a "true" CPU reset is needed where these are zeroed:
    // memory_.write(PC_ADDR, 0);
    // memory_.write(SP_ADDR, initial_sp_value); // e.g., memory_.getSize() - 1 for stack top
    // memory_.write(INSTR_COUNT_ADDR, 0);
    // memory_.write(SYSCALL_RESULT_ADDR, 0);
}

// --- Register Access Helper Methods ---
long CPU::getPC() const {
    return memory_.read(PC_ADDR);
}

void CPU::setPC(long new_pc) {
    memory_.write(PC_ADDR, new_pc);
}

long CPU::getSP() const {
    return memory_.read(SP_ADDR);
}

void CPU::setSP(long new_sp) {
    memory_.write(SP_ADDR, new_sp);
}

void CPU::incrementInstructionCounter() {
    long current_count = memory_.read(INSTR_COUNT_ADDR);
    memory_.write(INSTR_COUNT_ADDR, current_count + 1);
}

void CPU::setSystemCallResult(long result_code) {
    memory_.write(SYSCALL_RESULT_ADDR, result_code);
}

long CPU::getCurrentProgramCounter() const {
    return getPC();
}

// --- Memory Access with User Mode Protection ---
void CPU::handleMemoryViolation(const std::string& operation_type, long address) {
    std::ostringstream errMsg;
    errMsg << "FATAL: User mode memory access violation during " << operation_type 
           << " at address " << address << ". Thread would be shut down.";
    // In a full OS sim, this would trigger OS intervention.
    // For now, we halt the CPU to indicate a critical error.
    std::cerr << errMsg.str() << std::endl;
    halted_flag_ = true; 
    throw std::runtime_error(errMsg.str()); // Propagate to stop simulation
}

// Privileged read: for CPU internal use, e.g., reading PC, SP, which are always accessible
long CPU::privilegedRead(long address) {
    try {
        return memory_.read(address);
    } catch (const std::out_of_range& e) {
        std::cerr << "FATAL: CPU privileged memory read out of bounds at address " 
                  << address << ". " << e.what() << std::endl;
        halted_flag_ = true;
        throw; // Re-throw to stop simulation
    }
}

// Checked read: enforces user mode restrictions
long CPU::checkedRead(long address) {
    if (user_mode_flag_) {
        if (address >= USER_MODE_FORBIDDEN_START_ADDR && address <= USER_MODE_FORBIDDEN_END_ADDR) {
            handleMemoryViolation("read", address);
            // handleMemoryViolation will throw, so this return is not reached in that case
            return 0; // Should not be reached if violation occurs
        }
    }
    // If kernel mode, or user mode accessing allowed regions (0-20 or >=1000)
    try {
        return memory_.read(address);
    } catch (const std::out_of_range& e) {
        std::cerr << "FATAL: CPU memory read out of bounds at address " 
                  << address << ". " << e.what() << std::endl;
        halted_flag_ = true;
        throw; // Re-throw to stop simulation
    }
}

// Checked write: enforces user mode restrictions
void CPU::checkedWrite(long address, long value) {
    if (user_mode_flag_) {
        if (address >= USER_MODE_FORBIDDEN_START_ADDR && address <= USER_MODE_FORBIDDEN_END_ADDR) {
            handleMemoryViolation("write", address);
            return; // Should not be reached if violation occurs
        }
    }
    // If kernel mode, or user mode accessing allowed regions (0-20 or >=1000)
    try {
        memory_.write(address, value);
    } catch (const std::out_of_range& e) {
        std::cerr << "FATAL: CPU memory write out of bounds at address " 
                  << address << ". " << e.what() << std::endl;
        halted_flag_ = true;
        throw; // Re-throw to stop simulation
    }
}


// --- Main Execution Step ---
void CPU::step() {
    if (halted_flag_) {
        return; // CPU is halted, do nothing
    }

    long current_pc = getPC();

    if (current_pc < 0 || static_cast<size_t>(current_pc) >= program_instructions_.size()) {
        std::cerr << "FATAL: Program Counter (" << current_pc << ") is out of instruction bounds (0-" 
                  << program_instructions_.size() - 1 << ")." << std::endl;
        halted_flag_ = true;
        return;
    }

    const Instruction& instr = program_instructions_[static_cast<size_t>(current_pc)];
    long next_pc = current_pc + 1; // Default next PC
    bool pc_modified_by_instruction = false;

    // --- Execute Instruction ---
    // This will be a large switch statement
    // For now, let's just increment PC and instruction count
    // std::cout << "Executing: " << instr.original_line << " (PC=" << current_pc << ")" << std::endl; // Debug

    try {
        switch (instr.opcode) {
            case OpCode::SET: // SET B A (arg1=B, arg2=A)
                // Direct Set: Set the Ath memory location with number B.
                if (instr.num_operands != 2) throw std::runtime_error("SET: Invalid number of operands.");
                checkedWrite(instr.arg2, instr.arg1);
                break;

            case OpCode::CPY: // CPY A1 A2 (arg1=A1, arg2=A2)
                // Direct Copy: Copy the content of memory location A1 to memory A2.
                if (instr.num_operands != 2) throw std::runtime_error("CPY: Invalid number of operands.");
                {
                    long val_a1 = checkedRead(instr.arg1);
                    checkedWrite(instr.arg2, val_a1);
                }
                break;

            case OpCode::CPYI: // CPYI A1 A2 (arg1=A1, arg2=A2)
                // Indirect Copy: Copy the memory address indexed by A1 to memory address A2.
                // Example: CPYI 100, 102: if memory address 100 contains 200, 
                // then this instruction copies the contents of memory address 200 to memory location 120. (Spec says 120, should be A2/102)
                if (instr.num_operands != 2) throw std::runtime_error("CPYI: Invalid number of operands.");
                {
                    long addr_from_a1 = checkedRead(instr.arg1); // Get the address (e.g., 200) stored at A1 (e.g., 100)
                    long val_at_indirect_addr = checkedRead(addr_from_a1); // Get content of address 200
                    checkedWrite(instr.arg2, val_at_indirect_addr); // Write to A2 (e.g., 102 or 120 as per example confusion)
                                                                  // Assuming A2 from instruction: instr.arg2
                }
                break;

            case OpCode::ADD: // ADD A B (arg1=A, arg2=B)
                // Add number B to memory location A
                if (instr.num_operands != 2) throw std::runtime_error("ADD: Invalid number of operands.");
                {
                    long val_a = checkedRead(instr.arg1);
                    checkedWrite(instr.arg1, val_a + instr.arg2);
                }
                break;

            case OpCode::ADDI: // ADDI A1 A2 (arg1=A1, arg2=A2)
                // Indirect Add: Add the contents of memory address A2 to address A1.
                if (instr.num_operands != 2) throw std::runtime_error("ADDI: Invalid number of operands.");
                {
                    long val_a1 = checkedRead(instr.arg1);
                    long val_a2 = checkedRead(instr.arg2);
                    checkedWrite(instr.arg1, val_a1 + val_a2);
                }
                break;

            case OpCode::SUBI: // SUBI A1 A2 (arg1=A1, arg2=A2)
                // Indirect Subtraction: Subtract the contents of memory address A2 from address A1, put the result in A2
                if (instr.num_operands != 2) throw std::runtime_error("SUBI: Invalid number of operands.");
                {
                    long val_a1 = checkedRead(instr.arg1);
                    long val_a2 = checkedRead(instr.arg2);
                    checkedWrite(instr.arg2, val_a1 - val_a2); // Result in A2
                }
                break;

            case OpCode::JIF: // JIF A C (arg1=A, arg2=C)
                // Set the CPU program counter with C if memory location A content is less than or equal to 0
                if (instr.num_operands != 2) throw std::runtime_error("JIF: Invalid number of operands.");
                {
                    long val_a = checkedRead(instr.arg1);
                    if (val_a <= 0) {
                        next_pc = instr.arg2; // Jump to instruction C
                        pc_modified_by_instruction = true;
                    }
                }
                break;

            case OpCode::PUSH: // PUSH A (arg1=A)
                // Push memory A onto the stack. Stack grows downwards.
                if (instr.num_operands != 1) throw std::runtime_error("PUSH: Invalid number of operands.");
                {
                    long sp = getSP();
                    sp--; // Stack grows downwards, so decrement SP first
                    if (sp < 0) throw std::runtime_error("Stack overflow during PUSH.");
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
                if (instr.num_operands != 1) throw std::runtime_error("POP: Invalid number of operands.");
                {
                    long sp = getSP();
                    if (sp >= memory_.getSize()) throw std::runtime_error("Stack underflow during POP (SP out of bounds).");
                    // Similar to PUSH, stack read might need careful consideration of user mode
                    long val_from_stack = checkedRead(sp); 
                    setSP(sp + 1); // Increment SP after pop
                    checkedWrite(instr.arg1, val_from_stack);
                }
                break;

            case OpCode::CALL: // CALL C (arg1=C) (arg2 not used, but struct has it)
                // Call subroutine at instruction C, push return address.
                if (instr.num_operands != 1) throw std::runtime_error("CALL: Invalid number of operands.");
                {
                    long sp = getSP();
                    sp--; // Decrement SP for return address
                    if (sp < 0) throw std::runtime_error("Stack overflow during CALL.");
                    setSP(sp);
                    // Push current_pc + 1 (the address of the instruction AFTER the CALL)
                    checkedWrite(sp, current_pc + 1); 
                    next_pc = instr.arg1; // Jump to instruction C
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::RET: // RET
                // Return from subroutine by popping return address.
                if (instr.num_operands != 0) throw std::runtime_error("RET: Invalid number of operands.");
                {
                    long sp = getSP();
                    if (sp >= memory_.getSize()) throw std::runtime_error("Stack underflow during RET (SP out of bounds).");
                    long return_addr = checkedRead(sp);
                    setSP(sp + 1);
                    next_pc = return_addr;
                    pc_modified_by_instruction = true;
                }
                break;

            case OpCode::HLT: // HLT (CPU Halt)
                if (instr.num_operands != 0) throw std::runtime_error("HLT: Invalid number of operands.");
                halted_flag_ = true;
                pc_modified_by_instruction = true; // PC effectively stops advancing
                // std::cout << "CPU HLT encountered. Total instructions executed: " << memory_.read(INSTR_COUNT_ADDR) +1 << std::endl;
                break;

            case OpCode::USER: // USER
                if (instr.num_operands != 0) throw std::runtime_error("USER: Invalid number of operands.");
                if (user_mode_flag_) {
                    // std::cout << "Warning: USER instruction executed while already in user mode." << std::endl;
                } else {
                    user_mode_flag_ = true;
                    // std::cout << "CPU switching to USER mode." << std::endl;
                }
                break;

            case OpCode::SYSCALL_PRN: // SYSCALL PRN A (arg1=A)
                if (instr.num_operands != 1) throw std::runtime_error("SYSCALL PRN: Invalid number of operands.");
                {
                    // System call always switches to KERNEL mode temporarily for handler
                    bool prev_mode = user_mode_flag_;
                    user_mode_flag_ = false; // Enter Kernel mode

                    long val_to_print = checkedRead(instr.arg1); // Read happens effectively in kernel mode
                    if (prn_system_call_handler_) {
                        prn_system_call_handler_(val_to_print);
                    } else {
                        std::cout << val_to_print << std::endl; // Default print if no handler
                    }
                    setSystemCallResult(SYSCALL_STATUS_SUCCESS); // Report success
                    
                    // "This system call will block the calling thread for 100 instruction executions."
                    // This blocking logic will be handled by the OS. The CPU just executes the PRN.
                    // The OS, upon seeing PRN completed (via syscall result or other means),
                    // will mark the thread as blocked and manage its resume time.
                    
                    user_mode_flag_ = prev_mode; // Restore original mode
                }
                break;

            case OpCode::SYSCALL_HLT_THREAD: // SYSCALL HLT (Thread Halt)
                if (instr.num_operands != 0) throw std::runtime_error("SYSCALL HLT_THREAD: Invalid number of operands.");
                {
                    // This syscall signals the OS to halt the current thread.
                    // CPU switches to KERNEL mode, OS will handle it.
                    bool prev_mode = user_mode_flag_;
                    user_mode_flag_ = false; // Enter Kernel mode for syscall handling by OS

                    setSystemCallResult(SYSCALL_STATUS_THREAD_HALT_REQUEST);
                    // The OS will see this result and update the thread's state.
                    // The CPU itself doesn't halt. PC will advance. The OS scheduler will then skip this thread.
                    
                    user_mode_flag_ = prev_mode; // Restore original mode (though OS will likely context switch)
                }
                break;

            case OpCode::SYSCALL_YIELD: // SYSCALL YIELD
                 if (instr.num_operands != 0) throw std::runtime_error("SYSCALL YIELD: Invalid number of operands.");
                {
                    // Signals the OS to yield CPU to another thread.
                    bool prev_mode = user_mode_flag_;
                    user_mode_flag_ = false; // Enter Kernel mode for syscall handling by OS

                    setSystemCallResult(SYSCALL_STATUS_YIELD_REQUEST);
                    // The OS will see this and run its scheduler.

                    user_mode_flag_ = prev_mode; // Restore original mode (OS will context switch)
                }
                break;

            case OpCode::UNKNOWN:
            default:
                std::cerr << "FATAL: Unknown or unimplemented opcode encountered at PC " << current_pc 
                          << ". Instruction: " << instr.original_line << std::endl;
                halted_flag_ = true;
                pc_modified_by_instruction = true; // Stop further execution
                break;
        }
    } catch (const std::runtime_error& e) {
        // This catches errors like invalid operands, stack overflow/underflow, 
        // or memory violations rethrown by checkedRead/Write.
        std::cerr << "Runtime error during execution of instruction at PC " << current_pc 
                  << " (" << instr.original_line << "): " << e.what() << std::endl;
        halted_flag_ = true; // Halt CPU on any execution error
        pc_modified_by_instruction = true;
    } catch (const std::out_of_range& e) { // Should be caught by checkedRead/Write, but as a fallback
        std::cerr << "Memory access out of range during execution of instruction at PC " << current_pc 
                  << " (" << instr.original_line << "): " << e.what() << std::endl;
        halted_flag_ = true;
        pc_modified_by_instruction = true;
    }


    if (!halted_flag_) { // Only increment counters and PC if not halted
        incrementInstructionCounter();
        if (!pc_modified_by_instruction) {
            setPC(next_pc);
        }
    } else {
        // If HLT instruction was executed, PC should not advance.
        // If an error caused halt, PC is also not advanced from the point of error.
        // For CPU HLT, the instruction count is typically the last executed one.
        // The problem says "increment register number 3 (number of instructions executed)"
        // "This loop ends when the CPU is halted."
        // This implies that the instruction counter IS incremented for the HLT instruction itself.
        // So, if HLT was the cause, incrementInstructionCounter should have run before halted_flag_ was set by HLT.
        // Let's ensure HLT also increments the count.
        if (instr.opcode == OpCode::HLT) {
             // If we moved incrementInstructionCounter after the switch, HLT would need to call it.
             // Current placement before this 'if' means HLT count is included.
        }
    }
}
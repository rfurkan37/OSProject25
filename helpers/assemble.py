# assemble_pdf_format.py
import argparse
import re

# Define how many operands each *base* instruction takes.
# SYSCALL is handled specially as it has a sub-type.
OPCODE_ARGS_INFO = {
    "SET": 2, "CPY": 2, "CPYI": 2, "ADD": 2, "ADDI": 2, "SUBI": 2, "JIF": 2,
    "PUSH": 1, "POP": 1, "CALL": 1,
    "RET": 0, "HLT": 0, "USER": 0,
    # For SYSCALL, the type determines args:
    "SYSCALL_PRN": 1,
    "SYSCALL_HLT": 0, # Thread HLT
    "SYSCALL_YIELD": 0
}

def strip_comment_and_whitespace(line):
    return line.split('#', 1)[0].strip()

def assemble_pdf_style(input_filepath, output_filepath):
    processed_data_lines = []
    processed_instruction_lines = []
    current_section = None  # 'data', 'text', or None

    with open(input_filepath, 'r') as f_in:
        for line_num, raw_line in enumerate(f_in, 1):
            line_content = strip_comment_and_whitespace(raw_line)
            
            if not line_content: # Skip empty lines or lines with only comments
                continue

            # Section directives
            if line_content.lower() == "begin data section":
                if current_section == 'data':
                    raise ValueError(f"L{line_num}: Duplicate 'Begin Data Section'.")
                if current_section == 'text' and not processed_instruction_lines:
                    # Allow if previous text section was empty, otherwise could be an error
                    pass # Or raise error if instructions were expected before switching
                current_section = 'data'
                processed_data_lines.append(line_content) # Keep the directive
                continue
            elif line_content.lower() == "end data section":
                if current_section != 'data':
                    raise ValueError(f"L{line_num}: 'End Data Section' found without 'Begin Data Section' or in wrong section.")
                current_section = None # Reset section
                processed_data_lines.append(line_content) # Keep the directive
                continue
            elif line_content.lower() == "begin instruction section":
                if current_section == 'text':
                    raise ValueError(f"L{line_num}: Duplicate 'Begin Instruction Section'.")
                current_section = 'text'
                processed_instruction_lines.append(line_content) # Keep the directive
                continue
            elif line_content.lower() == "end instruction section":
                if current_section != 'text':
                    raise ValueError(f"L{line_num}: 'End Instruction Section' found without 'Begin Instruction Section' or in wrong section.")
                current_section = None # Reset section
                processed_instruction_lines.append(line_content) # Keep the directive
                continue

            if current_section is None:
                # Allow comments or empty lines outside sections, but not actual definitions/instructions
                if line_content: # if it's not just an empty line from stripping comment
                    raise ValueError(f"L{line_num}: Content '{line_content}' found outside of a Begin/End section.")
                continue


            if current_section == 'data':
                parts = line_content.split(maxsplit=1)
                if len(parts) != 2:
                    raise ValueError(f"L{line_num}: Data line '{line_content}' must be in 'address value' format.")
                try:
                    address = int(parts[0])
                    value = int(parts[1])
                except ValueError:
                    raise ValueError(f"L{line_num}: Data line '{line_content}' - address and value must be integers.")
                # Optionally, you could store and check for duplicate addresses or ordering if desired.
                # For now, just pass through valid lines.
                processed_data_lines.append(f"{address} {value}")

            elif current_section == 'text':
                parts = line_content.split(maxsplit=1) # Split off the line number
                if len(parts) < 2 : # Must have line number and at least a mnemonic
                    raise ValueError(f"L{line_num}: Instruction line '{line_content}' too short. Expected 'line_num MNEMONIC [args...]'.")
                
                try:
                    instr_line_idx_str = parts[0]
                    _ = int(instr_line_idx_str) # Validate it's a number
                except ValueError:
                    raise ValueError(f"L{line_num}: Instruction line number '{instr_line_idx_str}' is not a valid integer.")

                instruction_and_args = parts[1].split()
                mnemonic_token = instruction_and_args[0].upper()
                arg_tokens = instruction_and_args[1:]

                current_mnemonic_key = mnemonic_token
                if mnemonic_token == "SYSCALL":
                    if not arg_tokens:
                        raise ValueError(f"L{line_num}: SYSCALL instruction needs a type (e.g., PRN, HLT, YIELD).")
                    syscall_type = arg_tokens.pop(0).upper() # Consume the syscall type
                    current_mnemonic_key = f"SYSCALL_{syscall_type}"
                
                if current_mnemonic_key not in OPCODE_ARGS_INFO:
                    raise ValueError(f"L{line_num}: Unknown mnemonic or SYSCALL type '{parts[1].split()[0]}'.")

                expected_arg_count = OPCODE_ARGS_INFO[current_mnemonic_key]
                if len(arg_tokens) != expected_arg_count:
                    # Reconstruct the full mnemonic for error message
                    full_mnemonic_for_error = mnemonic_token
                    if current_mnemonic_key.startswith("SYSCALL_"):
                        full_mnemonic_for_error = f"SYSCALL {current_mnemonic_key.split('_',1)[1]}"

                    raise ValueError(f"L{line_num}: Mnemonic '{full_mnemonic_for_error}' expects {expected_arg_count} arguments, got {len(arg_tokens)} in '{' '.join(instruction_and_args)}'.")

                # Validate arguments are numbers (as per PDF format, no symbolic labels here)
                for i, arg_val_str in enumerate(arg_tokens):
                    try:
                        _ = int(arg_val_str)
                    except ValueError:
                        raise ValueError(f"L{line_num}: Argument '{arg_val_str}' for {current_mnemonic_key} must be an integer.")
                
                # Reconstruct the validated line to preserve original formatting as much as possible
                # but ensure it's clean.
                output_instr_line = f"{instr_line_idx_str} {mnemonic_token}"
                if current_mnemonic_key.startswith("SYSCALL_"): # Add back syscall type
                    output_instr_line += f" {current_mnemonic_key.split('_',1)[1]}"
                if arg_tokens: # Add instruction arguments
                     output_instr_line += f" {' '.join(arg_tokens)}"
                processed_instruction_lines.append(output_instr_line)

    # --- Write to output file ---
    with open(output_filepath, 'w') as f_out:
        if processed_data_lines:
            for line in processed_data_lines:
                f_out.write(line + "\n")
        
        if processed_instruction_lines:
            for line in processed_instruction_lines:
                f_out.write(line + "\n")
        
    print(f"Assembly (PDF-style validation & formatting) successful: '{input_filepath}' -> '{output_filepath}'")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="GTU-C312 Assembler for PDF-like format")
    parser.add_argument("input_file", help="Input assembly file (PDF format)")
    parser.add_argument("-o", "--output_file", default=None, help="Output image file")

    args = parser.parse_args()

    out_file = args.output_file
    if not out_file:
        # Simple default output name if not specified
        if '.' in args.input_file:
            base_name = args.input_file.rsplit('.', 1)[0]
            out_file = base_name + ".img"
        else:
            out_file = args.input_file + ".img"

    try:
        assemble_pdf_style(args.input_file, out_file)
    except FileNotFoundError:
        print(f"Error: Input file '{args.input_file}' not found.")
    except ValueError as e:
        print(f"Assembly Error: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
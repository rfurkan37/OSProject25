# assemble.py
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

def parse_label(line):
    """
    Checks if a line starts with a label (e.g., "MY_LABEL:").
    Returns (label_name, rest_of_line) or (None, original_line_stripped).
    """
    match = re.match(r'^\s*([a-zA-Z_][a-zA-Z0-9_]*):\s*(.*)', line)
    if match:
        return match.group(1), match.group(2).strip()
    return None, line.strip()

def assemble(input_filepath, output_filepath):
    data_labels = {}  # label_name -> address
    code_labels = {}  # label_name -> instruction_line_number

    # Store processed lines for Pass 2
    # Data: list of {'label': str|None, 'type': 'value'|'reserve', 'val_str': str, 'orig_line_num': int, 'address': int (assigned in pass1)}
    # Code: list of {'label': str|None, 'mnemonic': str, 'args_str': list[str], 'orig_line_num': int}
    data_definitions = []
    instruction_stream = []

    current_data_address = 0
    current_code_line_idx = 0
    current_section = None # 'data', 'text', or None

    # --- PASS 1: Collect labels, parse structure ---
    with open(input_filepath, 'r') as f_in:
        for line_num, raw_line in enumerate(f_in, 1):
            line = strip_comment_and_whitespace(raw_line)
            if not line:
                continue

            # Section directives
            if line.lower() == ".data":
                current_section = 'data'
                continue
            elif line.lower() == ".text":
                current_section = 'text'
                continue

            if current_section is None:
                raise ValueError(f"L{line_num}: Code or data '{line}' found before .data or .text directive.")

            label_name, rest_of_line = parse_label(line)

            if current_section == 'data':
                if label_name:
                    if label_name in data_labels or label_name in code_labels:
                        raise ValueError(f"L{line_num}: Label '{label_name}' redefined.")
                    data_labels[label_name] = current_data_address
                
                if not rest_of_line:
                    if label_name: # Label-only line in data usually means "current_address_marker"
                                   # or implies a default value (e.g. 0). Let's require a definition.
                        raise ValueError(f"L{line_num}: Data label '{label_name}' needs a definition (e.g., '123' or 'reserve 5').")
                    else: # Completely empty line after processing
                        continue

                parts = rest_of_line.split(maxsplit=1)
                data_keyword_or_value = parts[0]
                
                if data_keyword_or_value.lower() == 'reserve':
                    if len(parts) < 2:
                        raise ValueError(f"L{line_num}: 'reserve' needs a count (e.g., 'reserve 5').")
                    try:
                        count = int(parts[1])
                        if count <= 0:
                            raise ValueError(f"L{line_num}: 'reserve' count must be a positive integer.")
                    except ValueError:
                        raise ValueError(f"L{line_num}: Invalid count '{parts[1]}' for 'reserve'.")
                    
                    data_definitions.append({'label': label_name, 'type': 'reserve', 
                                             'val_str': str(count), 'orig_line_num': line_num, 
                                             'address': current_data_address})
                    current_data_address += count
                else: # It's a direct value (number or label to be resolved later)
                    data_definitions.append({'label': label_name, 'type': 'value', 
                                             'val_str': data_keyword_or_value, # The whole rest_of_line is the value if not 'reserve'
                                             'orig_line_num': line_num, 
                                             'address': current_data_address})
                    current_data_address += 1 # Each data value takes one memory location

            elif current_section == 'text':
                if label_name:
                    if label_name in code_labels or label_name in data_labels:
                        raise ValueError(f"L{line_num}: Label '{label_name}' redefined.")
                    code_labels[label_name] = current_code_line_idx
                
                if not rest_of_line: # Label-only line in .text is fine, marks the spot
                    if not label_name: # Completely empty line
                        continue
                    # If it was label_name only, the instruction is on the next line or it's an error.
                    # Our parse_label puts the rest on rest_of_line. So if rest_of_line is empty, it means label only.
                    # We associate label with the *next* actual instruction.
                    # For simplicity: a label should be on the same line as its instruction or data definition.
                    # Current code implies label_name applies to the instruction on *this* line.
                    # If rest_of_line is empty here, it means no instruction after label.
                    raise ValueError(f"L{line_num}: Code label '{label_name}' must be followed by an instruction on the same line.")

                # Process instruction: replace commas with spaces for easier splitting
                instruction_parts = rest_of_line.replace(',', ' ').split()
                mnemonic_token = instruction_parts[0].upper()
                arg_tokens = instruction_parts[1:]

                current_mnemonic = mnemonic_token
                if mnemonic_token == "SYSCALL":
                    if not arg_tokens:
                        raise ValueError(f"L{line_num}: SYSCALL instruction needs a type (e.g., PRN, HLT, YIELD).")
                    syscall_type = arg_tokens.pop(0).upper()
                    current_mnemonic = f"SYSCALL_{syscall_type}" # Internal representation
                
                if current_mnemonic not in OPCODE_ARGS_INFO:
                    raise ValueError(f"L{line_num}: Unknown mnemonic or SYSCALL type '{rest_of_line.split()[0]}'.")

                expected_arg_count = OPCODE_ARGS_INFO[current_mnemonic]
                if len(arg_tokens) != expected_arg_count:
                    raise ValueError(f"L{line_num}: Mnemonic '{current_mnemonic.replace('_',' ')}' expects {expected_arg_count} arguments, got {len(arg_tokens)}.")

                instruction_stream.append({'label': label_name, 'mnemonic': current_mnemonic, 
                                           'args_str': arg_tokens, 'orig_line_num': line_num,
                                           'code_line_idx': current_code_line_idx})
                current_code_line_idx += 1

    # --- PASS 2: Resolve labels and generate output ---
    with open(output_filepath, 'w') as f_out:
        # Data Section
        f_out.write("Begin Data Section\n")
        for item in data_definitions:
            address = item['address']
            if item['type'] == 'reserve':
                count = int(item['val_str'])
                for i in range(count):
                    f_out.write(f"{address + i} 0\n") # Reserve fills with 0
            elif item['type'] == 'value':
                val_str = item['val_str']
                try:
                    resolved_value = int(val_str) # Is it a literal number?
                except ValueError: # Not a literal, must be a label
                    if val_str in data_labels:
                        resolved_value = data_labels[val_str]
                    elif val_str in code_labels: # Allow storing code addresses in data
                        resolved_value = code_labels[val_str]
                    else:
                        raise ValueError(f"L{item['orig_line_num']}: Undefined label '{val_str}' used in data definition.")
                f_out.write(f"{address} {resolved_value}\n")
        f_out.write("End Data Section\n")

        # Instruction Section
        f_out.write("Begin Instruction Section\n")
        for instr in instruction_stream:
            output_args = []
            for arg_s in instr['args_str']:
                try:
                    resolved_arg = int(arg_s) # Literal number
                except ValueError: # Must be a label
                    if arg_s in data_labels:
                        resolved_arg = data_labels[arg_s]
                    elif arg_s in code_labels:
                        resolved_arg = code_labels[arg_s]
                    else:
                        raise ValueError(f"L{instr['orig_line_num']}: Undefined label '{arg_s}' used in instruction argument.")
                output_args.append(str(resolved_arg))
            
            # Format mnemonic for output (SYSCALL_X -> SYSCALL X)
            output_mnemonic = instr['mnemonic']
            if output_mnemonic.startswith("SYSCALL_"):
                output_mnemonic = f"SYSCALL {output_mnemonic.split('_', 1)[1]}"
            
            line_to_write = f"{instr['code_line_idx']} {output_mnemonic}"
            if output_args:
                line_to_write += f" {' '.join(output_args)}"
            f_out.write(line_to_write + "\n")
        f_out.write("End Instruction Section\n")

    print(f"Assembly successful: '{input_filepath}' -> '{output_filepath}'")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="GTU-C312 Assembler")
    parser.add_argument("input_file", help="Input assembly file (.g312asm)")
    parser.add_argument("-o", "--output_file", default=None, help="Output image file (.img)")

    args = parser.parse_args()

    out_file = args.output_file
    if not out_file:
        if args.input_file.lower().endswith(".g312asm"):
            out_file = args.input_file[:-7] + ".img"
        else:
            out_file = args.input_file + ".img" # Fallback if no specific extension

    try:
        assemble(args.input_file, out_file)
    except FileNotFoundError:
        print(f"Error: Input file '{args.input_file}' not found.")
    except ValueError as e:
        print(f"Assembly Error: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
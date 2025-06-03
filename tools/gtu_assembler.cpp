// tools/gtu_assembler.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm> // For std::transform, std::remove
#include <iomanip>   // For std::setw (optional for cleaner output)
#include <unordered_map>

// Bring in OpCode definition if we want to be super strict,
// or just use string comparisons for mnemonics.
// For simplicity here, we'll use string comparisons but map to expected arg counts.
// If you include ../src/instruction.h, ensure your build system can find it.
// #include "../src/instruction.h" // Might need to adjust include path

// Define mnemonic properties (name, expected operand count)
struct MnemonicInfo
{
    std::string name;
    int operand_count;
    // OpCode opcode_val; // Optional: if mapping to enum
};

const std::unordered_map<std::string, MnemonicInfo> MNEMONIC_TABLE = {
    {"SET", {"SET", 2}},
    {"CPY", {"CPY", 2}},
    {"CPYI", {"CPYI", 2}},
    {"CPYI2", {"CPYI2", 2}}, // Optional
    {"ADD", {"ADD", 2}},
    {"ADDI", {"ADDI", 2}},
    {"SUBI", {"SUBI", 2}},
    {"JIF", {"JIF", 2}},
    {"PUSH", {"PUSH", 1}},
    {"POP", {"POP", 1}},
    {"CALL", {"CALL", 1}},
    {"RET", {"RET", 0}},
    {"HLT", {"HLT", 0}},
    {"USER", {"USER", 1}} // USER A (takes one address operand)
    // SYSCALL is special
};

const std::unordered_map<std::string, MnemonicInfo> SYSCALL_SUBTYPE_TABLE = {
    {"PRN", {"SYSCALL PRN", 1}},
    {"HLT", {"SYSCALL HLT", 0}}, // Thread HLT
    {"YIELD", {"SYSCALL YIELD", 0}}};

// Helper to trim whitespace and comments
std::string trim_and_remove_comments(const std::string &s)
{
    std::string result = s;
    size_t comment_pos = result.find('#');
    if (comment_pos != std::string::npos)
    {
        result = result.substr(0, comment_pos);
    }
    // Trim leading whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r\f\v"));
    // Trim trailing whitespace
    result.erase(result.find_last_not_of(" \t\n\r\f\v") + 1);
    return result;
}

// Helper to split string by delimiter, handles multiple spaces
std::vector<std::string> split_string(const std::string &s, char delimiter = ' ')
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
    {
        // Handle multiple spaces by further check or istringstream property
        // The default istringstream >> token behavior handles multiple spaces well.
        // If using getline, need to filter empty tokens if multiple delimiters are together.
        // For simple space split, istringstream >> is easier:
    }

    // Reset stream for token extraction
    tokenStream.clear();
    tokenStream.str(s);
    std::string temp_token;
    while (tokenStream >> temp_token)
    {
        tokens.push_back(temp_token);
    }
    return tokens;
}

bool is_number(const std::string &s)
{
    if (s.empty())
        return false;
    char *end = nullptr;
    std::strtol(s.c_str(), &end, 10); // Use strtol for robust checking
    // Check if the entire string was consumed and it's not just a '-' or '+'
    return (*end == '\0' && !s.empty() && (s.length() > 1 || std::isdigit(s[0])));
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: ./gtu_assembler <input_file.g312> [output_file.img]" << std::endl;
        return 1;
    }

    std::string input_filename = argv[1];
    std::string output_filename;

    if (argc == 3)
    {
        output_filename = argv[2];
    }
    else
    {
        size_t dot_pos = input_filename.rfind(".g312");
        if (dot_pos != std::string::npos)
        {
            output_filename = input_filename.substr(0, dot_pos) + ".img";
        }
        else
        {
            output_filename = input_filename + ".img";
        }
    }

    std::ifstream infile(input_filename);
    if (!infile.is_open())
    {
        std::cerr << "Error: Could not open input file '" << input_filename << "'." << std::endl;
        return 1;
    }

    std::ofstream outfile(output_filename);
    if (!outfile.is_open())
    {
        std::cerr << "Error: Could not open output file '" << output_filename << "'." << std::endl;
        infile.close();
        return 1;
    }

    std::string line;
    int line_number = 0;
    enum class Section
    {
        NONE,
        DATA,
        INSTRUCTION
    };
    Section current_section = Section::NONE;
    int instruction_pc_counter = 0; // For validating instruction line numbers

    std::vector<std::string> processed_lines; // To store validated lines before writing

    while (std::getline(infile, line))
    {
        line_number++;
        std::string original_line = line; // For error messages
        std::string processed_line_content = trim_and_remove_comments(line);

        if (processed_line_content.empty())
        {
            // outfile << line << std::endl; // Preserve empty lines/comments if desired
            processed_lines.push_back(line); // Preserve full original line for comments/spacing
            continue;
        }

        std::string temp_line_upper = processed_line_content;
        std::transform(temp_line_upper.begin(), temp_line_upper.end(), temp_line_upper.begin(), ::toupper);

        if (temp_line_upper == "BEGIN DATA SECTION")
        {
            if (current_section != Section::NONE)
            {
                std::cerr << "Error L" << line_number << ": Unexpected 'Begin Data Section'. Current section: "
                          << static_cast<int>(current_section) << std::endl;
                return 1;
            }
            current_section = Section::DATA;
            processed_lines.push_back(processed_line_content);
            continue;
        }
        else if (temp_line_upper == "END DATA SECTION")
        {
            if (current_section != Section::DATA)
            {
                std::cerr << "Error L" << line_number << ": 'End Data Section' without matching 'Begin'." << std::endl;
                return 1;
            }
            current_section = Section::NONE;
            processed_lines.push_back(processed_line_content);
            continue;
        }
        else if (temp_line_upper == "BEGIN INSTRUCTION SECTION")
        {
            if (current_section != Section::NONE)
            {
                std::cerr << "Error L" << line_number << ": Unexpected 'Begin Instruction Section'." << std::endl;
                return 1;
            }
            current_section = Section::INSTRUCTION;
            instruction_pc_counter = 0; // Reset for new instruction section
            processed_lines.push_back(processed_line_content);
            continue;
        }
        else if (temp_line_upper == "END INSTRUCTION SECTION")
        {
            if (current_section != Section::INSTRUCTION)
            {
                std::cerr << "Error L" << line_number << ": 'End Instruction Section' without matching 'Begin'." << std::endl;
                return 1;
            }
            current_section = Section::NONE;
            processed_lines.push_back(processed_line_content);
            continue;
        }

        if (current_section == Section::NONE)
        {
            std::cerr << "Error L" << line_number << ": Content '" << processed_line_content << "' outside of any section." << std::endl;
            return 1;
        }

        // --- Process lines within sections ---
        if (current_section == Section::DATA)
        {
            std::vector<std::string> tokens = split_string(processed_line_content);
            if (tokens.size() != 2)
            {
                std::cerr << "Error L" << line_number << " (Data): Invalid format. Expected 'address value'. Got: '"
                          << processed_line_content << "'" << std::endl;
                return 1;
            }
            if (!is_number(tokens[0]) || !is_number(tokens[1]))
            {
                std::cerr << "Error L" << line_number << " (Data): Address and value must be integers. Got: '"
                          << processed_line_content << "'" << std::endl;
                return 1;
            }
            // Valid data line
            processed_lines.push_back(tokens[0] + " " + tokens[1]); // Reconstruct for consistent spacing
        }
        else if (current_section == Section::INSTRUCTION)
        {
            std::vector<std::string> tokens = split_string(processed_line_content);
            if (tokens.empty())
            { // Should have been caught by processed_line_content.empty()
                continue;
            }

            // Validate instruction line number
            if (!is_number(tokens[0]))
            {
                std::cerr << "Error L" << line_number << " (Instruction): Expected line index as first token. Got: '"
                          << tokens[0] << "'" << std::endl;
                return 1;
            }
            long instr_idx = std::stol(tokens[0]);
            if (instr_idx != instruction_pc_counter)
            {
                // This is a warning for now, as the simulator uses vector index.
                // But could be an error if strict sequential numbering is required by spec for the .img file.
                // The example shows "6 SET 2 0" then "7 HLT", so seems sequential.
                std::cerr << "Warning L" << line_number << " (Instruction): Line index '" << instr_idx
                          << "' does not match expected sequential PC '" << instruction_pc_counter << "'." << std::endl;
            }

            if (tokens.size() < 2)
            { // Must have at least line_idx and mnemonic
                std::cerr << "Error L" << line_number << " (Instruction): Incomplete instruction. Missing mnemonic. Got: '"
                          << processed_line_content << "'" << std::endl;
                return 1;
            }

            std::string mnemonic = tokens[1];
            std::transform(mnemonic.begin(), mnemonic.end(), mnemonic.begin(), ::toupper);

            std::vector<std::string> args;
            int expected_args = -1;
            std::string full_mnemonic_for_error = mnemonic;

            if (mnemonic == "SYSCALL")
            {
                if (tokens.size() < 3)
                {
                    std::cerr << "Error L" << line_number << " (Instruction): SYSCALL missing subtype (PRN, HLT, YIELD). Got: '"
                              << processed_line_content << "'" << std::endl;
                    return 1;
                }
                std::string subtype = tokens[2];
                std::transform(subtype.begin(), subtype.end(), subtype.begin(), ::toupper);
                full_mnemonic_for_error += " " + subtype;

                auto it = SYSCALL_SUBTYPE_TABLE.find(subtype);
                if (it == SYSCALL_SUBTYPE_TABLE.end())
                {
                    std::cerr << "Error L" << line_number << " (Instruction): Unknown SYSCALL subtype '" << subtype << "'. Got: '"
                              << processed_line_content << "'" << std::endl;
                    return 1;
                }
                expected_args = it->second.operand_count;
                for (size_t i = 3; i < tokens.size(); ++i)
                { // Args start after PC, MNEMONIC, SUBTYPE
                    args.push_back(tokens[i]);
                }
            }
            else
            {
                auto it = MNEMONIC_TABLE.find(mnemonic);
                if (it == MNEMONIC_TABLE.end())
                {
                    std::cerr << "Error L" << line_number << " (Instruction): Unknown mnemonic '" << mnemonic << "'. Got: '"
                              << processed_line_content << "'" << std::endl;
                    return 1;
                }
                expected_args = it->second.operand_count;
                for (size_t i = 2; i < tokens.size(); ++i)
                { // Args start after PC, MNEMONIC
                    // Handle comma: if "val1," it's one token. if "val1 ," it's two.
                    // Simplest for now: assume args are separated by spaces primarily.
                    // The spec shows "SET -20, 100". This means "val," is one token or we need to parse better.
                    // Let's assume "split_string" gives "val1," and "val2" or "val1" and "val2"
                    // For "SET B, A", tokens might be ["idx", "SET", "B,", "A"] or ["idx", "SET", "B", ",", "A"]
                    // The current split_string is basic. For robustness:
                    // std::string arg_token_raw = tokens[i];
                    // if (arg_token_raw.back() == ',') arg_token_raw.pop_back();
                    // if (!arg_token_raw.empty()) args.push_back(arg_token_raw);
                    // This doesn't handle "B , A". For now, assume "B,A" or "B A".

                    // Let's refine argument collection to handle one optional comma for 2-arg instructions.
                    // The first arg token is tokens[2].
                    // If expected_args == 2:
                    //   token[2] is arg1. It might have a comma: "val1,".
                    //   token[3] is arg2.
                    // If tokens[2] is "val1," and tokens[3] is "val2", it's fine.
                    // If tokens[2] is "val1" and tokens[3] is "," and tokens[4] is "val2" -> needs better splitter or parser.
                    // Current split_string will give ["idx", "SET", "B,", "A"] for "idx SET B, A"
                    // And ["idx", "SET", "B", "A"] for "idx SET B A"
                    // And ["idx", "SET", "B", ",", "A"] for "idx SET B , A" -> problem for current loop.

                    // Let's reconstruct args more carefully for validation:
                    // After mnemonic (tokens[1]), all subsequent tokens are potential args or commas.
                }
                // Rebuild args vector from tokens[2] onwards, stripping commas from ends of tokens
                // And skipping standalone comma tokens.
                for (size_t i = 2; i < tokens.size(); ++i)
                {
                    std::string current_arg_token = tokens[i];
                    if (current_arg_token == ",")
                        continue; // Skip standalone comma
                    if (!current_arg_token.empty() && current_arg_token.back() == ',')
                    {
                        current_arg_token.pop_back();
                    }
                    if (!current_arg_token.empty())
                    { // Ensure not an empty string after stripping comma
                        args.push_back(current_arg_token);
                    }
                }
            }

            if (static_cast<int>(args.size()) != expected_args)
            {
                std::cerr << "Error L" << line_number << " (Instruction): Mnemonic '" << full_mnemonic_for_error
                          << "' expects " << expected_args << " arguments, got " << args.size()
                          << " in '" << processed_line_content << "'" << std::endl;
                return 1;
            }

            for (const auto &arg : args)
            {
                if (!is_number(arg))
                {
                    std::cerr << "Error L" << line_number << " (Instruction): Argument '" << arg << "' for '"
                              << full_mnemonic_for_error << "' must be an integer. Line: '"
                              << processed_line_content << "'" << std::endl;
                    return 1;
                }
            }

            // Valid instruction line. Reconstruct it for output for consistent format.
            std::string validated_instr_line = tokens[0] + " " + mnemonic;
            if (mnemonic == "SYSCALL")
            {
                validated_instr_line += " " + tokens[2]; // Subtype
            }
            for (size_t i = 0; i < args.size(); ++i)
            {
                validated_instr_line += " " + args[i];
                // Optional: Add comma back if it was "SET B, A" style.
                // For simplicity, output space separated. The parser in sim.cpp will handle both.
                // If strict output comma needed:
                // if (expected_args == 2 && i == 0) validated_instr_line += ",";
            }
            processed_lines.push_back(validated_instr_line);
            instruction_pc_counter++;
        }
    }

    // After parsing all lines, write the processed (and original comment/empty) lines
    for (const auto &p_line : processed_lines)
    {
        outfile << p_line << std::endl;
    }

    std::cout << "Assembly validation successful: '" << input_filename << "' -> '" << output_filename << "'" << std::endl;

    infile.close();
    outfile.close();
    return 0;
}
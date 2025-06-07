#ifndef PARSER_H
#define PARSER_H

#include <vector>
#include <iosfwd>
#include <string>

// Forward declarations
struct Instruction;

// Instruction parser for .img files (pre-assembled)
// For .g312 files, use tools/gtu_assembler first to convert to .img
std::vector<Instruction> parseInstructionSection(std::ifstream &fileStream, const std::string &filename, int &lineOffset);

#endif // PARSER_H 
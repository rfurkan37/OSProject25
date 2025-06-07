// src/memory.cpp
// src/memory.cpp
#include "memory.h"
#include "common.h"
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm> // For std::fill, std::max, std::min
#include <iomanip> // For std::setw

Memory::Memory(size_t initialSize) : size_(initialSize)
{
    if (initialSize == 0) {
        throw std::invalid_argument("Memory size cannot be zero.");
    }
    // REGISTERS_END_ADDR is 20, so minimum size is 21 (0-20)
    if (initialSize < REGISTERS_END_ADDR + 1) {
        std::cerr << "Warning: Memory size " << initialSize 
                  << " is less than " << (REGISTERS_END_ADDR + 1) 
                  << " (minimum for registers)." << std::endl;
    }
    data_.resize(size_, 0L);
}

void Memory::checkAddress(long address) const
{
    if (address < 0 || static_cast<size_t>(address) >= size_) {
        std::ostringstream errMsg;
        errMsg << "Memory access violation: Address " << address
               << " is out of bounds (0-" << size_ - 1 << ").";
        throw std::out_of_range(errMsg.str());
    }
}

long Memory::read(long address) const
{
    checkAddress(address);
    return data_[static_cast<size_t>(address)];
}

void Memory::write(long address, long value)
{
    checkAddress(address);
    data_[static_cast<size_t>(address)] = value;
}

void Memory::clear()
{
    std::fill(data_.begin(), data_.end(), 0L);
}

static std::string trimLine(std::string line)
{
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos) {
        line = line.substr(0, commentPos);
    }
    line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
    line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
    return line;
}

bool Memory::loadDataSection(std::ifstream &imageFileStream, int& linesReadCount)
{
    if (!imageFileStream.is_open() || imageFileStream.eof()) {
        std::cerr << "Error: File stream is not open or at EOF." << std::endl;
        return false;
    }

    std::string line;
    bool inDataSection = false;

    // Find "Begin Data Section"
    while (std::getline(imageFileStream, line)) {
        linesReadCount++;
        line = trimLine(line);
        if (line == "Begin Data Section") {
            inDataSection = true;
            break;
        }
    }

    if (!inDataSection) {
        std::cerr << "Error: 'Begin Data Section' marker not found." << std::endl;
        // It's possible the file only contains an instruction section, so reset stream for instruction parsing
        imageFileStream.clear(); // Clear EOF flags
        imageFileStream.seekg(0, std::ios::beg); // Rewind
        linesReadCount = 0; // Reset lines read for instruction parser
        return true; // Not a fatal error for the whole program if data section is optional
    }

    // Process data lines
    while (std::getline(imageFileStream, line)) {
        linesReadCount++;
        line = trimLine(line);

        if (line == "End Data Section") {
            return true;
        }
        if (line.empty()) {
            continue;
        }

        std::istringstream iss(line);
        long address, value;
        char comma; // To consume potential comma if format is address, value

        // Try parsing "address value"
        iss >> address;
        if (iss.fail()) {
             std::cerr << "Error: Invalid data format (address) in line: '" << line << "' at file line " << linesReadCount << "." << std::endl;
            return false;
        }
        // Check for optional comma
        if (iss.peek() == ',') {
            iss >> comma;
        }
        iss >> value;
        if (iss.fail()) {
             std::cerr << "Error: Invalid data format (value) in line: '" << line << "' at file line " << linesReadCount << "." << std::endl;
            return false;
        }


        // Check for trailing characters
        std::string remaining;
        iss >> remaining;
        if (!remaining.empty()) {
            std::cerr << "Error: Trailing characters in data line: '" << line << "' at file line " << linesReadCount << "." << std::endl;
            return false;
        }


        try {
            write(address, value);
        } catch (const std::out_of_range &e) {
            std::cerr << "Error: " << e.what() << " (loading line: '" << line << "' at file line " << linesReadCount << ")" << std::endl;
            return false;
        }
    }

    std::cerr << "Error: 'End Data Section' marker not found before EOF." << std::endl;
    return false;
}

void Memory::dumpMemoryRange(std::ostream &out, long startAddr, long endAddr) const
{
    long effectiveStartAddr = std::max(0L, startAddr);
    long effectiveEndAddr = std::min(static_cast<long>(size_ - 1), endAddr);

    if (effectiveStartAddr > effectiveEndAddr || effectiveStartAddr >= static_cast<long>(size_)) { // Added check for start >= size
        return;
    }

    for (long addr = effectiveStartAddr; addr <= effectiveEndAddr; ++addr) {
        out << addr << ":" << data_[static_cast<size_t>(addr)] << std::endl;
    }
}

void Memory::dumpMemoryRangeTable(std::ostream &out, long startAddr, long endAddr) const
{
    long effectiveStartAddr = std::max(0L, startAddr);
    long effectiveEndAddr = std::min(static_cast<long>(size_ - 1), endAddr);

    if (effectiveStartAddr > effectiveEndAddr || effectiveStartAddr >= static_cast<long>(size_)) {
        return;
    }

    const int COLS = 10; // 10 columns per row
    
    // Print header
    out << "Addr:  |";
    for (int i = 0; i < COLS; ++i) {
        out << std::setw(6) << i << " |";
    }
    out << std::endl;
    
    // Print separator line
    out << "-------|";
    for (int i = 0; i < COLS; ++i) {
        out << "-------|";
    }
    out << std::endl;

    // Print data rows
    for (long addr = effectiveStartAddr; addr <= effectiveEndAddr; addr += COLS) {
        long rowStart = addr;
        long rowEnd = std::min(addr + COLS - 1, effectiveEndAddr);
        
        // Print row address
        out << std::setw(6) << rowStart << " |";
        
        // Print values for this row
        for (long col = 0; col < COLS; ++col) {
            long currentAddr = rowStart + col;
            if (currentAddr <= rowEnd) {
                long value = data_[static_cast<size_t>(currentAddr)];
                out << std::setw(6) << value << " |";
            } else {
                out << "       |"; // Empty cell for addresses beyond range
            }
        }
        out << std::endl;
    }
}

void Memory::dumpImportantRegions(std::ostream &out) const
{
    out << "--- Registers (0-" << REGISTERS_END_ADDR << ") - TABLE FORMAT ---" << std::endl;
    dumpMemoryRangeTable(out, 0, REGISTERS_END_ADDR);

    // OS_DATA_END_ADDR is 999
    out << "--- OS Data Area (" << OS_DATA_START_ADDR << "-" << OS_DATA_END_ADDR << ") - TABLE FORMAT ---" << std::endl;
    dumpMemoryRangeTable(out, OS_DATA_START_ADDR, std::min(OS_DATA_END_ADDR, static_cast<long>(size_ - 1)));

    if (size_ > USER_MEMORY_START_ADDR) {
        // Show extended thread data areas instead of just a small sample
        
        // First show a basic sample for context (1000-1019) - use table format for consistency
        long thread_sample_end_addr = std::min(USER_MEMORY_START_ADDR + 19L, static_cast<long>(size_ - 1));
        if (thread_sample_end_addr >= USER_MEMORY_START_ADDR) {
            out << "--- Sample User Area (" << USER_MEMORY_START_ADDR << "-" << thread_sample_end_addr << ") - TABLE FORMAT ---" << std::endl;
            dumpMemoryRangeTable(out, USER_MEMORY_START_ADDR, thread_sample_end_addr);
        }
        
        // Thread 1 data area (1100-1199) - use table format for compact display
        long thread1_start = 1100;
        long thread1_end = std::min(1199L, static_cast<long>(size_ - 1));
        if (thread1_end >= thread1_start && static_cast<long>(size_) > thread1_start) {
            out << "--- Thread 1 Data Area (" << thread1_start << "-" << thread1_end << ") - TABLE FORMAT ---" << std::endl;
            dumpMemoryRangeTable(out, thread1_start, thread1_end);
        }
        
        // Thread 2 data area (1200-1299) - use table format for compact display
        long thread2_start = 1200;
        long thread2_end = std::min(1299L, static_cast<long>(size_ - 1));
        if (thread2_end >= thread2_start && static_cast<long>(size_) > thread2_start) {
            out << "--- Thread 2 Data Area (" << thread2_start << "-" << thread2_end << ") - TABLE FORMAT ---" << std::endl;
            dumpMemoryRangeTable(out, thread2_start, thread2_end);
        }
        
        // Thread 3 data area (1300-1399) - use table format for compact display  
        long thread3_start = 1300;
        long thread3_end = std::min(1399L, static_cast<long>(size_ - 1));
        if (thread3_end >= thread3_start && static_cast<long>(size_) > thread3_start) {
            out << "--- Thread 3 Data Area (" << thread3_start << "-" << thread3_end << ") - TABLE FORMAT ---" << std::endl;
            dumpMemoryRangeTable(out, thread3_start, thread3_end);
        }
    }
}
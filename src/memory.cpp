// src/memory.cpp
#include "memory.h"
#include <sstream>   // For std::istringstream, std::ostringstream
#include <iomanip>   // For std::setw (optional for dump formatting)
#include <algorithm> // For std::max, std::min

// Constructor
Memory::Memory(size_t initialSize) : size_(initialSize) {
    if (initialSize == 0) { // Or a more practical minimum like 21 for registers
        throw std::invalid_argument("Memory size cannot be zero (or less than minimum required).");
    }
    if (initialSize < 21) { // Project implies registers 0-20
         std::cerr << "Warning: Memory size " << initialSize << " is less than 21 (minimum for registers)." << std::endl;
    }
    data_.resize(size_, 0L); // Initialize with zeros
}

// Helper to check address validity
void Memory::checkAddress(long address) const {
    if (address < 0 || static_cast<size_t>(address) >= size_) {
        std::ostringstream errMsg;
        errMsg << "Memory access violation: Address " << address
               << " is out of bounds (0-" << size_ - 1 << ").";
        throw std::out_of_range(errMsg.str());
    }
}

// Reads a long value from the specified memory address.
long Memory::read(long address) const {
    checkAddress(address);
    return data_[static_cast<size_t>(address)];
}

// Writes a long value to the specified memory address.
void Memory::write(long address, long value) {
    checkAddress(address);
    data_[static_cast<size_t>(address)] = value;
}

// Clears all memory to zero.
void Memory::clear() {
    std::fill(data_.begin(), data_.end(), 0L);
}

// Loads the "Data Section" from a program file stream into memory.
bool Memory::loadDataSection(std::ifstream& imageFileStream) {
    if (!imageFileStream.is_open() || imageFileStream.eof()) {
        std::cerr << "Error:loadDataSection: File stream is not open or at EOF." << std::endl;
        return false;
    }

    std::string line;
    bool inDataSection = false;

    // First, find "Begin Data Section"
    while (std::getline(imageFileStream, line)) {
        // Trim whitespace and comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line == "Begin Data Section") {
            inDataSection = true;
            break;
        }
    }

    if (!inDataSection) {
        std::cerr << "Error:loadDataSection: 'Begin Data Section' marker not found." << std::endl;
        // Reset stream position to beginning if caller needs to re-read for instructions
        // imageFileStream.clear(); 
        // imageFileStream.seekg(0, std::ios::beg); 
        return false; 
    }

    // Now process lines until "End Data Section"
    while (std::getline(imageFileStream, line)) {
        // Trim whitespace and comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line == "End Data Section") {
            return true; // Successfully loaded data section
        }

        if (line.empty()) { // Skip empty lines
            continue;
        }

        std::istringstream iss(line);
        long address, value;
        if (!(iss >> address >> value)) {
            std::cerr << "Error:loadDataSection: Invalid data format in line: '" << line << "'. Expected 'address value'." << std::endl;
            // Continue to try and parse other lines? Or fail hard?
            // For now, let's fail if any line is malformed within the section.
            return false;
        }
        
        // Check if address is within bounds AFTER parsing
        if (address < 0 || static_cast<size_t>(address) >= size_) {
            std::cerr << "Error:loadDataSection: Address " << address 
                      << " from line '" << line << "' is out of memory bounds (0-" << size_ - 1 << ")." << std::endl;
            return false;
        }

        try {
            write(address, value); // Use our checked write, though not strictly necessary for initial load
        } catch (const std::out_of_range& e) { // Should be caught by the check above, but defense in depth
            std::cerr << "Error:loadDataSection: " << e.what() << " for line '" << line << "'" << std::endl;
            return false;
        }
    }

    // If we reach here, it means EOF was hit before "End Data Section"
    std::cerr << "Error:loadDataSection: 'End Data Section' marker not found before EOF." << std::endl;
    return false;
}

// Dumps memory contents for a specified range to the given output stream.
void Memory::dumpMemoryRange(std::ostream& out, long startAddr, long endAddr) const {
    long effectiveStartAddr = std::max(0L, startAddr);
    long effectiveEndAddr = std::min(static_cast<long>(size_ - 1), endAddr);

    if (effectiveStartAddr > effectiveEndAddr) {
        return;
    }

    for (long addr = effectiveStartAddr; addr <= effectiveEndAddr; ++addr) {
        out << addr << ":" << data_[static_cast<size_t>(addr)] << std::endl;
    }
}

// Dumps predefined important regions.
void Memory::dumpImportantRegions(std::ostream& out) const {
    out << "--- Registers (Memory Mapped: 0-20) ---" << std::endl;
    dumpMemoryRange(out, 0, 20);

    out << "--- OS Data Area Sample (e.g., 21-99 or as configured) ---" << std::endl;
    dumpMemoryRange(out, 21, std::min(99L, static_cast<long>(size_ -1) )); 

    // For example, show the start of where thread 1's data might be
    if (size_ > 1000) {
        out << "--- Thread 1 Data Area Sample (e.g., 1000-1019) ---" << std::endl;
        dumpMemoryRange(out, 1000, std::min(1019L, static_cast<long>(size_ -1)));
    }
}

// Returns the total size of the memory.
size_t Memory::getSize() const {
    return size_;
}
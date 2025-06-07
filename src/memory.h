// src/memory.h
#ifndef MEMORY_H
#define MEMORY_H

#include <stdexcept> // For std::out_of_range, std::invalid_argument - needed for exceptions
#include <iosfwd>    // Forward declarations for stream types
#include <vector>    // For std::vector<long> member - required for member variables

class Memory
{
public:
    // Constructor: Initializes memory of a given size with all zeros.
    // Minimum size of 11000 to accommodate OS and 10 threads' basic data segments.
    explicit Memory(size_t initialSize = 11000);

    // Reads a long value from the specified memory address.
    // Throws std::out_of_range if address is invalid.
    long read(long address) const;

    // Writes a long value to the specified memory address.
    // Throws std::out_of_range if address is invalid.
    void write(long address, long value);

    // Loads the "Data Section" from a program file into memory.
    // The file is expected to contain lines defining initial memory values.
    // It parses lines starting from "Begin Data Section" until "End Data Section".
    // Format within data section: "address value" (e.g., "0 0", "10 50").
    // Ignores comments (#) and empty lines.
    // Returns true on success, false on failure (e.g., file not found, parse error, section markers missing).
    bool loadDataSection(std::ifstream &imageFileStream, int& lines_read_count); // Takes an open file stream

    // Dumps memory contents for a specified range to the given output stream.
    // Prints each address and its content in the format "address:value".
    // Ensures startAddr and endAddr are within valid bounds.
    void dumpMemoryRange(std::ostream &out, long startAddr, long endAddr) const;

    // Dumps memory contents in a compact table format (10 columns per row).
    // More organized and space-efficient than dumpMemoryRange for viewing large ranges.
    void dumpMemoryRangeTable(std::ostream &out, long startAddr, long endAddr) const;

    // Dumps predefined important regions to the output stream.
    // This is a utility function for debugging, typically showing registers and OS areas.
    void dumpImportantRegions(std::ostream &out) const;

    // Returns the total size of the memory (number of long locations).
    size_t getSize() const { return size_; }

    // Clears all memory to zero.
    void clear();

private:
    std::vector<long> data_;
    size_t size_; // Stores the actual configured size of the memory

    // Helper to check address validity and throw std::out_of_range if invalid.
    void checkAddress(long address) const;
};

#endif // MEMORY_H
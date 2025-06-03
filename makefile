# Makefile for GTU OS Project

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g # -g for debugging, -O2 for optimization later
LDFLAGS =

# Source directories
SRC_DIR = src
TOOLS_DIR = tools
EXAMPLES_DIR = examples

# Simulator executable
SIM_EXEC = gtu_sim
SIM_SOURCES = $(SRC_DIR)/cpu.cpp $(SRC_DIR)/memory.cpp $(SRC_DIR)/main.cpp # Change main.cpp if you named it os_simulator.cpp
SIM_OBJECTS = $(SIM_SOURCES:.cpp=.o)

# Assembler executable
ASSEMBLER_EXEC = $(TOOLS_DIR)/gtu_assembler
ASSEMBLER_SOURCES = $(TOOLS_DIR)/gtu_assembler.cpp
ASSEMBLER_OBJECTS = $(ASSEMBLER_SOURCES:.cpp=.o)

# Default target
all: $(SIM_EXEC) $(ASSEMBLER_EXEC)

# --- Simulator Compilation ---
$(SIM_EXEC): $(SIM_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp $(SRC_DIR)/common.h $(SRC_DIR)/instruction.h $(SRC_DIR)/cpu.h $(SRC_DIR)/memory.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -c $< -o $@

# --- Assembler Compilation ---
$(ASSEMBLER_EXEC): $(ASSEMBLER_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TOOLS_DIR)/%.o: $(TOOLS_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@


# --- Assembling .g312 files to .img ---
# This rule allows you to type 'make examples/my_program.img'
# It will find 'examples/my_program.g312' and assemble it.
$(EXAMPLES_DIR)/%.img: $(EXAMPLES_DIR)/%.g312 $(ASSEMBLER_EXEC)
	@echo "Assembling $< -> $@"
	$(ASSEMBLER_EXEC) $< $@

# --- Running the Simulator ---
# Example: make run FILE=examples/simple_add.img
# Example: make run_debug FILE=examples/simple_add.img DEBUG_MODE=1
# Note: This requires the .img file to exist (either manually assembled or via make examples/....img)
run:
ifndef FILE
	$(error FILE is not set. Usage: make run FILE=path/to/your.img)
endif
	@echo "Running simulator with $(FILE)..."
	./$(SIM_EXEC) $(FILE)

run_debug:
ifndef FILE
	$(error FILE is not set. Usage: make run_debug FILE=path/to/your.img DEBUG_MODE=<0|1|2|3>)
endif
ifndef DEBUG_MODE
	$(error DEBUG_MODE is not set. Usage: make run_debug FILE=path/to/your.img DEBUG_MODE=<0|1|2|3>)
endif
	@echo "Running simulator with $(FILE) in DEBUG_MODE $(DEBUG_MODE)..."
	./$(SIM_EXEC) $(FILE) -D$(DEBUG_MODE)


# --- Clean up ---
clean:
	@echo "Cleaning up..."
	rm -f $(SIM_EXEC) $(ASSEMBLER_EXEC)
	rm -f $(SRC_DIR)/*.o $(TOOLS_DIR)/*.o
	rm -f $(EXAMPLES_DIR)/*.img # Be careful with this if you have manually created .img files you want to keep
	@echo "Clean complete."

# Phony targets (targets that are not files)
.PHONY: all clean run run_debug

# Optional: A rule to assemble all .g312 files in examples directory
# Find all .g312 files in EXAMPLES_DIR
G312_FILES = $(wildcard $(EXAMPLES_DIR)/*.g312)
# Create corresponding .img file names
IMG_FILES = $(G312_FILES:.g312=.img)

assemble_all_examples: $(IMG_FILES)
	@echo "All example .g312 files assembled."

# List object files for dependency tracking more explicitly if headers change
# This is already implicitly handled by the .o rule dependencies for src files.
# $(SIM_OBJECTS): $(SRC_DIR)/common.h $(SRC_DIR)/instruction.h $(SRC_DIR)/cpu.h $(SRC_DIR)/memory.h
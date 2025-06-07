# Makefile for GTU OS Project
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -DNDEBUG

# Directories
SRC_DIR = src
TOOLS_DIR = tools
EXAMPLES_DIR = examples
PROGRAMS_DIR = programs

# Executables
SIM_EXEC = gtu_sim
ASSEMBLER_EXEC = $(TOOLS_DIR)/gtu_assembler

# Source files (removed label_resolver.cpp since we simplified)
SIM_SOURCES = $(SRC_DIR)/cpu.cpp $(SRC_DIR)/memory.cpp $(SRC_DIR)/main.cpp $(SRC_DIR)/instruction.cpp $(SRC_DIR)/parser.cpp
SIM_OBJECTS = $(SIM_SOURCES:.cpp=.o)
ASSEMBLER_SOURCES = $(TOOLS_DIR)/gtu_assembler.cpp
ASSEMBLER_OBJECTS = $(ASSEMBLER_SOURCES:.cpp=.o)

.PHONY: all clean run run_debug assemble_and_run assemble_all_examples test test_phase1

all: $(SIM_EXEC) $(ASSEMBLER_EXEC)

# Simulator (simplified - only handles .img files)
$(SIM_EXEC): $(SIM_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Generate symbols file if it doesn't exist
$(PROGRAMS_DIR)/os_and_threads_symbols.h: $(PROGRAMS_DIR)/os_and_threads.g312 $(ASSEMBLER_EXEC)
	@echo "Generating symbol definitions for C++ code..."
	$(ASSEMBLER_EXEC) $(PROGRAMS_DIR)/os_and_threads.g312 $(PROGRAMS_DIR)/os_and_threads.img $(PROGRAMS_DIR)/os_and_threads_symbols.h

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp $(PROGRAMS_DIR)/os_and_threads_symbols.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(PROGRAMS_DIR) -DUSE_ASSEMBLED_SYMBOLS -c $< -o $@

# Assembler (converts .g312 to .img)
$(ASSEMBLER_EXEC): $(ASSEMBLER_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TOOLS_DIR)/%.o: $(TOOLS_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Assembly rule for examples
$(EXAMPLES_DIR)/%.img: $(EXAMPLES_DIR)/%.g312 $(ASSEMBLER_EXEC)
	@echo "Assembling $< -> $@"
	$(ASSEMBLER_EXEC) $< $@

# Assembly rule for programs
$(PROGRAMS_DIR)/%.img: $(PROGRAMS_DIR)/%.g312 $(ASSEMBLER_EXEC)
	@echo "Assembling $< -> $@"
	$(ASSEMBLER_EXEC) $< $@

# Assembly rule for tests
tests/%.img: tests/%.g312 $(ASSEMBLER_EXEC)
	@echo "Assembling $< -> $@"
	$(ASSEMBLER_EXEC) $< $@

# Generic assembly rule for any .g312 file
%.img: %.g312 $(ASSEMBLER_EXEC)
	@echo "Assembling $< -> $@ (with symbol export)"
	$(ASSEMBLER_EXEC) $< $@ $(<:.g312=_symbols.h)

# Enhanced run targets - automatically assemble if needed
run:
ifndef FILE
	$(error FILE is not set. Usage: make run FILE=path/to/your.g312_or_.img)
endif
ifeq ($(suffix $(FILE)),.g312)
	@echo "Assembling $(FILE) first..."
	$(MAKE) $(FILE:.g312=.img)
	@echo "Running simulator with $(FILE:.g312=.img)..."
	./$(SIM_EXEC) $(FILE:.g312=.img)
else
	@echo "Running simulator with $(FILE)..."
	./$(SIM_EXEC) $(FILE)
endif

run_debug:
ifndef FILE
	$(error FILE is not set. Usage: make run_debug FILE=path/to/your.g312_or_.img DEBUG_MODE=<0|1|2|3>)
endif
ifndef DEBUG_MODE
	$(error DEBUG_MODE is not set. Usage: make run_debug FILE=path/to/your.g312_or_.img DEBUG_MODE=<0|1|2|3>)
endif
ifeq ($(suffix $(FILE)),.g312)
	@echo "Assembling $(FILE) first..."
	$(MAKE) $(FILE:.g312=.img)
	@echo "Running simulator with $(FILE:.g312=.img) in DEBUG_MODE $(DEBUG_MODE)..."
	./$(SIM_EXEC) $(FILE:.g312=.img) -D$(DEBUG_MODE)
else
	@echo "Running simulator with $(FILE) in DEBUG_MODE $(DEBUG_MODE)..."
	./$(SIM_EXEC) $(FILE) -D$(DEBUG_MODE)
endif

# Two-step workflow helper
assemble_and_run:
ifndef FILE
	$(error FILE is not set. Usage: make assemble_and_run FILE=program.g312 [DEBUG_MODE=1])
endif
	@echo "=== STEP 1: Assembling $(FILE) ==="
	$(ASSEMBLER_EXEC) $(FILE) $(FILE:.g312=.img)
	@echo "=== STEP 2: Running $(FILE:.g312=.img) ==="
ifdef DEBUG_MODE
	./$(SIM_EXEC) $(FILE:.g312=.img) -D$(DEBUG_MODE)
else
	./$(SIM_EXEC) $(FILE:.g312=.img)
endif

# Clean
clean:
	@echo "Cleaning up..."
	rm -f $(SIM_EXEC) $(ASSEMBLER_EXEC)
	rm -f $(SRC_DIR)/*.o $(TOOLS_DIR)/*.o $(EXAMPLES_DIR)/*.img $(PROGRAMS_DIR)/*.img $(PROGRAMS_DIR)/*_symbols.h
	@echo "Clean complete."

# Assemble all examples
G312_FILES = $(wildcard $(EXAMPLES_DIR)/*.g312)
IMG_FILES = $(G312_FILES:.g312=.img)

assemble_all_examples: $(IMG_FILES)
	@echo "All example .g312 files assembled."

# Test targets
test: $(SIM_EXEC) $(ASSEMBLER_EXEC)
	@./tests/test_runner.sh all

test_phase1: $(SIM_EXEC) $(ASSEMBLER_EXEC)
	@./tests/test_runner.sh phase1
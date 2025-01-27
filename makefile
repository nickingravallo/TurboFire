# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17

# Target executable and source files
TARGET = TurboFire
SRC = TurboFire.cpp
OUT_DIR = output

# Default rule
all: $(OUT_DIR)/$(TARGET)

# Rule to build the target
$(OUT_DIR)/$(TARGET): $(SRC)
	mkdir -p $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -o $(OUT_DIR)/$(TARGET) $(SRC)

# Clean rule
clean:
	rm -rf $(OUT_DIR)

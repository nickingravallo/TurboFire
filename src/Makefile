CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O3 -march=native -fopenmp
CXXFLAGS = -O3 -std=c++17 -I./omp -fopenmp
LDFLAGS = -fopenmp

# All C object files needed
C_OBJS = main.o parse.o

# All C++ object files needed
CXX_OBJS = evaluator.o omp/HandEvaluator.o

TARGET = turbofire

all: $(TARGET)

$(TARGET): $(C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(C_OBJS) $(CXX_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o omp/*.o $(TARGET)

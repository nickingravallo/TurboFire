CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17
SOURCES = TurboFire.cpp solver.cpp hands.cpp evaluator.cpp omp/HandEvaluator.cpp chance.cpp
RANGE_FLAGS = -DTURBOFIRE_RANGE_DIR='"$(abspath ../ranges)"'
HEADERS = solver.hpp hands.hpp evaluator.hpp chance.hpp $(wildcard omp/*.h omp/*.hxx)

turbofire: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(RANGE_FLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

.PHONY: check
check:
	@set -e; binary=$$(mktemp); trap 'rm -f "$$binary"' EXIT; \
	$(CXX) $(CPPFLAGS) $(RANGE_FLAGS) $(CXXFLAGS) test_chance.cpp $(filter-out TurboFire.cpp,$(SOURCES)) $(LDFLAGS) -o "$$binary"; \
	"$$binary"

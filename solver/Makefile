CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17
SOURCES = TurboFire.cpp solver.cpp hands.cpp evaluator.cpp omp/HandEvaluator.cpp chance.cpp
HEADERS = solver.hpp hands.hpp evaluator.hpp chance.hpp $(wildcard omp/*.h omp/*.hxx)

turbofire: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

.PHONY: check
check:
	@set -e; binary=$$(mktemp); trap 'rm -f "$$binary"' EXIT; \
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) test_chance.cpp $(filter-out TurboFire.cpp,$(SOURCES)) $(LDFLAGS) -o "$$binary"; \
	"$$binary"

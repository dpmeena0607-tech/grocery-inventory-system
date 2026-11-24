CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
INCLUDES  = -Iinclude

all: grocery-inventory

grocery-inventory: src/main.cpp src/Inventory.cpp include/Inventory.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/main.cpp src/Inventory.cpp -o $@

test-runner: tests/test_main.cpp src/Inventory.cpp include/Inventory.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_main.cpp src/Inventory.cpp -o $@

test: test-runner
	./test-runner

# Try the program on a copy of the sample data (the copy is ignored by git).
demo: grocery-inventory
	cp data/sample_inventory.csv data/demo.csv
	./grocery-inventory data/demo.csv

clean:
	rm -f grocery-inventory test-runner data/demo.csv data/*.tmp test_inv*

.PHONY: all test demo clean

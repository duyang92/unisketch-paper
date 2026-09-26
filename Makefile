CXX ?= g++
CPPFLAGS := -I .
CXXFLAGS ?= -std=c++17 -O3 -mavx2
TARGET := build/unisketch
SOURCES := simulation/main.cpp utils/MurmurHash3.cpp
HEADERS := $(shell find algorithms utils -type f -name '*.h' -print)

.PHONY: all test run-minimal run-all clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@

test: all
	./tests/cli_test.sh
	./tests/sanitizer_test.sh

run-minimal: all
	./scripts/run_minimal.sh

run-all: all
	./scripts/run_all.sh

clean:
	rm -rf build

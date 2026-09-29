CXX ?= g++
CPPFLAGS := -I .
CXXFLAGS ?= -std=c++17 -O3 -mavx2
TARGET := build/unisketch
MULTIPERIOD_TARGET := build/unisketch-multiperiod
SOURCES := simulation/main.cpp utils/MurmurHash3.cpp
MULTIPERIOD_SOURCES := simulation/multiperiod.cpp utils/MurmurHash3.cpp
HEADERS := $(shell find algorithms utils -type f -name '*.h' -print)

.PHONY: all make-single make-multi test test-all test-single test-multi \
	run-minimal run-all run-single run-multi clean

KPSE_K ?= 2
HSCD_TOP_K ?= 10
HSCD_DIRECTION ?= increase
MEMORY_KB ?= 2048
SEED ?= 1
VIRTUAL_BITMAP_BITS ?= 5000

export PERIOD_INPUTS KPSE_K HSCD_TOP_K HSCD_DIRECTION MEMORY_KB SEED
export VIRTUAL_BITMAP_BITS SINGLE_INPUT SSD_THRESHOLD

all: make-single make-multi

make-single: $(TARGET)

make-multi: $(MULTIPERIOD_TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@

$(MULTIPERIOD_TARGET): $(MULTIPERIOD_SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MULTIPERIOD_SOURCES) -o $@

test: test-all

test-all: test-single test-multi
	bash ./tests/make_targets_test.sh

test-single: make-single
	./tests/cli_test.sh
	./tests/sanitizer_test.sh single

test-multi: make-multi
	bash ./tests/multiperiod_cli_test.sh
	./tests/sanitizer_test.sh multi

run-minimal: make-single
	./scripts/run_minimal.sh

run-single: make-single
	bash ./scripts/run_single.sh

run-multi:
	bash ./scripts/validate_period_inputs.sh
	$(MAKE) make-multi
	bash ./scripts/run_multi.sh

run-all:
	bash ./scripts/validate_period_inputs.sh
	$(MAKE) all
	bash ./scripts/run_all.sh

clean:
	rm -rf build

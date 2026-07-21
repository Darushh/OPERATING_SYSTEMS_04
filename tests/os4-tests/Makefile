# Test suite for HW4 (OS 234123).
#
#   make            build and run the tests for parts 1, 2 and 3
#   make 2          run only the malloc_2 tests
#   make 3 FILTER=srealloc      run only the malloc_3 tests whose name contains "srealloc"
#   make 3 ARGS=--list          list the test names
#   make clean
#
# The implementations are found automatically: ../hw4, then ../os-hw4, then this
# directory. Override it if your malloc_*.cpp live somewhere else:
#
#   make SRC_DIR=/path/to/your/code
#
# Run this on the Ubuntu VM: the tests use sbrk(), mmap() and /proc.

CXX      ?= g++
CXXFLAGS ?= -g -Wall -Wextra -Wno-unused-result -Wno-unused-parameter
SRC_DIR  ?= $(or $(patsubst %/,%,$(dir $(firstword \
              $(wildcard ../hw4/malloc_1.cpp ../os-hw4/malloc_1.cpp malloc_1.cpp)))),../os-hw4)
BIN_DIR  := bin

HEADERS  := os_malloc.h test_utils.h stats_utils.h basic_model.h buddy_model.h malloc3_tests.h

.PHONY: all 1 2 3 4 clean help
.DELETE_ON_ERROR:

all: 1 2 3

help:
	@echo "make [1|2|3|4]            build and run one part (default: 1, 2 and 3)"
	@echo "make 3 FILTER=srealloc    run only the tests whose name contains 'srealloc'"
	@echo "make 3 ARGS=--list        list the test names"
	@echo "make 3 ARGS=--no-fork     run in-process, for gdb"
	@echo "make SRC_DIR=<dir>        where malloc_1.cpp .. malloc_4.cpp live (default ../hw4)"

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

define build_and_run
$(BIN_DIR)/test$(1): test_malloc_$(1).cpp $(SRC_DIR)/malloc_$(1).cpp $(HEADERS) | $(BIN_DIR)
	@echo "compiling $(SRC_DIR)/malloc_$(1).cpp"
	@$(CXX) $(CXXFLAGS) $(SRC_DIR)/malloc_$(1).cpp test_malloc_$(1).cpp -o $$@

$(1): $(BIN_DIR)/test$(1)
	@./$(BIN_DIR)/test$(1) $(FILTER) $(ARGS)
endef

$(foreach part,1 2 3 4,$(eval $(call build_and_run,$(part))))

clean:
	rm -rf $(BIN_DIR)

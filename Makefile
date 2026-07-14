# Makefile for Testing and Submitting HW4
# OS Course - Technion

# Compiler settings
CXX = g++
CXXFLAGS = -Wno-unused-result

# Test binaries
TEST1_BIN = test1
TEST2_BIN = test2
TEST3_BIN = test3
TEST4_BIN = test4

# Source files
MALLOC1_SRC = malloc_1.cpp
MALLOC2_SRC = malloc_2.cpp
MALLOC3_SRC = malloc_3.cpp
MALLOC4_SRC = malloc_4.cpp
SUBMITTERS = submitters.txt

# Test source files
TEST1_SRC = test_malloc_1.cpp
TEST2_SRC = test_malloc_2.cpp
TEST3_SRC = test_malloc_3.cpp
TEST4_SRC = test_malloc_4.cpp

# Header file
HEADER = os_malloc.h

.PHONY: all clean check-os submit help test1 test2 test3 test4

# Default target
all: test1 test2 test3

help:
	@echo "========================================="
	@echo "Malloc Testing and Submission Makefile"
	@echo "========================================="
	@echo ""
	@echo "Available targets:"
	@echo "  make test1    - Test malloc_1 implementation"
	@echo "  make test2    - Test malloc_2 implementation"
	@echo "  make test3    - Test malloc_3 implementation"
	@echo "  make test4    - Test malloc_4 implementation (optional)"
	@echo "  make all      - Run tests 1, 2, and 3"
	@echo "  make submit   - Create submission zip file"
	@echo "  make clean    - Remove compiled binaries and zip files"
	@echo "  make check-os - Check OS compatibility"
	@echo ""
	@echo "To prepare submission:"
	@echo "  1. Create submitters.txt with format: <Name> <Email> <ID> (two lines)"
	@echo "  2. Run 'make submit' to create the zip file"
	@echo ""

# OS compatibility check
check-os:
	@if [ -f /etc/os-release ]; then \
		. /etc/os-release; \
		if [ "$$VERSION_ID" = "18.04" ]; then \
			echo "✓ Ubuntu 18.04"; \
		else \
			echo "⚠ Not Ubuntu 18.04 (grading uses Ubuntu 18.04)"; \
		fi \
	else \
		echo "⚠ Cannot detect OS (grading uses Ubuntu 18.04)"; \
	fi

# Test malloc_1
test1: check-os
	@if [ ! -f $(MALLOC1_SRC) ]; then \
		echo "ERROR: $(MALLOC1_SRC) not found!"; \
		echo "Please create your malloc_1 implementation first."; \
		exit 1; \
	fi
	@echo "Compiling $(MALLOC1_SRC)..."
	$(CXX) $(MALLOC1_SRC) $(TEST1_SRC) $(CXXFLAGS) -o $(TEST1_BIN)
	@echo "Running malloc_1 tests..."
	@./$(TEST1_BIN)

# Test malloc_2
test2: check-os
	@if [ ! -f $(MALLOC2_SRC) ]; then \
		echo "ERROR: $(MALLOC2_SRC) not found!"; \
		echo "Please create your malloc_2 implementation first."; \
		exit 1; \
	fi
	@echo "Compiling $(MALLOC2_SRC)..."
	$(CXX) $(MALLOC2_SRC) $(TEST2_SRC) $(CXXFLAGS) -o $(TEST2_BIN)
	@echo "Running malloc_2 tests..."
	@./$(TEST2_BIN)

# Test malloc_3
test3: check-os
	@if [ ! -f $(MALLOC3_SRC) ]; then \
		echo "ERROR: $(MALLOC3_SRC) not found!"; \
		echo "Please create your malloc_3 implementation first."; \
		exit 1; \
	fi
	@echo "Compiling $(MALLOC3_SRC)..."
	$(CXX) $(MALLOC3_SRC) $(TEST3_SRC) $(CXXFLAGS) -o $(TEST3_BIN)
	@echo "Running malloc_3 tests..."
	@./$(TEST3_BIN)

# Test malloc_4 (optional)
test4: check-os
	@if [ ! -f $(MALLOC4_SRC) ]; then \
		echo "NOTE: $(MALLOC4_SRC) not found (this is optional)."; \
		exit 0; \
	fi
	@echo "Compiling $(MALLOC4_SRC)..."
	$(CXX) $(MALLOC4_SRC) $(TEST4_SRC) $(CXXFLAGS) -o $(TEST4_BIN)
	@echo "Running malloc_4 tests..."
	@./$(TEST4_BIN)

# Create submission zip
submit:
	@echo "========================================="
	@echo "Creating Submission Package"
	@echo "========================================="
	@if [ ! -f $(SUBMITTERS) ]; then \
		echo "ERROR: $(SUBMITTERS) not found!"; \
		echo ""; \
		echo "Please create $(SUBMITTERS) with the following format:"; \
		echo "  <Name> <Email> <ID>"; \
		echo "  <Name> <Email> <ID>"; \
		echo ""; \
		echo "Example:"; \
		echo "  Linus Torvalds linus@gmail.com 234567890"; \
		echo "  Ken Thompson ken@belllabs.com 345678901"; \
		exit 1; \
	fi
	@ID1=$$(sed -n '1p' $(SUBMITTERS) | awk '{print $$NF}' | tr -d ' \t\r\n'); \
	ID2=$$(sed -n '2p' $(SUBMITTERS) | awk '{print $$NF}' | tr -d ' \t\r\n'); \
	if [ -z "$$ID1" ] || [ -z "$$ID2" ]; then \
		echo "ERROR: $(SUBMITTERS) must contain exactly 2 student IDs!"; \
		exit 1; \
	fi; \
	ZIPNAME="$$ID1-$$ID2.zip"; \
	echo "Student IDs: $$ID1, $$ID2"; \
	echo "Creating: $$ZIPNAME"; \
	echo ""; \
	FILES_TO_ZIP="$(SUBMITTERS)"; \
	if [ -f $(MALLOC1_SRC) ]; then \
		FILES_TO_ZIP="$$FILES_TO_ZIP $(MALLOC1_SRC)"; \
		echo "  ✓ Including $(MALLOC1_SRC)"; \
	else \
		echo "  ⚠ WARNING: $(MALLOC1_SRC) not found"; \
	fi; \
	if [ -f $(MALLOC2_SRC) ]; then \
		FILES_TO_ZIP="$$FILES_TO_ZIP $(MALLOC2_SRC)"; \
		echo "  ✓ Including $(MALLOC2_SRC)"; \
	else \
		echo "  ⚠ WARNING: $(MALLOC2_SRC) not found"; \
	fi; \
	if [ -f $(MALLOC3_SRC) ]; then \
		FILES_TO_ZIP="$$FILES_TO_ZIP $(MALLOC3_SRC)"; \
		echo "  ✓ Including $(MALLOC3_SRC)"; \
	else \
		echo "  ⚠ WARNING: $(MALLOC3_SRC) not found"; \
	fi; \
	if [ -f $(MALLOC4_SRC) ]; then \
		FILES_TO_ZIP="$$FILES_TO_ZIP $(MALLOC4_SRC)"; \
		echo "  ✓ Including $(MALLOC4_SRC) (bonus)"; \
	else \
		echo "  ℹ $(MALLOC4_SRC) not found (optional - no bonus)"; \
	fi; \
	echo ""; \
	zip -q $$ZIPNAME $$FILES_TO_ZIP; \
	echo "=========================================" ; \
	echo "✓ Submission created: $$ZIPNAME"; \
	echo "=========================================" ; \
	echo ""; \
	echo "Please verify the contents:"; \
	unzip -l $$ZIPNAME

# Clean build artifacts
clean:
	@echo "Cleaning up..."
	rm -f $(TEST1_BIN) $(TEST2_BIN) $(TEST3_BIN) $(TEST4_BIN)
	rm -f *.zip
	@echo "Done."

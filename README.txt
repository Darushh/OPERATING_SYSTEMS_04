========================================
Assignment Checker - HW4
OS Course - Technion
========================================

OVERVIEW
--------
Basic sanity tests for your malloc implementation. These tests verify basic
functionality only - passing them does NOT guarantee full marks.

INSTALLATION
------------
Copy all files from this directory to your working directory with your malloc
implementations.

CREATING submitters.txt
------------------------
Create a file named "submitters.txt" with two lines containing:
<Name> <Email> <ID>

Example:
Linus Torvalds linus@gmail.com 234567890
Ken Thompson ken@belllabs.com 345678901

(Replace with your actual information)

USAGE
-----
make test1    - Test malloc_1
make test2    - Test malloc_2
make test3    - Test malloc_3
make test4    - Test malloc_4 (optional)
make submit   - Create submission zip
make clean    - Remove binaries

WHAT IS TESTED
--------------
malloc_1: Only smalloc() required
  - Basic allocation, arrays, rejection of large/zero sizes

malloc_2: All functions + statistics required
  - smalloc(), scalloc(), sfree(), srealloc()
  - _num_free_blocks(), _num_free_bytes()
  - _num_allocated_blocks(), _num_allocated_bytes()
  - _num_meta_data_bytes(), _size_meta_data()

malloc_3: Part 2 + buddy allocator features
  - Alignment, merging, splitting, mmap for large allocations

malloc_4: Optional bonus

OS COMPATIBILITY
----------------
Grading uses Ubuntu 18.04. The Makefile will warn if you're on a different OS.

SUBMISSION
----------
1. Create submitters.txt with format: <Name> <Email> <ID> (two lines)
2. Run: make submit
3. Upload the generated <id1>-<id2>.zip file

The zip will contain: malloc_1.cpp, malloc_2.cpp, malloc_3.cpp, malloc_4.cpp
(if exists), and submitters.txt.

IMPORTANT
---------
These are BASIC tests only. Your code will be graded with more comprehensive
tests. Use these to catch obvious errors and verify submission format.

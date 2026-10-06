# CppUTest without cmake: build the framework as a static lib once, then link tests (C code compiled as C, tests as C++).
V        := ../_vendor/cpputest
CXX      ?= c++
CC       ?= cc
CPPFLAGS := -I$(V)/include -I../fw
CXXFLAGS := -std=c++14 -g -Wall
LIBSRC   := $(wildcard $(V)/src/CppUTest/*.cpp) \
            $(filter-out %/GTest.cpp,$(wildcard $(V)/src/CppUTestExt/*.cpp)) \
            $(V)/src/Platforms/Gcc/UtestPlatform.cpp
LIBOBJ   := $(patsubst $(V)/src/%.cpp,build/lib/%.o,$(LIBSRC))

.PHONY: test clean
test: build/tests
	./build/tests -v

build/lib/%.o: $(V)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/libCppUTest.a: $(LIBOBJ)
	ar rcs $@ $^

build/%.o: ../fw/%.c
	@mkdir -p build
	$(CC) -std=c11 -g -Wall -I../fw -c $< -o $@

build/tests: AllTests.cpp test_rb.cpp test_sensor.cpp build/rb.o build/sensor.o build/libCppUTest.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build

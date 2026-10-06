# GoogleTest/gMock without cmake: the "-all" translation units build the whole framework in two files.
V        := ../_vendor/googletest
CXX      ?= c++
CC       ?= cc
CPPFLAGS := -I$(V)/googletest/include -I$(V)/googletest -I$(V)/googlemock/include -I$(V)/googlemock -I../fw
CXXFLAGS := -std=c++17 -g -Wall -Wextra -pthread

.PHONY: test clean
test: build/tests
	./build/tests

build/gtest-all.o: $(V)/googletest/src/gtest-all.cc
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/gmock-all.o: $(V)/googlemock/src/gmock-all.cc
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/gmock_main.o: $(V)/googlemock/src/gmock_main.cc
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/%.o: ../fw/%.c
	@mkdir -p build
	$(CC) -std=c11 -g -Wall -I../fw -c $< -o $@

build/tests: test_rb.cc test_sensor.cc build/rb.o build/sensor.o build/gtest-all.o build/gmock-all.o build/gmock_main.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build

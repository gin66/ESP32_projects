CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wconversion -Werror -I../../include

test_power_axis: test_power_axis.cpp ../../src/haus_power_axis.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f test_power_axis

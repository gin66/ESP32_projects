CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -I../../include

test_water_model: test_water_model.cpp ../../src/haus_water_model.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f test_water_model

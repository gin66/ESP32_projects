CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wconversion -Wshadow -I../../include

test_heater_control: test_heater_control.cpp ../../src/haus_water_model.cpp ../../src/haus_heater.cpp ../../src/haus_heater_bin.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f test_heater_control

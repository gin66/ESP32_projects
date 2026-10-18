# Host checks for the quartz-timed ADC bins.

GCC ?= $(shell for c in gcc-16 gcc-15 gcc-14 gcc-13 gcc-12 gcc; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)
GXX ?= $(shell for c in g++-16 g++-15 g++-14 g++-13 g++-12 g++; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)

CXXFLAGS := -std=c++14 -Wall -Wextra -Wconversion -Wshadow -Werror -g -I../../src

BIN  := test_adc_wave
SRCS := test_adc_wave.cpp ../../src/adc_wave.cpp

.PHONY: test clean

test: $(BIN)
	./$(BIN)

$(BIN): $(SRCS) ../../src/adc_wave.h
	$(GXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(BIN)

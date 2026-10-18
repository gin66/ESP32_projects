# Host tests for the 20-bin ADC waveform demodulator (S29.01).
#
# No hardware, no Arduino: src/haus_wave_demod.cpp is the same source the
# device compiles, fed with synthetic sines and captured 0x2C waveforms.

GCC ?= $(shell for c in gcc-16 gcc-15 gcc-14 gcc-13 gcc-12 gcc; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)
GXX ?= $(shell for c in g++-16 g++-15 g++-14 g++-13 g++-12 g++; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)

CXXFLAGS := -std=c++17 -Wall -Wextra -Wconversion -Wshadow -Werror -g \
            -I../../include
LDLIBS   := -lm

BIN  := test_wave_demod
SRCS := ../../src/haus_wave_demod.cpp test_wave_demod.cpp
OBJS := haus_wave_demod.o test_wave_demod.o
HDRS := ../../include/haus_wave_demod.h

.PHONY: test sanitize compiler clean

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	$(GXX) -o $@ $(OBJS) $(LDLIBS)

haus_wave_demod.o: ../../src/haus_wave_demod.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

test_wave_demod.o: test_wave_demod.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

sanitize:
	$(GXX) $(CXXFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $(BIN)_san $(SRCS) $(LDLIBS)
	./$(BIN)_san

compiler:
	@$(GCC) --version | head -1
	@$(GXX) --version | head -1

clean:
	rm -f $(BIN) $(BIN)_san *.o
	rm -rf *.dSYM

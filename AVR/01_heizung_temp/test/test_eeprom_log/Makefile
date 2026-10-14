# Host tests for the EEPROM restore-point ring.
#
# The algorithm is pure C with no hardware dependency, so it is compiled here for
# the host against a fault-injecting EEPROM mock. The same src/eeprom_log.cpp is
# what the ATmega328p build uses — this is not a reimplementation.
#
# Why host and not simavr: the failure this log must survive is power loss part
# way through a record write, at every byte offset, thousands of times. simavr
# builds a whole firmware image and traces it; it cannot inject a supply collapse
# mid-EEPROM-cycle. Here the injection is one field assignment.
#
# Why an explicit gcc: /usr/bin/gcc on macOS is the Apple clang shim, which
# emits a .dSYM bundle (and the Info.plist inside it) for -g builds. A real GCC
# from Homebrew keeps DWARF in the object file and produces nothing extra.
#
# -Wconversion is on deliberately: it is what stops an `int` sneaking in. `int` is
# 16-bit on the ATmega and 32-bit here, so a stray one would make the harness
# disagree with the firmware for reasons unrelated to the algorithm.

GCC ?= $(shell for c in gcc-16 gcc-15 gcc-14 gcc-13 gcc-12 gcc; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)
GXX ?= $(shell for c in g++-16 g++-15 g++-14 g++-13 g++-12 g++; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)

CXXFLAGS := -std=c++14 -Wall -Wextra -Wconversion -Wshadow -Werror -g -I../../src
LDLIBS   := -lm

BIN  := test_eeprom_log
SRCS := ../../src/eeprom_log.cpp eeprom_mock.cpp test_eeprom_log.cpp
OBJS := eeprom_log.o eeprom_mock.o test_eeprom_log.o
HDRS := eeprom_mock.h ../../src/eeprom_log.h

.PHONY: test sanitize compiler clean

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	$(GXX) -o $@ $(OBJS) $(LDLIBS)

eeprom_log.o: ../../src/eeprom_log.cpp ../../src/eeprom_log.h
	$(GXX) -c $(CXXFLAGS) -o $@ $<

eeprom_mock.o: eeprom_mock.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

test_eeprom_log.o: test_eeprom_log.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

# Same tests, sanitizers on. The randomised block does a lot of index arithmetic;
# this is the cheap way to prove none of it runs off the end.
sanitize:
	$(GXX) $(CXXFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $(BIN)_san $(SRCS)
	./$(BIN)_san

compiler:
	@$(GCC) --version | head -1
	@$(GXX) --version | head -1

clean:
	rm -f $(BIN) $(BIN)_san *.o
	rm -rf *.dSYM
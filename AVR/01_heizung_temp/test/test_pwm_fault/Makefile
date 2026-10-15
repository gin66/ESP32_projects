# Host tests for the PWM output-path fault escalation.
#
# Same reasoning as test_eeprom_log: the escalation ends in a watchdog reset and
# its third step takes the pin over permanently, neither of which can be observed
# on a bench without breaking hardware. So the decision logic lives in a pure
# module and is compiled here for the host.
#
# Prefer a real GCC: /usr/bin/gcc on macOS is the Apple clang shim, which emits a
# .dSYM bundle for -g builds. Real GCC keeps DWARF in the .o and nothing extra.
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

BIN  := test_pwm_fault
SRCS := test_pwm_fault.cpp ../../src/pwm_fault.cpp ../../src/eeprom_log.cpp
OBJS := test_pwm_fault.o pwm_fault.o eeprom_log.o
HDRS := ../../src/pwm_fault.h ../../src/eeprom_log.h

.PHONY: test sanitize compiler clean

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	$(GXX) -o $@ $(OBJS) $(LDLIBS)

test_pwm_fault.o: test_pwm_fault.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

pwm_fault.o: ../../src/pwm_fault.cpp ../../src/pwm_fault.h
	$(GXX) -c $(CXXFLAGS) -o $@ $<

eeprom_log.o: ../../src/eeprom_log.cpp ../../src/eeprom_log.h
	$(GXX) -c $(CXXFLAGS) -o $@ $<

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
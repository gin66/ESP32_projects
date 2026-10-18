# Host tests for the history store, SD rollover and SPIFFS ring.
#
# The algorithm is compiled for the host against an in-memory SD/SPIFFS layer.
# The same src/haus_history.cpp is what the ESP32 build uses — this is not a
# reimplementation.
#
# What is covered:
#   A  a simulated day: 1 440 minute samples, 24 hourly columns
#   B  midnight rollover: exactly two SD writes, day path from the last sample
#   C  stale marking: one minute without refresh, excluded from L2
#   D  SPIFFS ring order and the newest-column read
#   E  ring wrap: the 8 760-hour index overwrites the oldest slot

GCC ?= $(shell for c in gcc-16 gcc-15 gcc-14 gcc-13 gcc-12 gcc; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)
GXX ?= $(shell for c in g++-16 g++-15 g++-14 g++-13 g++-12 g++; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)

CXXFLAGS := -std=c++17 -Wall -Wextra -Wconversion -Wshadow -Werror -g \
            -DHAUS_HOST -I../../include -I../../src
LDLIBS   :=

BIN  := test_history
SRCS := ../../src/haus_history.cpp test_history.cpp
OBJS := haus_history.o test_history.o
HDRS := ../../include/haus_history.h ../../include/haus_gui.h \
        ../../include/haus_rollup.h

.PHONY: test sanitize compiler clean

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	$(GXX) -o $@ $(OBJS) $(LDLIBS)

haus_history.o: ../../src/haus_history.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

test_history.o: test_history.cpp $(HDRS)
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

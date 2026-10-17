# Host tests for the pure rank → ROM → function resolver (S9).
#
# No hardware, no Arduino: haus_sensor_map.cpp is the same source the device
# compiles, fed with virtual identity frames and 0x200 slots.

GCC ?= $(shell for c in gcc-16 gcc-15 gcc-14 gcc-13 gcc-12 gcc; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)
GXX ?= $(shell for c in g++-16 g++-15 g++-14 g++-13 g++-12 g++; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)

CXXFLAGS := -std=c++17 -Wall -Wextra -Wconversion -Wshadow -Werror -g \
            -I../../include
LDLIBS   :=

BIN  := test_sensor_map
SRCS := ../../src/haus_sensor_map.cpp test_sensor_map.cpp
OBJS := haus_sensor_map.o test_sensor_map.o
HDRS := ../../include/haus_sensor_map.h ../../include/haus_gui.h

.PHONY: test sanitize compiler clean

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	$(GXX) -o $@ $(OBJS) $(LDLIBS)

haus_sensor_map.o: ../../src/haus_sensor_map.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

test_sensor_map.o: test_sensor_map.cpp $(HDRS)
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

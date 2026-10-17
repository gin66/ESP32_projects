# Host tests for the streaming chart downsampler.
#
# The same src/haus_chart.cpp the ESP32 build uses is compiled here. The
# builder's working buffers are static; this test checks the 512-record and
# 64-column chunk limits, sentinel/stale exclusion, and GAP marking.
#
# -Wconversion is on deliberately: the chart does index arithmetic and narrows
# min/max to int16_t.

GCC ?= $(shell for c in gcc-16 gcc-15 gcc-14 gcc-13 gcc-12 gcc; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)
GXX ?= $(shell for c in g++-16 g++-15 g++-14 g++-13 g++-12 g++; do \
                 command -v $$c >/dev/null 2>&1 && echo $$c && break; done)

CXXFLAGS := -std=c++17 -Wall -Wextra -Wconversion -Wshadow -Werror -g \
            -I../../include
LDLIBS   :=

BIN  := test_chart
SRCS := ../../src/haus_chart.cpp test_chart.cpp
OBJS := haus_chart.o test_chart.o
HDRS := ../../include/haus_chart.h ../../include/haus_gui.h \
        ../../include/haus_rollup.h

.PHONY: test sanitize compiler clean

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	$(GXX) -o $@ $(OBJS) $(LDLIBS)

haus_chart.o: ../../src/haus_chart.cpp $(HDRS)
	$(GXX) -c $(CXXFLAGS) -o $@ $<

test_chart.o: test_chart.cpp $(HDRS)
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

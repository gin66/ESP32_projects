CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I. -I../../include -I../../lib/font5x7 -I../../lib

SRCS = main.cpp recorder.cpp analyzer.cpp ../../src/led_effects.cpp
OBJS = $(subst .cpp,.o,$(SRCS))

TARGET = test_harness

.PHONY: all clean run list

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

main.o: main.cpp mock_platform.h recorder.h analyzer.h test_scenarios.h ../../include/led_effects.h ../../include/led_config.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

recorder.o: recorder.cpp recorder.h ../../include/led_config.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

analyzer.o: analyzer.cpp analyzer.h recorder.h ../../include/led_config.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

../../src/led_effects.o: ../../src/led_effects.cpp ../../include/led_effects.h ../../include/led_config.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o ../../src/led_effects.o

run: $(TARGET)
	./$(TARGET)

list: $(TARGET)
	./$(TARGET) --list

test-trapezoid: $(TARGET)
	./$(TARGET) --scenario 8 --viz --frames 0-20

test-rings: $(TARGET)
	./$(TARGET) --scenario 11 --viz --frames 0-20

test-wave: $(TARGET)
	./$(TARGET) --scenario 14 --viz --frames 0-20

test-all-bg: $(TARGET)
	./$(TARGET) --scenario 7 --viz --frames 0-10
	./$(TARGET) --scenario 8 --viz --frames 0-10
	./$(TARGET) --scenario 11 --viz --frames 0-10
	./$(TARGET) --scenario 14 --viz --frames 0-10

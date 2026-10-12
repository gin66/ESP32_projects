CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I. -I../../include

SRCS = main.cpp recorder.cpp analyzer.cpp ../../src/led_effects.cpp
OBJS = $(subst .cpp,.o,$(SRCS))

TARGET = test_harness

.PHONY: all clean run

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

viz_demo: viz_demo.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) viz_demo *.o ../../src/led_effects.o

run: $(TARGET)
	./$(TARGET)

run-viz: $(TARGET)
	./$(TARGET) --viz --threshold 20 --scenario 19

list:
	./$(TARGET) --list

test-one: $(TARGET)
	./$(TARGET) --scenario 19 --viz --threshold 15

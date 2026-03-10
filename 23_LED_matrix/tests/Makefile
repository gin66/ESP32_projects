CC = g++
CFLAGS = -std=c++11 -Wall -I. -I../include -I../src -lm

TARGET = test_xy_index
SINE_TARGET = test_sine

all: $(TARGET) $(SINE_TARGET)

$(TARGET): test_xy_index.cpp ../src/led_effects.cpp leds.cpp unity.h leds.h
	$(CC) $(CFLAGS) -o $(TARGET) test_xy_index.cpp ../src/led_effects.cpp leds.cpp

$(SINE_TARGET): test_sine.cpp unity.h
	$(CC) $(CFLAGS) -o $(SINE_TARGET) test_sine.cpp

run: $(TARGET) $(SINE_TARGET)
	@echo "Running xy index tests..."
	@./$(TARGET)
	@echo ""
	@echo "Running sine calculation tests..."
	@./$(SINE_TARGET)

run-xy: $(TARGET)
	./$(TARGET)

run-sine: $(SINE_TARGET)
	./$(SINE_TARGET)

clean:
	rm -f $(TARGET) $(SINE_TARGET)

.PHONY: all run run-xy run-sine clean

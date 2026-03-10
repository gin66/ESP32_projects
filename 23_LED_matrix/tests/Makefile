CC = g++
CFLAGS = -std=c++11 -Wall -I. -I../include -I../src

TARGET = test_xy_index

all: $(TARGET)

$(TARGET): test_xy_index.cpp ../src/led_effects.cpp leds.cpp unity.h leds.h
	$(CC) $(CFLAGS) -o $(TARGET) test_xy_index.cpp ../src/led_effects.cpp leds.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean

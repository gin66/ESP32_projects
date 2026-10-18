.PHONY: all build run test clean

all: run

build:
	cmake -B build -G Ninja 2>&1 && cmake --build build

run: build
	./build/hauszentrale_host

test:
	cmake -B build -G Ninja 2>&1 && cmake --build build --target sml_power_test
	./build/sml_power_test

clean:
	rm -rf build *.png

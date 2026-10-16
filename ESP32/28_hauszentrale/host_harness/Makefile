.PHONY: all build run clean

all: build

build:
	cmake -B build -G Ninja 2>&1 && cmake --build build

run: build
	./build/hauszentrale_host

clean:
	rm -rf build

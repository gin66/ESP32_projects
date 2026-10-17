.PHONY: all build run clean

all: run

build:
	cmake -B build -G Ninja 2>&1 && cmake --build build

run: build
	rm *.png
	./build/hauszentrale_host

clean:
	rm -rf build *.png

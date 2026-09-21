.PHONY: all build test clean

all: clean build test

build:
	gcc -o server main.c -pthread

test:
	./server

clean:
	rm server -f

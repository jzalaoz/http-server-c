.PHONY: all build test clean

all: clean build test

build:
	gcc -o main main.c -pthread

test:
	./main ./public

clean:
	rm main -f

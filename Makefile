CC=gcc
CFLAGS=-Wall -Werror -pedantic -ggdb -std=c11 -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -pg

cpu: main.c
	$(CC) $(CFLAGS) -o T800-64 main.c cpu.c bytecode.c syscall.c

test: tests.c
	$(CC) $(CFLAGS) -o tests tests.c cpu.c bytecode.c syscall.c

all: test cpu

run: cpu
	./T800-64

clean:
	@rm -rf *.o
	@rm -rf T800-64
	@rm -rf tests

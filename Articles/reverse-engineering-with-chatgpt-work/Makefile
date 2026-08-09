CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all clean check

all: recovered-ls

recovered-ls: recovered_ls.c
	$(CC) $(CFLAGS) -o $@ $<

check: recovered-ls
	./recovered-ls -1 .
	./recovered-ls -la recovered_ls.c

clean:
	rm -f recovered-ls

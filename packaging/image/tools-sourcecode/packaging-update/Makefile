CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99 -D_FILE_OFFSET_BITS=64

.PHONY: all clean

all: packaging-update

packaging-update: packaging.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f packaging-update *.o

.PHONY: all clean

all: packaging-raw

packaging-raw: packaging.c
	$(CC) $(CFLAGS) -Wall -Wextra -Werror -O2 -o $@ $<

clean:
	$(RM) packaging-raw

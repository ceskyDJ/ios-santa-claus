# Project: IOS - 2nd project
# Author:  Michal Šmahel (xsmahe01)
# Date:    April-May 2021
#
# Usage:
#   - compile:             make
#   - pack to archive:     make pack
#   - clean:               make clean

CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic

.PHONY: all pack clean

# make
all: proj2

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

## ## ##
## ## ##

# Compiling programs composited of multiple modules
proj2: proj2.c
	$(CC) proj2.c -o proj2

# make pack
pack:
	zip proj2.zip *.c *.h Makefile

# make clean
clean:
	rm -f proj2 *.o
SOURCES := $(wildcard *.c libls/*.c)
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
PROGRAM := main

CFLAGS := -std=c99 -Wall -Wextra -O2
CPPFLAGS := -D_POSIX_C_SOURCE=200809L
LDLIBS := -lm

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJECTS) $(PROGRAM)

.PHONY: all clean

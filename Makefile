HEADERS := $(wildcard *.h libls/*.h)
SOURCES := $(wildcard *.c libls/*.c)
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
PROGRAM := main

CFLAGS := -std=c99 -pedantic -Wall -Wextra -O2
CPPFLAGS := -D_POSIX_C_SOURCE=200809L
LDLIBS := -lm -lreadline

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJECTS) $(PROGRAM)

.PHONY: all clean

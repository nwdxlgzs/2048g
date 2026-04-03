CC = gcc
CFLAGS = -O3 -Wall -Wextra -std=c99
LDFLAGS = -lm

ifdef COMSPEC
    # Windows (MinGW)
    CFLAGS += -D_GNU_SOURCE=1
else
    # Unix-like
    CFLAGS += -fPIC -D_GNU_SOURCE
endif

OBJS = 2048g.o game.o platform.o
TARGET = 2048g

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
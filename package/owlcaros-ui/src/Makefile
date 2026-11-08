CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

TARGET = owlcaros-ui
OBJS = main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

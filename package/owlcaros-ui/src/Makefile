CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=
SDL_CFLAGS = $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf)
SDL_LIBS = $(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf)

TARGET = owlcaros-ui
OBJS = main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(SDL_LIBS)

main.o: main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

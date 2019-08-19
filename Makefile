CC ?= gcc
LD ?= gcc
CFLAGS := ${CFLAGS}
CFLAGS += -march=native -O2 -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wpedantic
CFLAGS += $(shell pkg-config --cflags freetype2 cairo libpng zlib)
LDLIBS := $(shell pkg-config --libs freetype2 cairo libpng zlib) -lm

font-thumbnail: font-thumbnail.o asprintf/asprintf.o

.PHONY: clean
clean:
	-rm font-thumbnail *.o asprintf/*.o

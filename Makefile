CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0 gmodule-2.0`
LIBS = `pkg-config --libs gtk+-3.0 gmodule-2.0`
DEBUG = -Wall -g

OBJECTS = gzmzone.o

.PHONY: clean

all: gzmzone

gzmzone: $(OBJECTS)
	$(CC) $(DEBUG) $(LIBS) $(OBJECTS) -o $@

gzmzone.o: gzmzone.c
	$(CC) $(DEBUG) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o gzmzone

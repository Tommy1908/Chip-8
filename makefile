CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Werror
LIBS = `sdl2-config --cflags --libs`
TARGET = chip8

SRCS = chip8.c config.c media.c


all: clean
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS) $(LIBS)

# Debug info
debug: clean
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS) $(LIBS) -DDEBUG

clean:
	rm -f $(TARGET)
	
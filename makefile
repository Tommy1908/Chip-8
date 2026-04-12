CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Werror
LIBS = `sdl2-config --cflags --libs`
TARGET = chip8


all: clean
	$(CC) chip8.c -o $(TARGET) $(CFLAGS) $(LIBS)

# Debug info
debug: clean
	$(CC) chip8.c -o $(TARGET) $(CFLAGS) $(LIBS) -DDEBUG

clean:
	rm -f $(TARGET)
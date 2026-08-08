CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -I.

SRC = \
	src/vted_terminal.c \
	src/vted.c

TARGET = vted

all:
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
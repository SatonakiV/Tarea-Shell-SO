CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11
OBJ = main.o shell.o parser.o

.PHONY: all clean

all: mishell

mishell: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c shell.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) mishell
CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11
SANFLAGS = -g -fsanitize=address,undefined
OBJ = main.o shell.o parser.o executor.o jobs.o pmon.o

.PHONY: all clean test

all: mishell

mishell: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c shell.h executor.h jobs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) mishell test_exec test_suicida

test: test_exec test_suicida
	./test_exec

test_exec: test_exec.c executor.c executor.h shell.h jobs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANFLAGS) -o $@ test_exec.c executor.c

test_suicida: test_suicida.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

CC=gcc -Wall -Wextra
EXE=notes

all: $(EXE)

$(EXE): main.c
	$(CC) $^ -o $@

clean:
	rm -rf $(EXE)

.PHONY: all clean

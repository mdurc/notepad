CC = gcc
CFLAGS = -Wall -Wextra -Werror
PROGRAM = notes

SRC_DIR = ./src
BUILD_DIR = ./build
SRC_LIST = $(wildcard $(SRC_DIR)/*.c)
OBJ_LIST = $(SRC_LIST:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

all: $(PROGRAM)

$(PROGRAM): $(OBJ_LIST)
	$(CC) $(CFLAGS) $(OBJ_LIST) -o $(PROGRAM)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(PROGRAM) err.txt

.PHONY: all clean

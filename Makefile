TARGET = my_shell
SRC_DIR = src
OBJ = $(SRC_DIR)/main.c $(SRC_DIR)/input_parser.c $(SRC_DIR)/helpers.c $(SRC_DIR)/builtins.c $(SRC_DIR)/executor.c $(SRC_DIR)/shortcuts.c $(SRC_DIR)/tools.c $(SRC_DIR)/extras.c $(SRC_DIR)/pipeline.c
CFLAGS = -Wall -Wextra -Werror
CC = gcc
LIBS = -ledit

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

clean:
	rm -f $(SRC_DIR)/*.o

fclean: clean
	rm -f $(TARGET)

re: fclean all

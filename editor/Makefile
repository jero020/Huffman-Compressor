CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -D_GNU_SOURCE -pthread
CPPFLAGS = -Isrc -I../include -MMD -MP
LDFLAGS = -pthread
SRC_DIR = src
OBJ_DIR = obj

# Modulos del compresor Huffman concurrente (raiz del repositorio)
HUF_DIR  = ../src
HUF_SRCS = $(HUF_DIR)/huffman.c $(HUF_DIR)/hf_concurrent.c $(HUF_DIR)/file_guard.c

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o) $(HUF_SRCS:$(HUF_DIR)/%.c=$(OBJ_DIR)/huf_%.o)
EXEC = editor

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/huf_%.o: $(HUF_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $@

# Pruebas del Parcial 1 (o/p/a/d/q) + pruebas de concurrencia del Parcial 2
test: $(EXEC)
	./pruebas_editor.sh
	./pruebas_concurrencia.sh

# Editor compilado con ThreadSanitizer corriendo las pruebas de concurrencia
tsan:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -O1 -g -fsanitize=thread" LDFLAGS="-pthread -fsanitize=thread"
	./pruebas_concurrencia.sh
	$(MAKE) clean

clean:
	rm -rf $(OBJ_DIR) $(EXEC)

-include $(OBJS:.o=.d)

.PHONY: all clean test tsan

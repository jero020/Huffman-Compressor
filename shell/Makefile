CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE
TARGET = sys_shell
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_editor.c cat_compresor.c
OBJS = $(SRCS:.c=.o)

# El editor EAFIT (con el compresor Huffman) es un binario independiente en
# ../editor, integrado al shell mediante fork()+execvp() (cat_editor.c).
all: $(TARGET) editor huffcli

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

editor:
	$(MAKE) -C ../editor

huffcli:
	$(MAKE) -C .. huffcli

# Prueba de la cadena completa: shell -> editor -> compresion en segundo plano
test: all
	./pruebas_shell.sh

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all editor huffcli test clean

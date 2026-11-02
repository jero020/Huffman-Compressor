CC      ?= gcc
CFLAGS  ?= -O2 -g
BASE    = -Wall -Wextra -std=c11 -D_GNU_SOURCE -pthread -Iinclude
LDFLAGS ?= -pthread

LIB_SRC = src/huffman.c src/hf_concurrent.c src/file_guard.c
LIB_HDR = include/huffman.h include/hf_concurrent.h include/file_guard.h

all: huffcli demo_editor

huffcli: $(LIB_SRC) src/huffcli.c $(LIB_HDR)
	$(CC) $(BASE) $(CFLAGS) -o $@ $(LIB_SRC) src/huffcli.c $(LDFLAGS)

demo_editor: $(LIB_SRC) src/demo_editor.c $(LIB_HDR)
	$(CC) $(BASE) $(CFLAGS) -o $@ $(LIB_SRC) src/demo_editor.c $(LDFLAGS)

# Pruebas de integridad (cmp + md5sum) y determinismo entre # de hilos
test: huffcli
	./tests/run_tests.sh ./huffcli

# ThreadSanitizer: detecta condiciones de carrera
tsan:
	$(MAKE) clean
	$(MAKE) huffcli CFLAGS="-O1 -g -fsanitize=thread"  LDFLAGS="-pthread -fsanitize=thread"
	./tests/run_tests.sh ./huffcli
	$(MAKE) clean

# AddressSanitizer + LeakSanitizer: memoria inválida y fugas
asan:
	$(MAKE) clean
	$(MAKE) huffcli CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-pthread -fsanitize=address,undefined"
	./tests/run_tests.sh ./huffcli
	$(MAKE) clean

# Valgrind (memcheck + helgrind)
valgrind: huffcli
	head -c 3000000 /dev/urandom > /tmp/hf_vg.bin
	valgrind --leak-check=full --error-exitcode=1 ./huffcli -c /tmp/hf_vg.bin /tmp/hf_vg.huf -t 4 -s 256
	valgrind --leak-check=full --error-exitcode=1 ./huffcli -d /tmp/hf_vg.huf /tmp/hf_vg.out -t 4
	cmp /tmp/hf_vg.bin /tmp/hf_vg.out && echo "valgrind: OK"
	valgrind --tool=helgrind --error-exitcode=1 ./huffcli -c /tmp/hf_vg.bin /tmp/hf_vg.huf -t 4 -s 256

clean:
	rm -f huffcli demo_editor *.huf *.tmp

.PHONY: all test tsan asan valgrind clean

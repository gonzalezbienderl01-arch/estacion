# Estacion de Telemetria ITLA
# Uso: make | make run | make clean

CC      = gcc
# -MMD -MP: make recompila un .c cuando cambia alguno de los .h que usa
CFLAGS  = -std=c11 -Wall -Wextra -O2 -MMD -MP
LDLIBS  = -lcurl -lpthread -ldl -lm

SRC     = src/main.c src/red.c src/datos.c src/comandos.c src/descarga.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))
SQLITE  = build/sqlite3.o

all: estacion

estacion: $(OBJ) $(SQLITE)
	$(CC) $(OBJ) $(SQLITE) -o $@ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -Ilib/sqlite -c $< -o $@

# SQLite se compila aparte (amalgamation), sin -Wall porque no es codigo nuestro
$(SQLITE): lib/sqlite/sqlite3.c | build
	$(CC) -O2 -c $< -o $@

lib/sqlite/sqlite3.c:
	@echo "ERROR: falta lib/sqlite/sqlite3.c (y sqlite3.h). Descarga la amalgamation desde https://www.sqlite.org/download.html"
	@exit 1

build:
	mkdir -p build

-include $(OBJ:.o=.d)

run: all
	./estacion config

clean:
	rm -rf build estacion

.PHONY: all run clean

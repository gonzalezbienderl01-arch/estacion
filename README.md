# Estación de Telemetría ITLA

Programa en C11 (`estacion`) que usa libcurl y SQLite para hablar con el servidor de planta del ITLA.

## Compilación

Requisitos: `gcc`, `make`, `libcurl` (con cabeceras) y la amalgamation de SQLite en `lib/sqlite/` (`sqlite3.c` y `sqlite3.h`).

    make        # compila con -std=c11 -Wall -Wextra
    make clean

## Uso

    export ITLA_HOST=10.0.0.200        # o itla-meca.duckdns.org desde casa
    export ITLA_MATRICULA=<tu matricula>
    export ITLA_TOKEN=<tu token>

    ./estacion config      # Nivel 0 (hecho)
    ./estacion descargar   # Nivel 1 (pendiente)
    ./estacion analizar    # Nivel 2 (pendiente)
    ./estacion enviar      # Niveles 3 y 4 (pendiente)
    ./estacion escuchar    # Nivel 5 (pendiente)

## Estructura

- `src/main.c`: lee `argv` y llama al subcomando.
- `src/comandos.c`: la lógica de cada subcomando.
- `src/red.c`: todo lo de libcurl (un solo easy handle reutilizado).
- `src/datos.c`: todo lo de SQLite (sentencias preparadas).

## Decisiones

- El token y el host se leen del entorno; nunca están en el código.
- Se revisan los dos niveles de error: `CURLcode` y código HTTP.
- SQL con datos variables siempre con sentencias preparadas (`?`).
- SQLite con `journal_mode=WAL` y `busy_timeout` de 2000 ms.

## Uso de IA

Se usó un asistente de IA (Claude) como apoyo para estructurar y explicar el código. Todo el código fue revisado y comprendido por el autor.

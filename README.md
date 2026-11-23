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
    ./estacion descargar   # Nivel 1 (hecho)
    ./estacion analizar    # Nivel 2 (hecho)
    ./estacion enviar      # Niveles 3 y 4 (pendiente)
    ./estacion escuchar    # Nivel 5 (pendiente)

## Estructura

- `src/main.c`: lee `argv`, busca el subcomando en una tabla y lo ejecuta.
- `src/comandos.c`: Nivel 0 (`config`).
- `src/descarga.c`: Nivel 1 (`descargar`).
- `src/analisis.c`: Nivel 2 (`analizar`).
- `src/red.c`: todo lo de libcurl (un solo easy handle reutilizado, GET y POST).
- `src/datos.c`: todo lo de SQLite (sentencias preparadas).
- `pruebas/servidor_falso.py`: servidor de práctica que corre solo en tu PC.

## Decisiones

- El token y el host se leen del entorno; nunca están en el código.
- Se revisan los dos niveles de error: `CURLcode` y código HTTP.
- SQL con datos variables siempre con sentencias preparadas (`?`).
- SQLite con `journal_mode=WAL` y `busy_timeout` de 2000 ms.
- **Descarga (Nivel 1):** se piden páginas de 200 filas con `desde_id` y se repite con el id más alto
  recibido hasta que una página trae menos de 200. Las filas repetidas las absorbe el `PRIMARY KEY`
  con `INSERT OR IGNORE`, y el resumen (`COUNT` y `SUM`) lo calcula SQL, no un bucle en C.
- **Una transacción por página:** la red se usa fuera de la transacción, así la base solo queda
  bloqueada el instante de guardar (`enviar` y `escuchar` usan la misma base).
- **CSV:** se parte con `strchr`, no con `strtok`, porque `strtok` se salta los campos vacíos.
  Un campo vacío se guarda como `NULL`, no como 0.
- Si el servidor responde 429 se espera 1 s y se reintenta (máximo 3 veces). Repetir `descargar`
  no duplica filas.

- **Análisis (Nivel 2):** una sola consulta SQL agrupa por máquina. Una subconsulta marca cada fila como
  válida o descartada (`estado` no es `ok`, `temp = -999.0` o algún campo `NULL`) y `MIN`, `MAX`, `AVG`
  y `COUNT` se calculan con SQL, sin bucles en C. `AVG` se redondea con `ROUND(..., 2)` y el umbral
  entra como parámetro (`?1`) leído de la tabla `config`.
- **JSON con `snprintf`:** el cuerpo se arma en un buffer fijo y cada `snprintf` comprueba el espacio
  restante. Cada envío al servidor gasta uno de los 10 intentos, así que primero se muestra la tabla
  en pantalla y se compara antes de volver a enviar.
- **Errores HTTP compartidos:** `respuesta_ok()` en `red.c` traduce 200/400/401/403/429 a mensajes
  claros y la usan `descargar` y `analizar`.

## Pruebas sin el servidor real

En una terminal:

    python3 pruebas/servidor_falso.py

En otra:

    export ITLA_HOST=127.0.0.1
    export ITLA_MATRICULA=<tu matricula>
    export ITLA_TOKEN=abcdefghijklmnopqrstuvwxyz012345   # token de PRUEBA, solo sirve con el servidor falso
    ./estacion config
    ./estacion descargar
    ./estacion analizar

Para revisar la memoria: `valgrind --leak-check=full ./estacion descargar`.

## Uso de IA

Se usó un asistente de IA (Claude) como apoyo para estructurar, escribir y explicar el código.
El autor revisó y comprende cada parte, y puede explicarla y modificarla en la defensa.

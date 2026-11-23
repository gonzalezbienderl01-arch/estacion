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
    ./estacion enviar      # Niveles 3 y 4 (hecho)
    ./estacion escuchar    # Nivel 5 (hecho)

## Estructura

- `src/main.c`: lee `argv`, busca el subcomando en una tabla y lo ejecuta.
- `src/comandos.c`: Nivel 0 (`config`).
- `src/descarga.c`: Nivel 1 (`descargar`).
- `src/analisis.c`: Nivel 2 (`analizar`).
- `src/envio.c`: Niveles 3 y 4 (`enviar`): bucle con cola de telemetria y latido MQTT.
- `src/sensor.c`: sensor simulado (temperatura 25-45 C, corriente 2-5 A).
- `src/escuchar.c`: Nivel 5 (`escuchar`): suscripción MQTT a las alarmas y confirmación HTTP.
- `src/senal.c`: Ctrl+C / SIGTERM (una bandera que revisan `enviar` y `escuchar`).
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

- **Telemetría resiliente (Nivel 3):** cada lectura se guarda primero en la tabla `telemetria`
  (`enviado = 0`) y después se intenta enviar. Solo se marca `enviado = 1` cuando el servidor
  responde 201 (creada) o 200 (duplicada). El `seq` sale de `MAX(seq) + 1` en SQLite, así que
  sobrevive a un reinicio y las pendientes de una corrida anterior se envían primero y en orden.
- **Un solo bucle, sin hilos:** el bucle lleva tres plazos (próxima lectura, próximo latido, próximo
  envío) con el reloj monotónico y duerme hasta el más cercano. Cada vuelta envía **una** lectura
  pendiente, así una cola larga no atrasa las lecturas ni los latidos.
- **503 y red caída:** con 503 se espera `Retry-After` (leído con `CURLINFO_RETRY_AFTER`) más 500 ms
  sin hacer peticiones, mientras las lecturas siguen guardándose. Si no hay conexión se espera
  1, 2, 4... hasta 30 s. Entre envíos de la cola hay 250 ms para no recibir 429.
- **Latido (Nivel 4):** `LATIDO;<seq>;<pendientes>;<ts>` por MQTT (QoS 0) con libcurl, usando un
  segundo easy handle (`mqtt://`, usuario = matrícula, clave = token). Si el broker no responde,
  el latido se pierde y no se reintenta, como pide el reto.
- **Ctrl+C:** un manejador de señales solo levanta una bandera; el bucle termina limpio y muestra
  cuántas lecturas quedaron pendientes.

- **Alarmas (Nivel 5):** la suscripción usa su propio easy handle MQTT y la confirmación
  (`POST /alarmas/ack`) va por el handle HTTP, nunca por el de la suscripción. `curl_easy_perform`
  no regresa mientras la conexión viva, así que cada mensaje llega a un callback y un bucle
  reconecta con espera creciente (1, 2, 4... hasta 30 s) si se corta.
- **Formato binario de MQTT:** cada mensaje llega como 2 bytes con el largo del tópico, el tópico y
  el contenido terminado en `\n`. El callback acumula los bytes en un buffer, procesa solo los
  mensajes completos y deja el resto para la siguiente llamada (varios mensajes pueden llegar
  pegados y uno puede llegar partido).
- **Sin duplicados:** `codigo` es `PRIMARY KEY`. Una alarma ya confirmada se ignora (el broker la
  vuelve a entregar al reconectar); una guardada pero sin confirmar se reintenta, también cada 2 s
  desde el callback de progreso, porque con la conexión viva el broker no la reenvía.
- **Ctrl+C:** `CURLOPT_NOPROGRESS` en 0 y un `CURLOPT_XFERINFOFUNCTION` que devuelve 1 cuando el
  manejador de señales levantó la bandera: así `perform` regresa y se cierra todo en orden.

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
    ./estacion enviar      # Ctrl+C para terminar
    ./estacion escuchar    # en otra terminal; con --alarmas=10 en el servidor falso

El servidor falso también levanta un broker MQTT en el puerto 1883 y acepta estas opciones:

    python3 pruebas/servidor_falso.py --caidas                    # dos caidas 503 con Retry-After
    python3 pruebas/servidor_falso.py --alarmas=10                # una alarma MQTT cada 10 s
    curl http://127.0.0.1:4000/admin/cortar-mqtt                  # tumba el MQTT para probar la reconexion
    python3 pruebas/servidor_falso.py --intervalo=1 --latido=3    # prueba mas rapida
    curl -s -H "Authorization: Bearer $ITLA_TOKEN" http://127.0.0.1:4000/api/v1/mi-progreso

Para revisar la memoria: `valgrind --leak-check=full ./estacion descargar`.

## Uso de IA

Se usó un asistente de IA (Claude) como apoyo para estructurar, escribir y explicar el código.
El autor revisó y comprende cada parte, y puede explicarla y modificarla en la defensa.

#ifndef ENVIO_H
#define ENVIO_H

#include "red.h"
#include "datos.h"

/* Niveles 3 y 4: genera una lectura cada intervalo_s, la guarda en la cola de SQLite
 * y la envia al servidor; ademas publica un latido MQTT cada latido_s.
 * Corre hasta Ctrl+C. Devuelve 0 si termino bien, distinto de 0 si fallo. */
int cmd_enviar(Red *red, sqlite3 *db);

#endif

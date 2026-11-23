#ifndef ESCUCHAR_H
#define ESCUCHAR_H

#include "red.h"
#include "datos.h"

/* Nivel 5: se suscribe al topico de alarmas por MQTT, guarda cada alarma nueva en
 * SQLite y la confirma por HTTP. Corre hasta Ctrl+C. */
int cmd_escuchar(Red *red, sqlite3 *db);

#endif

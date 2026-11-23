#ifndef COMANDOS_H
#define COMANDOS_H

#include "red.h"
#include "datos.h"

/* Nivel 0: GET /config, guarda en la tabla config e imprime lo importante.
 * Devuelve 0 si todo bien, distinto de 0 si falla. */
int cmd_config(Red *red, sqlite3 *db);

#endif

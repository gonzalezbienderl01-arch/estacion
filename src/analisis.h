#ifndef ANALISIS_H
#define ANALISIS_H

#include "red.h"
#include "datos.h"

/* Nivel 2: calcula las estadisticas por maquina con SQL, las muestra y las
 * envia al servidor. Devuelve 0 si todo bien, distinto de 0 si falla. */
int cmd_analizar(Red *red, sqlite3 *db);

#endif

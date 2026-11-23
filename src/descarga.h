#ifndef DESCARGA_H
#define DESCARGA_H

#include "red.h"
#include "datos.h"

/* Nivel 1: baja las lecturas de planta por paginas, las guarda en SQLite y
 * envia el resumen a /respuestas/descarga.
 * Devuelve 0 si el servidor confirma (correcto=1), distinto de 0 si algo falla. */
int cmd_descargar(Red *red, sqlite3 *db);

#endif

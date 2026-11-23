#ifndef DATOS_H
#define DATOS_H

#include <stddef.h>
#include "sqlite3.h"

#define DATOS_ARCHIVO "estacion.db"

/* Abre la base, activa WAL y busy_timeout, y crea las 4 tablas si no existen.
 * Devuelve 0 si todo bien, -1 si falla (ya imprime el motivo). */
int  datos_abrir(const char *ruta, sqlite3 **db);
void datos_cerrar(sqlite3 *db);

/* Transacciones. */
int  datos_comenzar(sqlite3 *db);
int  datos_confirmar(sqlite3 *db);
void datos_revertir(sqlite3 *db);

/* Tabla config (clave PRIMARY KEY: si existe, se actualiza). */
int  datos_guardar_config(sqlite3 *db, const char *clave, const char *valor);

/* Devuelve 0 si la clave existe (valor copiado en salida), 1 si no existe, -1 si hay error. */
int  datos_leer_config(sqlite3 *db, const char *clave, char *salida, size_t tam);

#endif

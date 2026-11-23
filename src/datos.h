#ifndef DATOS_H
#define DATOS_H

#include <stddef.h>
#include "sqlite3.h"

#define DATOS_ARCHIVO "estacion.db"

/* Una fila de lecturas_planta tal como llega del servidor.
 * Los campos que vienen vacios se guardan como NULL en la base. */
typedef struct {
    long long   id;
    const char *maquina;        /* NULL si vino vacio */
    const char *estado;         /* NULL si vino vacio */
    int         tiene_ts;
    int         tiene_temp;
    int         tiene_corriente;
    int         tiene_vibracion;
    long long   ts;
    double      temp;
    double      corriente;
    double      vibracion;
} Lectura;

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

/* Tabla lecturas_planta. La sentencia se prepara una vez y se reutiliza fila a fila. */
int  datos_preparar_insertar_lectura(sqlite3 *db, sqlite3_stmt **st);
void datos_finalizar(sqlite3_stmt *st);

/* INSERT OR IGNORE: el PRIMARY KEY absorbe las filas repetidas.
 * Devuelve 1 si la fila era nueva, 0 si ya existia (se ignoro), -1 si hay error. */
int  datos_insertar_lectura(sqlite3 *db, sqlite3_stmt *st, const Lectura *l);

/* Cuenta las filas y suma los ids con SQL (no con bucles en C). 0 = bien, -1 = error. */
int  datos_resumen_lecturas(sqlite3 *db, long long *filas_unicas, long long *suma_ids);

#endif

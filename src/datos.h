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

/* Resultado del analisis de una maquina (Nivel 2). */
#define ANALISIS_MAX_MAQUINAS 16
typedef struct {
    char      maquina[16];
    long long validas;
    long long descartadas;
    int       hay_temp;         /* 0 si la maquina no tiene ninguna fila valida */
    double    temp_min;
    double    temp_max;
    double    temp_prom;        /* ya redondeada a 2 decimales por SQL */
    long long sobre_umbral;     /* filas validas con temp > umbral */
} Analisis;

/* Una sola consulta SQL agrupa por maquina. Una fila es invalida si estado no es "ok",
 * si temp vale -999.0 o si falta cualquier campo. Devuelve cuantas maquinas llenó
 * (0 o mas) o -1 si hay error. */
int  datos_analizar(sqlite3 *db, double umbral, Analisis *salida, int max);

/* Una fila de la cola de telemetria. */
typedef struct {
    long long seq;
    long long ts;
    double    temp;
    double    corriente;
} Telemetria;

/* El numero de secuencia vive en SQLite: sobrevive a un reinicio del programa. */
int  datos_siguiente_seq(sqlite3 *db, long long *seq);
int  datos_guardar_telemetria(sqlite3 *db, const Telemetria *t);          /* enviado = 0 */
/* Busca la pendiente mas vieja (menor seq). Devuelve 1 si hay, 0 si no hay, -1 si error. */
int  datos_telemetria_pendiente(sqlite3 *db, Telemetria *t);
int  datos_sumar_intento(sqlite3 *db, long long seq);
int  datos_marcar_enviada(sqlite3 *db, long long seq);                    /* enviado = 1 */
int  datos_contar_pendientes(sqlite3 *db, long long *cuantas);

#endif

#include "datos.h"

#include <stdio.h>

/* Solo SQL fijo (sin datos variables), por eso aqui se permite sqlite3_exec. */
static const char *ESQUEMA =
    "CREATE TABLE IF NOT EXISTS config ("
    "  clave TEXT PRIMARY KEY,"
    "  valor TEXT NOT NULL);"

    "CREATE TABLE IF NOT EXISTS lecturas_planta ("
    "  id INTEGER PRIMARY KEY,"
    "  maquina TEXT,"
    "  ts INTEGER,"
    "  temp REAL,"
    "  corriente REAL,"
    "  vibracion REAL,"
    "  estado TEXT);"

    "CREATE TABLE IF NOT EXISTS telemetria ("
    "  seq INTEGER PRIMARY KEY,"
    "  ts INTEGER NOT NULL,"
    "  temp REAL NOT NULL,"
    "  corriente REAL NOT NULL,"
    "  enviado INTEGER NOT NULL DEFAULT 0 CHECK (enviado IN (0, 1)),"
    "  intentos INTEGER NOT NULL DEFAULT 0);"

    "CREATE TABLE IF NOT EXISTS alarmas ("
    "  codigo TEXT PRIMARY KEY,"
    "  maquina TEXT,"
    "  ts_servidor INTEGER,"
    "  ts_recibida INTEGER,"
    "  ts_ack INTEGER);";

static int ejecutar_fijo(sqlite3 *db, const char *sql)
{
    char *msg = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &msg) != SQLITE_OK) {
        fprintf(stderr, "Error SQL: %s\n", msg ? msg : sqlite3_errmsg(db));
        sqlite3_free(msg);
        return -1;
    }
    return 0;
}

int datos_abrir(const char *ruta, sqlite3 **db)
{
    if (sqlite3_open(ruta, db) != SQLITE_OK) {
        fprintf(stderr, "Error abriendo %s: %s\n", ruta,
                *db ? sqlite3_errmsg(*db) : "sin memoria");
        sqlite3_close(*db);
        *db = NULL;
        return -1;
    }

    /* enviar y escuchar usan la base a la vez: WAL + espera ante bloqueo. */
    sqlite3_busy_timeout(*db, 2000);

    if (ejecutar_fijo(*db, "PRAGMA journal_mode=WAL;") != 0 ||
        ejecutar_fijo(*db, ESQUEMA) != 0) {
        datos_cerrar(*db);
        *db = NULL;
        return -1;
    }
    return 0;
}

void datos_cerrar(sqlite3 *db)
{
    if (db != NULL)
        sqlite3_close(db);
}

int datos_comenzar(sqlite3 *db)   { return ejecutar_fijo(db, "BEGIN;"); }
int datos_confirmar(sqlite3 *db)  { return ejecutar_fijo(db, "COMMIT;"); }
void datos_revertir(sqlite3 *db)  { (void)ejecutar_fijo(db, "ROLLBACK;"); }

int datos_guardar_config(sqlite3 *db, const char *clave, const char *valor)
{
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO config (clave, valor) VALUES (?, ?);",
        -1, &st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparando INSERT config: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(st, 1, clave, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, valor, -1, SQLITE_STATIC);

    rc = sqlite3_step(st);
    if (rc != SQLITE_DONE)
        fprintf(stderr, "Error guardando '%s': %s\n", clave, sqlite3_errmsg(db));

    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int datos_leer_config(sqlite3 *db, const char *clave, char *salida, size_t tam)
{
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT valor FROM config WHERE clave = ?;", -1, &st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparando SELECT config: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(st, 1, clave, -1, SQLITE_STATIC);

    int resultado;
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        snprintf(salida, tam, "%s", (const char *)sqlite3_column_text(st, 0));
        resultado = 0;
    } else if (rc == SQLITE_DONE) {
        resultado = 1;
    } else {
        fprintf(stderr, "Error leyendo '%s': %s\n", clave, sqlite3_errmsg(db));
        resultado = -1;
    }

    sqlite3_finalize(st);
    return resultado;
}

int datos_preparar_insertar_lectura(sqlite3 *db, sqlite3_stmt **st)
{
    int rc = sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO lecturas_planta "
        "(id, maquina, ts, temp, corriente, vibracion, estado) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);",
        -1, st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparando INSERT lecturas: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

void datos_finalizar(sqlite3_stmt *st)
{
    sqlite3_finalize(st);               /* con NULL no hace nada */
}

/* Los tres ayudantes de abajo ponen el valor, o NULL si el campo vino vacio. */
static void enlazar_texto(sqlite3_stmt *st, int pos, const char *texto)
{
    if (texto != NULL)
        sqlite3_bind_text(st, pos, texto, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, pos);
}

static void enlazar_entero(sqlite3_stmt *st, int pos, int hay, long long v)
{
    if (hay)
        sqlite3_bind_int64(st, pos, v);
    else
        sqlite3_bind_null(st, pos);
}

static void enlazar_real(sqlite3_stmt *st, int pos, int hay, double v)
{
    if (hay)
        sqlite3_bind_double(st, pos, v);
    else
        sqlite3_bind_null(st, pos);
}

int datos_insertar_lectura(sqlite3 *db, sqlite3_stmt *st, const Lectura *l)
{
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);

    sqlite3_bind_int64(st, 1, l->id);
    enlazar_texto(st, 2, l->maquina);
    enlazar_entero(st, 3, l->tiene_ts, l->ts);
    enlazar_real(st, 4, l->tiene_temp, l->temp);
    enlazar_real(st, 5, l->tiene_corriente, l->corriente);
    enlazar_real(st, 6, l->tiene_vibracion, l->vibracion);
    enlazar_texto(st, 7, l->estado);

    if (sqlite3_step(st) != SQLITE_DONE) {
        fprintf(stderr, "Error insertando la lectura %lld: %s\n", l->id, sqlite3_errmsg(db));
        return -1;
    }

    /* Si el PRIMARY KEY la rechazo por repetida, no cambio ninguna fila. */
    return sqlite3_changes(db) > 0 ? 1 : 0;
}

int datos_resumen_lecturas(sqlite3 *db, long long *filas_unicas, long long *suma_ids)
{
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT COUNT(*), COALESCE(SUM(id), 0) FROM lecturas_planta;",
        -1, &st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparando el resumen: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *filas_unicas = sqlite3_column_int64(st, 0);
        *suma_ids = sqlite3_column_int64(st, 1);
    } else {
        fprintf(stderr, "Error calculando el resumen: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? 0 : -1;
}

/* Toda la estadistica se calcula en SQL (el reto prohibe bucles en C para esto).
 * La subconsulta marca cada fila como valida (1) o descartada (0); la consulta de
 * afuera agrupa por maquina. Las funciones MIN/MAX/AVG ignoran los NULL del CASE,
 * asi solo cuentan las filas validas. */
static const char *SQL_ANALISIS =
    "SELECT maquina,"
    "       COALESCE(SUM(v), 0),"
    "       COUNT(*) - COALESCE(SUM(v), 0),"
    "       MIN(CASE WHEN v = 1 THEN temp END),"
    "       MAX(CASE WHEN v = 1 THEN temp END),"
    "       ROUND(AVG(CASE WHEN v = 1 THEN temp END), 2),"
    "       COALESCE(SUM(CASE WHEN v = 1 AND temp > ?1 THEN 1 END), 0) "
    "FROM ("
    "  SELECT maquina, temp,"
    "         CASE WHEN estado = 'ok' COLLATE NOCASE"
    "               AND temp <> -999.0"
    "               AND ts IS NOT NULL AND temp IS NOT NULL"
    "               AND corriente IS NOT NULL AND vibracion IS NOT NULL"
    "              THEN 1 ELSE 0 END AS v"
    "  FROM lecturas_planta WHERE maquina IS NOT NULL) "
    "GROUP BY maquina ORDER BY maquina;";

int datos_analizar(sqlite3 *db, double umbral, Analisis *salida, int max)
{
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db, SQL_ANALISIS, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparando el analisis: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_double(st, 1, umbral);

    int n = 0;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (n >= max) {
            fprintf(stderr, "Error: hay mas de %d maquinas en la base\n", max);
            sqlite3_finalize(st);
            return -1;
        }
        Analisis *a = &salida[n++];
        snprintf(a->maquina, sizeof a->maquina, "%s", (const char *)sqlite3_column_text(st, 0));
        a->validas = sqlite3_column_int64(st, 1);
        a->descartadas = sqlite3_column_int64(st, 2);
        a->hay_temp = sqlite3_column_type(st, 3) != SQLITE_NULL;
        a->temp_min = a->hay_temp ? sqlite3_column_double(st, 3) : 0.0;
        a->temp_max = a->hay_temp ? sqlite3_column_double(st, 4) : 0.0;
        a->temp_prom = a->hay_temp ? sqlite3_column_double(st, 5) : 0.0;
        a->sobre_umbral = sqlite3_column_int64(st, 6);
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error calculando el analisis: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return -1;
    }
    sqlite3_finalize(st);
    return n;
}

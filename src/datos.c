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

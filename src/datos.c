/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

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

/* ---------- Cola de telemetria (Nivel 3) ---------- */

/* Prepara una sentencia, ejecuta un solo paso y la finaliza. Para las que no devuelven filas. */
static int paso_unico(sqlite3 *db, sqlite3_stmt *st, const char *que)
{
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error SQL (%s): %s\n", que, sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

int datos_siguiente_seq(sqlite3 *db, long long *seq)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(seq), 0) + 1 FROM telemetria;",
                           -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando seq: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW)
        *seq = sqlite3_column_int64(st, 0);
    else
        fprintf(stderr, "Error leyendo seq: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? 0 : -1;
}

int datos_guardar_telemetria(sqlite3 *db, const Telemetria *t)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO telemetria (seq, ts, temp, corriente, enviado, intentos) "
            "VALUES (?, ?, ?, ?, 0, 0);", -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando INSERT telemetria: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int64(st, 1, t->seq);
    sqlite3_bind_int64(st, 2, t->ts);
    sqlite3_bind_double(st, 3, t->temp);
    sqlite3_bind_double(st, 4, t->corriente);
    return paso_unico(db, st, "guardar telemetria");
}

int datos_telemetria_pendiente(sqlite3 *db, Telemetria *t)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT seq, ts, temp, corriente FROM telemetria "
            "WHERE enviado = 0 ORDER BY seq LIMIT 1;", -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando SELECT pendiente: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    int rc = sqlite3_step(st);
    int resultado;
    if (rc == SQLITE_ROW) {
        t->seq = sqlite3_column_int64(st, 0);
        t->ts = sqlite3_column_int64(st, 1);
        t->temp = sqlite3_column_double(st, 2);
        t->corriente = sqlite3_column_double(st, 3);
        resultado = 1;
    } else if (rc == SQLITE_DONE) {
        resultado = 0;
    } else {
        fprintf(stderr, "Error leyendo pendiente: %s\n", sqlite3_errmsg(db));
        resultado = -1;
    }
    sqlite3_finalize(st);
    return resultado;
}

int datos_sumar_intento(sqlite3 *db, long long seq)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "UPDATE telemetria SET intentos = intentos + 1 WHERE seq = ?;",
                           -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando UPDATE intentos: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int64(st, 1, seq);
    return paso_unico(db, st, "sumar intento");
}

int datos_marcar_enviada(sqlite3 *db, long long seq)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "UPDATE telemetria SET enviado = 1 WHERE seq = ?;",
                           -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando UPDATE enviado: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int64(st, 1, seq);
    return paso_unico(db, st, "marcar enviada");
}

int datos_contar_pendientes(sqlite3 *db, long long *cuantas)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM telemetria WHERE enviado = 0;",
                           -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando el conteo: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW)
        *cuantas = sqlite3_column_int64(st, 0);
    else
        fprintf(stderr, "Error contando pendientes: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? 0 : -1;
}

/* ---------- Alarmas (Nivel 5) ---------- */

int datos_alarma_estado(sqlite3 *db, const char *codigo, int *existe, int *confirmada)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT ts_ack IS NOT NULL FROM alarmas WHERE codigo = ?;",
                           -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando SELECT alarma: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(st, 1, codigo, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    *existe = (rc == SQLITE_ROW);
    *confirmada = (rc == SQLITE_ROW) ? sqlite3_column_int(st, 0) : 0;
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
        fprintf(stderr, "Error leyendo la alarma %s: %s\n", codigo, sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return (rc == SQLITE_ROW || rc == SQLITE_DONE) ? 0 : -1;
}

int datos_guardar_alarma(sqlite3 *db, const char *codigo, const char *maquina,
                         long long ts_servidor, long long ts_recibida)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO alarmas (codigo, maquina, ts_servidor, ts_recibida) "
            "VALUES (?, ?, ?, ?);", -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando INSERT alarma: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(st, 1, codigo, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, maquina, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, ts_servidor);
    sqlite3_bind_int64(st, 4, ts_recibida);
    return paso_unico(db, st, "guardar alarma");
}

int datos_confirmar_alarma(sqlite3 *db, const char *codigo, long long ts_ack)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "UPDATE alarmas SET ts_ack = ? WHERE codigo = ?;",
                           -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando UPDATE alarma: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int64(st, 1, ts_ack);
    sqlite3_bind_text(st, 2, codigo, -1, SQLITE_STATIC);
    return paso_unico(db, st, "confirmar alarma");
}

int datos_alarmas_sin_confirmar(sqlite3 *db, char codigos[][ALARMA_CODIGO_MAX], int max)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT codigo FROM alarmas WHERE ts_ack IS NULL ORDER BY ts_recibida LIMIT ?;",
            -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando SELECT sin confirmar: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(st, 1, max);

    int n = 0, rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW && n < max) {
        snprintf(codigos[n], ALARMA_CODIGO_MAX, "%s", (const char *)sqlite3_column_text(st, 0));
        n++;
    }
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        fprintf(stderr, "Error leyendo alarmas sin confirmar: %s\n", sqlite3_errmsg(db));
        n = -1;
    }
    sqlite3_finalize(st);
    return n;
}

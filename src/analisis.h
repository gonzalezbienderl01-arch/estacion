/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#ifndef ANALISIS_H
#define ANALISIS_H

#include "red.h"
#include "datos.h"

/* Nivel 2: calcula las estadisticas por maquina con SQL, las muestra y las
 * envia al servidor. Devuelve 0 si todo bien, distinto de 0 si falla. */
int cmd_analizar(Red *red, sqlite3 *db);

#endif

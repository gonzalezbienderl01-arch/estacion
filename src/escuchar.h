/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#ifndef ESCUCHAR_H
#define ESCUCHAR_H

#include "red.h"
#include "datos.h"

/* Nivel 5: se suscribe al topico de alarmas por MQTT, guarda cada alarma nueva en
 * SQLite y la confirma por HTTP. Corre hasta Ctrl+C. */
int cmd_escuchar(Red *red, sqlite3 *db);

#endif

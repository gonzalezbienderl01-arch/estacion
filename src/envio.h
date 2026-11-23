/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#ifndef ENVIO_H
#define ENVIO_H

#include "red.h"
#include "datos.h"

/* Niveles 3 y 4: genera una lectura cada intervalo_s, la guarda en la cola de SQLite
 * y la envia al servidor; ademas publica un latido MQTT cada latido_s.
 * Corre hasta Ctrl+C. Devuelve 0 si termino bien, distinto de 0 si fallo. */
int cmd_enviar(Red *red, sqlite3 *db);

#endif

#!/usr/bin/env python3
"""Servidor falso para probar ./estacion sin el servidor real del profesor.

Corre SOLO en tu computadora (127.0.0.1, puerto 4000). Simula:
  GET  /api/v1/ping
  GET  /api/v1/config
  GET  /api/v1/lecturas?desde_id=N&limite=M   (2000 filas, con repetidas y campos vacios)
  POST /api/v1/respuestas/descarga            (responde correcto=1 o correcto=0)
  POST /api/v1/respuestas/analisis            (campo por campo, maximo 10 intentos)
  POST /api/v1/telemetria                     (201 creada, 200 duplicada, 503 con Retry-After)
  GET  /api/v1/mi-progreso                    (resumen de lo que ha recibido el servidor)
  Broker MQTT en 127.0.0.1:1883               (recibe tus latidos)

Uso (en una terminal):
    python3 pruebas/servidor_falso.py
    python3 pruebas/servidor_falso.py --429-una-vez     # responde 429 la primera vez
    python3 pruebas/servidor_falso.py --intervalo=1 --latido=3   # va mas rapido para probar
    python3 pruebas/servidor_falso.py --caidas          # dos caidas 503 (seg 8-16 y 28-36 tras la 1a lectura)

Uso (en otra terminal):
    export ITLA_HOST=127.0.0.1
    export ITLA_MATRICULA=20241302
    export ITLA_TOKEN=abcdefghijklmnopqrstuvwxyz012345   # token de PRUEBA, solo sirve aqui
    ./estacion descargar
"""
import json
import math
import random
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

import mqtt_falso

TOKEN_PRUEBA = "abcdefghijklmnopqrstuvwxyz012345"   # 32 caracteres
TOTAL = 2000
REPETIDAS_POR_PAGINA = 3
RESPONDER_429_UNA_VEZ = "--429-una-vez" in sys.argv
CAIDAS = "--caidas" in sys.argv
VENTANAS_503 = [(8, 16), (28, 36)]      # segundos desde la primera lectura recibida


def argumento(nombre, defecto):
    """Lee --nombre=valor de la linea de comandos (para acelerar las pruebas)."""
    for a in sys.argv:
        if a.startswith("--%s=" % nombre):
            return a.split("=", 1)[1]
    return defecto


INTERVALO_S = argumento("intervalo", "5")
LATIDO_S = argumento("latido", "10")

UMBRAL = 72.50
MAX_INTENTOS = 10
estado = {"ya_dio_429": False, "intentos": 0, "t0": None, "retry_hasta": 0.0,
          "telemetria": {}, "duplicadas": 0, "violaciones_503": 0, "tarde": 0, "peticiones_503": 0}


def generar_filas():
    azar = random.Random(1302)
    filas = []
    for i in range(1, TOTAL + 1):
        maquina = "M%d" % ((i % 4) + 1)
        ts = 1763654400 + i * 5
        temp = "%.2f" % azar.uniform(40, 80)
        corriente = "%.2f" % azar.uniform(2, 5)
        vibracion = "%.2f" % azar.uniform(0, 2)
        estado_fila = "OK"
        if i % 37 == 0:
            estado_fila = "FALLA"
        if i % 41 == 0:
            temp = "-999.0"
        if i % 29 == 0:
            corriente = ""                  # campo vacio (strtok se lo saltaria)
        if i % 53 == 0:
            vibracion = ""
        filas.append("%d,%s,%d,%s,%s,%s,%s" % (i, maquina, ts, temp, corriente, vibracion, estado_fila))
    return filas


FILAS = generar_filas()
SUMA_IDS = sum(range(1, TOTAL + 1))


def estadisticas_esperadas():
    """Calcula lo que debe dar el analisis, de forma independiente al programa en C."""
    por_maquina = {}
    for fila in FILAS:
        id_, maquina, ts, temp, corriente, vibracion, est = fila.split(",")
        valida = (est.lower() == "ok" and "" not in (ts, temp, corriente, vibracion)
                  and float(temp) != -999.0)
        datos = por_maquina.setdefault(maquina, {"validas": 0, "descartadas": 0, "temps": []})
        if valida:
            datos["validas"] += 1
            datos["temps"].append(float(temp))
        else:
            datos["descartadas"] += 1
    esperado = {}
    for maquina, d in por_maquina.items():
        t = d["temps"]
        esperado[maquina] = {
            "validas": d["validas"], "descartadas": d["descartadas"],
            "temp_min": min(t), "temp_max": max(t), "temp_prom": round(sum(t) / len(t), 2),
            "sobre_umbral": sum(1 for x in t if x > UMBRAL),
        }
    return esperado


ESPERADO = estadisticas_esperadas()
CAMPOS = ["validas", "descartadas", "temp_min", "temp_max", "temp_prom", "sobre_umbral"]


def pagina(desde_id, limite):
    """Devuelve filas con id > desde_id, y repite las ultimas de la pagina anterior."""
    repetidas = FILAS[max(0, desde_id - REPETIDAS_POR_PAGINA):desde_id] if desde_id > 0 else []
    nuevas = FILAS[desde_id:desde_id + (limite - len(repetidas))]
    return repetidas + nuevas


class Manejador(BaseHTTPRequestHandler):
    def responder(self, codigo, texto, tipo="text/plain", extra=None):
        cuerpo = texto.encode()
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        for nombre, valor in (extra or {}).items():
            self.send_header(nombre, valor)
        self.send_header("Content-Length", str(len(cuerpo)))
        self.end_headers()
        self.wfile.write(cuerpo)

    def autorizado(self):
        if self.headers.get("Authorization") != "Bearer " + TOKEN_PRUEBA:
            self.responder(401, "no autorizado\n")
            return False
        return True

    def do_GET(self):
        url = urlparse(self.path)
        print("GET", self.path, "| UA:", self.headers.get("User-Agent"), flush=True)

        if url.path == "/api/v1/ping":
            self.responder(200, "ok=1\nhora=1763740800\nip_publica=127.0.0.1\n")
        elif not self.autorizado():
            return
        elif url.path == "/api/v1/config":
            self.responder(200, "matricula=20241302\nhora=1763740800\numbral_temp=72.50\n"
                                "intervalo_s=%s\nlatido_s=%s\nmaquinas=M1,M2,M3,M4\nlote_max=200\n"
                           % (INTERVALO_S, LATIDO_S))
        elif url.path == "/api/v1/mi-progreso":
            self.progreso()
        elif url.path == "/api/v1/lecturas":
            self.lecturas(parse_qs(url.query))
        else:
            self.responder(404, "no existe\n")

    def lecturas(self, consulta):
        if RESPONDER_429_UNA_VEZ and not estado["ya_dio_429"]:
            estado["ya_dio_429"] = True
            self.responder(429, "demasiado rapido\n")
            return
        try:
            desde_id = int(consulta.get("desde_id", ["0"])[0])
            limite = int(consulta.get("limite", ["200"])[0])
        except ValueError:
            self.responder(400, "parametros invalidos\n")
            return
        if not 1 <= limite <= 200 or desde_id < 0:
            self.responder(400, "parametros fuera de rango\n")
            return
        filas = pagina(desde_id, limite)
        cabecera = "id,maquina,ts,temp,corriente,vibracion,estado\n"
        self.responder(200, cabecera + "".join(f + "\n" for f in filas), "text/csv")

    def do_POST(self):
        largo = int(self.headers.get("Content-Length", "0"))
        cuerpo = self.rfile.read(largo).decode()
        print("POST", self.path, cuerpo, "| tipo:", self.headers.get("Content-Type"), flush=True)
        if not self.autorizado():
            return
        if self.path == "/api/v1/telemetria":
            self.telemetria(cuerpo)
            return
        if self.path == "/api/v1/respuestas/analisis":
            self.analisis(cuerpo)
            return
        if self.path != "/api/v1/respuestas/descarga":
            self.responder(404, "no existe\n")
            return
        try:
            datos = json.loads(cuerpo)
        except json.JSONDecodeError:
            self.responder(400, "json invalido\n")
            return
        ok = datos.get("filas_unicas") == TOTAL and datos.get("suma_ids") == SUMA_IDS
        self.responder(200, "correcto=%d\n" % (1 if ok else 0))

    def telemetria(self, cuerpo):
        ahora = time.time()
        if estado["t0"] is None:
            estado["t0"] = ahora
        if ahora < estado["retry_hasta"]:
            estado["violaciones_503"] += 1          # el cliente no respeto el Retry-After
        relativo = ahora - estado["t0"]
        if CAIDAS:
            for inicio, fin in VENTANAS_503:
                if inicio <= relativo < fin:
                    espera = math.ceil(fin - relativo)
                    estado["retry_hasta"] = ahora + espera
                    estado["peticiones_503"] += 1
                    self.responder(503, "servidor en mantenimiento\n", extra={"Retry-After": str(espera)})
                    return
        try:
            lectura = json.loads(cuerpo)
            seq, ts = int(lectura["seq"]), int(lectura["ts"])
            float(lectura["temp"]), float(lectura["corriente"])
        except (json.JSONDecodeError, KeyError, TypeError, ValueError):
            self.responder(400, "json invalido\n")
            return
        duplicada = seq in estado["telemetria"]
        if duplicada:
            estado["duplicadas"] += 1
        else:
            estado["telemetria"][seq] = ahora
            if ahora - ts >= 30:
                estado["tarde"] += 1                # lectura recuperada de la cola
        if duplicada:
            self.responder(200, "estado=duplicada\n")
        else:
            self.responder(201, "estado=creada\n")

    def progreso(self):
        seqs = sorted(estado["telemetria"])
        completitud = len(seqs) / (seqs[-1] - seqs[0] + 1) if seqs else 0.0
        lineas = ["telemetria_unicas=%d" % len(seqs),
                  "seq_min=%d" % (seqs[0] if seqs else 0),
                  "seq_max=%d" % (seqs[-1] if seqs else 0),
                  "completitud=%.3f" % completitud,
                  "duplicadas=%d" % estado["duplicadas"],
                  "recuperadas_tarde=%d" % estado["tarde"],
                  "peticiones_503=%d" % estado["peticiones_503"],
                  "violaciones_retry_after=%d" % estado["violaciones_503"],
                  "latidos=%d" % len(estado.get("latidos", []))]
        self.responder(200, "\n".join(lineas) + "\n")

    def analisis(self, cuerpo):
        if estado["intentos"] >= MAX_INTENTOS:
            self.responder(403, "intentos_restantes=0\n")
            return
        try:
            enviado = {m["maquina"]: m for m in json.loads(cuerpo)["maquinas"]}
        except (json.JSONDecodeError, KeyError, TypeError):
            self.responder(400, "json invalido\n")
            return
        estado["intentos"] += 1
        lineas = []
        for maquina in sorted(ESPERADO):
            for campo in CAMPOS:
                try:
                    bien = abs(float(enviado[maquina][campo]) - ESPERADO[maquina][campo]) <= 0.01
                except (KeyError, TypeError, ValueError):
                    bien = False
                lineas.append("%s.%s=%s" % (maquina, campo, "ok" if bien else "mal"))
        lineas.append("intentos_restantes=%d" % (MAX_INTENTOS - estado["intentos"]))
        self.responder(200, "\n".join(lineas) + "\n")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    mqtt_falso.iniciar("20241302", TOKEN_PRUEBA, estado)
    print("Servidor falso en http://127.0.0.1:4000 y MQTT en 127.0.0.1:1883 (Ctrl+C para salir)", flush=True)
    HTTPServer(("127.0.0.1", 4000), Manejador).serve_forever()

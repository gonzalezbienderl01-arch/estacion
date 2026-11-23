#!/usr/bin/env python3
"""Servidor falso para probar ./estacion sin el servidor real del profesor.

Corre SOLO en tu computadora (127.0.0.1, puerto 4000). Simula:
  GET  /api/v1/ping
  GET  /api/v1/config
  GET  /api/v1/lecturas?desde_id=N&limite=M   (2000 filas, con repetidas y campos vacios)
  POST /api/v1/respuestas/descarga            (responde correcto=1 o correcto=0)

Uso (en una terminal):
    python3 pruebas/servidor_falso.py
    python3 pruebas/servidor_falso.py --429-una-vez     # responde 429 la primera vez

Uso (en otra terminal):
    export ITLA_HOST=127.0.0.1
    export ITLA_MATRICULA=20241302
    export ITLA_TOKEN=abcdefghijklmnopqrstuvwxyz012345   # token de PRUEBA, solo sirve aqui
    ./estacion descargar
"""
import json
import random
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

TOKEN_PRUEBA = "abcdefghijklmnopqrstuvwxyz012345"   # 32 caracteres
TOTAL = 2000
REPETIDAS_POR_PAGINA = 3
RESPONDER_429_UNA_VEZ = "--429-una-vez" in sys.argv

estado = {"ya_dio_429": False}


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


def pagina(desde_id, limite):
    """Devuelve filas con id > desde_id, y repite las ultimas de la pagina anterior."""
    repetidas = FILAS[max(0, desde_id - REPETIDAS_POR_PAGINA):desde_id] if desde_id > 0 else []
    nuevas = FILAS[desde_id:desde_id + (limite - len(repetidas))]
    return repetidas + nuevas


class Manejador(BaseHTTPRequestHandler):
    def responder(self, codigo, texto, tipo="text/plain"):
        cuerpo = texto.encode()
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
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
                                "intervalo_s=5\nlatido_s=10\nmaquinas=M1,M2,M3,M4\nlote_max=200\n")
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

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print("Servidor falso en http://127.0.0.1:4000 (Ctrl+C para salir)", flush=True)
    HTTPServer(("127.0.0.1", 4000), Manejador).serve_forever()

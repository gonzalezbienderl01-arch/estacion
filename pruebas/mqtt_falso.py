"""Broker MQTT 3.1.1 minimo, solo para practicar sin el broker del profesor.

Lo arranca servidor_falso.py junto con el servidor HTTP. Soporta lo justo:
CONNECT (usuario = matricula, clave = token), PUBLISH con QoS 0, SUBSCRIBE,
PINGREQ y DISCONNECT. Imita el control de acceso del reto: cada estudiante solo
puede escribir en su topico de latido y leer su topico de alarma.
"""
import socket
import threading
import time

PUERTO = 1883


def leer_exacto(conexion, n):
    datos = b""
    while len(datos) < n:
        trozo = conexion.recv(n - len(datos))
        if not trozo:
            raise ConnectionError("cerrada")
        datos += trozo
    return datos


def leer_paquete(conexion):
    """Devuelve (tipo, banderas, contenido) de un paquete MQTT."""
    primero = leer_exacto(conexion, 1)[0]
    largo, multiplicador = 0, 1
    while True:
        byte = leer_exacto(conexion, 1)[0]
        largo += (byte & 127) * multiplicador
        multiplicador *= 128
        if not byte & 128:
            break
    return primero >> 4, primero & 15, leer_exacto(conexion, largo) if largo else b""


def codificar_largo(n):
    salida = b""
    while True:
        byte = n % 128
        n //= 128
        salida += bytes([byte | 128 if n else byte])
        if not n:
            return salida


def texto_con_largo(datos, pos):
    n = int.from_bytes(datos[pos:pos + 2], "big")
    return datos[pos + 2:pos + 2 + n].decode(errors="replace"), pos + 2 + n


class Broker:
    def __init__(self, usuario_ok, token_ok, estado):
        self.usuario_ok = usuario_ok
        self.token_ok = token_ok
        self.estado = estado                    # diccionario compartido con el servidor HTTP
        self.suscritos = {}                     # topico -> lista de conexiones
        self.retenidos = {}                     # topico -> contenido (mensaje retenido)
        self.candado = threading.Lock()
        estado.setdefault("latidos", [])

    # ---- publicar hacia los suscriptores ----
    def publicar(self, topico, contenido, retener=False):
        paquete_cuerpo = len(topico.encode()).to_bytes(2, "big") + topico.encode() + contenido
        cabecera = bytes([0x30 | (1 if retener else 0)]) + codificar_largo(len(paquete_cuerpo))
        with self.candado:
            if retener:
                if contenido:
                    self.retenidos[topico] = contenido
                else:
                    self.retenidos.pop(topico, None)
            for conexion in list(self.suscritos.get(topico, [])):
                try:
                    conexion.sendall(cabecera + paquete_cuerpo)
                except OSError:
                    self.suscritos[topico].remove(conexion)

    def entregar_retenido(self, conexion, topico):
        contenido = self.retenidos.get(topico)
        if contenido:
            cuerpo = len(topico.encode()).to_bytes(2, "big") + topico.encode() + contenido
            conexion.sendall(bytes([0x31]) + codificar_largo(len(cuerpo)) + cuerpo)

    # ---- una conexion de cliente ----
    def atender(self, conexion):
        usuario = None
        try:
            while True:
                tipo, banderas, datos = leer_paquete(conexion)
                if tipo == 1:                                   # CONNECT
                    usuario = self.conectar(conexion, datos)
                    if usuario is None:
                        return
                elif tipo == 3 and usuario:                     # PUBLISH
                    self.recibir_publicacion(usuario, banderas, datos)
                elif tipo == 8 and usuario:                     # SUBSCRIBE
                    self.suscribir(conexion, usuario, datos)
                elif tipo == 12:                                # PINGREQ
                    conexion.sendall(b"\xd0\x00")
                elif tipo == 14:                                # DISCONNECT
                    return
        except (ConnectionError, OSError):
            pass
        finally:
            with self.candado:
                for lista in self.suscritos.values():
                    if conexion in lista:
                        lista.remove(conexion)
            conexion.close()

    def conectar(self, conexion, datos):
        _, pos = texto_con_largo(datos, 0)                      # nombre del protocolo
        banderas = datos[pos + 1]
        pos += 4                                                # nivel (1) + banderas (1) + keepalive (2)
        _, pos = texto_con_largo(datos, pos)                    # client id
        if banderas & 0x04:                                     # mensaje de voluntad: se ignora
            _, pos = texto_con_largo(datos, pos)
            _, pos = texto_con_largo(datos, pos)
        usuario = clave = ""
        if banderas & 0x80:
            usuario, pos = texto_con_largo(datos, pos)
        if banderas & 0x40:
            clave, pos = texto_con_largo(datos, pos)
        if usuario != self.usuario_ok or clave != self.token_ok:
            print("MQTT: conexion rechazada (usuario/clave)", flush=True)
            conexion.sendall(b"\x20\x02\x00\x05")
            return None
        conexion.sendall(b"\x20\x02\x00\x00")
        return usuario

    def recibir_publicacion(self, usuario, banderas, datos):
        topico, pos = texto_con_largo(datos, 0)
        if (banderas >> 1) & 3:
            pos += 2                                            # identificador de paquete (QoS > 0)
        contenido = datos[pos:].decode(errors="replace")
        if topico != "itla/estudiantes/%s/latido" % usuario:    # el ACL del broker lo descarta
            print("MQTT: publicacion en", topico, "descartada por ACL", flush=True)
            return
        self.estado["latidos"].append((time.time(), contenido.strip()))
        print("MQTT latido:", contenido.strip(), flush=True)

    def suscribir(self, conexion, usuario, datos):
        identificador = datos[:2]
        pos, codigos, topicos = 2, b"", []
        while pos < len(datos):
            topico, pos = texto_con_largo(datos, pos)
            pos += 1                                            # QoS pedido
            permitido = topico in ("itla/planta/%s/alarma" % usuario, "itla/planta/general/reloj",
                                   "itla/servidor/ip")
            codigos += b"\x00" if permitido else b"\x80"
            if permitido:
                topicos.append(topico)
        conexion.sendall(b"\x90" + codificar_largo(2 + len(codigos)) + identificador + codigos)
        with self.candado:
            for topico in topicos:
                self.suscritos.setdefault(topico, []).append(conexion)
        print("MQTT: suscrito a", topicos, flush=True)
        for topico in topicos:
            self.entregar_retenido(conexion, topico)

    def servir(self):
        servidor = socket.socket()
        servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        servidor.bind(("127.0.0.1", PUERTO))
        servidor.listen(8)
        while True:
            conexion, _ = servidor.accept()
            threading.Thread(target=self.atender, args=(conexion,), daemon=True).start()


def iniciar(usuario_ok, token_ok, estado):
    broker = Broker(usuario_ok, token_ok, estado)
    threading.Thread(target=broker.servir, daemon=True).start()
    return broker

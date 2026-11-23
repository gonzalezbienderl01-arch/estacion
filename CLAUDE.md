# Reglas del proyecto

Proyecto en C11 (libcurl + SQLite) del reto "Estación de Telemetría ITLA". Se evalúa con revisión de código y defensa oral.

- Escribe todos los comentarios del código y los mensajes de commit en español.
- No agregues líneas Co-Authored-By ni menciones a Claude en los mensajes de commit.
- Explícame cada cambio en palabras sencillas y no cambies nada sin mostrármelo antes.
- Compila con `gcc -std=c11 -Wall -Wextra`: cero advertencias.
- Todo SQL con datos variables usa sentencias preparadas (`?`).
- El token y el host salen solo de `ITLA_TOKEN` e `ITLA_HOST`; nunca van en el código ni en el repositorio.
- Revisa siempre `CURLcode` y el código HTTP; reutiliza un solo easy handle.
- No uses `strtok` para el CSV (se salta los campos vacíos).
- No subas `estacion.db`, binarios, `build/` ni el token.

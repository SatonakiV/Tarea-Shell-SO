# Tarea-Shell-SO

## Integrantes

**Sección 2 — Grupo 8**

- Joaquín Gustavo Adauy Castillo
- Diego Felipe Fuentes Conejeros
- Isaac Amadeus Nelson Castro Villalobos
- Ignacio Esteban Placencia Palma

## Descripción

Implementación de una shell simple en C para Linux que integra las principales funcionalidades trabajadas en la asignatura: ejecución de comandos mediante procesos hijos, comandos internos, redirecciones de entrada y salida, pipelines de largo arbitrario, ejecución y seguimiento de procesos en background, manejo de señales como `SIGCHLD`, `SIGINT` y `SIGQUIT`, y monitoreo de procesos mediante el comando interno `pmon`, que obtiene información directamente desde `/proc`.

## Compilación

Desde la raíz del repositorio ejecutar

```bash
make
```
Esto genera el ejecutable `mishell` 

## Ejecución

Una vez compilado el proyecto, ejecutar

```bash
./mishell
```

Y para eliminar los archivos generados por la ejecución

```bash
make clean
```

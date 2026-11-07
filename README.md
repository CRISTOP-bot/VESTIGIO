# VESTIGIO

**Application Failure Analysis** · *Todo fallo deja una pista.*

VESTIGIO está implementado en **C11** como herramienta de línea de comandos para Linux/POSIX. Inicia procesos hijo reales, recopila stdout y stderr sin confundirlos, registra marcas temporales de recepción, el PID y el resultado de `waitpid`, y guarda las evidencias en JSON. El análisis separa el hecho observado de una hipótesis sobre causa raíz.

## Compilar y probar

Requiere un compilador C11 y APIs POSIX (`fork`, `execvp`, `poll`, `waitpid`). No necesita librerías de terceros:

```sh
make
make test
./vestigio --help
```

## Uso

```sh
./vestigio run -- ./mi_aplicacion --opcion
./vestigio run --output fallo.json -- ./mi_aplicacion argumento
./vestigio analyze-text /ruta/a/un/log.txt
```

La captura ejecuta directamente el programa sin shell, conecta stdin a `/dev/null`, y guarda el registro JSON. El comando devuelve el código de salida del proceso o `128 + señal`, como es habitual en shells POSIX. Para cambiar el nombre del archivo de registro, usa `--output`.

## Evidencias y límites

- **Confirmado:** resultado observado por `waitpid` (código de salida o señal); salida no cero confirma solo ese código, no que haya sido un crash.
- **Probable:** mensajes explícitos como AddressSanitizer, UndefinedBehaviorSanitizer, `std::bad_alloc`, dependencia ausente, deadlock explícito o excepción. Los patrones por sí solos no prueban causalidad.
- **SIGSEGV:** se confirma la señal; el diagnóstico de acceso inválido queda probable y no atribuye función ni línea sin stack trace.
- Los tiempos indican cuándo VESTIGIO recibió cada línea, no cuándo sucedió la operación dentro de la app. La salida concurrente de stdout/stderr no permite reconstruir el orden interno entre threads.
- Esta versión no toma core dumps, no adjunta procesos ya iniciados, no inspecciona memoria ni threads, módulos cargados, símbolos o logs del sistema. No deduce deadlocks por silencio. `analyze-text` procesa texto suministrado por el usuario; no interpreta registros JSON históricos.
- Es una implementación POSIX; en Windows nativo no compila sin portar la captura a APIs de procesos/IO de Windows. La interfaz gráfica solicitada queda pendiente; no hay botones ni paneles ficticios.

## Arquitectura

- `include/vestigio/vestigio.h`: modelos de ejecución y API del motor.
- `src/capture.c`: captura concurrente de pipes, eventos de salida y resultado del proceso.
- `src/analyzers.c`: analizadores de señal/exit y patrones de logs, cada uno con fundamento y recomendación.
- `src/store.c`: serialización de informes JSON con escape de cadenas.
- `src/main.c`: CLI y análisis de logs previos.
- `tests/test_capture.sh`: pruebas de integración con procesos reales y señal SIGSEGV.

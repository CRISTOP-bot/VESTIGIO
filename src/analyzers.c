#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "vestigio/vestigio.h"
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static int contains_ci(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (!n) return 1;
    for (; *haystack; ++haystack) {
        size_t i = 0;
        while (i < n && haystack[i] &&
               tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == n) return 1;
    }
    return 0;
}

static void json_string(FILE *out, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    fputc('"', out);
    for (; *p; ++p) {
        switch (*p) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default: if (*p < 0x20) fprintf(out, "\\u%04x", *p); else fputc(*p, out);
        }
    }
    fputc('"', out);
}

static const struct Rule { const char *needle, *title, *description, *fix; } rules[] = {
    {"AddressSanitizer:", "Diagnóstico de AddressSanitizer", "El log contiene un diagnóstico explícito de AddressSanitizer.", "Leer el stack trace del sanitizer y reproducir la misma build con símbolos."},
    {"UndefinedBehaviorSanitizer:", "Diagnóstico de UndefinedBehaviorSanitizer", "El log contiene un diagnóstico explícito de UndefinedBehaviorSanitizer.", "Revisar la operación y el stack trace indicado por el sanitizer."},
    {"std::bad_alloc", "Posible fallo de asignación de memoria", "El log menciona std::bad_alloc; se requiere contexto para identificar el punto de asignación.", "Inspeccionar el stack trace y el consumo de memoria de la ejecución."},
    {"out of memory", "Posible falta de memoria", "El log menciona falta de memoria; no identifica por sí solo el componente causante.", "Correlacionar con métricas de memoria y logs del sistema."},
    {"cannot open shared object file", "Biblioteca compartida no encontrada", "El loader informa que falta una biblioteca compartida.", "Instalar o empaquetar la biblioteca correcta y verificar rutas de carga."},
    {"error while loading shared libraries", "Fallo al cargar una biblioteca", "El loader reportó un fallo al cargar una biblioteca.", "Verificar dependencia, arquitectura y configuración del loader."},
    {"module not found", "Dependencia no encontrada", "El log reporta que no se encontró un módulo requerido.", "Instalar la dependencia correspondiente y revisar configuración de búsqueda."},
    {"deadlock detected", "Aviso explícito de deadlock", "El log incluye un mensaje explícito de deadlock; no se infiere por ausencia de salida.", "Capturar simultáneamente los stacks de threads y revisar el orden de locks."},
    {"pthread_create", "Posible fallo al crear thread", "El log menciona pthread_create; confirmar el código de error y recursos disponibles.", "Revisar el retorno de pthread_create y los límites de procesos/threads."},
    {"runtime error:", "Error de runtime", "El log contiene un marcador explícito de error de runtime.", "Consultar el contexto y el stack trace asociado al mensaje."},
    {"Traceback (most recent call last):", "Excepción Python no controlada", "El proceso escribió un traceback de Python en su salida.", "Inspeccionar la excepción final y corregir o manejar el error en el punto adecuado."},
    {"uncaught exception", "Posible excepción no controlada", "El log contiene un marcador de excepción no controlada.", "Obtener el stack trace completo y revisar el límite de manejo de excepciones."},
};

static int write_text_findings(const char *text, FILE *out, int *emitted) {
    size_t i;
    int found = 0;
    for (i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i) {
        if (!contains_ci(text, rules[i].needle)) continue;
        if (*emitted) fputs(",\n", out);
        fputs("{\"status\":\"probable\",\"title\":", out);
        json_string(out, rules[i].title);
        fputs(",\"description\":", out); json_string(out, rules[i].description);
        fputs(",\"evidence\":", out); json_string(out, rules[i].needle);
        fputs(",\"confidence_basis\":\"Coincide un marcador de texto observado; no demuestra causalidad.\",\"recommendation\":", out);
        json_string(out, rules[i].fix); fputc('}', out);
        *emitted = 1; ++found;
    }
    return found;
}

int vestigio_analyze_text(const char *text, FILE *out) {
    size_t i;
    int emitted = 0, found = 0;
    for (i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i) {
        if (!contains_ci(text, rules[i].needle)) continue;
        fputs("{\"status\":\"probable\",\"title\":", out); json_string(out, rules[i].title);
        fputs(",\"description\":", out); json_string(out, rules[i].description);
        fputs(",\"evidence\":", out); json_string(out, rules[i].needle);
        fputs(",\"confidence_basis\":\"Coincide un marcador de texto observado; no demuestra causalidad.\",\"recommendation\":", out);
        json_string(out, rules[i].fix); fputs("}\n", out);
        ++found; emitted = 1;
    }
    (void)emitted;
    return found;
}

int vestigio_analyze_run(const VestigioRun *run, FILE *out) {
    size_t i;
    int emitted = 0, matches = 0, segv = 0;
    fputs("[\n", out);
    if (run->has_signal) {
        const char *signame = strsignal(run->term_signal);
#ifdef SIGSEGV
        if (run->term_signal == SIGSEGV) segv = 1;
#endif
        fputs("{\"status\":", out); json_string(out, segv ? "probable" : "confirmed");
        fputs(",\"title\":", out); json_string(out, segv ? "Terminación por SIGSEGV" : "Terminación por señal");
        fputs(",\"description\":", out);
        if (segv) json_string(out, "El proceso terminó por SIGSEGV, señal compatible con acceso inválido a memoria. La señal no identifica la instrucción ni prueba la causa raíz.");
        else json_string(out, "Se observó una terminación por señal; la señal no determina por sí sola la causa raíz.");
        fputs(",\"evidence\":", out); json_string(out, signame ? signame : "señal desconocida");
        fputs(",\"confidence_basis\":\"La señal fue registrada por waitpid; la causa raíz requiere stack trace o core dump.\",\"recommendation\":\"Repetir con símbolos de depuración y analizar stack trace/core dump.\"}", out);
        emitted = 1; ++matches;
    } else if (run->has_exit_code && run->exit_code != 0) {
        if (emitted) fputs(",\n", out);
        fprintf(out, "{\"status\":\"confirmed\",\"title\":\"Salida no exitosa\",\"description\":\"El proceso terminó con código %d; el código no identifica por sí solo la causa.\",\"evidence\":\"exit code %d\",\"confidence_basis\":\"Código de salida recogido directamente mediante waitpid.\",\"recommendation\":\"Consultar el significado del código de salida y revisar stdout/stderr previos.\"}", run->exit_code, run->exit_code);
        emitted = 1; ++matches;
    }
    for (i = 0; i < run->event_count; ++i) {
        if (strcmp(run->events[i].stream, "stderr") == 0) {
            matches += write_text_findings(run->events[i].message, out, &emitted);
        }
    }
    if (!matches) fputs("{\"status\":\"unknown\",\"title\":\"Causa desconocida\",\"description\":\"La evidencia capturada no basta para identificar una causa raíz.\",\"evidence\":null,\"confidence_basis\":\"No se observó señal/código no exitoso ni marcadores conocidos.\",\"recommendation\":\"Añadir logs estructurados, símbolos y captura de core dumps.\"}", out);
    fputs("\n]", out);
    return matches;
}

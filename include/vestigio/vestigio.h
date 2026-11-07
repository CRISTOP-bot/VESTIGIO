#ifndef VESTIGIO_VESTIGIO_H
#define VESTIGIO_VESTIGIO_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char timestamp[40];
    char stream[8];
    char *message;
} VestigioEvent;

typedef struct {
    VestigioEvent *events;
    size_t event_count;
    size_t event_capacity;
    int exit_code;
    int term_signal;
    int has_exit_code;
    int has_signal;
    char started_at[40];
    char finished_at[40];
    long process_id;
} VestigioRun;

void vestigio_run_init(VestigioRun *run);
void vestigio_run_destroy(VestigioRun *run);
int vestigio_capture(VestigioRun *run, char *const argv[]);
int vestigio_write_json(const VestigioRun *run, char *const argv[], const char *path);
int vestigio_analyze_text(const char *text, FILE *out);
int vestigio_analyze_run(const VestigioRun *run, FILE *out);

#ifdef __cplusplus
}
#endif
#endif

#define _POSIX_C_SOURCE 200809L
#include "vestigio/vestigio.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *f) {
    fputs("VESTIGIO — Application Failure Analysis\nTodo fallo deja una pista.\n\n"
          "Uso:\n  vestigio run [--output archivo.json] -- comando [argumentos...]\n"
          "  vestigio analyze-text archivo-de-log\n\n"
          "Captura un proceso hijo, registra stdout/stderr y el resultado POSIX.\n",f);
}
static char *default_filename(void) {
    time_t now=time(NULL);struct tm local;char stamp[32];char *path;
    if(localtime_r(&now,&local)==NULL)return strdup("vestigio-run.json");
    if(!strftime(stamp,sizeof(stamp),"vestigio-run-%Y%m%d-%H%M%S.json",&local))return strdup("vestigio-run.json");
    path=strdup(stamp);return path;
}
static int analyze_file(const char *path) {
    FILE *f=fopen(path,"r");char *line=NULL;size_t cap=0;ssize_t n;int matches=0;
    if(!f){fprintf(stderr,"vestigio: no se pudo abrir %s: %s\n",path,strerror(errno));return 2;}
    while((n=getline(&line,&cap,f))>=0){(void)n;matches+=vestigio_analyze_text(line,stdout);}
    free(line);fclose(f);
    if(!matches)puts("  {\"status\":\"unknown\",\"title\":\"Sin patrón reconocido\",\"description\":\"No se encontró un marcador conocido; esto no demuestra que no haya un fallo.\"}");
    return 0;
}
int main(int argc,char **argv) {
    int i=2;const char *output=NULL;char *owned=NULL;VestigioRun run;int rc;
    if(argc<2||strcmp(argv[1],"--help")==0||strcmp(argv[1],"-h")==0){usage(argc<2?stderr:stdout);return argc<2?2:0;}
    if(strcmp(argv[1],"analyze-text")==0){if(argc!=3){usage(stderr);return 2;}return analyze_file(argv[2]);}
    if(strcmp(argv[1],"run")!=0){usage(stderr);return 2;}
    while(i<argc&&strcmp(argv[i],"--")!=0){
        if(strcmp(argv[i],"--output")==0&&i+1<argc){output=argv[i+1];i+=2;}
        else {fprintf(stderr,"vestigio: argumento no reconocido: %s\n",argv[i]);usage(stderr);return 2;}
    }
    if(i<argc&&strcmp(argv[i],"--")==0)++i;
    if(i>=argc){fprintf(stderr,"vestigio: falta el comando a ejecutar\n");usage(stderr);return 2;}
    if(!output){owned=default_filename();output=owned;if(!output){perror("strdup");return 2;}}
    vestigio_run_init(&run);
    if(vestigio_capture(&run,&argv[i])!=0){fprintf(stderr,"vestigio: captura falló: %s\n",strerror(errno));vestigio_run_destroy(&run);free(owned);return 2;}
    if(vestigio_write_json(&run,&argv[i],output)!=0){fprintf(stderr,"vestigio: no se pudo escribir %s: %s\n",output,strerror(errno));vestigio_run_destroy(&run);free(owned);return 2;}
    printf("Registro: %s\n",output);
    printf("Ejecución: PID %ld · %zu eventos · ",run.process_id,run.event_count);
    if(run.has_signal)printf("terminó por señal %d\n",run.term_signal);else printf("código de salida %d\n",run.exit_code);
    puts("Diagnóstico:");(void)vestigio_analyze_run(&run,stdout);
    rc=run.has_signal?128+run.term_signal:run.exit_code;
    vestigio_run_destroy(&run);free(owned);
    return rc>255?255:rc;
}

#define _POSIX_C_SOURCE 200809L
#include "vestigio/vestigio.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void write_string(FILE *f,const char *s) {
    const unsigned char *p=(const unsigned char *)(s?s:""); fputc('"',f);
    for(;*p;++p) switch(*p) {
        case '"':fputs("\\\"",f);break; case '\\':fputs("\\\\",f);break;
        case '\n':fputs("\\n",f);break;case '\r':fputs("\\r",f);break;case '\t':fputs("\\t",f);break;
        default:if(*p<32)fprintf(f,"\\u%04x",*p);else fputc(*p,f);
    }
    fputc('"',f);
}
int vestigio_write_json(const VestigioRun *run,char *const argv[],const char *path) {
    FILE *f=fopen(path,"w"); size_t i;
    if(!f)return -1;
    fputs("{\n  \"schema_version\": 1,\n  \"started_at\": ",f);write_string(f,run->started_at);
    fputs(",\n  \"finished_at\": ",f);write_string(f,run->finished_at);
    fprintf(f,",\n  \"pid\": %ld,\n  \"command\": [",run->process_id);
    for(i=0;argv[i];++i){if(i)fputs(", ",f);write_string(f,argv[i]);}
    fputs("],\n  \"exit_code\": ",f);if(run->has_exit_code)fprintf(f,"%d",run->exit_code);else fputs("null",f);
    fputs(",\n  \"signal\": ",f);if(run->has_signal)fprintf(f,"%d",run->term_signal);else fputs("null",f);
    fputs(",\n  \"events\": [\n",f);
    for(i=0;i<run->event_count;++i){const VestigioEvent *e=&run->events[i];if(i)fputs(",\n",f);fputs("    {\"timestamp\": ",f);write_string(f,e->timestamp);fputs(", \"stream\": ",f);write_string(f,e->stream);fputs(", \"message\": ",f);write_string(f,e->message);fputs("}",f);}
    fputs("\n  ],\n  \"diagnoses\": ",f);(void)vestigio_analyze_run(run,f);fputs("\n}\n",f);
    if(fclose(f)!=0)return -1;
    return 0;
}

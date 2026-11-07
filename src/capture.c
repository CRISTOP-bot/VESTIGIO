#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "vestigio/vestigio.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void timestamp_now(char dst[40]) {
    struct timespec ts;
    struct tm tm_value;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
        localtime_r(&ts.tv_sec, &tm_value) == NULL) {
        snprintf(dst, 40, "unknown-time");
        return;
    }
    (void)strftime(dst, 32, "%Y-%m-%dT%H:%M:%S", &tm_value);
    (void)snprintf(dst + strlen(dst), 40 - strlen(dst), ".%03ld%+03ld:%02ld",
                   ts.tv_nsec / 1000000L,
                   tm_value.tm_gmtoff / 3600L,
                   labs(tm_value.tm_gmtoff / 60L) % 60L);
}

void vestigio_run_init(VestigioRun *run) {
    memset(run, 0, sizeof(*run));
    run->exit_code = -1;
    run->term_signal = 0;
}

void vestigio_run_destroy(VestigioRun *run) {
    size_t i;
    for (i = 0; i < run->event_count; ++i) free(run->events[i].message);
    free(run->events);
    vestigio_run_init(run);
}

static int add_event(VestigioRun *run, const char *stream, const char *message) {
    VestigioEvent *event;
    char *copy;
    if (run->event_count == run->event_capacity) {
        size_t next = run->event_capacity ? run->event_capacity * 2 : 16;
        VestigioEvent *grown;
        if (next < run->event_capacity || next > SIZE_MAX / sizeof(*grown)) {
            errno = ENOMEM;
            return -1;
        }
        grown = realloc(run->events, next * sizeof(*grown));
        if (!grown) return -1;
        run->events = grown;
        run->event_capacity = next;
    }
    copy = strdup(message);
    if (!copy) return -1;
    event = &run->events[run->event_count++];
    timestamp_now(event->timestamp);
    (void)snprintf(event->stream, sizeof(event->stream), "%s", stream);
    event->message = copy;
    return 0;
}

static int emit_lines(VestigioRun *run, const char *stream, char *buffer,
                      size_t *used, size_t capacity, const char *bytes, size_t length,
                      int flush) {
    size_t i;
    for (i = 0; i < length; ++i) {
        if (bytes[i] == '\n') {
            buffer[*used] = '\0';
            if (add_event(run, stream, buffer) != 0) return -1;
            *used = 0;
        } else {
            if (*used + 1 >= capacity) {
                buffer[*used] = '\0';
                if (add_event(run, stream, buffer) != 0) return -1;
                *used = 0;
            }
            buffer[(*used)++] = bytes[i];
        }
    }
    if (flush && *used) {
        buffer[*used] = '\0';
        if (add_event(run, stream, buffer) != 0) return -1;
        *used = 0;
    }
    return 0;
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int vestigio_capture(VestigioRun *run, char *const argv[]) {
    int out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1};
    pid_t child;
    int out_open = 1, err_open = 1, status = 0, wait_done = 0, failed = 0;
    char out_buffer[8192], err_buffer[8192];
    size_t out_used = 0, err_used = 0;
    if (!run || !argv || !argv[0]) { errno = EINVAL; return -1; }
    vestigio_run_init(run);
    timestamp_now(run->started_at);
    if (pipe(out_pipe) || pipe(err_pipe)) goto fail;
    child = fork();
    if (child < 0) goto fail;
    if (child == 0) {
        int nullfd = open("/dev/null", O_RDONLY);
        if (nullfd >= 0) { (void)dup2(nullfd, STDIN_FILENO); if (nullfd > STDERR_FILENO) close(nullfd); }
        (void)close(out_pipe[0]); (void)close(err_pipe[0]);
        (void)dup2(out_pipe[1], STDOUT_FILENO); (void)dup2(err_pipe[1], STDERR_FILENO);
        (void)close(out_pipe[1]); (void)close(err_pipe[1]);
        execvp(argv[0], argv);
        {
            int exec_error = errno;
            dprintf(STDERR_FILENO, "VESTIGIO: execvp(%s): %s\n", argv[0], strerror(exec_error));
            _exit(exec_error == ENOENT ? 127 : 126);
        }
    }
    run->process_id = (long)child;
    (void)close(out_pipe[1]); out_pipe[1] = -1;
    (void)close(err_pipe[1]); err_pipe[1] = -1;
    if (set_nonblock(out_pipe[0]) != 0 || set_nonblock(err_pipe[0]) != 0) { failed = 1; goto drain; }
    while (out_open || err_open) {
        struct pollfd fds[2];
        nfds_t n = 0;
        int rc;
        if (out_open) { fds[n].fd=out_pipe[0]; fds[n].events=POLLIN|POLLHUP|POLLERR; fds[n].revents=0; ++n; }
        if (err_open) { fds[n].fd=err_pipe[0]; fds[n].events=POLLIN|POLLHUP|POLLERR; fds[n].revents=0; ++n; }
        do { rc = poll(fds, n, -1); } while (rc < 0 && errno == EINTR);
        if (rc < 0) { failed=1; break; }
        for (nfds_t i=0; i<n; ++i) {
            int fd=fds[i].fd, isout=(fd==out_pipe[0]);
            if (!(fds[i].revents & (POLLIN|POLLHUP|POLLERR))) continue;
            for (;;) {
                char chunk[4096];
                ssize_t got=read(fd,chunk,sizeof(chunk));
                if (got>0) {
                    if (emit_lines(run,isout?"stdout":"stderr",isout?out_buffer:err_buffer,
                                   isout?&out_used:&err_used,sizeof(out_buffer),chunk,(size_t)got,0)!=0) {
                        failed=1; break;
                    }
                    continue;
                }
                if (got==0) {
                    if (emit_lines(run,isout?"stdout":"stderr",isout?out_buffer:err_buffer,
                                   isout?&out_used:&err_used,sizeof(out_buffer),"",0,1)!=0) failed=1;
                    if (isout) { close(out_pipe[0]); out_pipe[0]=-1; out_open=0; }
                    else { close(err_pipe[0]); err_pipe[0]=-1; err_open=0; }
                } else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) {
                    failed=1;
                }
                break;
            }
            if (failed) break;
        }
        if (failed) break;
    }
 drain:
    if (failed && child > 0) (void)kill(child, SIGKILL);
    if (out_pipe[0]>=0) close(out_pipe[0]);
    if (err_pipe[0]>=0) close(err_pipe[0]);
    if (child > 0) {
        while (waitpid(child,&status,0)<0) { if (errno==EINTR) continue; failed=1; break; }
        wait_done=1;
    }
    (void)wait_done;
    timestamp_now(run->finished_at);
    if (!failed && WIFEXITED(status)) { run->has_exit_code=1; run->exit_code=WEXITSTATUS(status); }
    else if (!failed && WIFSIGNALED(status)) { run->has_signal=1; run->term_signal=WTERMSIG(status); }
    else if (failed) errno=EIO;
    if (failed) return -1;
    return 0;
 fail:
    {
        int saved=errno;
        if (out_pipe[0] >= 0) close(out_pipe[0]);
        if (out_pipe[1] >= 0) close(out_pipe[1]);
        if (err_pipe[0] >= 0) close(err_pipe[0]);
        if (err_pipe[1] >= 0) close(err_pipe[1]);
        errno=saved;
        return -1;
    }
}

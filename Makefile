CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
SOURCES = src/main.c src/capture.c src/analyzers.c src/store.c

.PHONY: all test clean
all: vestigio
vestigio: $(SOURCES) include/vestigio/vestigio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@
test: tests/test_capture.sh vestigio
	sh tests/test_capture.sh ./vestigio
clean:
	rm -f vestigio

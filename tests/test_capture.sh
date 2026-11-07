#!/bin/sh
set -eux
BIN=${1:-./vestigio}
TMP=${TMPDIR:-/tmp}/vestigio-test-$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP"
# Capture stdout and stderr and retain the real exit code in the record.
set +e
"$BIN" run --output "$TMP/run.json" -- sh -c 'printf "hello\\n"; printf "std::bad_alloc\\n" >&2; exit 7' >"$TMP/cli.out"
rc=$?
set -e
[ "$rc" -eq 7 ]
grep -q '"exit_code": 7' "$TMP/run.json"
grep -q '"stream": "stdout"' "$TMP/run.json"
grep -q '"stream": "stderr"' "$TMP/run.json"
grep -q 'Posible fallo de asignación de memoria' "$TMP/run.json"
# SIGSEGV is recorded as a signal; its root cause is not overclaimed.
set +e
"$BIN" run --output "$TMP/segv.json" -- sh -c 'kill -SEGV $$' >/dev/null
rc=$?
set -e
[ "$rc" -eq 139 ]
grep -q '"signal": 11' "$TMP/segv.json"
grep -q '"status":"probable"' "$TMP/segv.json"
grep -q 'no identifica la instrucción' "$TMP/segv.json"
# Offline logs are analyzed conservatively.
printf 'error while loading shared libraries: libmissing.so\n' >"$TMP/log.txt"
"$BIN" analyze-text "$TMP/log.txt" | grep -q 'Biblioteca compartida no encontrada'
echo 'OK: 3 integration checks'

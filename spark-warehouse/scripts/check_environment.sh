#!/usr/bin/env bash
set -eu

if [ -f "$HOME/.hadoop_env" ]; then
  # shellcheck disable=SC1091
  . "$HOME/.hadoop_env"
fi

failures=0

check_command() {
  label="$1"
  command_name="$2"
  if command -v "$command_name" >/dev/null 2>&1; then
    printf 'OK   %-12s %s\n' "$label" "$(command -v "$command_name")"
  else
    printf 'MISS %-12s command %s not found\n' "$label" "$command_name"
    failures=$((failures + 1))
  fi
}

check_command Java java
check_command Hadoop hadoop
check_command HDFS hdfs
check_command YARN yarn
check_command Spark spark-submit
check_command PySpark pyspark
check_command Python python3

if command -v python3 >/dev/null 2>&1; then
  python3 - <<'PY' || failures=$((failures + 1))
import importlib.util
for module in ('pyspark', 'flask', 'yaml'):
    print(('OK  ' if importlib.util.find_spec(module) else 'MISS'), 'python module', module)
PY
fi

if command -v jps >/dev/null 2>&1; then
  printf '\nJava services:\n'
  timeout 10 jps || printf 'WARN jps did not respond within 10 seconds\n'
fi

if command -v hdfs >/dev/null 2>&1; then
  printf '\nHDFS report summary:\n'
  timeout 15 hdfs dfsadmin -report 2>/dev/null | sed -n '1,16p' || failures=$((failures + 1))
fi

if command -v yarn >/dev/null 2>&1; then
  printf '\nYARN nodes:\n'
  timeout 15 yarn node -list 2>/dev/null || failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
  printf '\nEnvironment check failed with %s missing or unavailable item(s).\n' "$failures"
  exit 1
fi

printf '\nEnvironment check passed.\n'

# Source this file from Bash; Spark and Hadoop keep separate Java runtimes.
export PROJECT_ROOT="${PROJECT_ROOT:-$HOME/MapForECarCharger}"
export SPARK_VENV="${SPARK_VENV:-$HOME/apps/map-for-ecar-spark42}"
if [[ -z ${JAVA_HOME:-} ]]; then
  for candidate in /usr/lib/jvm/java-17-openjdk-amd64 "$HOME"/.local/opt/jdk17* "$HOME"/.local/opt/jdk8*; do
    [[ -x "$candidate/bin/java" ]] || continue
    JAVA_HOME=$candidate
    break
  done
fi
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"
export HADOOP_HOME="${HADOOP_HOME:-$HOME/apps/hadoop-3.2.1}"
export SPARK_HOME="$($SPARK_VENV/bin/python -c 'import pathlib,pyspark; print(pathlib.Path(pyspark.__file__).parent)')"
unset PYTHONPATH
export PYSPARK_PYTHON="$SPARK_VENV/bin/python"
export PYSPARK_DRIVER_PYTHON="$SPARK_VENV/bin/python"
if [[ -n ${NODE_HOME:-} ]]; then
  export PATH="$NODE_HOME/bin:$PATH"
elif ! command -v node >/dev/null 2>&1; then
  for candidate in "$HOME"/.local/opt/node-v*/bin; do
    [[ -x "$candidate/node" ]] || continue
    export PATH="$candidate:$PATH"
    break
  done
fi
export PATH="$SPARK_VENV/bin:$HOME/.local/node24/node_modules/.bin:$JAVA_HOME/bin:$HADOOP_HOME/bin:$HADOOP_HOME/sbin:$SPARK_HOME/bin:$PATH"

# Source this file from Bash; Spark and Hadoop keep separate Java runtimes.
export PROJECT_ROOT="${PROJECT_ROOT:-$HOME/MapForECarCharger}"
export SPARK_VENV="${SPARK_VENV:-$HOME/apps/map-for-ecar-spark42}"
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export HADOOP_HOME="${HADOOP_HOME:-$HOME/apps/hadoop-3.2.1}"
export SPARK_HOME="$($SPARK_VENV/bin/python -c 'import pathlib,pyspark; print(pathlib.Path(pyspark.__file__).parent)')"
unset PYTHONPATH
export PYSPARK_PYTHON="$SPARK_VENV/bin/python"
export PYSPARK_DRIVER_PYTHON="$SPARK_VENV/bin/python"
export PATH="$SPARK_VENV/bin:$HOME/.local/node24/node_modules/.bin:$JAVA_HOME/bin:$HADOOP_HOME/bin:$HADOOP_HOME/sbin:$SPARK_HOME/bin:$PATH"

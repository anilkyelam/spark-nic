#!/bin/bash

#
# Run spark sort application
#

SIZE=10000      # input size, 10 GB
CACHE=1         # Cache file on HDFS


# Prepare
SCRIPT_DIR=$(dirname "$0")
bash ${SCRIPT_DIR}/prepare.sh ${SIZE} ${CACHE}

# Start Spark webserver
bash ${SPARK_HOME}/sbin/start-history-server.sh

# Run
CLASS=SortNoDisk
${SPARK_HOME}/bin/spark-submit \
    --num-executors 2 --executor-cores 6 --executor-memory 30g --driver-cores 1 --driver-memory 5g    \
    --class PowerMeasurements.${CLASS} apps/sort/target/scala-2.12/sparksort_2.12-0.1.jar yarn \
    /user/ayelam/${SIZE}mb.input /user/ayelam/sort_outputs/${SIZE}mb.output /user/ayelam/taskstats_${unixstamp}
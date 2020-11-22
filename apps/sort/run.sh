#!/bin/bash

#
# Run spark sort application
#

DEFAULT_SORT_SIZE=1000
usage="\n
-r, --rebuild \t\t rebuild app source before deploying it\n
-sz, --size \t\t sort input size. default is $DEFAULT_SORT_SIZE mb\n
-sv, --save \t\t save this run for future\n
-n, --name \t\t optional name for this saved run; default based on current time\n
-d, --desc \t\t optional description for this saved run\n
-h, --help \t\t this usage information message\n"

# Parse command line arguments
for i in "$@"
do
case $i in

    -r | --rebuild)     # Rebuild sort app source 
    REBUILD="-r"
    ;;

    -sz=*|--size=*)      # Optional sort data size in MB; Default is $DEFAULT_SORT_SIZE mb
    SIZE="${i#*=}"
    ;;
    
    -sv | --save)      # Save the output for further analysis
    SAVE=1
    ;;

    -n=*|--name=*)      # Optional name for this saved run; Default based on current time.
    NAME="${i#*=}"
    ;;

    -d=*|--desc=*)      # Optional description for this saved run
    DESC="${i#*=}"
    ;;

    -h | *)             # help or unknown option
    echo "Unknown Option: $i"
    echo -e $usage
    exit
    ;;
esac
done

# Constants
SPARKLOG_RELPATH="hadoop/logs/userlogs"
SIZE=${SIZE:-`echo $DEFAULT_SORT_SIZE`}     # input size, 1 GB
CACHE="-c"                                  # Cache file on HDFS

# Check hdfs is running
hdfs dfsadmin -report  &> /dev/null
if [[ $? -ne 0 ]]; then
    echo "ERROR! HDFS cluster not running or in a bad state. Exiting!"
    exit -1
fi

# Prepare
SCRIPT_DIR=$(dirname "$0")
bash ${SCRIPT_DIR}/prepare.sh -s=${SIZE} ${CACHE} ${REBUILD}

# Restart Spark webserver
bash ${SPARK_HOME}/sbin/stop-history-server.sh
bash ${SPARK_HOME}/sbin/start-history-server.sh

# Run sorting
CLASS=SortNoDisk
${SPARK_HOME}/bin/spark-submit \
    --num-executors 2 --executor-cores 1 --executor-memory 30g --driver-cores 1 --driver-memory 5g    \
    --class PowerMeasurements.${CLASS} apps/sort/target/scala-2.12/sparksort_2.12-0.1.jar yarn \
    /user/ayelam/${SIZE}mb.input /user/ayelam/sort_outputs/${SIZE}mb.output /user/ayelam/taskstats_${unixstamp} 2>&1 | tee spark.log

# CLASS=TeraSort
# ${SPARK_HOME}/bin/spark-submit \
#     --num-executors 2 --executor-cores 1 --executor-memory 30g --driver-cores 1 --driver-memory 5g    \
#     --class PowerMeasurements.${CLASS} apps/sort/target/scala-2.12/sparksort_2.12-0.1.jar yarn \
#     /user/ayelam/${SIZE}mb.input 2>&1 | tee spark.log
# echo "RESULT CODE: $?"

# # Save output
if [[ $SAVE ]]; then
    # Create output dir for this run
    APP_DIR=$(dirname $SCRIPT_DIR)
    REPO_DIR=$(dirname $APP_DIR)       # TODO: Is this the best way to get to repo root?
    OUTDIR="${REPO_DIR}/out"
    mkdir -p ${OUTDIR}
    if [[ -z "$NAME" ]]; then   NAME=$(date +'%m-%d-%H-%M');     fi
    RUNDIR="$OUTDIR/$NAME"
    mkdir -p ${RUNDIR}

    # Copy relevant logs
    mkdir -p ${RUNDIR}/data
    cp spark.log ${RUNDIR}/data
    appname=$(cat spark.log | egrep -o  "(application_[0-9]+_[0-9]+)" | head -n1)
    sparklogs=${REPO_DIR}/${SPARKLOG_RELPATH}/${appname}
    cp -r ${sparklogs}/* ${RUNDIR}/data/

    if [[ $DESC ]]; then    echo "$DESC" > ${RUNDIR}/desc;   fi         # save description
    cp ${SPARK_HOME}/etc/conf/spark-defaults.conf ${RUNDIR}/            # save spark conf for the run
    echo "Copied logs at: ${RUNDIR}/"
fi

# rm spark.log
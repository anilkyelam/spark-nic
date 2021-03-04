#!/bin/bash

#
# Run a hibench workload
#

usage="\n
-r, --rebuild \t\t rebuild app source before deploying it\n
-wt, --wtype \t\t required hibench workload type. valid: dal, graph, micro, ml, sql, streaming, websearch\n
-wn, --wname \t\t required hibench workload name. valid: varies by wtype; for e.g., pagerank, nweight for graph type\n
-sv, --save \t\t save this run for future\n
-n, --name \t\t optional name for this saved run; default based on current time\n
-d, --desc \t\t optional description for this saved run\n
-h, --help \t\t this usage information message\n"

# Parse command line arguments
for i in "$@"
do
case $i in
    -r | --rebuild)         # Rebuild source 
    REBUILD=1
    RFLAG="--rebuild"
    ;;

    -wt=*|--wtype=*)        # Workload type: dal, graph, micro, ml, sql, streaming, websearch
    WTYPE="${i#*=}"
    ;;

    -wn=*|--wname=*)        # Workload name. Varies by wtype; for e.g., pagerank, nweight for graph type
    WNAME="${i#*=}"
    ;;
    
    -sv | --save)           # Save the output for further analysis
    SAVE=1
    ;;

    -n=*|--name=*)          # Optional name for this saved run; Default based on current time.
    NAME="${i#*=}"
    ;;

    -d=*|--desc=*)          # Optional description for this saved run
    DESC="${i#*=}"
    ;;

    -h | --help)
    echo -e $usage
    exit
    ;;

    *)                      # unknown option
    echo "Unkown Option: $i"
    echo -e $usage
    exit
    ;;
esac
done

# Constants
GUID=$(date +'%m-%d-%H-%M')
SPARKLOG_RELPATH="hadoop/logs/userlogs"
SCRIPT_DIR=$(dirname "$0")
HIBENCH_DIR=${SCRIPT_DIR}/HiBench
TMP_DIR=${SCRIPT_DIR}/tmp
TMP_OUTDIR=${TMP_DIR}/${GUID}


# Check parameters
dir=$(dirname "$0")
ls ${HIBENCH_DIR}/bin/workloads/ | grep -e "^${WTYPE}$" &> /dev/null
if [[ $? -ne 0 ]]; then 
    echo "Specify valid spark workload type with -wt=. Valid values are:" `ls ${HIBENCH_DIR}/bin/workloads`
    exit 1
fi

ls ${HIBENCH_DIR}/bin/workloads/${WTYPE}/ | grep -e "^${WNAME}$" &> /dev/null
if [[ $? -ne 0 ]]; then 
    echo "Specify valid spark workload name with -wn=. Valid values for ${WTYPE} type are:" `ls ${HIBENCH_DIR}/bin/workloads/${WTYPE}`
    exit 1
fi

if [ -d ${SCRIPT_DIR}/bin/workloads/${WTYPE}/${WNAME}/spark/run.sh ]; then
    echo "ERROR! No support for this workload on spark."
    exit 1
fi

# Check hdfs is running
hdfs dfsadmin -report  &> /dev/null
if [[ $? -ne 0 ]]; then
    echo "ERROR! HDFS cluster not running or in a bad state. Exiting!"
    exit -1
fi


# Prepare
bash ${HIBENCH_DIR}/prepare.sh -wt=${WTYPE} -wn=${WNAME} ${RFLAG}

# Restart Spark webserver
bash ${SPARK_HOME}/sbin/stop-history-server.sh
bash ${SPARK_HOME}/sbin/start-history-server.sh


# Prepare tmp directory and set conf to write logs
mkdir -p ${TMP_DIR}
mkdir -p ${TMP_OUTDIR}
sed -i "s/^spark.record.dir.*$/spark.record.dir \/home\/ayelam\/spark-nic\/spark_\/tmp\/${GUID}\//" ${HIBENCH_DIR}/conf/spark.conf       #FIXME: hardcoded path

# Run bench
bash ${HIBENCH_DIR}/bin/workloads/${WTYPE}/${WNAME}/spark/run.sh

# # Save output
if [[ $SAVE ]]; then
    # Create output dir for this run
    OUTDIR="${SCRIPT_DIR}/out"
    mkdir -p ${OUTDIR}
    if [[ -z "$NAME" ]]; then   NAME=${GUID};     fi
    RUNDIR="$OUTDIR/$NAME"
    mkdir -p ${RUNDIR}

    # Copy relevant logs
    mkdir -p ${RUNDIR}/data
    # cp spark.log ${RUNDIR}/data
    cp -r ${TMP_OUTDIR}/* ${RUNDIR}/data/

    if [[ $DESC ]]; then    echo "$DESC" > ${RUNDIR}/desc;   fi         # save description
    cp ${HIBENCH_DIR}/conf/spark.conf ${RUNDIR}/                        # save spark conf for the run
    echo "Copied logs at: ${RUNDIR}/"
fi

# rm spark.log
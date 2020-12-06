#!/bin/bash

#
# Prepares sort application for running on Spark
# Generate input data file for sort application
# and add it to hdfs
#

# Parse command line arguments
for i in "$@"
do
case $i in
    -r | --rebuild)
    REBUILD=1
    ;;

    -s=*|--size=*)      # Input size in MB
    SIZE="${i#*=}"
    ;;
    
    -c | --cache)       # Cache input file in HDFS before running sort
    CACHE_HDFS_FILE=1
    ;;

    -h | --help)
    echo -e $usage
    exit
    ;;

    *)                  # unknown option
    echo "Unkown Option: $i"
    echo -e $usage
    exit
    ;;
esac
done

SIZE_IN_MB=${SIZE:-10000}          # Default is 10GB

dir=$(dirname "$0")
if [[ $REBUILD ]]; then
    pushd "$dir"
    sbt package
    popd "$dir"
fi

# Check hdfs is running
hdfs dfsadmin -report  &> /dev/null
if [[ $? -ne 0 ]]; then
    echo "ERROR! HDFS cluster not running or in a bad state. Exiting!"
    exit -1
fi

echo "Deleting previous outputs saved on hdfs"
hdfs dfs -rm -r -f -skipTrash /user/ayelam/sort_outputs/*


# Adding file to hdfs
echo "Checking if required input file exists"
FOLDER_PATH_HDFS="/user/ayelam"
FILE_PATH_HDFS="${FOLDER_PATH_HDFS}/${SIZE_IN_MB}mb.input"
hdfs dfs -test -e ${FILE_PATH_HDFS}
if [ $? != 0 ]; then
    echo "Input file not found, creating it".
    FILE_PATH_LOCAL="/usr/local/home/ayelam/${SIZE_IN_MB}mb.input"       # Use local disk, avoid NFS

    # Run gensort to generate sort input. It takes count and produces (count*100B) sized file.
    GEN_SORT_COUNT=$((SIZE_IN_MB*10000))
    ${dir}/gensort -b0 ${GEN_SORT_COUNT} ${FILE_PATH_LOCAL}
    # echo ${FILE_PATH_LOCAL}

    # Copy over to hdfs
    hdfs dfs -mkdir -p ${FOLDER_PATH_HDFS}
    hdfs dfs -D dfs.replication=2  -put ${FILE_PATH_LOCAL} ${FILE_PATH_HDFS}
    hdfs dfs -setrep 1 ${FILE_PATH_HDFS}
    
    # Remove local file
    # rm ${FILE_PATH_LOCAL}

    # Test again
    hdfs dfs -test -e ${FILE_PATH_HDFS}
    if [ $? != 0 ]; then
            echo "Could not create input file for spark sort, check errors"
            exit -1
    fi
else
    echo "Input file ${FILE_PATH_HDFS} already exists."
fi


# Adding file to hdfs cache
if [ "${CACHE_HDFS_FILE}" == 1 ]; then
    echo "Checking if the hdfs file is cached"
    hdfs cacheadmin -listDirectives | grep "${FILE_PATH_HDFS}"
    if [ $? != 0 ]; then
            echo "File not in cache, refreshing the cache and adding the file"
            hdfs cacheadmin -removeDirectives -path "/user/ayelam"
            hdfs cacheadmin -addPool cache-pool
            hdfs cacheadmin -addDirective -path "${FILE_PATH_HDFS}" -pool cache-pool
            
            # This initiates caching process. Ideally we would want to wait and check until it finishes, but for now
            # we just wait and hope that it's done. (TODO: Configure this time for larger inputs)
            sleep 300

            hdfs cacheadmin -listDirectives
    else
            echo "File is cached, moving on."
    fi
fi

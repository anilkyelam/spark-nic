#!/bin/bash

#
# Run RDMA server and client
#

# Metadata
CLIENT_HOST=yak-00.sysnet.ucsd.edu
CLIENT_INTF=enp4s0
CLIENT_IP=10.0.0.1
SERVER_HOST=yak-01.sysnet.ucsd.edu
SERVER_INTF=enp129s0
SERVER_IP=10.0.0.2

# Parse command line arguments
for i in "$@"
do
case $i in
    -f|--force)             # force run
    FORCE=1
    ;;

    # -r|--rebuild)           # rebuilds rdma source before using it for testing
    # REBUILD=1
    # ;;
    
    -o=*|--opts=*)          # options for rdma_client program
    OPTS="${i#*=}"
    ;;
     
    -so=*|--srvopts=*)      # options for rdma_server program
    SRV_OPTS="${i#*=}"
    ;;
    
    *)                      # unknown option
    ;;
esac
done

# Make sure to run this script on client host
if [[ "$(hostname)" != "$CLIENT_HOST" ]]; then
    echo "ERROR! Current host $(hostname) is not listed as client"
    exit 1
fi

# Rebuild source
REBUILD=1
CUR_PATH=`realpath $0`
DIR=$(dirname $CUR_PATH)
if [[ $REBUILD ]]; 
then
    pushd ${DIR}
    cmake .
    make clean
    make
    popd
fi

# Setup server on server host (assuming same path on the other machine)
echo "Starting server"
SERVER_PORT=20886
ssh "$SERVER_HOST" "pkill rdma_server"
ssh "$SERVER_HOST" "nohup ${DIR}/bin/rdma_server -p $SERVER_PORT ${SRV_OPTS} &> /dev/null &"
if [[ $? -ne 0 ]]; then 
    echo "ERROR! Setting up RDMA server failed!";
fi
sleep 1

# Run client on the client host 
echo "Running client with options: $OPTS"
# sudo perf stat -e task-clock,cycles,instructions,cache-references,cache-misses 
# taskset 0x4 ${DIR}/bin/rdma_client -a ${SERVER_IP} -p ${SERVER_PORT} ${OPTS}
numactl --physcpubind=2 --membind=0 ${DIR}/bin/rdma_client -a ${SERVER_IP} -p ${SERVER_PORT} ${OPTS}

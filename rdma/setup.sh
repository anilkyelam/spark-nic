#!/bin/bash

#
# Set up and test the RDMA connection
#

# Metadata
CLIENT_HOST=yak-00.sysnet.ucsd.edu
CLIENT_INTF=enp4s0
CLIENT_IP=10.0.0.1
SERVER_HOST=yak-01.sysnet.ucsd.edu
SERVER_INTF=enp129s0
SERVER_IP=10.0.0.2


# Make sure to run this script on client host
if [[ "$(hostname)" != "$CLIENT_HOST" ]]; then
    echo "ERROR! Current host $(hostname) is not listed as client"
    exit 1
fi


# Make sure the switch is not configured with custom openflow rules
echo "Checking for openflow rules on the switch"
pwd=$(cat switch.password)
rules=$(sshpass -p "$pwd" ssh sw100 cli -h '"enable" "show openflow flows"')
num_rules=$(echo "$rules" | grep "in_port" | wc -l)
if [[ $num_rules -ne 0 ]] && [[ $num_rules -ne 2 ]];
then 
    # expecting zero or two rules from Stew. If not, something has changed on his side.
    echo "ERROR! expecting only two custom rules but found $num_rules; double-check to be sure.";
    echo "$rules"
    exit 1
elif [[ $num_rules -eq 2  ]];
then
    # two rules as expected; remove.
    echo "Found 2 openflow rules on the switch; deleting them"
    sshpass -p "$pwd" ssh sw100 cli -h '"enable" "configure terminal" "openflow del-flows 1"'
    sshpass -p "$pwd" ssh sw100 cli -h '"enable" "configure terminal" "openflow del-flows 2"'
else 
    echo "no custom openflow rules on switch; we're good!"
fi


# Setup interfaces
function setup_intf {
    iface=$1
    ipaddr=$2
    sudo ip link set $iface up                          #set the link up
    sudo ifconfig $iface $ipaddr netmask 255.255.0.0    #turn the interface on and configure ip
}

# NOTE: May wanna enable passwordless sudo for ip, ifconfig, etc on all hosts
# setup_intf "$SERVER_INTF" "$SERVER_IP"
ssh "$SERVER_HOST" "$(typeset -f setup_intf); setup_intf $SERVER_INTF $SERVER_IP" -S
ssh "$CLIENT_HOST" "$(typeset -f setup_intf); setup_intf $CLIENT_INTF $CLIENT_IP" -S
echo "Interfaces set up!"


# Test RDMA connection
echo "Testing RDMA connection"

# Use rdma-example repo
CUR_PATH=`realpath $0`
DIR=$(dirname $CUR_PATH)
pushd ${DIR}
cmake .
make clean
make
popd

# Setup server on server host (assuming same path on the other machine)
echo "Starting server"
SERVER_PORT=20886
ssh "$SERVER_HOST" "pkill rdma_server"
ssh "$SERVER_HOST" "nohup ${DIR}/bin/rdma_server -p $SERVER_PORT &> /dev/null &"
if [[ $? -ne 0 ]]; then 
    echo "ERROR! Setting up RDMA server failed!";
fi
sleep 1

# Run client on the client host 
echo "Running client"
${DIR}/bin/rdma_client -a ${SERVER_IP} -p ${SERVER_PORT} --simple
if [[ $? -ne 0 ]]; then     echo "ERROR! RDMA CONNECTION UNSUCCESFUL!";
else                        echo "RDMA CONNECTION SUCCESFUL!";    fi

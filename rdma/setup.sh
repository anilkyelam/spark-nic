#!/bin/bash

#
# Set up and test the RDMA connection
#

# Metadata
SERVER_HOST=yak-00.sysnet.ucsd.edu
SERVER_INTF=enp4s0
SERVER_IP=10.0.0.1
CLIENT_HOST=yak-01.sysnet.ucsd.edu
CLIENT_INTF=enp129s0
CLIENT_IP=10.0.0.2


# Make sure to run this script on server host
if [[ "$(hostname)" != "$SERVER_HOST" ]]; then
    echo "ERROR! Current host $(hostname) is not listed as server"
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
setup_intf "$SERVER_INTF" "$SERVER_IP"
ssh "$CLIENT_HOST" "$(typeset -f setup_intf); setup_intf $CLIENT_INTF $CLIENT_IP" -S
echo "Interfaces set up!"


# Test RDMA connection
echo "Testing RDMA connection"
# git submodule init      # so that rdma-example repo is present

# Use rdma-example repo
REPO_PATH=$(realpath learn/rdma-example/)
pushd ${REPO_PATH}
cmake .
make clean
make

# Setup server
SERVER_PORT=20886
pkill rdma_server
echo "Starting server"
${REPO_PATH}/bin/rdma_server -p $SERVER_PORT &
sleep 1

# Run client on the client host (assuming same path on the other server)
echo "Running client"
ssh "$CLIENT_HOST" "${REPO_PATH}/bin/rdma_client -a ${SERVER_IP} -p ${SERVER_PORT} -s teststring"
if [[ $? -ne 0 ]]; then     echo "ERROR! RDMA CONNECTION UNSUCCESFUL!";
else                        echo "RDMA CONNECTION SUCCESFUL!";    fi

# Kill background processes
pkill rdma_server
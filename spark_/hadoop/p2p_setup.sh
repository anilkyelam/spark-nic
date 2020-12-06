#!/bin/bash

#
# Sets up the p2p connections between machines (i.e., no switch)
# Just add the hosts, their interfaces and which pairs are connected
#


# Network endpoints information
# One entry for each host interface per line
#   Id  HostName    HostId  Interface   MAC    
read -r -d '' ENDPOINTS << EOM
    1   spark-29    1       enp5s0f0    3c:fd:fe:aa:c6:90
    2   spark-29    1       enp5s0f1    3c:fd:fe:aa:c6:91
    3   spark-30    2       enp5s0f0    3c:fd:fe:aa:c5:98
    4   spark-30    2       enp5s0f1    3c:fd:fe:aa:c5:99
EOM

# Network topology information 
# One entry per wire: provide endpoint ids
#   Id  Ep1 Ep2
read -r -d '' TOPOLOGY << EOM
    1   2   4
EOM

# Current server
hostname=`hostname | cut -d. -f1`
echo "Configuring interfaces on $hostname"

# Get endpoints for this server
endpoints=$(echo "$ENDPOINTS" | grep "$hostname")
count=$(echo "$endpoints" | wc -l)
if [[ $count -eq 0 ]]; then
    echo "Host $hostname has no endpoints listed."
    exit 1
fi 


# For each wire
while IFS= read -r wire; do
    wireid=$(echo $wire | awk '{print $1}')
    ep1=$(echo $wire    | awk '{print $2}')
    ep2=$(echo $wire    | awk '{print $3}')
    # echo $wireid, $ep1, $ep2

    # Does any of the endpoints belong to this host?
    ept1=$(echo "$ENDPOINTS" | egrep "^\s*${ep1}\s+${hostname}\s+")
    ept2=$(echo "$ENDPOINTS" | egrep "^\s*${ep2}\s+${hostname}\s+")
    ept=${ept2:-"$ept1"}
    if [[ -z $ept ]]; then 
        continue
    fi

    # If an endpoint exists
    id=$(echo $ept | awk '{print $1}')
    host=$(echo $ept | awk '{print $2}')
    hostid=$(echo $ept | awk '{print $3}')
    intf=$(echo $ept | awk '{print $4}')
    mac=$(echo $ept | awk '{print $5}')
    # echo $id, $host, $intf, $ipv4, $mac

    # Each wire should have its own subnet (with proper mask) or "ip route" is 
    # gonna cause issues when there are multiple wires on single host.
    ip=10.0.$wireid.$hostid
    # echo "Setting up $host:$intf with ip: $ip"
    sudo ip link set $intf up                               # wake it UP
    sudo ifconfig $intf $ip netmask 255.255.255.0           # set ip 

    # Get remote endpoint and add an arp entry
    remote_id=$(echo $wire | awk '{ if ($2 == '$id') print $3; else print $2; }')
    remote_ep=$(echo "$ENDPOINTS" | egrep "^\s*${remote_id}\s+")
    remote_hostid=$(echo $remote_ep| awk '{print $3}')
    remote_mac=$(echo $remote_ep| awk '{print $5}')
    remote_ip=10.0.$wireid.${remote_hostid}
    # echo "Adding arp entry on $intf: $remote_ip $remote_mac"
    sudo arp -s $remote_ip $remote_mac
    echo "WIRE $wireid Endpoint IPs: Mine - $ip, Remote - $remote_ip"
    echo "Ensure that 10.0.$wireid.0/24 is whitelisted in iptables!"

done <<< "$TOPOLOGY"

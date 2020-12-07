#!/bin/bash

#
# Sets up hdfs + yarn cluser
#


# Undo any custom changes to /etc/hosts file
sudo cp /etc/hosts_default /etc/hosts

# Set up network
# Current setup is a p2p connection b/w spark-29 and spark-30
dir=$(dirname "$0")
dir=$(realpath "$dir")
bash $dir/p2p_setup.sh 
ssh spark-30 -t "bash $dir/p2p_setup.sh"  # assuming that the other server has the same folder structure.

# Add custom hosts file
ssh spark-30 -t "sudo cp $dir/hosts /etc/hosts"  # assuming that the other server has the same folder structure.
sudo cp $dir/hosts /etc/hosts

# Check connection
ping -c 1 spark-29.sysnet.ucsd.edu
ping -c 1 spark-30.sysnet.ucsd.edu

# Start cluster
start-dfs.sh
start-yarn.sh

#!/bin/bash

#
# Cleans hdfs + yarn cluser
#

usage="\n
-f, --force \t force stop the cluster\n
-h, --help \t this usage information message\n"

# Parse command line arguments
for i in "$@"
do
case $i in
    -f | --force)
    FORCE=1
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

# Stop cluster
num_proc=$(jps | wc -l)
if [[ $num_proc -gt 1 ]] || [[ "$FORCE" ]]; 
then 
    stop-yarn.sh
    stop-dfs.sh
fi

# Undo any custom changes to /etc/hosts file on all nodes
ssh spark-30 -t "sudo cp /etc/hosts_default /etc/hosts"
sudo cp /etc/hosts_default /etc/hosts
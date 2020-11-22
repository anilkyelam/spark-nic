#
# Set up iptables policy for hdfs/spark cluster nodes
# (Hadoop and YARN expose a number of ports publicly, guarding them is important!)
# This script allows inbound connections only for a limited number of IPs
#

# sudo iptables -S
sudo iptables -P INPUT ACCEPT
sudo iptables -P FORWARD ACCEPT
sudo iptables -P OUTPUT ACCEPT
sudo iptables -N LOGGING
sudo iptables -A INPUT -p icmp -m icmp --icmp-type 8 -m state --state NEW -j ACCEPT
sudo iptables -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT

sudo iptables -A INPUT -s 137.110.222.0/28 -j ACCEPT        # Sysnet infra
sudo iptables -A INPUT -s 127.0.0.1/32 -j ACCEPT
sudo iptables -A INPUT -s 127.0.0.1/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.80/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.79/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.66/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.67/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.68/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.69/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.70/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.71/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.72/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.73/32 -j ACCEPT
sudo iptables -A INPUT -s 137.110.222.74/32 -j ACCEPT

sudo iptables -A INPUT -s 137.110.222.46/32 -j ACCEPT       # b09-26.sysnet.ucsd.edu
sudo iptables -A INPUT -s 169.228.66.227/32 -j ACCEPT       # spark-29.sysnet.ucsd.edu
sudo iptables -A INPUT -s 169.228.66.228/32 -j ACCEPT       # spark-30.sysnet.ucsd.edu
sudo iptables -A INPUT -s 137.110.222.168/32 -j ACCEPT      # ayelam.sysnet.ucsd.edu
sudo iptables -A INPUT -s 10.0.1.0/24       -j ACCEPT       # Spark nodes local subnet

sudo iptables -A INPUT -j LOGGING
sudo iptables -A INPUT -j DROP

sudo iptables -A OUTPUT -d 137.110.222.0/28 -j ACCEPT
sudo iptables -A OUTPUT -d 127.0.0.1/32 -j ACCEPT
sudo iptables -A OUTPUT -d 127.0.0.1/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.80/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.79/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.66/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.67/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.68/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.69/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.70/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.71/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.72/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.73/32 -j ACCEPT
sudo iptables -A OUTPUT -d 137.110.222.74/32 -j ACCEPT

sudo iptables -A OUTPUT -d 137.110.222.46/32 -j ACCEPT       # b09-26.sysnet.ucsd.edu
sudo iptables -A OUTPUT -d 169.228.66.227/32 -j ACCEPT       # spark-29.sysnet.ucsd.edu
sudo iptables -A OUTPUT -d 169.228.66.228/32 -j ACCEPT       # spark-30.sysnet.ucsd.edu
sudo iptables -A OUTPUT -d 137.110.222.168/32 -j ACCEPT      # ayelam.sysnet.ucsd.edu
sudo iptables -A OUTPUT -d 10.0.1.0/24 -j ACCEPT             # Spark nodes local subnet

sudo iptables -A LOGGING -m limit --limit 2/min -j LOG --log-prefix "IPTables-Dropped: "
sudo iptables -S
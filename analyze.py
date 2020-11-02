#
# Analyze results (errors) from Lambda Neighbor Discovery run
#

import argparse
import csv
import os
from itertools import combinations 
import re
import operator
import glob
import math 
from shutil import copyfile
import numpy as np


# Constants

# Structs
class Record:
    mapid = None
    idx = None
    addr = None
    addrk = None
    addrv = None

def main():
    appid = "application_1604303541584_0001"

    # Get all data
    files = "tmp/logs/saved/{0}/container*/stdout".format(appid)
    records = []
    for filename in glob.glob(files):
        with open(filename, 'r') as file:                                                                                                                                                                                                                                             
            for line in file:
                vals = line.split(',')
                if len(vals) == 6: 
                    r = Record()
                    r.mapid = int(vals[0])
                    r.idx = int(vals[1])
                    r.addr = int(vals[2], base=16)
                    r.addrk = int(vals[3], base=16)
                    r.addrv = int(vals[4], base=16)
                    records.append(r)
                    # print(vals)
                    # print(mapId, recordId, base, base1, base2)
        print(len(records))

    # Map tasks
    print(set([r.mapid for r in records]))

    # Save them for plotting
    outfile = "tmp.csv"
    prev_addr = None
    with open(os.path.join("", outfile), 'w') as csvfile:
        first = True
        for r in [r for r in records if r.mapid == 80]:
            if first:
                fieldnames = ["idx", "address", "addressk", "addressv", "offset1", "offset2","offset3"]
                writer = csv.writer(csvfile)
                writer.writerow(fieldnames)
                first = False
            writer.writerow([r.idx, r.addr, r.addrk, r.addrv, r.addrk-r.addr, r.addrv-r.addr, 0 if prev_addr is None else r.addr - prev_addr])
            prev_addr = r.addr
    

if __name__ == "__main__":
    main()
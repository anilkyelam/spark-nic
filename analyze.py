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


# check for outliers in a list of numbers
def find_outliers(array):
    OUTLIER_THRESHOLD = 4   # standard deviations
    mean = np.mean(array)
    std = np.std(array)
    outliers = []
    for i, el in enumerate(array):
        if abs(el - mean) * 1.0 / std > OUTLIER_THRESHOLD:
            outliers.append(i)
    return outliers


def main():
    # Parse and validate args
    parser = argparse.ArgumentParser("Analyze spark logs")
    parser.add_argument('-i', '--expname', action='store', help='Name of the experiment run, used to look for data under out/<expname>', required=True)
    args = parser.parse_args()
        
    expdir = os.path.join("out", args.expname)
    datadir = os.path.join(expdir, "data")
    if not os.path.exists(datadir):
        print("ERROR. Data not found at {0}".format(expdir))
        return -1

    # Get all data
    datadir = os.path.abspath(datadir)
    files = "{0}/container*/stdout".format(datadir)
    records = []
    for filename in glob.glob(files):
        with open(filename, 'r') as file:                                                                                                                                                                                                                                             
            for line in file:
                vals = line.split(',')
                if len(vals) == 6: 
                    r = Record()
                    r.mapid = int(vals[0])
                    r.idx = int(vals[1])
                    r.addr = int(vals[2])
                    r.addrk = int(vals[3])
                    r.addrv = int(vals[4])
                    records.append(r)
                    # print(vals)
                    # print(mapId, recordId, base, base1, base2)
        # print(len(records))

    # Map tasks
    maps = set([r.mapid for r in records])
    print("Available map tasks: " + str(maps))

    # Save them for plotting
    for mapId in maps:
        datafile = "memdata{0}.csv".format(mapId)
        outfile = os.path.join(expdir, datafile)
        prev_addr = None

        # Check for outliers
        records_scoped = [r for r in records if r.mapid == mapId]
        outliers = find_outliers([r.addr for r in records_scoped])
        outliers += find_outliers([r.addrk for r in records_scoped])
        outliers += find_outliers([r.addrv for r in records_scoped])
        # print([(i, records[i].addr, records[i].addrk, records[i].addrv) for i in set(outliers)])
        outlier_records = [records_scoped[i] for i in set(outliers)]
        for r in outlier_records:
            records_scoped.remove(r)
        print("Removed {0} outliers.".format(len(outlier_records)))

        with open(os.path.join("", outfile), 'w') as csvfile:
            first = True
            for r in records_scoped:
                if first:
                    fieldnames = ["idx", "address", "addressk", "addressv", "offsetk", "offsetv","offset"]
                    writer = csv.writer(csvfile)
                    writer.writerow(fieldnames)
                    first = False
                writer.writerow([r.idx, r.addr, r.addrk, r.addrv, r.addrk-r.addr, r.addrv-r.addr, 0 if prev_addr is None else r.addr - prev_addr])
                prev_addr = r.addr
    

if __name__ == "__main__":
    main()
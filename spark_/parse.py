#
# Parse data from spark log files, clean it 
# and put it into CSV format for plotting.
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
class Object:
    addr = None
    size = None
    depth = None 

class Record:
    shuffleid = None
    mapid = None
    idx = None
    count = 0
    objects = None
    gaps = None
    startaddr = None
    endaddr = None

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
    files = "{0}/records*".format(datadir)
    records = []
    for filename in glob.glob(files):
        with open(filename, 'r') as file:                                                                                                                                                                                                                                             
            for line in file:
                vals = line.split(',')
                if len(vals) < 4: 
                    print("ERROR! Expecting at least 3 columns for any record, found otherwise in {}".format(filename))
                    return -1

                r = Record()
                r.shuffleid = int(vals[0])
                r.mapid = int(vals[1])
                r.idx = int(vals[2])
                r.count = int(vals[3])
                r.objects = []
                r.gaps = []
                lastaddr = None
                for i in range(r.count):
                    o = Object()
                    o.addr = int(vals[4+i*3])
                    o.size = int(vals[5+i*3])
                    o.depth = int(vals[6+i*3])
                    r.objects.append(o)
                    if lastaddr:    r.gaps.append(o.addr - lastaddr)
                    lastaddr = o.addr + o.size
                    if not r.startaddr or o.addr < r.startaddr:     r.startaddr = o.addr
                    if not r.endaddr or lastaddr > r.endaddr:       r.endaddr = lastaddr
                records.append(r)
        # print(len(records))

    # Maps and shuffles
    shuffles = set([r.shuffleid for r in records])
    maps = set([r.mapid for r in records])
    print("Available shuffles: " + str(shuffles))
    # print("Available maps: " + str(maps) + ", shuffles: " + str(shuffles))

    # Save stats per shuffle
    for shuffleid in shuffles:
        datafile = "shuffle{}.csv".format(shuffleid)
        outfile = os.path.join(expdir, datafile)
        prev_addr = None

        records_scoped = [r for r in records if r.shuffleid == shuffleid]
        with open(os.path.join("", outfile), 'w') as csvfile:
            first = True
            for i, r in enumerate(records_scoped):
                if first:
                    fieldnames = ["idx", "objects", "depth", "startaddr", "span", "gaps"]
                    writer = csv.writer(csvfile)
                    writer.writerow(fieldnames)
                    first = False
                depths = [o.depth for o in r.objects]
                writer.writerow([i, r.count, np.mean(depths), r.startaddr, r.endaddr - r.startaddr, sum(r.gaps)])

    # # Save metadata for all shuffles
    # datafile = "info.csv".format(shuffleid)
    # outfile = os.path.join(expdir, datafile)
    # with open(os.path.join("", outfile), 'w') as csvfile:
    #     for shuffleid in shuffles:
    #         if first:
    #             fieldnames = ["shuffleid", "objects", "depth"]
    #             writer = csv.writer(csvfile)
    #             writer.writerow(fieldnames)
    #             first = False
    #         depths = [o.depth for o in r.objects]
    #         writer.writerow([i, r.count, np.mean(depths), r.startaddr, r.endaddr - r.startaddr, sum(r.gaps)])


    

if __name__ == "__main__":
    main()
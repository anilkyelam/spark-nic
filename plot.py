import sys
import os
import re
import math
from datetime import datetime
from datetime import timedelta
import time
import random
import matplotlib
import matplotlib.pyplot as plt
from collections import Counter, defaultdict
import argparse
import pandas as pd
import numpy as np
from enum import Enum
import scipy.stats as scstats


colors = ['b', 'g', 'r', 'brown', 'c','k', 'orange', 'm','orangered','y']
linetypes = ['g-','g--','g-+']
markers = ['x','+','o','s','+', '|', '^']

class PlotType(Enum):
    line = 'line'
    scatter = 'scatter'
    bar = 'bar'
    barstacked = 'barstacked'
    cdf = 'cdf'
    hist = 'hist'

    def __str__(self):
        return self.value

class LegendLoc(Enum):
    none = "none"
    best = 'best'
    topout = "topout"
    rightout = "rightout"
    rightin = "rightin"
    center = "center"
    topleft = "topleft"

    def matplotlib_loc(self):
        if self.value == LegendLoc.none:       return None
        if self.value == LegendLoc.best:       return 'best'
        if self.value == LegendLoc.rightin:    return 'right'
        if self.value == LegendLoc.center:     return 'center'
        if self.value == LegendLoc.topleft:    return 'topleft'
        if self.value == LegendLoc.topout:     return 'upper center'
        if self.value == LegendLoc.rightout:   return 'center left'

    def __str__(self):
        return self.value

def set_axes_legend_loc(ax, lns, labels, loc):
    if loc == LegendLoc.none:
        return
    if loc in (LegendLoc.best, LegendLoc.rightin, LegendLoc.center, LegendLoc.topleft):
        ax.legend(lns, labels, loc=loc.matplotlib_loc(), ncol=1, fancybox=True, shadow=True)
    if loc == LegendLoc.topout:
        ax.legend(lns, labels, loc=loc.matplotlib_loc(), bbox_to_anchor=(0.5, 1.05), ncol=3, fancybox=True, shadow=True)
    if loc == LegendLoc.rightout:
        ax.legend(lns, labels, loc=loc.matplotlib_loc(), bbox_to_anchor=(1, 1), ncol=1, fancybox=True, shadow=True)


class LineStyle(Enum):
    solid = 'solid'
    dashed = "dashed"
    dotdash = "dashdot"

    def __str__(self):
        return self.value

class OutputFormat(Enum):
    pdf = 'pdf'
    png = "png"
    eps = "eps"

    def __str__(self):
        return self.value

def gen_cdf(npArray, num_bin):
   x = np.sort(npArray)
   y = 1. * np.arange(len(npArray)) / (len(npArray) - 1)
#    h, edges = np.histogram(npArray, density=True, bins=num_bin )
#    h = np.cumsum(h)/np.cumsum(h).max()
#    x = edges.repeat(2)[:-1]
#    y = np.zeros_like(x)
#    y[1:] = h.repeat(2)
   return x, y


# Argument Parser
def parse_args():
    parser = argparse.ArgumentParser("Python Generic Plotter: Only accepts CSV files")

    parser.add_argument('-d', '--datafile', 
        action='append', 
        help='path to the data file, multiple values are allowed')
        
    parser.add_argument('-xc', '--xcol', 
        action='store', 
        help='X column  from csv file. Defaults to row index if not provided.', 
        required=False)

    parser.add_argument('-yc', '--ycol', 
        action='append', 
        help='Y column from csv file, multiple values are allowed')

    parser.add_argument('-dxc', '--dfilexcol', 
        nargs=2,
        action='store', 
        help='X column  from a specific csv file. Defaults to row index if not provided.', 
        required=False)

    parser.add_argument('-dyc', '--dfileycol',
        nargs=2,
        action='append',
        metavar=('datafile', 'ycol'),
        help='Y column from a specific file that is included with this argument')

    parser.add_argument('-o', '--output', 
        action='store', 
        help='path to the output plot png', 
        default="result.png")

    parser.add_argument('-p', '--print_', 
        action='store_true', 
        help='print data (with nicer format) instead of plot', 
        default=False)

    # plot options
    parser.add_argument('-z', '--ptype', 
        action='store', 
        help='type of the plot. Defaults to line',
        type=PlotType, 
        choices=list(PlotType), 
        default=PlotType.line)

    parser.add_argument('-t', '--ptitle', 
        action='store', 
        help='title of the plot')

    parser.add_argument('-xl', '--xlabel', 
        action='store', 
        help='Custom x-axis label')

    parser.add_argument('-yl', '--ylabel', 
        action='store', 
        help='Custom y-axis label')

    parser.add_argument('-xm', '--xmul', 
        action='store', 
        type=float,
        help='Custom x-axis multiplier constant (e.g., to change units)',
        default=1)

    parser.add_argument('-ym', '--ymul', 
        action='store', 
        type=float,
        help='Custom y-axis multiplier constant (e.g., to change units)',
        default=1)

    parser.add_argument('--xlog', 
        action='store_true', 
        help='Plot x-axis on log scale',
        default=False)
        
    parser.add_argument('--ylog', 
        action='store_true', 
        help='Plot y-axis on log scale',
        default=False)

    parser.add_argument('--xstr', 
        action='store_true', 
        help='Set X-values as string labels (applies to a bar plot)',
        default=False)
    
    parser.add_argument('--xmin', 
        action='store', 
        type=float,
        help='Custom x-axis lower limit')

    parser.add_argument('--ymin', 
        action='store', 
        type=float,
        help='Custom y-axis lower limit')

    parser.add_argument('--xmax', 
        action='store', 
        type=float,
        help='Custom x-axis upper limit')

    parser.add_argument('--ymax', 
        action='store', 
        type=float,
        help='Custom y-axis upper limit')

    parser.add_argument('-hl', '--hline', 
        action='store', 
        type=float,
        help='Add a horizantal line at specified y-value')

    parser.add_argument('-vl', '--vline', 
        action='store', 
        type=float,
        help='Add a vertical line at specified x-value')

    parser.add_argument('-pg', '--pgroup', 
        action='append', 
        help='plot group, number, can provide one group id per ycol or datafile. Plots with same group id will get single plot attribtes like color, label, etc')

    parser.add_argument('-l', '--plabel', 
        action='append', 
        help='plot label, can provide one label per ycol or datafile')
    
    parser.add_argument('-ls', '--linestyle', 
        action='append', 
        help='line style of the plot of the plot. Can provide one label per ycol or datafile, Defaults to solid',
        # type=LineStyle, 
        # choices=list(LineStyle))
    )

    parser.add_argument('-cmi', '--colormarkerincr', 
        action='append', 
        help='whether to move to the next color/marker pair, one per ycol or datafile',
        type=int
    )

    parser.add_argument('-li', '--labelincr', 
        action='append', 
        help='whether to move to the next label in the list, one per ycol or datafile',
        type=int
    )

    parser.add_argument('-nm', '--nomarker',  
        action='store_true', 
        help='dont add markers to plots', 
        default=False)

    parser.add_argument('-nt', '--notail',  
        action='store', 
        help='eliminate last x%% tail from CDF. Defaults to 1%%', 
        nargs='?',
        type=float,
        const=0.1)

    parser.add_argument('-nh', '--nohead',  
        action='store', 
        help='eliminate first x%% head from CDF. Defaults to 1%%', 
        nargs='?',
        type=float,
        const=1.0)

    parser.add_argument('-dl', '--dashed', 
        action='store', 
        nargs='+', 
        type=int,
        help='use dashed lines for plots with these set of indexes (index starts at 1)')

    parser.add_argument('-s', '--show',  
        action='store_true', 
        help='Display the plot after saving it. Blocks the program.', 
        default=False)
    
    parser.add_argument('-ll', '--lloc', 
        action='store', 
        help='Custom legend location',
        type=LegendLoc, 
        choices=list(LegendLoc), 
        default=LegendLoc.best)

    parser.add_argument('-fs', '--fontsize', 
        action='store', 
        type=int,
        help='Font size of plot labels, ticks, etc',
        default=15)

    parser.add_argument('-of', '--outformat', 
        action='store', 
        help='Output file format',
        type=OutputFormat, 
        choices=list(OutputFormat), 
        default=OutputFormat.pdf)

    ## Twin Axis Settings
    parser.add_argument('-tw', '--twin', 
        action='store', 
        type=int,
        help='add a twin y-axis for y cols starting from this index (count from 1)',
        default=100)
    
    parser.add_argument('-tyl', '--tylabel', 
        action='store', 
        help='Custom y-axis label for twin axis')
        
    parser.add_argument('-tym', '--tymul', 
        action='store', 
        type=float,
        help='Custom y-axis multiplier constant (e.g., to change units) for twin Y-axis',
        default=1)
    
    parser.add_argument('-tll', '--tlloc', 
        action='store', 
        help='Custom legend location for twin axis',
        type=LegendLoc, 
        choices=list(LegendLoc), 
        default=LegendLoc.best)

    args = parser.parse_args()
    return args


def main():
    args = parse_args()

    # Plot can be: 
    # 1. One datafile with multiple ycolumns plotted against single xcolumn
    # 2. Single ycolumn from multiple datafiles plotted against an xcolumn
    # 3. If ycols from multiple datafiles must be plotted, use -dyc argument style
    dyc=(args.dfileycol is not None)
    dandyc=(args.datafile is not None or args.ycol is not None)
    if (dyc and dandyc) or not (dyc or dandyc):
        print("Use either the (-dyc) or the (-d and -yc) approach - atleast one, and not both!")
        return -1

    if (args.datafile or args.ycol) and \
        (args.datafile and len(args.datafile) > 1) and \
        (args.ycol and len(args.ycol) > 1):
        print("Only one of datafile or ycolumn arguments can provide multiple values. Use -dyc style if this doesn't work for you.")
        return -1

    # Get files, xcols and ycols from args
    num_plots = 0
    dfile_xcol = None
    dfile_ycol_map = []     #Maintain the input order
    if args.dfileycol:
        dfilexcol = args.dfilexcol
        for (dfile, ycol) in args.dfileycol:
            if args.xcol and not dfile_xcol:
                dfile_xcol = (dfile, args.xcol)
            dfile_ycol_map.append((dfile, ycol))
            num_plots += 1
    else:
        for dfile in args.datafile:
            if args.xcol and not dfile_xcol:
                dfile_xcol = (dfile, args.xcol)
            for ycol in args.ycol:
                dfile_ycol_map.append((dfile, ycol))
                num_plots += 1  

    if not args.labelincr and args.plabel and len(args.plabel) != num_plots:
        print("If plot labels are provided and --labelincr is not, they must be provided for all the plots and are mapped one-to-one in input order")
        return -1
    
    if args.labelincr:
        if not args.plabel:
            print("If --labelincr is specified, plot labels must be specified with -l/--plabel")
            return -1

        if len(args.plabel) <= sum(args.labelincr):
            print("If plot labels and --labelincr are provided, sum of lable increments should not cross the number of plot labels")
            return -1
    

    xlabel = args.xlabel if args.xlabel else args.xcol
    ylabel = args.ylabel if args.ylabel else dfile_ycol_map[0][1]

    cidx = 0
    midx = 0
    lidx = 0
    aidx = 0
    labelidx = 0

    font = {'family' : 'sans-serif',
            'size'   : args.fontsize}
    matplotlib.rc('font', **font)
    matplotlib.rc('figure', autolayout=True)
    matplotlib.rc('figure', autolayout=True)
    matplotlib.rcParams['pdf.fonttype'] = 42        # required for latex embedded figures

    fig, axmain = plt.subplots(1, 1, figsize=(8,5))
    fig.suptitle(args.ptitle if args.ptitle else '')
    #plt.ylim(0, 1000)

    if args.xlog:
        axmain.set_xscale('log', basex=2)
    if args.ylog:
        axmain.set_yscale('log')

    xcol = None
    if dfile_xcol:
        df = pd.read_csv(dfile_xcol[0])
        xcol = df[dfile_xcol[1]]

    plot_num = 0
    lns = []
    ax = axmain
    ymul = args.ymul
    twin = False
    base_dataset = None     
    for (datafile, ycol) in dfile_ycol_map:

        if (plot_num + 1) == args.twin:
            # Switch to twin axis, reset Y-axis settings
            ax = axmain.twinx()
            ylabel = args.tylabel if args.tylabel else ycol
            ymul = args.tymul
            twin = True

        if not os.path.exists(datafile):
            print("Datafile {0} does not exist".format(datafile))
            return -1

        df = pd.read_csv(datafile)
        if args.print_:
            for ycol in ycols:
                label = "{0}:{1}".format(datafile, ycol)
                print(label, df[ycol].mean(), df[ycol].std())
            continue
 
        if args.plabel:                 label = args.plabel[labelidx] if args.labelincr else args.plabel[plot_num]
        elif len(args.datafile) == 1:   label = ycol
        else:                           label = datafile

        if args.pgroup:                 gid = args.pgroup[plot_num]
        else:                           gid = plot_num

        if xcol is None:
            xcol = df.index

        if args.ptype == PlotType.line:
            xc = xcol
            yc = df[ycol]
            xc = [x * args.xmul for x in xc]
            yc = [y * ymul for y in yc]
            if args.xstr:   xc = [str(x) for x in xc]

            if args.nomarker:
                lns += ax.plot(xc, yc, label=label, color=colors[cidx])
            else:
                lns += ax.plot(xc, yc, label=label, color=colors[cidx], 
                    marker=markers[midx],markerfacecolor=colors[cidx])

        elif args.ptype == PlotType.scatter:
            xc = xcol
            yc = df[ycol]
            xc = [x * args.xmul for x in xc]
            yc = [y * ymul for y in yc]

            if args.nomarker:
                ax.scatter(xc, yc, label=label, color=colors[cidx])
            else:
                ax.scatter(xc, yc, label=label, color=colors[cidx], marker=markers[midx])

        elif args.ptype == PlotType.bar:
            xc = xcol
            yc = df[ycol]
            xc = [x * args.xmul for x in xc]
            yc = [y * ymul for y in yc]
            if args.xstr:   xc = [str(x) for x in xc]
            ax.bar(xc, yc, label=label, color=colors[cidx])
            if args.xstr:   ax.set_xticks(xc)
            if args.xstr:   ax.set_xticklabels(xc, rotation='45')
            

        elif args.ptype == PlotType.barstacked:
            xc = xcol
            yc = df[ycol]
            xc = [x * args.xmul for x in xc]
            yc = np.array([y * ymul for y in yc])
            if args.xstr:   xc = [str(x) for x in xc]
            ax.bar(xc, yc, bottom=base_dataset, label=label, color=colors[cidx])
            if args.xstr:   ax.set_xticks(xc)
            if args.xstr:   ax.set_xticklabels(xc, rotation='45')
            base_dataset = yc if base_dataset is None else base_dataset + yc

        elif args.ptype == PlotType.hist:
            raise NotImplementedError("hist")

        elif args.ptype == PlotType.cdf:
            xc, yc = gen_cdf(df[ycol], 100000)

            head = 0
            tail = len(xc)
            if args.nohead:
                for i, val in enumerate(yc):
                    if val <= args.nohead/100.0:
                        head = i
            if args.notail:
                for i, val in enumerate(yc):
                    if val > (100.0 - args.notail)/100.0:
                        tail = i
                        break

            for i, val in enumerate(yc):
                if val > 0.99:
                    idx = i
                    break

            xc = xc[head:tail]
            yc = yc[head:tail]
            xc = [x * args.xmul for x in xc]
            yc = [y * ymul for y in yc]

            if args.nomarker:
                lns += ax.plot(xc, yc, label=label, color=colors[cidx])
            else:
                lns += ax.plot(xc, yc, label=label, color=colors[cidx],
                    marker=markers[midx],markerfacecolor=colors[cidx])

            ylabel = "CDF"

        if args.colormarkerincr:
            if args.colormarkerincr[plot_num] == 1:
                cidx = (cidx + 1) % len(colors)
                midx = (midx + 1) % len(markers)
        else:
            cidx = (cidx + 1) % len(colors)
            midx = (midx + 1) % len(markers)
     
        if args.labelincr:
            if args.labelincr[plot_num] == 1:
                labelidx = (labelidx + 1) 
        

        plot_num += 1
        if args.ymin:    ax.set_ylim(ymin=args.ymin)
        if args.ymax:    ax.set_ylim(ymax=args.ymax)
        ax.set_ylabel(ylabel)
    
    if args.xmin:   axmain.set_xlim(xmin=args.xmin)
    if args.xmax:   axmain.set_xlim(xmax=args.xmax)
    axmain.set_xlabel(xlabel)
    # axmain.set_ylabel(ylabel)

    # Set dashes if necessary
    if args.dashed:
        for d in args.dashed:
            lns[d-1].set_dashes([3,1])

    if args.linestyle:
        for idx, ls in enumerate(args.linestyle):
            lns[idx].set_linestyle(ls)
    
    if args.lloc != LegendLoc.none and args.ptype in [PlotType.bar, PlotType.barstacked, PlotType.hist]:
        plt.legend(loc=args.lloc.matplotlib_loc())
    else:
        # TODO: Fix labels for bar plot
        labels = [l.get_label() for l in lns]
        set_axes_legend_loc(axmain, lns, labels, args.lloc)

    # Add horizantal and/or vertical lines
    if args.hline:
        plt.axhline(x=args.hline)
        plt.text(args.hline, 0, str(args.hline))
    if args.vline:
        plt.axvline(x=args.vline, ls='dashed')
        plt.text(args.vline, 0, str(args.vline))

    # plt.savefig(args.output, format="eps")
    plt.savefig(args.output, format=str(args.outformat))
    if args.show:
        plt.show()

if __name__ == '__main__':
    main()
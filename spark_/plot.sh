#
# Commands for generating various plots
#

# Parse command line arguments
for i in "$@"
do
case $i in
    -g|--gen)                   # re-run experiments to generate data
    gen=1
    ;;

    -d=*|--runid=*)             # provide run id if plotting data from previous runs; 
    RUNID_GIVEN="${i#*=}"       # ignored if --gen is set
    ;;

    -c=*|--readme=*)            # optional comments for this run; 
    README="${i#*=}"            # saved along with data for future ref
    ;;
    
    *)                          # unknown option
    ;;
esac
done


# Folders
CUR_PATH=`realpath $0`
DIR=$(dirname $CUR_PATH)
DATADIR=${DIR}/out
mkdir -p ${DATADIR}

# Figure out data location
if [[ $gen ]]; then   
    runid=$(date '+%m-%d-%H-%M');    # unique id
    mkdir -p $DATADIR/$runid
else         
    if [[ -z "$RUNID_GIVEN" ]]; then
        echo "ERROR! Provide runid under data/ for plotting, or use --gen to generate data"
        exit 1
    fi           
    runid=$RUNID_GIVEN; 
    if [ ! -d $DATADIR/$runid ]; then
        echo "ERROR! Data for $runid not found under $DATADIR"
        exit 1
    fi
fi

RUNDIR=$DATADIR/$runid
if [[ $README ]]; then
    echo "$README" > $RUNDIR/readme
fi

# plots location
PLOTDIR=${RUNDIR}/plots
PLOTEXT=png                 # supported: png or pdf
mkdir -p ${PLOTDIR} 

# #
# # START PLOTTING
# # (Uncomment a section as required)
# #


#==============================================================#

# # Pagerank object layout for shuffles
wtype=graph
wname=pagerank
if [[ $gen ]]; then
    bash ${DIR}/hibench.sh --save --name=${runid} -wt=graph -wn=pagerank
    python parse.py -i ${runid}
fi

for f in `ls ${RUNDIR}/shuffle*`; do 
    label=`basename $f`
    plots="$plots -d $f -l $label"
done

plotfile=${PLOTDIR}/span_${wtype}_${wname}_${runid}.${PLOTEXT}
python ../tools/plot.py ${plots} -z cdf -yc "span" -o ${plotfile} -of ${PLOTEXT} 
display ${plotfile} &

plotfile=${PLOTDIR}/gaps_${wtype}_${wname}_${runid}.${PLOTEXT}
python ../tools/plot.py ${plots} -z cdf -yc "gaps" -o ${plotfile} -of ${PLOTEXT} 
display ${plotfile} &

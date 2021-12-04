#
# Commands for generating various plots
#

# Parse command line arguments
for i in "$@"
do
case $i in
    -g|--gen)                 # re-run experiments to generate data
    REGEN=1
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
DATADIR=${DIR}/data
mkdir -p ${DATADIR}

# Figure out data location
if [[ $REGEN ]]; then   
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
    echo "$README" >> $RUNDIR/readme
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

# # # RDMA read/write RTT plot
# datafile=${RUNDIR}/rtts
# plotfile=${PLOTDIR}/rtt_${runid}.${PLOTEXT}
# if [[ $REGEN ]]; then
#   bash run.sh -o="-r -o ${datafile}"
# elif [ ! -f $datafile ]; then
#     echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#     exit 1
# fi
# python ../tools/plot.py -d ${datafile} \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write" -yc "read" -yl "RTT (micro-sec)" \
#     -o ${plotfile} -of ${PLOTEXT} 
# display ${plotfile} &

#==============================================================#

# # # RDMA read/write xput plots 
# # # (changing payload size for various window sizes)
# # # (relevant runs: 12-16-02-06, )
# vary=0
# for concur in 1 4 16 64 128 256; 
# do 
#     echo "Running xput with $concur concurrent requests"; 
#     datafile=${RUNDIR}/xput_window_${concur}
#     if [[ $REGEN ]]; then
#         bash run.sh -o="-x -c ${concur} -m ${vary} -o ${datafile}"
#     elif [ ! -f $datafile ]; then
#         echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#         exit 1
#     fi
#     files="$files -d ${datafile} -l $concur"
# done

# plotfile=${PLOTDIR}/write_xput_${runid}.${PLOTEXT}
# python ../tools/plot.py $files          \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write_gbps" -yl "Goodput (gbps)"    \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "window size"
# display ${plotfile} &

# plotfile=${PLOTDIR}/write_xput_ops_${runid}.${PLOTEXT}
# python ../tools/plot.py $files          \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write_ops" -yl "Goodput (ops)"    \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "window size"
# display ${plotfile} &

# plotfile=${PLOTDIR}/read_xput_${runid}.${PLOTEXT}
# python ../tools/plot.py $files          \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "read_gbps" -yl "Goodput (gbps)"    \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "window size"
# display ${plotfile} &

# concur=16
# plotfile=${PLOTDIR}/write_xput_concur${concur}_${runid}.${PLOTEXT}
# python ../tools/plot.py -d ${RUNDIR}/xput_window_${concur} -l "$concur" \
#     -xc "msg size" -xl "msg size (B)"                       \
#     -yc "write_ops" -yl "million ops/sec" --ymul 1e-6       \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "window size"
# display ${plotfile} &

#================================================================#

# # RDMA read/write xput plots 
# # (for various window sizes)
qp=2
for concur in 1; 
do 
    echo "Running xput with $concur concurrent requests"; 
    datafile=${RUNDIR}/xput_window_${concur}
    if [[ $REGEN ]]; then
        bash run.sh -o="-x -c ${concur} -o ${datafile}"
    elif [ ! -f $datafile ]; then
        echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
        exit 1
    fi
    files="$files -d ${datafile} -l $concur"
done

# plotfile=${PLOTDIR}/write_xput_${runid}.png
# python3 ../tools/plot.py -d data/12-01-15-18/xput_window_1          \
#     -xc "concur" -xl "window size"   \
#     -yc "cas_ops" -l "cas" -yl "MOPS" --ymul 1e-6  \
#     -yc "write_ops" -l "write"    \
#     -yc "read_ops" -l "read"    \
#     -o ${plotfile} -of png -fs 11 --ltitle "OPS"
# display ${plotfile} &

# plotfile=${PLOTDIR}/write_xput_ops_${runid}.png
# python ../tools/plot.py $files          \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write_ops" -yl "Goodput (ops)"    \
#     -o ${plotfile} -of png -fs 11 --ltitle "window size"
# display ${plotfile} &

# plotfile=${PLOTDIR}/read_xput_${runid}.png
# python ../tools/plot.py $files          \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "read_gbps" -yl "Goodput (gbps)"    \
#     -o ${plotfile} -of png -fs 11 --ltitle "window size"
# display ${plotfile} &

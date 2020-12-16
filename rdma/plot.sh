#
# Commands for generating various plots
#

# Parse command line arguments
for i in "$@"
do
case $i in
    -r|--regen)                 # re-run experiments to generate data
    REGEN=1
    ;;

    -d=*|--runid=*)             # provide run id if plotting data from previous runs; 
    RUNID_GIVEN="${i#*=}"       # ignored if --regen is set
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
PLOTDIR=${DIR}/plots
mkdir -p ${PLOTDIR} 


# Figure out data location
if [[ $REGEN ]]; then   
    runid=$(date '+%m-%d-%H-%M');    # unique id
    mkdir -p $DATADIR/$runid
else         
    if [[ -z "$RUNID_GIVEN" ]]; then
        echo "ERROR! Provide runid under data/ for plotting, or use --regen to generate data"
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


# #
# # START PLOTTING
# #


# # # RDMA read/write RTT plot
# datafile=${RUNDIR}/rtts
# plotfile=${PLOTDIR}/rtt_${runid}.png
# if [[ $REGEN ]]; then
#   bash run.sh -o="-r -o ${datafile}"
# elif [ ! -f $datafile ]; then
#     echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#     exit 1
# fi
# python ../tools/plot.py -d ${datafile} \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write" -yc "read" -yl "RTT (micro-sec)" \
#     -o ${plotfile} -of png 
# display ${plotfile} &


# # RDMA read/write xput plots 
# # (for various window sizes)
for concur in 1 4 16 64 128 256; 
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

plotfile=${PLOTDIR}/write_xput_${runid}.png
python ../tools/plot.py $files          \
    -xc "msg size" -xl "msg size (B)"   \
    -yc "write_gbps" -yl "Goodput (gbps)"    \
    -o ${plotfile} -of png -fs 11 --ltitle "window size"
display ${plotfile} &

plotfile=${PLOTDIR}/write_xput_ops_${runid}.png
python ../tools/plot.py $files          \
    -xc "msg size" -xl "msg size (B)"   \
    -yc "write_ops" -yl "Goodput (ops)"    \
    -o ${plotfile} -of png -fs 11 --ltitle "window size"
display ${plotfile} &

plotfile=${PLOTDIR}/read_xput_${runid}.png
python ../tools/plot.py $files          \
    -xc "msg size" -xl "msg size (B)"   \
    -yc "read_gbps" -yl "Goodput (gbps)"    \
    -o ${plotfile} -of png -fs 11 --ltitle "window size"
display ${plotfile} &
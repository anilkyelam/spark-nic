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
PLOTEXT=png             # supported: png or pdf
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
# # (Uncomment a section as required)
# #


#==============================================================#

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

# plotfile=${PLOTDIR}/write_xput_${runid}.png
# python ../tools/plot.py $files          \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write_gbps" -yl "Goodput (gbps)"    \
#     -o ${plotfile} -of png -fs 11 --ltitle "window size"
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

# concur=16
# plotfile=${PLOTDIR}/write_xput_concur${concur}_${runid}.png
# python ../tools/plot.py -d ${RUNDIR}/xput_window_${concur} -l "$concur" \
#     -xc "msg size" -xl "msg size (B)"                       \
#     -yc "write_ops" -yl "million ops/sec" --ymul 1e-6       \
#     -o ${plotfile} -of png -fs 11 --ltitle "window size"
# display ${plotfile} &

#================================================================#

# # RDMA read/write xput plots 
# # (changing window size for various payload sizes)
# # (relevant runs: 12-20-23-32, )
# vary=0
# for msgsize in 8 32 64 128 256 512 1024 2048 4096; 
# do 
#     echo "Running xput with $msgsize B msg size for various window sizes"; 
#     datafile=${RUNDIR}/xput_msgsize_${msgsize}
#     if [[ $REGEN ]]; then
#         bash run.sh -o="-x -c ${vary} -m ${msgsize} -o ${datafile}"
#     elif [ ! -f $datafile ]; then
#         echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#         exit 1
#     fi
#     files="$files -d ${datafile} -l ${msgsize}B"
# done

# plotfile=${PLOTDIR}/write_xput_${runid}.png
# python ../tools/plot.py $files                      \
#     -xc "window size" -xl "outstanding requests"    \
#     -yc "write_gbps" -yl "Goodput (gbps)"           \
#     -o ${plotfile} -of png -fs 11 --ltitle "payload size"
# display ${plotfile} &

# plotfile=${PLOTDIR}/write_xput_ops_${runid}.png
# python ../tools/plot.py $files                          \
#     -xc "window size" -xl "outstanding requests"        \
#     -yc "write_ops" -yl "million ops/sec"  --ymul 1e-6  \
#     -o ${plotfile} -of png -fs 11 --ltitle "payload size" 
# display ${plotfile} &

# plotfile=${PLOTDIR}/read_xput_${runid}.png
# python ../tools/plot.py $files                      \
#     -xc "window size" -xl "outstanding requests"    \
#     -yc "read_gbps" -yl "Goodput (gbps)"            \
#     -o ${plotfile} -of png -fs 11 --ltitle "payload size" --xlog
# display ${plotfile} &

# msgsize=64
# plotfile=${PLOTDIR}/write_xput_msgsize${msgsize}_${runid}.png
# python ../tools/plot.py -d  ${RUNDIR}/xput_msgsize_${msgsize}   \
#     -xc "window size" -xl "outstanding requests"                \
#     -yc "write_ops" -yl "million ops/sec"  --ymul 1e-6          \
#     -o ${plotfile} -of png -fs 11 --ltitle "payload size" --xlog
# display ${plotfile} &


#================================================================#

# # Cost of memory registration in datapath
# # RTT numbers with mr register and deregister in datapath
# # (relevant runs: 01-03-22-38 )
# datafile=${RUNDIR}/rtts_mr
# plotfile=${PLOTDIR}/rtt_mr_${runid}.png
# if [[ $REGEN ]]; then
#     # TODO: Need to set mr_mode to MR_MODE_REGISTER_IN_DATAPTH in the client
#     bash run.sh -o="-r -o ${datafile}"     
# elif [ ! -f $datafile ]; then
#     echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#     exit 1
# fi
# python ../tools/plot.py -d ${datafile} \
#     -xc "msg size" -xl "msg size (B)"   \
#     -yc "write" -yc "read" -yl "RTT (micro-sec)" \
#     -o ${plotfile} -of png 
# display ${plotfile} &

#================================================================#

# # RDMA read/write xput plots for various data transfer modes
# # (changing number of scatter-gather pieces per payload)
vary=0
# for concur in 1 4 16 64; do   for msgsize in 64 128 256 512 1024 2048 4096; do        # (runs 01-04-23-40 and earlier)
# for concur in 128; do           for msgsize in 64 128 256 512 720 1024 2048; do         # (run: 01-08-23-39) 
for concur in 128; do           for msgsize in 1440; do                                 # (run: 01-09-17-22) 
        echo "Running xput with $msgsize B msg size for window sizes $concur"; 
        datafile=${RUNDIR}/xputv2_msgsize_${msgsize}_window_${concur}
        if [[ $REGEN ]]; then
            bash run.sh -o="-y -c ${concur} -m ${msgsize} -o ${datafile}"
        elif [ ! -f $datafile ]; then
            echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
            exit 1
        fi

        plotfile=${PLOTDIR}/sg_xput_${msgsize}B_${concur}R_${runid}.${PLOTEXT}
        python ../tools/plot.py -d ${datafile} \
            -xc "sg pieces" -xl "num sg pieces"   \
            -yc "base_ops" -l "no gather (baseline)" -ls dashed  \
            -yc "cpu_gather_ops" -l "CPU gather" -ls solid  \
            -yc "nic_gather_ops" -l "NIC gather" -ls solid  \
            -yc "piece_by_piece_ops" -l "Piece by Piece" -ls dashed  \
            --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Size: ${msgsize}B, Concurrency: ${concur}" \
            -o ${plotfile} -of ${PLOTEXT}  -fs 9 
        display ${plotfile} &
    done
done

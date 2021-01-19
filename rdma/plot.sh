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

# plotfile=${PLOTDIR}/write_xput_${runid}.${PLOTEXT}
# python ../tools/plot.py $files                      \
#     -xc "window size" -xl "outstanding requests"    \
#     -yc "write_gbps" -yl "Goodput (gbps)"           \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "payload size"
# display ${plotfile} &

# plotfile=${PLOTDIR}/write_xput_ops_${runid}.${PLOTEXT}
# python ../tools/plot.py $files                          \
#     -xc "window size" -xl "outstanding requests"        \
#     -yc "write_ops" -yl "million ops/sec"  --ymul 1e-6  \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "payload size" 
# display ${plotfile} &

# plotfile=${PLOTDIR}/read_xput_${runid}.${PLOTEXT}
# python ../tools/plot.py $files                      \
#     -xc "window size" -xl "outstanding requests"    \
#     -yc "read_gbps" -yl "Goodput (gbps)"            \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "payload size" --xlog
# display ${plotfile} &

# msgsize=64
# plotfile=${PLOTDIR}/write_xput_msgsize${msgsize}_${runid}.${PLOTEXT}
# python ../tools/plot.py -d  ${RUNDIR}/xput_msgsize_${msgsize}   \
#     -xc "window size" -xl "outstanding requests"                \
#     -yc "write_ops" -yl "million ops/sec"  --ymul 1e-6          \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "payload size" --xlog
# display ${plotfile} &


#================================================================#

# # Cost of memory registration in datapath
# # RTT numbers with mr register and deregister in datapath
# # (relevant runs: 01-03-22-38 )
# datafile=${RUNDIR}/rtts_mr
# plotfile=${PLOTDIR}/rtt_mr_${runid}.${PLOTEXT}
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
#     -o ${plotfile} -of ${PLOTEXT} 
# display ${plotfile} &

#================================================================#

# # RDMA read/write xput plots for various data transfer modes
# # (changing number of scatter-gather pieces per payload)
# vary=0
# # for concur in 1 4 16 64; do   for msgsize in 64 128 256 512 1024 2048 4096; do        # (runs 01-04-23-40 and earlier)
# # for concur in 128; do           for msgsize in 64 128 256 512 720 1024 2048; do         # (run: 01-08-23-39) 
# for concur in 128; do           for msgsize in 1440; do                                 # (run: 01-09-17-22) 
#         echo "Running xput with $msgsize B msg size for window sizes $concur"; 
#         datafile=${RUNDIR}/xputv2_msgsize_${msgsize}_window_${concur}
#         if [[ $REGEN ]]; then
#             bash run.sh -o="-y -c ${concur} -m ${msgsize} -o ${datafile}"
#         elif [ ! -f $datafile ]; then
#             echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#             exit 1
#         fi

#         plotfile=${PLOTDIR}/sg_xput_${msgsize}B_${concur}R_${runid}.${PLOTEXT}
#         python ../tools/plot.py -d ${datafile} \
#             -xc "sg pieces" -xl "num sg pieces"   \
#             -yc "base_ops" -l "no gather (baseline)" -ls dashed  \
#             -yc "cpu_gather_ops" -l "CPU gather" -ls solid  \
#             -yc "nic_gather_ops" -l "NIC gather" -ls solid  \
#             -yc "piece_by_piece_ops" -l "Piece by Piece" -ls dashed  \
#             --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Size: ${msgsize}B, Concurrency: ${concur}" \
#             -o ${plotfile} -of ${PLOTEXT}  -fs 9 
#         display ${plotfile} &
#     done
# done


#================================================================#

# # RDMA read/write xput plots for various data transfer modes, ALONG WITH cpu usage (cq poll time)
# # (changing number of scatter-gather pieces per payload)

# # # Cpu poll time as we increase window size for various message sizes
# # # This one is to illustrate CPU poll time as a potential indicator of CPU efficiency...
# vary=0
# for msgsize in 64 256 512 1024 2048; 
# do 
#     echo "Running xput with $msgsize B msg size for various window sizes"; 
#     datafile=${RUNDIR}/xput_pt_msgsize_${msgsize}
#     if [[ $REGEN ]]; then
#         bash run.sh -o="--xput -c ${vary} -m ${msgsize} -o ${datafile}"
#     elif [ ! -f $datafile ]; then
#         echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#         exit 1
#     fi
#     files="$files -d ${datafile} -l ${msgsize}B"
# done

# plotfile=${PLOTDIR}/write_xput_ops_${runid}.${PLOTEXT}
# python ../tools/plot.py $files                          \
#     -xc "window size" -xl "outstanding requests"        \
#     -yc "write_ops" -yl "million ops/sec"  --ymul 1e-6  \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "payload size" 
# display ${plotfile} &

# plotfile=${PLOTDIR}/write_cpu_pt_${runid}.${PLOTEXT}
# python ../tools/plot.py $files                          \
#     -xc "window size" -xl "outstanding requests"        \
#     -yc "write_pp" -yl "CQ Poll Time %"                 \
#     -o ${plotfile} -of ${PLOTEXT} -fs 11 --ltitle "payload size" 
# display ${plotfile} &

# # now getting xput for various data transfer modes alsong with cpu usage.
# runs: 01-11-03-06, 01-12-18-34
# # export MLX5_SHUT_UP_BF=1        # disabling blueflame (run: 01-12-18-34)
# for concur in 16 64; do       for msgsize in 64 128 256 512 720 1024 1440; do
#     echo "Running xput with $msgsize B msg size for window sizes $concur"; 
#     datafile=${RUNDIR}/xputv2_msgsize_${msgsize}_window_${concur}
#     if [[ $REGEN ]]; then
#         bash run.sh -o="-y -c ${concur} -m ${msgsize} -o ${datafile}"
#     elif [ ! -f $datafile ]; then
#         echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
#         exit 1
#     fi

#     plotfile=${PLOTDIR}/sg_xput_${msgsize}B_${concur}R_${runid}.${PLOTEXT}
#     python ../tools/plot.py -d ${datafile} \
#         -xc "sg pieces" -xl "num sg pieces"   \
#         -yc "base_ops" -l "no gather (baseline)" -ls dashed  \
#         -yc "cpu_gather_ops" -l "CPU gather" -ls solid  \
#         -yc "nic_gather_ops" -l "NIC gather" -ls solid  \
#         -yc "piece_by_piece_ops" -l "Piece by Piece" -ls dashed  \
#         --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Size: ${msgsize}B, Concurrency: ${concur}" \
#         -o ${plotfile} -of ${PLOTEXT}  -fs 9 
#     display ${plotfile} &

#     plotfile=${PLOTDIR}/sg_cput_pt_${msgsize}B_${concur}R_${runid}.${PLOTEXT}
#     python ../tools/plot.py -d ${datafile} \
#         -xc "sg pieces" -xl "num sg pieces"   \
#         -yc "base_pp" -l "no gather (baseline)" -ls dashed  \
#         -yc "cpu_gather_pp" -l "CPU gather" -ls solid  \
#         -yc "nic_gather_pp" -l "NIC gather" -ls solid  \
#         -yc "piece_by_piece_pp" -l "Piece by Piece" -ls dashed  \
#         -yl "CQ Poll Time %" --ltitle "Size: ${msgsize}B, Concurrency: ${concur}" \
#         -o ${plotfile} -of ${PLOTEXT}  -fs 9 
#     display ${plotfile} &
#     done
# done


#================================================================#

# RDMA SG Xput and CPU usage with/without BlueFlame support
# runs: 01-13-12-08, 01-13-16-17

# for concur in 128; do       for msgsize in 64 256 512 720 1024 1440; do
#     # run without blueflame
#     echo "Running xput with $msgsize B msg size for window sizes $concur (BLUEFLAME disabled)";
#     export MLX5_SHUT_UP_BF=0      
#     data_nobf=${RUNDIR}/xputv2_${msgsize}B_without_bf
#     if [[ $REGEN ]]; then
#         bash run.sh -o="--xputv2 -c ${concur} -m ${msgsize} -o ${data_nobf}"
#     elif [ ! -f $data_nobf ]; then
#         echo "ERROR! Datafile $data_nobf not found for this run. Try --regen or another runid."
#         exit 1
#     fi
    
#     # run with blueflame
#     echo "Running xput with $msgsize B msg size for window sizes $concur (BLUEFLAME enabled)";
#     export MLX5_SHUT_UP_BF=1      
#     data_bf=${RUNDIR}/xputv2_${msgsize}B_with_bf        # NOTE: with_bf means "with bf shut up" i.e., disabled
#     if [[ $REGEN ]]; then
#         bash run.sh -o="--xputv2 -c ${concur} -m ${msgsize} -o ${data_bf}"
#     elif [ ! -f $data_bf ]; then
#         echo "ERROR! Datafile $data_bf not found for this run. Try --regen or another runid."
#         exit 1
#     fi

#     # We are interested in scatter-gather xput and cpu usage
#     plotfile=${PLOTDIR}/sg_xput_${msgsize}B_bf_${runid}.${PLOTEXT}
#     python ../tools/plot.py \
#         -xc "sg pieces" -xl "num sg pieces" \
#         -dyc ${data_nobf} "base_ops" -l "no gather (baseline)" -ls dashed  \
#         -dyc ${data_nobf} "nic_gather_ops" -l "MMIO" -ls solid  \
#         -dyc ${data_bf} "nic_gather_ops" -l "Doorbell" -ls solid  \
#         --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Size: ${msgsize}B, Reqs in flight: ${concur}" \
#         -o ${plotfile} -of ${PLOTEXT}  -fs 11
#     display ${plotfile} &

#     plotfile=${PLOTDIR}/sg_cpu_${msgsize}B_bf_${runid}.${PLOTEXT}
#     python ../tools/plot.py \
#         -xc "sg pieces" -xl "num sg pieces" \
#         -dyc ${data_nobf} "base_pp" -l "no gather (baseline)" -ls dashed  \
#         -dyc ${data_nobf} "nic_gather_pp" -l "MMIO" -ls solid  \
#         -dyc ${data_bf} "nic_gather_pp" -l "Doorbell" -ls solid  \
#         -yl "CQ Poll Time %" --ltitle "Size: ${msgsize}B, Reqs in flight: ${concur}" \
#         -o ${plotfile} -of ${PLOTEXT}  -fs 11
#     display ${plotfile} &
# done
# done

# # put all sizes in one plot to compare
# # for msgsize in 64 256 512 1024; do
# for msgsize in 1440; do
#     xputplots="${xputplots} -dyc ${RUNDIR}/xputv2_${msgsize}B_with_bf nic_gather_ops -l ${msgsize}B(doorbell)  -ls solid"
#     xputplots="${xputplots} -dyc ${RUNDIR}/xputv2_${msgsize}B_without_bf nic_gather_ops -l ${msgsize}B(mmio)   -ls dashed"
#     cpuplots="${cpuplots} -dyc ${RUNDIR}/xputv2_${msgsize}B_with_bf nic_gather_pp -l ${msgsize}B(doorbell)  -ls solid"
#     cpuplots="${cpuplots} -dyc ${RUNDIR}/xputv2_${msgsize}B_without_bf nic_gather_pp -l ${msgsize}B(mmio)   -ls dashed"
# done

# plotfile=${PLOTDIR}/sg_xput_across_sizes_${runid}.${PLOTEXT}    # xput
# python ../tools/plot.py ${xputplots}    \
#     -xc "sg pieces" -xl "num sg pieces" --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Payload size" \
#     -o ${plotfile} -of ${PLOTEXT}  -fs 10
# display ${plotfile} &
# plotfile=${PLOTDIR}/sg_cpu_across_sizes_${runid}.${PLOTEXT}     # cpu
# python ../tools/plot.py ${cpuplots}    \
#     -xc "sg pieces" -xl "num sg pieces" -yl "CQ Poll Time %" --ltitle "Payload size" \
#     -o ${plotfile} -of ${PLOTEXT}  -fs 10
# display ${plotfile} &

#================================================================#

# # # RTT numbers with/without blueflame to see difference in latencies
# data_nobf=${RUNDIR}/rtts_nobf
# data_bf=${RUNDIR}/rtts_bf
# plotfile=${PLOTDIR}/rtt_bf_${runid}.${PLOTEXT}
# if [[ $REGEN ]]; then
#     export MLX5_SHUT_UP_BF=1  
#     bash run.sh -o="--rtt -o ${data_nobf}"
#     export MLX5_SHUT_UP_BF=0
#     bash run.sh -o="--rtt -o ${data_bf}"
# elif [ ! -f $data_bf ] || [ ! -f $data_nobf ]; then
#     echo "ERROR! Datafiles $data_nobf or $data_bf not found for this run. Try --regen or another runid."
#     exit 1
# fi
# python ../tools/plot.py \
#     -xc "msg size" -xl "msg size (B)" -yl "RTT (micro-sec)" \
#     -dyc ${data_nobf} "write" -l "write (doorbell)" -ls solid \
#     -dyc ${data_bf} "write" -l "write (mmio)"  -ls dashed \
#     -dyc ${data_nobf} "read" -l "read (doorbell)" -ls solid \
#     -dyc ${data_bf} "read" -l "read (mmio)"  -ls dashed \
#     -o ${plotfile} -of ${PLOTEXT} --ltitle "Operation" -fs 11
# display ${plotfile} &

#================================================================#


# # RDMA SG Xput and CPU usage with varying number of queue pairs
# # runs: 01-15-15-20

export MLX5_SHUT_UP_BF=1        # Doorbell to be default from now on.
for concur in 128; do   for msgsize in 64 256 512 720 1024 1440; do
    xputplotsq=
    cpuplotsq=
    for qps in 1 2 4 8; do
        datafile=${RUNDIR}/sg_xput_qps_${qps}_${msgsize}B_${concur}w
        if [[ $REGEN ]]; then
            bash run.sh -o="--xputv2 -c ${concur} -m ${msgsize} -q ${qps} -o ${datafile}" -so="-q ${qps}"
        elif [ ! -f $datafile ]; then
            echo "ERROR! Datafile $datafile not found for this run. Try --regen or another runid."
            exit 1
        fi

        # Plots per QP
        plotfile=${PLOTDIR}/sg_xput_${qps}qps_${msgsize}B_${concur}R.${PLOTEXT}
        python ../tools/plot.py -d ${datafile} \
            -xc "sg pieces" -xl "num sg pieces"   \
            -yc "base_ops" -l "no gather (baseline)" -ls dashed  \
            -yc "cpu_gather_ops" -l "CPU gather" -ls solid  \
            -yc "nic_gather_ops" -l "NIC gather" -ls solid  \
            -yc "piece_by_piece_ops" -l "Piece by Piece" -ls dashed  \
            --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Size: ${msgsize}B, QPs: ${qps}" \
            -o ${plotfile} -of ${PLOTEXT}  -fs 10
        display ${plotfile} &

        plotfile=${PLOTDIR}/sg_cpu_${qps}qps_${msgsize}B_${concur}R.${PLOTEXT}
        python ../tools/plot.py -d ${datafile} \
            -xc "sg pieces" -xl "num sg pieces"   \
            -yc "base_pp" -l "no gather (baseline)" -ls dashed  \
            -yc "cpu_gather_pp" -l "CPU gather" -ls solid  \
            -yc "nic_gather_pp" -l "NIC gather" -ls solid  \
            -yc "piece_by_piece_pp" -l "Piece by Piece" -ls dashed  \
            -yl "CQ Poll Time %" --ltitle "Size: ${msgsize}B, QPs: ${qps}" \
            -o ${plotfile} -of ${PLOTEXT}  -fs 10
        display ${plotfile} &

        xputplotsq="${xputplotsq} -dyc ${datafile} nic_gather_ops -l sg(qps:${qps})  -ls solid"
        xputplotsq="${xputplotsq} -dyc ${datafile} base_ops -l base(qps:${qps})  -ls dashed"
        cpuplotsq="${cpuplotsq} -dyc ${datafile} nic_gather_pp -l sg(qps:${qps})  -ls solid"
        cpuplotsq="${cpuplotsq} -dyc ${datafile} base_pp -l base(qps:${qps})  -ls dashed"
    done

    # Plots across QPs
    plotfile=${PLOTDIR}/sg_xput_across_qps_${msgsize}B_${concur}R.${PLOTEXT}    # xput
    python ../tools/plot.py ${xputplotsq}    \
        -xc "sg pieces" -xl "num sg pieces" --ymul 1e-6 -yl "Xput (Million ops)" --ltitle "Size: ${msgsize} B" \
        -o ${plotfile} -of ${PLOTEXT}  -fs 10
    display ${plotfile} &
    plotfile=${PLOTDIR}/sg_cpu_across_qps_${msgsize}B_${concur}R.${PLOTEXT}     # cpu
    python ../tools/plot.py ${cpuplotsq}    \
        -xc "sg pieces" -xl "num sg pieces" -yl "CQ Poll Time %" --ltitle "Size: ${msgsize} B" \
        -o ${plotfile} -of ${PLOTEXT}  -fs 10
    display ${plotfile} &
done; done


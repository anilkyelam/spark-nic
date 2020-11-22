
expname=$1
if [[ -z "$expname" ]]; then    
    dir=$(find out/ -maxdepth 1 -type d | tail -n1);   # Latest by default
    expname=$(basename $dir)
fi
dir=out/$expname
echo "Looking at exp: $expname"

# Parse data from logs
python analyze.py -i ${expname}

idx=${2:-0}
datafile=$dir/memdata${idx}.csv
if [ ! -f $datafile ]; then
    echo "Data $datafile not found!"
    exit 1
fi

# Generate plots
plotdir="$dir/plots"
mkdir -p $plotdir
# python plot.py -z scatter -d $datafile -xc "idx" -yc "address" -of png -o $plotdir/layout.png -t "Memory layout" 
# display $plotdir/layout.png &
# python plot.py -z scatter -d $datafile -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed1.png -t "Memory layout" --xmin 100000 --xmax 101000 --ymin 2.38e10 --ymax 2.42e10
# display $plotdir/layout-zoomed1.png &
# python plot.py -z scatter -d $datafile -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed2.png -t "Memory layout" --xmin 100000 --xmax 101000 --ymin 2.3955e10 --ymax 2.397e10
# display $plotdir/layout-zoomed2.png &
# python plot.py -z scatter -d $datafile -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed3.png -t "Memory layout" --xmin 100000 --xmax 100200 --ymin 2.395975e10 --ymax 2.396125e10
# display $plotdir/layout-zoomed3.png &
# python plot.py -z scatter -d $datafile -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed4.png -t "Memory layout" --ymin 2.394700e10 --ymax 2.394705e10
# display $plotdir/layout-zoomed4.png &
# # python plot.py -z cdf -d $datafile -yc "address" -xl "Address (Bytes)" -of png -o $plotdir/layout-cdf.png -nm -t "Memory layout CDF"
# # display $plotdir/layout-cdf.png &

python plot.py -z cdf -d $datafile -yc "offsetk" -xl "Offset (Bytes)" -of png -o $plotdir/keyoffset.png -nm -nh 0.1 -nt 0.1 --xmin -200 --xmax 200
display $plotdir/keyoffset.png &

python plot.py -z cdf -d $datafile -yc "offsetv" -xl "Offset (Bytes)" -of png -o $plotdir/valoffset.png -nm -nh 0.1 -nt 0.1 --xmin -200 --xmax 200
display $plotdir/valoffset.png &

python plot.py -z cdf -d $datafile -yc "offset" -xl "Offset (Bytes)" -of png -o $plotdir/recoffset.png -nm -nt 0.2 --xmin 0 --xmax 10000
display $plotdir/recoffset.png &
# python plot.py -z cdf -d $datafile -yc "offset" -xl "Offset (Bytes)" -of png -o $plotdir/recoffset-tail.png -nm -t "Offset b/w consecutive objects" -nh 98 --xmin 7500 --ymin 0.975 --ymax 1.005 --hline 1 --xlog
# display $plotdir/recoffset-tail.png &
# python plot.py -z cdf -d $datafile -yc "offset" -xl "Offset (Bytes)" -of png -o $plotdir/recoffset-tail2.png -nm -t "Offset b/w consecutive objects" -nh 98 --xmin 7500 --ymin 0.999 --ymax 1.0001 --hline 1 --xlog
# display $plotdir/recoffset-tail2.png &
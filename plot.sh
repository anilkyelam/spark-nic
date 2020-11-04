
dir=$1
if [[ -z "$dir" ]]; then    dir=$(find out/ -maxdepth 1 -type d | tail -n1);    fi     # Latest by default
expname=$(basename $dir)
echo "Looking at exp: $expname"

# Parse data from logs
python analyze.py -i ${expname}

# Generate plots
plotdir="$dir/plots"
mkdir -p $plotdir
idx=40

python plot.py -z scatter -d $dir/memdata${idx}.csv -xc "idx" -yc "address" -of png -o $plotdir/layout.png -t "Memory layout"
display $plotdir/layout.png &

python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "address" -xl "Address (Bytes)" -of png -o $plotdir/layout-cdf.png -nm -t "Memory layout CDF"
display $plotdir/layout-cdf.png &

python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offsetk" -xl "Offset (Bytes)" -of png -o $plotdir/keyoffset.png -nm --xlog -t "Root Object and Key Offset"
display $plotdir/keyoffset.png &
python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offsetk" -xl "Offset (Bytes)" -of png -o $plotdir/keyoffset-nt5.png -nm -t "Root Object and Key Offset (no 5% tail)" -nt 5
display $plotdir/keyoffset-nt5.png &

python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offsetv" -xl "Offset (Bytes)" -of png -o $plotdir/valoffset.png -nm --xlog -t "Root Object and Value Offset"
display $plotdir/valoffset.png &
python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offsetv" -xl "Offset (Bytes)" -of png -o $plotdir/valoffset-nt5.png -nm -t "Root Object and Value Offset (no 5% tail)" -nt 5
display $plotdir/valoffset-nt5.png &

python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offset" -xl "Offset (Bytes)" -of png -o $plotdir/recoffset.png -nm --xlog -t "Offset b/w consecutive objects"
display $plotdir/recoffset.png &
python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offset" -xl "Offset (Bytes)" -of png -o $plotdir/recoffset-nt5.png -nm -t "Offset b/w consecutive objects (no 5% tail)" -nt 5
display $plotdir/recoffset-nt5.png &

expname=$1
if [[ -z "$expname" ]]; then    
    dir=$(find out/ -maxdepth 1 -type d | tail -n1);   # Latest by default
    expname=$(basename $dir)
fi
dir=out/$expname
echo "Looking at exp: $expname"

# Parse data from logs
# python analyze.py -i ${expname}

# Generate plots
plotdir="$dir/plots"
mkdir -p $plotdir
idx=${2:-0}

# python plot.py -z scatter -d $dir/memdata${idx}.csv -xc "idx" -yc "address" -of png -o $plotdir/layout.png -t "Memory layout" 
# display $plotdir/layout.png &
# python plot.py -z scatter -d $dir/memdata${idx}.csv -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed1.png -t "Memory layout" --xmin 10000 --xmax 11000 --ymin 2.38e10 --ymax 2.42e10
# python plot.py -z scatter -d $dir/memdata${idx}.csv -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed2.png -t "Memory layout" --xmin 10000 --xmax 10100 --ymin 2.38e10 --ymax 2.40e10
# python plot.py -z scatter -d $dir/memdata${idx}.csv -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed3.png -t "Memory layout" --xmin 10000 --xmax 10100 --ymin 2.3947e10 --ymax 2.3949e10
python plot.py -z scatter -d $dir/memdata${idx}.csv -xc "idx" -yc "address" -of png -o $plotdir/layout-zoomed4.png -t "Memory layout" --ymin 2.394700e10 --ymax 2.394705e10
# display $plotdir/layout-zoomed1.png &
# display $plotdir/layout-zoomed2.png &
# display $plotdir/layout-zoomed3.png &
display $plotdir/layout-zoomed4.png &
# python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "address" -xl "Address (Bytes)" -of png -o $plotdir/layout-cdf.png -nm -t "Memory layout CDF"
# display $plotdir/layout-cdf.png &

# python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offsetk" -xl "Offset (Bytes)" -of png -o $plotdir/keyoffset.png -nm -t "Root Object and Key Offset" -nt 0.1 --xmin 0 --xmax 100
# display $plotdir/keyoffset.png &

# python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offsetv" -xl "Offset (Bytes)" -of png -o $plotdir/valoffset.png -nm -t "Root Object and Value Offset" -nt 0.1 --xmin 0 --xmax 100
# display $plotdir/valoffset.png &

# python plot.py -z cdf -d $dir/memdata${idx}.csv -yc "offset" -xl "Offset (Bytes)" -of png -o $plotdir/recoffset.png -nm -t "Offset b/w consecutive objects" -nt 0.2 --xmin 0 --hline 1
# display $plotdir/recoffset.png &
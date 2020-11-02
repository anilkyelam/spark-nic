

# python plot.py -z scatter -d tmp.csv -xc "idx" -yc "address" -of png -o plots/addresses.png -s
# gv plots/addresses.png &


python plot.py -z cdf -d tmp.csv -yc "address" -xl "Address (Bytes)" -of png -o plots/addresses-cdf.png -nm -t "Base Address CDF"
gv plots/addresses-cdf.png &

python plot.py -z cdf -d tmp.csv -yc "offset1" -xl "Offset (Bytes)" -of png -o plots/offset1.png -nm --xlog -t "Root Object and Key Offset"
gv plots/offset1.png &
python plot.py -z cdf -d tmp.csv -yc "offset1" -xl "Offset (Bytes)" -of png -o plots/offset1-nt5.png -nm -t "Root Object and Key Offset (no 5% tail)" -nt 5
gv plots/offset1-nt5.png &

python plot.py -z cdf -d tmp.csv -yc "offset2" -xl "Offset (Bytes)" -of png -o plots/offset2.png -nm --xlog -t "Root Object and Value Offset"
gv plots/offset2.png &
python plot.py -z cdf -d tmp.csv -yc "offset2" -xl "Offset (Bytes)" -of png -o plots/offset2-nt5.png -nm -t "Root Object and Value Offset (no 5% tail)" -nt 5
gv plots/offset2-nt5.png &

python plot.py -z cdf -d tmp.csv -yc "offset3" -xl "Offset (Bytes)" -of png -o plots/offset3.png -nm --xlog -t "Offset b/w consecutive objects"
gv plots/offset3.png &
python plot.py -z cdf -d tmp.csv -yc "offset3" -xl "Offset (Bytes)" -of png -o plots/offset3-nt5.png -nm -t "Offset b/w consecutive objects (no 5% tail)" -nt 5
gv plots/offset3-nt5.png &
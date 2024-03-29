#
# List all experimental runs with information for the given date/time pattern prefix
#

prefix=$1
if [ -z "$prefix" ];  then    prefix=$(date +"%m-%d");    fi    # today

OUT=`echo Exp,Comments`
# for f in `ls out/08-20-1*/stats.json`; do
for f in `ls out/${prefix}*/desc`; do
    # echo $f
    dir=`dirname $f`
    expname=`basename $dir`
    comments=$(cat $dir/desc)
    

    # Print all
    LINE=`echo $expname,$comments`
    OUT=`echo -e "${OUT}\n${LINE}"`
done

echo "$OUT" | column -s, -t
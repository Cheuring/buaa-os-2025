n=9
if (($# == 0))
then
	cat ./stderr.txt
elif (($# == 1))
	sed -n "$1,\$p" ./stderr.txt
then
	sed -n "$1,$(($2 - 1))p" ./stderr.txt
fi

n=9
if [[ $# -eq 0 ]]
then
	cat ./stderr.txt
elif [[ $# -eq 1 ]]
then
	sed -n "$1,\$p" ./stderr.txt
else
	sed -n "$1,$(($2 - 1))p" ./stderr.txt
fi

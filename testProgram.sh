mkdir output
rm testRes.txt
touch testRes.txt

dd=$(cd ./dataSet && ls | grep '*.in')
cc=1
for i in $dd; do
	./test.out ./dataSet/$i.in > ./output/$i.out
	if [ diff ./dataSet/$i.out ./output/$i.out ]
	then
		echo "$i 1" >> testRes.txt
	else
		echo "$i 0" >> testRes.txt
		cc=0
	fi
done
echo $cc > temp.txt
cat temp.txt testRes.txt > testRes.txt
rm temp.txt

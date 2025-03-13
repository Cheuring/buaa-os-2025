mkdir codeSet
dd=$(ls ./code | sed 's/.sy//g')
echo "#include\"include/libsy.h\"" > temp.txt

for i in $dd; do
	cat temp.txt ./code/$i.sy > ./codeSet/$i.c
done

ff=$(ls ./codeSet)
for i in $ff; do
	sed -i 's/getInt/getint/g' ./codeSet/$i
done

rm temp.txt

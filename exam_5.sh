for i in {0..20}; do
	sed "s/REPLACE/${i}/g" ./origin/code/$i.c > ./result/code/$i.c
done

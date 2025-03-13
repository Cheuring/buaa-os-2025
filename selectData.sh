mkdir dataSet

if [[ $1 == 'all' ]]
then
	cp -r ./data/* ./dataSet
else
	cp ./data/$1* ./dataSet
fi

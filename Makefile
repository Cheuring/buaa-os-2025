.PHONY: clean

out: calc case_all
	./calc < case_all > out

op = add sub mul div
case_all: case_add case_sub case_mul case_div
	for i in $(op); do \
		cat case_$$i >> case_all; \
	done

case_%: casegen
	./casegen $* 100 > case_$*

casegen: casegen.c
	gcc -o casegen casegen.c

calc: calc.c
	gcc -o calc calc.c

clean:
	rm -f out calc casegen case_* *.o

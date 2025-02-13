.PHONY: clean

out: calc case_all
	./calc < case_all > out

OP = add sub mul div

case_all: $(OP:%=case_%)
	rm -f case_all
	for i in $(OP); do \
		cat case_$$i >> case_all; \
	done

case_%: casegen
	./casegen $* 100 > $@

# casegen: casegen.c
# 	gcc -o $@ $<

# calc: calc.c
# 	gcc -o $@ $<
casegen calc: %: %.c
	gcc -o $@ $<

clean:
	rm -f out calc casegen case_* *.o

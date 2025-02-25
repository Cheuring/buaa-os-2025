.PHONY: clean sub

all: fibo

sub:
	$(MAKE) -C ./code

fibo: sub
	gcc ./code/*.o -o $@

clean:
	$(MAKE) -C ./code clean

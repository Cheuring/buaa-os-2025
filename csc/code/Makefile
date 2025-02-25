.PHONY: clean

all: fibo.o main.o

%.o: %.c
	gcc -I ../include -c $< -o $@

clean:
	rm *.o

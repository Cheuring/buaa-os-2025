
all: check
	gcc -o ./out/main -I./src/include ./src/main.c ./src/output.c
check: check.c
	gcc -c $< -o $@.o

run: ./out/main
	./out/main
clean:
	rm *.o ./out/main
.PHONY: clean run

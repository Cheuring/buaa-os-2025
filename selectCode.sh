rm testfile.c

link ./codeSet/$1.c testfile.c 
gcc -o test.out -I./include testfile.c

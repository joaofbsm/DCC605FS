gcc -g -std=c99 -Wall -c fs.c
gcc -g -std=c99 -Wall -I. tests/test5.c fs.o -o test5

#!/bin/bash
set -u

i=5

gcc -g -std=c99 -Wall -c fs.c 
gcc -g -std=c99 -Wall -I. tests/test$i.c fs.o -o test$i 

exit 0

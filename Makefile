all: iss

iss: sim.c sim.h
	gcc -Wall sim.c -o iss


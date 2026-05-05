all: asm

asm: asm.c
	gcc -Wall asm.c -o asm

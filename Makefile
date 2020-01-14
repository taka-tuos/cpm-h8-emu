TARGET	= cpm-h8
OBJ		= main.o z80cpm.o z80emu.o xprintf.o

PREFIX	= 

CFLAGS	:= -O2 -g -std=gnu99
ASFLAGS	:= 
LDFLAGS	:= 
LIBS	:= -lm -lc

include Makefile.in

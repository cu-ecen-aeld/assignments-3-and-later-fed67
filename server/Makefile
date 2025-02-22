CC ?= gcc

ifdef CROSS_COMPILE 
	CC = $(CROSS_COMPILE)gcc
endif

all:
	$(CC) aesdsocket.c -g -O0 -o aesdsocket

default:
	$(CC) aesdsocket.c -g -O0 -o aesdsocket

clean:
	rm -f aesdsocket

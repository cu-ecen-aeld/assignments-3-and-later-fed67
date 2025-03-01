CC ?= gcc
CFLAGS ?= 
LDFLAGS ?= 


ifdef CROSS_COMPILE 
	CC = $(CROSS_COMPILE)gcc
endif

all:
	$(CC) $(CFLAGS) aesdsocket.c -g -O0 -o aesdsocket $(LDFLAGS)


clean:
	rm -f aesdsocket

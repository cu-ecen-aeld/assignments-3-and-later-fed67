CC ?= gcc
CFLAGS ?= -DUSE_AESD_CHAR_DEVICE
LDFLAGS += -lm

ifdef CROSS_COMPILE 
	CC = $(CROSS_COMPILE)gcc
endif

all:
	$(CC) $(CFLAGS) aesdsocket.c -g -O0 -o aesdsocket $(LDFLAGS) -I../aesd-char-driver


clean:
	rm -f aesdsocket

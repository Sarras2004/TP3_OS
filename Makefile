CC      = gcc
CFLAGS  = -Wall -Werror
LDFLAGS = -lreadline -lpthread

SRCS    = biceps.c beuip.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all memory-leak clean

all: biceps

biceps: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

memory-leak: CFLAGS += -g -O0
memory-leak: $(OBJS)
	$(CC) $(CFLAGS) -o biceps-memory-leaks $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o biceps biceps-memory-leaks .biceps_history

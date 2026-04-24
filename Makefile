CC      = gcc
CFLAGS  = -Wall -Werror -pthread
LDFLAGS = -pthread

SRC    = biceps.c
TARGET = biceps
TARGET_MEMCHK = biceps-memory-leaks

.PHONY: all memory-leak clean

all: $(TARGET)

$(TARGET): $(SRC) creme.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

memory-leak: $(SRC) creme.h
	$(CC) $(CFLAGS) -g -O0 $(LDFLAGS) -o $(TARGET_MEMCHK) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET_MEMCHK)
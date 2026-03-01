# Makefile for UD Messaging example

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I../common
LDFLAGS  = -libverbs

COMMON   = ../common/rdma_common.c
TARGET   = ud_send
SRC      = ud_send.c $(COMMON)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) ../common/rdma_common.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

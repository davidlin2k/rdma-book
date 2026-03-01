# Makefile for RC RDMA Read example

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I../common
LDFLAGS  = -libverbs

COMMON   = ../common/rdma_common.c
TARGET   = rdma_read
SRC      = rdma_read.c $(COMMON)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) ../common/rdma_common.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

# Makefile for RC RDMA Write example

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I../common
LDFLAGS  = -libverbs

COMMON   = ../common/rdma_common.c
TARGET   = rdma_write
SRC      = rdma_write.c $(COMMON)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) ../common/rdma_common.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

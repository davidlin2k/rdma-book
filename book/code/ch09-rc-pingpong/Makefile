# Makefile for RC Send/Receive Ping-Pong example

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I../common
LDFLAGS  = -libverbs

COMMON   = ../common/rdma_common.c
TARGET   = rc_pingpong
SRC      = rc_pingpong.c $(COMMON)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) ../common/rdma_common.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

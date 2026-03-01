# Makefile for Chapter 12 RDMA Benchmark
#
# Prerequisites: libibverbs development headers
#   Ubuntu/Debian: apt install libibverbs-dev
#   RHEL/CentOS:   yum install libibverbs-devel

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
LDFLAGS = -libverbs -lpthread

TARGET  = rdma_bench
SRCS    = rdma_bench.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

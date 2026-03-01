# Makefile for Chapter 11: Shared Receive Queue (SRQ) Example
#
# Prerequisites: rdma-core development libraries
#   sudo apt-get install libibverbs-dev  (Debian/Ubuntu)
#   sudo yum install libibverbs-devel    (RHEL/CentOS)

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
LDFLAGS = -libverbs -lpthread

TARGETS = srq_example

.PHONY: all clean

all: $(TARGETS)

srq_example: srq_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)

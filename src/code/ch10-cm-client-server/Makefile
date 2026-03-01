# Makefile for Chapter 10: RDMA_CM Client-Server Example
#
# Prerequisites: rdma-core development libraries
#   sudo apt-get install libibverbs-dev librdmacm-dev  (Debian/Ubuntu)
#   sudo yum install libibverbs-devel librdmacm-devel  (RHEL/CentOS)

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
LDFLAGS = -lrdmacm -libverbs

TARGETS = cm_server cm_client

.PHONY: all clean

all: $(TARGETS)

cm_server: cm_server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

cm_client: cm_client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)

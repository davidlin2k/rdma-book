# Makefile for Chapter 8: hello_verbs
#
# Build:
#   make            - build hello_verbs
#   make clean      - remove built files
#   make run        - build and run (uses first available RDMA device)
#   make run DEV=rxe0  - build and run with a specific device
#
# Prerequisites:
#   - libibverbs-dev (Debian/Ubuntu) or rdma-core-devel (RHEL/Fedora)
#   - An RDMA device (e.g., Soft-RoCE: sudo modprobe rdma_rxe &&
#     sudo rdma link add rxe0 type rxe netdev eth0)

CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -O2 -g -std=c11
LDFLAGS  = -libverbs

# Optional: use pkg-config if available
PKGCONFIG := $(shell pkg-config --exists libibverbs 2>/dev/null && echo yes)
ifeq ($(PKGCONFIG),yes)
  CFLAGS  += $(shell pkg-config --cflags libibverbs)
  LDFLAGS  = $(shell pkg-config --libs libibverbs)
endif

# Device name for 'make run' (override with: make run DEV=mlx5_0)
DEV ?=

TARGETS = hello_verbs

# ---- Rules ----

all: $(TARGETS)

hello_verbs: hello_verbs.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: hello_verbs
	./hello_verbs $(DEV)

clean:
	rm -f $(TARGETS)

.PHONY: all clean run

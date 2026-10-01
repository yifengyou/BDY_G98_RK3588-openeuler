# SPDX-License-Identifier: GPL-2.0
# Makefile for mfs demo

CFLAGS = -Wall -Wextra

PROGS := mfsd mfsd_prefetch

all: $(PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) $(PROGS)

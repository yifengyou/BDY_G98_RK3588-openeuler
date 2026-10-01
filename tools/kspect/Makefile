# SPDX-License-Identifier: GPL-2.0

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/share/man/man1

MAN1 = ifstool.1
TARGET  = ifstool

all: man

man: $(MAN1)

install-man:
	install -d $(MANDIR)
	install -m 0644 $(MAN1) $(MANDIR)

install-tools:
	install -d $(BINDIR)
	install -m 0755 $(TARGET) $(BINDIR)

install: install-tools install-man

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(MANDIR)/$(MAN1)

.PHONY: all install-tools install-man install

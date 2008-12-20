# File: Makefile
#
# Copyright (C) 2008 Andreas Gruenbacher <agruen@suse.de>, SUSE Labs
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this program. If not, see http://www.gnu.org/licenses/.
#
VERSION := 0.9.2
RELEASE := $(shell date +%Y%m%d)
CFLAGS := -g -Wall

prefix := /usr/local
bindir := $(prefix)/bin
mandir := $(prefix)/man

ALL_TESTS := $(wildcard test/*.test)
ROOT_TESTS := $(wildcard test/root-*.test)
BROKEN_TESTS :=  $(wildcard test/broken-*.test)
TESTS := $(filter-out $(BROKEN_TESTS) $(ROOT_TESTS),$(ALL_TESTS))
ifeq ($(shell whoami),root)
TESTS += $(ROOT_TESTS)
endif

SOURCES := Makefile queue.[ch] pty_fork.[ch] shrun.c shrun.1 TODO COPYING \
	   shrun.spec.in .gitignore test/.gitignore $(ALL_TESTS)

all: shrun

shrun: shrun.o queue.o pty_fork.o

%.ok: PATH := $(CURDIR):$(PATH)
%.ok: %.test shrun
	@echo "[$<]"
	@shrun $(<)
	@touch $@

check: $(TESTS:.test=.ok)

install:
	install -d $(DESTDIR)$(bindir)
	install -m 755 -s shrun $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(mandir)/man1
	install -m 644 shrun.1 $(DESTDIR)$(mandir)/man1

uninstall:
	rm -f $(DESTDIR)$(bindir)/shrun
	rm -f $(DESTDIR)$(mandir)/man1/shrun.1

dist:
	@ln -s . shrun-$(VERSION)
	tar czf shrun-$(VERSION).tar.gz \
		$(patsubst %,shrun-$(VERSION)/%,$(SOURCES))
	@rm shrun-$(VERSION)
 
rpm: dist
	@sed -e 's:@VERSION@:$(VERSION):g' \
	     -e 's:@RELEASE@:$(RELEASE):g' \
	     shrun.spec.in > shrun.spec
	@rm -rf rpmbuild
	@mkdir -p rpmbuild rpm/src
	rpmbuild --eval '%define _sourcedir $(CURDIR)' \
		 --eval '%define _srcrpmdir $(CURDIR)/rpm/src' \
		 --eval '%define _rpmdir $(CURDIR)/rpm' \
		 --eval '%define _builddir $(CURDIR)/rpmbuild' \
		 --eval '%define _specdir $(CURDIR)' \
		 -ba shrun.spec
	@rm -f shrun.spec
	@rm -rf rpmbuild

clean:
	rm -f queue.o pty_fork.o shrun.o shrun $(ALL_TESTS:.test=.ok) shrun.spec
	rm -rf rpmbuild

.PHONY: all check install uninstall dist rpm clean

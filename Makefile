VERSION := 0.9.1
CFLAGS := -g -Wall

ALL_TESTS := $(wildcard test/*.test)
ROOT_TESTS := $(wildcard test/root-*.test)
BROKEN_TESTS :=  $(wildcard test/broken-*.test)
TESTS := $(filter-out $(BROKEN_TESTS) $(ROOT_TESTS),$(ALL_TESTS))
ifeq ($(shell whoami),root)
TESTS += $(ROOT_TESTS)
endif

all: shrun

shrun: shrun.o queue.o pty_fork.o

%.ok: PATH := $(CURDIR):$(PATH)
%.ok: %.test shrun
	@echo "[$<]"
	@shrun $(<)
	@touch $@

.PHONY: check
check: $(TESTS:.test=.ok)

dist:
	ln -s . shrun-$(VERSION)
	tar czf shrun-$(VERSION).tar.gz $(patsubst %,shrun-$(VERSION)/%,Makefile queue.[ch] pty_fork.[ch] shrun.c shrun.1 TODO COPYING $(ALL_TESTS))
	rm shrun-$(VERSION)

clean:
	rm -f queue.o pty_fork.o shrun.o shrun $(ALL_TESTS:.test=.ok)

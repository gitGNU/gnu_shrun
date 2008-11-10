VERSION := 0.8
CFLAGS := -g -Wall
TESTS := $(wildcard test/*.test)
BROKEN_TESTS := $(wildcard test/broken*.test)

all: shrun

shrun: shrun.o queue.o

%.ok: PATH := $(CURDIR):$(PATH)
%.ok: %.test shrun
	@echo "[$<]"
	@shrun $(<)
	@touch $@

.PHONY: check
check: $(patsubst %.test,%.ok,$(filter-out $(BROKEN_TESTS),$(TESTS)))

dist:
	ln -s . shrun-$(VERSION)
	tar cvzf shrun-$(VERSION).tar.gz shrun-$(VERSION)/{Makefile,queue.[ch],shrun.c,README,TODO,COPYING} $(TESTS:%=shrun-$(VERSION)/%)
	rm shrun-$(VERSION)

clean:
	rm -f iobuffer.o iobuffer queue.o shrun.o shrun $(TESTS:.test=.ok)

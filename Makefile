VERSION := 0.9
CFLAGS := -g -Wall
BROKEN_TESTS := $(wildcard test/broken*.test)
TESTS := $(filter-out $(BROKEN_TESTS),$(wildcard test/*.test))

all: shrun

shrun: shrun.o queue.o

%.ok: PATH := $(CURDIR):$(PATH)
%.ok: %.test shrun
	@echo "[$<]"
	@shrun $(<)
	@touch $@

.PHONY: check
check: $(TESTS:.test=.ok)

dist:
	ln -s . shrun-$(VERSION)
	tar czf shrun-$(VERSION).tar.gz $(patsubst %,shrun-$(VERSION)/%,Makefile queue.[ch] shrun.c README TODO COPYING $(TESTS) $(BROKEN_TESTS))
	rm shrun-$(VERSION)

clean:
	rm -f iobuffer.o iobuffer queue.o shrun.o shrun $(TESTS:.test=.ok)

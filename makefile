CC=gcc
RM=rm -f
INSTALL=install
CFLAGS=-std=c99 -s -nostdlib -fomit-frame-pointer -O3
PREF=/usr/local/
BIN=$(PREF)bin/

all: true false strip

true: true.c common.h
	@echo -n COMPILING
	@$(CC) $(CFLAGS) $^ -o $@
	@echo ... done.
false: false.c common.h
	@echo -n COMPILING
	@$(CC) $(CFLAGS) $^ -o $@
	@echo ... done.
.PHONY : clean install

clean:
	$(RM) true false

strip:
	@echo -n STRIPING
	@strip true
	@strip false
	@echo ... done.

install: all
	#$(INSTALL) true $(BIN)
	#$(INSTALL) false $(BIN)
	@echo "The following components should be installed manually"
	@echo "since they may break base system."
	@echo "  true, false

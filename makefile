CC=gcc
RM=rm -f
INSTALL=install
FREE_CFLAGS=-std=c99 -s -nostdlib -fomit-frame-pointer -O3
CFLAGS=-std=c99 -fomit-frame-pointer -O3
PREF=/usr/local/
BIN=$(PREF)bin/

all: true false quickexec autorestart strip

true: true.c common.h
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
false: false.c common.h
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
autorestart: autorestart.c
	@echo -n COMPILING
	@$(CC) $(CFLAGS) $^ -o $@
	@echo ... done.
quickexec: quickexec.c
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
	@strip quickexec
	@strip autorestart
	@echo ... done.

install: all
	$(INSTALL) quickexec $(BIN)
	$(INSTALL) autorestart $(BIN)
	#$(INSTALL) true $(BIN)
	#$(INSTALL) false $($IN)
	@echo "The following components should be installed manually"
	@echo "since they may break base system."
	@echo "  true, false"

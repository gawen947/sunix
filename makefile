CC=colorgcc
RM=rm -f
INSTALL=install
FREE_CFLAGS=-std=c99 -s -nostdlib -fomit-frame-pointer -O2 -ggdb
CFLAGS=-std=c99 -fomit-frame-pointer -O2 -ggdb
PREF=/usr/local/
BIN=$(PREF)bin/

TLIBC_SRC := tlibc.h
SUBARCH   := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
		                  -e s/arm.*/arm/ -e s/sa110/arm/ \
				       -e s/s390x/s390/ -e s/parisc64/parisc/ \
      				 -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
			      	 -e s/sh[234].*/sh/ )
ARCH      ?= $(SUBARCH)

ifeq ($(ARCH),i386)
        TLIBC_SRC += _i386_syscall.S _i386_syscall.c
endif
ifeq ($(ARCH),x86_64)
        TLIBC_SRC += _x86_64_syscall.S _x86_64_syscall.c
endif

all: true false quickexec autorestart uptime-ng scat strip

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
uptime-ng: uptime-ng.c
	@echo -n COMPILING
	@$(CC) $(CFLAGS) $^ -o $@
	@echo ... done.
scat: scat.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.

.PHONY : clean install

clean:
	$(RM) true false quickexec autorestart uptime-ng scat

strip:
	@echo -n STRIPING
	@strip true
	@strip false
	@strip quickexec
	@strip autorestart
	@strip uptime-ng
	@echo ... done.

install: all
	$(INSTALL) quickexec $(BIN)
	$(INSTALL) autorestart $(BIN)
	$(INSTALL) uptime-ng $(BIN)
	#$(INSTALL) true $(BIN)
	#$(INSTALL) false $($IN)
	@echo "The following components should be installed manually"
	@echo "since they may break base system."
	@echo "  true, false"

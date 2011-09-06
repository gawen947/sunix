CC=colorgcc
RM=rm -f
INSTALL=install
FREE_CFLAGS=-std=c99 -s -nostdlib -fomit-frame-pointer -O2
CFLAGS=-std=c99 -fomit-frame-pointer -O2
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

all: true false quickexec autorestart uptime-ng cat echo basename sleep unlink yes link strip

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
cat: cat.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
echo: echo.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
basename: basename.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
sleep: sleep.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
unlink: unlink.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
yes: yes.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.
link: link.c $(TLIBC_SRC)
	@echo -n COMPILING
	@$(CC) $(FREE_CFLAGS) $^ -o $@
	@echo ... done.

.PHONY : clean install

clean:
	$(RM) true false quickexec autorestart uptime-ng cat echo basename sleep unlink yes

strip:
	@echo -n STRIPING
	@strip true
	@strip false
	@strip cat
	@strip echo
	@strip sleep
	@strip basename
	@strip unlink
	@strip yes
	@strip quickexec
	@strip autorestart
	@strip uptime-ng
	@echo ... done.

core-install: all
	@echo "Installing core files, hope you've backed up coreutils"
	$(INSTALL) true /bin
	$(INSTALL) false /bin
	$(INSTALL) cat /bin
	$(INSTALL) echo /bin
	$(INSTALL) basename /usr/bin
	$(INSTALL) sleep /bin
	$(INSTALL) unlink /usr/bin
	$(INSTALL) yes /usr/bin

install: all
	$(INSTALL) quickexec $(BIN)
	$(INSTALL) autorestart $(BIN)
	$(INSTALL) uptime-ng $(BIN)
	@echo "The following components should be installed manually"
	@echo "since they may break base system."
	@echo "  true, false, cat, echo, basename, sleep, unlink, yes"

# for `make release' and `make html'
DESTDIR = .

# for `make install'
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SCRIPTDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/man/man1
INSTALL = install
IPROG =#   flags for installing programs (default none)
IDATA = -m 0644  # flags for installing data

all: base64.1 base64
progs: base64
man: base64.1

base64: base64.c
	$(CC) $(CFLAGS) -o $@ $<

%.1: %.but
	halibut --man=$@ $<

clean:
	rm -f *.1 base64 *.html *.tar.gz

html:
	halibut --html=base64.html base64.but
	mv base64.html $(DESTDIR)

release: base64.1
	mkdir -p reltmp/base64
	ln -s ../../base64.c reltmp/base64
	ln -s ../../base64.1 reltmp/base64
	ln -s ../../base64.but reltmp/base64
	ln -s ../../Makefile reltmp/base64
	tar -C reltmp -chzf $(DESTDIR)/base64.tar.gz base64
	rm -rf reltmp

install: install-progs install-man
install-progs: base64
	mkdir -p $(BINDIR)
	$(INSTALL) $(IPROG) base64 $(BINDIR)/base64
install-man: base64.1
	mkdir -p $(MANDIR)
	$(INSTALL) $(IDATA) base64.1 $(MANDIR)/base64.1

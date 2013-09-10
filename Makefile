
CC ?= gcc
CFLAGS ?= -g
#CFLAGS = -g -DDEBUG

PREFIX ?= /usr
PUBLIC_SEQUENCES = $(PREFIX)/share/AutoZen

# uncomment one of the OS= lines below if you're compiling on one of those OSen. 
#OS= -D__FreeBSD__
#OS= -D__OpenBSD__
#OS= -D__OpenBSD__

#DEBUG_LIBS= -lefence

all: autozen seq2wav

strip: autozen seq2wav
	strip autozen seq2wav

autozen: autozen.c *.xpm
	$(CC) $(OS) -D_REENTRANT -DPUBLIC_SEQUENCES='"$(PUBLIC_SEQUENCES)"' $(CFLAGS) `pkg-config --cflags gtk+-2.0` autozen.c -o autozen `pkg-config --libs gtk+-2.0`

seq2wav: seq2wav.c
	$(CC) seq2wav.c -o seq2wav -lm
clean: 
	rm -f autozen seq2wav

install: all
	install -d $(PREFIX)/bin
	install zentime $(PREFIX)/bin
	install -s seq2wav $(PREFIX)/bin
	install -s autozen $(PREFIX)/bin
	install -d $(PREFIX)/share/AutoZen
	install -m 644 *.seq $(PREFIX)/share/AutoZen
	install -d $(PREFIX)/share/doc/AutoZen/HTML/images
	install -m 644 doc/HTML/*.html $(PREFIX)/share/doc/AutoZen/HTML
	install -m 644 doc/HTML/images/* $(PREFIX)/share/doc/AutoZen/HTML/images
	install -d $(PREFIX)/man/man1
	install -m 644 doc/autozen.1 $(PREFIX)/man/man1

tags: *.[ch] *.xpm
	ctags *.[ch] *.xpm

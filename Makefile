PREFIX := /usr/local
CFLAGS=-g -Wall -Os

pstrings: pstrings.o 

clean:
	rm -f pstrings.o pstrings

install: pstrings pstrings.1
	cp pstrings ${PREFIX}/bin
	cp pstrings.1 ${PREFIX}/share/man/man1


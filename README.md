
= pstrings =

pstrings is a strings program for Linux processes. You specify the pid, and it dumps all the printable strings for the processes' address space. This can be useful to get data out of uncooperative programs.

To build:

	make 
	sudo make install       (to /usr/local/bin, overwrite with PREFIX=....) 

To use:

	pstrings $(pidof process-name)
	Options:
        -nMINLENGTH  only display strings of MINLENGTH and longer (default 4)
        -r           include read-only mappings (conflicts with -a)
        -x           include executable mappings (conflicts with -a)
        -a           include all mappings (conflicts with -r/-x)
        -o           prefix each string with address in program
        -p           prefix each string with pid
        -m           prefix each string with mapping name
        -fPERCENT    only output strings with at least PERCENT alpha-numeric characters
        -lLOCALE     use LOCALE to decide for printable strings (only 8bit));


Andi Kleen

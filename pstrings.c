/* strings on a running process 
 * 
 * Copyright (c) 2013 Intel Corporation
 * Author: Andi Kleen
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Compilation: make pstrings
 * Run without arguments for usage.
 * 
 * Notebook:
 * Process is stopped for full dump. Yield regularly?
 * Support for wide character sets.
 */
 
#define _GNU_SOURCE 1
#include <stdio.h>
#include <sys/ptrace.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <fnmatch.h>
#include <errno.h>
#include <stdarg.h>
#include <locale.h>

#define DEFAULT_PAT "rw[^x]?"

int minlength = 4;
char mappat[10] = DEFAULT_PAT; 
float filterp;

enum { 
	PID	 = (1 << 0),
	ADDRESS  = (1 << 1),
	MAPPING	 = (1 << 2),
	ALNUM_P  = (1 << 3),
} printflags;

/* Return nth field in a string. */ 
/* not generic. does not terminate field. no error check. */
char *nth_field(char *s, int n)
{
	int i;
	for (i = 0; i < n; i++) { 
		s += strcspn(s, " \t");
		s += strspn(s, " \t");
	}	
	return s;
}

/* Check percentage of alpha-numeric characters in a string */
int alnum_filter(char *s, int len)
{
	int i;
	int n = 0;
	float p;
	
	for (i = 0; i < len; i++)
		if (isalnum(s[i]))
			n++;
	p = ((float)n / (float)len) * 100;
	return p >= filterp;
}

struct context { 
	pid_t pid;
	char *mapping;
	char *buf;
};

/* Print a string with optional annotations. */
void dump(char *s, int len, unsigned long long addr, struct context *ctx)
{
	if (printflags) { 
		if ((printflags & ALNUM_P) && !alnum_filter(s, len))
			return;
		if (printflags & PID) 
			printf("%u:", ctx->pid);
		if (printflags & MAPPING)
			printf("%.*s:", (int)strcspn(ctx->mapping, "\n"), 
				ctx->mapping);
		if (printflags & ADDRESS)
			printf("%llx:", addr);
	}
	fwrite(s, 1, len, stdout);
	putchar('\n');
}

/* Find printable strings in a memory area. */
void strings(char *buf, int buflen, unsigned long long addr, struct context *ctx)
{
	int i;
	int len = 0;

	for (i = 0; i < buflen; i++) { 
		if (isprint(buf[i])) {
			len++; 
			continue;
		}
		if (len >= minlength)
			dump(buf + i - len, len, addr + i - len, ctx);
		len = 0;
	}
	if (len >= minlength)
		dump(buf + i - len, len, addr + i - len, ctx);
}

#define TRANSFER (1 << 20)

/* Copy and process a single mapping. */
int mapping_strings(int fd, unsigned long long start, 
			unsigned long long end, struct context *ctx)
{
	while (start < end) { 
		uint64_t len = end - start;
		if (len > TRANSFER)
			len = TRANSFER;

		int n = pread(fd, ctx->buf, len, start);
		if (n > 0) 
			strings(ctx->buf, n, start, ctx);	
		else if (n < 0)
			fprintf(stderr, "Cannot read %llx-%llx: %s\n", 
					start, start + len, 
					strerror(errno));

		start += len;
	}	
	return 0;
}

/* Find all mappings in a process */
int process_mappings(struct context *ctx, int memfd)
{
	char fn[64];

	snprintf(fn, sizeof fn, "/proc/%u/maps", ctx->pid);
	FILE *mf = fopen(fn, "r");
	if (!mf) { 
		perror(fn);
		return -1;
	}

	char *line = NULL;
	size_t linelen = 0;
	int n;
	while ((n = getline(&line, &linelen, mf)) > 0) { 
		unsigned long long start, end;
		char acc[10];

		if (sscanf(line, "%llx-%llx %9s", &start, &end, acc) != 3)
			continue;

		if (fnmatch(mappat, acc, 0) == 0) { 
			ctx->mapping = nth_field(line, 5);
			if (ctx->mapping[0] == '\n')
				ctx->mapping = "anon";
			mapping_strings(memfd, start, end, ctx);
		} 
	}
	free(line);
	fclose(mf);
	return 0;
}

/* Attach to a pid and call workers */
int attach(pid_t pid)
{
	int ret = -1;
	int memfd = -1;
	struct context ctx;

	if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) {
		perror("PTRACE_ATTACH");
		return -1;
	}

	if (waitpid(pid, NULL, 0) < 0)
		perror("waitpid");

	char fn[60];
	snprintf(fn, sizeof fn, "/proc/%u/mem", pid);
	memfd = open(fn, O_RDONLY);
	if (memfd < 0) {
		perror(fn);
		goto error;
	}

	ctx.buf = malloc(TRANSFER);
	if (!ctx.buf)
		exit(ENOMEM);
	ctx.pid = pid;

	ret = process_mappings(&ctx, memfd);
	free(ctx.buf);
error:
	close(memfd);
	if (ptrace(PTRACE_DETACH, pid, 0, 0) < 0)
		perror("PTRACE_DETACH");
	return ret;
}

void usage(void)
{
	fprintf(stderr, "Usage: pstrings options... pid ...\n");
	fprintf(stderr, 
	"Display strings in the memory of a running process.\n"
        "Options:\n"
	"-nMINLENGTH  only display strings of MINLENGTH and longer (default 4)\n"
	"-r           include read-only mappings (conflicts with -a)\n"
	"-x           include executable mappings (conflicts with -a)\n"
	"-a           include all mappings (conflicts with -r/-x)\n"
	"-o           prefix each string with address in program\n"
	"-p	      prefix each string with pid\n"
	"-m           prefix each string with mapping name\n"
	"-fPERCENT    only output strings with at least PERCENT alpha-numeric characters\n"
	"-lLOCALE     use LOCALE to decide for printable strings (only 8bit)\n");
	exit(1);
}

int main(int ac, char **av)
{
	int err = 0;
	int c;

	while ((c = getopt(ac, av, "n:rxf:mpoal:")) != -1) { 
		switch (c) { 
		case 'n':
			minlength = atoi(optarg);
			if (minlength < 1 || minlength > TRANSFER)
				usage();
			break;
		case 'r':
			if (mappat[0] == '*') 
				usage();
			mappat[1] = '?';		
			break;
		case 'x': 
			if (mappat[0] == '*') 
				usage();
			mappat[2] = '*';
			mappat[3] = 0;
			break;
		case 'a':
			if (strcmp(mappat, DEFAULT_PAT))
				usage();
			strcpy(mappat, "*");
			break;
		case 'm':
			printflags |= MAPPING;
			break;
		case 'p':
			printflags |= PID;
			break;
		case 'o':
			printflags |= ADDRESS;
			break;
		case 'f':
			printflags |= ALNUM_P;
			if (sscanf(optarg, "%f", &filterp) != 1)
				usage();
			break;
		case 'l':
			if (setlocale(LC_CTYPE, optarg) == NULL) {
				fprintf(stderr, "Cannot set locale `%s'\n", optarg);
				exit(1);
			}
			break;
		default:
			usage();
		}
		
	}	
	if (optind == ac)
		usage();
	av += optind - 1;

	while (*++av) { 
		char *end;
		long pid = strtoul(*av, &end, 0);
		if (end == *av)
			usage();
		err |= attach(pid);
	}
	return err;
}

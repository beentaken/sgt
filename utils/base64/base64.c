#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define isbase64(c) (    ((c) >= 'A' && (c) <= 'Z') || \
                         ((c) >= 'a' && (c) <= 'z') || \
                         ((c) >= '0' && (c) <= '9') || \
                         (c) == '+' || (c) == '/' || (c) == '=' \
                         )

int base64_decode_atom(char *atom, unsigned char *out) {
    int vals[4];
    int i, v, len;
    unsigned word;
    char c;
    
    for (i = 0; i < 4; i++) {
	c = atom[i];
	if (c >= 'A' && c <= 'Z')
	    v = c - 'A';
	else if (c >= 'a' && c <= 'z')
	    v = c - 'a' + 26;
	else if (c >= '0' && c <= '9')
	    v = c - '0' + 52;
	else if (c == '+')
	    v = 62;
	else if (c == '/')
	    v = 63;
	else if (c == '=')
	    v = -1;
	else
	    return 0;		       /* invalid atom */
	vals[i] = v;
    }

    if (vals[0] == -1 || vals[1] == -1)
	return 0;
    if (vals[2] == -1 && vals[3] != -1)
	return 0;

    if (vals[3] != -1)
	len = 3;
    else if (vals[2] != -1)
	len = 2;
    else
	len = 1;

    word = ((vals[0] << 18) |
	    (vals[1] << 12) |
	    ((vals[2] & 0x3F) << 6) |
	    (vals[3] & 0x3F));
    out[0] = (word >> 16) & 0xFF;
    if (len > 1)
	out[1] = (word >> 8) & 0xFF;
    if (len > 2)
	out[2] = word & 0xFF;
    return len;
}

void base64_encode_atom(unsigned char *data, int n, char *out) {
    static const char base64_chars[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    unsigned word;

    word = data[0] << 16;
    if (n > 1)
	word |= data[1] << 8;
    if (n > 2)
	word |= data[2];
    out[0] = base64_chars[(word >> 18) & 0x3F];
    out[1] = base64_chars[(word >> 12) & 0x3F];
    if (n > 1)
	out[2] = base64_chars[(word >> 6) & 0x3F];
    else
	out[2] = '=';
    if (n > 2)
	out[3] = base64_chars[word & 0x3F];
    else
	out[3] = '=';
}

const char usagemsg[] =
    "usage: base64 [-d] [filename]        decode from a file or from stdin\n"
    "   or: base64 -e [-cNNN] [filename]  encode from a file or from stdin\n"
    "where: -d     decode mode (default)\n"
    "       -e     encode mode\n"
    "       -cNNN  set number of chars per line for encoded output\n"
    " also: base64 --version              report version number\n"
    "       base64 --help                 display this help text\n"
    "       base64 --licence              display the (MIT) licence text\n"
    ;

void usage(void) {
    fputs(usagemsg, stdout);
}

const char licencemsg[] =
    "base64 is copyright 2001,2004 Simon Tatham.\n"
    "\n"
    "Permission is hereby granted, free of charge, to any person\n"
    "obtaining a copy of this software and associated documentation files\n"
    "(the \"Software\"), to deal in the Software without restriction,\n"
    "including without limitation the rights to use, copy, modify, merge,\n"
    "publish, distribute, sublicense, and/or sell copies of the Software,\n"
    "and to permit persons to whom the Software is furnished to do so,\n"
    "subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be\n"
    "included in all copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n"
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\n"
    "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n"
    "NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS\n"
    "BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN\n"
    "ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN\n"
    "CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
    "SOFTWARE.\n"
    ;

void licence(void) {
    fputs(licencemsg, stdout);
}

void version(void) {
#define SVN_REV "$Revision$"
    char rev[sizeof(SVN_REV)];
    char *p, *q;

    strcpy(rev, SVN_REV);

    for (p = rev; *p && *p != ':'; p++);
    if (*p) {
        p++;
        while (*p && isspace(*p)) p++;
        for (q = p; *q && *q != '$'; q++);
        if (*q) *q = '\0';
        printf("base64 revision %s\n", p);
    } else {
        printf("base64: unknown version\n");
    }
}

int main(int ac, char **av) {
    int encoding = 0;
    int cpl = 64;
    FILE *fp;
    char *fname;
    char *eptr;

    fname = NULL;

    while (--ac) {
        char *v, *p = *++av;
        if (*p == '-') {
            while (*p) {
                char c = *++p;
                switch (c) {
                  case '-':
		    p++;
                    if (!strcmp(p, "version")) {
                        version();
                        exit(0);
                    } else if (!strcmp(p, "help")) {
                        usage();
                        exit(0);
                    } else if (!strcmp(p, "licence") ||
			!strcmp(p, "license")) {
                        licence();
                        exit(0);
                    } else {
                        fprintf(stderr, "base64: unknown long option '--%s'\n",
				p);
			exit(1);
		    }
                    break;
                  case 'v':
                  case 'V':
                    version();
                    exit(0);
                    break;
                  case 'h':
                  case 'H':
                    usage();
                    exit(0);
                    break;
                  case 'd':
                    encoding = 0;
                    break;
                  case 'e':
                    encoding = 1;
                    break;
                  case 'c':
		    /*
		     * Options requiring values.
		     */
		    v = p+1;
		    if (!*v && ac > 1) {
			--ac;
			v = *++av;
		    }
		    if (!*v) {
                        fprintf(stderr, "base64: option '-%c' expects"
                                " an argument\n", c);
			exit(1);
		    }
		    switch (c) {
		      case 'c':
			cpl = strtol(v, &eptr, 10);
			if (eptr && *eptr) {
			    fprintf(stderr, "base64: option -c expects"
				    " a numeric argument\n");
			    exit(1);
			}
			if (cpl % 4) {
			    fprintf(stderr, "base64: chars per line should be"
				    " divisible by 4\n");
			    exit(1);
			}
			break;
		    }
		    p = "";
                    break;
                }
            }
        } else {
            if (!fname)
                fname = p;
            else {
                fprintf(stderr, "base64: expected only one filename\n");
                exit(0);
            }
        }
    }

    if (fname) {
        fp = fopen(fname, encoding ? "rb" : "r");
	if (!fp) {
	    fprintf(stderr, "base64: unable to open '%s': %s\n", fname,
		    strerror(errno));
	    exit(1);
	}
    } else
        fp = stdin;

    if (encoding) {
        unsigned char in[3];
        char out[4];
        int column;
        int n;

        column = 0;
        while (1) {
            if (cpl && column >= cpl) {
                putchar('\n');
                column = 0;
            }
            n = fread(in, 1, 3, fp);
            if (n == 0) break;
            base64_encode_atom(in, n, out);
            fwrite(out, 1, 4, stdout);
            column += 4;
        }

        putchar('\n');
    } else {
        char in[4];
        unsigned char out[3];
        int c, i, n, eof;

        eof = 0;
        do {
            for (i = 0; i < 4; i++) {
                do {
                    c = fgetc(fp);
                } while (c != EOF && !isbase64(c));
                if (c == EOF) {
                    eof = 1;
                    break;
                }
                in[i] = c;
            }
            if (i > 0) {
                if (i < 4) {
                    fprintf(stderr, "base64: warning: number of base64"
                            " characters was not a multiple of 4\n");
                    while (i < 4) in[i++] = '=';
                }
                n = base64_decode_atom(in, out);
                fwrite(out, 1, n, stdout);
            }
        } while (!eof);
    }

    if (fname)
        fclose(fp);

    return 0;
}

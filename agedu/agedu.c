/*
 * Main program for agedu.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fnmatch.h>

#include "du.h"
#include "trie.h"
#include "index.h"
#include "malloc.h"
#include "html.h"
#include "httpd.h"

#define PNAME "agedu"

#define lenof(x) (sizeof((x))/sizeof(*(x)))

void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", PNAME);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

struct inclusion_exclusion {
    int include;
    const char *wildcard;
    int path;
};

struct ctx {
    triebuild *tb;
    dev_t datafile_dev, filesystem_dev;
    ino_t datafile_ino;
    time_t last_output_update;
    int progress, progwidth;
    struct inclusion_exclusion *inex;
    int ninex;
    int crossfs;
};

static int gotdata(void *vctx, const char *pathname, const struct stat64 *st)
{
    struct ctx *ctx = (struct ctx *)vctx;
    struct trie_file file;
    time_t t;
    int i, include;
    const char *filename;

    /*
     * Filter out our own data file.
     */
    if (st->st_dev == ctx->datafile_dev && st->st_ino == ctx->datafile_ino)
	return 0;

    /*
     * Don't cross the streams^W^Wany file system boundary.
     */
    if (!ctx->crossfs && st->st_dev != ctx->filesystem_dev)
	return 0;

    /*
     * Filter based on wildcards.
     */
    include = 1;
    filename = strrchr(pathname, '/');
    if (!filename)
	filename = pathname;
    else
	filename++;
    for (i = 0; i < ctx->ninex; i++) {
	if (fnmatch(ctx->inex[i].wildcard,
		    ctx->inex[i].path ? pathname : filename,
		    FNM_PATHNAME) == 0)
	    include = ctx->inex[i].include;
    }
    if (!include)
	return 1;		       /* filter, but don't prune */

    file.blocks = st->st_blocks;
    file.atime = st->st_atime;
    triebuild_add(ctx->tb, pathname, &file);

    t = time(NULL);
    if (t != ctx->last_output_update) {
	if (ctx->progress) {
	    fprintf(stderr, "%-*.*s\r", ctx->progwidth, ctx->progwidth,
		    pathname);
	    fflush(stderr);
	}
	ctx->last_output_update = t;
    }

    return 1;
}

static void text_query(const void *mappedfile, const char *rootdir,
		       time_t t, int depth)
{
    size_t maxpathlen;
    char *pathbuf;
    unsigned long xi1, xi2;
    unsigned long long s1, s2;

    maxpathlen = trie_maxpathlen(mappedfile);
    pathbuf = snewn(maxpathlen + 1, char);

    /*
     * We want to query everything between the supplied filename
     * (inclusive) and that filename with a ^A on the end
     * (exclusive). So find the x indices for each.
     */
    sprintf(pathbuf, "%s\001", rootdir);
    xi1 = trie_before(mappedfile, rootdir);
    xi2 = trie_before(mappedfile, pathbuf);

    /*
     * Now do the lookups in the age index.
     */
    s1 = index_query(mappedfile, xi1, t);
    s2 = index_query(mappedfile, xi2, t);

    /* Display in units of 2 512-byte blocks = 1Kb */
    printf("%-11llu %s\n", (s2 - s1) / 2, rootdir);

    if (depth > 0) {
	/*
	 * Now scan for first-level subdirectories and report
	 * those too.
	 */
	xi1++;
	while (xi1 < xi2) {
	    trie_getpath(mappedfile, xi1, pathbuf);
	    text_query(mappedfile, pathbuf, t, depth-1);
	    strcat(pathbuf, "\001");
	    xi1 = trie_before(mappedfile, pathbuf);
	}
    }
}

/*
 * Largely frivolous way to define all my command-line options. I
 * present here a parametric macro which declares a series of
 * _logical_ option identifiers, and for each one declares zero or
 * more short option characters and zero or more long option
 * words. Then I repeatedly invoke that macro with its arguments
 * defined to be various other macros, which allows me to
 * variously:
 * 
 *  - define an enum allocating a distinct integer value to each
 *    logical option id
 *  - define a string consisting of precisely all the short option
 *    characters
 *  - define a string array consisting of all the long option
 *    strings
 *  - define (with help from auxiliary enums) integer arrays
 *    parallel to both of the above giving the logical option id
 *    for each physical short and long option
 *  - define an array indexed by logical option id indicating
 *    whether the option in question takes a value.
 *
 * It's not at all clear to me that this trickery is actually
 * particularly _efficient_ - it still, after all, requires going
 * linearly through the option list at run time and doing a
 * strcmp, whereas in an ideal world I'd have liked the lists of
 * long and short options to be pre-sorted so that a binary search
 * or some other more efficient lookup was possible. (Not that
 * asymptotic algorithmic complexity is remotely vital in option
 * parsing, but if I were doing this in, say, Lisp or something
 * with an equivalently powerful preprocessor then once I'd had
 * the idea of preparing the option-parsing data structures at
 * compile time I would probably have made the effort to prepare
 * them _properly_. I could have Perl generate me a source file
 * from some sort of description, I suppose, but that would seem
 * like overkill. And in any case, it's more of a challenge to
 * achieve as much as possible by cunning use of cpp and enum than
 * to just write some sensible and logical code in a Turing-
 * complete language. I said it was largely frivolous :-)
 *
 * This approach does have the virtue that it brings together the
 * option ids and option spellings into a single combined list and
 * defines them all in exactly one place. If I want to add a new
 * option, or a new spelling for an option, I only have to modify
 * the main OPTIONS macro below and then add code to process the
 * new logical id.
 *
 * (Though, really, even that isn't ideal, since it still involves
 * modifying the source file in more than one place. In a
 * _properly_ ideal world, I'd be able to interleave the option
 * definitions with the code fragments that process them. And then
 * not bother defining logical identifiers for them at all - those
 * would be automatically generated, since I wouldn't have any
 * need to specify them manually in another part of the code.)
 */

#define OPTIONS(NOVAL, VAL, SHORT, LONG) \
    NOVAL(HELP) SHORT(h) LONG(help) \
    NOVAL(VERSION) SHORT(V) LONG(version) \
    NOVAL(LICENCE) LONG(licence) LONG(license) \
    NOVAL(SCAN) SHORT(s) LONG(scan) \
    NOVAL(DUMP) SHORT(d) LONG(dump) \
    NOVAL(TEXT) SHORT(t) LONG(text) \
    NOVAL(HTML) SHORT(H) LONG(html) \
    NOVAL(HTTPD) SHORT(w) LONG(web) LONG(server) LONG(httpd) \
    NOVAL(PROGRESS) LONG(progress) LONG(scan_progress) \
    NOVAL(NOPROGRESS) LONG(no_progress) LONG(no_scan_progress) \
    NOVAL(TTYPROGRESS) LONG(tty_progress) LONG(tty_scan_progress) \
		       LONG(progress_tty) LONG(scan_progress_tty) \
    NOVAL(CROSSFS) LONG(cross_fs) \
    NOVAL(NOCROSSFS) LONG(no_cross_fs) \
    VAL(DATAFILE) SHORT(f) LONG(file) \
    VAL(MINAGE) SHORT(a) LONG(age) LONG(min_age) LONG(minimum_age) \
    VAL(AUTH) LONG(auth) LONG(http_auth) LONG(httpd_auth) \
              LONG(server_auth) LONG(web_auth) \
    VAL(INCLUDE) LONG(include) \
    VAL(INCLUDEPATH) LONG(include_path) \
    VAL(EXCLUDE) LONG(exclude) \
    VAL(EXCLUDEPATH) LONG(exclude_path)

#define IGNORE(x)
#define DEFENUM(x) OPT_ ## x,
#define ZERO(x) 0,
#define ONE(x) 1,
#define STRING(x) #x ,
#define STRINGNOCOMMA(x) #x
#define SHORTNEWOPT(x) SHORTtmp_ ## x = OPT_ ## x,
#define SHORTTHISOPT(x) SHORTtmp2_ ## x, SHORTVAL_ ## x = SHORTtmp2_ ## x - 1,
#define SHORTOPTVAL(x) SHORTVAL_ ## x,
#define SHORTTMP(x) SHORTtmp3_ ## x,
#define LONGNEWOPT(x) LONGtmp_ ## x = OPT_ ## x,
#define LONGTHISOPT(x) LONGtmp2_ ## x, LONGVAL_ ## x = LONGtmp2_ ## x - 1,
#define LONGOPTVAL(x) LONGVAL_ ## x,
#define LONGTMP(x) SHORTtmp3_ ## x,

enum { OPTIONS(DEFENUM,DEFENUM,IGNORE,IGNORE) NOPTIONS };
enum { OPTIONS(IGNORE,IGNORE,SHORTTMP,IGNORE) NSHORTOPTS };
enum { OPTIONS(IGNORE,IGNORE,IGNORE,LONGTMP) NLONGOPTS };
static const int opthasval[NOPTIONS] = {OPTIONS(ZERO,ONE,IGNORE,IGNORE)};
static const char shortopts[] = {OPTIONS(IGNORE,IGNORE,STRINGNOCOMMA,IGNORE)};
static const char *const longopts[] = {OPTIONS(IGNORE,IGNORE,IGNORE,STRING)};
enum { OPTIONS(SHORTNEWOPT,SHORTNEWOPT,SHORTTHISOPT,IGNORE) };
enum { OPTIONS(LONGNEWOPT,LONGNEWOPT,IGNORE,LONGTHISOPT) };
static const int shortvals[] = {OPTIONS(IGNORE,IGNORE,SHORTOPTVAL,IGNORE)};
static const int longvals[] = {OPTIONS(IGNORE,IGNORE,IGNORE,LONGOPTVAL)};

int main(int argc, char **argv)
{
    int fd, count;
    struct ctx actx, *ctx = &actx;
    struct stat st;
    off_t totalsize, realsize;
    void *mappedfile;
    triewalk *tw;
    indexbuild *ib;
    const struct trie_file *tf;
    char *filename = "agedu.dat";
    char *rootdir = NULL;
    int doing_opts = 1;
    enum { USAGE, TEXT, HTML, SCAN, DUMP, HTTPD } mode = USAGE;
    char *minage = "0d";
    int auth = HTTPD_AUTH_MAGIC | HTTPD_AUTH_BASIC;
    int progress = 1;
    struct inclusion_exclusion *inex = NULL;
    int ninex = 0, inexsize = 0;
    int crossfs = 0;

#ifdef DEBUG_MAD_OPTION_PARSING_MACROS
    {
	static const char *const optnames[NOPTIONS] = {
	    OPTIONS(STRING,STRING,IGNORE,IGNORE)
	};
	int i;
	for (i = 0; i < NSHORTOPTS; i++)
	    printf("-%c == %s [%s]\n", shortopts[i], optnames[shortvals[i]],
		   opthasval[shortvals[i]] ? "value" : "no value");
	for (i = 0; i < NLONGOPTS; i++)
	    printf("--%s == %s [%s]\n", longopts[i], optnames[longvals[i]],
		   opthasval[longvals[i]] ? "value" : "no value");
    }
#endif

    while (--argc > 0) {
        char *p = *++argv;

        if (doing_opts && *p == '-') {
	    int wordstart = 1;

            if (!strcmp(p, "--")) {
                doing_opts = 0;
		continue;
            }

	    p++;
	    while (*p) {
		int optid = -1;
		int i;
		char *optval;

		if (wordstart && *p == '-') {
		    /*
		     * GNU-style long option.
		     */
		    p++;
		    optval = strchr(p, '=');
		    if (optval)
			*optval++ = '\0';

		    for (i = 0; i < NLONGOPTS; i++) {
			const char *opt = longopts[i], *s = p;
			int match = 1;
			/*
			 * The underscores in the option names
			 * defined above may be given by the user
			 * as underscores or dashes, or omitted
			 * entirely.
			 */
			while (*opt) {
			    if (*opt == '_') {
				if (*s == '-' || *s == '_')
				    s++;
			    } else {
				if (*opt != *s) {
				    match = 0;
				    break;
				}
				s++;
			    }
			    opt++;
			}
			if (match && !*s) {
			    optid = longvals[i];
			    break;
			}
		    }

		    if (optid < 0) {
			fprintf(stderr, "%s: unrecognised option '--%s'\n",
				PNAME, p);
			return 1;
		    }

		    if (!opthasval[optid]) {
			if (optval) {
			    fprintf(stderr, "%s: unexpected argument to option"
				    " '--%s'\n", PNAME, p);
			    return 1;
			}
		    } else {
			if (!optval) {
			    if (--argc > 0) {
				optval = *++argv;
			    } else {
				fprintf(stderr, "%s: option '--%s' expects"
					" an argument\n", PNAME, p);
				return 1;
			    }
			}
		    }

		    p += strlen(p);    /* finished with this argument word */
		} else {
		    /*
		     * Short option.
		     */
                    char c = *p++;

		    for (i = 0; i < NSHORTOPTS; i++)
			if (c == shortopts[i]) {
			    optid = shortvals[i];
			    break;
			}

		    if (optid < 0) {
			fprintf(stderr, "%s: unrecognised option '-%c'\n",
				PNAME, c);
			return 1;
		    }

		    if (opthasval[optid]) {
                        if (*p) {
                            optval = p;
                            p += strlen(p);
                        } else if (--argc > 0) {
                            optval = *++argv;
                        } else {
                            fprintf(stderr, "%s: option '-%c' expects"
                                    " an argument\n", PNAME, c);
                            return 1;
                        }
		    } else {
			optval = NULL;
		    }
		}

		wordstart = 0;

		/*
		 * Now actually process the option.
		 */
		switch (optid) {
		  case OPT_HELP:
		    printf("FIXME: usage();\n");
		    return 0;
		  case OPT_VERSION:
		    printf("FIXME: version();\n");
		    return 0;
		  case OPT_LICENCE:
		    printf("FIXME: licence();\n");
		    return 0;
		  case OPT_SCAN:
		    mode = SCAN;
		    break;
		  case OPT_DUMP:
		    mode = DUMP;
		    break;
		  case OPT_TEXT:
		    mode = TEXT;
		    break;
		  case OPT_HTML:
		    mode = HTML;
		    break;
		  case OPT_HTTPD:
		    mode = HTTPD;
		    break;
		  case OPT_PROGRESS:
		    progress = 2;
		    break;
		  case OPT_NOPROGRESS:
		    progress = 0;
		    break;
		  case OPT_TTYPROGRESS:
		    progress = 1;
		    break;
		  case OPT_CROSSFS:
		    crossfs = 1;
		    break;
		  case OPT_NOCROSSFS:
		    crossfs = 0;
		    break;
		  case OPT_DATAFILE:
		    filename = optval;
		    break;
		  case OPT_MINAGE:
		    minage = optval;
		    break;
		  case OPT_AUTH:
		    if (!strcmp(optval, "magic"))
			auth = HTTPD_AUTH_MAGIC;
		    else if (!strcmp(optval, "basic"))
			auth = HTTPD_AUTH_BASIC;
		    else if (!strcmp(optval, "none"))
			auth = HTTPD_AUTH_NONE;
		    else if (!strcmp(optval, "default"))
			auth = HTTPD_AUTH_MAGIC | HTTPD_AUTH_BASIC;
		    else {
			fprintf(stderr, "%s: unrecognised authentication"
				" type '%s'\n%*s  options are 'magic',"
				" 'basic', 'none', 'default'\n",
				PNAME, optval, (int)strlen(PNAME), "");
			return 1;
		    }
		    break;
		  case OPT_INCLUDE:
		  case OPT_INCLUDEPATH:
		  case OPT_EXCLUDE:
		  case OPT_EXCLUDEPATH:
		    if (ninex >= inexsize) {
			inexsize = ninex * 3 / 2 + 16;
			inex = sresize(inex, inexsize,
				       struct inclusion_exclusion);
		    }
		    inex[ninex].path = (optid == OPT_INCLUDEPATH ||
					optid == OPT_EXCLUDEPATH);
		    inex[ninex].include = (optid == OPT_INCLUDE ||
					   optid == OPT_INCLUDEPATH);
		    inex[ninex].wildcard = optval;
		    ninex++;
		    break;
		}
	    }
        } else {
	    if (!rootdir) {
		rootdir = p;
	    } else {
		fprintf(stderr, "%s: unexpected argument '%s'\n", PNAME, p);
		return 1;
	    }
        }
    }

    if (!rootdir)
	rootdir = ".";

    if (mode == USAGE) {
	printf("FIXME: usage();\n");
	return 0;
    } else if (mode == SCAN) {

	fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, S_IRWXU);
	if (fd < 0) {
	    fprintf(stderr, "%s: %s: open: %s\n", PNAME, filename,
		    strerror(errno));
	    return 1;
	}

	if (stat(rootdir, &st) < 0) {
	    fprintf(stderr, "%s: %s: stat: %s\n", PNAME, rootdir,
		    strerror(errno));
	    return 1;
	}
	ctx->filesystem_dev = crossfs ? 0 : st.st_dev;

	if (fstat(fd, &st) < 0) {
	    perror("agedu: fstat");
	    return 1;
	}
	ctx->datafile_dev = st.st_dev;
	ctx->datafile_ino = st.st_ino;
	ctx->inex = inex;
	ctx->ninex = ninex;
	ctx->crossfs = crossfs;

	ctx->last_output_update = time(NULL);

	/* progress==1 means report progress only if stderr is a tty */
	if (progress == 1)
	    progress = isatty(2) ? 2 : 0;
	ctx->progress = progress;
	{
	    struct winsize ws;
	    if (progress && ioctl(2, TIOCGWINSZ, &ws) == 0)
		ctx->progwidth = ws.ws_col - 1;
	    else
		ctx->progwidth = 79;
	}

	/*
	 * Scan the directory tree, and write out the trie component
	 * of the data file.
	 */
	ctx->tb = triebuild_new(fd);
	du(rootdir, gotdata, ctx);
	count = triebuild_finish(ctx->tb);
	triebuild_free(ctx->tb);

	if (ctx->progress) {
	    fprintf(stderr, "%-*s\r", ctx->progwidth, "");
	    fflush(stderr);
	}

	/*
	 * Work out how much space the cumulative index trees will
	 * take; enlarge the file, and memory-map it.
	 */
	if (fstat(fd, &st) < 0) {
	    perror("agedu: fstat");
	    return 1;
	}

	printf("Built pathname index, %d entries, %ju bytes\n", count,
	       (intmax_t)st.st_size);

	totalsize = index_compute_size(st.st_size, count);

	if (lseek(fd, totalsize-1, SEEK_SET) < 0) {
	    perror("agedu: lseek");
	    return 1;
	}
	if (write(fd, "\0", 1) < 1) {
	    perror("agedu: write");
	    return 1;
	}

	printf("Upper bound on index file size = %ju bytes\n",
	       (intmax_t)totalsize);

	mappedfile = mmap(NULL, totalsize, PROT_READ|PROT_WRITE,MAP_SHARED, fd, 0);
	if (!mappedfile) {
	    perror("agedu: mmap");
	    return 1;
	}

	ib = indexbuild_new(mappedfile, st.st_size, count);
	tw = triewalk_new(mappedfile);
	while ((tf = triewalk_next(tw, NULL)) != NULL)
	    indexbuild_add(ib, tf);
	triewalk_free(tw);
	realsize = indexbuild_realsize(ib);
	indexbuild_free(ib);

	munmap(mappedfile, totalsize);
	ftruncate(fd, realsize);
	close(fd);
	printf("Actual index file size = %ju bytes\n", (intmax_t)realsize);
    } else if (mode == TEXT) {
	time_t t;
	struct tm tm;
	int nunits;
	char unit[2];
	size_t pathlen;

	t = time(NULL);

	if (2 != sscanf(minage, "%d%1[DdWwMmYy]", &nunits, unit)) {
	    fprintf(stderr, "%s: minimum age should be a number followed by"
		    " one of d,w,m,y\n", PNAME);
	    return 1;
	}

	if (unit[0] == 'd') {
	    t -= 86400 * nunits;
	} else if (unit[0] == 'w') {
	    t -= 86400 * 7 * nunits;
	} else {
	    int ym;

	    tm = *localtime(&t);
	    ym = tm.tm_year * 12 + tm.tm_mon;

	    if (unit[0] == 'm')
		ym -= nunits;
	    else
		ym -= 12 * nunits;

	    tm.tm_year = ym / 12;
	    tm.tm_mon = ym % 12;

	    t = mktime(&tm);
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
	    fprintf(stderr, "%s: %s: open: %s\n", PNAME, filename,
		    strerror(errno));
	    return 1;
	}
	if (fstat(fd, &st) < 0) {
	    perror("agedu: fstat");
	    return 1;
	}
	totalsize = st.st_size;
	mappedfile = mmap(NULL, totalsize, PROT_READ, MAP_SHARED, fd, 0);
	if (!mappedfile) {
	    perror("agedu: mmap");
	    return 1;
	}

	/*
	 * Trim trailing slash, just in case.
	 */
	pathlen = strlen(rootdir);
	if (pathlen > 0 && rootdir[pathlen-1] == '/')
	    rootdir[--pathlen] = '\0';

	text_query(mappedfile, rootdir, t, 1);
    } else if (mode == HTML) {
	size_t pathlen;
	unsigned long xi;
	char *html;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
	    fprintf(stderr, "%s: %s: open: %s\n", PNAME, filename,
		    strerror(errno));
	    return 1;
	}
	if (fstat(fd, &st) < 0) {
	    perror("agedu: fstat");
	    return 1;
	}
	totalsize = st.st_size;
	mappedfile = mmap(NULL, totalsize, PROT_READ, MAP_SHARED, fd, 0);
	if (!mappedfile) {
	    perror("agedu: mmap");
	    return 1;
	}

	/*
	 * Trim trailing slash, just in case.
	 */
	pathlen = strlen(rootdir);
	if (pathlen > 0 && rootdir[pathlen-1] == '/')
	    rootdir[--pathlen] = '\0';

	xi = trie_before(mappedfile, rootdir);
	html = html_query(mappedfile, xi, NULL);
	fputs(html, stdout);
    } else if (mode == DUMP) {
	size_t maxpathlen;
	char *buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
	    fprintf(stderr, "%s: %s: open: %s\n", PNAME, filename,
		    strerror(errno));
	    return 1;
	}
	if (fstat(fd, &st) < 0) {
	    perror("agedu: fstat");
	    return 1;
	}
	totalsize = st.st_size;
	mappedfile = mmap(NULL, totalsize, PROT_READ, MAP_SHARED, fd, 0);
	if (!mappedfile) {
	    perror("agedu: mmap");
	    return 1;
	}

	maxpathlen = trie_maxpathlen(mappedfile);
	buf = snewn(maxpathlen, char);

	tw = triewalk_new(mappedfile);
	while ((tf = triewalk_next(tw, buf)) != NULL) {
	    printf("%s: %llu %llu\n", buf, tf->blocks, tf->atime);
	}
	triewalk_free(tw);
    } else if (mode == HTTPD) {
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
	    fprintf(stderr, "%s: %s: open: %s\n", PNAME, filename,
		    strerror(errno));
	    return 1;
	}
	if (fstat(fd, &st) < 0) {
	    perror("agedu: fstat");
	    return 1;
	}
	totalsize = st.st_size;
	mappedfile = mmap(NULL, totalsize, PROT_READ, MAP_SHARED, fd, 0);
	if (!mappedfile) {
	    perror("agedu: mmap");
	    return 1;
	}

	run_httpd(mappedfile, auth);
    }

    return 0;
}

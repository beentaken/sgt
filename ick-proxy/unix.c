/*
 * Main program for pure Unix command-line ick-proxy.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <syslog.h>

#ifndef NO_X11
#include <X11/X.h>
#include <X11/Xlib.h>
#endif

#include "misc.h"
#include "proxy.h"
#include "tree234.h"
#include "unix.h"

const char usagemsg[] =
    "  usage: ick-proxy [options]\n"
    "     or: ick-proxy [options] <subcommand>\n"
    "     or: ick-proxy [options] --multiuser\n"
    "     or: ick-proxy [options] -t <test-url>\n"
    "  where: <subcommand>   write a single PAC and run with lifetime of subcommand\n"
    "         --multiuser    run as a daemon serving PACs over HTTP\n"
    "         -t             test-rewrite a single URL and terminate\n"
    "              otherwise write a single PAC and run with lifetime of X session\n"
    "options: -p <port>      specify master port for in multi-user mode\n"
    "         -u <username>  run as unprivileged user in multi-user mode\n"
    "         -s <script>    override default location for rewrite script\n"
    "         -i <inpac>     override default location for input .PAC file\n"
    "         -o <outpac>    override default location for output .PAC file\n"
    "         -display <display>  specify X display to attach to in default mode\n"
    " also:   ick-proxy --version  report version number\n"
    "         ick-proxy --help     display this help text\n"
    "         ick-proxy --licence  display the (MIT) licence text\n"
    ;

void usage(void) {
    fputs(usagemsg, stdout);
}

const char licencemsg[] =
    "ick-proxy is copyright 2004-8 Simon Tatham.\n"
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
    /*
     * This string is edited by the bob build script to show the
     * Subversion revision number used in the checkout.
     * 
     * At least, I hope so.
     */
    char *revision = "~SVNREVISION~";
    if (revision[0] == '~') {
	/* If not, we fall back to this. */
	printf("ick-proxy, unspecified revision\n");
    } else {
	printf("ick-proxy revision %s\n", revision);
    }
}

#ifndef NO_X11
Display *disp;
int xreadfd(int fd)
{
    XEvent ev;
    XNextEvent(disp, &ev);
    return 0;			       /* FIXME? */
}
#endif

int main(int argc, char **argv)
{
    char **singleusercmd = NULL;
    char *testurl = NULL;
    char *dropprivuser = NULL;
#ifndef NO_X11
    char *display = NULL; 
#endif
    int doing_opts = 1;
    int port = 880;
    int multiuser = 0;

    /*
     * Parse the arguments.
     */
    while (--argc > 0) {
        char *p = *++argv;
        if (*p == '-' && doing_opts) {
            if (!strcmp(p, "--multiuser")) {
		multiuser = 1;
#ifndef NO_X11
	    } else if (!strcmp(p, "--display") || !strcmp(p, "-display")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: %s expected a parameter\n", p);
                    return 1;
                }
                display = *++argv;
#endif
            } else if (!strcmp(p, "-p")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: -p expected a parameter\n");
                    return 1;
                }
                port = atoi(*++argv);
            } else if (!strcmp(p, "-s")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: -s expected a parameter\n");
                    return 1;
                }
		override_script = *++argv;
            } else if (!strcmp(p, "-i")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: -i expected a parameter\n");
                    return 1;
                }
		override_inpac = *++argv;
            } else if (!strcmp(p, "-o")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: -o expected a parameter\n");
                    return 1;
                }
		override_outpac = *++argv;
            } else if (!strcmp(p, "-t")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: -t expected a parameter\n");
                    return 1;
                }
		testurl = *++argv;
            } else if (!strcmp(p, "-u")) {
                if (--argc <= 0) {
                    fprintf(stderr, "ick-proxy: -u expected a parameter\n");
                    return 1;
                }
		dropprivuser = *++argv;
            } else if (!strcmp(p, "--help") || !strcmp(p, "-help") ||
                       !strcmp(p, "-?") || !strcmp(p, "-h")) {
                /* Valid help request. */
                usage();
                return 0;
            } else if (!strcmp(p, "--licence") || !strcmp(p, "--license")) {
                licence();
                return 0;
            } else if (!strcmp(p, "--version")) {
                version();
                return 0;
            } else if (!strcmp(p, "--")) {
                doing_opts = 0;
            } else {
                /* Invalid option; give help as well as whining. */
                fprintf(stderr, "ick-proxy: unrecognised option '%s'\n", p);
                usage();
                return 1;
            }
        } else {
            singleusercmd = argv;
            break;
        }
    }

    /*
     * Handle command-line URL-test mode.
     */
    if (testurl) {
	char *err, *ret;

	ick_proxy_setup();

	ret = rewrite_url(&err, "", testurl);
	if (!ret) {
	    fprintf(stderr, "ick-proxy: rewrite error: %s\n", err);
	    sfree(err);
	    return 1;
	} else {
	    puts(ret);
	    sfree(ret);
	    return 0;
	}
    }

    /*
     * Otherwise, hand off to uxmain.
     */
    if (!multiuser && !singleusercmd) {
#ifdef NO_X11
	fprintf(stderr, "ick-proxy: expected a command in single-user"
		" mode\n");
	return 1;
#else
	/*
	 * Open the X display.
	 */
	disp = XOpenDisplay(display);
	if (!disp) {
	    fprintf(stderr, "ick-proxy: unable to open X display\n");
	    return 1;
	}

	/*
	 * When working with an X display, we daemonise, if only
	 * to avoid zombie processes if we're started from a
	 * .xsession that isn't wait()ing for us.
	 */
	return uxmain(0, -1, NULL, NULL, ConnectionNumber(disp), xreadfd, 1);
#endif
    }

    /*
     * The `daemon' flag is set iff we are multi-user.
     */
    return uxmain(multiuser, port, dropprivuser, singleusercmd,
		  -1, NULL, multiuser);
}

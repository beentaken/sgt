/*
 * PLink - a command-line (stdin/stdout) variant of PuTTY.
 */

#ifndef AUTO_WINSOCK
#include <winsock2.h>
#endif
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define PUTTY_DO_GLOBALS		       /* actually _define_ globals */
#include "putty.h"

void fatalbox (char *p, ...) {
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ", p);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    WSACleanup();
    exit(1);
}
void connection_fatal (char *p, ...) {
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ", p);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    WSACleanup();
    exit(1);
}

static char *password = NULL;

/*
 * Stubs for linking with other modules.
 */
void write_clip (void *data, int len) { }
void term_deselect(void) { }

HANDLE outhandle;
DWORD orig_console_mode;

void begin_session(void) {
    if (!cfg.ldisc_term)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
    else
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), orig_console_mode);
}

void term_out(void)
{
    int reap;
    DWORD ret;
    reap = 0;
    while (reap < inbuf_head) {
        if (!WriteFile(outhandle, inbuf+reap, inbuf_head-reap, &ret, NULL))
            return;                    /* give up in panic */
        reap += ret;
    }
    inbuf_head = 0;
}

struct input_data {
    DWORD len;
    char buffer[4096];
    HANDLE event;
};

static int get_password(const char *prompt, char *str, int maxlen)
{
    HANDLE hin, hout;
    DWORD savemode, i;

    if (password) {
        static int tried_once = 0;

        if (tried_once) {
            return 0;
        } else {
            strncpy(str, password, maxlen);
            str[maxlen-1] = '\0';
            tried_once = 1;
            return 1;
        }
    }

    hin = GetStdHandle(STD_INPUT_HANDLE);
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot get standard input/output handles");
        return 0;
    }

    GetConsoleMode(hin, &savemode);
    SetConsoleMode(hin, (savemode & (~ENABLE_ECHO_INPUT)) |
                   ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT);

    WriteFile(hout, prompt, strlen(prompt), &i, NULL);
    ReadFile(hin, str, maxlen-1, &i, NULL);

    SetConsoleMode(hin, savemode);

    if ((int)i > maxlen) i = maxlen-1; else i = i - 2;
    str[i] = '\0';

    WriteFile(hout, "\r\n", 2, &i, NULL);

    return 1;
}

int WINAPI stdin_read_thread(void *param) {
    struct input_data *idata = (struct input_data *)param;
    HANDLE inhandle;

    inhandle = GetStdHandle(STD_INPUT_HANDLE);

    while (ReadFile(inhandle, idata->buffer, sizeof(idata->buffer),
                    &idata->len, NULL)) {
        SetEvent(idata->event);
    }

    idata->len = 0;
    SetEvent(idata->event);

    return 0;
}

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("PuTTY Link: command-line connection utility\n");
    printf("%s\n", ver);
    printf("Usage: plink [options] [user@]host [command]\n");
    printf("Options:\n");
    printf("  -v        show verbose messages\n");
    printf("  -ssh      force use of ssh protocol\n");
    printf("  -P port   connect to specified port\n");
    printf("  -pw passw login with specified password\n");
    exit(1);
}

int main(int argc, char **argv) {
    WSADATA wsadata;
    WORD winsock_ver;
    WSAEVENT netevent, stdinevent;
    HANDLE handles[2];
    SOCKET socket;
    DWORD threadid;
    struct input_data idata;
    int sending;
    int portnumber = -1;

    ssh_get_password = get_password;

    flags = FLAG_STDERR;
    /*
     * Process the command line.
     */
    do_defaults(NULL);
    default_protocol = cfg.protocol;
    default_port = cfg.port;
    while (--argc) {
        char *p = *++argv;
        if (*p == '-') {
            if (!strcmp(p, "-ssh")) {
		default_protocol = cfg.protocol = PROT_SSH;
		default_port = cfg.port = 22;
	    } else if (!strcmp(p, "-v")) {
                flags |= FLAG_VERBOSE;
	    } else if (!strcmp(p, "-log")) {
                logfile = "putty.log";
            } else if (!strcmp(p, "-pw") && argc > 1) {
                --argc, password = *++argv;
            } else if (!strcmp(p, "-l") && argc > 1) {
                char *username;
                --argc, username = *++argv;
                strncpy(cfg.username, username, sizeof(cfg.username));
                cfg.username[sizeof(cfg.username)-1] = '\0';
            } else if (!strcmp(p, "-P") && argc > 1) {
                --argc, portnumber = atoi(*++argv);
            }
	} else if (*p) {
            if (!*cfg.host) {
                char *q = p;
                /*
                 * If the hostname starts with "telnet:", set the
                 * protocol to Telnet and process the string as a
                 * Telnet URL.
                 */
                if (!strncmp(q, "telnet:", 7)) {
                    char c;

                    q += 7;
                    if (q[0] == '/' && q[1] == '/')
                        q += 2;
                    cfg.protocol = PROT_TELNET;
                    p = q;
                    while (*p && *p != ':' && *p != '/') p++;
                    c = *p;
                    if (*p)
                        *p++ = '\0';
                    if (c == ':')
                        cfg.port = atoi(p);
                    else
                        cfg.port = -1;
                    strncpy (cfg.host, q, sizeof(cfg.host)-1);
                    cfg.host[sizeof(cfg.host)-1] = '\0';
                } else {
                    /*
                     * Three cases. Either (a) there's a nonzero
                     * length string followed by an @, in which
                     * case that's user and the remainder is host.
                     * Or (b) there's only one string, not counting
                     * a potential initial @, and it exists in the
                     * saved-sessions database. Or (c) only one
                     * string and it _doesn't_ exist in the
                     * database.
                     */
                    char *r = strrchr(p, '@');
                    if (r == p) p++, r = NULL;   /* discount initial @ */
                    if (r == NULL) {
                        /*
                         * One string.
                         */
                        do_defaults (p);
                        if (cfg.host[0] == '\0') {
                            /* No settings for this host; use defaults */
                            strncpy(cfg.host, p, sizeof(cfg.host)-1);
                            cfg.host[sizeof(cfg.host)-1] = '\0';
                            cfg.port = 22;
                        }
                    } else {
                        *r++ = '\0';
                        strncpy(cfg.username, p, sizeof(cfg.username)-1);
                        cfg.username[sizeof(cfg.username)-1] = '\0';
                        strncpy(cfg.host, r, sizeof(cfg.host)-1);
                        cfg.host[sizeof(cfg.host)-1] = '\0';
                        cfg.port = 22;
                    }
                }
            } else {
                int len = sizeof(cfg.remote_cmd) - 1;
                char *cp = cfg.remote_cmd;
                int len2;

                strncpy(cp, p, len); cp[len] = '\0';
                len2 = strlen(cp); len -= len2; cp += len2;
                while (--argc) {
                    if (len > 0)
                        len--, *cp++ = ' ';
                    strncpy(cp, *++argv, len); cp[len] = '\0';
                    len2 = strlen(cp); len -= len2; cp += len2;
                }
                cfg.nopty = TRUE;      /* command => no terminal */
                cfg.ldisc_term = TRUE; /* use stdin like a line buffer */
                break;                 /* done with cmdline */
            }
	}
    }

    if (!*cfg.host) {
        usage();
    }
    if (portnumber != -1)
        cfg.port = portnumber;

    if (!*cfg.remote_cmd)
        flags |= FLAG_INTERACTIVE;

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    {
        int i;
        back = NULL;
        for (i = 0; backends[i].backend != NULL; i++)
            if (backends[i].protocol == cfg.protocol) {
                back = backends[i].backend;
                break;
            }
        if (back == NULL) {
            fprintf(stderr, "Internal fault: Unsupported protocol found\n");
            return 1;
        }
    }

    /*
     * Initialise WinSock.
     */
    winsock_ver = MAKEWORD(2, 0);
    if (WSAStartup(winsock_ver, &wsadata)) {
	MessageBox(NULL, "Unable to initialise WinSock", "WinSock Error",
		   MB_OK | MB_ICONEXCLAMATION);
	return 1;
    }
    if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 0) {
	MessageBox(NULL, "WinSock version is incompatible with 2.0",
		   "WinSock Error", MB_OK | MB_ICONEXCLAMATION);
	WSACleanup();
	return 1;
    }

    /*
     * Start up the connection.
     */
    {
	char *error;
	char *realhost;

	error = back->init (NULL, cfg.host, cfg.port, &realhost);
	if (error) {
	    fprintf(stderr, "Unable to open connection:\n%s", error);
	    return 1;
	}
    }

    netevent = CreateEvent(NULL, FALSE, FALSE, NULL);
    stdinevent = CreateEvent(NULL, FALSE, FALSE, NULL);

    GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &orig_console_mode);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
    outhandle = GetStdHandle(STD_OUTPUT_HANDLE);

    /*
     * Now we must send the back end oodles of stuff.
     */
    socket = back->socket();
    /*
     * Turn off ECHO and LINE input modes. We don't care if this
     * call fails, because we know we aren't necessarily running in
     * a console.
     */
    WSAEventSelect(socket, netevent, FD_READ | FD_CLOSE);
    handles[0] = netevent;
    handles[1] = stdinevent;
    sending = FALSE;
    while (1) {
        int n;
        n = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (n == 0) {
            WSANETWORKEVENTS things;
            if (!WSAEnumNetworkEvents(socket, netevent, &things)) {
                if (things.lNetworkEvents & FD_READ)
                    back->msg(0, FD_READ);
                if (things.lNetworkEvents & FD_CLOSE) {
                    back->msg(0, FD_CLOSE);
                    break;
                }
            }
            term_out();
            if (!sending && back->sendok()) {
                /*
                 * Create a separate thread to read from stdin.
                 * This is a total pain, but I can't find another
                 * way to do it:
                 *
                 *  - an overlapped ReadFile or ReadFileEx just
                 *    doesn't happen; we get failure from
                 *    ReadFileEx, and ReadFile blocks despite being
                 *    given an OVERLAPPED structure. Perhaps we
                 *    can't do overlapped reads on consoles. WHY
                 *    THE HELL NOT?
                 * 
                 *  - WaitForMultipleObjects(netevent, console)
                 *    doesn't work, because it signals the console
                 *    when _anything_ happens, including mouse
                 *    motions and other things that don't cause
                 *    data to be readable - so we're back to
                 *    ReadFile blocking.
                 */
                idata.event = stdinevent;
                if (!CreateThread(NULL, 0, stdin_read_thread,
                                  &idata, 0, &threadid)) {
                    fprintf(stderr, "Unable to create second thread\n");
                    exit(1);
                }
                sending = TRUE;
            }
        } else if (n == 1) {
            if (idata.len > 0) {
                back->send(idata.buffer, idata.len);
            } else {
                back->special(TS_EOF);
            }
        }
    }
    WSACleanup();
    return 0;
}

/*
 * mboxread.c: read an external mbox folder and import its
 * contents.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "timber.h"

static int is_mbox_message_separator(char *p)
{
    /*
     * In Timber v0, the Perl regexp which recognises a separator
     * line was:
     *
     * /^From [^ ]+ *( [A-Z][a-z]{2}){2} [\d ]\d [\d ]\d:\d\d:\d\d \d\d\d\d$/
     */
    if (*p != 'F') return FALSE; p++;
    if (*p != 'r') return FALSE; p++;
    if (*p != 'o') return FALSE; p++;
    if (*p != 'm') return FALSE; p++;
    if (*p != ' ') return FALSE; p++;
    if (*p == '\n' || *p == ' ' || !*p) return FALSE;
    while (*p && *p != '\n' && *p != ' ') p++;
    if (*p != ' ') return FALSE; p++;
    while (*p == ' ') p++;
    if (*p < 'A' || *p > 'Z') return FALSE; p++;
    if (*p < 'a' || *p > 'z') return FALSE; p++;
    if (*p < 'a' || *p > 'z') return FALSE; p++;
    if (*p != ' ') return FALSE; p++;
    if (*p < 'A' || *p > 'Z') return FALSE; p++;
    if (*p < 'a' || *p > 'z') return FALSE; p++;
    if (*p < 'a' || *p > 'z') return FALSE; p++;
    if (*p != ' ') return FALSE; p++;
    if (*p != ' ' && (*p < '0' || *p > '9')) return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p != ' ') return FALSE; p++;
    if (*p != ' ' && (*p < '0' || *p > '9')) return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p != ':') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p != ':') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p != ' ') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p < '0' || *p > '9') return FALSE; p++;
    if (*p != '\n') return FALSE; p++;

    return TRUE;
}

int import_message(char *message, int msglen, char *separator)
{
    char *location;
    assert(msglen > 0 && message[msglen-1] == '\n');
    location = store_literal(message, msglen, separator);
    if (!location)
	return FALSE;
    parse_for_db(NULL, location, message, msglen);
    sfree(location);
    return TRUE;
}

int import_mbox_message(char *message, int msglen)
{
    char *separator = NULL;

    if (msglen >= 5 && !memcmp(message, "From ", 5)) {
	/*
	 * Split off the first line to be used as the separator.
	 */
	char *p = message;
	while (p < message+msglen && *p != '\n')
	    p++;
	if (p < message+msglen) {
	    *p++ = '\0';
	    separator = message;
	    msglen -= (p - message);
	    message = p;
	}
    }

    return import_message(message, msglen, separator);
}

void import_mbox_folder(char *folder)
{
    FILE *fp;
    char *message;
    int msglen, msgsize;

    fp = fopen(folder, "r");
    if (!fp)
	fatal(err_perror, folder, "fopen");

    msgsize = 1024;
    msglen = 0;
    message = snewn(msgsize, char);

    while (fgets(message + msglen, msgsize - msglen, fp)) {
	/*
	 * Strip out CRs.
	 */
	{
	    int i, j;
	    for (i = j = 0; message[msglen + i]; i++) {
		if (message[msglen + i] == '\r' &&
		    message[msglen + i + 1] == '\n')
		    i++;
		message[msglen + j++] = message[msglen + i];
	    }
	    message[msglen + j] = '\0';
	}
	if (msglen == 0 || message[msglen-1] == '\n') {

	    /*
	     * We're at the start of a line, so check for message
	     * separators and suchlike.
	     */
	    if (is_mbox_message_separator(message+msglen) && msglen > 0) {
		/*
		 * File this message.
		 */
		if (!import_mbox_message(message, msglen)) {
		    fclose(fp);
		    return;
		}
		/*
		 * Now return to the beginning of the buffer.
		 */
		memmove(message, message + msglen,
			1 + strlen(message + msglen));
		msglen = 0;
	    }

	    /*
	     * See if this line starts with an escaped `From '.
	     */
	    if (!strncmp(message + msglen, ">From ", 6)) {
		/* Unescape it. */
		memmove(message + msglen, message + msglen + 1,
			1 + strlen(message + msglen + 1));
	    }
	}
	msglen += strlen(message + msglen);

	if (msgsize - msglen < 512 || msgsize - msglen < msgsize/4) {
	    msgsize = msgsize * 3 / 2;
	    message = sresize(message, msgsize, char);
	}
    }

    /*
     * File the last message, after first removing a trailing
     * newline if present.
     */
    if (msglen > 0) {
	if (message[msglen-1] == '\n')
	    msglen--;
	import_mbox_message(message, msglen);
    }

    fclose(fp);
}

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <time.h>
#include <assert.h>
#include "putty.h"
#include "terminal.h"

#define poslt(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x < (p2).x ) )
#define posle(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x <= (p2).x ) )
#define poseq(p1,p2) ( (p1).y == (p2).y && (p1).x == (p2).x )
#define posdiff(p1,p2) ( ((p1).y - (p2).y) * (term->cols+1) + (p1).x - (p2).x )

/* Product-order comparisons for rectangular block selection. */
#define posPlt(p1,p2) ( (p1).y <= (p2).y && (p1).x < (p2).x )
#define posPle(p1,p2) ( (p1).y <= (p2).y && (p1).x <= (p2).x )

#define incpos(p) ( (p).x == term->cols ? ((p).x = 0, (p).y++, 1) : ((p).x++, 0) )
#define decpos(p) ( (p).x == 0 ? ((p).x = term->cols, (p).y--, 1) : ((p).x--, 0) )

#define VT52_PLUS

#define CL_ANSIMIN	0x0001	       /* Codes in all ANSI like terminals. */
#define CL_VT100	0x0002	       /* VT100 */
#define CL_VT100AVO	0x0004	       /* VT100 +AVO; 132x24 (not 132x14) & attrs */
#define CL_VT102	0x0008	       /* VT102 */
#define CL_VT220	0x0010	       /* VT220 */
#define CL_VT320	0x0020	       /* VT320 */
#define CL_VT420	0x0040	       /* VT420 */
#define CL_VT510	0x0080	       /* VT510, NB VT510 includes ANSI */
#define CL_VT340TEXT	0x0100	       /* VT340 extensions that appear in the VT420 */
#define CL_SCOANSI	0x1000	       /* SCOANSI not in ANSIMIN. */
#define CL_ANSI		0x2000	       /* ANSI ECMA-48 not in the VT100..VT420 */
#define CL_OTHER	0x4000	       /* Others, Xterm, linux, putty, dunno, etc */

#define TM_VT100	(CL_ANSIMIN|CL_VT100)
#define TM_VT100AVO	(TM_VT100|CL_VT100AVO)
#define TM_VT102	(TM_VT100AVO|CL_VT102)
#define TM_VT220	(TM_VT102|CL_VT220)
#define TM_VTXXX	(TM_VT220|CL_VT340TEXT|CL_VT510|CL_VT420|CL_VT320)
#define TM_SCOANSI	(CL_ANSIMIN|CL_SCOANSI)

#define TM_PUTTY	(0xFFFF)

#define compatibility(x) \
    if ( ((CL_##x)&term->compatibility_level) == 0 ) { 	\
       term->termstate=TOPLEVEL;			\
       break;						\
    }
#define compatibility2(x,y) \
    if ( ((CL_##x|CL_##y)&term->compatibility_level) == 0 ) { \
       term->termstate=TOPLEVEL;			\
       break;						\
    }

#define has_compat(x) ( ((CL_##x)&term->compatibility_level) != 0 )

const char sco2ansicolour[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

#define sel_nl_sz  (sizeof(sel_nl)/sizeof(wchar_t))
const wchar_t sel_nl[] = SEL_NL;

/*
 * Fetch the character at a particular position in a line array,
 * for purposes of `wordtype'. The reason this isn't just a simple
 * array reference is that if the character we find is UCSWIDE,
 * then we must look one space further to the left.
 */
#define UCSGET(a, x) \
    ( (x)>0 && (a)[(x)].chr == UCSWIDE ? (a)[(x)-1].chr : (a)[(x)].chr )

/*
 * Detect the various aliases of U+0020 SPACE.
 */
#define IS_SPACE_CHR(chr) \
	((chr) == 0x20 || (DIRECT_CHAR(chr) && ((chr) & 0xFF) == 0x20))

/*
 * Spot magic CSETs.
 */
#define CSET_OF(chr) (DIRECT_CHAR(chr)||DIRECT_FONT(chr) ? (chr)&CSET_MASK : 0)

/*
 * Internal prototypes.
 */
static void resizeline(Terminal *, termline *, int);
static termline *lineptr(Terminal *, int, int, int);
static void unlineptr(termline *);
static void do_paint(Terminal *, Context, int);
static void erase_lots(Terminal *, int, int, int);
static void swap_screen(Terminal *, int, int, int);
static void update_sbar(Terminal *);
static void deselect(Terminal *);
static void term_print_finish(Terminal *);
#ifdef OPTIMISE_SCROLL
static void scroll_display(Terminal *, int, int, int);
#endif /* OPTIMISE_SCROLL */

static termline *newline(Terminal *term, int cols, int bce)
{
    termline *line;
    int j;

    line = snew(termline);
    line->chars = snewn(cols, termchar);
    for (j = 0; j < cols; j++)
	line->chars[j] = (bce ? term->erase_char : term->basic_erase_char);
    line->cols = line->size = cols;
    line->lattr = LATTR_NORM;
    line->temporary = FALSE;
    line->cc_free = 0;

    return line;
}

static void freeline(termline *line)
{
    if (line) {
	sfree(line->chars);
	sfree(line);
    }
}

static void unlineptr(termline *line)
{
    if (line->temporary)
	freeline(line);
}

/*
 * Diagnostic function: verify that a termline has a correct
 * combining character structure.
 * 
 * XXX-REMOVE-BEFORE-RELEASE: This is a performance-intensive
 * check. Although it's currently really useful for getting all the
 * bugs out of the new cc stuff, it will want to be absent when we
 * make a proper release.
 */
static void cc_check(termline *line)
{
    unsigned char *flags;
    int i, j;

    assert(line->size >= line->cols);

    flags = snewn(line->size, unsigned char);

    for (i = 0; i < line->size; i++)
	flags[i] = (i < line->cols);

    for (i = 0; i < line->cols; i++) {
	j = i;
	while (line->chars[j].cc_next) {
	    j += line->chars[j].cc_next;
	    assert(j >= line->cols && j < line->size);
	    assert(!flags[j]);
	    flags[j] = TRUE;
	}
    }

    j = line->cc_free;
    if (j) {
	while (1) {
	    assert(j >= line->cols && j < line->size);
	    assert(!flags[j]);
	    flags[j] = TRUE;
	    if (line->chars[j].cc_next)
		j += line->chars[j].cc_next;
	    else
		break;
	}
    }

    j = 0;
    for (i = 0; i < line->size; i++)
	j += (flags[i] != 0);

    assert(j == line->size);

    sfree(flags);
}

/*
 * Add a combining character to a character cell.
 */
static void add_cc(termline *line, int col, unsigned long chr)
{
    int newcc;

    assert(col >= 0 && col < line->cols);

    /*
     * Start by extending the cols array if the free list is empty.
     */
    if (!line->cc_free) {
	int n = line->size;
	line->size += 16 + (line->size - line->cols) / 2;
	line->chars = sresize(line->chars, line->size, termchar);
	line->cc_free = n;
	while (n < line->size) {
	    if (n+1 < line->size)
		line->chars[n].cc_next = 1;
	    else
		line->chars[n].cc_next = 0;
	    n++;
	}
    }

    /*
     * Now walk the cc list of the cell in question.
     */
    while (line->chars[col].cc_next)
	col += line->chars[col].cc_next;

    /*
     * `col' now points at the last cc currently in this cell; so
     * we simply add another one.
     */
    newcc = line->cc_free;
    if (line->chars[newcc].cc_next)
	line->cc_free = newcc + line->chars[newcc].cc_next;
    else
	line->cc_free = 0;
    line->chars[newcc].cc_next = 0;
    line->chars[newcc].chr = chr;
    line->chars[col].cc_next = newcc - col;

    cc_check(line);		       /* XXX-REMOVE-BEFORE-RELEASE */
}

/*
 * Clear the combining character list in a character cell.
 */
static void clear_cc(termline *line, int col)
{
    int oldfree, origcol = col;

    assert(col >= 0 && col < line->cols);

    if (!line->chars[col].cc_next)
	return;			       /* nothing needs doing */

    oldfree = line->cc_free;
    line->cc_free = col + line->chars[col].cc_next;
    while (line->chars[col].cc_next)
	col += line->chars[col].cc_next;
    if (oldfree)
	line->chars[col].cc_next = oldfree - col;
    else
	line->chars[col].cc_next = 0;

    line->chars[origcol].cc_next = 0;

    cc_check(line);		       /* XXX-REMOVE-BEFORE-RELEASE */
}

/*
 * Compare two character cells for equality. Special case required
 * in do_paint() where we override what we expect the chr and attr
 * fields to be.
 */
static int termchars_equal_override(termchar *a, termchar *b,
				    unsigned long bchr, unsigned long battr)
{
    /* FULL-TERMCHAR */
    if (a->chr != bchr)
	return FALSE;
    if (a->attr != battr)
	return FALSE;
    while (a->cc_next || b->cc_next) {
	if (!a->cc_next || !b->cc_next)
	    return FALSE;	       /* one cc-list ends, other does not */
	a += a->cc_next;
	b += b->cc_next;
	if (a->chr != b->chr)
	    return FALSE;
    }
    return TRUE;
}

static int termchars_equal(termchar *a, termchar *b)
{
    return termchars_equal_override(a, b, b->chr, b->attr);
}

/*
 * Copy a character cell. (Requires a pointer to the destination
 * termline, so as to access its free list.)
 */
static void copy_termchar(termline *destline, int x, termchar *src)
{
    clear_cc(destline, x);

    destline->chars[x] = *src;	       /* copy everything except cc-list */
    destline->chars[x].cc_next = 0;    /* and make sure this is zero */

    while (src->cc_next) {
	src += src->cc_next;
	add_cc(destline, x, src->chr);
    }

    cc_check(destline);		       /* XXX-REMOVE-BEFORE-RELEASE */
}

/*
 * Move a character cell within its termline.
 */
static void move_termchar(termline *line, termchar *dest, termchar *src)
{
    /* First clear the cc list from the original char, just in case. */
    clear_cc(line, dest - line->chars);

    /* Move the character cell and adjust its cc_next. */
    *dest = *src;		       /* copy everything except cc-list */
    if (src->cc_next)
	dest->cc_next = src->cc_next - (dest-src);

    /* Ensure the original cell doesn't have a cc list. */
    src->cc_next = 0;

    cc_check(line);		       /* XXX-REMOVE-BEFORE-RELEASE */
}

/*
 * Compress and decompress a termline into an RLE-based format for
 * storing in scrollback. (Since scrollback almost never needs to
 * be modified and exists in huge quantities, this is a sensible
 * tradeoff, particularly since it allows us to continue adding
 * features to the main termchar structure without proportionally
 * bloating the terminal emulator's memory footprint unless those
 * features are in constant use.)
 */
struct buf {
    unsigned char *data;
    int len, size;
};
static void add(struct buf *b, unsigned char c)
{
    if (b->len >= b->size) {
	b->size = (b->len * 3 / 2) + 512;
	b->data = sresize(b->data, b->size, unsigned char);
    }
    b->data[b->len++] = c;
}
static int get(struct buf *b)
{
    return b->data[b->len++];
}
static void makerle(struct buf *b, termline *ldata,
		    void (*makeliteral)(struct buf *b, termchar *c,
					unsigned long *state))
{
    int hdrpos, hdrsize, n, prevlen, prevpos, thislen, thispos, prev2;
    termchar *c = ldata->chars;
    unsigned long state = 0, oldstate;

    n = ldata->cols;

    hdrpos = b->len;
    hdrsize = 0;
    add(b, 0);
    prevlen = prevpos = 0;
    prev2 = FALSE;

    while (n-- > 0) {
	thispos = b->len;
	makeliteral(b, c++, &state);
	thislen = b->len - thispos;
	if (thislen == prevlen &&
	    !memcmp(b->data + prevpos, b->data + thispos, thislen)) {
	    /*
	     * This literal precisely matches the previous one.
	     * Turn it into a run if it's worthwhile.
	     * 
	     * With one-byte literals, it costs us two bytes to
	     * encode a run, plus another byte to write the header
	     * to resume normal output; so a three-element run is
	     * neutral, and anything beyond that is unconditionally
	     * worthwhile. With two-byte literals or more, even a
	     * 2-run is a win.
	     */
	    if (thislen > 1 || prev2) {
		int runpos, runlen;

		/*
		 * It's worth encoding a run. Start at prevpos,
		 * unless hdrsize==0 in which case we can back up
		 * another one and start by overwriting hdrpos.
		 */

		hdrsize--;	       /* remove the literal at prevpos */
		if (prev2) {
		    assert(hdrsize > 0);
		    hdrsize--;
		    prevpos -= prevlen;/* and possibly another one */
		}

		if (hdrsize == 0) {
		    assert(prevpos == hdrpos + 1);
		    runpos = hdrpos;
		    b->len = prevpos+prevlen;
		} else {
		    memmove(b->data + prevpos+1, b->data + prevpos, prevlen);
		    runpos = prevpos;
		    b->len = prevpos+prevlen+1;
		    /*
		     * Terminate the previous run of ordinary
		     * literals.
		     */
		    assert(hdrsize >= 1 && hdrsize <= 128);
		    b->data[hdrpos] = hdrsize - 1;
		}

		runlen = prev2 ? 3 : 2;

		while (n > 0 && runlen < 129) {
		    int tmppos, tmplen;
		    tmppos = b->len;
		    oldstate = state;
		    makeliteral(b, c, &state);
		    tmplen = b->len - tmppos;
		    b->len = tmppos;
		    if (tmplen != thislen ||
			memcmp(b->data + runpos+1, b->data + tmppos, tmplen)) {
			state = oldstate;
			break;	       /* run over */
		    }
		    n--, c++, runlen++;
		}

		assert(runlen >= 2 && runlen <= 129);
		b->data[runpos] = runlen + 0x80 - 2;

		hdrpos = b->len;
		hdrsize = 0;
		add(b, 0);
		/* And ensure this run doesn't interfere with the next. */
		prevlen = prevpos = 0;
		prev2 = FALSE;

		continue;
	    } else {
		/*
		 * Just flag that the previous two literals were
		 * identical, in case we find a third identical one
		 * we want to turn into a run.
		 */
		prev2 = TRUE;
		prevlen = thislen;
		prevpos = thispos;
	    }
	} else {
	    prev2 = FALSE;
	    prevlen = thislen;
	    prevpos = thispos;
	}

	/*
	 * This character isn't (yet) part of a run. Add it to
	 * hdrsize.
	 */
	hdrsize++;
	if (hdrsize == 128) {
	    b->data[hdrpos] = hdrsize - 1;
	    hdrpos = b->len;
	    hdrsize = 0;
	    add(b, 0);
	    prevlen = prevpos = 0;
	    prev2 = FALSE;
	}
    }

    /*
     * Clean up.
     */
    if (hdrsize > 0) {
	assert(hdrsize <= 128);
	b->data[hdrpos] = hdrsize - 1;
    } else {
	b->len = hdrpos;
    }
}
static void makeliteral_chr(struct buf *b, termchar *c, unsigned long *state)
{
    /*
     * My encoding for characters is UTF-8-like, in that it stores
     * 7-bit ASCII in one byte and uses high-bit-set bytes as
     * introducers to indicate a longer sequence. However, it's
     * unlike UTF-8 in that it doesn't need to be able to
     * resynchronise, and therefore I don't want to waste two bits
     * per byte on having recognisable continuation characters.
     * Also I don't want to rule out the possibility that I may one
     * day use values 0x80000000-0xFFFFFFFF for interesting
     * purposes, so unlike UTF-8 I need a full 32-bit range.
     * Accordingly, here is my encoding:
     * 
     * 00000000-0000007F: 0xxxxxxx (but see below)
     * 00000080-00003FFF: 10xxxxxx xxxxxxxx
     * 00004000-001FFFFF: 110xxxxx xxxxxxxx xxxxxxxx
     * 00200000-0FFFFFFF: 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
     * 10000000-FFFFFFFF: 11110ZZZ xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
     * 
     * (`Z' is like `x' but is always going to be zero since the
     * values I'm encoding don't go above 2^32. In principle the
     * five-byte form of the encoding could extend to 2^35, and
     * there could be six-, seven-, eight- and nine-byte forms as
     * well to allow up to 64-bit values to be encoded. But that's
     * completely unnecessary for these purposes!)
     * 
     * The encoding as written above would be very simple, except
     * that 7-bit ASCII can occur in several different ways in the
     * terminal data; sometimes it crops up in the D800 page
     * (CSET_ASCII) but at other times it's in the 0000 page (real
     * Unicode). Therefore, this encoding is actually _stateful_:
     * the one-byte encoding of 00-7F actually indicates `reuse the
     * upper three bytes of the last character', and to encode an
     * absolute value of 00-7F you need to use the two-byte form
     * instead.
     */
    if ((c->chr & ~0x7F) == *state) {
	add(b, (unsigned char)(c->chr & 0x7F));
    } else if (c->chr < 0x4000) {
	add(b, (unsigned char)(((c->chr >> 8) & 0x3F) | 0x80));
	add(b, (unsigned char)(c->chr & 0xFF));
    } else if (c->chr < 0x200000) {
	add(b, (unsigned char)(((c->chr >> 16) & 0x1F) | 0xC0));
	add(b, (unsigned char)((c->chr >> 8) & 0xFF));
	add(b, (unsigned char)(c->chr & 0xFF));
    } else if (c->chr < 0x10000000) {
	add(b, (unsigned char)(((c->chr >> 24) & 0x0F) | 0xE0));
	add(b, (unsigned char)((c->chr >> 16) & 0xFF));
	add(b, (unsigned char)((c->chr >> 8) & 0xFF));
	add(b, (unsigned char)(c->chr & 0xFF));
    } else {
	add(b, 0xF0);
	add(b, (unsigned char)((c->chr >> 24) & 0xFF));
	add(b, (unsigned char)((c->chr >> 16) & 0xFF));
	add(b, (unsigned char)((c->chr >> 8) & 0xFF));
	add(b, (unsigned char)(c->chr & 0xFF));
    }
    *state = c->chr & ~0xFF;
}
static void makeliteral_attr(struct buf *b, termchar *c, unsigned long *state)
{
    /*
     * My encoding for attributes is 16-bit-granular and assumes
     * that the top bit of the word is never required. I either
     * store a two-byte value with the top bit clear (indicating
     * just that value), or a four-byte value with the top bit set
     * (indicating the same value with its top bit clear).
     */
    if (c->attr < 0x8000) {
	add(b, (unsigned char)((c->attr >> 8) & 0xFF));
	add(b, (unsigned char)(c->attr & 0xFF));
    } else {
	add(b, (unsigned char)(((c->attr >> 24) & 0x7F) | 0x80));
	add(b, (unsigned char)((c->attr >> 16) & 0xFF));
	add(b, (unsigned char)((c->attr >> 8) & 0xFF));
	add(b, (unsigned char)(c->attr & 0xFF));
    }
}
static void makeliteral_cc(struct buf *b, termchar *c, unsigned long *state)
{
    /*
     * For combining characters, I just encode a bunch of ordinary
     * chars using makeliteral_chr, and terminate with a \0
     * character (which I know won't come up as a combining char
     * itself).
     * 
     * I don't use the stateful encoding in makeliteral_chr.
     */
    unsigned long zstate;
    termchar z;

    while (c->cc_next) {
	c += c->cc_next;

	assert(c->chr != 0);

	zstate = 0;
	makeliteral_chr(b, c, &zstate);
    }

    z.chr = 0;
    zstate = 0;
    makeliteral_chr(b, &z, &zstate);
}

static termline *decompressline(unsigned char *data, int *bytes_used);

static unsigned char *compressline(termline *ldata)
{
    struct buf buffer = { NULL, 0, 0 }, *b = &buffer;

    /*
     * First, store the column count, 7 bits at a time, least
     * significant `digit' first, with the high bit set on all but
     * the last.
     */
    {
	int n = ldata->cols;
	while (n >= 128) {
	    add(b, (unsigned char)((n & 0x7F) | 0x80));
	    n >>= 7;
	}
	add(b, (unsigned char)(n));
    }

    /*
     * Next store the lattrs; same principle.
     */
    {
	int n = ldata->lattr;
	while (n >= 128) {
	    add(b, (unsigned char)((n & 0x7F) | 0x80));
	    n >>= 7;
	}
	add(b, (unsigned char)(n));
    }

    /*
     * Now we store a sequence of separate run-length encoded
     * fragments, each containing exactly as many symbols as there
     * are columns in the ldata.
     * 
     * All of these have a common basic format:
     * 
     *  - a byte 00-7F indicates that X+1 literals follow it
     * 	- a byte 80-FF indicates that a single literal follows it
     * 	  and expects to be repeated (X-0x80)+2 times.
     * 
     * The format of the `literals' varies between the fragments.
     */
    makerle(b, ldata, makeliteral_chr);
    makerle(b, ldata, makeliteral_attr);
    makerle(b, ldata, makeliteral_cc);

    /*
     * Diagnostics: ensure that the compressed data really does
     * decompress to the right thing.
     * 
     * XXX-REMOVE-BEFORE-RELEASE: This is a bit performance-heavy
     * to be leaving in production code.
     */
#ifndef CHECK_SB_COMPRESSION
    {
	int dused;
	termline *dcl;
	int i;

#ifdef DIAGNOSTIC_SB_COMPRESSION
	for (i = 0; i < b->len; i++) {
	    printf(" %02x ", b->data[i]);
	}
	printf("\n");
#endif

	dcl = decompressline(b->data, &dused);
	assert(b->len == dused);
	assert(ldata->cols == dcl->cols);
	assert(ldata->lattr == dcl->lattr);
	for (i = 0; i < ldata->cols; i++)
	    assert(termchars_equal(&ldata->chars[i], &dcl->chars[i]));

#ifdef DIAGNOSTIC_SB_COMPRESSION
	printf("%d cols (%d bytes) -> %d bytes (factor of %g)\n",
	       ldata->cols, 4 * ldata->cols, dused,
	       (double)dused / (4 * ldata->cols));
#endif

	freeline(dcl);
    }
#endif

    /*
     * Trim the allocated memory so we don't waste any, and return.
     */
    return sresize(b->data, b->len, unsigned char);
}

static void readrle(struct buf *b, termline *ldata,
		    void (*readliteral)(struct buf *b, termchar *c,
					termline *ldata, unsigned long *state))
{
    int n = 0;
    unsigned long state = 0;

    while (n < ldata->cols) {
	int hdr = get(b);

	if (hdr >= 0x80) {
	    /* A run. */

	    int pos = b->len, count = hdr + 2 - 0x80;
	    while (count--) {
		assert(n < ldata->cols);
		b->len = pos;
		readliteral(b, ldata->chars + n, ldata, &state);
		n++;
	    }
	} else {
	    /* Just a sequence of consecutive literals. */

	    int count = hdr + 1;
	    while (count--) {
		assert(n < ldata->cols);
		readliteral(b, ldata->chars + n, ldata, &state);
		n++;
	    }
	}
    }

    assert(n == ldata->cols);
}
static void readliteral_chr(struct buf *b, termchar *c, termline *ldata,
			    unsigned long *state)
{
    int byte;

    /*
     * 00000000-0000007F: 0xxxxxxx
     * 00000080-00003FFF: 10xxxxxx xxxxxxxx
     * 00004000-001FFFFF: 110xxxxx xxxxxxxx xxxxxxxx
     * 00200000-0FFFFFFF: 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
     * 10000000-FFFFFFFF: 11110ZZZ xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
     */

    byte = get(b);
    if (byte < 0x80) {
	c->chr = byte | *state;
    } else if (byte < 0xC0) {
	c->chr = (byte &~ 0xC0) << 8;
	c->chr |= get(b);
    } else if (byte < 0xE0) {
	c->chr = (byte &~ 0xE0) << 16;
	c->chr |= get(b) << 8;
	c->chr |= get(b);
    } else if (byte < 0xF0) {
	c->chr = (byte &~ 0xF0) << 24;
	c->chr |= get(b) << 16;
	c->chr |= get(b) << 8;
	c->chr |= get(b);
    } else {
	assert(byte == 0xF0);
	c->chr = get(b) << 24;
	c->chr |= get(b) << 16;
	c->chr |= get(b) << 8;
	c->chr |= get(b);
    }
    *state = c->chr & ~0xFF;
}
static void readliteral_attr(struct buf *b, termchar *c, termline *ldata,
			     unsigned long *state)
{
    int val;

    val = get(b) << 8;
    val |= get(b);

    if (val >= 0x8000) {
	val <<= 16;
	val |= get(b) << 8;
	val |= get(b);
    }

    c->attr = val;
}
static void readliteral_cc(struct buf *b, termchar *c, termline *ldata,
			   unsigned long *state)
{
    termchar n;
    unsigned long zstate;
    int x = c - ldata->chars;

    c->cc_next = 0;

    while (1) {
	zstate = 0;
	readliteral_chr(b, &n, ldata, &zstate);
	if (!n.chr)
	    break;
	add_cc(ldata, x, n.chr);
    }
}

static termline *decompressline(unsigned char *data, int *bytes_used)
{
    int ncols, byte, shift;
    struct buf buffer, *b = &buffer;
    termline *ldata;

    b->data = data;
    b->len = 0;

    /*
     * First read in the column count.
     */
    ncols = shift = 0;
    do {
	byte = get(b);
	ncols |= (byte & 0x7F) << shift;
	shift += 7;
    } while (byte & 0x80);

    /*
     * Now create the output termline.
     */
    ldata = snew(termline);
    ldata->chars = snewn(ncols, termchar);
    ldata->cols = ldata->size = ncols;
    ldata->temporary = TRUE;
    ldata->cc_free = 0;

    /*
     * We must set all the cc pointers in ldata->chars to 0 right
     * now, so that cc diagnostics that verify the integrity of the
     * whole line will make sense while we're in the middle of
     * building it up.
     */
    {
	int i;
	for (i = 0; i < ldata->cols; i++)
	    ldata->chars[i].cc_next = 0;
    }

    /*
     * Now read in the lattr.
     */
    ldata->lattr = shift = 0;
    do {
	byte = get(b);
	ldata->lattr |= (byte & 0x7F) << shift;
	shift += 7;
    } while (byte & 0x80);

    /*
     * Now we read in each of the RLE streams in turn.
     */
    readrle(b, ldata, readliteral_chr);
    readrle(b, ldata, readliteral_attr);
    readrle(b, ldata, readliteral_cc);

    /* Return the number of bytes read, for diagnostic purposes. */
    if (bytes_used)
	*bytes_used = b->len;

    return ldata;
}

/*
 * Resize a line to make it `cols' columns wide.
 */
static void resizeline(Terminal *term, termline *line, int cols)
{
    int i, oldcols;

    if (line->cols != cols) {

	oldcols = line->cols;

	/*
	 * This line is the wrong length, which probably means it
	 * hasn't been accessed since a resize. Resize it now.
	 * 
	 * First, go through all the characters that will be thrown
	 * out in the resize (if we're shrinking the line) and
	 * return their cc lists to the cc free list.
	 */
	for (i = cols; i < oldcols; i++)
	    clear_cc(line, i);

	/*
	 * If we're shrinking the line, we now bodily move the
	 * entire cc section from where it started to where it now
	 * needs to be. (We have to do this before the resize, so
	 * that the data we're copying is still there. However, if
	 * we're expanding, we have to wait until _after_ the
	 * resize so that the space we're copying into is there.)
	 */
	if (cols < oldcols)
	    memmove(line->chars + cols, line->chars + oldcols,
		    (line->size - line->cols) * TSIZE);

	/*
	 * Now do the actual resize, leaving the _same_ amount of
	 * cc space as there was to begin with.
	 */
	line->size += cols - oldcols;
	line->chars = sresize(line->chars, line->size, TTYPE);
	line->cols = cols;

	/*
	 * If we're expanding the line, _now_ we move the cc
	 * section.
	 */
	if (cols > oldcols)
	    memmove(line->chars + cols, line->chars + oldcols,
		    (line->size - line->cols) * TSIZE);

	/*
	 * Go through what's left of the original line, and adjust
	 * the first cc_next pointer in each list. (All the
	 * subsequent ones are still valid because they are
	 * relative offsets within the cc block.) Also do the same
	 * to the head of the cc_free list.
	 */
	for (i = 0; i < oldcols && i < cols; i++)
	    if (line->chars[i].cc_next)
		line->chars[i].cc_next += cols - oldcols;
	if (line->cc_free)
	    line->cc_free += cols - oldcols;

	/*
	 * And finally fill in the new space with erase chars. (We
	 * don't have to worry about cc lists here, because we
	 * _know_ the erase char doesn't have one.)
	 */
	for (i = oldcols; i < cols; i++)
	    line->chars[i] = term->basic_erase_char;

	cc_check(line);		       /* XXX-REMOVE-BEFORE-RELEASE */
    }
}

/*
 * Get the number of lines in the scrollback.
 */
static int sblines(Terminal *term)
{
    int sblines = count234(term->scrollback);
    if (term->cfg.erase_to_scrollback &&
	term->alt_which && term->alt_screen) {
	    sblines += term->alt_sblines;
    }
    return sblines;
}

/*
 * Retrieve a line of the screen or of the scrollback, according to
 * whether the y coordinate is non-negative or negative
 * (respectively).
 */
static termline *lineptr(Terminal *term, int y, int lineno, int screen)
{
    termline *line;
    tree234 *whichtree;
    int treeindex;

    if (y >= 0) {
	whichtree = term->screen;
	treeindex = y;
    } else {
	int altlines = 0;

	assert(!screen);

	if (term->cfg.erase_to_scrollback &&
	    term->alt_which && term->alt_screen) {
	    altlines = term->alt_sblines;
	}
	if (y < -altlines) {
	    whichtree = term->scrollback;
	    treeindex = y + altlines + count234(term->scrollback);
	} else {
	    whichtree = term->alt_screen;
	    treeindex = y + term->alt_sblines;
	    /* treeindex = y + count234(term->alt_screen); */
	}
    }
    if (whichtree == term->scrollback) {
	unsigned char *cline = index234(whichtree, treeindex);
	line = decompressline(cline, NULL);
    } else {
	line = index234(whichtree, treeindex);
    }

    /* We assume that we don't screw up and retrieve something out of range. */
    assert(line != NULL);

    resizeline(term, line, term->cols);
    /* FIXME: should we sort the compressed scrollback out here? */

    return line;
}

#define lineptr(x) (lineptr)(term,x,__LINE__,FALSE)
#define scrlineptr(x) (lineptr)(term,x,__LINE__,TRUE)

/*
 * Set up power-on settings for the terminal.
 */
static void power_on(Terminal *term)
{
    term->curs.x = term->curs.y = 0;
    term->alt_x = term->alt_y = 0;
    term->savecurs.x = term->savecurs.y = 0;
    term->alt_t = term->marg_t = 0;
    if (term->rows != -1)
	term->alt_b = term->marg_b = term->rows - 1;
    else
	term->alt_b = term->marg_b = 0;
    if (term->cols != -1) {
	int i;
	for (i = 0; i < term->cols; i++)
	    term->tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }
    term->alt_om = term->dec_om = term->cfg.dec_om;
    term->alt_ins = term->insert = FALSE;
    term->alt_wnext = term->wrapnext = term->save_wnext = FALSE;
    term->alt_wrap = term->wrap = term->cfg.wrap_mode;
    term->alt_cset = term->cset = term->save_cset = 0;
    term->alt_utf = term->utf = term->save_utf = 0;
    term->utf_state = 0;
    term->alt_sco_acs = term->sco_acs = term->save_sco_acs = 0;
    term->cset_attr[0] = term->cset_attr[1] = term->save_csattr = CSET_ASCII;
    term->rvideo = 0;
    term->in_vbell = FALSE;
    term->cursor_on = 1;
    term->big_cursor = 0;
    term->default_attr = term->save_attr = term->curr_attr = ATTR_DEFAULT;
    term->term_editing = term->term_echoing = FALSE;
    term->app_cursor_keys = term->cfg.app_cursor;
    term->app_keypad_keys = term->cfg.app_keypad;
    term->use_bce = term->cfg.bce;
    term->blink_is_real = term->cfg.blinktext;
    term->erase_char = term->basic_erase_char;
    term->alt_which = 0;
    term_print_finish(term);
    {
	int i;
	for (i = 0; i < 256; i++)
	    term->wordness[i] = term->cfg.wordness[i];
    }
    if (term->screen) {
	swap_screen(term, 1, FALSE, FALSE);
	erase_lots(term, FALSE, TRUE, TRUE);
	swap_screen(term, 0, FALSE, FALSE);
	erase_lots(term, FALSE, TRUE, TRUE);
    }
}

/*
 * Force a screen update.
 */
void term_update(Terminal *term)
{
    Context ctx;
    ctx = get_ctx(term->frontend);
    if (ctx) {
	int need_sbar_update = term->seen_disp_event;
	if (term->seen_disp_event && term->cfg.scroll_on_disp) {
	    term->disptop = 0;	       /* return to main screen */
	    term->seen_disp_event = 0;
	    need_sbar_update = TRUE;
	}

	if (need_sbar_update)
	    update_sbar(term);
	do_paint(term, ctx, TRUE);
	sys_cursor(term->frontend, term->curs.x, term->curs.y - term->disptop);
	free_ctx(ctx);
    }
}

/*
 * Called from front end when a keypress occurs, to trigger
 * anything magical that needs to happen in that situation.
 */
void term_seen_key_event(Terminal *term)
{
    /*
     * On any keypress, clear the bell overload mechanism
     * completely, on the grounds that large numbers of
     * beeps coming from deliberate key action are likely
     * to be intended (e.g. beeps from filename completion
     * blocking repeatedly).
     */
    term->beep_overloaded = FALSE;
    while (term->beephead) {
	struct beeptime *tmp = term->beephead;
	term->beephead = tmp->next;
	sfree(tmp);
    }
    term->beeptail = NULL;
    term->nbeeps = 0;

    /*
     * Reset the scrollback on keypress, if we're doing that.
     */
    if (term->cfg.scroll_on_key) {
	term->disptop = 0;	       /* return to main screen */
	term->seen_disp_event = 1;
    }
}

/*
 * Same as power_on(), but an external function.
 */
void term_pwron(Terminal *term)
{
    power_on(term);
    if (term->ldisc)		       /* cause ldisc to notice changes */
	ldisc_send(term->ldisc, NULL, 0, 0);
    term->disptop = 0;
    deselect(term);
    term_update(term);
}

static void set_erase_char(Terminal *term)
{
    term->erase_char = term->basic_erase_char;
    if (term->use_bce)
	term->erase_char.attr = (term->curr_attr &
				 (ATTR_FGMASK | ATTR_BGMASK));
}

/*
 * When the user reconfigures us, we need to check the forbidden-
 * alternate-screen config option, disable raw mouse mode if the
 * user has disabled mouse reporting, and abandon a print job if
 * the user has disabled printing.
 */
void term_reconfig(Terminal *term, Config *cfg)
{
    /*
     * Before adopting the new config, check all those terminal
     * settings which control power-on defaults; and if they've
     * changed, we will modify the current state as well as the
     * default one. The full list is: Auto wrap mode, DEC Origin
     * Mode, BCE, blinking text, character classes.
     */
    int reset_wrap, reset_decom, reset_bce, reset_blink, reset_charclass;
    int i;

    reset_wrap = (term->cfg.wrap_mode != cfg->wrap_mode);
    reset_decom = (term->cfg.dec_om != cfg->dec_om);
    reset_bce = (term->cfg.bce != cfg->bce);
    reset_blink = (term->cfg.blinktext != cfg->blinktext);
    reset_charclass = 0;
    for (i = 0; i < lenof(term->cfg.wordness); i++)
	if (term->cfg.wordness[i] != cfg->wordness[i])
	    reset_charclass = 1;

    /*
     * If the bidi or shaping settings have changed, flush the bidi
     * cache completely.
     */
    if (term->cfg.arabicshaping != cfg->arabicshaping ||
	term->cfg.bidi != cfg->bidi) {
	for (i = 0; i < term->bidi_cache_size; i++) {
	    sfree(term->pre_bidi_cache[i].chars);
	    sfree(term->post_bidi_cache[i].chars);
	    term->pre_bidi_cache[i].width = -1;
	    term->pre_bidi_cache[i].chars = NULL;
	    term->post_bidi_cache[i].width = -1;
	    term->post_bidi_cache[i].chars = NULL;
	}
    }

    term->cfg = *cfg;		       /* STRUCTURE COPY */

    if (reset_wrap)
	term->alt_wrap = term->wrap = term->cfg.wrap_mode;
    if (reset_decom)
	term->alt_om = term->dec_om = term->cfg.dec_om;
    if (reset_bce) {
	term->use_bce = term->cfg.bce;
	set_erase_char(term);
    }
    if (reset_blink)
	term->blink_is_real = term->cfg.blinktext;
    if (reset_charclass)
	for (i = 0; i < 256; i++)
	    term->wordness[i] = term->cfg.wordness[i];

    if (term->cfg.no_alt_screen)
	swap_screen(term, 0, FALSE, FALSE);
    if (term->cfg.no_mouse_rep) {
	term->xterm_mouse = 0;
	set_raw_mouse_mode(term->frontend, 0);
    }
    if (term->cfg.no_remote_charset) {
	term->cset_attr[0] = term->cset_attr[1] = CSET_ASCII;
	term->sco_acs = term->alt_sco_acs = 0;
	term->utf = 0;
    }
    if (!*term->cfg.printer) {
	term_print_finish(term);
    }
}

/*
 * Clear the scrollback.
 */
void term_clrsb(Terminal *term)
{
    termline *line;
    term->disptop = 0;
    while ((line = delpos234(term->scrollback, 0)) != NULL) {
	sfree(line);            /* this is compressed data, not a termline */
    }
    term->tempsblines = 0;
    term->alt_sblines = 0;
    update_sbar(term);
}

/*
 * Initialise the terminal.
 */
Terminal *term_init(Config *mycfg, struct unicode_data *ucsdata,
		    void *frontend)
{
    Terminal *term;

    /*
     * Allocate a new Terminal structure and initialise the fields
     * that need it.
     */
    term = snew(Terminal);
    term->frontend = frontend;
    term->ucsdata = ucsdata;
    term->cfg = *mycfg;		       /* STRUCTURE COPY */
    term->logctx = NULL;
    term->compatibility_level = TM_PUTTY;
    strcpy(term->id_string, "\033[?6c");
    term->last_blink = term->last_tblink = 0;
    term->paste_buffer = NULL;
    term->paste_len = 0;
    term->last_paste = 0;
    bufchain_init(&term->inbuf);
    bufchain_init(&term->printer_buf);
    term->printing = term->only_printing = FALSE;
    term->print_job = NULL;
    term->vt52_mode = FALSE;
    term->cr_lf_return = FALSE;
    term->seen_disp_event = FALSE;
    term->xterm_mouse = term->mouse_is_down = FALSE;
    term->reset_132 = FALSE;
    term->blinker = term->tblinker = 0;
    term->has_focus = 1;
    term->repeat_off = FALSE;
    term->termstate = TOPLEVEL;
    term->selstate = NO_SELECTION;
    term->curstype = 0;

    term->screen = term->alt_screen = term->scrollback = NULL;
    term->tempsblines = 0;
    term->alt_sblines = 0;
    term->disptop = 0;
    term->disptext = NULL;
    term->dispcursx = term->dispcursy = -1;
    term->tabs = NULL;
    deselect(term);
    term->rows = term->cols = -1;
    power_on(term);
    term->beephead = term->beeptail = NULL;
#ifdef OPTIMISE_SCROLL
    term->scrollhead = term->scrolltail = NULL;
#endif /* OPTIMISE_SCROLL */
    term->nbeeps = 0;
    term->lastbeep = FALSE;
    term->beep_overloaded = FALSE;
    term->attr_mask = 0xffffffff;
    term->resize_fn = NULL;
    term->resize_ctx = NULL;
    term->in_term_out = FALSE;
    term->ltemp = NULL;
    term->ltemp_size = 0;
    term->wcFrom = NULL;
    term->wcTo = NULL;
    term->wcFromTo_size = 0;

    term->bidi_cache_size = 0;
    term->pre_bidi_cache = term->post_bidi_cache = NULL;

    /* FULL-TERMCHAR */
    term->basic_erase_char.chr = CSET_ASCII | ' ';
    term->basic_erase_char.attr = ATTR_DEFAULT;
    term->basic_erase_char.cc_next = 0;
    term->erase_char = term->basic_erase_char;

    return term;
}

void term_free(Terminal *term)
{
    termline *line;
    struct beeptime *beep;
    int i;

    while ((line = delpos234(term->scrollback, 0)) != NULL)
	sfree(line);		       /* compressed data, not a termline */
    freetree234(term->scrollback);
    while ((line = delpos234(term->screen, 0)) != NULL)
	freeline(line);
    freetree234(term->screen);
    while ((line = delpos234(term->alt_screen, 0)) != NULL)
	freeline(line);
    freetree234(term->alt_screen);
    if (term->disptext) {
	for (i = 0; i < term->rows; i++)
	    freeline(term->disptext[i]);
    }
    sfree(term->disptext);
    while (term->beephead) {
	beep = term->beephead;
	term->beephead = beep->next;
	sfree(beep);
    }
    bufchain_clear(&term->inbuf);
    if(term->print_job)
	printer_finish_job(term->print_job);
    bufchain_clear(&term->printer_buf);
    sfree(term->paste_buffer);
    sfree(term->ltemp);
    sfree(term->wcFrom);
    sfree(term->wcTo);

    for (i = 0; i < term->bidi_cache_size; i++) {
	sfree(term->pre_bidi_cache[i].chars);
	sfree(term->post_bidi_cache[i].chars);
    }
    sfree(term->pre_bidi_cache);
    sfree(term->post_bidi_cache);

    sfree(term);
}

/*
 * Set up the terminal for a given size.
 */
void term_size(Terminal *term, int newrows, int newcols, int newsavelines)
{
    tree234 *newalt;
    termline **newdisp, *line;
    int i, j, oldrows = term->rows;
    int sblen;
    int save_alt_which = term->alt_which;

    if (newrows == term->rows && newcols == term->cols &&
	newsavelines == term->savelines)
	return;			       /* nothing to do */

    deselect(term);
    swap_screen(term, 0, FALSE, FALSE);

    term->alt_t = term->marg_t = 0;
    term->alt_b = term->marg_b = newrows - 1;

    if (term->rows == -1) {
	term->scrollback = newtree234(NULL);
	term->screen = newtree234(NULL);
	term->tempsblines = 0;
	term->rows = 0;
    }

    /*
     * Resize the screen and scrollback. We only need to shift
     * lines around within our data structures, because lineptr()
     * will take care of resizing each individual line if
     * necessary. So:
     * 
     *  - If the new screen is longer, we shunt lines in from temporary
     *    scrollback if possible, otherwise we add new blank lines at
     *    the bottom.
     *
     *  - If the new screen is shorter, we remove any blank lines at
     *    the bottom if possible, otherwise shunt lines above the cursor
     *    to scrollback if possible, otherwise delete lines below the
     *    cursor.
     * 
     *  - Then, if the new scrollback length is less than the
     *    amount of scrollback we actually have, we must throw some
     *    away.
     */
    sblen = count234(term->scrollback);
    /* Do this loop to expand the screen if newrows > rows */
    assert(term->rows == count234(term->screen));
    while (term->rows < newrows) {
	if (term->tempsblines > 0) {
	    unsigned char *cline;
	    /* Insert a line from the scrollback at the top of the screen. */
	    assert(sblen >= term->tempsblines);
	    cline = delpos234(term->scrollback, --sblen);
	    line = decompressline(cline, NULL);
	    sfree(cline);
	    line->temporary = FALSE;   /* reconstituted line is now real */
	    term->tempsblines -= 1;
	    addpos234(term->screen, line, 0);
	    term->curs.y += 1;
	    term->savecurs.y += 1;
	} else {
	    /* Add a new blank line at the bottom of the screen. */
	    line = newline(term, newcols, FALSE);
	    addpos234(term->screen, line, count234(term->screen));
	}
	term->rows += 1;
    }
    /* Do this loop to shrink the screen if newrows < rows */
    while (term->rows > newrows) {
	if (term->curs.y < term->rows - 1) {
	    /* delete bottom row, unless it contains the cursor */
	    sfree(delpos234(term->screen, term->rows - 1));
	} else {
	    /* push top row to scrollback */
	    line = delpos234(term->screen, 0);
	    addpos234(term->scrollback, compressline(line), sblen++);
	    freeline(line);
	    term->tempsblines += 1;
	    term->curs.y -= 1;
	    term->savecurs.y -= 1;
	}
	term->rows -= 1;
    }
    assert(term->rows == newrows);
    assert(count234(term->screen) == newrows);

    /* Delete any excess lines from the scrollback. */
    while (sblen > newsavelines) {
	line = delpos234(term->scrollback, 0);
	sfree(line);
	sblen--;
    }
    if (sblen < term->tempsblines)
	term->tempsblines = sblen;
    assert(count234(term->scrollback) <= newsavelines);
    assert(count234(term->scrollback) >= term->tempsblines);
    term->disptop = 0;

    /* Make a new displayed text buffer. */
    newdisp = snewn(newrows, termline *);
    for (i = 0; i < newrows; i++) {
	newdisp[i] = newline(term, newcols, FALSE);
	for (j = 0; j < newcols; j++)
	    newdisp[i]->chars[i].attr = ATTR_INVALID;
    }
    if (term->disptext) {
	for (i = 0; i < oldrows; i++)
	    freeline(term->disptext[i]);
    }
    sfree(term->disptext);
    term->disptext = newdisp;
    term->dispcursx = term->dispcursy = -1;

    /* Make a new alternate screen. */
    newalt = newtree234(NULL);
    for (i = 0; i < newrows; i++) {
	line = newline(term, newcols, TRUE);
	addpos234(newalt, line, i);
    }
    if (term->alt_screen) {
	while (NULL != (line = delpos234(term->alt_screen, 0)))
	    freeline(line);
	freetree234(term->alt_screen);
    }
    term->alt_screen = newalt;
    term->alt_sblines = 0;

    term->tabs = sresize(term->tabs, newcols, unsigned char);
    {
	int i;
	for (i = (term->cols > 0 ? term->cols : 0); i < newcols; i++)
	    term->tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }

    /* Check that the cursor positions are still valid. */
    if (term->savecurs.y < 0)
	term->savecurs.y = 0;
    if (term->savecurs.y >= newrows)
	term->savecurs.y = newrows - 1;
    if (term->curs.y < 0)
	term->curs.y = 0;
    if (term->curs.y >= newrows)
	term->curs.y = newrows - 1;
    if (term->curs.x >= newcols)
	term->curs.x = newcols - 1;
    term->alt_x = term->alt_y = 0;
    term->wrapnext = term->alt_wnext = FALSE;

    term->rows = newrows;
    term->cols = newcols;
    term->savelines = newsavelines;

    swap_screen(term, save_alt_which, FALSE, FALSE);

    update_sbar(term);
    term_update(term);
    if (term->resize_fn)
	term->resize_fn(term->resize_ctx, term->cols, term->rows);
}

/*
 * Hand a function and context pointer to the terminal which it can
 * use to notify a back end of resizes.
 */
void term_provide_resize_fn(Terminal *term,
			    void (*resize_fn)(void *, int, int),
			    void *resize_ctx)
{
    term->resize_fn = resize_fn;
    term->resize_ctx = resize_ctx;
    if (term->cols > 0 && term->rows > 0)
	resize_fn(resize_ctx, term->cols, term->rows);
}

/* Find the bottom line on the screen that has any content.
 * If only the top line has content, returns 0.
 * If no lines have content, return -1.
 */ 
static int find_last_nonempty_line(Terminal * term, tree234 * screen)
{
    int i;
    for (i = count234(screen) - 1; i >= 0; i--) {
	termline *line = index234(screen, i);
	int j;
	for (j = 0; j < line->cols; j++)
	    if (!termchars_equal(&line->chars[j], &term->erase_char))
		break;
	if (j != line->cols) break;
    }
    return i;
}

/*
 * Swap screens. If `reset' is TRUE and we have been asked to
 * switch to the alternate screen, we must bring most of its
 * configuration from the main screen and erase the contents of the
 * alternate screen completely. (This is even true if we're already
 * on it! Blame xterm.)
 */
static void swap_screen(Terminal *term, int which, int reset, int keep_cur_pos)
{
    int t;
    tree234 *ttr;

    if (!which)
	reset = FALSE;		       /* do no weird resetting if which==0 */

    if (which != term->alt_which) {
	term->alt_which = which;

	ttr = term->alt_screen;
	term->alt_screen = term->screen;
	term->screen = ttr;
	term->alt_sblines = find_last_nonempty_line(term, term->alt_screen) + 1;
	t = term->curs.x;
	if (!reset && !keep_cur_pos)
	    term->curs.x = term->alt_x;
	term->alt_x = t;
	t = term->curs.y;
	if (!reset && !keep_cur_pos)
	    term->curs.y = term->alt_y;
	term->alt_y = t;
	t = term->marg_t;
	if (!reset) term->marg_t = term->alt_t;
	term->alt_t = t;
	t = term->marg_b;
	if (!reset) term->marg_b = term->alt_b;
	term->alt_b = t;
	t = term->dec_om;
	if (!reset) term->dec_om = term->alt_om;
	term->alt_om = t;
	t = term->wrap;
	if (!reset) term->wrap = term->alt_wrap;
	term->alt_wrap = t;
	t = term->wrapnext;
	if (!reset) term->wrapnext = term->alt_wnext;
	term->alt_wnext = t;
	t = term->insert;
	if (!reset) term->insert = term->alt_ins;
	term->alt_ins = t;
	t = term->cset;
	if (!reset) term->cset = term->alt_cset;
	term->alt_cset = t;
	t = term->utf;
	if (!reset) term->utf = term->alt_utf;
	term->alt_utf = t;
	t = term->sco_acs;
	if (!reset) term->sco_acs = term->alt_sco_acs;
	term->alt_sco_acs = t;
    }

    if (reset && term->screen) {
	/*
	 * Yes, this _is_ supposed to honour background-colour-erase.
	 */
	erase_lots(term, FALSE, TRUE, TRUE);
    }
}

/*
 * Update the scroll bar.
 */
static void update_sbar(Terminal *term)
{
    int nscroll = sblines(term);
    set_sbar(term->frontend, nscroll + term->rows,
	     nscroll + term->disptop, term->rows);
}

/*
 * Check whether the region bounded by the two pointers intersects
 * the scroll region, and de-select the on-screen selection if so.
 */
static void check_selection(Terminal *term, pos from, pos to)
{
    if (poslt(from, term->selend) && poslt(term->selstart, to))
	deselect(term);
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is TRUE if the scrolling is permitted to
 * affect the scrollback buffer.
 */
static void scroll(Terminal *term, int topline, int botline, int lines, int sb)
{
    termline *line;
    int i, seltop, olddisptop, shift;

    if (topline != 0 || term->alt_which != 0)
	sb = FALSE;

    olddisptop = term->disptop;
    shift = lines;
    if (lines < 0) {
	while (lines < 0) {
	    line = delpos234(term->screen, botline);
            resizeline(term, line, term->cols);
	    for (i = 0; i < term->cols; i++)
		copy_termchar(line, i, &term->erase_char);
	    line->lattr = LATTR_NORM;
	    addpos234(term->screen, line, topline);

	    if (term->selstart.y >= topline && term->selstart.y <= botline) {
		term->selstart.y++;
		if (term->selstart.y > botline) {
		    term->selstart.y = botline + 1;
		    term->selstart.x = 0;
		}
	    }
	    if (term->selend.y >= topline && term->selend.y <= botline) {
		term->selend.y++;
		if (term->selend.y > botline) {
		    term->selend.y = botline + 1;
		    term->selend.x = 0;
		}
	    }

	    lines++;
	}
    } else {
	while (lines > 0) {
	    line = delpos234(term->screen, topline);
	    cc_check(line);	       /* XXX-REMOVE-BEFORE-RELEASE */
	    if (sb && term->savelines > 0) {
		int sblen = count234(term->scrollback);
		/*
		 * We must add this line to the scrollback. We'll
		 * remove a line from the top of the scrollback if
		 * the scrollback is full.
		 */
		if (sblen == term->savelines) {
		    unsigned char *cline;

		    sblen--;
		    cline = delpos234(term->scrollback, 0);
		    sfree(cline);
		} else
		    term->tempsblines += 1;

		addpos234(term->scrollback, compressline(line), sblen);

		/* now `line' itself can be reused as the bottom line */

		/*
		 * If the user is currently looking at part of the
		 * scrollback, and they haven't enabled any options
		 * that are going to reset the scrollback as a
		 * result of this movement, then the chances are
		 * they'd like to keep looking at the same line. So
		 * we move their viewpoint at the same rate as the
		 * scroll, at least until their viewpoint hits the
		 * top end of the scrollback buffer, at which point
		 * we don't have the choice any more.
		 * 
		 * Thanks to Jan Holmen Holsten for the idea and
		 * initial implementation.
		 */
		if (term->disptop > -term->savelines && term->disptop < 0)
		    term->disptop--;
	    }
            resizeline(term, line, term->cols);
	    for (i = 0; i < term->cols; i++)
		copy_termchar(line, i, &term->erase_char);
	    line->lattr = LATTR_NORM;
	    addpos234(term->screen, line, botline);

	    /*
	     * If the selection endpoints move into the scrollback,
	     * we keep them moving until they hit the top. However,
	     * of course, if the line _hasn't_ moved into the
	     * scrollback then we don't do this, and cut them off
	     * at the top of the scroll region.
	     * 
	     * This applies to selstart and selend (for an existing
	     * selection), and also selanchor (for one being
	     * selected as we speak).
	     */
	    seltop = sb ? -term->savelines : topline;

	    if (term->selstate != NO_SELECTION) {
		if (term->selstart.y >= seltop &&
		    term->selstart.y <= botline) {
		    term->selstart.y--;
		    if (term->selstart.y < seltop) {
			term->selstart.y = seltop;
			term->selstart.x = 0;
		    }
		}
		if (term->selend.y >= seltop && term->selend.y <= botline) {
		    term->selend.y--;
		    if (term->selend.y < seltop) {
			term->selend.y = seltop;
			term->selend.x = 0;
		    }
		}
		if (term->selanchor.y >= seltop &&
		    term->selanchor.y <= botline) {
		    term->selanchor.y--;
		    if (term->selanchor.y < seltop) {
			term->selanchor.y = seltop;
			term->selanchor.x = 0;
		    }
		}
	    }

	    lines--;
	}
    }
#ifdef OPTIMISE_SCROLL
    shift += term->disptop - olddisptop;
    if (shift < term->rows && shift > -term->rows && shift != 0)
	scroll_display(term, topline, botline, shift);
#endif /* OPTIMISE_SCROLL */
}

#ifdef OPTIMISE_SCROLL
/*
 * Add a scroll of a region on the screen into the pending scroll list.
 * `lines' is +ve for scrolling forward, -ve for backward.
 *
 * If the scroll is on the same area as the last scroll in the list,
 * merge them.
 */
static void save_scroll(Terminal *term, int topline, int botline, int lines)
{
    struct scrollregion *newscroll;
    if (term->scrolltail &&
	term->scrolltail->topline == topline && 
	term->scrolltail->botline == botline) {
	term->scrolltail->lines += lines;
    } else {
	newscroll = snew(struct scrollregion);
	newscroll->topline = topline;
	newscroll->botline = botline;
	newscroll->lines = lines;
	newscroll->next = NULL;

	if (!term->scrollhead)
	    term->scrollhead = newscroll;
	else
	    term->scrolltail->next = newscroll;
	term->scrolltail = newscroll;
    }
}

/*
 * Scroll the physical display, and our conception of it in disptext.
 */
static void scroll_display(Terminal *term, int topline, int botline, int lines)
{
    int distance, nlines, i, j;

    distance = lines > 0 ? lines : -lines;
    nlines = botline - topline + 1 - distance;
    if (lines > 0) {
	for (i = 0; i < nlines; i++)
	    for (j = 0; j < term->cols; j++)
		copy_termchar(term->disptext[start+i], j,
			      term->disptext[start+i+distance]->chars+j);
	if (term->dispcursy >= 0 &&
	    term->dispcursy >= topline + distance &&
	    term->dispcursy < topline + distance + nlines)
	    term->dispcursy -= distance;
	for (i = 0; i < distance; i++)
	    for (j = 0; j < term->cols; j++)
		term->disptext[start+nlines+i]->chars[j].attr |= ATTR_INVALID;
    } else {
	for (i = nlines; i-- ;)
	    for (j = 0; j < term->cols; j++)
		copy_termchar(term->disptext[start+i+distance], j,
			      term->disptext[start+i]->chars+j);
	if (term->dispcursy >= 0 &&
	    term->dispcursy >= topline &&
	    term->dispcursy < topline + nlines)
	    term->dispcursy += distance;
	for (i = 0; i < distance; i++)
	    for (j = 0; j < term->cols; j++)
		term->disptext[start+i]->chars[j].attr |= ATTR_INVALID;
    }
    save_scroll(term, topline, botline, lines);
}
#endif /* OPTIMISE_SCROLL */

/*
 * Move the cursor to a given position, clipping at boundaries. We
 * may or may not want to clip at the scroll margin: marg_clip is 0
 * not to, 1 to disallow _passing_ the margins, and 2 to disallow
 * even _being_ outside the margins.
 */
static void move(Terminal *term, int x, int y, int marg_clip)
{
    if (x < 0)
	x = 0;
    if (x >= term->cols)
	x = term->cols - 1;
    if (marg_clip) {
	if ((term->curs.y >= term->marg_t || marg_clip == 2) &&
	    y < term->marg_t)
	    y = term->marg_t;
	if ((term->curs.y <= term->marg_b || marg_clip == 2) &&
	    y > term->marg_b)
	    y = term->marg_b;
    }
    if (y < 0)
	y = 0;
    if (y >= term->rows)
	y = term->rows - 1;
    term->curs.x = x;
    term->curs.y = y;
    term->wrapnext = FALSE;
}

/*
 * Save or restore the cursor and SGR mode.
 */
static void save_cursor(Terminal *term, int save)
{
    if (save) {
	term->savecurs = term->curs;
	term->save_attr = term->curr_attr;
	term->save_cset = term->cset;
	term->save_utf = term->utf;
	term->save_wnext = term->wrapnext;
	term->save_csattr = term->cset_attr[term->cset];
	term->save_sco_acs = term->sco_acs;
    } else {
	term->curs = term->savecurs;
	/* Make sure the window hasn't shrunk since the save */
	if (term->curs.x >= term->cols)
	    term->curs.x = term->cols - 1;
	if (term->curs.y >= term->rows)
	    term->curs.y = term->rows - 1;

	term->curr_attr = term->save_attr;
	term->cset = term->save_cset;
	term->utf = term->save_utf;
	term->wrapnext = term->save_wnext;
	/*
	 * wrapnext might reset to False if the x position is no
	 * longer at the rightmost edge.
	 */
	if (term->wrapnext && term->curs.x < term->cols-1)
	    term->wrapnext = FALSE;
	term->cset_attr[term->cset] = term->save_csattr;
	term->sco_acs = term->save_sco_acs;
	set_erase_char(term);
    }
}

/*
 * This function is called before doing _anything_ which affects
 * only part of a line of text. It is used to mark the boundary
 * between two character positions, and it indicates that some sort
 * of effect is going to happen on only one side of that boundary.
 * 
 * The effect of this function is to check whether a CJK
 * double-width character is straddling the boundary, and to remove
 * it and replace it with two spaces if so. (Of course, one or
 * other of those spaces is then likely to be replaced with
 * something else again, as a result of whatever happens next.)
 * 
 * Also, if the boundary is at the right-hand _edge_ of the screen,
 * it implies something deliberate is being done to the rightmost
 * column position; hence we must clear LATTR_WRAPPED2.
 * 
 * The input to the function is the coordinates of the _second_
 * character of the pair.
 */
static void check_boundary(Terminal *term, int x, int y)
{
    termline *ldata;

    /* Validate input coordinates, just in case. */
    if (x == 0 || x > term->cols)
	return;

    ldata = scrlineptr(y);
    if (x == term->cols) {
	ldata->lattr &= ~LATTR_WRAPPED2;
    } else {
	if (ldata->chars[x].chr == UCSWIDE) {
	    clear_cc(ldata, x-1);
	    clear_cc(ldata, x);
	    ldata->chars[x-1].chr = ' ' | CSET_ASCII;
	    ldata->chars[x] = ldata->chars[x-1];
	}
    }
}

/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
static void erase_lots(Terminal *term,
		       int line_only, int from_begin, int to_end)
{
    pos start, end;
    int erase_lattr;
    int erasing_lines_from_top = 0;

    if (line_only) {
	start.y = term->curs.y;
	start.x = 0;
	end.y = term->curs.y + 1;
	end.x = 0;
	erase_lattr = FALSE;
    } else {
	start.y = 0;
	start.x = 0;
	end.y = term->rows;
	end.x = 0;
	erase_lattr = TRUE;
    }
    if (!from_begin) {
	start = term->curs;
    }
    if (!to_end) {
	end = term->curs;
	incpos(end);
    }
    if (!from_begin || !to_end)
	check_boundary(term, term->curs.x, term->curs.y);
    check_selection(term, start, end);

    /* Clear screen also forces a full window redraw, just in case. */
    if (start.y == 0 && start.x == 0 && end.y == term->rows)
	term_invalidate(term);

    /* Lines scrolled away shouldn't be brought back on if the terminal
     * resizes. */
    if (start.y == 0 && start.x == 0 && end.x == 0 && erase_lattr)
	erasing_lines_from_top = 1;

    if (term->cfg.erase_to_scrollback && erasing_lines_from_top) {
	/* If it's a whole number of lines, starting at the top, and
	 * we're fully erasing them, erase by scrolling and keep the
	 * lines in the scrollback. */
	int scrolllines = end.y;
	if (end.y == term->rows) {
	    /* Shrink until we find a non-empty row.*/
	    scrolllines = find_last_nonempty_line(term, term->screen) + 1;
	}
	if (scrolllines > 0)
	    scroll(term, 0, scrolllines - 1, scrolllines, TRUE);
    } else {
	termline *ldata = scrlineptr(start.y);
	while (poslt(start, end)) {
	    if (start.x == term->cols) {
		if (!erase_lattr)
		    ldata->lattr &= ~(LATTR_WRAPPED | LATTR_WRAPPED2);
		else
		    ldata->lattr = LATTR_NORM;
	    } else {
		copy_termchar(ldata, start.x, &term->erase_char);
	    }
	    if (incpos(start) && start.y < term->rows) {
		ldata = scrlineptr(start.y);
	    }
	}
    }

    /* After an erase of lines from the top of the screen, we shouldn't
     * bring the lines back again if the terminal enlarges (since the user or
     * application has explictly thrown them away). */
    if (erasing_lines_from_top && !(term->alt_which))
	term->tempsblines = 0;
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void insch(Terminal *term, int n)
{
    int dir = (n < 0 ? -1 : +1);
    int m, j;
    pos cursplus;
    termline *ldata;

    n = (n < 0 ? -n : n);
    if (n > term->cols - term->curs.x)
	n = term->cols - term->curs.x;
    m = term->cols - term->curs.x - n;
    cursplus.y = term->curs.y;
    cursplus.x = term->curs.x + n;
    check_selection(term, term->curs, cursplus);
    check_boundary(term, term->curs.x, term->curs.y);
    if (dir < 0)
	check_boundary(term, term->curs.x + n, term->curs.y);
    ldata = scrlineptr(term->curs.y);
    if (dir < 0) {
	for (j = 0; j < m; j++)
	    move_termchar(ldata,
			  ldata->chars + term->curs.x + j,
			  ldata->chars + term->curs.x + j + n);
	while (n--)
	    copy_termchar(ldata, term->curs.x + m++, &term->erase_char);
    } else {
	for (j = m; j-- ;)
	    move_termchar(ldata,
			  ldata->chars + term->curs.x + j + n,
			  ldata->chars + term->curs.x + j);
	while (n--)
	    copy_termchar(ldata, term->curs.x + n, &term->erase_char);
    }
}

/*
 * Toggle terminal mode `mode' to state `state'. (`query' indicates
 * whether the mode is a DEC private one or a normal one.)
 */
static void toggle_mode(Terminal *term, int mode, int query, int state)
{
    unsigned long ticks;

    if (query)
	switch (mode) {
	  case 1:		       /* DECCKM: application cursor keys */
	    term->app_cursor_keys = state;
	    break;
	  case 2:		       /* DECANM: VT52 mode */
	    term->vt52_mode = !state;
	    if (term->vt52_mode) {
		term->blink_is_real = FALSE;
		term->vt52_bold = FALSE;
	    } else {
		term->blink_is_real = term->cfg.blinktext;
	    }
	    break;
	  case 3:		       /* DECCOLM: 80/132 columns */
	    deselect(term);
	    if (!term->cfg.no_remote_resize)
		request_resize(term->frontend, state ? 132 : 80, term->rows);
	    term->reset_132 = state;
	    term->alt_t = term->marg_t = 0;
	    term->alt_b = term->marg_b = term->rows - 1;
	    move(term, 0, 0, 0);
	    erase_lots(term, FALSE, TRUE, TRUE);
	    break;
	  case 5:		       /* DECSCNM: reverse video */
	    /*
	     * Toggle reverse video. If we receive an OFF within the
	     * visual bell timeout period after an ON, we trigger an
	     * effective visual bell, so that ESC[?5hESC[?5l will
	     * always be an actually _visible_ visual bell.
	     */
	    ticks = GETTICKCOUNT();
	    /* turn off a previous vbell to avoid inconsistencies */
	    if (ticks - term->vbell_startpoint >= VBELL_TIMEOUT)
		term->in_vbell = FALSE;
	    if (term->rvideo && !state &&    /* we're turning it off... */
		(ticks - term->rvbell_startpoint) < VBELL_TIMEOUT) {/*...soon*/
		/* If there's no vbell timeout already, or this one lasts
		 * longer, replace vbell_timeout with ours. */
		if (!term->in_vbell ||
		    (term->rvbell_startpoint - term->vbell_startpoint <
		     VBELL_TIMEOUT))
		    term->vbell_startpoint = term->rvbell_startpoint;
		term->in_vbell = TRUE; /* may clear rvideo but set in_vbell */
	    } else if (!term->rvideo && state) {
		/* This is an ON, so we notice the time and save it. */
		term->rvbell_startpoint = ticks;
	    }
	    term->rvideo = state;
	    term->seen_disp_event = TRUE;
	    if (state)
		term_update(term);
	    break;
	  case 6:		       /* DECOM: DEC origin mode */
	    term->dec_om = state;
	    break;
	  case 7:		       /* DECAWM: auto wrap */
	    term->wrap = state;
	    break;
	  case 8:		       /* DECARM: auto key repeat */
	    term->repeat_off = !state;
	    break;
	  case 10:		       /* DECEDM: set local edit mode */
	    term->term_editing = state;
	    if (term->ldisc)	       /* cause ldisc to notice changes */
		ldisc_send(term->ldisc, NULL, 0, 0);
	    break;
	  case 25:		       /* DECTCEM: enable/disable cursor */
	    compatibility2(OTHER, VT220);
	    term->cursor_on = state;
	    term->seen_disp_event = TRUE;
	    break;
	  case 47:		       /* alternate screen */
	    compatibility(OTHER);
	    deselect(term);
	    swap_screen(term, term->cfg.no_alt_screen ? 0 : state, FALSE, FALSE);
	    term->disptop = 0;
	    break;
	  case 1000:		       /* xterm mouse 1 */
	    term->xterm_mouse = state ? 1 : 0;
	    set_raw_mouse_mode(term->frontend, state);
	    break;
	  case 1002:		       /* xterm mouse 2 */
	    term->xterm_mouse = state ? 2 : 0;
	    set_raw_mouse_mode(term->frontend, state);
	    break;
	  case 1047:                   /* alternate screen */
	    compatibility(OTHER);
	    deselect(term);
	    swap_screen(term, term->cfg.no_alt_screen ? 0 : state, TRUE, TRUE);
	    term->disptop = 0;
	    break;
	  case 1048:                   /* save/restore cursor */
	    if (!term->cfg.no_alt_screen)
                save_cursor(term, state);
	    if (!state) term->seen_disp_event = TRUE;
	    break;
	  case 1049:                   /* cursor & alternate screen */
	    if (state && !term->cfg.no_alt_screen)
		save_cursor(term, state);
	    if (!state) term->seen_disp_event = TRUE;
	    compatibility(OTHER);
	    deselect(term);
	    swap_screen(term, term->cfg.no_alt_screen ? 0 : state, TRUE, FALSE);
	    if (!state && !term->cfg.no_alt_screen)
		save_cursor(term, state);
	    term->disptop = 0;
	    break;
    } else
	switch (mode) {
	  case 4:		       /* IRM: set insert mode */
	    compatibility(VT102);
	    term->insert = state;
	    break;
	  case 12:		       /* SRM: set echo mode */
	    term->term_echoing = !state;
	    if (term->ldisc)	       /* cause ldisc to notice changes */
		ldisc_send(term->ldisc, NULL, 0, 0);
	    break;
	  case 20:		       /* LNM: Return sends ... */
	    term->cr_lf_return = state;
	    break;
	  case 34:		       /* WYULCURM: Make cursor BIG */
	    compatibility2(OTHER, VT220);
	    term->big_cursor = !state;
	}
}

/*
 * Process an OSC sequence: set window title or icon name.
 */
static void do_osc(Terminal *term)
{
    if (term->osc_w) {
	while (term->osc_strlen--)
	    term->wordness[(unsigned char)
		term->osc_string[term->osc_strlen]] = term->esc_args[0];
    } else {
	term->osc_string[term->osc_strlen] = '\0';
	switch (term->esc_args[0]) {
	  case 0:
	  case 1:
	    if (!term->cfg.no_remote_wintitle)
		set_icon(term->frontend, term->osc_string);
	    if (term->esc_args[0] == 1)
		break;
	    /* fall through: parameter 0 means set both */
	  case 2:
	  case 21:
	    if (!term->cfg.no_remote_wintitle)
		set_title(term->frontend, term->osc_string);
	    break;
	}
    }
}

/*
 * ANSI printing routines.
 */
static void term_print_setup(Terminal *term)
{
    bufchain_clear(&term->printer_buf);
    term->print_job = printer_start_job(term->cfg.printer);
}
static void term_print_flush(Terminal *term)
{
    void *data;
    int len;
    int size;
    while ((size = bufchain_size(&term->printer_buf)) > 5) {
	bufchain_prefix(&term->printer_buf, &data, &len);
	if (len > size-5)
	    len = size-5;
	printer_job_data(term->print_job, data, len);
	bufchain_consume(&term->printer_buf, len);
    }
}
static void term_print_finish(Terminal *term)
{
    void *data;
    int len, size;
    char c;

    if (!term->printing && !term->only_printing)
	return;			       /* we need do nothing */

    term_print_flush(term);
    while ((size = bufchain_size(&term->printer_buf)) > 0) {
	bufchain_prefix(&term->printer_buf, &data, &len);
	c = *(char *)data;
	if (c == '\033' || c == '\233') {
	    bufchain_consume(&term->printer_buf, size);
	    break;
	} else {
	    printer_job_data(term->print_job, &c, 1);
	    bufchain_consume(&term->printer_buf, 1);
	}
    }
    printer_finish_job(term->print_job);
    term->print_job = NULL;
    term->printing = term->only_printing = FALSE;
}

/*
 * Remove everything currently in `inbuf' and stick it up on the
 * in-memory display. There's a big state machine in here to
 * process escape sequences...
 */
void term_out(Terminal *term)
{
    unsigned long c;
    int unget;
    unsigned char localbuf[256], *chars;
    int nchars = 0;

    unget = -1;

    chars = NULL;		       /* placate compiler warnings */
    while (nchars > 0 || bufchain_size(&term->inbuf) > 0) {
	if (unget == -1) {
	    if (nchars == 0) {
		void *ret;
		bufchain_prefix(&term->inbuf, &ret, &nchars);
		if (nchars > sizeof(localbuf))
		    nchars = sizeof(localbuf);
		memcpy(localbuf, ret, nchars);
		bufchain_consume(&term->inbuf, nchars);
		chars = localbuf;
		assert(chars != NULL);
	    }
	    c = *chars++;
	    nchars--;

	    /*
	     * Optionally log the session traffic to a file. Useful for
	     * debugging and possibly also useful for actual logging.
	     */
	    if (term->cfg.logtype == LGTYP_DEBUG && term->logctx)
		logtraffic(term->logctx, (unsigned char) c, LGTYP_DEBUG);
	} else {
	    c = unget;
	    unget = -1;
	}

	/* Note only VT220+ are 8-bit VT102 is seven bit, it shouldn't even
	 * be able to display 8-bit characters, but I'll let that go 'cause
	 * of i18n.
	 */

	/*
	 * If we're printing, add the character to the printer
	 * buffer.
	 */
	if (term->printing) {
	    bufchain_add(&term->printer_buf, &c, 1);

	    /*
	     * If we're in print-only mode, we use a much simpler
	     * state machine designed only to recognise the ESC[4i
	     * termination sequence.
	     */
	    if (term->only_printing) {
		if (c == '\033')
		    term->print_state = 1;
		else if (c == (unsigned char)'\233')
		    term->print_state = 2;
		else if (c == '[' && term->print_state == 1)
		    term->print_state = 2;
		else if (c == '4' && term->print_state == 2)
		    term->print_state = 3;
		else if (c == 'i' && term->print_state == 3)
		    term->print_state = 4;
		else
		    term->print_state = 0;
		if (term->print_state == 4) {
		    term_print_finish(term);
		}
		continue;
	    }
	}

	/* First see about all those translations. */
	if (term->termstate == TOPLEVEL) {
	    if (in_utf(term))
		switch (term->utf_state) {
		  case 0:
		    if (c < 0x80) {
			/* UTF-8 must be stateless so we ignore iso2022. */
			if (term->ucsdata->unitab_ctrl[c] != 0xFF) 
			     c = term->ucsdata->unitab_ctrl[c];
			else c = ((unsigned char)c) | CSET_ASCII;
			break;
		    } else if ((c & 0xe0) == 0xc0) {
			term->utf_size = term->utf_state = 1;
			term->utf_char = (c & 0x1f);
		    } else if ((c & 0xf0) == 0xe0) {
			term->utf_size = term->utf_state = 2;
			term->utf_char = (c & 0x0f);
		    } else if ((c & 0xf8) == 0xf0) {
			term->utf_size = term->utf_state = 3;
			term->utf_char = (c & 0x07);
		    } else if ((c & 0xfc) == 0xf8) {
			term->utf_size = term->utf_state = 4;
			term->utf_char = (c & 0x03);
		    } else if ((c & 0xfe) == 0xfc) {
			term->utf_size = term->utf_state = 5;
			term->utf_char = (c & 0x01);
		    } else {
			c = UCSERR;
			break;
		    }
		    continue;
		  case 1:
		  case 2:
		  case 3:
		  case 4:
		  case 5:
		    if ((c & 0xC0) != 0x80) {
			unget = c;
			c = UCSERR;
			term->utf_state = 0;
			break;
		    }
		    term->utf_char = (term->utf_char << 6) | (c & 0x3f);
		    if (--term->utf_state)
			continue;

		    c = term->utf_char;

		    /* Is somebody trying to be evil! */
		    if (c < 0x80 ||
			(c < 0x800 && term->utf_size >= 2) ||
			(c < 0x10000 && term->utf_size >= 3) ||
			(c < 0x200000 && term->utf_size >= 4) ||
			(c < 0x4000000 && term->utf_size >= 5))
			c = UCSERR;

		    /* Unicode line separator and paragraph separator are CR-LF */
		    if (c == 0x2028 || c == 0x2029)
			c = 0x85;

		    /* High controls are probably a Baaad idea too. */
		    if (c < 0xA0)
			c = 0xFFFD;

		    /* The UTF-16 surrogates are not nice either. */
		    /*       The standard give the option of decoding these: 
		     *       I don't want to! */
		    if (c >= 0xD800 && c < 0xE000)
			c = UCSERR;

		    /* ISO 10646 characters now limited to UTF-16 range. */
		    if (c > 0x10FFFF)
			c = UCSERR;

		    /* This is currently a TagPhobic application.. */
		    if (c >= 0xE0000 && c <= 0xE007F)
			continue;

		    /* U+FEFF is best seen as a null. */
		    if (c == 0xFEFF)
			continue;
		    /* But U+FFFE is an error. */
		    if (c == 0xFFFE || c == 0xFFFF)
			c = UCSERR;

		    break;
	    }
	    /* Are we in the nasty ACS mode? Note: no sco in utf mode. */
	    else if(term->sco_acs && 
		    (c!='\033' && c!='\012' && c!='\015' && c!='\b'))
	    {
	       if (term->sco_acs == 2) c |= 0x80;
	       c |= CSET_SCOACS;
	    } else {
		switch (term->cset_attr[term->cset]) {
		    /* 
		     * Linedraw characters are different from 'ESC ( B'
		     * only for a small range. For ones outside that
		     * range, make sure we use the same font as well as
		     * the same encoding.
		     */
		  case CSET_LINEDRW:
		    if (term->ucsdata->unitab_ctrl[c] != 0xFF)
			c = term->ucsdata->unitab_ctrl[c];
		    else
			c = ((unsigned char) c) | CSET_LINEDRW;
		    break;

		  case CSET_GBCHR:
		    /* If UK-ASCII, make the '#' a LineDraw Pound */
		    if (c == '#') {
			c = '}' | CSET_LINEDRW;
			break;
		    }
		  /*FALLTHROUGH*/ case CSET_ASCII:
		    if (term->ucsdata->unitab_ctrl[c] != 0xFF)
			c = term->ucsdata->unitab_ctrl[c];
		    else
			c = ((unsigned char) c) | CSET_ASCII;
		    break;
		case CSET_SCOACS:
		    if (c>=' ') c = ((unsigned char)c) | CSET_SCOACS;
		    break;
		}
	    }
	}

	/* How about C1 controls ? */
	if ((c & -32) == 0x80 && term->termstate < DO_CTRLS &&
	    !term->vt52_mode && has_compat(VT220)) {
	    term->termstate = SEEN_ESC;
	    term->esc_query = FALSE;
	    c = '@' + (c & 0x1F);
	}

	/* Or the GL control. */
	if (c == '\177' && term->termstate < DO_CTRLS && has_compat(OTHER)) {
	    if (term->curs.x && !term->wrapnext)
		term->curs.x--;
	    term->wrapnext = FALSE;
	    /* destructive backspace might be disabled */
	    if (!term->cfg.no_dbackspace) {
		check_boundary(term, term->curs.x, term->curs.y);
		check_boundary(term, term->curs.x+1, term->curs.y);
		copy_termchar(scrlineptr(term->curs.y),
			      term->curs.x, &term->erase_char);
	    }
	} else
	    /* Or normal C0 controls. */
	if ((c & ~0x1F) == 0 && term->termstate < DO_CTRLS) {
	    switch (c) {
	      case '\005':	       /* ENQ: terminal type query */
		/* Strictly speaking this is VT100 but a VT100 defaults to
		 * no response. Other terminals respond at their option.
		 *
		 * Don't put a CR in the default string as this tends to
		 * upset some weird software.
		 *
		 * An xterm returns "xterm" (5 characters)
		 */
		compatibility(ANSIMIN);
		if (term->ldisc) {
		    char abuf[256], *s, *d;
		    int state = 0;
		    for (s = term->cfg.answerback, d = abuf; *s; s++) {
			if (state) {
			    if (*s >= 'a' && *s <= 'z')
				*d++ = (*s - ('a' - 1));
			    else if ((*s >= '@' && *s <= '_') ||
				     *s == '?' || (*s & 0x80))
				*d++ = ('@' ^ *s);
			    else if (*s == '~')
				*d++ = '^';
			    state = 0;
			} else if (*s == '^') {
			    state = 1;
			} else
			    *d++ = *s;
		    }
		    lpage_send(term->ldisc, DEFAULT_CODEPAGE,
			       abuf, d - abuf, 0);
		}
		break;
	      case '\007':	      /* BEL: Bell */
		{
		    struct beeptime *newbeep;
		    unsigned long ticks;

		    ticks = GETTICKCOUNT();

		    if (!term->beep_overloaded) {
			newbeep = snew(struct beeptime);
			newbeep->ticks = ticks;
			newbeep->next = NULL;
			if (!term->beephead)
			    term->beephead = newbeep;
			else
			    term->beeptail->next = newbeep;
			term->beeptail = newbeep;
			term->nbeeps++;
		    }

		    /*
		     * Throw out any beeps that happened more than
		     * t seconds ago.
		     */
		    while (term->beephead &&
			   term->beephead->ticks < ticks - term->cfg.bellovl_t) {
			struct beeptime *tmp = term->beephead;
			term->beephead = tmp->next;
			sfree(tmp);
			if (!term->beephead)
			    term->beeptail = NULL;
			term->nbeeps--;
		    }

		    if (term->cfg.bellovl && term->beep_overloaded &&
			ticks - term->lastbeep >= (unsigned)term->cfg.bellovl_s) {
			/*
			 * If we're currently overloaded and the
			 * last beep was more than s seconds ago,
			 * leave overload mode.
			 */
			term->beep_overloaded = FALSE;
		    } else if (term->cfg.bellovl && !term->beep_overloaded &&
			       term->nbeeps >= term->cfg.bellovl_n) {
			/*
			 * Now, if we have n or more beeps
			 * remaining in the queue, go into overload
			 * mode.
			 */
			term->beep_overloaded = TRUE;
		    }
		    term->lastbeep = ticks;

		    /*
		     * Perform an actual beep if we're not overloaded.
		     */
		    if (!term->cfg.bellovl || !term->beep_overloaded) {
			beep(term->frontend, term->cfg.beep);
			if (term->cfg.beep == BELL_VISUAL) {
			    term->in_vbell = TRUE;
			    term->vbell_startpoint = ticks;
			    term_update(term);
			}
		    }
		    term->seen_disp_event = TRUE;
		}
		break;
	      case '\b':	      /* BS: Back space */
		if (term->curs.x == 0 &&
		    (term->curs.y == 0 || term->wrap == 0))
		    /* do nothing */ ;
		else if (term->curs.x == 0 && term->curs.y > 0)
		    term->curs.x = term->cols - 1, term->curs.y--;
		else if (term->wrapnext)
		    term->wrapnext = FALSE;
		else
		    term->curs.x--;
		term->seen_disp_event = TRUE;
		break;
	      case '\016':	      /* LS1: Locking-shift one */
		compatibility(VT100);
		term->cset = 1;
		break;
	      case '\017':	      /* LS0: Locking-shift zero */
		compatibility(VT100);
		term->cset = 0;
		break;
	      case '\033':	      /* ESC: Escape */
		if (term->vt52_mode)
		    term->termstate = VT52_ESC;
		else {
		    compatibility(ANSIMIN);
		    term->termstate = SEEN_ESC;
		    term->esc_query = FALSE;
		}
		break;
	      case '\015':	      /* CR: Carriage return */
		term->curs.x = 0;
		term->wrapnext = FALSE;
		term->seen_disp_event = TRUE;
		term->paste_hold = 0;
		if (term->logctx)
		    logtraffic(term->logctx, (unsigned char) c, LGTYP_ASCII);
		break;
	      case '\014':	      /* FF: Form feed */
		if (has_compat(SCOANSI)) {
		    move(term, 0, 0, 0);
		    erase_lots(term, FALSE, FALSE, TRUE);
		    term->disptop = 0;
		    term->wrapnext = FALSE;
		    term->seen_disp_event = 1;
		    break;
		}
	      case '\013':	      /* VT: Line tabulation */
		compatibility(VT100);
	      case '\012':	      /* LF: Line feed */
		if (term->curs.y == term->marg_b)
		    scroll(term, term->marg_t, term->marg_b, 1, TRUE);
		else if (term->curs.y < term->rows - 1)
		    term->curs.y++;
		if (term->cfg.lfhascr)
		    term->curs.x = 0;
		term->wrapnext = FALSE;
		term->seen_disp_event = 1;
		term->paste_hold = 0;
		if (term->logctx)
		    logtraffic(term->logctx, (unsigned char) c, LGTYP_ASCII);
		break;
	      case '\t':	      /* HT: Character tabulation */
		{
		    pos old_curs = term->curs;
		    termline *ldata = scrlineptr(term->curs.y);

		    do {
			term->curs.x++;
		    } while (term->curs.x < term->cols - 1 &&
			     !term->tabs[term->curs.x]);

		    if ((ldata->lattr & LATTR_MODE) != LATTR_NORM) {
			if (term->curs.x >= term->cols / 2)
			    term->curs.x = term->cols / 2 - 1;
		    } else {
			if (term->curs.x >= term->cols)
			    term->curs.x = term->cols - 1;
		    }

		    check_selection(term, old_curs, term->curs);
		}
		term->seen_disp_event = TRUE;
		break;
	    }
	} else
	    switch (term->termstate) {
	      case TOPLEVEL:
		/* Only graphic characters get this far;
		 * ctrls are stripped above */
		{
		    termline *cline = scrlineptr(term->curs.y);
		    int width = 0;
		    if (DIRECT_CHAR(c))
			width = 1;
		    if (!width)
			width = wcwidth((wchar_t) c);

		    if (term->wrapnext && term->wrap && width > 0) {
			cline->lattr |= LATTR_WRAPPED;
			if (term->curs.y == term->marg_b)
			    scroll(term, term->marg_t, term->marg_b, 1, TRUE);
			else if (term->curs.y < term->rows - 1)
			    term->curs.y++;
			term->curs.x = 0;
			term->wrapnext = FALSE;
			cline = scrlineptr(term->curs.y);
		    }
		    if (term->insert && width > 0)
			insch(term, width);
		    if (term->selstate != NO_SELECTION) {
			pos cursplus = term->curs;
			incpos(cursplus);
			check_selection(term, term->curs, cursplus);
		    }
		    if (((c & CSET_MASK) == CSET_ASCII ||
			 (c & CSET_MASK) == 0) &&
			term->logctx)
			logtraffic(term->logctx, (unsigned char) c,
				   LGTYP_ASCII);

		    switch (width) {
		      case 2:
			/*
			 * If we're about to display a double-width
			 * character starting in the rightmost
			 * column, then we do something special
			 * instead. We must print a space in the
			 * last column of the screen, then wrap;
			 * and we also set LATTR_WRAPPED2 which
			 * instructs subsequent cut-and-pasting not
			 * only to splice this line to the one
			 * after it, but to ignore the space in the
			 * last character position as well.
			 * (Because what was actually output to the
			 * terminal was presumably just a sequence
			 * of CJK characters, and we don't want a
			 * space to be pasted in the middle of
			 * those just because they had the
			 * misfortune to start in the wrong parity
			 * column. xterm concurs.)
			 */
			check_boundary(term, term->curs.x, term->curs.y);
			check_boundary(term, term->curs.x+2, term->curs.y);
			if (term->curs.x == term->cols-1) {
			    copy_termchar(cline, term->curs.x,
					  &term->erase_char);
			    cline->lattr |= LATTR_WRAPPED | LATTR_WRAPPED2;
			    if (term->curs.y == term->marg_b)
				scroll(term, term->marg_t, term->marg_b,
				       1, TRUE);
			    else if (term->curs.y < term->rows - 1)
				term->curs.y++;
			    term->curs.x = 0;
			    /* Now we must check_boundary again, of course. */
			    check_boundary(term, term->curs.x, term->curs.y);
			    check_boundary(term, term->curs.x+2, term->curs.y);
			}

			/* FULL-TERMCHAR */
			clear_cc(cline, term->curs.x);
			cline->chars[term->curs.x].chr = c;
			cline->chars[term->curs.x].attr = term->curr_attr;

			term->curs.x++;

			/* FULL-TERMCHAR */
			clear_cc(cline, term->curs.x);
			cline->chars[term->curs.x].chr = UCSWIDE;
			cline->chars[term->curs.x].attr = term->curr_attr;

			break;
		      case 1:
			check_boundary(term, term->curs.x, term->curs.y);
			check_boundary(term, term->curs.x+1, term->curs.y);

			/* FULL-TERMCHAR */
			clear_cc(cline, term->curs.x);
			cline->chars[term->curs.x].chr = c;
			cline->chars[term->curs.x].attr = term->curr_attr;

			break;
		      case 0:
			if (term->curs.x > 0) {
			    int x = term->curs.x - 1;

			    /* If we're in wrapnext state, the character
			     * to combine with is _here_, not to our left. */
			    if (term->wrapnext)
				x++;

			    /*
			     * If the previous character is
			     * UCSWIDE, back up another one.
			     */
			    if (cline->chars[x].chr == UCSWIDE) {
				assert(x > 0);
				x--;
			    }

			    add_cc(cline, x, c);
			    term->seen_disp_event = 1;
			}
			continue;
		      default:
			continue;
		    }
		    term->curs.x++;
		    if (term->curs.x == term->cols) {
			term->curs.x--;
			term->wrapnext = TRUE;
			if (term->wrap && term->vt52_mode) {
			    cline->lattr |= LATTR_WRAPPED;
			    if (term->curs.y == term->marg_b)
				scroll(term, term->marg_t, term->marg_b, 1, TRUE);
			    else if (term->curs.y < term->rows - 1)
				term->curs.y++;
			    term->curs.x = 0;
			    term->wrapnext = FALSE;
			}
		    }
		    term->seen_disp_event = 1;
		}
		break;

	      case OSC_MAYBE_ST:
		/*
		 * This state is virtually identical to SEEN_ESC, with the
		 * exception that we have an OSC sequence in the pipeline,
		 * and _if_ we see a backslash, we process it.
		 */
		if (c == '\\') {
		    do_osc(term);
		    term->termstate = TOPLEVEL;
		    break;
		}
		/* else fall through */
	      case SEEN_ESC:
		if (c >= ' ' && c <= '/') {
		    if (term->esc_query)
			term->esc_query = -1;
		    else
			term->esc_query = c;
		    break;
		}
		term->termstate = TOPLEVEL;
		switch (ANSI(c, term->esc_query)) {
		  case '[':		/* enter CSI mode */
		    term->termstate = SEEN_CSI;
		    term->esc_nargs = 1;
		    term->esc_args[0] = ARG_DEFAULT;
		    term->esc_query = FALSE;
		    break;
		  case ']':		/* OSC: xterm escape sequences */
		    /* Compatibility is nasty here, xterm, linux, decterm yuk! */
		    compatibility(OTHER);
		    term->termstate = SEEN_OSC;
		    term->esc_args[0] = 0;
		    break;
		  case '7':		/* DECSC: save cursor */
		    compatibility(VT100);
		    save_cursor(term, TRUE);
		    break;
		  case '8':	 	/* DECRC: restore cursor */
		    compatibility(VT100);
		    save_cursor(term, FALSE);
		    term->seen_disp_event = TRUE;
		    break;
		  case '=':		/* DECKPAM: Keypad application mode */
		    compatibility(VT100);
		    term->app_keypad_keys = TRUE;
		    break;
		  case '>':		/* DECKPNM: Keypad numeric mode */
		    compatibility(VT100);
		    term->app_keypad_keys = FALSE;
		    break;
		  case 'D':	       /* IND: exactly equivalent to LF */
		    compatibility(VT100);
		    if (term->curs.y == term->marg_b)
			scroll(term, term->marg_t, term->marg_b, 1, TRUE);
		    else if (term->curs.y < term->rows - 1)
			term->curs.y++;
		    term->wrapnext = FALSE;
		    term->seen_disp_event = TRUE;
		    break;
		  case 'E':	       /* NEL: exactly equivalent to CR-LF */
		    compatibility(VT100);
		    term->curs.x = 0;
		    if (term->curs.y == term->marg_b)
			scroll(term, term->marg_t, term->marg_b, 1, TRUE);
		    else if (term->curs.y < term->rows - 1)
			term->curs.y++;
		    term->wrapnext = FALSE;
		    term->seen_disp_event = TRUE;
		    break;
		  case 'M':	       /* RI: reverse index - backwards LF */
		    compatibility(VT100);
		    if (term->curs.y == term->marg_t)
			scroll(term, term->marg_t, term->marg_b, -1, TRUE);
		    else if (term->curs.y > 0)
			term->curs.y--;
		    term->wrapnext = FALSE;
		    term->seen_disp_event = TRUE;
		    break;
		  case 'Z':	       /* DECID: terminal type query */
		    compatibility(VT100);
		    if (term->ldisc)
			ldisc_send(term->ldisc, term->id_string,
				   strlen(term->id_string), 0);
		    break;
		  case 'c':	       /* RIS: restore power-on settings */
		    compatibility(VT100);
		    power_on(term);
		    if (term->ldisc)   /* cause ldisc to notice changes */
			ldisc_send(term->ldisc, NULL, 0, 0);
		    if (term->reset_132) {
			if (!term->cfg.no_remote_resize)
			    request_resize(term->frontend, 80, term->rows);
			term->reset_132 = 0;
		    }
		    term->disptop = 0;
		    term->seen_disp_event = TRUE;
		    break;
		  case 'H':	       /* HTS: set a tab */
		    compatibility(VT100);
		    term->tabs[term->curs.x] = TRUE;
		    break;

		  case ANSI('8', '#'):	/* DECALN: fills screen with Es :-) */
		    compatibility(VT100);
		    {
			termline *ldata;
			int i, j;
			pos scrtop, scrbot;

			for (i = 0; i < term->rows; i++) {
			    ldata = scrlineptr(i);
			    for (j = 0; j < term->cols; j++) {
				copy_termchar(ldata, j,
					      &term->basic_erase_char);
				ldata->chars[j].chr = 'E';
			    }
			    ldata->lattr = LATTR_NORM;
			}
			term->disptop = 0;
			term->seen_disp_event = TRUE;
			scrtop.x = scrtop.y = 0;
			scrbot.x = 0;
			scrbot.y = term->rows;
			check_selection(term, scrtop, scrbot);
		    }
		    break;

		  case ANSI('3', '#'):
		  case ANSI('4', '#'):
		  case ANSI('5', '#'):
		  case ANSI('6', '#'):
		    compatibility(VT100);
		    {
			int nlattr;

			switch (ANSI(c, term->esc_query)) {
			  case ANSI('3', '#'): /* DECDHL: 2*height, top */
			    nlattr = LATTR_TOP;
			    break;
			  case ANSI('4', '#'): /* DECDHL: 2*height, bottom */
			    nlattr = LATTR_BOT;
			    break;
			  case ANSI('5', '#'): /* DECSWL: normal */
			    nlattr = LATTR_NORM;
			    break;
			  default: /* case ANSI('6', '#'): DECDWL: 2*width */
			    nlattr = LATTR_WIDE;
			    break;
			}
			scrlineptr(term->curs.y)->lattr = nlattr;
		    }
		    break;
		  /* GZD4: G0 designate 94-set */
		  case ANSI('A', '('):
		    compatibility(VT100);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[0] = CSET_GBCHR;
		    break;
		  case ANSI('B', '('):
		    compatibility(VT100);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[0] = CSET_ASCII;
		    break;
		  case ANSI('0', '('):
		    compatibility(VT100);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[0] = CSET_LINEDRW;
		    break;
		  case ANSI('U', '('): 
		    compatibility(OTHER);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[0] = CSET_SCOACS; 
		    break;
		  /* G1D4: G1-designate 94-set */
		  case ANSI('A', ')'):
		    compatibility(VT100);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[1] = CSET_GBCHR;
		    break;
		  case ANSI('B', ')'):
		    compatibility(VT100);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[1] = CSET_ASCII;
		    break;
		  case ANSI('0', ')'):
		    compatibility(VT100);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[1] = CSET_LINEDRW;
		    break;
		  case ANSI('U', ')'): 
		    compatibility(OTHER);
		    if (!term->cfg.no_remote_charset)
			term->cset_attr[1] = CSET_SCOACS; 
		    break;
		  /* DOCS: Designate other coding system */
		  case ANSI('8', '%'):	/* Old Linux code */
		  case ANSI('G', '%'):
		    compatibility(OTHER);
		    if (!term->cfg.no_remote_charset)
			term->utf = 1;
		    break;
		  case ANSI('@', '%'):
		    compatibility(OTHER);
		    if (!term->cfg.no_remote_charset)
			term->utf = 0;
		    break;
		}
		break;
	      case SEEN_CSI:
		term->termstate = TOPLEVEL;  /* default */
		if (isdigit(c)) {
		    if (term->esc_nargs <= ARGS_MAX) {
			if (term->esc_args[term->esc_nargs - 1] == ARG_DEFAULT)
			    term->esc_args[term->esc_nargs - 1] = 0;
			term->esc_args[term->esc_nargs - 1] =
			    10 * term->esc_args[term->esc_nargs - 1] + c - '0';
		    }
		    term->termstate = SEEN_CSI;
		} else if (c == ';') {
		    if (++term->esc_nargs <= ARGS_MAX)
			term->esc_args[term->esc_nargs - 1] = ARG_DEFAULT;
		    term->termstate = SEEN_CSI;
		} else if (c < '@') {
		    if (term->esc_query)
			term->esc_query = -1;
		    else if (c == '?')
			term->esc_query = TRUE;
		    else
			term->esc_query = c;
		    term->termstate = SEEN_CSI;
		} else
		    switch (ANSI(c, term->esc_query)) {
		      case 'A':       /* CUU: move up N lines */
			move(term, term->curs.x,
			     term->curs.y - def(term->esc_args[0], 1), 1);
			term->seen_disp_event = TRUE;
			break;
		      case 'e':		/* VPR: move down N lines */
			compatibility(ANSI);
			/* FALLTHROUGH */
		      case 'B':		/* CUD: Cursor down */
			move(term, term->curs.x,
			     term->curs.y + def(term->esc_args[0], 1), 1);
			term->seen_disp_event = TRUE;
			break;
		      case ANSI('c', '>'):	/* DA: report xterm version */
			compatibility(OTHER);
			/* this reports xterm version 136 so that VIM can
			   use the drag messages from the mouse reporting */
			if (term->ldisc)
			    ldisc_send(term->ldisc, "\033[>0;136;0c", 11, 0);
			break;
		      case 'a':		/* HPR: move right N cols */
			compatibility(ANSI);
			/* FALLTHROUGH */
		      case 'C':		/* CUF: Cursor right */ 
			move(term, term->curs.x + def(term->esc_args[0], 1),
			     term->curs.y, 1);
			term->seen_disp_event = TRUE;
			break;
		      case 'D':       /* CUB: move left N cols */
			move(term, term->curs.x - def(term->esc_args[0], 1),
			     term->curs.y, 1);
			term->seen_disp_event = TRUE;
			break;
		      case 'E':       /* CNL: move down N lines and CR */
			compatibility(ANSI);
			move(term, 0,
			     term->curs.y + def(term->esc_args[0], 1), 1);
			term->seen_disp_event = TRUE;
			break;
		      case 'F':       /* CPL: move up N lines and CR */
			compatibility(ANSI);
			move(term, 0,
			     term->curs.y - def(term->esc_args[0], 1), 1);
			term->seen_disp_event = TRUE;
			break;
		      case 'G':	      /* CHA */
		      case '`':       /* HPA: set horizontal posn */
			compatibility(ANSI);
			move(term, def(term->esc_args[0], 1) - 1,
			     term->curs.y, 0);
			term->seen_disp_event = TRUE;
			break;
		      case 'd':       /* VPA: set vertical posn */
			compatibility(ANSI);
			move(term, term->curs.x,
			     ((term->dec_om ? term->marg_t : 0) +
			      def(term->esc_args[0], 1) - 1),
			     (term->dec_om ? 2 : 0));
			term->seen_disp_event = TRUE;
			break;
		      case 'H':	     /* CUP */
		      case 'f':      /* HVP: set horz and vert posns at once */
			if (term->esc_nargs < 2)
			    term->esc_args[1] = ARG_DEFAULT;
			move(term, def(term->esc_args[1], 1) - 1,
			     ((term->dec_om ? term->marg_t : 0) +
			      def(term->esc_args[0], 1) - 1),
			     (term->dec_om ? 2 : 0));
			term->seen_disp_event = TRUE;
			break;
		      case 'J':       /* ED: erase screen or parts of it */
			{
			    unsigned int i = def(term->esc_args[0], 0) + 1;
			    if (i > 3)
				i = 0;
			    erase_lots(term, FALSE, !!(i & 2), !!(i & 1));
			}
			term->disptop = 0;
			term->seen_disp_event = TRUE;
			break;
		      case 'K':       /* EL: erase line or parts of it */
			{
			    unsigned int i = def(term->esc_args[0], 0) + 1;
			    if (i > 3)
				i = 0;
			    erase_lots(term, TRUE, !!(i & 2), !!(i & 1));
			}
			term->seen_disp_event = TRUE;
			break;
		      case 'L':       /* IL: insert lines */
			compatibility(VT102);
			if (term->curs.y <= term->marg_b)
			    scroll(term, term->curs.y, term->marg_b,
				   -def(term->esc_args[0], 1), FALSE);
			term->seen_disp_event = TRUE;
			break;
		      case 'M':       /* DL: delete lines */
			compatibility(VT102);
			if (term->curs.y <= term->marg_b)
			    scroll(term, term->curs.y, term->marg_b,
				   def(term->esc_args[0], 1),
				   TRUE);
			term->seen_disp_event = TRUE;
			break;
		      case '@':       /* ICH: insert chars */
			/* XXX VTTEST says this is vt220, vt510 manual says vt102 */
			compatibility(VT102);
			insch(term, def(term->esc_args[0], 1));
			term->seen_disp_event = TRUE;
			break;
		      case 'P':       /* DCH: delete chars */
			compatibility(VT102);
			insch(term, -def(term->esc_args[0], 1));
			term->seen_disp_event = TRUE;
			break;
		      case 'c':       /* DA: terminal type query */
			compatibility(VT100);
			/* This is the response for a VT102 */
			if (term->ldisc)
			    ldisc_send(term->ldisc, term->id_string,
 				       strlen(term->id_string), 0);
			break;
		      case 'n':       /* DSR: cursor position query */
			if (term->ldisc) {
			    if (term->esc_args[0] == 6) {
				char buf[32];
				sprintf(buf, "\033[%d;%dR", term->curs.y + 1,
					term->curs.x + 1);
				ldisc_send(term->ldisc, buf, strlen(buf), 0);
			    } else if (term->esc_args[0] == 5) {
				ldisc_send(term->ldisc, "\033[0n", 4, 0);
			    }
			}
			break;
		      case 'h':       /* SM: toggle modes to high */
		      case ANSI_QUE('h'):
			compatibility(VT100);
			{
			    int i;
			    for (i = 0; i < term->esc_nargs; i++)
				toggle_mode(term, term->esc_args[i],
					    term->esc_query, TRUE);
			}
			break;
		      case 'i':		/* MC: Media copy */
		      case ANSI_QUE('i'):
			compatibility(VT100);
			{
			    if (term->esc_nargs != 1) break;
			    if (term->esc_args[0] == 5 && *term->cfg.printer) {
				term->printing = TRUE;
				term->only_printing = !term->esc_query;
				term->print_state = 0;
				term_print_setup(term);
			    } else if (term->esc_args[0] == 4 &&
				       term->printing) {
				term_print_finish(term);
			    }
			}
			break;			
		      case 'l':       /* RM: toggle modes to low */
		      case ANSI_QUE('l'):
			compatibility(VT100);
			{
			    int i;
			    for (i = 0; i < term->esc_nargs; i++)
				toggle_mode(term, term->esc_args[i],
					    term->esc_query, FALSE);
			}
			break;
		      case 'g':       /* TBC: clear tabs */
			compatibility(VT100);
			if (term->esc_nargs == 1) {
			    if (term->esc_args[0] == 0) {
				term->tabs[term->curs.x] = FALSE;
			    } else if (term->esc_args[0] == 3) {
				int i;
				for (i = 0; i < term->cols; i++)
				    term->tabs[i] = FALSE;
			    }
			}
			break;
		      case 'r':       /* DECSTBM: set scroll margins */
			compatibility(VT100);
			if (term->esc_nargs <= 2) {
			    int top, bot;
			    top = def(term->esc_args[0], 1) - 1;
			    bot = (term->esc_nargs <= 1
				   || term->esc_args[1] == 0 ?
				   term->rows :
				   def(term->esc_args[1], term->rows)) - 1;
			    if (bot >= term->rows)
				bot = term->rows - 1;
			    /* VTTEST Bug 9 - if region is less than 2 lines
			     * don't change region.
			     */
			    if (bot - top > 0) {
				term->marg_t = top;
				term->marg_b = bot;
				term->curs.x = 0;
				/*
				 * I used to think the cursor should be
				 * placed at the top of the newly marginned
				 * area. Apparently not: VMS TPU falls over
				 * if so.
				 *
				 * Well actually it should for
				 * Origin mode - RDB
				 */
				term->curs.y = (term->dec_om ?
						term->marg_t : 0);
				term->seen_disp_event = TRUE;
			    }
			}
			break;
		      case 'm':       /* SGR: set graphics rendition */
			{
			    /* 
			     * A VT100 without the AVO only had one
			     * attribute, either underline or
			     * reverse video depending on the
			     * cursor type, this was selected by
			     * CSI 7m.
			     *
			     * case 2:
			     *  This is sometimes DIM, eg on the
			     *  GIGI and Linux
			     * case 8:
			     *  This is sometimes INVIS various ANSI.
			     * case 21:
			     *  This like 22 disables BOLD, DIM and INVIS
			     *
			     * The ANSI colours appear on any
			     * terminal that has colour (obviously)
			     * but the interaction between sgr0 and
			     * the colours varies but is usually
			     * related to the background colour
			     * erase item. The interaction between
			     * colour attributes and the mono ones
			     * is also very implementation
			     * dependent.
			     *
			     * The 39 and 49 attributes are likely
			     * to be unimplemented.
			     */
			    int i;
			    for (i = 0; i < term->esc_nargs; i++) {
				switch (def(term->esc_args[i], 0)) {
				  case 0:	/* restore defaults */
				    term->curr_attr = term->default_attr;
				    break;
				  case 1:	/* enable bold */
				    compatibility(VT100AVO);
				    term->curr_attr |= ATTR_BOLD;
				    break;
				  case 21:	/* (enable double underline) */
				    compatibility(OTHER);
				  case 4:	/* enable underline */
				    compatibility(VT100AVO);
				    term->curr_attr |= ATTR_UNDER;
				    break;
				  case 5:	/* enable blink */
				    compatibility(VT100AVO);
				    term->curr_attr |= ATTR_BLINK;
				    break;
				  case 6:	/* SCO light bkgrd */
				    compatibility(SCOANSI);
				    term->blink_is_real = FALSE;
				    term->curr_attr |= ATTR_BLINK;
				    break;
				  case 7:	/* enable reverse video */
				    term->curr_attr |= ATTR_REVERSE;
				    break;
				  case 10:      /* SCO acs off */
				    compatibility(SCOANSI);
				    if (term->cfg.no_remote_charset) break;
				    term->sco_acs = 0; break;
				  case 11:      /* SCO acs on */
				    compatibility(SCOANSI);
				    if (term->cfg.no_remote_charset) break;
				    term->sco_acs = 1; break;
				  case 12:      /* SCO acs on, |0x80 */
				    compatibility(SCOANSI);
				    if (term->cfg.no_remote_charset) break;
				    term->sco_acs = 2; break;
				  case 22:	/* disable bold */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_BOLD;
				    break;
				  case 24:	/* disable underline */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_UNDER;
				    break;
				  case 25:	/* disable blink */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_BLINK;
				    break;
				  case 27:	/* disable reverse video */
				    compatibility2(OTHER, VT220);
				    term->curr_attr &= ~ATTR_REVERSE;
				    break;
				  case 30:
				  case 31:
				  case 32:
				  case 33:
				  case 34:
				  case 35:
				  case 36:
				  case 37:
				    /* foreground */
				    term->curr_attr &= ~ATTR_FGMASK;
				    term->curr_attr |=
					(term->esc_args[i] - 30)<<ATTR_FGSHIFT;
				    break;
				  case 90:
				  case 91:
				  case 92:
				  case 93:
				  case 94:
				  case 95:
				  case 96:
				  case 97:
				    /* xterm-style bright foreground */
				    term->curr_attr &= ~ATTR_FGMASK;
				    term->curr_attr |=
					((term->esc_args[i] - 90 + 16)
                                         << ATTR_FGSHIFT);
				    break;
				  case 39:	/* default-foreground */
				    term->curr_attr &= ~ATTR_FGMASK;
				    term->curr_attr |= ATTR_DEFFG;
				    break;
				  case 40:
				  case 41:
				  case 42:
				  case 43:
				  case 44:
				  case 45:
				  case 46:
				  case 47:
				    /* background */
				    term->curr_attr &= ~ATTR_BGMASK;
				    term->curr_attr |=
					(term->esc_args[i] - 40)<<ATTR_BGSHIFT;
				    break;
				  case 100:
				  case 101:
				  case 102:
				  case 103:
				  case 104:
				  case 105:
				  case 106:
				  case 107:
				    /* xterm-style bright background */
				    term->curr_attr &= ~ATTR_BGMASK;
				    term->curr_attr |=
					((term->esc_args[i] - 100 + 16)
                                         << ATTR_BGSHIFT);
				    break;
				  case 49:	/* default-background */
				    term->curr_attr &= ~ATTR_BGMASK;
				    term->curr_attr |= ATTR_DEFBG;
				    break;
				}
			    }
			    set_erase_char(term);
			}
			break;
		      case 's':       /* save cursor */
			save_cursor(term, TRUE);
			break;
		      case 'u':       /* restore cursor */
			save_cursor(term, FALSE);
			term->seen_disp_event = TRUE;
			break;
		      case 't': /* DECSLPP: set page size - ie window height */
			/*
			 * VT340/VT420 sequence DECSLPP, DEC only allows values
			 *  24/25/36/48/72/144 other emulators (eg dtterm) use
			 * illegal values (eg first arg 1..9) for window changing 
			 * and reports.
			 */
			if (term->esc_nargs <= 1
			    && (term->esc_args[0] < 1 ||
				term->esc_args[0] >= 24)) {
			    compatibility(VT340TEXT);
			    if (!term->cfg.no_remote_resize)
				request_resize(term->frontend, term->cols,
					       def(term->esc_args[0], 24));
			    deselect(term);
			} else if (term->esc_nargs >= 1 &&
				   term->esc_args[0] >= 1 &&
				   term->esc_args[0] < 24) {
			    compatibility(OTHER);

			    switch (term->esc_args[0]) {
				int x, y, len;
				char buf[80], *p;
			      case 1:
				set_iconic(term->frontend, FALSE);
				break;
			      case 2:
				set_iconic(term->frontend, TRUE);
				break;
			      case 3:
				if (term->esc_nargs >= 3) {
				    if (!term->cfg.no_remote_resize)
					move_window(term->frontend,
						    def(term->esc_args[1], 0),
						    def(term->esc_args[2], 0));
				}
				break;
			      case 4:
				/* We should resize the window to a given
				 * size in pixels here, but currently our
				 * resizing code isn't healthy enough to
				 * manage it. */
				break;
			      case 5:
				/* move to top */
				set_zorder(term->frontend, TRUE);
				break;
			      case 6:
				/* move to bottom */
				set_zorder(term->frontend, FALSE);
				break;
			      case 7:
				refresh_window(term->frontend);
				break;
			      case 8:
				if (term->esc_nargs >= 3) {
				    if (!term->cfg.no_remote_resize)
					request_resize(term->frontend,
						       def(term->esc_args[2], term->cfg.width),
						       def(term->esc_args[1], term->cfg.height));
				}
				break;
			      case 9:
				if (term->esc_nargs >= 2)
				    set_zoomed(term->frontend,
					       term->esc_args[1] ?
					       TRUE : FALSE);
				break;
			      case 11:
				if (term->ldisc)
				    ldisc_send(term->ldisc,
					       is_iconic(term->frontend) ?
					       "\033[1t" : "\033[2t", 4, 0);
				break;
			      case 13:
				if (term->ldisc) {
				    get_window_pos(term->frontend, &x, &y);
				    len = sprintf(buf, "\033[3;%d;%dt", x, y);
				    ldisc_send(term->ldisc, buf, len, 0);
				}
				break;
			      case 14:
				if (term->ldisc) {
				    get_window_pixels(term->frontend, &x, &y);
				    len = sprintf(buf, "\033[4;%d;%dt", x, y);
				    ldisc_send(term->ldisc, buf, len, 0);
				}
				break;
			      case 18:
				if (term->ldisc) {
				    len = sprintf(buf, "\033[8;%d;%dt",
						  term->rows, term->cols);
				    ldisc_send(term->ldisc, buf, len, 0);
				}
				break;
			      case 19:
				/*
				 * Hmmm. Strictly speaking we
				 * should return `the size of the
				 * screen in characters', but
				 * that's not easy: (a) window
				 * furniture being what it is it's
				 * hard to compute, and (b) in
				 * resize-font mode maximising the
				 * window wouldn't change the
				 * number of characters. *shrug*. I
				 * think we'll ignore it for the
				 * moment and see if anyone
				 * complains, and then ask them
				 * what they would like it to do.
				 */
				break;
			      case 20:
				if (term->ldisc &&
				    !term->cfg.no_remote_qtitle) {
				    p = get_window_title(term->frontend, TRUE);
				    len = strlen(p);
				    ldisc_send(term->ldisc, "\033]L", 3, 0);
				    ldisc_send(term->ldisc, p, len, 0);
				    ldisc_send(term->ldisc, "\033\\", 2, 0);
				}
				break;
			      case 21:
				if (term->ldisc &&
				    !term->cfg.no_remote_qtitle) {
				    p = get_window_title(term->frontend,FALSE);
				    len = strlen(p);
				    ldisc_send(term->ldisc, "\033]l", 3, 0);
				    ldisc_send(term->ldisc, p, len, 0);
				    ldisc_send(term->ldisc, "\033\\", 2, 0);
				}
				break;
			    }
			}
			break;
		      case 'S':		/* SU: Scroll up */
			compatibility(SCOANSI);
			scroll(term, term->marg_t, term->marg_b,
			       def(term->esc_args[0], 1), TRUE);
			term->wrapnext = FALSE;
			term->seen_disp_event = TRUE;
			break;
		      case 'T':		/* SD: Scroll down */
			compatibility(SCOANSI);
			scroll(term, term->marg_t, term->marg_b,
			       -def(term->esc_args[0], 1), TRUE);
			term->wrapnext = FALSE;
			term->seen_disp_event = TRUE;
			break;
		      case ANSI('|', '*'): /* DECSNLS */
			/* 
			 * Set number of lines on screen
			 * VT420 uses VGA like hardware and can
			 * support any size in reasonable range
			 * (24..49 AIUI) with no default specified.
			 */
			compatibility(VT420);
			if (term->esc_nargs == 1 && term->esc_args[0] > 0) {
			    if (!term->cfg.no_remote_resize)
				request_resize(term->frontend, term->cols,
					       def(term->esc_args[0],
						   term->cfg.height));
			    deselect(term);
			}
			break;
		      case ANSI('|', '$'): /* DECSCPP */
			/*
			 * Set number of columns per page
			 * Docs imply range is only 80 or 132, but
			 * I'll allow any.
			 */
			compatibility(VT340TEXT);
			if (term->esc_nargs <= 1) {
			    if (!term->cfg.no_remote_resize)
				request_resize(term->frontend,
					       def(term->esc_args[0],
						   term->cfg.width), term->rows);
			    deselect(term);
			}
			break;
		      case 'X':     /* ECH: write N spaces w/o moving cursor */
			/* XXX VTTEST says this is vt220, vt510 manual
			 * says vt100 */
			compatibility(ANSIMIN);
			{
			    int n = def(term->esc_args[0], 1);
			    pos cursplus;
			    int p = term->curs.x;
			    termline *cline = scrlineptr(term->curs.y);

			    if (n > term->cols - term->curs.x)
				n = term->cols - term->curs.x;
			    cursplus = term->curs;
			    cursplus.x += n;
			    check_boundary(term, term->curs.x, term->curs.y);
			    check_boundary(term, term->curs.x+n, term->curs.y);
			    check_selection(term, term->curs, cursplus);
			    while (n--)
				copy_termchar(cline, p++,
					      &term->erase_char);
			    term->seen_disp_event = TRUE;
			}
			break;
		      case 'x':       /* DECREQTPARM: report terminal characteristics */
			compatibility(VT100);
			if (term->ldisc) {
			    char buf[32];
			    int i = def(term->esc_args[0], 0);
			    if (i == 0 || i == 1) {
				strcpy(buf, "\033[2;1;1;112;112;1;0x");
				buf[2] += i;
				ldisc_send(term->ldisc, buf, 20, 0);
			    }
			}
			break;
		      case 'Z':		/* CBT: BackTab for xterm */
			compatibility(OTHER);
			{
			    int i = def(term->esc_args[0], 1);
			    pos old_curs = term->curs;

			    for(;i>0 && term->curs.x>0; i--) {
				do {
				    term->curs.x--;
				} while (term->curs.x >0 &&
					 !term->tabs[term->curs.x]);
			    }
			    check_selection(term, old_curs, term->curs);
			}
			break;
		      case ANSI('c', '='):      /* Hide or Show Cursor */
			compatibility(SCOANSI);
			switch(term->esc_args[0]) {
			  case 0:  /* hide cursor */
			    term->cursor_on = FALSE;
			    break;
			  case 1:  /* restore cursor */
			    term->big_cursor = FALSE;
			    term->cursor_on = TRUE;
			    break;
			  case 2:  /* block cursor */
			    term->big_cursor = TRUE;
			    term->cursor_on = TRUE;
			    break;
			}
			break;
		      case ANSI('C', '='):
			/*
			 * set cursor start on scanline esc_args[0] and
			 * end on scanline esc_args[1].If you set
			 * the bottom scan line to a value less than
			 * the top scan line, the cursor will disappear.
			 */
			compatibility(SCOANSI);
			if (term->esc_nargs >= 2) {
			    if (term->esc_args[0] > term->esc_args[1])
				term->cursor_on = FALSE;
			    else
				term->cursor_on = TRUE;
			}
			break;
		      case ANSI('D', '='):
			compatibility(SCOANSI);
			term->blink_is_real = FALSE;
			if (term->esc_args[0]>=1)
			    term->curr_attr |= ATTR_BLINK;
			else
			    term->curr_attr &= ~ATTR_BLINK;
			break;
		      case ANSI('E', '='):
			compatibility(SCOANSI);
			term->blink_is_real = (term->esc_args[0] >= 1);
			break;
		      case ANSI('F', '='):      /* set normal foreground */
			compatibility(SCOANSI);
			if (term->esc_args[0] >= 0 && term->esc_args[0] < 16) {
			    long colour =
 				(sco2ansicolour[term->esc_args[0] & 0x7] |
				 ((term->esc_args[0] & 0x8) << 1)) <<
				ATTR_FGSHIFT;
			    term->curr_attr &= ~ATTR_FGMASK;
			    term->curr_attr |= colour;
			    term->default_attr &= ~ATTR_FGMASK;
			    term->default_attr |= colour;
			}
			break;
		      case ANSI('G', '='):      /* set normal background */
			compatibility(SCOANSI);
			if (term->esc_args[0] >= 0 && term->esc_args[0] < 16) {
			    long colour =
 				(sco2ansicolour[term->esc_args[0] & 0x7] |
				 ((term->esc_args[0] & 0x8) << 1)) <<
				ATTR_BGSHIFT;
			    term->curr_attr &= ~ATTR_BGMASK;
			    term->curr_attr |= colour;
			    term->default_attr &= ~ATTR_BGMASK;
			    term->default_attr |= colour;
			}
			break;
		      case ANSI('L', '='):
			compatibility(SCOANSI);
			term->use_bce = (term->esc_args[0] <= 0);
			set_erase_char(term);
			break;
		      case ANSI('p', '"'): /* DECSCL: set compat level */
			/*
			 * Allow the host to make this emulator a
			 * 'perfect' VT102. This first appeared in
			 * the VT220, but we do need to get back to
			 * PuTTY mode so I won't check it.
			 *
			 * The arg in 40..42,50 are a PuTTY extension.
			 * The 2nd arg, 8bit vs 7bit is not checked.
			 *
			 * Setting VT102 mode should also change
			 * the Fkeys to generate PF* codes as a
			 * real VT102 has no Fkeys. The VT220 does
			 * this, F11..F13 become ESC,BS,LF other
			 * Fkeys send nothing.
			 *
			 * Note ESC c will NOT change this!
			 */

			switch (term->esc_args[0]) {
			  case 61:
			    term->compatibility_level &= ~TM_VTXXX;
			    term->compatibility_level |= TM_VT102;
			    break;
			  case 62:
			    term->compatibility_level &= ~TM_VTXXX;
			    term->compatibility_level |= TM_VT220;
			    break;

			  default:
			    if (term->esc_args[0] > 60 &&
				term->esc_args[0] < 70)
				term->compatibility_level |= TM_VTXXX;
			    break;

			  case 40:
			    term->compatibility_level &= TM_VTXXX;
			    break;
			  case 41:
			    term->compatibility_level = TM_PUTTY;
			    break;
			  case 42:
			    term->compatibility_level = TM_SCOANSI;
			    break;

			  case ARG_DEFAULT:
			    term->compatibility_level = TM_PUTTY;
			    break;
			  case 50:
			    break;
			}

			/* Change the response to CSI c */
			if (term->esc_args[0] == 50) {
			    int i;
			    char lbuf[64];
			    strcpy(term->id_string, "\033[?");
			    for (i = 1; i < term->esc_nargs; i++) {
				if (i != 1)
				    strcat(term->id_string, ";");
				sprintf(lbuf, "%d", term->esc_args[i]);
				strcat(term->id_string, lbuf);
			    }
			    strcat(term->id_string, "c");
			}
#if 0
			/* Is this a good idea ? 
			 * Well we should do a soft reset at this point ...
			 */
			if (!has_compat(VT420) && has_compat(VT100)) {
			    if (!term->cfg.no_remote_resize) {
				if (term->reset_132)
				    request_resize(132, 24);
				else
				    request_resize(80, 24);
			    }
			}
#endif
			break;
		    }
		break;
	      case SEEN_OSC:
		term->osc_w = FALSE;
		switch (c) {
		  case 'P':	       /* Linux palette sequence */
		    term->termstate = SEEN_OSC_P;
		    term->osc_strlen = 0;
		    break;
		  case 'R':	       /* Linux palette reset */
		    palette_reset(term->frontend);
		    term_invalidate(term);
		    term->termstate = TOPLEVEL;
		    break;
		  case 'W':	       /* word-set */
		    term->termstate = SEEN_OSC_W;
		    term->osc_w = TRUE;
		    break;
		  case '0':
		  case '1':
		  case '2':
		  case '3':
		  case '4':
		  case '5':
		  case '6':
		  case '7':
		  case '8':
		  case '9':
		    term->esc_args[0] = 10 * term->esc_args[0] + c - '0';
		    break;
		  case 'L':
		    /*
		     * Grotty hack to support xterm and DECterm title
		     * sequences concurrently.
		     */
		    if (term->esc_args[0] == 2) {
			term->esc_args[0] = 1;
			break;
		    }
		    /* else fall through */
		  default:
		    term->termstate = OSC_STRING;
		    term->osc_strlen = 0;
		}
		break;
	      case OSC_STRING:
		/*
		 * This OSC stuff is EVIL. It takes just one character to get into
		 * sysline mode and it's not initially obvious how to get out.
		 * So I've added CR and LF as string aborts.
		 * This shouldn't effect compatibility as I believe embedded 
		 * control characters are supposed to be interpreted (maybe?) 
		 * and they don't display anything useful anyway.
		 *
		 * -- RDB
		 */
		if (c == '\012' || c == '\015') {
		    term->termstate = TOPLEVEL;
		} else if (c == 0234 || c == '\007') {
		    /*
		     * These characters terminate the string; ST and BEL
		     * terminate the sequence and trigger instant
		     * processing of it, whereas ESC goes back to SEEN_ESC
		     * mode unless it is followed by \, in which case it is
		     * synonymous with ST in the first place.
		     */
		    do_osc(term);
		    term->termstate = TOPLEVEL;
		} else if (c == '\033')
		    term->termstate = OSC_MAYBE_ST;
		else if (term->osc_strlen < OSC_STR_MAX)
		    term->osc_string[term->osc_strlen++] = (char)c;
		break;
	      case SEEN_OSC_P:
		{
		    int max = (term->osc_strlen == 0 ? 21 : 16);
		    int val;
		    if ((int)c >= '0' && (int)c <= '9')
			val = c - '0';
		    else if ((int)c >= 'A' && (int)c <= 'A' + max - 10)
			val = c - 'A' + 10;
		    else if ((int)c >= 'a' && (int)c <= 'a' + max - 10)
			val = c - 'a' + 10;
		    else {
			term->termstate = TOPLEVEL;
			break;
		    }
		    term->osc_string[term->osc_strlen++] = val;
		    if (term->osc_strlen >= 7) {
			palette_set(term->frontend, term->osc_string[0],
				    term->osc_string[1] * 16 + term->osc_string[2],
				    term->osc_string[3] * 16 + term->osc_string[4],
				    term->osc_string[5] * 16 + term->osc_string[6]);
			term_invalidate(term);
			term->termstate = TOPLEVEL;
		    }
		}
		break;
	      case SEEN_OSC_W:
		switch (c) {
		  case '0':
		  case '1':
		  case '2':
		  case '3':
		  case '4':
		  case '5':
		  case '6':
		  case '7':
		  case '8':
		  case '9':
		    term->esc_args[0] = 10 * term->esc_args[0] + c - '0';
		    break;
		  default:
		    term->termstate = OSC_STRING;
		    term->osc_strlen = 0;
		}
		break;
	      case VT52_ESC:
		term->termstate = TOPLEVEL;
		term->seen_disp_event = TRUE;
		switch (c) {
		  case 'A':
		    move(term, term->curs.x, term->curs.y - 1, 1);
		    break;
		  case 'B':
		    move(term, term->curs.x, term->curs.y + 1, 1);
		    break;
		  case 'C':
		    move(term, term->curs.x + 1, term->curs.y, 1);
		    break;
		  case 'D':
		    move(term, term->curs.x - 1, term->curs.y, 1);
		    break;
		    /*
		     * From the VT100 Manual
		     * NOTE: The special graphics characters in the VT100
		     *       are different from those in the VT52
		     *
		     * From VT102 manual:
		     *       137 _  Blank             - Same
		     *       140 `  Reserved          - Humm.
		     *       141 a  Solid rectangle   - Similar
		     *       142 b  1/                - Top half of fraction for the
		     *       143 c  3/                - subscript numbers below.
		     *       144 d  5/
		     *       145 e  7/
		     *       146 f  Degrees           - Same
		     *       147 g  Plus or minus     - Same
		     *       150 h  Right arrow
		     *       151 i  Ellipsis (dots)
		     *       152 j  Divide by
		     *       153 k  Down arrow
		     *       154 l  Bar at scan 0
		     *       155 m  Bar at scan 1
		     *       156 n  Bar at scan 2
		     *       157 o  Bar at scan 3     - Similar
		     *       160 p  Bar at scan 4     - Similar
		     *       161 q  Bar at scan 5     - Similar
		     *       162 r  Bar at scan 6     - Same
		     *       163 s  Bar at scan 7     - Similar
		     *       164 t  Subscript 0
		     *       165 u  Subscript 1
		     *       166 v  Subscript 2
		     *       167 w  Subscript 3
		     *       170 x  Subscript 4
		     *       171 y  Subscript 5
		     *       172 z  Subscript 6
		     *       173 {  Subscript 7
		     *       174 |  Subscript 8
		     *       175 }  Subscript 9
		     *       176 ~  Paragraph
		     *
		     */
		  case 'F':
		    term->cset_attr[term->cset = 0] = CSET_LINEDRW;
		    break;
		  case 'G':
		    term->cset_attr[term->cset = 0] = CSET_ASCII;
		    break;
		  case 'H':
		    move(term, 0, 0, 0);
		    break;
		  case 'I':
		    if (term->curs.y == 0)
			scroll(term, 0, term->rows - 1, -1, TRUE);
		    else if (term->curs.y > 0)
			term->curs.y--;
		    term->wrapnext = FALSE;
		    break;
		  case 'J':
		    erase_lots(term, FALSE, FALSE, TRUE);
		    term->disptop = 0;
		    break;
		  case 'K':
		    erase_lots(term, TRUE, FALSE, TRUE);
		    break;
#if 0
		  case 'V':
		    /* XXX Print cursor line */
		    break;
		  case 'W':
		    /* XXX Start controller mode */
		    break;
		  case 'X':
		    /* XXX Stop controller mode */
		    break;
#endif
		  case 'Y':
		    term->termstate = VT52_Y1;
		    break;
		  case 'Z':
		    if (term->ldisc)
			ldisc_send(term->ldisc, "\033/Z", 3, 0);
		    break;
		  case '=':
		    term->app_keypad_keys = TRUE;
		    break;
		  case '>':
		    term->app_keypad_keys = FALSE;
		    break;
		  case '<':
		    /* XXX This should switch to VT100 mode not current or default
		     *     VT mode. But this will only have effect in a VT220+
		     *     emulation.
		     */
		    term->vt52_mode = FALSE;
		    term->blink_is_real = term->cfg.blinktext;
		    break;
#if 0
		  case '^':
		    /* XXX Enter auto print mode */
		    break;
		  case '_':
		    /* XXX Exit auto print mode */
		    break;
		  case ']':
		    /* XXX Print screen */
		    break;
#endif

#ifdef VT52_PLUS
		  case 'E':
		    /* compatibility(ATARI) */
		    move(term, 0, 0, 0);
		    erase_lots(term, FALSE, FALSE, TRUE);
		    term->disptop = 0;
		    break;
		  case 'L':
		    /* compatibility(ATARI) */
		    if (term->curs.y <= term->marg_b)
			scroll(term, term->curs.y, term->marg_b, -1, FALSE);
		    break;
		  case 'M':
		    /* compatibility(ATARI) */
		    if (term->curs.y <= term->marg_b)
			scroll(term, term->curs.y, term->marg_b, 1, TRUE);
		    break;
		  case 'b':
		    /* compatibility(ATARI) */
		    term->termstate = VT52_FG;
		    break;
		  case 'c':
		    /* compatibility(ATARI) */
		    term->termstate = VT52_BG;
		    break;
		  case 'd':
		    /* compatibility(ATARI) */
		    erase_lots(term, FALSE, TRUE, FALSE);
		    term->disptop = 0;
		    break;
		  case 'e':
		    /* compatibility(ATARI) */
		    term->cursor_on = TRUE;
		    break;
		  case 'f':
		    /* compatibility(ATARI) */
		    term->cursor_on = FALSE;
		    break;
		    /* case 'j': Save cursor position - broken on ST */
		    /* case 'k': Restore cursor position */
		  case 'l':
		    /* compatibility(ATARI) */
		    erase_lots(term, TRUE, TRUE, TRUE);
		    term->curs.x = 0;
		    term->wrapnext = FALSE;
		    break;
		  case 'o':
		    /* compatibility(ATARI) */
		    erase_lots(term, TRUE, TRUE, FALSE);
		    break;
		  case 'p':
		    /* compatibility(ATARI) */
		    term->curr_attr |= ATTR_REVERSE;
		    break;
		  case 'q':
		    /* compatibility(ATARI) */
		    term->curr_attr &= ~ATTR_REVERSE;
		    break;
		  case 'v':	       /* wrap Autowrap on - Wyse style */
		    /* compatibility(ATARI) */
		    term->wrap = 1;
		    break;
		  case 'w':	       /* Autowrap off */
		    /* compatibility(ATARI) */
		    term->wrap = 0;
		    break;

		  case 'R':
		    /* compatibility(OTHER) */
		    term->vt52_bold = FALSE;
		    term->curr_attr = ATTR_DEFAULT;
		    set_erase_char(term);
		    break;
		  case 'S':
		    /* compatibility(VI50) */
		    term->curr_attr |= ATTR_UNDER;
		    break;
		  case 'W':
		    /* compatibility(VI50) */
		    term->curr_attr &= ~ATTR_UNDER;
		    break;
		  case 'U':
		    /* compatibility(VI50) */
		    term->vt52_bold = TRUE;
		    term->curr_attr |= ATTR_BOLD;
		    break;
		  case 'T':
		    /* compatibility(VI50) */
		    term->vt52_bold = FALSE;
		    term->curr_attr &= ~ATTR_BOLD;
		    break;
#endif
		}
		break;
	      case VT52_Y1:
		term->termstate = VT52_Y2;
		move(term, term->curs.x, c - ' ', 0);
		break;
	      case VT52_Y2:
		term->termstate = TOPLEVEL;
		move(term, c - ' ', term->curs.y, 0);
		break;

#ifdef VT52_PLUS
	      case VT52_FG:
		term->termstate = TOPLEVEL;
		term->curr_attr &= ~ATTR_FGMASK;
		term->curr_attr &= ~ATTR_BOLD;
		term->curr_attr |= (c & 0x7) << ATTR_FGSHIFT;
		if ((c & 0x8) || term->vt52_bold)
		    term->curr_attr |= ATTR_BOLD;

		set_erase_char(term);
		break;
	      case VT52_BG:
		term->termstate = TOPLEVEL;
		term->curr_attr &= ~ATTR_BGMASK;
		term->curr_attr &= ~ATTR_BLINK;
		term->curr_attr |= (c & 0x7) << ATTR_BGSHIFT;

		/* Note: bold background */
		if (c & 0x8)
		    term->curr_attr |= ATTR_BLINK;

		set_erase_char(term);
		break;
#endif
	      default: break;	       /* placate gcc warning about enum use */
	    }
	if (term->selstate != NO_SELECTION) {
	    pos cursplus = term->curs;
	    incpos(cursplus);
	    check_selection(term, term->curs, cursplus);
	}
    }

    term_print_flush(term);
    logflush(term->logctx);
}

/*
 * To prevent having to run the reasonably tricky bidi algorithm
 * too many times, we maintain a cache of the last lineful of data
 * fed to the algorithm on each line of the display.
 */
static int term_bidi_cache_hit(Terminal *term, int line,
			       termchar *lbefore, int width)
{
    int i;

    if (!term->pre_bidi_cache)
	return FALSE;		       /* cache doesn't even exist yet! */

    if (line >= term->bidi_cache_size)
	return FALSE;		       /* cache doesn't have this many lines */

    if (!term->pre_bidi_cache[line].chars)
	return FALSE;		       /* cache doesn't contain _this_ line */

    if (term->pre_bidi_cache[line].width != width)
	return FALSE;		       /* line is wrong width */

    for (i = 0; i < width; i++)
	if (!termchars_equal(term->pre_bidi_cache[line].chars+i, lbefore+i))
	    return FALSE;	       /* line doesn't match cache */

    return TRUE;		       /* it didn't match. */
}

static void term_bidi_cache_store(Terminal *term, int line, termchar *lbefore,
				  termchar *lafter, int width)
{
    if (!term->pre_bidi_cache || term->bidi_cache_size <= line) {
	int j = term->bidi_cache_size;
	term->bidi_cache_size = line+1;
	term->pre_bidi_cache = sresize(term->pre_bidi_cache,
				       term->bidi_cache_size,
				       struct bidi_cache_entry);
	term->post_bidi_cache = sresize(term->post_bidi_cache,
					term->bidi_cache_size,
					struct bidi_cache_entry);
	while (j < term->bidi_cache_size) {
	    term->pre_bidi_cache[j].chars =
		term->post_bidi_cache[j].chars = NULL;
	    term->pre_bidi_cache[j].width =
		term->post_bidi_cache[j].width = -1;
	    j++;
	}
    }

    sfree(term->pre_bidi_cache[line].chars);
    sfree(term->post_bidi_cache[line].chars);

    term->pre_bidi_cache[line].width = width;
    term->pre_bidi_cache[line].chars = snewn(width, termchar);
    term->post_bidi_cache[line].width = width;
    term->post_bidi_cache[line].chars = snewn(width, termchar);

    memcpy(term->pre_bidi_cache[line].chars, lbefore, width * TSIZE);
    memcpy(term->post_bidi_cache[line].chars, lafter, width * TSIZE);
}

/*
 * Given a context, update the window. Out of paranoia, we don't
 * allow WM_PAINT responses to do scrolling optimisations.
 */
static void do_paint(Terminal *term, Context ctx, int may_optimise)
{
    int i, it, j, our_curs_y, our_curs_x;
    int rv, cursor;
    pos scrpos;
    wchar_t *ch;
    int chlen;
    termchar cursor_background;
    unsigned long ticks;
#ifdef OPTIMISE_SCROLL
    struct scrollregion *sr;
#endif /* OPTIMISE_SCROLL */

    cursor_background = term->basic_erase_char;

    chlen = 1024;
    ch = snewn(chlen, wchar_t);

    /*
     * Check the visual bell state.
     */
    if (term->in_vbell) {
	ticks = GETTICKCOUNT();
	if (ticks - term->vbell_startpoint >= VBELL_TIMEOUT)
	    term->in_vbell = FALSE;
    }

    rv = (!term->rvideo ^ !term->in_vbell ? ATTR_REVERSE : 0);

    /* Depends on:
     * screen array, disptop, scrtop,
     * selection, rv, 
     * cfg.blinkpc, blink_is_real, tblinker, 
     * curs.y, curs.x, blinker, cfg.blink_cur, cursor_on, has_focus, wrapnext
     */

    /* Has the cursor position or type changed ? */
    if (term->cursor_on) {
	if (term->has_focus) {
	    if (term->blinker || !term->cfg.blink_cur)
		cursor = TATTR_ACTCURS;
	    else
		cursor = 0;
	} else
	    cursor = TATTR_PASCURS;
	if (term->wrapnext)
	    cursor |= TATTR_RIGHTCURS;
    } else
	cursor = 0;
    our_curs_y = term->curs.y - term->disptop;
    {
	/*
	 * Adjust the cursor position in the case where it's
	 * resting on the right-hand half of a CJK wide character.
	 * xterm's behaviour here, which seems adequate to me, is
	 * to display the cursor covering the _whole_ character,
	 * exactly as if it were one space to the left.
	 */
	termline *ldata = lineptr(term->curs.y);
	our_curs_x = term->curs.x;
	if (our_curs_x > 0 &&
	    ldata->chars[our_curs_x].chr == UCSWIDE)
	    our_curs_x--;
	unlineptr(ldata);
    }

    /*
     * If the cursor is not where it was last time we painted, and
     * its previous position is visible on screen, invalidate its
     * previous position.
     */
    if (term->dispcursy >= 0 &&
	(term->curstype != cursor ||
	 term->dispcursy != our_curs_y ||
	 term->dispcursx != our_curs_x)) {
	termchar *dispcurs = term->disptext[term->dispcursy]->chars +
	    term->dispcursx;

	if (term->dispcursx > 0 && dispcurs->chr == UCSWIDE)
	    dispcurs[-1].attr |= ATTR_INVALID;
	if (term->dispcursx < term->cols-1 && dispcurs[1].chr == UCSWIDE)
	    dispcurs[1].attr |= ATTR_INVALID;
	dispcurs->attr |= ATTR_INVALID;

	term->curstype = 0;
    }
    term->dispcursx = term->dispcursy = -1;

#ifdef OPTIMISE_SCROLL
    /* Do scrolls */
    sr = term->scrollhead;
    while (sr) {
	struct scrollregion *next = sr->next;
	do_scroll(ctx, sr->topline, sr->botline, sr->lines);
	sfree(sr);
	sr = next;
    }
    term->scrollhead = term->scrolltail = NULL;
#endif /* OPTIMISE_SCROLL */

    /* The normal screen data */
    for (i = 0; i < term->rows; i++) {
	termline *ldata;
	termchar *lchars;
	int dirty_line, dirty_run, selected;
	unsigned long attr = 0, cset = 0;
	int updated_line = 0;
	int start = 0;
	int ccount = 0;
	int last_run_dirty = 0;

	scrpos.y = i + term->disptop;
	ldata = lineptr(scrpos.y);

	dirty_run = dirty_line = (ldata->lattr !=
				  term->disptext[i]->lattr);
	term->disptext[i]->lattr = ldata->lattr;

	/* Do Arabic shaping and bidi. */
	if(!term->cfg.bidi || !term->cfg.arabicshaping) {

	    if (!term_bidi_cache_hit(term, i, ldata->chars, term->cols)) {

		if (term->wcFromTo_size < term->cols) {
		    term->wcFromTo_size = term->cols;
		    term->wcFrom = sresize(term->wcFrom, term->wcFromTo_size,
					   bidi_char);
		    term->wcTo = sresize(term->wcTo, term->wcFromTo_size,
					 bidi_char);
		}

		for(it=0; it<term->cols ; it++)
		{
		    unsigned long uc = (ldata->chars[it].chr);

		    switch (uc & CSET_MASK) {
		      case CSET_LINEDRW:
			if (!term->cfg.rawcnp) {
			    uc = term->ucsdata->unitab_xterm[uc & 0xFF];
			    break;
			}
		      case CSET_ASCII:
			uc = term->ucsdata->unitab_line[uc & 0xFF];
			break;
		      case CSET_SCOACS:
			uc = term->ucsdata->unitab_scoacs[uc&0xFF];
			break;
		    }
		    switch (uc & CSET_MASK) {
		      case CSET_ACP:
			uc = term->ucsdata->unitab_font[uc & 0xFF];
			break;
		      case CSET_OEMCP:
			uc = term->ucsdata->unitab_oemcp[uc & 0xFF];
			break;
		    }

		    term->wcFrom[it].origwc = term->wcFrom[it].wc =
			(wchar_t)uc;
		    term->wcFrom[it].index = it;
		}

		if(!term->cfg.bidi)
		    do_bidi(term->wcFrom, term->cols);

		/* this is saved iff done from inside the shaping */
		if(!term->cfg.bidi && term->cfg.arabicshaping)
		    for(it=0; it<term->cols; it++)
			term->wcTo[it] = term->wcFrom[it];

		if(!term->cfg.arabicshaping)
		    do_shape(term->wcFrom, term->wcTo, term->cols);

		if (term->ltemp_size < ldata->size) {
		    term->ltemp_size = ldata->size;
		    term->ltemp = sresize(term->ltemp, term->ltemp_size,
					  termchar);
		}

		memcpy(term->ltemp, ldata->chars, ldata->size * TSIZE);

		for(it=0; it<term->cols ; it++)
		{
		    term->ltemp[it] = ldata->chars[term->wcTo[it].index];
		    if (term->ltemp[it].cc_next)
			term->ltemp[it].cc_next -=
			it - term->wcTo[it].index;

		    if (term->wcTo[it].origwc != term->wcTo[it].wc)
			term->ltemp[it].chr = term->wcTo[it].wc;
		}
		term_bidi_cache_store(term, i, ldata->chars,
				      term->ltemp, ldata->size);

		lchars = term->ltemp;
	    } else {
		lchars = term->post_bidi_cache[i].chars;
	    }
	} else
	    lchars = ldata->chars;

	for (j = 0; j < term->cols; j++) {
	    unsigned long tattr, tchar;
	    termchar *d = lchars + j;
	    int break_run, do_copy;
	    scrpos.x = j;

	    tchar = d->chr;
	    tattr = d->attr;

            if (!term->cfg.ansi_colour)
                tattr = (tattr & ~(ATTR_FGMASK | ATTR_BGMASK)) | 
                ATTR_DEFFG | ATTR_DEFBG;

	    switch (tchar & CSET_MASK) {
	      case CSET_ASCII:
		tchar = term->ucsdata->unitab_line[tchar & 0xFF];
		break;
	      case CSET_LINEDRW:
		tchar = term->ucsdata->unitab_xterm[tchar & 0xFF];
		break;
	      case CSET_SCOACS:  
		tchar = term->ucsdata->unitab_scoacs[tchar&0xFF]; 
		break;
	    }
	    if (j < term->cols-1 && d[1].chr == UCSWIDE)
		tattr |= ATTR_WIDE;

	    /* Video reversing things */
	    if (term->selstate == DRAGGING || term->selstate == SELECTED) {
		if (term->seltype == LEXICOGRAPHIC)
		    selected = (posle(term->selstart, scrpos) &&
				poslt(scrpos, term->selend));
		else
		    selected = (posPle(term->selstart, scrpos) &&
				posPlt(scrpos, term->selend));
	    } else
		selected = FALSE;
	    tattr = (tattr ^ rv
		     ^ (selected ? ATTR_REVERSE : 0));

	    /* 'Real' blinking ? */
	    if (term->blink_is_real && (tattr & ATTR_BLINK)) {
		if (term->has_focus && term->tblinker) {
		    tchar = term->ucsdata->unitab_line[(unsigned char)' '];
		}
		tattr &= ~ATTR_BLINK;
	    }

	    /*
	     * Check the font we'll _probably_ be using to see if 
	     * the character is wide when we don't want it to be.
	     */
	    if (tchar != term->disptext[i]->chars[j].chr ||
		tattr != (term->disptext[i]->chars[j].attr &~
			  ATTR_NARROW)) {
		if ((tattr & ATTR_WIDE) == 0 && char_width(ctx, tchar) == 2)
		    tattr |= ATTR_NARROW;
	    } else if (term->disptext[i]->chars[j].attr & ATTR_NARROW)
		tattr |= ATTR_NARROW;

	    /* Cursor here ? Save the 'background' */
	    if (i == our_curs_y && j == our_curs_x) {
		/* FULL-TERMCHAR */
		cursor_background.chr = tchar;
		cursor_background.attr = tattr;
		/* For once, this cc_next field is an absolute index in lchars */
		if (d->cc_next)
		    cursor_background.cc_next = d->cc_next + j;
		else
		    cursor_background.cc_next = 0;
		term->dispcursx = j;
		term->dispcursy = i;
	    }

	    if ((term->disptext[i]->chars[j].attr ^ tattr) & ATTR_WIDE)
		dirty_line = TRUE;

	    break_run = ((tattr ^ attr) & term->attr_mask) != 0;

	    /* Special hack for VT100 Linedraw glyphs */
	    if (tchar >= 0x23BA && tchar <= 0x23BD)
		break_run = TRUE;

	    /*
	     * Separate out sequences of characters that have the
	     * same CSET, if that CSET is a magic one.
	     */
	    if (CSET_OF(tchar) != cset)
		break_run = TRUE;

	    /*
	     * Break on both sides of any combined-character cell.
	     */
	    if (d->cc_next != 0 ||
		(j > 0 && d[-1].cc_next != 0))
		break_run = TRUE;

	    if (!term->ucsdata->dbcs_screenfont && !dirty_line) {
		if (term->disptext[i]->chars[j].chr == tchar &&
		    term->disptext[i]->chars[j].attr == tattr)
		    break_run = TRUE;
		else if (!dirty_run && ccount == 1)
		    break_run = TRUE;
	    }

	    if (break_run) {
		if ((dirty_run || last_run_dirty) && ccount > 0) {
		    do_text(ctx, start, i, ch, ccount, attr, ldata->lattr);
		    updated_line = 1;
		}
		start = j;
		ccount = 0;
		attr = tattr;
		cset = CSET_OF(tchar);
		if (term->ucsdata->dbcs_screenfont)
		    last_run_dirty = dirty_run;
		dirty_run = dirty_line;
	    }

	    do_copy = FALSE;
	    if (!termchars_equal_override(&term->disptext[i]->chars[j],
					  d, tchar, tattr)) {
		do_copy = TRUE;
		dirty_run = TRUE;
	    }

	    if (ccount >= chlen) {
		chlen = ccount + 256;
		ch = sresize(ch, chlen, wchar_t);
	    }
	    ch[ccount++] = (wchar_t) tchar;

	    if (d->cc_next) {
		termchar *dd = d;

		while (dd->cc_next) {
		    unsigned long schar;

		    dd += dd->cc_next;

		    schar = dd->chr;
		    switch (schar & CSET_MASK) {
		      case CSET_ASCII:
			schar = term->ucsdata->unitab_line[schar & 0xFF];
			break;
		      case CSET_LINEDRW:
			schar = term->ucsdata->unitab_xterm[schar & 0xFF];
			break;
		      case CSET_SCOACS:
			schar = term->ucsdata->unitab_scoacs[schar&0xFF];
			break;
		    }

		    if (ccount >= chlen) {
			chlen = ccount + 256;
			ch = sresize(ch, chlen, wchar_t);
		    }
		    ch[ccount++] = (wchar_t) schar;
		}

		attr |= TATTR_COMBINING;
	    }

	    if (do_copy) {
		copy_termchar(term->disptext[i], j, d);
		term->disptext[i]->chars[j].chr = tchar;
		term->disptext[i]->chars[j].attr = tattr;
	    }

	    /* If it's a wide char step along to the next one. */
	    if (tattr & ATTR_WIDE) {
		if (++j < term->cols) {
		    d++;
		    /*
		     * By construction above, the cursor should not
		     * be on the right-hand half of this character.
		     * Ever.
		     */
		    assert(!(i == our_curs_y && j == our_curs_x));
		    if (!termchars_equal(&term->disptext[i]->chars[j], d))
			dirty_run = TRUE;
		    copy_termchar(term->disptext[i], j, d);
		}
	    }
	}
	if (dirty_run && ccount > 0) {
	    do_text(ctx, start, i, ch, ccount, attr, ldata->lattr);
	    updated_line = 1;
	}

	/* Cursor on this line ? (and changed) */
	if (i == our_curs_y && (term->curstype != cursor || updated_line)) {
	    ch[0] = (wchar_t) cursor_background.chr;
	    attr = cursor_background.attr | cursor;
	    ccount = 1;

	    if (cursor_background.cc_next) {
		termchar *dd = ldata->chars + cursor_background.cc_next;

		while (1) {
		    unsigned long schar;

		    schar = dd->chr;
		    switch (schar & CSET_MASK) {
		      case CSET_ASCII:
			schar = term->ucsdata->unitab_line[schar & 0xFF];
			break;
		      case CSET_LINEDRW:
			schar = term->ucsdata->unitab_xterm[schar & 0xFF];
			break;
		      case CSET_SCOACS:
			schar = term->ucsdata->unitab_scoacs[schar&0xFF];
			break;
		    }

		    if (ccount >= chlen) {
			chlen = ccount + 256;
			ch = sresize(ch, chlen, wchar_t);
		    }
		    ch[ccount++] = (wchar_t) schar;

		    if (dd->cc_next)
			dd += dd->cc_next;
		    else
			break;
		}

		attr |= TATTR_COMBINING;
	    }

	    do_cursor(ctx, our_curs_x, i, ch, ccount, attr, ldata->lattr);
	    term->curstype = cursor;
	}

	unlineptr(ldata);
    }

    sfree(ch);
}

/*
 * Flick the switch that says if blinking things should be shown or hidden.
 */

void term_blink(Terminal *term, int flg)
{
    long now, blink_diff;

    now = GETTICKCOUNT();
    blink_diff = now - term->last_tblink;

    /* Make sure the text blinks no more than 2Hz; we'll use 0.45 s period. */
    if (blink_diff < 0 || blink_diff > (TICKSPERSEC * 9 / 20)) {
	term->last_tblink = now;
	term->tblinker = !term->tblinker;
    }

    if (flg) {
	term->blinker = 1;
	term->last_blink = now;
	return;
    }

    blink_diff = now - term->last_blink;

    /* Make sure the cursor blinks no faster than system blink rate */
    if (blink_diff >= 0 && blink_diff < (long) CURSORBLINK)
	return;

    term->last_blink = now;
    term->blinker = !term->blinker;
}

/*
 * Invalidate the whole screen so it will be repainted in full.
 */
void term_invalidate(Terminal *term)
{
    int i, j;

    for (i = 0; i < term->rows; i++)
	for (j = 0; j < term->cols; j++)
	    term->disptext[i]->chars[j].attr = ATTR_INVALID;
}

/*
 * Paint the window in response to a WM_PAINT message.
 */
void term_paint(Terminal *term, Context ctx,
		int left, int top, int right, int bottom, int immediately)
{
    int i, j;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= term->cols) right = term->cols-1;
    if (bottom >= term->rows) bottom = term->rows-1;

    for (i = top; i <= bottom && i < term->rows; i++) {
	if ((term->disptext[i]->lattr & LATTR_MODE) == LATTR_NORM)
	    for (j = left; j <= right && j < term->cols; j++)
		term->disptext[i]->chars[j].attr = ATTR_INVALID;
	else
	    for (j = left / 2; j <= right / 2 + 1 && j < term->cols; j++)
		term->disptext[i]->chars[j].attr = ATTR_INVALID;
    }

    /* This should happen soon enough, also for some reason it sometimes 
     * fails to actually do anything when re-sizing ... painting the wrong
     * window perhaps ?
     */
    if (immediately)
        do_paint (term, ctx, FALSE);
}

/*
 * Attempt to scroll the scrollback. The second parameter gives the
 * position we want to scroll to; the first is +1 to denote that
 * this position is relative to the beginning of the scrollback, -1
 * to denote it is relative to the end, and 0 to denote that it is
 * relative to the current position.
 */
void term_scroll(Terminal *term, int rel, int where)
{
    int sbtop = -sblines(term);
#ifdef OPTIMISE_SCROLL
    int olddisptop = term->disptop;
    int shift;
#endif /* OPTIMISE_SCROLL */

    term->disptop = (rel < 0 ? 0 : rel > 0 ? sbtop : term->disptop) + where;
    if (term->disptop < sbtop)
	term->disptop = sbtop;
    if (term->disptop > 0)
	term->disptop = 0;
    update_sbar(term);
#ifdef OPTIMISE_SCROLL
    shift = (term->disptop - olddisptop);
    if (shift < term->rows && shift > -term->rows)
	scroll_display(term, 0, term->rows - 1, shift);
#endif /* OPTIMISE_SCROLL */
    term_update(term);
}

static void clipme(Terminal *term, pos top, pos bottom, int rect, int desel)
{
    wchar_t *workbuf;
    wchar_t *wbptr;		       /* where next char goes within workbuf */
    int old_top_x;
    int wblen = 0;		       /* workbuf len */
    int buflen;			       /* amount of memory allocated to workbuf */

    buflen = 5120;		       /* Default size */
    workbuf = snewn(buflen, wchar_t);
    wbptr = workbuf;		       /* start filling here */
    old_top_x = top.x;		       /* needed for rect==1 */

    while (poslt(top, bottom)) {
	int nl = FALSE;
	termline *ldata = lineptr(top.y);
	pos nlpos;

	/*
	 * nlpos will point at the maximum position on this line we
	 * should copy up to. So we start it at the end of the
	 * line...
	 */
	nlpos.y = top.y;
	nlpos.x = term->cols;

	/*
	 * ... move it backwards if there's unused space at the end
	 * of the line (and also set `nl' if this is the case,
	 * because in normal selection mode this means we need a
	 * newline at the end)...
	 */
	if (!(ldata->lattr & LATTR_WRAPPED)) {
	    while (IS_SPACE_CHR(ldata->chars[nlpos.x - 1].chr) &&
		   !ldata->chars[nlpos.x - 1].cc_next &&
		   poslt(top, nlpos))
		decpos(nlpos);
	    if (poslt(nlpos, bottom))
		nl = TRUE;
	} else if (ldata->lattr & LATTR_WRAPPED2) {
	    /* Ignore the last char on the line in a WRAPPED2 line. */
	    decpos(nlpos);
	}

	/*
	 * ... and then clip it to the terminal x coordinate if
	 * we're doing rectangular selection. (In this case we
	 * still did the above, so that copying e.g. the right-hand
	 * column from a table doesn't fill with spaces on the
	 * right.)
	 */
	if (rect) {
	    if (nlpos.x > bottom.x)
		nlpos.x = bottom.x;
	    nl = (top.y < bottom.y);
	}

	while (poslt(top, bottom) && poslt(top, nlpos)) {
#if 0
	    char cbuf[16], *p;
	    sprintf(cbuf, "<U+%04x>", (ldata[top.x] & 0xFFFF));
#else
	    wchar_t cbuf[16], *p;
	    int set, c;
	    int x = top.x;

	    if (ldata->chars[x].chr == UCSWIDE) {
		top.x++;
		continue;
	    }

	    while (1) {
		int uc = ldata->chars[x].chr;

		switch (uc & CSET_MASK) {
		  case CSET_LINEDRW:
		    if (!term->cfg.rawcnp) {
			uc = term->ucsdata->unitab_xterm[uc & 0xFF];
			break;
		    }
		  case CSET_ASCII:
		    uc = term->ucsdata->unitab_line[uc & 0xFF];
		    break;
		  case CSET_SCOACS:
		    uc = term->ucsdata->unitab_scoacs[uc&0xFF];
		    break;
		}
		switch (uc & CSET_MASK) {
		  case CSET_ACP:
		    uc = term->ucsdata->unitab_font[uc & 0xFF];
		    break;
		  case CSET_OEMCP:
		    uc = term->ucsdata->unitab_oemcp[uc & 0xFF];
		    break;
		}

		set = (uc & CSET_MASK);
		c = (uc & ~CSET_MASK);
		cbuf[0] = uc;
		cbuf[1] = 0;

		if (DIRECT_FONT(uc)) {
		    if (c >= ' ' && c != 0x7F) {
			char buf[4];
			WCHAR wbuf[4];
			int rv;
			if (is_dbcs_leadbyte(term->ucsdata->font_codepage, (BYTE) c)) {
			    buf[0] = c;
			    buf[1] = (char) (0xFF & ldata->chars[top.x + 1].chr);
			    rv = mb_to_wc(term->ucsdata->font_codepage, 0, buf, 2, wbuf, 4);
			    top.x++;
			} else {
			    buf[0] = c;
			    rv = mb_to_wc(term->ucsdata->font_codepage, 0, buf, 1, wbuf, 4);
			}

			if (rv > 0) {
			    memcpy(cbuf, wbuf, rv * sizeof(wchar_t));
			    cbuf[rv] = 0;
			}
		    }
		}
#endif

		for (p = cbuf; *p; p++) {
		    /* Enough overhead for trailing NL and nul */
		    if (wblen >= buflen - 16) {
			buflen += 100;
			workbuf = sresize(workbuf, buflen, wchar_t);
			wbptr = workbuf + wblen;
		    }
		    wblen++;
		    *wbptr++ = *p;
		}

		if (ldata->chars[x].cc_next)
		    x += ldata->chars[x].cc_next;
		else
		    break;
	    }
	    top.x++;
	}
	if (nl) {
	    int i;
	    for (i = 0; i < sel_nl_sz; i++) {
		wblen++;
		*wbptr++ = sel_nl[i];
	    }
	}
	top.y++;
	top.x = rect ? old_top_x : 0;

	unlineptr(ldata);
    }
#if SELECTION_NUL_TERMINATED
    wblen++;
    *wbptr++ = 0;
#endif
    write_clip(term->frontend, workbuf, wblen, desel); /* transfer to clipbd */
    if (buflen > 0)		       /* indicates we allocated this buffer */
	sfree(workbuf);
}

void term_copyall(Terminal *term)
{
    pos top;
    pos bottom;
    tree234 *screen = term->screen;
    top.y = -sblines(term);
    top.x = 0;
    bottom.y = find_last_nonempty_line(term, screen);
    bottom.x = term->cols;
    clipme(term, top, bottom, 0, TRUE);
}

/*
 * The wordness array is mainly for deciding the disposition of the
 * US-ASCII characters.
 */
static int wordtype(Terminal *term, int uc)
{
    struct ucsword {
	int start, end, ctype;
    };
    static const struct ucsword ucs_words[] = {
	{
	128, 160, 0}, {
	161, 191, 1}, {
	215, 215, 1}, {
	247, 247, 1}, {
	0x037e, 0x037e, 1},	       /* Greek question mark */
	{
	0x0387, 0x0387, 1},	       /* Greek ano teleia */
	{
	0x055a, 0x055f, 1},	       /* Armenian punctuation */
	{
	0x0589, 0x0589, 1},	       /* Armenian full stop */
	{
	0x0700, 0x070d, 1},	       /* Syriac punctuation */
	{
	0x104a, 0x104f, 1},	       /* Myanmar punctuation */
	{
	0x10fb, 0x10fb, 1},	       /* Georgian punctuation */
	{
	0x1361, 0x1368, 1},	       /* Ethiopic punctuation */
	{
	0x166d, 0x166e, 1},	       /* Canadian Syl. punctuation */
	{
	0x17d4, 0x17dc, 1},	       /* Khmer punctuation */
	{
	0x1800, 0x180a, 1},	       /* Mongolian punctuation */
	{
	0x2000, 0x200a, 0},	       /* Various spaces */
	{
	0x2070, 0x207f, 2},	       /* superscript */
	{
	0x2080, 0x208f, 2},	       /* subscript */
	{
	0x200b, 0x27ff, 1},	       /* punctuation and symbols */
	{
	0x3000, 0x3000, 0},	       /* ideographic space */
	{
	0x3001, 0x3020, 1},	       /* ideographic punctuation */
	{
	0x303f, 0x309f, 3},	       /* Hiragana */
	{
	0x30a0, 0x30ff, 3},	       /* Katakana */
	{
	0x3300, 0x9fff, 3},	       /* CJK Ideographs */
	{
	0xac00, 0xd7a3, 3},	       /* Hangul Syllables */
	{
	0xf900, 0xfaff, 3},	       /* CJK Ideographs */
	{
	0xfe30, 0xfe6b, 1},	       /* punctuation forms */
	{
	0xff00, 0xff0f, 1},	       /* half/fullwidth ASCII */
	{
	0xff1a, 0xff20, 1},	       /* half/fullwidth ASCII */
	{
	0xff3b, 0xff40, 1},	       /* half/fullwidth ASCII */
	{
	0xff5b, 0xff64, 1},	       /* half/fullwidth ASCII */
	{
	0xfff0, 0xffff, 0},	       /* half/fullwidth ASCII */
	{
	0, 0, 0}
    };
    const struct ucsword *wptr;

    switch (uc & CSET_MASK) {
      case CSET_LINEDRW:
	uc = term->ucsdata->unitab_xterm[uc & 0xFF];
	break;
      case CSET_ASCII:
	uc = term->ucsdata->unitab_line[uc & 0xFF];
	break;
      case CSET_SCOACS:  
	uc = term->ucsdata->unitab_scoacs[uc&0xFF]; 
	break;
    }
    switch (uc & CSET_MASK) {
      case CSET_ACP:
	uc = term->ucsdata->unitab_font[uc & 0xFF];
	break;
      case CSET_OEMCP:
	uc = term->ucsdata->unitab_oemcp[uc & 0xFF];
	break;
    }

    /* For DBCS fonts I can't do anything useful. Even this will sometimes
     * fail as there's such a thing as a double width space. :-(
     */
    if (term->ucsdata->dbcs_screenfont &&
	term->ucsdata->font_codepage == term->ucsdata->line_codepage)
	return (uc != ' ');

    if (uc < 0x80)
	return term->wordness[uc];

    for (wptr = ucs_words; wptr->start; wptr++) {
	if (uc >= wptr->start && uc <= wptr->end)
	    return wptr->ctype;
    }

    return 2;
}

/*
 * Spread the selection outwards according to the selection mode.
 */
static pos sel_spread_half(Terminal *term, pos p, int dir)
{
    termline *ldata;
    short wvalue;
    int topy = -sblines(term);

    ldata = lineptr(p.y);

    switch (term->selmode) {
      case SM_CHAR:
	/*
	 * In this mode, every character is a separate unit, except
	 * for runs of spaces at the end of a non-wrapping line.
	 */
	if (!(ldata->lattr & LATTR_WRAPPED)) {
	    termchar *q = ldata->chars + term->cols;
	    while (q > ldata->chars &&
		   IS_SPACE_CHR(q[-1].chr) && !q[-1].cc_next)
		q--;
	    if (q == ldata->chars + term->cols)
		q--;
	    if (p.x >= q - ldata->chars)
		p.x = (dir == -1 ? q - ldata->chars : term->cols - 1);
	}
	break;
      case SM_WORD:
	/*
	 * In this mode, the units are maximal runs of characters
	 * whose `wordness' has the same value.
	 */
	wvalue = wordtype(term, UCSGET(ldata->chars, p.x));
	if (dir == +1) {
	    while (1) {
		int maxcols = (ldata->lattr & LATTR_WRAPPED2 ?
			       term->cols-1 : term->cols);
		if (p.x < maxcols-1) {
		    if (wordtype(term, UCSGET(ldata->chars, p.x+1)) == wvalue)
			p.x++;
		    else
			break;
		} else {
		    if (ldata->lattr & LATTR_WRAPPED) {
			termline *ldata2;
			ldata2 = lineptr(p.y+1);
			if (wordtype(term, UCSGET(ldata2->chars, 0))
			    == wvalue) {
			    p.x = 0;
			    p.y++;
			    unlineptr(ldata);
			    ldata = ldata2;
			} else {
			    unlineptr(ldata2);
			    break;
			}
		    } else
			break;
		}
	    }
	} else {
	    while (1) {
		if (p.x > 0) {
		    if (wordtype(term, UCSGET(ldata->chars, p.x-1)) == wvalue)
			p.x--;
		    else
			break;
		} else {
		    termline *ldata2;
		    int maxcols;
		    if (p.y <= topy)
			break;
		    ldata2 = lineptr(p.y-1);
		    maxcols = (ldata2->lattr & LATTR_WRAPPED2 ?
			      term->cols-1 : term->cols);
		    if (ldata2->lattr & LATTR_WRAPPED) {
			if (wordtype(term, UCSGET(ldata2->chars, maxcols-1))
			    == wvalue) {
			    p.x = maxcols-1;
			    p.y--;
			    unlineptr(ldata);
			    ldata = ldata2;
			} else {
			    unlineptr(ldata2);
			    break;
			}
		    } else
			break;
		}
	    }
	}
	break;
      case SM_LINE:
	/*
	 * In this mode, every line is a unit.
	 */
	p.x = (dir == -1 ? 0 : term->cols - 1);
	break;
    }

    unlineptr(ldata);
    return p;
}

static void sel_spread(Terminal *term)
{
    if (term->seltype == LEXICOGRAPHIC) {
	term->selstart = sel_spread_half(term, term->selstart, -1);
	decpos(term->selend);
	term->selend = sel_spread_half(term, term->selend, +1);
	incpos(term->selend);
    }
}

void term_do_paste(Terminal *term)
{
    wchar_t *data;
    int len;

    get_clip(term->frontend, &data, &len);
    if (data && len > 0) {
        wchar_t *p, *q;

	term_seen_key_event(term);     /* pasted data counts */

        if (term->paste_buffer)
            sfree(term->paste_buffer);
        term->paste_pos = term->paste_hold = term->paste_len = 0;
        term->paste_buffer = snewn(len, wchar_t);

        p = q = data;
        while (p < data + len) {
            while (p < data + len &&
                   !(p <= data + len - sel_nl_sz &&
                     !memcmp(p, sel_nl, sizeof(sel_nl))))
                p++;

            {
                int i;
                for (i = 0; i < p - q; i++) {
                    term->paste_buffer[term->paste_len++] = q[i];
                }
            }

            if (p <= data + len - sel_nl_sz &&
                !memcmp(p, sel_nl, sizeof(sel_nl))) {
                term->paste_buffer[term->paste_len++] = '\015';
                p += sel_nl_sz;
            }
            q = p;
        }

        /* Assume a small paste will be OK in one go. */
        if (term->paste_len < 256) {
            if (term->ldisc)
		luni_send(term->ldisc, term->paste_buffer, term->paste_len, 0);
            if (term->paste_buffer)
                sfree(term->paste_buffer);
            term->paste_buffer = 0;
            term->paste_pos = term->paste_hold = term->paste_len = 0;
        }
    }
    get_clip(term->frontend, NULL, NULL);
}

void term_mouse(Terminal *term, Mouse_Button braw, Mouse_Button bcooked,
		Mouse_Action a, int x, int y, int shift, int ctrl, int alt)
{
    pos selpoint;
    termline *ldata;
    int raw_mouse = (term->xterm_mouse &&
		     !term->cfg.no_mouse_rep &&
		     !(term->cfg.mouse_override && shift));
    int default_seltype;

    if (y < 0) {
	y = 0;
	if (a == MA_DRAG && !raw_mouse)
	    term_scroll(term, 0, -1);
    }
    if (y >= term->rows) {
	y = term->rows - 1;
	if (a == MA_DRAG && !raw_mouse)
	    term_scroll(term, 0, +1);
    }
    if (x < 0) {
	if (y > 0) {
	    x = term->cols - 1;
	    y--;
	} else
	    x = 0;
    }
    if (x >= term->cols)
	x = term->cols - 1;

    selpoint.y = y + term->disptop;
    selpoint.x = x;
    ldata = lineptr(selpoint.y);
    if ((ldata->lattr & LATTR_MODE) != LATTR_NORM)
	selpoint.x /= 2;
    unlineptr(ldata);

    if (raw_mouse) {
	int encstate = 0, r, c;
	char abuf[16];

	if (term->ldisc) {

	    switch (braw) {
	      case MBT_LEFT:
		encstate = 0x20;	       /* left button down */
		break;
	      case MBT_MIDDLE:
		encstate = 0x21;
		break;
	      case MBT_RIGHT:
		encstate = 0x22;
		break;
	      case MBT_WHEEL_UP:
		encstate = 0x60;
		break;
	      case MBT_WHEEL_DOWN:
		encstate = 0x61;
		break;
	      default: break;	       /* placate gcc warning about enum use */
	    }
	    switch (a) {
	      case MA_DRAG:
		if (term->xterm_mouse == 1)
		    return;
		encstate += 0x20;
		break;
	      case MA_RELEASE:
		encstate = 0x23;
		term->mouse_is_down = 0;
		break;
	      case MA_CLICK:
		if (term->mouse_is_down == braw)
		    return;
		term->mouse_is_down = braw;
		break;
	      default: break;	       /* placate gcc warning about enum use */
	    }
	    if (shift)
		encstate += 0x04;
	    if (ctrl)
		encstate += 0x10;
	    r = y + 33;
	    c = x + 33;

	    sprintf(abuf, "\033[M%c%c%c", encstate, c, r);
	    ldisc_send(term->ldisc, abuf, 6, 0);
	}
	return;
    }

    /*
     * Set the selection type (rectangular or normal) at the start
     * of a selection attempt, from the state of Alt.
     */
    if (!alt ^ !term->cfg.rect_select)
	default_seltype = RECTANGULAR;
    else
	default_seltype = LEXICOGRAPHIC;
	
    if (term->selstate == NO_SELECTION) {
	term->seltype = default_seltype;
    }

    if (bcooked == MBT_SELECT && a == MA_CLICK) {
	deselect(term);
	term->selstate = ABOUT_TO;
	term->seltype = default_seltype;
	term->selanchor = selpoint;
	term->selmode = SM_CHAR;
    } else if (bcooked == MBT_SELECT && (a == MA_2CLK || a == MA_3CLK)) {
	deselect(term);
	term->selmode = (a == MA_2CLK ? SM_WORD : SM_LINE);
	term->selstate = DRAGGING;
	term->selstart = term->selanchor = selpoint;
	term->selend = term->selstart;
	incpos(term->selend);
	sel_spread(term);
    } else if ((bcooked == MBT_SELECT && a == MA_DRAG) ||
	       (bcooked == MBT_EXTEND && a != MA_RELEASE)) {
	if (term->selstate == ABOUT_TO && poseq(term->selanchor, selpoint))
	    return;
	if (bcooked == MBT_EXTEND && a != MA_DRAG &&
	    term->selstate == SELECTED) {
	    if (term->seltype == LEXICOGRAPHIC) {
		/*
		 * For normal selection, we extend by moving
		 * whichever end of the current selection is closer
		 * to the mouse.
		 */
		if (posdiff(selpoint, term->selstart) <
		    posdiff(term->selend, term->selstart) / 2) {
		    term->selanchor = term->selend;
		    decpos(term->selanchor);
		} else {
		    term->selanchor = term->selstart;
		}
	    } else {
		/*
		 * For rectangular selection, we have a choice of
		 * _four_ places to put selanchor and selpoint: the
		 * four corners of the selection.
		 */
		if (2*selpoint.x < term->selstart.x + term->selend.x)
		    term->selanchor.x = term->selend.x-1;
		else
		    term->selanchor.x = term->selstart.x;

		if (2*selpoint.y < term->selstart.y + term->selend.y)
		    term->selanchor.y = term->selend.y;
		else
		    term->selanchor.y = term->selstart.y;
	    }
	    term->selstate = DRAGGING;
	}
	if (term->selstate != ABOUT_TO && term->selstate != DRAGGING)
	    term->selanchor = selpoint;
	term->selstate = DRAGGING;
	if (term->seltype == LEXICOGRAPHIC) {
	    /*
	     * For normal selection, we set (selstart,selend) to
	     * (selpoint,selanchor) in some order.
	     */
	    if (poslt(selpoint, term->selanchor)) {
		term->selstart = selpoint;
		term->selend = term->selanchor;
		incpos(term->selend);
	    } else {
		term->selstart = term->selanchor;
		term->selend = selpoint;
		incpos(term->selend);
	    }
	} else {
	    /*
	     * For rectangular selection, we may need to
	     * interchange x and y coordinates (if the user has
	     * dragged in the -x and +y directions, or vice versa).
	     */
	    term->selstart.x = min(term->selanchor.x, selpoint.x);
	    term->selend.x = 1+max(term->selanchor.x, selpoint.x);
	    term->selstart.y = min(term->selanchor.y, selpoint.y);
	    term->selend.y =   max(term->selanchor.y, selpoint.y);
	}
	sel_spread(term);
    } else if ((bcooked == MBT_SELECT || bcooked == MBT_EXTEND) &&
	       a == MA_RELEASE) {
	if (term->selstate == DRAGGING) {
	    /*
	     * We've completed a selection. We now transfer the
	     * data to the clipboard.
	     */
	    clipme(term, term->selstart, term->selend,
		   (term->seltype == RECTANGULAR), FALSE);
	    term->selstate = SELECTED;
	} else
	    term->selstate = NO_SELECTION;
    } else if (bcooked == MBT_PASTE
	       && (a == MA_CLICK
#if MULTICLICK_ONLY_EVENT
		   || a == MA_2CLK || a == MA_3CLK
#endif
		   )) {
	request_paste(term->frontend);
    }

    term_update(term);
}

void term_key(Terminal *term, Key_Sym keysym, wchar_t *text, size_t tlen,
	      unsigned int modifiers, unsigned int flags)
{
    char output[10];
    char *p = output;
    int prependesc = FALSE;
#if 0
    int i;

    fprintf(stderr, "keysym = %d, %d chars:", keysym, tlen);
    for (i = 0; i < tlen; i++)
	fprintf(stderr, " %04x", (unsigned)text[i]);
    fprintf(stderr, "\n");
#endif

    /* XXX Num Lock */
    if ((flags & PKF_REPEAT) && term->repeat_off)
	return;

    /* Currently, Meta always just prefixes everything with ESC. */
    if (modifiers & PKM_META)
	prependesc = TRUE;
    modifiers &= ~PKM_META;

    /*
     * Alt is only used for Alt+keypad, which isn't supported yet, so
     * ignore it.
     */
    modifiers &= ~PKM_ALT;

    /* Standard local function keys */
    switch (modifiers & (PKM_SHIFT | PKM_CONTROL)) {
      case PKM_SHIFT:
	if (keysym == PK_PAGEUP)
	    /* scroll up one page */;
	if (keysym == PK_PAGEDOWN)
	    /* scroll down on page */;
	if (keysym == PK_INSERT)
	    term_do_paste(term);
	break;
      case PKM_CONTROL:
	if (keysym == PK_PAGEUP)
	    /* scroll up one line */;
	if (keysym == PK_PAGEDOWN)
	    /* scroll down one line */;
	/* Control-Numlock for app-keypad mode switch */
	if (keysym == PK_PF1)
	    term->app_keypad_keys ^= 1;
	break;
    }

    if (modifiers & PKM_ALT) {
	/* Alt+F4 (close) */
	/* Alt+Return (full screen) */
	/* Alt+Space (system menu) */
    }

    if (keysym == PK_NULL && (modifiers & PKM_CONTROL) && tlen == 1 &&
	text[0] >= 0x20 && text[0] <= 0x7e) {
	/* ASCII chars + Control */
	if ((text[0] >= 0x40 && text[0] <= 0x5f) ||
	    (text[0] >= 0x61 && text[0] <= 0x7a))
	    text[0] &= 0x1f;
	else {
	    /*
	     * Control-2 should return ^@ (0x00), Control-6 should return
	     * ^^ (0x1E), and Control-Minus should return ^_ (0x1F). Since
	     * the DOS keyboard handling did it, and we have nothing better
	     * to do with the key combo in question, we'll also map
	     * Control-Backquote to ^\ (0x1C).
	     */
	    switch (text[0]) {
	      case ' ': text[0] = 0x00; break;
	      case '-': text[0] = 0x1f; break;
	      case '/': text[0] = 0x1f; break;
	      case '2': text[0] = 0x00; break;
	      case '3': text[0] = 0x1b; break;
	      case '4': text[0] = 0x1c; break;
	      case '5': text[0] = 0x1d; break;
	      case '6': text[0] = 0x1e; break;
	      case '7': text[0] = 0x1f; break;
	      case '8': text[0] = 0x7f; break;
	      case '`': text[0] = 0x1c; break;
	    }
	}
    }

    /* Nethack keypad */
    if (term->cfg.nethack_keypad) {
	char c = 0;
	switch (keysym) {
	  case PK_KP1: c = 'b'; break;
	  case PK_KP2: c = 'j'; break;
	  case PK_KP3: c = 'n'; break;
	  case PK_KP4: c = 'h'; break;
	  case PK_KP5: c = '.'; break;
	  case PK_KP6: c = 'l'; break;
	  case PK_KP7: c = 'y'; break;
	  case PK_KP8: c = 'k'; break;
	  case PK_KP9: c = 'u'; break;
	  default: break; /* else gcc warns `enum value not used' */
	}
	if (c != 0) {
	    if (c != '.') {
		if (modifiers & PKM_CONTROL)
		    c &= 0x1f;
		else if (modifiers & PKM_SHIFT)
		    c = toupper(c);
	    }
	    *p++ = c;
	    goto done;
	}
    }

    /* Numeric Keypad */
    if (PK_ISKEYPAD(keysym)) {
	int xkey = 0;

	/*
	 * In VT400 mode, PFn always emits an escape sequence.  In
	 * Linux and tilde modes, this only happens in app keypad mode.
	 */
	if (term->cfg.funky_type == FUNKY_VT400 ||
	    ((term->cfg.funky_type == FUNKY_LINUX ||
	      term->cfg.funky_type == FUNKY_TILDE) &&
	     term->app_keypad_keys && !term->cfg.no_applic_k)) {
	    switch (keysym) {
	      case PK_PF1: xkey = 'P'; break;
	      case PK_PF2: xkey = 'Q'; break;
	      case PK_PF3: xkey = 'R'; break;
	      case PK_PF4: xkey = 'S'; break;
	      default: break; /* else gcc warns `enum value not used' */
	    }
	}
	if (term->app_keypad_keys && !term->cfg.no_applic_k) {
	    switch (keysym) {
	      case PK_KP0: xkey = 'p'; break;
	      case PK_KP1: xkey = 'q'; break;
	      case PK_KP2: xkey = 'r'; break;
	      case PK_KP3: xkey = 's'; break;
	      case PK_KP4: xkey = 't'; break;
	      case PK_KP5: xkey = 'u'; break;
	      case PK_KP6: xkey = 'v'; break;
	      case PK_KP7: xkey = 'w'; break;
	      case PK_KP8: xkey = 'x'; break;
	      case PK_KP9: xkey = 'y'; break;
	      case PK_KPDECIMAL: xkey = 'n'; break;
	      case PK_KPENTER: xkey = 'M'; break;
	      default: break; /* else gcc warns `enum value not used' */
	    }
	    if (term->cfg.funky_type == FUNKY_XTERM && tlen > 0) {
		/*
		 * xterm can't see the layout of the keypad, so it has
		 * to rely on the X keysyms returned by the keys.
		 * Hence, we look at the strings here, not the PuTTY
		 * keysyms (which describe the layout).
		 */
		switch (text[0]) {
		  case '+':
		    if (modifiers & PKM_SHIFT)
			xkey = 'l';
		    else
			xkey = 'k';
		    break;
		  case '/': xkey = 'o'; break;
		  case '*': xkey = 'j'; break;
		  case '-': xkey = 'm'; break;
		}
	    } else {
		/*
		 * In all other modes, we try to retain the layout of
		 * the DEC keypad in application mode.
		 */
		switch (keysym) {
		  case PK_KPBIGPLUS:
		    /* This key covers the '-' and ',' keys on a VT220 */
		    if (modifiers & PKM_SHIFT)
			xkey = 'm'; /* VT220 '-' */
		    else
			xkey = 'l'; /* VT220 ',' */
		    break;
		  case PK_KPMINUS: xkey = 'm'; break;
		  case PK_KPCOMMA: xkey = 'l'; break;
		  default: break; /* else gcc warns `enum value not used' */
		}
	    }
	}
	if (xkey) {
	    if (term->vt52_mode) {
		if (xkey >= 'P' && xkey <= 'S')
		    p += sprintf((char *) p, "\x1B%c", xkey);
		else
		    p += sprintf((char *) p, "\x1B?%c", xkey);
	    } else
		p += sprintf((char *) p, "\x1BO%c", xkey);
	    goto done;
	}
	/* Not in application mode -- treat the number pad as arrow keys? */
	if ((flags & PKF_NUMLOCK) == 0) {
	    switch (keysym) {
	      case PK_KP0: keysym = PK_INSERT; break;
	      case PK_KP1: keysym = PK_END; break;
	      case PK_KP2: keysym = PK_DOWN; break;
	      case PK_KP3: keysym = PK_PAGEDOWN; break;
	      case PK_KP4: keysym = PK_LEFT; break;
	      case PK_KP5: keysym = PK_REST; break;
	      case PK_KP6: keysym = PK_RIGHT; break;
	      case PK_KP7: keysym = PK_HOME; break;
	      case PK_KP8: keysym = PK_UP; break;
	      case PK_KP9: keysym = PK_PAGEUP; break;
	      default: break; /* else gcc warns `enum value not used' */
	    }
	}
    }

    /* Miscellaneous keys */
    switch (keysym) {
      case PK_ESCAPE:
	*p++ = 0x1b;
	goto done;
      case PK_BACKSPACE:
	    if (modifiers == 0)
		*p++ = (term->cfg.bksp_is_delete ? 0x7F : 0x08);
	    else if (modifiers == PKM_SHIFT)
		/* We do the opposite of what is configured */
		*p++ = (term->cfg.bksp_is_delete ? 0x08 : 0x7F);
	    else break;
	    goto done;
      case PK_TAB:
	if (modifiers == 0)
	    *p++ = 0x09;
	else if (modifiers == PKM_SHIFT)
	    *p++ = 0x1B, *p++ = '[', *p++ = 'Z';
	else break;
	goto done;
	/* XXX window.c has ctrl+shift+space sending 0xa0 */
      case PK_PAUSE:
	if (modifiers == PKM_CONTROL)
	    *p++ = 26;
	else break;
	goto done;
      case PK_RETURN:
      case PK_KPENTER: /* Odd keypad modes handled above */
	if (modifiers == 0) {
	    *p++ = 0x0d;
	    if (term->cr_lf_return)
		*p++ = 0x0a;
	    goto done;
	}
      default: break; /* else gcc warns `enum value not used' */
    }

    /* SCO function keys and editing keys */
    if (term->cfg.funky_type == FUNKY_SCO) {
	if (PK_ISFKEY(keysym) && keysym <= PK_F12) {
	    static char const codes[] =
		"MNOPQRSTUVWX" "YZabcdefghij" "klmnopqrstuv" "wxyz@[\\]^_`{";
	    int index = keysym - PK_F1;

	    if (modifiers & PKM_SHIFT) index += 12;
	    if (modifiers & PKM_CONTROL) index += 24;
	    p += sprintf((char *) p, "\x1B[%c", codes[index]);
	    goto done;
	}
	if (PK_ISEDITING(keysym)) {
	    int xkey = 0;

	    switch (keysym) {
	      case PK_DELETE:   *p++ = 0x7f; goto done;
	      case PK_HOME:     xkey = 'H'; break;
	      case PK_INSERT:   xkey = 'L'; break;
	      case PK_END:      xkey = 'F'; break;
	      case PK_PAGEUP:   xkey = 'I'; break;
	      case PK_PAGEDOWN: xkey = 'G'; break;
	      default: break; /* else gcc warns `enum value not used' */
	    }
	    p += sprintf((char *) p, "\x1B[%c", xkey);
	}
    }

    if (PK_ISEDITING(keysym) && (modifiers & PKM_SHIFT) == 0) {
	int code;

	if (term->cfg.funky_type == FUNKY_XTERM) {
	    /* Xterm shuffles these keys, apparently. */
	    switch (keysym) {
	      case PK_HOME:     keysym = PK_INSERT;   break;
	      case PK_INSERT:   keysym = PK_HOME;     break;
	      case PK_DELETE:   keysym = PK_END;      break;
	      case PK_END:      keysym = PK_PAGEUP;   break;
	      case PK_PAGEUP:   keysym = PK_DELETE;   break;
	      case PK_PAGEDOWN: keysym = PK_PAGEDOWN; break;
	      default: break; /* else gcc warns `enum value not used' */
	    }
	}

	/* RXVT Home/End */
	if (term->cfg.rxvt_homeend &&
	    (keysym == PK_HOME || keysym == PK_END)) {
	    p += sprintf((char *) p, keysym == PK_HOME ? "\x1B[H" : "\x1BOw");
	    goto done;
	}

	if (term->vt52_mode) {
	    int xkey;

	    /*
	     * A real VT52 doesn't have these, and a VT220 doesn't
	     * send anything for them in VT52 mode.
	     */
	    switch (keysym) {
	      case PK_HOME:     xkey = 'H'; break;
	      case PK_INSERT:   xkey = 'L'; break;
	      case PK_DELETE:   xkey = 'M'; break;
	      case PK_END:      xkey = 'E'; break;
	      case PK_PAGEUP:   xkey = 'I'; break;
	      case PK_PAGEDOWN: xkey = 'G'; break;
	      default: xkey=0; break; /* else gcc warns `enum value not used'*/
	    }
	    p += sprintf((char *) p, "\x1B%c", xkey);
	    goto done;
	}

	switch (keysym) {
	  case PK_HOME:     code = 1; break;
	  case PK_INSERT:   code = 2; break;
	  case PK_DELETE:   code = 3; break;
	  case PK_END:      code = 4; break;
	  case PK_PAGEUP:   code = 5; break;
	  case PK_PAGEDOWN: code = 6; break;
	  default: code = 0; break; /* else gcc warns `enum value not used' */
	}
	p += sprintf((char *) p, "\x1B[%d~", code);
	goto done;
    }

    if (PK_ISFKEY(keysym)) {
	/* Map Shift+F1-F10 to F11-F20 */
	if (keysym >= PK_F1 && keysym <= PK_F10 && (modifiers & PKM_SHIFT))
	    keysym += 10;
	if ((term->vt52_mode || term->cfg.funky_type == FUNKY_VT100P) &&
	    keysym <= PK_F14) {
	    /* XXX This overrides the XTERM/VT52 mode below */
	    int offt = 0;
	    if (keysym >= PK_F6)  offt++;
	    if (keysym >= PK_F12) offt++;
	    p += sprintf((char *) p, term->vt52_mode ? "\x1B%c" : "\x1BO%c",
			 'P' + keysym - PK_F1 - offt);
	    goto done;
	}
	if (term->cfg.funky_type == FUNKY_LINUX && keysym <= PK_F5) {
	    p += sprintf((char *) p, "\x1B[[%c", 'A' + keysym - PK_F1);
	    goto done;
	}
	if (term->cfg.funky_type == FUNKY_XTERM && keysym <= PK_F4) {
	    if (term->vt52_mode)
		p += sprintf((char *) p, "\x1B%c", 'P' + keysym - PK_F1);
	    else
		p += sprintf((char *) p, "\x1BO%c", 'P' + keysym - PK_F1);
	    goto done;
	}
	p += sprintf((char *) p, "\x1B[%d~", 11 + keysym - PK_F1);
	goto done;
    }

    if (PK_ISCURSOR(keysym)) {
	int xkey;

	switch (keysym) {
	  case PK_UP:    xkey = 'A'; break;
	  case PK_DOWN:  xkey = 'B'; break;
	  case PK_RIGHT: xkey = 'C'; break;
	  case PK_LEFT:  xkey = 'D'; break;
	  case PK_REST:  xkey = 'G'; break; /* centre key on number pad */
	  default: xkey = 0; break; /* else gcc warns `enum value not used' */
	}
	if (term->vt52_mode)
	    p += sprintf((char *) p, "\x1B%c", xkey);
	else {
	    int app_flg = (term->app_cursor_keys && !term->cfg.no_applic_c);

	    /* Useful mapping of Ctrl-arrows */
	    if (modifiers == PKM_CONTROL)
		app_flg = !app_flg;

	    if (app_flg)
		p += sprintf((char *) p, "\x1BO%c", xkey);
	    else
		p += sprintf((char *) p, "\x1B[%c", xkey);
	}
	goto done;
    }

  done:
    if (p > output || tlen > 0) {
	/*
	 * Interrupt an ongoing paste. I'm not sure
	 * this is sensible, but for the moment it's
	 * preferable to having to faff about buffering
	 * things.
	 */
	term_nopaste(term);

	/*
	 * We need not bother about stdin backlogs
	 * here, because in GUI PuTTY we can't do
	 * anything about it anyway; there's no means
	 * of asking Windows to hold off on KEYDOWN
	 * messages. We _have_ to buffer everything
	 * we're sent.
	 */
	term_seen_key_event(term);

	if (prependesc) {
#if 0
	    fprintf(stderr, "sending ESC\n");
#endif
	    ldisc_send(term->ldisc, "\x1b", 1, 1);
	}

	if (p > output) {
#if 0
	    fprintf(stderr, "sending %d bytes:", p - output);
	    for (i = 0; i < p - output; i++)
		fprintf(stderr, " %02x", output[i]);
	    fprintf(stderr, "\n");
#endif
	    ldisc_send(term->ldisc, output, p - output, 1);
	} else if (tlen > 0) {
#if 0
	    fprintf(stderr, "sending %d unichars:", tlen);
	    for (i = 0; i < tlen; i++)
		fprintf(stderr, " %04x", (unsigned) text[i]);
	    fprintf(stderr, "\n");
#endif
	    luni_send(term->ldisc, text, tlen, 1);
	}
    }
}

void term_nopaste(Terminal *term)
{
    if (term->paste_len == 0)
	return;
    sfree(term->paste_buffer);
    term->paste_buffer = NULL;
    term->paste_len = 0;
}

int term_paste_pending(Terminal *term)
{
    return term->paste_len != 0;
}

void term_paste(Terminal *term)
{
    long now, paste_diff;

    if (term->paste_len == 0)
	return;

    /* Don't wait forever to paste */
    if (term->paste_hold) {
	now = GETTICKCOUNT();
	paste_diff = now - term->last_paste;
	if (paste_diff >= 0 && paste_diff < 450)
	    return;
    }
    term->paste_hold = 0;

    while (term->paste_pos < term->paste_len) {
	int n = 0;
	while (n + term->paste_pos < term->paste_len) {
	    if (term->paste_buffer[term->paste_pos + n++] == '\015')
		break;
	}
	if (term->ldisc)
	    luni_send(term->ldisc, term->paste_buffer + term->paste_pos, n, 0);
	term->paste_pos += n;

	if (term->paste_pos < term->paste_len) {
	    term->paste_hold = 1;
	    return;
	}
    }
    sfree(term->paste_buffer);
    term->paste_buffer = NULL;
    term->paste_len = 0;
}

static void deselect(Terminal *term)
{
    term->selstate = NO_SELECTION;
    term->selstart.x = term->selstart.y = term->selend.x = term->selend.y = 0;
}

void term_deselect(Terminal *term)
{
    deselect(term);
    term_update(term);
}

int term_ldisc(Terminal *term, int option)
{
    if (option == LD_ECHO)
	return term->term_echoing;
    if (option == LD_EDIT)
	return term->term_editing;
    return FALSE;
}

int term_data(Terminal *term, int is_stderr, const char *data, int len)
{
    bufchain_add(&term->inbuf, data, len);

    if (!term->in_term_out) {
	term->in_term_out = TRUE;
	term_blink(term, 1);
	term_out(term);
	term->in_term_out = FALSE;
    }

    /*
     * term_out() always completely empties inbuf. Therefore,
     * there's no reason at all to return anything other than zero
     * from this function, because there _can't_ be a question of
     * the remote side needing to wait until term_out() has cleared
     * a backlog.
     *
     * This is a slightly suboptimal way to deal with SSH2 - in
     * principle, the window mechanism would allow us to continue
     * to accept data on forwarded ports and X connections even
     * while the terminal processing was going slowly - but we
     * can't do the 100% right thing without moving the terminal
     * processing into a separate thread, and that might hurt
     * portability. So we manage stdout buffering the old SSH1 way:
     * if the terminal processing goes slowly, the whole SSH
     * connection stops accepting data until it's ready.
     *
     * In practice, I can't imagine this causing serious trouble.
     */
    return 0;
}

void term_provide_logctx(Terminal *term, void *logctx)
{
    term->logctx = logctx;
}

/*
 * iso2022s.c - support for ISO-2022 subset encodings.
 * 
 * (The `s' suffix on the filename is there to leave `iso2022.c'
 * free for the unlikely event that I ever attempt to implement
 * _full_ ISO-2022 in this library!)
 */

#ifndef ENUM_CHARSETS

#include <stdio.h>
#include <assert.h>

#include "charset.h"
#include "internal.h"

#define SO (0x0E)
#define SI (0x0F)
#define ESC (0x1B)

/* Functional description of a single ISO 2022 escape sequence. */
struct iso2022_escape {
    char const *sequence;
    unsigned long andbits, xorbits;
    /*
     * For output, these variables help us figure out which escape
     * sequences we need to get where we want to be.
     */
    int container, subcharset;
};

struct iso2022 {
    /*
     * List of escape sequences supported in this subset. Must be
     * in ASCII order, so that we can narrow down the list as
     * necessary.
     */
    struct iso2022_escape *escapes;    /* must be sorted in ASCII order! */
    int nescapes;

    /*
     * We assign indices from 0 upwards to the sub-charsets of a
     * given ISO 2022 subset. nbytes[i] tells us how many bytes per
     * character are required by sub-charset i. (It's a string
     * mainly because that makes it easier to declare in C syntax
     * than an int array.)
     */
    char const *nbytes;

    /*
     * The characters in this string are indices-plus-one (so that
     * NUL can still terminate) of escape sequences in `escapes'.
     * These escapes are output in the given sequence to reset the
     * encoding state, unless it turns out that a given escape
     * would not change the state at all.
     */
    char const *reset;

    /*
     * For output, some ISO 2022 subsets _mandate_ an initial shift
     * sequence. If so, here it is so we can output it. (For the
     * sake of basic sanity we won't bother to _require_ it on
     * input, although it should of course be listed under
     * `escapes' above so that we ignore it when present.)
     */
    char const *initial_sequence;

    /*
     * Function calls to do the actual translation.
     */
    long int (*to_ucs)(int subcharset, unsigned long bytes);
    int (*from_ucs)(long int ucs, int *subcharset, unsigned long *bytes);
};

static void read_iso2022s(charset_spec const *charset, long int input_chr,
			  charset_state *state,
			  void (*emit)(void *ctx, long int output),
			  void *emitctx)
{
    struct iso2022 const *iso = (struct iso2022 *)charset->data;

    /*
     * For reading ISO-2022 subsets, we divide up our state
     * variables as follows:
     * 
     * 	- The top byte of s0 (bits 31:24) indicates, if nonzero,
     * 	  that we are part-way through a recognised ISO-2022 escape
     * 	  sequence. Five of those bits (31:27) give the index of
     * 	  the first member of the escapes list matching what we
     * 	  have so far; the remaining three (26:24) give the number
     * 	  of characters we have seen so far.
     * 
     * 	- The top nibble of s1 (bits 31:28) indicate which
     * 	  _container_ is currently selected. This isn't quite as
     * 	  simple as it sounds, since we have to preserve memory of
     * 	  which of the SI/SO containers we came from when we're
     * 	  temporarily in SS2/SS3. Hence, what happens is:
     *     + bit 28 indicates SI/SO.
     * 	   + if we're in an SS2/SS3 container, that's indicated by
     * 	     the three bits above that being nonzero and holding
     * 	     either 2 or 3.
     * 	   + Hence: 0 is SI, 1 is SO, 4 is SS2-from-SI, 5 is
     * 	     SS2-from-SO, 6 is SS3-from-SI, 7 is SS3-from-SO.
     * 
     * 	- The next nibble of s1 (27:24) indicates how many bytes
     * 	  have been accumulated in the current character.
     * 
     * 	- The remaining three bytes of s1 are divided into four
     * 	  six-bit sections, and each section gives the current
     * 	  sub-charset selected in one of the possible containers.
     * 	  (Those containers are SI, SO, SS2 and SS3, respectively
     * 	  and in order from the bottom of s0 to the top.)
     * 
     * 	- The bottom 24 bits of s0 give the accumulated character
     * 	  data so far.
     * 
     * (Note that this means s1 contains all the parts of the state
     * which might need to be operated on by escape sequences.
     * Cunning, eh?)
     */

    /*
     * So. Firstly, we process escape sequences, if we're in the
     * middle of one or if we see a possible introducer (SI, SO,
     * ESC).
     */
    if ((state->s0 >> 24) ||
	(input_chr == SO || input_chr == SI || input_chr == ESC)) {
	int n = (state->s0 >> 24) & 7, i = (state->s0 >> 27), oi = i, j;

	/*
	 * If this is the start of an escape sequence, we might be
	 * in mid-character. If so, clear the character state and
	 * emit an error token for the incomplete character.
	 */
	if (state->s1 & 0x0F000000) {
	    state->s1 &= ~0x0F000000;
	    state->s0 &= 0xFF000000;
	    /*
	     * If we were in the SS2 or SS3 container, we
	     * automatically exit it.
	     */
	    if (state->s1 & 0xE0000000)
		state->s1 &= 0x1FFFFFFF;
	    emit(emitctx, ERROR);
	}

	j = i;
	while (j < iso->nescapes &&
	       !memcmp(iso->escapes[j].sequence,
		       iso->escapes[oi].sequence, n)) {
	    if (iso->escapes[j].sequence[n] < input_chr)
		i = ++j;
	    else
		break;
	}
	if (i >= iso->nescapes ||
	    memcmp(iso->escapes[i].sequence,
		   iso->escapes[oi].sequence, n) ||
	    iso->escapes[i].sequence[n] != input_chr) {
	    /*
	     * This character does not appear in any valid escape
	     * sequence. Therefore, we must emit all the characters
	     * we had previously swallowed, plus this one, and
	     * return to non-escape-sequence state.
	     */
	    for (j = 0; j < n; j++)
		emit(emitctx, iso->escapes[oi].sequence[j]);
	    emit(emitctx, input_chr);
	    state->s0 = 0;
	    return;
	}

	/*
	 * Otherwise, we have found an additional character in our
	 * escape sequence. See if we have reached the _end_ of our
	 * sequence (and therefore must process the sequence).
	 */
	n++;
	if (!iso->escapes[i].sequence[n]) {
	    state->s0 = 0;
	    state->s1 &= iso->escapes[i].andbits;
	    state->s1 ^= iso->escapes[i].xorbits;
	    return;
	}

	/*
	 * Failing _that_, we simply update our escape-sequence-
	 * tracking state.
	 */
	assert(i < 32 && n < 8);
	state->s0 = (i << 27) | (n << 24);
	return;
    }

    /*
     * If this isn't an escape sequence, it must be part of a
     * character. One possibility is that it's a control character
     * (outside the space 21-7E), in which case we output it verbatim.
     */
    if (input_chr < 0x21 || input_chr > 0x7E) {
	/*
	 * We might be in mid-multibyte-character. If so, clear the
	 * character state and emit an error token for the
	 * incomplete character.
	 */
	if (state->s1 & 0x0F000000) {
	    state->s1 &= ~0x0F000000;
	    state->s0 &= 0xFF000000;
	    emit(emitctx, ERROR);
	    /*
	     * If we were in the SS2 or SS3 container, we
	     * automatically exit it.
	     */
	    if (state->s1 & 0xE0000000)
		state->s1 &= 0x1FFFFFFF;
	}

	emit(emitctx, input_chr);
	return;
    }

    /*
     * Otherwise, accumulate character data.
     */
    {
	unsigned long chr;
	int chrlen, cont, subcharset, bytes;

	/* The current character and its length. */
	chr = ((state->s0 & 0x00FFFFFF) << 8) | input_chr;
	chrlen = ((state->s1 >> 24) & 0xF) + 1;
	/* The current sub-charset. */
	cont = (state->s1 >> 28);
	if (cont > 1) cont >>= 1;
	subcharset = (state->s1 >> (6*cont)) & 0x3F;
	/* The number of bytes-per-character in that sub-charset. */
	bytes = iso->nbytes[subcharset];

	/*
	 * If this character is now complete, we convert and emit
	 * it. Otherwise, we simply update the state and return.
	 */
	if (chrlen >= bytes) {
	    emit(emitctx, iso->to_ucs(subcharset, chr));
	    chr = chrlen = 0;
	    /*
	     * If we were in the SS2 or SS3 container, we
	     * automatically exit it.
	     */
	    if (state->s1 & 0xE0000000)
		state->s1 &= 0x1FFFFFFF;
	}
	state->s0 = (state->s0 & 0xFF000000) | chr;
	state->s1 = (state->s1 & 0xF0FFFFFF) | (chrlen << 24);
    }
}

static void write_iso2022s(charset_spec const *charset, long int input_chr,
			   charset_state *state,
			   void (*emit)(void *ctx, long int output),
			   void *emitctx)
{
    struct iso2022 const *iso = (struct iso2022 *)charset->data;
    int subcharset, len, i, j, cont;
    unsigned long bytes;

    /*
     * For output, our s1 state variable contains most of the same
     * stuff as it did for input - current container, and current
     * subcharset selected in each container.
     */

    if (input_chr == -1) {
	unsigned long oldstate;
	int k;

	/*
	 * Special case: reset encoding state.
	 */
	for (i = 0; iso->reset[i]; i++) {
	    j = iso->reset[i] - 1;
	    oldstate = state->s1;
	    state->s1 &= iso->escapes[j].andbits;
	    state->s1 ^= iso->escapes[j].xorbits;
	    if (state->s1 != oldstate) {
		/* We must actually emit this sequence. */
		for (k = 0; iso->escapes[j].sequence[k]; k++)
		    emit(emitctx, iso->escapes[j].sequence[k]);
	    }
	}

	return;
    }

    /*
     * Analyse the character and find out what subcharset it needs
     * to go in.
     */
    if (!iso->from_ucs(input_chr, &subcharset, &bytes)) {
	emit(emitctx, ERROR);
	return;
    }

    /*
     * Now begins the fun. We now know what subcharset we want. So
     * we must find out which container we should select it into,
     * select it into it if necessary, select that _container_ if
     * necessary, and then output the given bytes.
     */
    for (i = 0; i < iso->nescapes; i++)
	if (iso->escapes[i].subcharset == subcharset)
	    break;
    assert(i < iso->nescapes);

    /*
     * We've found the escape sequence which would select this
     * subcharset into a container. However, that subcharset might
     * already _be_ selected in that container! Check before we go
     * to the effort of emitting the sequence.
     */
    cont = iso->escapes[i].container;
    if (((state->s1 >> (6*cont)) & 0x3F) != subcharset) {
	for (j = 0; iso->escapes[i].sequence[j]; j++)
	    emit(emitctx, iso->escapes[i].sequence[j]);
	state->s1 &= iso->escapes[i].andbits;
	state->s1 ^= iso->escapes[i].xorbits;
    }

    /*
     * Now we know what container our subcharset is in, so we want
     * to select that container.
     */
    if (cont > 1) {
	/* SS2 or SS3; just output the sequence and be done. */
	emit(emitctx, ESC);
	emit(emitctx, 'L' + cont);     /* comes out to 'N' or 'O' */
    } else {
	/* Emit SI or SO, but only if the current container isn't already
	 * the right one. */
	if ((state->s1 >> 28) != cont) {
	    emit(emitctx, cont ? SO : SI);
	    state->s1 = (state->s1 & 0x0FFFFFFF) & (cont << 28);
	}
    }

    /*
     * We're done. Subcharset is selected in container, container
     * is selected. All we need now is to write out the bytes.
     */
    len = iso->nbytes[subcharset];
    while (len--)
	emit(emitctx, (bytes >> (8*len)) & 0xFF);

}

static long int iso2022jp_to_ucs(int subcharset, unsigned long bytes)
{
    switch (subcharset) {
      case 0: return bytes;	       /* one-byte ASCII */
      case 1:			       /* JIS X 0201 half-width katakana */
	if (bytes >= 0x21 && bytes <= 0x5F)
	    return bytes + (0xFF61 - 0x21);
	else
	    return ERROR;
	/* (no break needed since all control paths have returned) */
      case 2: return jisx0208_to_unicode(((bytes >> 8) & 0xFF) - 0x21,
					 ((bytes     ) & 0xFF) - 0x21);
      default: return ERROR;
    }
}
static int iso2022jp_from_ucs(long int ucs, int *subcharset,
			      unsigned long *bytes)
{
    int r, c;
    if (ucs < 0x80) {
	*subcharset = 0;
	*bytes = ucs;
	return 1;
    } else if (ucs >= 0xFF61 && ucs <= 0xFF9F) {
	*subcharset = 1;
	*bytes = ucs - (0xFF61 - 0x21);
	return 1;
    } else if (unicode_to_jisx0208(ucs, &r, &c)) {
	*subcharset = 2;
	*bytes = ((r+0x21) << 8) | (c+0x21);
	return 1;
    } else {
	return 0;
    }
}
static struct iso2022_escape iso2022jp_escapes[] = {
    {"\033$@", 0xFFFFFFC0, 0x00000002, -1, -1},   /* we ignore this one */
    {"\033$B", 0xFFFFFFC0, 0x00000002, 0, 2},
    {"\033(B", 0xFFFFFFC0, 0x00000000, 0, 0},
    {"\033(J", 0xFFFFFFC0, 0x00000001, 0, 1},
};
static struct iso2022 iso2022jp = {
    iso2022jp_escapes, lenof(iso2022jp_escapes),
    "\1\1\2", "\3", NULL, iso2022jp_to_ucs, iso2022jp_from_ucs
};
const charset_spec charset_CS_ISO2022_JP = {
    CS_ISO2022_JP, read_iso2022s, write_iso2022s, &iso2022jp
};

#else /* ENUM_CHARSETS */

ENUM_CHARSET(CS_ISO2022_JP)

#endif /* ENUM_CHARSETS */

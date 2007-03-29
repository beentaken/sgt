/* filigram.c - draw `filigram' pictures, based on the idea of plotting
 * the fractional part of a polynomial in x and y over a pixel grid
 *
 * This program is copyright 2000 Simon Tatham.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "misc.h"

#include "bmpwrite.h"

#include "cmdline.h"

#define VERSION "$Revision$"

/* ----------------------------------------------------------------------
 * Function prototypes and structure type predeclarations.
 */

struct Poly;
static struct Poly *polyread(char const *string);
static struct Poly *polypdiff(struct Poly *input, int wrtx);
static double polyeval(struct Poly *p, double x, double y);
static void polyfree(struct Poly *p);

struct Colours;
static struct Colours *colread(char const *string);
static void colfree(struct Colours *cols);
static struct RGB colfind(struct Colours *cols, int xg, int yg);

/* ----------------------------------------------------------------------
 * Routines for handling polynomials.
 */

struct PolyTerm {
    int constant;
    int xpower;
    int ypower;
    struct PolyTerm *next;
};

/*
 * A polynomial is stored as a 2-D array of PolyTerms, going up to
 * `maxdegree' in both cases. The nonzero terms are linked as a
 * list once the polynomial is finalised.
 */
struct Poly {
    int deg;
    struct PolyTerm *head;
    struct PolyTerm *terms;
};

/*
 * Build the linked list in a Poly structure.
 */
static void polymklist(struct Poly *p) {
    struct PolyTerm **tail;
    int i;
    tail = &p->head;
    for (i = 0; i < p->deg * p->deg; i++)
        if (p->terms[i].constant != 0) {
            *tail = &p->terms[i];
            tail = &p->terms[i].next;
        }
    *tail = NULL;
}

/*
 * Parse a string into a polynomial.
 *
 * String syntax must be: a number of terms, separated by + or -.
 * (Whitespace is fine, and is skipped.) Each term consists of an
 * optional integer, followed by an optional x power (x followed by
 * an integer), followed by an optional y power. For example:
 * 
 *   x2+2xy+y2
 * 
 * TODO if I ever get the energy: improve this to be a general
 * expression evaluator.
 */
static struct Poly *polyread(char const *string) {
    struct Poly *output;
    int sign, factor, xpower, ypower;
    int i, j, k;

    output = (struct Poly *)malloc(sizeof(struct Poly));
    output->deg = 0;
    output->terms = NULL;

    while (*string) {
        sign = +1;
        factor = 1;
        xpower = 0;
        ypower = 0;
        if (*string == '-') {
            sign = -1;
            string++;
        }
        i = strspn(string, "0123456789");
        if (i) {
            factor = atoi(string);
            string += i;
        }
        if (*string == 'x') {
            string++;
            xpower = 1;
            i = strspn(string, "0123456789");
            if (i) {
                xpower = atoi(string);
                string += i;
            }
        }
        if (*string == 'y') {
            string++;
            ypower = 1;
            i = strspn(string, "0123456789");
            if (i) {
                ypower = atoi(string);
                string += i;
            }
        }

        i = xpower;
        if (i < ypower) i = ypower;
        i++;
        if (i > output->deg) {
            int old;
            struct PolyTerm *newterms;

            old = output->deg;
            newterms = (struct PolyTerm *)malloc(i*i*sizeof(struct PolyTerm));
            for (j = 0; j < i; j++)
                for (k = 0; k < i; k++) {
                    newterms[k*i+j].xpower = j;
                    newterms[k*i+j].ypower = k;
                    if (j<old && k<old)
                        newterms[k*i+j].constant =
                        output->terms[k*old+j].constant;
                    else
                        newterms[k*i+j].constant = 0;
                }
            if (output->terms)
                free(output->terms);
            output->terms = newterms;
            output->deg = i;
        }
        output->terms[ypower*output->deg+xpower].constant += sign*factor;

        if (*string == '-') {
            /* do nothing */
        } else if (*string == '+') {
            string++;                  /* skip the plus */
        } else if (*string) {
            polyfree(output);
            return NULL;               /* error! */
        }
    }

    polymklist(output);
    return output;
}

/*
 * Partially differentiate a polynomial with respect to x (if
 * wrtx!=0) or to y (if wrtx==0).
 */
static struct Poly *polypdiff(struct Poly *input, int wrtx) {
    struct Poly *output;
    int deg, yp, xp, ydp, xdp, factor, constant;

    output = (struct Poly *)malloc(sizeof(struct Poly));
    deg = output->deg = input->deg;
    output->terms = (struct PolyTerm *)malloc(output->deg * output->deg *
                                              sizeof(struct PolyTerm));

    for (yp = 0; yp < deg; yp++)
        for (xp = 0; xp < deg; xp++) {
            xdp = xp; ydp = yp;
            factor = wrtx ? ++xdp : ++ydp;
            if (xdp >= deg || ydp >= deg)
                constant = 0;
            else
                constant = input->terms[ydp*deg+xdp].constant;
            output->terms[yp*deg+xp].xpower = xp;
            output->terms[yp*deg+xp].ypower = yp;
            output->terms[yp*deg+xp].constant = constant * factor;
        }

    polymklist(output);
    return output;
}

/*
 * Evaluate a polynomial.
 * 
 * TODO: make this more efficient by pre-computing the powers once
 * instead of doing them all, linearly, per term.
 */
static double polyeval(struct Poly *p, double x, double y) {
    struct PolyTerm *pt;
    double result, term;
    int i;

    result = 0.0;
    for (pt = p->head; pt; pt = pt->next) {
        term = pt->constant;
        for (i = 0; i < pt->xpower; i++) term *= x;
        for (i = 0; i < pt->ypower; i++) term *= y;
        result += term;
    }
    return result;
}

/*
 * Free a polynomial when finished.
 */
static void polyfree(struct Poly *p) {
    free(p->terms);
    free(p);
}

/* ----------------------------------------------------------------------
 * Routines for handling lists of colours.
 */

struct Colours {
    int xmod;
    int ncolours;
    struct RGB *colours;
};

/*
 * Read a colour list into a Colours structure.
 */
static struct Colours *colread(char const *string) {
    struct Colours *ret;
    int i;
    struct RGB c, *clist;
    int nc, csize;
    char *q;

    ret = (struct Colours *)malloc(sizeof(struct Colours));
    ret->xmod = ret->ncolours = 0;
    ret->colours = NULL;
    nc = csize = 0;
    clist = NULL;

    /* If there's an `integer+' prefix, set xmod. */
    i = strspn(string, "0123456789");
    if (string[i] == '+') {
	ret->xmod = atoi(string);
	string += i + 1;
    }

    while (*string) {
	/* Now we expect triplets of doubles separated by any punctuation. */
	c.r = strtod(string, &q); string = q; if (!*string) break; string++;
	c.g = strtod(string, &q); string = q; if (!*string) break; string++;
	c.b = strtod(string, &q); string = q;
	if (nc >= csize) {
	    csize = nc + 16;
	    clist = realloc(clist, csize * sizeof(struct RGB));
	}
	clist[nc++] = c;
	if (!*string) break; string++;
    }
    ret->ncolours = nc;
    ret->colours = clist;
    return ret;
}

/*
 * Free a Colours structure when done.
 */
static void colfree(struct Colours *cols) {
    free(cols->colours);
    free(cols);
}

/*
 * Look up a colour in a Colours structure.
 */
static struct RGB colfind(struct Colours *cols, int xg, int yg) {
    int n;
    if (cols->xmod) {
	xg %= cols->xmod;
        if (xg < 0) xg += cols->xmod;
	yg *= cols->xmod;
    }
    n = (xg+yg) % cols->ncolours;
    if (n < 0) n += cols->ncolours;
    return cols->colours[n];
}

/* ----------------------------------------------------------------------
 * The code to do the actual plotting.
 */

struct Params {
    int width, height;
    double x0, x1, y0, y1;
    double xscale, yscale, oscale;
    int fading;
    char const *filename;
    int outtype;
    struct Poly *poly;
    struct Colours *colours;
};

static int toint(double d) {
    int i = (int)d;
    if (i <= 0) {
        i = (int)(d + -i + 1) - (-i + 1);
    }
    return i;
}

static int plot(struct Params params) {
    struct Poly *dfdx, *dfdy;
    struct Bitmap *bm;

    double xstep, ystep;
    double x, xfrac, y, yfrac;
    double dzdx, dzdy, dxscale, dyscale;
    double z, xfade, yfade, fade;
    struct RGB c;
    int ii, i, j, xg, yg;

    dfdx = polypdiff(params.poly, 1);
    dfdy = polypdiff(params.poly, 0);

    bm = bmpinit(params.filename, params.width, params.height, params.outtype);

    xstep = (params.x1 - params.x0) / params.width;
    ystep = (params.y1 - params.y0) / params.height;
    dxscale = params.xscale * params.oscale;
    dyscale = params.yscale * params.oscale;
    for (ii = 0; ii < params.height; ii++) {
	if (params.outtype == BMP)
	    i = ii;
	else
	    i = params.height-1 - ii;
        y = params.y0 + ystep * i;
        yfrac = y / params.yscale; yfrac -= toint(yfrac);

        for (j = 0; j < params.width; j++) {
            x = params.x0 + xstep * j;
            xfrac = x / params.xscale; xfrac -= toint(xfrac);

            dzdx = polyeval(dfdx, x, y) * dxscale;
            dzdy = polyeval(dfdy, x, y) * dyscale;
            xg = toint(dzdx + 0.5); dzdx -= xg;
            yg = toint(dzdy + 0.5); dzdy -= yg;

            z = polyeval(params.poly, x, y) * params.oscale;
            z -= xg * xfrac;
            z -= yg * yfrac;
            z -= toint(z);

            xfade = dzdx; if (xfade < 0) xfade = -xfade;
            yfade = dzdy; if (yfade < 0) yfade = -yfade;
            fade = 1.0 - (xfade < yfade ? yfade : xfade) * 2;

	    c = colfind(params.colours, xg, yg);

	    if (params.fading) z *= fade;
	    z *= 256.0;

            bmppixel(bm, toint(c.r*z), toint(c.g*z), toint(c.b*z));
        }
        bmpendrow(bm);
    }

    bmpclose(bm);
    polyfree(dfdy);
    polyfree(dfdx);
    return 1;
}

/* ----------------------------------------------------------------------
 * Main program: parse the command line and call plot() when satisfied.
 */

int parsepoly(char *string, void *vret) {
    struct Poly **ret = (struct Poly **)vret;
    *ret = polyread(string);
    if (!*ret)
	return 0;
    return 1;
}

int parsecols(char *string, void *vret) {
    struct Colours **ret = (struct Colours **)vret;
    *ret = colread(string);
    if (!*ret)
	return 0;
    return 1;
}

int main(int argc, char **argv) {
    struct Params par;
    int i;
    double aspect;

    struct options {
	char *outfile;
	int outppm;
	struct Size imagesize, basesize;
	double xcentre, ycentre, xrange, yrange, iscale, oscale;
	int gotxcentre, gotycentre, gotxrange, gotyrange, gotiscale, gotoscale;
	int fade, isbase, verbose;
	struct Poly *poly;
	struct Colours *colours;
    } optdata = {
	NULL, FALSE, {0,0}, {0,0},
	0, 0, 0, 0, 0, 0,
	FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
	FALSE, FALSE, FALSE, NULL, NULL
    };

    static const struct Cmdline options[] = {
	{1, "--output", 'o', "file.bmp", "output bitmap name",
		"filename", parsestr, offsetof(struct options, outfile), -1},
        {1, "--ppm", 0, NULL, "output PPM rather than BMP",
		NULL, NULL, -1, offsetof(struct options, outppm)},
	{1, "--size", 's', "NNNxNNN", "output bitmap size",
		"output bitmap size", parsesize, offsetof(struct options, imagesize), -1},
	{1, "--xrange", 'x', "NNN", "mathematical x range",
		"x range", parseflt, offsetof(struct options, xrange), offsetof(struct options, gotxrange)},
	{1, "--yrange", 'y', "NNN", "mathematical y range",
		"y range", parseflt, offsetof(struct options, yrange), offsetof(struct options, gotyrange)},
	{2, "--xcentre\0--xcenter", 'X', "NNN", "mathematical x centre point",
		"x centre", parseflt, offsetof(struct options, xcentre), offsetof(struct options, gotxcentre)},
	{2, "--ycentre\0--ycenter", 'Y', "NNN", "mathematical y centre point",
		"y centre", parseflt, offsetof(struct options, ycentre), offsetof(struct options, gotycentre)},
	{1, "--basesize", 'b', "NNNxNNN", "base image size",
		"base image size", parsesize, offsetof(struct options, basesize), -1},
	{1, "--base", 'B', NULL, "this *is* the base image (default)",
		NULL, NULL, -1, offsetof(struct options, isbase)},
	{1, "--iscale", 'I', "NNN", "input scale (ie base pixel spacing)",
		"input scale", parseflt, offsetof(struct options, iscale), offsetof(struct options, gotiscale)},
	{1, "--oscale", 'O', "NNN", "output scale (ie modulus)",
		"output scale", parseflt, offsetof(struct options, oscale), offsetof(struct options, gotoscale)},
	{1, "--fade", 'f', NULL, "turn on fading at edges of patches",
		NULL, NULL, -1, offsetof(struct options, fade)},
	{1, "--poly", 'p', "x2+2xy+y2", "polynomial to plot",
		"polynomial", parsepoly, offsetof(struct options, poly), -1},
	{2, "--colours\0--colors", 'c', "1,0,0:0,1,0", "colours for patches",
		"colour specification", parsecols, offsetof(struct options, colours), -1},
	{1, "--verbose", 'v', NULL, "report details of what is done",
		NULL, NULL, -1, offsetof(struct options, verbose)},
    };

    char *usageextra[] = {
	" - at most one of -b, -B and -I should be used",
	" - can give 0 as one dimension in -s or -b and that dimension will",
	"   be computed so as to preserve the aspect ratio",
	" - if only one of -x and -y specified, will compute the other so",
	"   as to preserve the aspect ratio",
	" - colours can be prefixed with `N+' to get two-dimensional colour",
	"   selection (this needs to be better explained :-)"
    };

    parse_cmdline("filigram", argc, argv, options, lenof(options), &optdata);

    if (argc < 2)
	usage_message("filigram [options]",
		      options, lenof(options),
		      usageextra, lenof(usageextra));

    /*
     * Having read the arguments, now process them.
     */

    /* If no output scale, default to 1. */
    if (!optdata.gotoscale)
	par.oscale = 1.0;
    else
	par.oscale = optdata.oscale;

    /* If no output file, complain. */
    if (!optdata.outfile) {
	fprintf(stderr, "filigram: no output file specified: "
		"use something like `-o file.bmp'\n");
	return EXIT_FAILURE;
    } else
	par.filename = optdata.outfile;
    par.outtype = (optdata.outppm ? PPM : BMP);

    /* If no polynomial, complain. */
    if (!optdata.poly) {
	fprintf(stderr, "filigram: no polynomial specified: "
		"use something like `-p x2+2xy+y2'\n");
	return EXIT_FAILURE;
    } else
	par.poly = optdata.poly;

    /* If no colours, default to 1,1,1. */
    if (!optdata.colours)
	par.colours = colread("1,1,1");   /* assume this will succeed */
    else
	par.colours = optdata.colours;

    par.fading = optdata.fade;

    /*
     * If precisely one explicit aspect ratio specified, use it
     * to fill in blanks in other sizes.
     */
    aspect = 0;
    if (optdata.imagesize.w && optdata.imagesize.h) {
	aspect = (double)optdata.imagesize.w / optdata.imagesize.h;
    }
    if (optdata.basesize.w && optdata.basesize.h) {
	double newaspect = (double)optdata.basesize.w / optdata.basesize.h;
	if (newaspect != aspect)
	    aspect = -1;
	else
	    aspect = newaspect;
    }
    if (optdata.gotxrange && optdata.gotyrange) {
	double newaspect = optdata.xrange / optdata.yrange;
	if (newaspect != aspect)
	    aspect = -1;
	else
	    aspect = newaspect;
    }

    if (aspect > 0) {
	if (optdata.imagesize.w && !optdata.imagesize.h) optdata.imagesize.h = optdata.imagesize.w / aspect;
	if (!optdata.imagesize.w && optdata.imagesize.h) optdata.imagesize.w = optdata.imagesize.h * aspect;
	if (optdata.basesize.w && !optdata.basesize.h) optdata.basesize.h = optdata.basesize.w / aspect;
	if (!optdata.basesize.w && optdata.basesize.h) optdata.basesize.w = optdata.basesize.h * aspect;
	if (optdata.gotxrange && !optdata.gotyrange)
	    optdata.yrange = optdata.xrange / aspect, optdata.gotyrange = TRUE;
	if (!optdata.gotxrange && optdata.gotyrange)
	    optdata.xrange = optdata.yrange * aspect, optdata.gotxrange = TRUE;
    }

    /*
     * Now complain if no output image size was specified.
     */
    if (!optdata.imagesize.w || !optdata.imagesize.h) {
	fprintf(stderr, "filigram: no output size specified: "
		"use something like `-s 640x480'\n");
	return EXIT_FAILURE;
    } else {
	par.width = optdata.imagesize.w;
	par.height = optdata.imagesize.h;
    }

    /*
     * Also complain if no input mathematical extent specified.
     */
    if (!optdata.gotxrange || !optdata.gotyrange) {
	fprintf(stderr, "filigram: no image extent specified: "
		"use something like `-x 20 -y 15'\n");
	return EXIT_FAILURE;
    } else {
	if (!optdata.gotxcentre) optdata.xcentre = 0.0;
	if (!optdata.gotycentre) optdata.ycentre = 0.0;
	par.x0 = optdata.xcentre - optdata.xrange;
	par.x1 = optdata.xcentre + optdata.xrange;
	par.y0 = optdata.ycentre - optdata.yrange;
	par.y1 = optdata.ycentre + optdata.yrange;
    }

    /*
     * All that's left to set up is xscale and yscale. At this
     * stage we expect to see at most one of
     *   `optdata.isbase' true
     *   `optdata.basesize.w' and `optdata.basesize.h' both nonzero
     *   `optdata.gotiscale' true
     */
    i = (!!optdata.isbase) + (optdata.basesize.w && optdata.basesize.h) + (!!optdata.gotiscale);
    if (i > 1) {
	fprintf(stderr, "filigram: "
		"expected at most one of `-b', `-B', `-I'\n");
	return EXIT_FAILURE;
    } else if (i == 0) {
	/* Default: optdata.isbase is true. */
	optdata.isbase = TRUE;
    }
    if (optdata.isbase) {
	optdata.basesize = optdata.imagesize;
    }
    if (optdata.gotiscale)
	par.xscale = par.yscale = optdata.iscale;
    else {
	par.xscale = (par.x1 - par.x0) / optdata.basesize.w;
	par.yscale = (par.y1 - par.y0) / optdata.basesize.h;
    }

    /*
     * If we're in verbose mode, regurgitate the final
     * parameters.
     */
    if (optdata.verbose) {
	printf("Output file `%s', %d x %d\n",
	       par.filename, par.width, par.height);
	printf("Mathematical extent [%g,%g] x [%g,%g]\n",
	       par.x0, par.x1, par.y0, par.y1);
	printf("Base pixel spacing %g (horiz), %g (vert)\n",
	       par.xscale, par.yscale);
	printf("Output modulus %g\n",
	       par.oscale);
    }

    i = plot(par) ? EXIT_SUCCESS : EXIT_FAILURE;

    colfree(par.colours);
    polyfree(par.poly);
    return i;
}

/*
 * Unified font management for GTK.
 * 
 * PuTTY is willing to use both old-style X server-side bitmap
 * fonts _and_ GTK2/Pango client-side fonts. This requires us to
 * do a bit of work to wrap the two wildly different APIs into
 * forms the rest of the code can switch between seamlessly, and
 * also requires a custom font selector capable of handling both
 * types of font.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "putty.h"
#include "gtkfont.h"
#include "tree234.h"

/*
 * TODO on fontsel
 * ---------------
 * 
 *  - implement the preview pane
 * 
 *  - extend the font style language for X11 fonts so that we
 *    never get unexplained double size elements? Or, at least, so
 *    that _my_ font collection never produces them; that'd be a
 *    decent start.
 * 
 *  - decide what _should_ happen about font aliases. Should we
 *    resolve them as soon as they're clicked? Or be able to
 *    resolve them on demand, er, somehow? Or resolve them on exit
 *    from the function? Or what? If we resolve on demand, should
 *    we stop canonifying them on input, on the basis that we'd
 *    prefer to let the user _tell_ us when to canonify them?
 * 
 *  - think about points versus pixels, harder than I already have
 * 
 *  - work out why the list boxes don't go all the way to the RHS
 *    of the dialog box
 * 
 *  - big testing and shakedown!
 */

/*
 * Future work:
 * 
 *  - all the GDK font functions used in the x11font subclass are
 *    deprecated, so one day they may go away. When this happens -
 *    or before, if I'm feeling proactive - it oughtn't to be too
 *    difficult in principle to convert the whole thing to use
 *    actual Xlib font calls.
 * 
 *  - it would be nice if we could move the processing of
 *    underline and VT100 double width into this module, so that
 *    instead of using the ghastly pixmap-stretching technique
 *    everywhere we could tell the Pango backend to scale its
 *    fonts to double size properly and at full resolution.
 *    However, this requires me to learn how to make Pango stretch
 *    text to an arbitrary aspect ratio (for double-width only
 *    text, which perversely is harder than DW+DH), and right now
 *    I haven't the energy.
 */

/*
 * Ad-hoc vtable mechanism to allow font structures to be
 * polymorphic.
 * 
 * Any instance of `unifont' used in the vtable functions will
 * actually be the first element of a larger structure containing
 * data specific to the subtype. This is permitted by the ISO C
 * provision that one may safely cast between a pointer to a
 * structure and a pointer to its first element.
 */

#define FONTFLAG_CLIENTSIDE    0x0001
#define FONTFLAG_SERVERSIDE    0x0002
#define FONTFLAG_SERVERALIAS   0x0004
#define FONTFLAG_NONMONOSPACED 0x0008

typedef void (*fontsel_add_entry)(void *ctx, const char *realfontname,
				  const char *family, const char *charset,
				  const char *style, const char *stylekey,
				  int size, int flags,
				  const struct unifont_vtable *fontclass);

struct unifont_vtable {
    /*
     * `Methods' of the `class'.
     */
    unifont *(*create)(GtkWidget *widget, const char *name, int wide, int bold,
		       int shadowoffset, int shadowalways);
    void (*destroy)(unifont *font);
    void (*draw_text)(GdkDrawable *target, GdkGC *gc, unifont *font,
		      int x, int y, const char *string, int len, int wide,
		      int bold, int cellwidth);
    void (*enum_fonts)(GtkWidget *widget,
		       fontsel_add_entry callback, void *callback_ctx);
    char *(*canonify_fontname)(GtkWidget *widget, const char *name, int *size);
    char *(*scale_fontname)(GtkWidget *widget, const char *name, int size);

    /*
     * `Static data members' of the `class'.
     */
    const char *prefix;
};

/* ----------------------------------------------------------------------
 * GDK-based X11 font implementation.
 */

static void x11font_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
			      int x, int y, const char *string, int len,
			      int wide, int bold, int cellwidth);
static unifont *x11font_create(GtkWidget *widget, const char *name,
			       int wide, int bold,
			       int shadowoffset, int shadowalways);
static void x11font_destroy(unifont *font);
static void x11font_enum_fonts(GtkWidget *widget,
			       fontsel_add_entry callback, void *callback_ctx);
static char *x11font_canonify_fontname(GtkWidget *widget, const char *name,
				       int *size);
static char *x11font_scale_fontname(GtkWidget *widget, const char *name,
				    int size);

struct x11font {
    struct unifont u;
    /*
     * Actual font objects. We store a number of these, for
     * automatically guessed bold and wide variants.
     * 
     * The parallel array `allocated' indicates whether we've
     * tried to fetch a subfont already (thus distinguishing NULL
     * because we haven't tried yet from NULL because we tried and
     * failed, so that we don't keep trying and failing
     * subsequently).
     */
    GdkFont *fonts[4];
    int allocated[4];
    /*
     * `sixteen_bit' is true iff the font object is indexed by
     * values larger than a byte. That is, this flag tells us
     * whether we use gdk_draw_text_wc() or gdk_draw_text().
     */
    int sixteen_bit;
    /*
     * Data passed in to unifont_create().
     */
    int wide, bold, shadowoffset, shadowalways;
};

static const struct unifont_vtable x11font_vtable = {
    x11font_create,
    x11font_destroy,
    x11font_draw_text,
    x11font_enum_fonts,
    x11font_canonify_fontname,
    x11font_scale_fontname,
    "server"
};

char *x11_guess_derived_font_name(GdkFont *font, int bold, int wide)
{
    XFontStruct *xfs = GDK_FONT_XFONT(font);
    Display *disp = GDK_FONT_XDISPLAY(font);
    Atom fontprop = XInternAtom(disp, "FONT", False);
    unsigned long ret;
    if (XGetFontProperty(xfs, fontprop, &ret)) {
	char *name = XGetAtomName(disp, (Atom)ret);
	if (name && name[0] == '-') {
	    char *strings[13];
	    char *dupname, *extrafree = NULL, *ret;
	    char *p, *q;
	    int nstr;

	    p = q = dupname = dupstr(name); /* skip initial minus */
	    nstr = 0;

	    while (*p && nstr < lenof(strings)) {
		if (*p == '-') {
		    *p = '\0';
		    strings[nstr++] = p+1;
		}
		p++;
	    }

	    if (nstr < lenof(strings))
		return NULL;	       /* XLFD was malformed */

	    if (bold)
		strings[2] = "bold";

	    if (wide) {
		/* 4 is `wideness', which obviously may have changed. */
		/* 5 is additional style, which may be e.g. `ja' or `ko'. */
		strings[4] = strings[5] = "*";
		strings[11] = extrafree = dupprintf("%d", 2*atoi(strings[11]));
	    }

	    ret = dupcat("-", strings[ 0], "-", strings[ 1], "-", strings[ 2],
			 "-", strings[ 3], "-", strings[ 4], "-", strings[ 5],
			 "-", strings[ 6], "-", strings[ 7], "-", strings[ 8],
			 "-", strings[ 9], "-", strings[10], "-", strings[11],
			 "-", strings[12], NULL);
	    sfree(extrafree);
	    sfree(dupname);

	    return ret;
	}
    }
    return NULL;
}

static int x11_font_width(GdkFont *font, int sixteen_bit)
{
    if (sixteen_bit) {
	XChar2b space;
	space.byte1 = 0;
	space.byte2 = ' ';
	return gdk_text_width(font, (const gchar *)&space, 2);
    } else {
	return gdk_char_width(font, ' ');
    }
}

static unifont *x11font_create(GtkWidget *widget, const char *name,
			       int wide, int bold,
			       int shadowoffset, int shadowalways)
{
    struct x11font *xfont;
    GdkFont *font;
    XFontStruct *xfs;
    Display *disp;
    Atom charset_registry, charset_encoding;
    unsigned long registry_ret, encoding_ret;
    int pubcs, realcs, sixteen_bit;
    int i;

    font = gdk_font_load(name);
    if (!font)
	return NULL;

    xfs = GDK_FONT_XFONT(font);
    disp = GDK_FONT_XDISPLAY(font);

    charset_registry = XInternAtom(disp, "CHARSET_REGISTRY", False);
    charset_encoding = XInternAtom(disp, "CHARSET_ENCODING", False);

    pubcs = realcs = CS_NONE;
    sixteen_bit = FALSE;

    if (XGetFontProperty(xfs, charset_registry, &registry_ret) &&
	XGetFontProperty(xfs, charset_encoding, &encoding_ret)) {
	char *reg, *enc;
	reg = XGetAtomName(disp, (Atom)registry_ret);
	enc = XGetAtomName(disp, (Atom)encoding_ret);
	if (reg && enc) {
	    char *encoding = dupcat(reg, "-", enc, NULL);
	    pubcs = realcs = charset_from_xenc(encoding);

	    /*
	     * iso10646-1 is the only wide font encoding we
	     * support. In this case, we expect clients to give us
	     * UTF-8, which this module must internally convert
	     * into 16-bit Unicode.
	     */
	    if (!strcasecmp(encoding, "iso10646-1")) {
		sixteen_bit = TRUE;
		pubcs = realcs = CS_UTF8;
	    }

	    /*
	     * Hack for X line-drawing characters: if the primary
	     * font is encoded as ISO-8859-1, and has valid glyphs
	     * in the first 32 char positions, it is assumed that
	     * those glyphs are the VT100 line-drawing character
	     * set.
	     * 
	     * Actually, we'll hack even harder by only checking
	     * position 0x19 (vertical line, VT100 linedrawing
	     * `x'). Then we can check it easily by seeing if the
	     * ascent and descent differ.
	     */
	    if (pubcs == CS_ISO8859_1) {
		int lb, rb, wid, asc, desc;
		gchar text[2];

		text[1] = '\0';
		text[0] = '\x12';
		gdk_string_extents(font, text, &lb, &rb, &wid, &asc, &desc);
		if (asc != desc)
		    realcs = CS_ISO8859_1_X11;
	    }

	    sfree(encoding);
	}
    }

    xfont = snew(struct x11font);
    xfont->u.vt = &x11font_vtable;
    xfont->u.width = x11_font_width(font, sixteen_bit);
    xfont->u.ascent = font->ascent;
    xfont->u.descent = font->descent;
    xfont->u.height = xfont->u.ascent + xfont->u.descent;
    xfont->u.public_charset = pubcs;
    xfont->u.real_charset = realcs;
    xfont->fonts[0] = font;
    xfont->allocated[0] = TRUE;
    xfont->sixteen_bit = sixteen_bit;
    xfont->wide = wide;
    xfont->bold = bold;
    xfont->shadowoffset = shadowoffset;
    xfont->shadowalways = shadowalways;

    for (i = 1; i < lenof(xfont->fonts); i++) {
	xfont->fonts[i] = NULL;
	xfont->allocated[i] = FALSE;
    }

    return (unifont *)xfont;
}

static void x11font_destroy(unifont *font)
{
    struct x11font *xfont = (struct x11font *)font;
    int i;

    for (i = 0; i < lenof(xfont->fonts); i++)
	if (xfont->fonts[i])
	    gdk_font_unref(xfont->fonts[i]);
    sfree(font);
}

static void x11_alloc_subfont(struct x11font *xfont, int sfid)
{
    char *derived_name = x11_guess_derived_font_name
	(xfont->fonts[0], sfid & 1, !!(sfid & 2));
    xfont->fonts[sfid] = gdk_font_load(derived_name);   /* may be NULL */
    xfont->allocated[sfid] = TRUE;
    sfree(derived_name);
}

static void x11font_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
			      int x, int y, const char *string, int len,
			      int wide, int bold, int cellwidth)
{
    struct x11font *xfont = (struct x11font *)font;
    int sfid;
    int shadowbold = FALSE;

    wide -= xfont->wide;
    bold -= xfont->bold;

    /*
     * Decide which subfont we're using, and whether we have to
     * use shadow bold.
     */
    if (xfont->shadowalways && bold) {
	shadowbold = TRUE;
	bold = 0;
    }
    sfid = 2 * wide + bold;
    if (!xfont->allocated[sfid])
	x11_alloc_subfont(xfont, sfid);
    if (bold && !xfont->fonts[sfid]) {
	bold = 0;
	shadowbold = TRUE;
	sfid = 2 * wide + bold;
	if (!xfont->allocated[sfid])
	    x11_alloc_subfont(xfont, sfid);
    }

    if (!xfont->fonts[sfid])
	return;			       /* we've tried our best, but no luck */

    if (xfont->sixteen_bit) {
	/*
	 * This X font has 16-bit character indices, which means
	 * we expect our string to have been passed in UTF-8.
	 */
	XChar2b *xcs;
	wchar_t *wcs;
	int nchars, maxchars, i;

	/*
	 * Convert the input string to wide-character Unicode.
	 */
	maxchars = 0;
	for (i = 0; i < len; i++)
	    if ((unsigned char)string[i] <= 0x7F ||
		(unsigned char)string[i] >= 0xC0)
		maxchars++;
	wcs = snewn(maxchars+1, wchar_t);
	nchars = charset_to_unicode((char **)&string, &len, wcs, maxchars,
				    CS_UTF8, NULL, NULL, 0);
	assert(nchars <= maxchars);
	wcs[nchars] = L'\0';

	xcs = snewn(nchars, XChar2b);
	for (i = 0; i < nchars; i++) {
	    xcs[i].byte1 = wcs[i] >> 8;
	    xcs[i].byte2 = wcs[i];
	}

	gdk_draw_text(target, xfont->fonts[sfid], gc,
		      x, y, (gchar *)xcs, nchars*2);
	if (shadowbold)
	    gdk_draw_text(target, xfont->fonts[sfid], gc,
			  x + xfont->shadowoffset, y, (gchar *)xcs, nchars*2);
	sfree(xcs);
	sfree(wcs);
    } else {
	gdk_draw_text(target, xfont->fonts[sfid], gc, x, y, string, len);
	if (shadowbold)
	    gdk_draw_text(target, xfont->fonts[sfid], gc,
			  x + xfont->shadowoffset, y, string, len);
    }
}

static void x11font_enum_fonts(GtkWidget *widget,
			       fontsel_add_entry callback, void *callback_ctx)
{
    char **fontnames;
    char *tmp = NULL;
    int nnames, i, max, tmpsize;

    max = 32768;
    while (1) {
	fontnames = XListFonts(GDK_DISPLAY(), "*", max, &nnames);
	if (nnames >= max) {
	    XFreeFontNames(fontnames);
	    max *= 2;
	} else
	    break;
    }

    tmpsize = 0;

    for (i = 0; i < nnames; i++) {
	if (fontnames[i][0] == '-') {
	    /*
	     * Dismember an XLFD and convert it into the format
	     * we'll be using in the font selector.
	     */
	    char *components[14];
	    char *p, *font, *style, *stylekey, *charset;
	    int j, weightkey, slantkey, setwidthkey;
	    int thistmpsize, fontsize, flags;

	    thistmpsize = 4 * strlen(fontnames[i]) + 256;
	    if (tmpsize < thistmpsize) {
		tmpsize = thistmpsize;
		tmp = sresize(tmp, tmpsize, char);
	    }
	    strcpy(tmp, fontnames[i]);

	    p = tmp;
	    for (j = 0; j < 14; j++) {
		if (*p)
		    *p++ = '\0';
		components[j] = p;
		while (*p && *p != '-')
		    p++;
	    }
	    *p++ = '\0';

	    /*
	     * Font name is made up of fields 0 and 1, in reverse
	     * order with parentheses. (This is what the GTK 1.2 X
	     * font selector does, and it seems to come out
	     * looking reasonably sensible.)
	     */
	    font = p;
	    p += 1 + sprintf(p, "%s (%s)", components[1], components[0]);

	    /*
	     * Charset is made up of fields 12 and 13.
	     */
	    charset = p;
	    p += 1 + sprintf(p, "%s-%s", components[12], components[13]);

	    /*
	     * Style is a mixture of quite a lot of the fields,
	     * with some strange formatting.
	     */
	    style = p;
	    p += sprintf(p, "%s", components[2][0] ? components[2] :
			 "regular");
	    if (!g_strcasecmp(components[3], "i"))
		p += sprintf(p, " italic");
	    else if (!g_strcasecmp(components[3], "o"))
		p += sprintf(p, " oblique");
	    else if (!g_strcasecmp(components[3], "ri"))
		p += sprintf(p, " reverse italic");
	    else if (!g_strcasecmp(components[3], "ro"))
		p += sprintf(p, " reverse oblique");
	    else if (!g_strcasecmp(components[3], "ot"))
		p += sprintf(p, " other-slant");
	    if (components[4][0] && g_strcasecmp(components[4], "normal"))
		p += sprintf(p, " %s", components[4]);
	    if (!g_strcasecmp(components[10], "m"))
		p += sprintf(p, " [M]");
	    if (!g_strcasecmp(components[10], "c"))
		p += sprintf(p, " [C]");
	    if (components[5][0])
		p += sprintf(p, " %s", components[5]);

	    /*
	     * Style key is the same stuff as above, but with a
	     * couple of transformations done on it to make it
	     * sort more sensibly.
	     */
	    p++;
	    stylekey = p;
	    if (!g_strcasecmp(components[2], "medium") ||
		!g_strcasecmp(components[2], "regular") ||
		!g_strcasecmp(components[2], "normal") ||
		!g_strcasecmp(components[2], "book"))
		weightkey = 0;
	    else if (!g_strncasecmp(components[2], "demi", 4) ||
		     !g_strncasecmp(components[2], "semi", 4))
		weightkey = 1;
	    else
		weightkey = 2;
	    if (!g_strcasecmp(components[3], "r"))
		slantkey = 0;
	    else if (!g_strncasecmp(components[3], "r", 1))
		slantkey = 2;
	    else
		slantkey = 1;
	    if (!g_strcasecmp(components[4], "normal"))
		setwidthkey = 0;
	    else
		setwidthkey = 1;

	    p += sprintf(p, "%04d%04d%s%04d%04d%s%04d%04d%s%04d%s%04d%s",
			 weightkey,
			 strlen(components[2]), components[2],
			 slantkey,
			 strlen(components[3]), components[3],
			 setwidthkey,
			 strlen(components[4]), components[4],
			 strlen(components[10]), components[10],
			 strlen(components[5]), components[5]);

	    assert(p - tmp < thistmpsize);

	    /*
	     * Size is in pixels, for our application, so we
	     * derive it directly from the pixel size field,
	     * number 6.
	     */
	    fontsize = atoi(components[6]);

	    /*
	     * Flags: we need to know whether this is a monospaced
	     * font, which we do by examining the spacing field
	     * again.
	     */
	    flags = FONTFLAG_SERVERSIDE;
	    if (!strchr("CcMm", components[10][0]))
		flags |= FONTFLAG_NONMONOSPACED;

	    /*
	     * Not sure why, but sometimes the X server will
	     * deliver dummy font types in which fontsize comes
	     * out as zero. Filter those out.
	     */
	    if (fontsize)
		callback(callback_ctx, fontnames[i], font, charset,
			 style, stylekey, fontsize, flags, &x11font_vtable);
	} else {
	    /*
	     * This isn't an XLFD, so it must be an alias.
	     * Transmit it with mostly null data.
	     * 
	     * It would be nice to work out if it's monospaced
	     * here, but at the moment I can't see that being
	     * anything but computationally hideous. Ah well.
	     */
	    callback(callback_ctx, fontnames[i], fontnames[i], NULL,
		     NULL, NULL, 0, FONTFLAG_SERVERALIAS, &x11font_vtable);
	}
    }
    XFreeFontNames(fontnames);
}

static char *x11font_canonify_fontname(GtkWidget *widget, const char *name,
				       int *size)
{
    /*
     * When given an X11 font name to try to make sense of for a
     * font selector, we must attempt to load it (to see if it
     * exists), and then canonify it by extracting its FONT
     * property, which should give its full XLFD even if what we
     * originally had was an alias.
     */
    GdkFont *font = gdk_font_load(name);
    XFontStruct *xfs;
    Display *disp;
    Atom fontprop, fontprop2;
    unsigned long ret;

    if (!font)
	return NULL;		       /* didn't make sense to us, sorry */

    gdk_font_ref(font);

    xfs = GDK_FONT_XFONT(font);
    disp = GDK_FONT_XDISPLAY(font);
    fontprop = XInternAtom(disp, "FONT", False);

    if (XGetFontProperty(xfs, fontprop, &ret)) {
	char *name = XGetAtomName(disp, (Atom)ret);
	if (name) {
	    unsigned long fsize = 12;

	    fontprop2 = XInternAtom(disp, "PIXEL_SIZE", False);
	    if (XGetFontProperty(xfs, fontprop2, &fsize)) {
		*size = fsize;
		gdk_font_unref(font);
		return dupstr(name);
	    }
	}
    }

    gdk_font_unref(font);
    return NULL;		       /* something went wrong */
}

static char *x11font_scale_fontname(GtkWidget *widget, const char *name,
				    int size)
{
    return NULL;		       /* shan't */
}

/* ----------------------------------------------------------------------
 * Pango font implementation.
 */

static void pangofont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
				int x, int y, const char *string, int len,
				int wide, int bold, int cellwidth);
static unifont *pangofont_create(GtkWidget *widget, const char *name,
				 int wide, int bold,
				 int shadowoffset, int shadowalways);
static void pangofont_destroy(unifont *font);
static void pangofont_enum_fonts(GtkWidget *widget, fontsel_add_entry callback,
				 void *callback_ctx);
static char *pangofont_canonify_fontname(GtkWidget *widget, const char *name,
					 int *size);
static char *pangofont_scale_fontname(GtkWidget *widget, const char *name,
				      int size);

struct pangofont {
    struct unifont u;
    /*
     * Pango objects.
     */
    PangoFontDescription *desc;
    PangoFontset *fset;
    /*
     * The containing widget.
     */
    GtkWidget *widget;
    /*
     * Data passed in to unifont_create().
     */
    int bold, shadowoffset, shadowalways;
};

static const struct unifont_vtable pangofont_vtable = {
    pangofont_create,
    pangofont_destroy,
    pangofont_draw_text,
    pangofont_enum_fonts,
    pangofont_canonify_fontname,
    pangofont_scale_fontname,
    "client"
};

static unifont *pangofont_create(GtkWidget *widget, const char *name,
				 int wide, int bold,
				 int shadowoffset, int shadowalways)
{
    struct pangofont *pfont;
    PangoContext *ctx;
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontDescription *desc;
    PangoFontset *fset;
    PangoFontMetrics *metrics;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) {
	pango_font_description_free(desc);
	return NULL;
    }
#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map) {
	pango_font_description_free(desc);
	return NULL;
    }
    fset = pango_font_map_load_fontset(map, ctx, desc,
				       pango_context_get_language(ctx));
#else
    fset = pango_context_load_fontset(ctx, desc,
                                      pango_context_get_language(ctx));
#endif
    if (!fset) {
	pango_font_description_free(desc);
	return NULL;
    }
    metrics = pango_fontset_get_metrics(fset);
    if (!metrics ||
	pango_font_metrics_get_approximate_digit_width(metrics) == 0) {
	pango_font_description_free(desc);
	g_object_unref(fset);
	return NULL;
    }

    pfont = snew(struct pangofont);
    pfont->u.vt = &pangofont_vtable;
    pfont->u.width =
	PANGO_PIXELS(pango_font_metrics_get_approximate_digit_width(metrics));
    pfont->u.ascent = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics));
    pfont->u.descent = PANGO_PIXELS(pango_font_metrics_get_descent(metrics));
    pfont->u.height = pfont->u.ascent + pfont->u.descent;
    /* The Pango API is hardwired to UTF-8 */
    pfont->u.public_charset = CS_UTF8;
    pfont->u.real_charset = CS_UTF8;
    pfont->desc = desc;
    pfont->fset = fset;
    pfont->widget = widget;
    pfont->bold = bold;
    pfont->shadowoffset = shadowoffset;
    pfont->shadowalways = shadowalways;

    pango_font_metrics_unref(metrics);

    return (unifont *)pfont;
}

static void pangofont_destroy(unifont *font)
{
    struct pangofont *pfont = (struct pangofont *)font;
    pango_font_description_free(pfont->desc);
    g_object_unref(pfont->fset);
    sfree(font);
}

static void pangofont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
				int x, int y, const char *string, int len,
				int wide, int bold, int cellwidth)
{
    struct pangofont *pfont = (struct pangofont *)font;
    PangoLayout *layout;
    PangoRectangle rect;
    int shadowbold = FALSE;

    if (wide)
	cellwidth *= 2;

    y -= pfont->u.ascent;

    layout = pango_layout_new(gtk_widget_get_pango_context(pfont->widget));
    pango_layout_set_font_description(layout, pfont->desc);
    if (bold > pfont->bold) {
	if (pfont->shadowalways)
	    shadowbold = TRUE;
	else {
	    PangoFontDescription *desc2 =
		pango_font_description_copy_static(pfont->desc);
	    pango_font_description_set_weight(desc2, PANGO_WEIGHT_BOLD);
	    pango_layout_set_font_description(layout, desc2);
	}
    }

    while (len > 0) {
	int clen;

	/*
	 * Extract a single UTF-8 character from the string.
	 */
	clen = 1;
	while (clen < len &&
	       (unsigned char)string[clen] >= 0x80 &&
	       (unsigned char)string[clen] < 0xC0)
	    clen++;

	pango_layout_set_text(layout, string, clen);
	pango_layout_get_pixel_extents(layout, NULL, &rect);
	gdk_draw_layout(target, gc, x + (cellwidth - rect.width)/2,
			y + (pfont->u.height - rect.height)/2, layout);
	if (shadowbold)
	    gdk_draw_layout(target, gc, x + (cellwidth - rect.width)/2 + pfont->shadowoffset,
			    y + (pfont->u.height - rect.height)/2, layout);

	len -= clen;
	string += clen;
	x += cellwidth;
    }

    g_object_unref(layout);
}

/*
 * Dummy size value to be used when converting a
 * PangoFontDescription of a scalable font to a string for
 * internal use.
 */
#define PANGO_DUMMY_SIZE 12

static void pangofont_enum_fonts(GtkWidget *widget, fontsel_add_entry callback,
				 void *callback_ctx)
{
    PangoContext *ctx;
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontFamily **families;
    int i, nfamilies;

    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx)
	return;

    /*
     * Ask Pango for a list of font families, and iterate through
     * them.
     */
#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map)
	return;
    pango_font_map_list_families(map, &families, &nfamilies);
#else
    pango_context_list_families(ctx, &families, &nfamilies);
#endif
    for (i = 0; i < nfamilies; i++) {
	PangoFontFamily *family = families[i];
	const char *familyname;
	int flags;
	PangoFontFace **faces;
	int j, nfaces;

	/*
	 * Set up our flags for this font family, and get the name
	 * string.
	 */
	flags = FONTFLAG_CLIENTSIDE;
#ifndef PANGO_PRE_1POINT4
        /*
         * In very early versions of Pango, we can't tell
         * monospaced fonts from non-monospaced.
         */
	if (!pango_font_family_is_monospace(family))
	    flags |= FONTFLAG_NONMONOSPACED;
#endif
	familyname = pango_font_family_get_name(family);

	/*
	 * Go through the available font faces in this family.
	 */
	pango_font_family_list_faces(family, &faces, &nfaces);
	for (j = 0; j < nfaces; j++) {
	    PangoFontFace *face = faces[j];
	    PangoFontDescription *desc;
	    const char *facename;
	    int *sizes;
	    int k, nsizes, dummysize;

	    /*
	     * Get the face name string.
	     */
	    facename = pango_font_face_get_face_name(face);

	    /*
	     * Set up a font description with what we've got so
	     * far. We'll fill in the size field manually and then
	     * call pango_font_description_to_string() to give the
	     * full real name of the specific font.
	     */
	    desc = pango_font_face_describe(face);

	    /*
	     * See if this font has a list of specific sizes.
	     */
#ifndef PANGO_PRE_1POINT4
	    pango_font_face_list_sizes(face, &sizes, &nsizes);
#else
            /*
             * In early versions of Pango, that call wasn't
             * supported; we just have to assume everything is
             * scalable.
             */
            sizes = NULL;
#endif
	    if (!sizes) {
		/*
		 * Write a single entry with a dummy size.
		 */
		dummysize = PANGO_DUMMY_SIZE * PANGO_SCALE;
		sizes = &dummysize;
		nsizes = 1;
	    }

	    /*
	     * If so, go through them one by one.
	     */
	    for (k = 0; k < nsizes; k++) {
		char *fullname;
		char stylekey[128];

		pango_font_description_set_size(desc, sizes[k]);

		fullname = pango_font_description_to_string(desc);

		/*
		 * Construct the sorting key for font styles.
		 */
		{
		    char *p = stylekey;
		    int n;

		    n = pango_font_description_get_weight(desc);
		    /* Weight: normal, then lighter, then bolder */
		    if (n <= PANGO_WEIGHT_NORMAL)
			n = PANGO_WEIGHT_NORMAL - n;
		    p += sprintf(p, "%4d", n);

		    n = pango_font_description_get_style(desc);
		    p += sprintf(p, " %2d", n);

		    n = pango_font_description_get_stretch(desc);
		    /* Stretch: closer to normal sorts earlier */
		    n = 2 * abs(PANGO_STRETCH_NORMAL - n) +
			(n < PANGO_STRETCH_NORMAL);
		    p += sprintf(p, " %2d", n);

		    n = pango_font_description_get_variant(desc);
		    p += sprintf(p, " %2d", n);
		    
		}

		/*
		 * Got everything. Hand off to the callback.
		 * (The charset string is NULL, because only
		 * server-side X fonts use it.)
		 */
		callback(callback_ctx, fullname, familyname, NULL, facename,
			 stylekey,
			 (sizes == &dummysize ? 0 : PANGO_PIXELS(sizes[k])),
			 flags, &pangofont_vtable);

		g_free(fullname);
	    }
	    if (sizes != &dummysize)
		g_free(sizes);

	    pango_font_description_free(desc);
	}
	g_free(faces);
    }
    g_free(families);
}

static char *pangofont_canonify_fontname(GtkWidget *widget, const char *name,
					 int *size)
{
    /*
     * When given a Pango font name to try to make sense of for a
     * font selector, we must normalise it to PANGO_DUMMY_SIZE and
     * extract its original size (in pixels) into the `size' field.
     */
    PangoContext *ctx;
#ifndef PANGO_PRE_1POINT6
    PangoFontMap *map;
#endif
    PangoFontDescription *desc;
    PangoFontset *fset;
    PangoFontMetrics *metrics;
    char *newname, *retname;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) {
	pango_font_description_free(desc);
	return NULL;
    }
#ifndef PANGO_PRE_1POINT6
    map = pango_context_get_font_map(ctx);
    if (!map) {
	pango_font_description_free(desc);
	return NULL;
    }
    fset = pango_font_map_load_fontset(map, ctx, desc,
				       pango_context_get_language(ctx));
#else
    fset = pango_context_load_fontset(ctx, desc,
                                      pango_context_get_language(ctx));
#endif
    if (!fset) {
	pango_font_description_free(desc);
	return NULL;
    }
    metrics = pango_fontset_get_metrics(fset);
    if (!metrics ||
	pango_font_metrics_get_approximate_digit_width(metrics) == 0) {
	pango_font_description_free(desc);
	g_object_unref(fset);
	return NULL;
    }

    *size = PANGO_PIXELS(pango_font_description_get_size(desc));
    pango_font_description_set_size(desc, PANGO_DUMMY_SIZE * PANGO_SCALE);
    newname = pango_font_description_to_string(desc);
    retname = dupstr(newname);
    g_free(newname);

    pango_font_metrics_unref(metrics);
    pango_font_description_free(desc);
    g_object_unref(fset);

    return retname;
}

static char *pangofont_scale_fontname(GtkWidget *widget, const char *name,
				      int size)
{
    PangoFontDescription *desc;
    char *newname, *retname;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    pango_font_description_set_size(desc, size * PANGO_SCALE);
    newname = pango_font_description_to_string(desc);
    retname = dupstr(newname);
    g_free(newname);
    pango_font_description_free(desc);

    return retname;
}

/* ----------------------------------------------------------------------
 * Outermost functions which do the vtable dispatch.
 */

/*
 * Complete list of font-type subclasses. Listed in preference
 * order for unifont_create(). (That is, in the extremely unlikely
 * event that the same font name is valid as both a Pango and an
 * X11 font, it will be interpreted as the former in the absence
 * of an explicit type-disambiguating prefix.)
 */
static const struct unifont_vtable *unifont_types[] = {
    &pangofont_vtable,
    &x11font_vtable,
};

/*
 * Function which takes a font name and processes the optional
 * scheme prefix. Returns the tail of the font name suitable for
 * passing to individual font scheme functions, and also provides
 * a subrange of the unifont_types[] array above.
 * 
 * The return values `start' and `end' denote a half-open interval
 * in unifont_types[]; that is, the correct way to iterate over
 * them is
 * 
 *   for (i = start; i < end; i++) {...}
 */
static const char *unifont_do_prefix(const char *name, int *start, int *end)
{
    int colonpos = strcspn(name, ":");
    int i;

    if (name[colonpos]) {
	/*
	 * There's a colon prefix on the font name. Use it to work
	 * out which subclass to use.
	 */
	for (i = 0; i < lenof(unifont_types); i++) {
	    if (strlen(unifont_types[i]->prefix) == colonpos &&
		!strncmp(unifont_types[i]->prefix, name, colonpos)) {
		*start = i;
		*end = i+1;
		return name + colonpos + 1;
	    }
	}
	/*
	 * None matched, so return an empty scheme list to prevent
	 * any scheme from being called at all.
	 */
	*start = *end = 0;
	return name + colonpos + 1;
    } else {
	/*
	 * No colon prefix, so just use all the subclasses.
	 */
	*start = 0;
	*end = lenof(unifont_types);
	return name;
    }
}

unifont *unifont_create(GtkWidget *widget, const char *name, int wide,
			int bold, int shadowoffset, int shadowalways)
{
    int i, start, end;

    name = unifont_do_prefix(name, &start, &end);

    for (i = start; i < end; i++) {
	unifont *ret = unifont_types[i]->create(widget, name, wide, bold,
						shadowoffset, shadowalways);
	if (ret)
	    return ret;
    }
    return NULL;		       /* font not found in any scheme */
}

void unifont_destroy(unifont *font)
{
    font->vt->destroy(font);
}

void unifont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
		       int x, int y, const char *string, int len,
		       int wide, int bold, int cellwidth)
{
    font->vt->draw_text(target, gc, font, x, y, string, len,
			wide, bold, cellwidth);
}

/* ----------------------------------------------------------------------
 * Implementation of a unified font selector.
 */

typedef struct fontinfo fontinfo;

typedef struct unifontsel_internal {
    /* This must be the structure's first element, for cross-casting */
    unifontsel u;
    GtkListStore *family_model, *style_model, *size_model;
    GtkWidget *family_list, *style_list, *size_entry, *size_list;
    GtkWidget *filter_buttons[4];
    int filter_flags;
    tree234 *fonts_by_realname, *fonts_by_selorder;
    fontinfo *selected;
    int selsize;
    int inhibit_response;  /* inhibit callbacks when we change GUI controls */
} unifontsel_internal;

/*
 * The structure held in the tree234s. All the string members are
 * part of the same allocated area, so don't need freeing
 * separately.
 */
struct fontinfo {
    char *realname;
    char *family, *charset, *style, *stylekey;
    int size, flags;
    /*
     * Fallback sorting key, to permit multiple identical entries
     * to exist in the selorder tree.
     */
    int index;
    /*
     * Indices mapping fontinfo structures to indices in the list
     * boxes. sizeindex is irrelevant if the font is scalable
     * (size==0).
     */
    int familyindex, styleindex, sizeindex;
    /*
     * The class of font.
     */
    const struct unifont_vtable *fontclass;
};

static int fontinfo_realname_compare(void *av, void *bv)
{
    fontinfo *a = (fontinfo *)av;
    fontinfo *b = (fontinfo *)bv;
    return g_strcasecmp(a->realname, b->realname);
}

static int fontinfo_realname_find(void *av, void *bv)
{
    const char *a = (const char *)av;
    fontinfo *b = (fontinfo *)bv;
    return g_strcasecmp(a, b->realname);
}

static int strnullcasecmp(const char *a, const char *b)
{
    int i;

    /*
     * If exactly one of the inputs is NULL, it compares before
     * the other one.
     */
    if ((i = (!b) - (!a)) != 0)
	return i;

    /*
     * NULL compares equal.
     */
    if (!a)
	return 0;

    /*
     * Otherwise, ordinary strcasecmp.
     */
    return g_strcasecmp(a, b);
}

static int fontinfo_selorder_compare(void *av, void *bv)
{
    fontinfo *a = (fontinfo *)av;
    fontinfo *b = (fontinfo *)bv;
    int i;
    if ((i = strnullcasecmp(a->family, b->family)) != 0)
	return i;
    if ((i = strnullcasecmp(a->charset, b->charset)) != 0)
	return i;
    if ((i = strnullcasecmp(a->stylekey, b->stylekey)) != 0)
	return i;
    if ((i = strnullcasecmp(a->style, b->style)) != 0)
	return i;
    if (a->size != b->size)
	return (a->size < b->size ? -1 : +1);
    if (a->index != b->index)
	return (a->index < b->index ? -1 : +1);
    return 0;
}

static void unifontsel_setup_familylist(unifontsel_internal *fs)
{
    GtkTreeIter iter;
    int i, listindex, minpos = -1, maxpos = -1;
    char *currfamily = NULL;
    fontinfo *info;

    gtk_list_store_clear(fs->family_model);
    listindex = 0;

    /*
     * Search through the font tree for anything matching our
     * current filter criteria. When we find one, add its font
     * name to the list box.
     */
    for (i = 0 ;; i++) {
	info = (fontinfo *)index234(fs->fonts_by_selorder, i);
	/*
	 * info may be NULL if we've just run off the end of the
	 * tree. We must still do a processing pass in that
	 * situation, in case we had an unfinished font record in
	 * progress.
	 */
	if (info && (info->flags &~ fs->filter_flags)) {
	    info->familyindex = -1;
	    continue;		       /* we're filtering out this font */
	}
	if (!info || strnullcasecmp(currfamily, info->family)) {
	    /*
	     * We've either finished a family, or started a new
	     * one, or both.
	     */
	    if (currfamily) {
		gtk_list_store_append(fs->family_model, &iter);
		gtk_list_store_set(fs->family_model, &iter,
				   0, currfamily, 1, minpos, 2, maxpos+1, -1);
		listindex++;
	    }
	    if (info) {
		minpos = i;
		currfamily = info->family;
	    }
	}
	if (!info)
	    break;		       /* now we're done */
	info->familyindex = listindex;
	maxpos = i;
    }
}

static void unifontsel_setup_stylelist(unifontsel_internal *fs,
				       int start, int end)
{
    GtkTreeIter iter;
    int i, listindex, minpos = -1, maxpos = -1, started = FALSE;
    char *currcs = NULL, *currstyle = NULL;
    fontinfo *info;

    gtk_list_store_clear(fs->style_model);
    listindex = 0;
    started = FALSE;

    /*
     * Search through the font tree for anything matching our
     * current filter criteria. When we find one, add its charset
     * and/or style name to the list box.
     */
    for (i = start; i <= end; i++) {
	if (i == end)
	    info = NULL;
	else
	    info = (fontinfo *)index234(fs->fonts_by_selorder, i);
	/*
	 * info may be NULL if we've just run off the end of the
	 * relevant data. We must still do a processing pass in
	 * that situation, in case we had an unfinished font
	 * record in progress.
	 */
	if (info && (info->flags &~ fs->filter_flags)) {
	    info->styleindex = -1;
	    continue;		       /* we're filtering out this font */
	}
	if (!info || !started || strnullcasecmp(currcs, info->charset) ||
	     strnullcasecmp(currstyle, info->style)) {
	    /*
	     * We've either finished a style/charset, or started a
	     * new one, or both.
	     */
	    started = TRUE;
	    if (currstyle) {
		gtk_list_store_append(fs->style_model, &iter);
		gtk_list_store_set(fs->style_model, &iter,
				   0, currstyle, 1, minpos, 2, maxpos+1,
				   3, TRUE, -1);
		listindex++;
	    }
	    if (info) {
		minpos = i;
		if (info->charset && strnullcasecmp(currcs, info->charset)) {
		    gtk_list_store_append(fs->style_model, &iter);
		    gtk_list_store_set(fs->style_model, &iter,
				       0, info->charset, 1, -1, 2, -1,
				       3, FALSE, -1);
		    listindex++;
		}
		currcs = info->charset;
		currstyle = info->style;
	    }
	}
	if (!info)
	    break;		       /* now we're done */
	info->styleindex = listindex;
	maxpos = i;
    }
}

static const int unifontsel_default_sizes[] = { 10, 12, 14, 16, 20, 24, 32 };

static void unifontsel_setup_sizelist(unifontsel_internal *fs,
				      int start, int end)
{
    GtkTreeIter iter;
    int i, listindex;
    char sizetext[40];
    fontinfo *info;

    gtk_list_store_clear(fs->size_model);
    listindex = 0;

    /*
     * Search through the font tree for anything matching our
     * current filter criteria. When we find one, add its font
     * name to the list box.
     */
    for (i = start; i < end; i++) {
	info = (fontinfo *)index234(fs->fonts_by_selorder, i);
	if (info->flags &~ fs->filter_flags) {
	    info->sizeindex = -1;
	    continue;		       /* we're filtering out this font */
	}
	if (info->size) {
	    sprintf(sizetext, "%d", info->size);
	    info->sizeindex = listindex;
	    gtk_list_store_append(fs->size_model, &iter);
	    gtk_list_store_set(fs->size_model, &iter,
			       0, sizetext, 1, i, 2, info->size, -1);
	    listindex++;
	} else {
	    int j;

	    assert(i == start);
	    assert(i+1 == end);

	    for (j = 0; j < lenof(unifontsel_default_sizes); j++) {
		sprintf(sizetext, "%d", unifontsel_default_sizes[j]);
		gtk_list_store_append(fs->size_model, &iter);
		gtk_list_store_set(fs->size_model, &iter, 0, sizetext, 1, i,
				   2, unifontsel_default_sizes[j], -1);
		listindex++;
	    }
	}
    }
}

static void unifontsel_set_filter_buttons(unifontsel_internal *fs)
{
    int i;

    for (i = 0; i < lenof(fs->filter_buttons); i++) {
	int flagbit = GPOINTER_TO_INT(gtk_object_get_data
				      (GTK_OBJECT(fs->filter_buttons[i]),
				       "user-data"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fs->filter_buttons[i]),
				     !!(fs->filter_flags & flagbit));
    }
}

static void unifontsel_select_font(unifontsel_internal *fs,
				   fontinfo *info, int size, int leftlist)
{
    int index;
    int minval, maxval;
    GtkTreePath *treepath;
    GtkTreeIter iter;

    fs->inhibit_response = TRUE;

    fs->selected = info;
    fs->selsize = size;

    /*
     * Find the index of this fontinfo in the selorder list. 
     */
    index = -1;
    findpos234(fs->fonts_by_selorder, info, NULL, &index);
    assert(index >= 0);

    /*
     * Adjust the font selector flags and redo the font family
     * list box, if necessary.
     */
    if (leftlist <= 0 &&
	(fs->filter_flags | info->flags) != fs->filter_flags) {
	fs->filter_flags |= info->flags;
	unifontsel_set_filter_buttons(fs);
	unifontsel_setup_familylist(fs);
    }

    /*
     * Find the appropriate family name and select it in the list.
     */
    assert(info->familyindex >= 0);
    treepath = gtk_tree_path_new_from_indices(info->familyindex, -1);
    gtk_tree_selection_select_path
	(gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->family_list)),
	 treepath);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->family_list),
				 treepath, NULL, FALSE, 0.0, 0.0);
    gtk_tree_model_get_iter(GTK_TREE_MODEL(fs->family_model), &iter, treepath);
    gtk_tree_path_free(treepath);

    /*
     * Now set up the font style list.
     */
    gtk_tree_model_get(GTK_TREE_MODEL(fs->family_model), &iter,
		       1, &minval, 2, &maxval, -1);
    if (leftlist <= 1)
	unifontsel_setup_stylelist(fs, minval, maxval);

    /*
     * Find the appropriate style name and select it in the list.
     */
    if (info->style) {
	assert(info->styleindex >= 0);
	treepath = gtk_tree_path_new_from_indices(info->styleindex, -1);
	gtk_tree_selection_select_path
	    (gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->style_list)),
	     treepath);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->style_list),
				     treepath, NULL, FALSE, 0.0, 0.0);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(fs->style_model),
				&iter, treepath);
	gtk_tree_path_free(treepath);

	/*
	 * And set up the size list.
	 */
	gtk_tree_model_get(GTK_TREE_MODEL(fs->style_model), &iter,
			   1, &minval, 2, &maxval, -1);
	if (leftlist <= 2)
	    unifontsel_setup_sizelist(fs, minval, maxval);

	/*
	 * Find the appropriate size, and select it in the list.
	 */
	if (info->size) {
	    assert(info->sizeindex >= 0);
	    treepath = gtk_tree_path_new_from_indices(info->sizeindex, -1);
	    gtk_tree_selection_select_path
		(gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->size_list)),
		 treepath);
	    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->size_list),
					 treepath, NULL, FALSE, 0.0, 0.0);
	    gtk_tree_path_free(treepath);
	    size = info->size;
	} else {
	    int j;
	    for (j = 0; j < lenof(unifontsel_default_sizes); j++)
		if (unifontsel_default_sizes[j] == size) {
		    treepath = gtk_tree_path_new_from_indices(j, -1);
		    gtk_tree_view_set_cursor(GTK_TREE_VIEW(fs->size_list),
					     treepath, NULL, FALSE);
		    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(fs->size_list),
						 treepath, NULL, FALSE, 0.0,
						 0.0);
		    gtk_tree_path_free(treepath);
		}
	}

	/*
	 * And set up the font size text entry box.
	 */
	{
	    char sizetext[40];
	    sprintf(sizetext, "%d", size);
	    gtk_entry_set_text(GTK_ENTRY(fs->size_entry), sizetext);
	}
    } else {
	if (leftlist <= 2)
	    unifontsel_setup_sizelist(fs, 0, 0);
	gtk_entry_set_text(GTK_ENTRY(fs->size_entry), "");
    }

    /*
     * Grey out the font size edit box if we're not using a
     * scalable font.
     */
    gtk_entry_set_editable(GTK_ENTRY(fs->size_entry), fs->selected->size == 0);
    gtk_widget_set_sensitive(fs->size_entry, fs->selected->size == 0);

    fs->inhibit_response = FALSE;
}

static void unifontsel_button_toggled(GtkToggleButton *tb, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    int newstate = gtk_toggle_button_get_active(tb);
    int newflags;
    int flagbit = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(tb),
						      "user-data"));

    if (newstate)
	newflags = fs->filter_flags | flagbit;
    else
	newflags = fs->filter_flags & ~flagbit;

    if (fs->filter_flags != newflags) {
	fs->filter_flags = newflags;
	unifontsel_setup_familylist(fs);
    }
}

static void unifontsel_add_entry(void *ctx, const char *realfontname,
				 const char *family, const char *charset,
				 const char *style, const char *stylekey,
				 int size, int flags,
				 const struct unifont_vtable *fontclass)
{
    unifontsel_internal *fs = (unifontsel_internal *)ctx;
    fontinfo *info;
    int totalsize;
    char *p;

    totalsize = sizeof(fontinfo) + strlen(realfontname) +
	(family ? strlen(family) : 0) + (charset ? strlen(charset) : 0) +
	(style ? strlen(style) : 0) + (stylekey ? strlen(stylekey) : 0) + 10;
    info = (fontinfo *)smalloc(totalsize);
    info->fontclass = fontclass;
    p = (char *)info + sizeof(fontinfo);
    info->realname = p;
    strcpy(p, realfontname);
    p += 1+strlen(p);
    if (family) {
	info->family = p;
	strcpy(p, family);
	p += 1+strlen(p);
    } else
	info->family = NULL;
    if (charset) {
	info->charset = p;
	strcpy(p, charset);
	p += 1+strlen(p);
    } else
	info->charset = NULL;
    if (style) {
	info->style = p;
	strcpy(p, style);
	p += 1+strlen(p);
    } else
	info->style = NULL;
    if (stylekey) {
	info->stylekey = p;
	strcpy(p, stylekey);
	p += 1+strlen(p);
    } else
	info->stylekey = NULL;
    assert(p - (char *)info <= totalsize);
    info->size = size;
    info->flags = flags;
    info->index = count234(fs->fonts_by_selorder);

    /*
     * It's just conceivable that a misbehaving font enumerator
     * might tell us about the same font real name more than once,
     * in which case we should silently drop the new one.
     */
    if (add234(fs->fonts_by_realname, info) != info) {
	sfree(info);
	return;
    }
    /*
     * However, we should never get a duplicate key in the
     * selorder tree, because the index field carefully
     * disambiguates otherwise identical records.
     */
    add234(fs->fonts_by_selorder, info);
}

static void family_changed(GtkTreeSelection *treeselection, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeModel *treemodel;
    GtkTreeIter treeiter;
    int minval;
    fontinfo *info;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    if (!gtk_tree_selection_get_selected(treeselection, &treemodel, &treeiter))
	return;

    gtk_tree_model_get(treemodel, &treeiter, 1, &minval, -1);
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    unifontsel_select_font(fs, info, info->size ? info->size : fs->selsize, 1);
}

static void style_changed(GtkTreeSelection *treeselection, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeModel *treemodel;
    GtkTreeIter treeiter;
    int minval;
    fontinfo *info;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    if (!gtk_tree_selection_get_selected(treeselection, &treemodel, &treeiter))
	return;

    gtk_tree_model_get(treemodel, &treeiter, 1, &minval, -1);
    if (minval < 0)
        return;                    /* somehow a charset heading got clicked */
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    unifontsel_select_font(fs, info, info->size ? info->size : fs->selsize, 2);
}

static void size_changed(GtkTreeSelection *treeselection, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    GtkTreeModel *treemodel;
    GtkTreeIter treeiter;
    int minval, size;
    fontinfo *info;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    if (!gtk_tree_selection_get_selected(treeselection, &treemodel, &treeiter))
	return;

    gtk_tree_model_get(treemodel, &treeiter, 1, &minval, 2, &size, -1);
    info = (fontinfo *)index234(fs->fonts_by_selorder, minval);
    unifontsel_select_font(fs, info, info->size ? info->size : size, 3);
}

static void size_entry_changed(GtkEditable *ed, gpointer data)
{
    unifontsel_internal *fs = (unifontsel_internal *)data;
    const char *text;
    int size;

    if (fs->inhibit_response)	       /* we made this change ourselves */
	return;

    text = gtk_entry_get_text(GTK_ENTRY(ed));
    size = atoi(text);

    if (size > 0) {
	assert(fs->selected->size == 0);
	unifontsel_select_font(fs, fs->selected, size, 3);
    }
}

unifontsel *unifontsel_new(const char *wintitle)
{
    unifontsel_internal *fs = snew(unifontsel_internal);
    GtkWidget *table, *label, *w, *scroll;
    GtkListStore *model;
    GtkTreeViewColumn *column;
    int lists_height, font_width, style_width, size_width;
    int i;

    fs->inhibit_response = FALSE;

    {
	/*
	 * Invent some magic size constants.
	 */
	GtkRequisition req;
	label = gtk_label_new("Quite Long Font Name (Foundry)");
	gtk_widget_size_request(label, &req);
	font_width = req.width;
	lists_height = 14 * req.height;
	gtk_label_set_text(GTK_LABEL(label), "Italic Extra Condensed");
	gtk_widget_size_request(label, &req);
	style_width = req.width;
	gtk_label_set_text(GTK_LABEL(label), "48000");
	gtk_widget_size_request(label, &req);
	size_width = req.width;
#if GTK_CHECK_VERSION(2,10,0)
	g_object_ref_sink(label);
	g_object_unref(label);
#else
        gtk_object_sink(GTK_OBJECT(label));
#endif
    }

    /*
     * Create the dialog box and initialise the user-visible
     * fields in the returned structure.
     */
    fs->u.user_data = NULL;
    fs->u.window = GTK_WINDOW(gtk_dialog_new());
    gtk_window_set_title(fs->u.window, wintitle);
    fs->u.cancel_button = gtk_dialog_add_button
	(GTK_DIALOG(fs->u.window), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    fs->u.ok_button = gtk_dialog_add_button
	(GTK_DIALOG(fs->u.window), GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_widget_grab_default(fs->u.ok_button);

    /*
     * Now set up the internal fields, including in particular all
     * the controls that actually allow the user to select fonts.
     */
    table = gtk_table_new(3, 8, FALSE);
    gtk_widget_show(table);
    gtk_table_set_col_spacings(GTK_TABLE(table), 8);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fs->u.window)->vbox),
		       table, TRUE, TRUE, 0);

    label = gtk_label_new_with_mnemonic("_Font:");
    gtk_widget_show(label);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);

    /*
     * The Font list box displays only a string, but additionally
     * stores two integers which give the limits within the
     * tree234 of the font entries covered by this list entry.
     */
    model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), w);
    gtk_widget_show(w);
    column = gtk_tree_view_column_new_with_attributes
	("Font", gtk_cell_renderer_text_new(),
	 "text", 0, (char *)NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);
    g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(w))),
		     "changed", G_CALLBACK(family_changed), fs);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), w);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_widget_set_size_request(scroll, font_width, lists_height);
    gtk_table_attach(GTK_TABLE(table), scroll, 0, 1, 1, 3, GTK_FILL, 0, 0, 0);
    fs->family_model = model;
    fs->family_list = w;

    label = gtk_label_new_with_mnemonic("_Style:");
    gtk_widget_show(label);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);

    /*
     * The Style list box can contain insensitive elements
     * (character set headings for server-side fonts), so we add
     * an extra column to the list store to hold that information.
     */
    model = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT,
			       G_TYPE_BOOLEAN);
    w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), w);
    gtk_widget_show(w);
    column = gtk_tree_view_column_new_with_attributes
	("Style", gtk_cell_renderer_text_new(),
	 "text", 0, "sensitive", 3, (char *)NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);
    g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(w))),
		     "changed", G_CALLBACK(style_changed), fs);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), w);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_widget_set_size_request(scroll, style_width, lists_height);
    gtk_table_attach(GTK_TABLE(table), scroll, 1, 2, 1, 3, GTK_FILL, 0, 0, 0);
    fs->style_model = model;
    fs->style_list = w;

    label = gtk_label_new_with_mnemonic("Si_ze:");
    gtk_widget_show(label);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);

    /*
     * The Size label attaches primarily to a text input box so
     * that the user can select a size of their choice. The list
     * of available sizes is secondary.
     */
    fs->size_entry = w = gtk_entry_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), w);
    gtk_widget_set_size_request(w, size_width, -1);
    gtk_widget_show(w);
    gtk_table_attach(GTK_TABLE(table), w, 2, 3, 1, 2, GTK_FILL, 0, 0, 0);
    g_signal_connect(G_OBJECT(w), "changed", G_CALLBACK(size_entry_changed),
		     fs);

    model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);
    gtk_widget_show(w);
    column = gtk_tree_view_column_new_with_attributes
	("Size", gtk_cell_renderer_text_new(),
	 "text", 0, (char *)NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);
    g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(w))),
		     "changed", G_CALLBACK(size_changed), fs);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), w);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_table_attach(GTK_TABLE(table), scroll, 2, 3, 2, 3, GTK_FILL,
		     GTK_EXPAND | GTK_FILL, 0, 0);
    fs->size_model = model;
    fs->size_list = w;

    /*
     * FIXME: preview widget
     */
    i = 0;
    w = gtk_check_button_new_with_label("Show client-side fonts");
    gtk_object_set_data(GTK_OBJECT(w), "user-data",
			GINT_TO_POINTER(FONTFLAG_CLIENTSIDE));
    gtk_signal_connect(GTK_OBJECT(w), "toggled",
		       GTK_SIGNAL_FUNC(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[i++] = w;
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 4, 5, GTK_FILL, 0, 0, 0);
    w = gtk_check_button_new_with_label("Show server-side fonts");
    gtk_object_set_data(GTK_OBJECT(w), "user-data",
			GINT_TO_POINTER(FONTFLAG_SERVERSIDE));
    gtk_signal_connect(GTK_OBJECT(w), "toggled",
		       GTK_SIGNAL_FUNC(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[i++] = w;
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 5, 6, GTK_FILL, 0, 0, 0);
    w = gtk_check_button_new_with_label("Show server-side font aliases");
    gtk_object_set_data(GTK_OBJECT(w), "user-data",
			GINT_TO_POINTER(FONTFLAG_SERVERALIAS));
    gtk_signal_connect(GTK_OBJECT(w), "toggled",
		       GTK_SIGNAL_FUNC(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[i++] = w;
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 6, 7, GTK_FILL, 0, 0, 0);
    w = gtk_check_button_new_with_label("Show non-monospaced fonts");
    gtk_object_set_data(GTK_OBJECT(w), "user-data",
			GINT_TO_POINTER(FONTFLAG_NONMONOSPACED));
    gtk_signal_connect(GTK_OBJECT(w), "toggled",
		       GTK_SIGNAL_FUNC(unifontsel_button_toggled), fs);
    gtk_widget_show(w);
    fs->filter_buttons[i++] = w;
    gtk_table_attach(GTK_TABLE(table), w, 0, 3, 7, 8, GTK_FILL, 0, 0, 0);

    assert(i == lenof(fs->filter_buttons));
    fs->filter_flags = FONTFLAG_CLIENTSIDE | FONTFLAG_SERVERSIDE;
    unifontsel_set_filter_buttons(fs);

    /*
     * Go and find all the font names, and set up our master font
     * list.
     */
    fs->fonts_by_realname = newtree234(fontinfo_realname_compare);
    fs->fonts_by_selorder = newtree234(fontinfo_selorder_compare);
    for (i = 0; i < lenof(unifont_types); i++)
	unifont_types[i]->enum_fonts(GTK_WIDGET(fs->u.window),
				     unifontsel_add_entry, fs);

    /*
     * And set up the initial font names list.
     */
    unifontsel_setup_familylist(fs);

    fs->selected = NULL;

    return (unifontsel *)fs;
}

void unifontsel_destroy(unifontsel *fontsel)
{
    unifontsel_internal *fs = (unifontsel_internal *)fontsel;
    fontinfo *info;

    freetree234(fs->fonts_by_selorder);
    while ((info = delpos234(fs->fonts_by_realname, 0)) != NULL)
	sfree(info);
    freetree234(fs->fonts_by_realname);

    gtk_widget_destroy(GTK_WIDGET(fs->u.window));
    sfree(fs);
}

void unifontsel_set_name(unifontsel *fontsel, const char *fontname)
{
    unifontsel_internal *fs = (unifontsel_internal *)fontsel;
    int i, start, end, size;
    const char *fontname2 = NULL;
    fontinfo *info;

    /*
     * Provide a default if given an empty or null font name.
     */
    if (!fontname || !*fontname)
	fontname = "fixed";   /* Pango zealots might prefer "Monospace 12" */

    /*
     * Call the canonify_fontname function.
     */
    fontname = unifont_do_prefix(fontname, &start, &end);
    for (i = start; i < end; i++) {
	fontname2 = unifont_types[i]->canonify_fontname
	    (GTK_WIDGET(fs->u.window), fontname, &size);
	if (fontname2)
	    break;
    }
    if (i == end)
	return;			       /* font name not recognised */

    /*
     * Now look up the canonified font name in our index.
     */
    info = find234(fs->fonts_by_realname, (char *)fontname2,
		   fontinfo_realname_find);

    /*
     * If we've found the font, and its size field is either
     * correct or zero (the latter indicating a scalable font),
     * then we're done. Otherwise, try looking up the original
     * font name instead.
     */
    if (!info || (info->size != size && info->size != 0)) {
	info = find234(fs->fonts_by_realname, (char *)fontname,
		       fontinfo_realname_find);
	if (!info || info->size != size)
	    return;		       /* font name not in our index */
    }

    /*
     * Now we've got a fontinfo structure and a font size, so we
     * know everything we need to fill in all the fields in the
     * dialog.
     */
    unifontsel_select_font(fs, info, size, 0);
}

char *unifontsel_get_name(unifontsel *fontsel)
{
    unifontsel_internal *fs = (unifontsel_internal *)fontsel;
    char *name;

    assert(fs->selected);

    if (fs->selected->size == 0) {
	name = fs->selected->fontclass->scale_fontname
	    (GTK_WIDGET(fs->u.window), fs->selected->realname, fs->selsize);
	if (name)
	    return name;
    }

    return dupstr(fs->selected->realname);
}
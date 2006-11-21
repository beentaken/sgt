/*
 * `v', a simple image viewer designed to lack the various annoying
 * UI glitches I've encountered in other image viewers.
 */

/*
 * Things I don't like about other viewers:
 *
 *  - eeyes is mostly nice but really, really needs keypresses to
 *    be interpreted identically whether the list or image window
 *    is visible. Also I think I'd prefer the list window to be
 *    displayed only on request.
 *
 *  - ImageMagick's `display' is way too slow, and doesn't even
 *    have a list window.
 *
 *  - xv is shareware, and also slow (though not as slow as
 *    `display'), and I don't much like some of its design
 *    decisions (such as the `Bummer' box rather than a
 *    command-line error on startup, and the selection semantics in
 *    the list box). It also suffers from the eeyes problem (you
 *    can get caught out by which window is active).
 *
 *  - eog has too many panes; most if not all of an image viewer's
 *    screen space should be devoted to the image!
 */

/*
 * Desired features:
 *
 *  - ability to load lots of images and step back and forth
 *    between them.
 *     + passing wildcards / file lists on the command line,
 *       obviously
 *     + also it might be handy to be able to prepare a text file
 *       containing a list of image file names, and have v read
 *       from the text file (or stdin). Might be handy next time I
 *       want to create an animation (fractal, raytraced or
 *       whatever).
 *
 *  - scaling of images so they fit on the screen.
 *     + so we need to sense the screen size
 *     + and also it would be good to somehow sense the presence of
 *       the GNOME taskbar, although I've no idea how you can tell
 *       that.
 *     + should also be able to switch back to 1-1 viewing, which
 *       probably means scrollbars.
 *     + manual scaling would probably be nice too.
 * 
 *  - manual rotation by 90 degrees at a time would be helpful.
 *
 *  - an ee-like list window is a possibility, but can get in the
 *    way as much as it helps when viewing a big image. Uncertain.
 * 
 *  - I'm wondering about a mode in which the window stays the same
 *    size and images are centred in it (or scaled down /
 *    scrollbarred to fit), as well as the obvious mode in which
 *    the window changes size to adapt to each image.
 *     + in this mode, a good initial window size might be the
 *       maximum of all the images being shown?
 *     + perhaps a one-pixel black border within the grey default
 *       window background, which would have the advantage of
 *       making it always unambiguous where the image actually
 *       began?
 * 
 *  - sort out sensible alpha channel handling. I _think_ we're
 *    currently compositing the image on to black, which isn't
 *    ideal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "v.h"

#include <gdk/gdkkeysyms.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <gdk/gdkx.h>

/*
 * FIXME: These should be set from the current display size.
 * Tricky, because I'd also like to take the GNOME taskbar into
 * account. Not sure how to do that.
 */
int maxw = 1572, maxh = 1104;

/*
 * Size of border drawn around the image in max-size mode.
 */
#define BORDER 1

void fatal(char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "fatal error: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}

static void get_scaled_size(image *im, int *w, int *h)
{
    int iw, ih;

    iw = image_width(im);
    ih = image_height(im);

    /*
     * Scale the image down if it's too big.
     */
    if (iw > maxw || ih > maxh) {
	float wscale = (float)iw / maxw;
	float hscale = (float)ih / maxh;
	float scale = (wscale > hscale ? wscale : hscale);
	iw /= scale;
	ih /= scale;
    }

    if (w) *w = iw;
    if (h) *h = ih;
}

struct ilist {
    image **images;
    char **filenames;
    int n, size;
    int maxw, maxh;
};

struct ilist *ilist_new(void)
{
    struct ilist *il = snew(struct ilist);
    il->images = NULL;
    il->filenames = NULL;
    il->n = il->size = 0;
    il->maxw = il->maxh = 0;
    return il;
}

char *ilist_load(struct ilist *il, char *filename)
{
    char *err;
    int w, h;

    image *im = image_load(filename, &err);
    if (!im)
	return err;
    else {
	if (il->n >= il->size) {
	    il->size = il->size * 3 / 2 + 16;
	    il->images = sresize(il->images, il->size, image *);
	    il->filenames = sresize(il->filenames, il->size, char *);
	}
	il->images[il->n] = im;
	il->filenames[il->n] = dupstr(filename);
	il->n++;
        get_scaled_size(im, &w, &h);
        if (il->maxw < w) il->maxw = w;
        if (il->maxh < h) il->maxh = h;
	return NULL;
    }
}

void ilist_free(struct ilist *il)
{
    while (il->n > 0) {
	il->n--;
        if (il->images[il->n])
	    image_free(il->images[il->n]);
	sfree(il->filenames[il->n]);
    }
    sfree(il->images);
    sfree(il->filenames);
    sfree(il);
}

void ilist_tmpfree(struct ilist *il, int n)
{
    if (0 <= n && n < il->n) {
	if (il->images[n])
	    image_free(il->images[n]);
	il->images[n] = NULL;
    }
}

void ilist_reload(struct ilist *il, int n)
{
    char *err;
    assert(0 <= n && n < il->n);
    if (il->images[n])
	return;			       /* already loaded */
    il->images[n] = image_load(il->filenames[n], &err);
    if (!il->images[n]) {
	fprintf(stderr, "v: unable to reload image '%s': %s\n",
		il->filenames[n], err);
	exit(1);
    }
}

struct window {
    struct ilist *il;
    int pos;
    int use_max_size;
    int iw, ih, ox, oy, ww, wh;
    GtkWidget *window, *area;
    GdkPixmap *pm;
};

static void switch_to_image(struct window *w, int index)
{
    int iw, ih;
    image *im;

    assert(w->il->n > index);

    ilist_tmpfree(w->il, w->pos);

    w->pos = index;

    im = w->il->images[index];
    if (!im) {
	ilist_reload(w->il, index);
	im = w->il->images[index];
    }
    get_scaled_size(im, &iw, &ih);

    w->iw = iw;
    w->ih = ih;
    w->ox = w->oy = 0;

    if (w->use_max_size)
        gtk_drawing_area_size(GTK_DRAWING_AREA(w->area),
                              w->il->maxw + 2*BORDER,
                              w->il->maxh + 2*BORDER);
    else
        gtk_drawing_area_size(GTK_DRAWING_AREA(w->area), iw, ih);
    gtk_window_set_title(GTK_WINDOW(w->window), w->il->filenames[index]);

    /*
     * This section is conditional because we might not have shown
     * the window at all yet.
     */
    if (w->window->window) {
	GtkRequisition req;
	gtk_widget_size_request(w->window, &req);
	gdk_window_resize(w->window->window, req.width, req.height);
	gdk_window_clear(w->window->window);

        if (w->pm)
            gdk_pixmap_unref(w->pm);

        w->pm = gdk_pixmap_new(w->area->window, iw, ih, -1);
        image_to_pixmap(im, w->pm, iw, ih);
    }
}

static void window_destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static gint expose_area(GtkWidget *widget, GdkEventExpose *event,
                        gpointer data)
{
    struct window *w = (struct window *)data;
    int xr, yr, wr, hr;

#define OVERLAP_RECT(x1,y1,w1,h1,x2,y2,w2,h2) do { \
    xr = ((x1) > (x2) ? (x1) : (x2)); \
    yr = ((y1) > (y2) ? (y1) : (y2)); \
    wr = ((x1)+(w1) < (x2)+(w2) ? (x1)+(w1) : (x2)+(w2)) - xr; \
    hr = ((y1)+(h1) < (y2)+(h2) ? (y1)+(h1) : (y2)+(h2)) - yr; \
} while (0)
#define RECT_EXISTS (wr > 0 && hr > 0)

    if (w->pm) {
        OVERLAP_RECT(event->area.x, event->area.y,
                     event->area.width, event->area.height,
                     w->ox, w->oy, w->iw, w->ih);
        if (RECT_EXISTS)
            gdk_draw_pixmap(widget->window,
                            widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                            w->pm,
                            xr - w->ox, yr - w->oy, xr, yr, wr, hr);
    }

    /*
     * Black border around the image.
     */
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 w->ox - BORDER, w->oy - BORDER, w->iw + BORDER, BORDER);
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 w->ox + w->iw, w->oy - BORDER, BORDER, w->ih + BORDER);
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 w->ox - BORDER, w->oy, BORDER, w->ih + BORDER);
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 w->ox, w->oy + w->ih, w->iw + BORDER, BORDER);
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);

    /*
     * Background everywhere else.
     */
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 0, 0, w->ox + w->iw + BORDER, w->oy - BORDER);
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->bg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 w->ox + w->iw + BORDER, 0,
                 w->ww - (w->ox + w->iw + BORDER), w->oy + w->ih + BORDER);
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->bg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 0, w->oy - BORDER, w->ox - BORDER, w->wh - (w->oy - BORDER));
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->bg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);
    OVERLAP_RECT(event->area.x, event->area.y,
                 event->area.width, event->area.height,
                 w->ox - BORDER, w->oy + w->ih + BORDER,
                 w->ww - (w->ox - BORDER), w->wh - (w->oy + w->ih + BORDER));
    if (RECT_EXISTS)
        gdk_draw_rectangle(widget->window,
                           widget->style->bg_gc[GTK_WIDGET_STATE(widget)],
                           1, xr, yr, wr, hr);

#undef OVERLAP_RECT
#undef RECT_EXISTS

    return TRUE;
}

static gint configure_area(GtkWidget *widget,
                           GdkEventConfigure *event, gpointer data)
{
    struct window *w = (struct window *)data;

    w->ww = event->width;
    w->wh = event->height;
    w->ox = (event->width - w->iw) / 2;
    w->oy = (event->height - w->ih) / 2;

    return TRUE;
}

static int win_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct window *w = (struct window *)data;

    if (event->string[0] && !event->string[1]) {
	int c = event->string[0];

	if (c == 'q' || c == 'Q' || c == '\x11') {
	    gtk_widget_destroy(w->window);/* quit */
	    return TRUE;
	}
	if ((c == ' ' || c == 'n' || c == 'N') && w->pos+1 < w->il->n) {
	    switch_to_image(w, w->pos+1);
	    return TRUE;
	}
	if ((c == '\010' || c == '\177' ||
             c == 'b' || c == 'B' ||
             c == 'p' || c == 'P') && w->pos > 0) {
	    switch_to_image(w, w->pos-1);
	    return TRUE;
	}
    } else {
	int c = event->keyval;

	if (c == GDK_BackSpace && w->pos > 0) {
	    switch_to_image(w, w->pos-1);
	    return TRUE;
	}
    }

    return FALSE;
}

static struct window *new_window(struct ilist *il, int maxsize)
{
    struct window *w = snew(struct window);

    w->il = il;

    w->pm = NULL;

    w->use_max_size = maxsize;

    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    w->area = gtk_drawing_area_new();
    w->ww = w->wh = 0;

    gtk_widget_show(w->area);
    gtk_container_add(GTK_CONTAINER(w->window), w->area);

    gtk_signal_connect(GTK_OBJECT(w->window), "destroy",
                       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_signal_connect(GTK_OBJECT(w->window), "key_press_event",
		       GTK_SIGNAL_FUNC(win_key_press), w);
    gtk_signal_connect(GTK_OBJECT(w->area), "expose_event",
		       GTK_SIGNAL_FUNC(expose_area), w);
    gtk_signal_connect(GTK_OBJECT(w->area), "configure_event",
		       GTK_SIGNAL_FUNC(configure_area), w);

    switch_to_image(w, 0);             /* just to calculate the size */

    gtk_widget_show(w->window);

    switch_to_image(w, 0);             /* now actually create the pixmap */

    return w;
}

static void set_root_window(image *im)
{
    int iw, ih;
    GdkPixmap *pm;
    int screen;
    Window rootwin;

    iw = image_width(im);
    ih = image_height(im);

    screen = DefaultScreen(GDK_DISPLAY());
    rootwin = RootWindow(GDK_DISPLAY(), screen);

    pm = gdk_pixmap_new(gdk_window_foreign_new(rootwin), iw, ih, -1);
    image_to_pixmap(im, pm, iw, ih);

    XSetWindowBackgroundPixmap(GDK_DISPLAY(), rootwin, GDK_WINDOW_XWINDOW(pm));
    XClearWindow(GDK_DISPLAY(), rootwin);
    XSetCloseDownMode(GDK_DISPLAY(), RetainPermanent);
}

int main(int argc, char **argv)
{
    struct ilist *il;
    int opts = TRUE, errs = FALSE, nogo = FALSE, maxsize = FALSE;
    int ignoreloaderrs = FALSE, rootwin = FALSE;

    /*
     * GTK will gratuitously eat an argument which is a single
     * minus sign, so we must tinker with the argc/argv we feed
     * them. Annoying, but there we go.
     */
    {
	int gtk_argc;
	char **gtk_argv;
	int trail_argc;
	char **trail_argv;

	for (gtk_argc = 1; gtk_argc < argc; gtk_argc++)
	    if (!strcmp(argv[gtk_argc], "-"))
		break;
	gtk_argv = argv;

	trail_argc = argc - gtk_argc;
	trail_argv = argv + gtk_argc;

	gtk_init(&gtk_argc, &argv);

	/*
	 * Now concatenate the remains of gtk_argv with trail_argv.
	 */
	argc = gtk_argc + trail_argc;
	argv = snewn(argc, char *);
	memcpy(argv, gtk_argv, gtk_argc * sizeof(char *));
	memcpy(argv + gtk_argc, trail_argv, trail_argc * sizeof(char *));
    }

    image_init();

    il = ilist_new();

    /*
     * Parse command line arguments.
     */
    while (--argc) {
	char *p = *++argv;
	if (*p == '-' && p[1]) {
	    /*
	     * An option.
	     */
	    while (p && *++p) {
		char c = *p;
		switch (c) {
		  case '-':
		    /*
		     * Long option.
		     */
		    {
			char *opt, *val;
			opt = p++;     /* opt will have _one_ leading - */
			while (*p && *p != '=')
			    p++;	       /* find end of option */
			if (*p == '=') {
			    *p++ = '\0';
			    val = p;
			} else
			    val = NULL;

			assert(opt[0] == '-');
                        if (!opt[1]) {
                            opts = FALSE;   /* all the rest are filenames */
                        } else if (!strcmp(opt, "-maxsize")) {
                            maxsize = TRUE;
                        } else if (!strcmp(opt, "-ignore-load-errors")) {
                            ignoreloaderrs = TRUE;
                        } else if (!strcmp(opt, "-root")) {
                            rootwin = TRUE;
			} else if (!strcmp(opt, "-help")) {
			    help();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-version")) {
			    showversion();
			    nogo = TRUE;
			} else if (!strcmp(opt, "-licence") ||
				   !strcmp(opt, "-license")) {
			    licence();
			    nogo = TRUE;
			} else {
			    errs = TRUE;
                            fprintf(stderr, "v: unrecognised command-line"
                                    " option '-%s'\n", opt);
			}
		    }
		    p = NULL;
		    break;
		  case 'h':
		  case 'V':
		  case 'L':
		  case 'm':
		  case 'i':
		    /*
		     * Option requiring no parameter.
		     */
		    switch (c) {
		      case 'h':
			help();
			nogo = TRUE;
			break;
		      case 'V':
			showversion();
			nogo = TRUE;
			break;
		      case 'L':
			licence();
			nogo = TRUE;
			break;
		      case 'm':
			maxsize = TRUE;
			break;
		      case 'i':
			ignoreloaderrs = TRUE;
			break;
		    }
		    break;
#if 0                                  /* currently no options with params */
		  case 'C':
		    /*
		     * Option requiring parameter.
		     */
		    p++;
		    if (!*p && argc > 1)
			--argc, p = *++argv;
		    else if (!*p) {
			char opt[2];
			opt[0] = c;
			opt[1] = '\0';
			errs = TRUE;
                        fprintf(stderr, "v: option '-%c' expects an"
                                " argument\n", c);
		    }
		    /*
		     * Now c is the option and p is the parameter.
		     */
		    switch (c) {
		      case 'C':
                        /* p contains the option value; do something with it */
			break;
		    }
		    p = NULL;	       /* prevent continued processing */
		    break;
#endif
		  default:
		    /*
		     * Unrecognised option.
		     */
                    errs = TRUE;
                    fprintf(stderr, "v: unrecognised command-line"
                            " option '-%c'\n", c);
		}
	    }
	} else if (*p == '-') {
	    /*
	     * A non-option argument which is just a minus sign
	     * means we read filenames from stdin, one per line.
	     */
	    char buf[4096];
	    char *err;
	    while (fgets(buf, sizeof(buf), stdin)) {
		buf[strcspn(buf, "\r\n")] = '\0';
		err = ilist_load(il, buf);
		if (err) {
		    fprintf(stderr, "v: unable to load image '%s': %s\n",
			    buf, err);
		    if (!ignoreloaderrs)
			errs = TRUE;
		}
	    }
	} else {
	    /*
	     * A non-option argument.
	     */
	    char *err = ilist_load(il, p);
	    if (err) {
		fprintf(stderr, "v: unable to load image '%s': %s\n", p, err);
		if (!ignoreloaderrs)
		    errs = TRUE;
	    }
	}
    }

    if (errs)
	return 1;

    if (il->n == 0) {
        usage();
	nogo = TRUE;
    }

    if (nogo)
        return 0;

    if (rootwin) {
        set_root_window(il->images[0]);
    } else {
        new_window(il, maxsize);
        gtk_main();
    }

    return 0;
}

/* $Id: macterm.c,v 1.1.2.30 1999/03/28 02:06:10 ben Exp $ */
/*
 * Copyright (c) 1999 Ben Harris
 * All rights reserved.
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
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * macterm.c -- Macintosh terminal front-end
 */

#include <MacTypes.h>
#include <Controls.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <MacMemory.h>
#include <MacWindows.h>
#include <MixedMode.h>
#include <Palettes.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Resources.h>
#include <Scrap.h>
#include <Script.h>
#include <Sound.h>
#include <ToolUtils.h>

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "macresid.h"
#include "putty.h"
#include "mac.h"

#define DEFAULT_FG	16
#define DEFAULT_FG_BOLD	17
#define DEFAULT_BG	18
#define DEFAULT_BG_BOLD	19
#define CURSOR_FG	20
#define CURSOR_FG_BOLD	21
#define CURSOR_BG	22
#define CURSOR_BG_BOLD	23

#define PTOCC(x) ((x) < 0 ? -(-(x - font_width - 1) / font_width) : \
			    (x) / font_width)
#define PTOCR(y) ((y) < 0 ? -(-(y - font_height - 1) / font_height) : \
			    (y) / font_height)

struct mac_session {
    short		fontnum;
    int			font_ascent;
    int			font_leading;
    WindowPtr		window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
    WCTabHandle		wctab;
};

static void mac_initfont(struct mac_session *);
static void mac_initpalette(struct mac_session *);
static void mac_adjustwinbg(struct mac_session *);
static void mac_adjustsize(struct mac_session *, int, int);
static void mac_drawgrowicon(struct mac_session *s);
static pascal void mac_scrolltracker(ControlHandle, short);
static pascal void do_text_for_device(short, short, GDHandle, long);
static pascal void mac_set_attr_mask(short, short, GDHandle, long);
static int mac_keytrans(struct mac_session *, EventRecord *, unsigned char *);
static void text_click(struct mac_session *, EventRecord *);

#if TARGET_RT_MAC_CFM
static RoutineDescriptor mac_scrolltracker_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppControlActionProcInfo,
			     (ProcPtr)mac_scrolltracker);
static RoutineDescriptor do_text_for_device_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppDeviceLoopDrawingProcInfo,
			     (ProcPtr)do_text_for_device);
static RoutineDescriptor mac_set_attr_mask_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppDeviceLoopDrawingProcInfo,
			     (ProcPtr)mac_set_attr_mask);
#else /* not TARGET_RT_MAC_CFM */
#define mac_scrolltracker_upp	mac_scrolltracker
#define do_text_for_device_upp	do_text_for_device
#define mac_set_attr_mask_upp	mac_set_attr_mask
#endif /* not TARGET_RT_MAC_CFM */

/*
 * Temporary hack till I get the terminal emulator supporting multiple
 * sessions
 */

static struct mac_session *onlysession;

static void inbuf_putc(int c) {
    inbuf[inbuf_head] = c;
    inbuf_head = (inbuf_head+1) & INBUF_MASK;
}

static void inbuf_putstr(const char *c) {
    while (*c)
	inbuf_putc(*c++);
}

static void display_resource(unsigned long type, short id) {
    Handle h;
    int len, i;
    char *t;

    h = GetResource(type, id);
    if (h == NULL)
	fatalbox("Can't get test resource");
    SetResAttrs(h, GetResAttrs(h) | resLocked);
    t = *h;
    len = GetResourceSizeOnDisk(h);
    for (i = 0; i < len; i++) {
	inbuf_putc(t[i]);
	term_out();
    }
    SetResAttrs(h, GetResAttrs(h) & ~resLocked);
    ReleaseResource(h);
}
	

void mac_newsession(void) {
    struct mac_session *s;
    UInt32 starttime;
    char msg[128];

    /* This should obviously be initialised by other means */
    mac_loadconfig(&cfg);
    back = &loop_backend;
    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    onlysession = s;
	
    /* XXX: Own storage management? */
    if (mac_gestalts.qdvers == gestaltOriginalQD)
	s->window = GetNewWindow(wTerminal, NULL, (WindowPtr)-1);
    else
	s->window = GetNewCWindow(wTerminal, NULL, (WindowPtr)-1);
    SetWRefCon(s->window, (long)s);
    s->scrollbar = GetNewControl(cVScroll, s->window);
    term_init();
    term_size(cfg.height, cfg.width, cfg.savelines);
    mac_initfont(s);
    mac_initpalette(s);
    attr_mask = ATTR_MASK;
    /* Set to FALSE to not get palette updates in the background. */
    SetPalette(s->window, s->palette, TRUE); 
    ActivatePalette(s->window);
    ShowWindow(s->window);
    starttime = TickCount();
    display_resource('pTST', 128);
    sprintf(msg, "Elapsed ticks: %d\015\012", TickCount() - starttime);
    inbuf_putstr(msg);
    term_out();
}

static void mac_initfont(struct mac_session *s) {
    Str255 macfont;
    FontInfo fi;
 
    SetPort(s->window);
    macfont[0] = sprintf((char *)&macfont[1], "%s", cfg.font);
    GetFNum(macfont, &s->fontnum);
    TextFont(s->fontnum);
    TextFace(cfg.fontisbold ? bold : 0);
    TextSize(cfg.fontheight);
    GetFontInfo(&fi);
    font_width = CharWidth('W'); /* Well, it's what NCSA uses. */
    s->font_ascent = fi.ascent;
    s->font_leading = fi.leading;
    font_height = s->font_ascent + fi.descent + s->font_leading;
    mac_adjustsize(s, rows, cols);
}

/*
 * To be called whenever the window size changes.
 * rows and cols should be desired values.
 * It's assumed the terminal emulator will be informed, and will set rows
 * and cols for us.
 */
static void mac_adjustsize(struct mac_session *s, int newrows, int newcols) {
    int winwidth, winheight;

    winwidth = newcols * font_width + 15;
    winheight = newrows * font_height;
    SizeWindow(s->window, winwidth, winheight, true);
    HideControl(s->scrollbar);
    MoveControl(s->scrollbar, winwidth - 15, -1);
    SizeControl(s->scrollbar, 16, winheight - 13);
    ShowControl(s->scrollbar);
}

static void mac_initpalette(struct mac_session *s) {
  
    if (mac_gestalts.qdvers == gestaltOriginalQD)
	return;
    s->palette = NewPalette((*cfg.colours)->pmEntries, NULL, pmCourteous, 0);
    if (s->palette == NULL)
	fatalbox("Unable to create palette");
    CopyPalette(cfg.colours, s->palette, 0, 0, (*cfg.colours)->pmEntries);
    mac_adjustwinbg(s);
}

/*
 * Set the background colour of the window correctly.  Should be
 * called whenever the default background changes.
 */
static void mac_adjustwinbg(struct mac_session *s) {

#if 0 /* XXX doesn't link (at least for 68k) */
    if (mac_gestalts.windattr & gestaltWindowMgrPresent)
	SetWindowContentColor(s->window,
			      &(*s->palette)->pmInfo[DEFAULT_BG].ciRGB);
    else
#endif
    {
	if (s->wctab == NULL)
	    s->wctab = (WCTabHandle)NewHandle(sizeof(**s->wctab));
	if (s->wctab == NULL)
	    return; /* do without */
	(*s->wctab)->wCSeed = 0;
	(*s->wctab)->wCReserved = 0;
	(*s->wctab)->ctSize = 0;
	(*s->wctab)->ctTable[0].value = wContentColor;
	(*s->wctab)->ctTable[0].rgb = (*s->palette)->pmInfo[DEFAULT_BG].ciRGB;
	SetWinColor(s->window, s->wctab);
    }
}

/*
 * Set the cursor shape correctly
 */
void mac_adjusttermcursor(WindowPtr window, Point mouse, RgnHandle cursrgn) {
    struct mac_session *s;
    ControlHandle control;
    short part;
    int x, y;

    SetPort(window);
    s = (struct mac_session *)GetWRefCon(window);
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control == s->scrollbar) {
	SetCursor(&qd.arrow);
	RectRgn(cursrgn, &(*s->scrollbar)->contrlRect);
	SectRgn(cursrgn, window->visRgn, cursrgn);
    } else {
	x = mouse.h / font_width;
	y = mouse.v / font_height;
	SetCursor(*GetCursor(iBeamCursor));
	/* Ask for shape changes if we leave this character cell. */
	SetRectRgn(cursrgn, x * font_width, y * font_height,
		   (x + 1) * font_width, (y + 1) * font_height);
	SectRgn(cursrgn, window->visRgn, cursrgn);
    }
}

/*
 * Enable/disable menu items based on the active terminal window.
 */
void mac_adjusttermmenus(WindowPtr window) {
    struct mac_session *s;
    MenuHandle menu;
    long offset;

    s = (struct mac_session *)GetWRefCon(window);
    menu = GetMenuHandle(mEdit);
    EnableItem(menu, 0);
    DisableItem(menu, iUndo);
    DisableItem(menu, iCut);
    if (term_hasselection())
	EnableItem(menu, iCopy);
    else
	DisableItem(menu, iCopy);
    if (GetScrap(NULL, 'TEXT', &offset) == noTypeErr)
	DisableItem(menu, iPaste);
    else
	EnableItem(menu, iPaste);
    DisableItem(menu, iClear);
    EnableItem(menu, iSelectAll);
}

void mac_menuterm(WindowPtr window, short menu, short item) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    switch (menu) {
      case mEdit:
	switch (item) {
	  case iCopy:
	    term_copy();
	    break;
	  case iPaste:
	    term_paste();
	    break;
	}
    }
}
	    
void mac_clickterm(WindowPtr window, EventRecord *event) {
    struct mac_session *s;
    Point mouse;
    ControlHandle control;
    int part;

    s = (struct mac_session *)GetWRefCon(window);
    SetPort(window);
    mouse = event->where;
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control == s->scrollbar) {
	switch (part) {
	  case kControlIndicatorPart:
	    if (TrackControl(control, mouse, NULL) == kControlIndicatorPart)
		term_scroll(+1, GetControlValue(control));
	    break;
	  case kControlUpButtonPart:
	  case kControlDownButtonPart:
	  case kControlPageUpPart:
	  case kControlPageDownPart:
	    TrackControl(control, mouse, &mac_scrolltracker_upp);
	    break;
	}
    } else {
	text_click(s, event);
    }
}

static void text_click(struct mac_session *s, EventRecord *event) {
    Point localwhere;
    int row, col;
    static UInt32 lastwhen = 0;
    static struct mac_session *lastsess = NULL;
    static int lastrow = -1, lastcol = -1;
    static Mouse_Action lastact = MA_NOTHING;

    SetPort(s->window);
    localwhere = event->where;
    GlobalToLocal(&localwhere);

    col = PTOCC(localwhere.h);
    row = PTOCR(localwhere.v);
    if (event->when - lastwhen < GetDblTime() &&
	row == lastrow && col == lastcol && s == lastsess)
	lastact = (lastact == MA_CLICK ? MA_2CLK :
		   lastact == MA_2CLK ? MA_3CLK :
		   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    else
	lastact = MA_CLICK;
    term_mouse(event->modifiers & shiftKey ? MB_EXTEND : MB_SELECT, lastact,
	       col, row);
    lastsess = s;
    lastrow = row;
    lastcol = col;
    while (StillDown()) {
	GetMouse(&localwhere);
	col = PTOCC(localwhere.h);
	row = PTOCR(localwhere.v);
	term_mouse(event->modifiers & shiftKey ? MB_EXTEND : MB_SELECT,
		   MA_DRAG, col, row);
	if (row > rows - 1)
	    term_scroll(0, row - (rows - 1));
	else if (row < 0)
	    term_scroll(0, row);
    }
    term_mouse(event->modifiers & shiftKey ? MB_EXTEND : MB_SELECT, MA_RELEASE,
	       col, row);
    lastwhen = TickCount();
}

void write_clip(void *data, int len) {
    
    if (ZeroScrap() != noErr)
	return;
    PutScrap(len, 'TEXT', data);
}

void get_clip(void **p, int *lenp) {
    static Handle h = NULL;
    long offset;

    if (p == NULL) {
	/* release memory */
	if (h != NULL)
	    DisposeHandle(h);
	h = NULL;
    } else
	if (GetScrap(NULL, 'TEXT', &offset) > 0) {
	    h = NewHandle(0);
	    *lenp = GetScrap(h, 'TEXT', &offset);
	    HLock(h);
	    *p = *h;
	    if (*p == NULL || *lenp <= 0)
		fatalbox("Empty scrap");
	} else {
	    *p = NULL;
	    *lenp = 0;
	}
}

static pascal void mac_scrolltracker(ControlHandle control, short part) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon((*control)->contrlOwner);
    switch (part) {
      case kControlUpButtonPart:
	term_scroll(0, -1);
	break;
      case kControlDownButtonPart:
	term_scroll(0, +1);
	break;
      case kControlPageUpPart:
	term_scroll(0, -(rows - 1));
	break;
      case kControlPageDownPart:
	term_scroll(0, +(rows - 1));
	break;
    }
}

#define K_SPACE	0x3100
#define K_BS	0x3300
#define K_F1	0x7a00
#define K_F2	0x7800
#define K_F3	0x6300
#define K_F4	0x7600
#define K_F5	0x6000
#define K_F6	0x6100
#define K_F7	0x6200
#define K_F8	0x6400
#define K_F9	0x6500
#define K_F10	0x6d00
#define K_F11	0x6700
#define K_F12	0x6f00
#define K_INSERT 0x7200
#define K_HOME	0x7300
#define K_PRIOR	0x7400
#define K_DELETE 0x7500
#define K_END	0x7700
#define K_NEXT	0x7900
#define K_LEFT	0x7b00
#define K_RIGHT	0x7c00
#define K_DOWN	0x7d00
#define K_UP	0x7e00
#define KP_0	0x5200
#define KP_1	0x5300
#define KP_2	0x5400
#define KP_3	0x5500
#define KP_4	0x5600
#define KP_5	0x5700
#define KP_6	0x5800
#define KP_7	0x5900
#define KP_8	0x5b00
#define KP_9	0x5c00
#define KP_CLEAR 0x4700
#define KP_EQUAL 0x5100
#define KP_SLASH 0x4b00
#define KP_STAR	0x4300
#define KP_PLUS	0x4500
#define KP_MINUS 0x4e00
#define KP_DOT	0x4100
#define KP_ENTER 0x4c00

void mac_keyterm(WindowPtr window, EventRecord *event) {
    unsigned char buf[20];
    int len;
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    len = mac_keytrans(s, event, buf);
    back->send((char *)buf, len);
}

static UInt32 mac_rekey(EventModifiers newmodifiers, UInt32 oldmessage) {
    UInt32 transresult, state;
    Ptr kchr;

    state = 0;
    kchr = (Ptr)GetScriptManagerVariable(smKCHRCache);
    transresult = KeyTranslate(kchr,
			       (oldmessage & keyCodeMask) >> 8 |
			       newmodifiers & 0xff00,
			       &state);
    /*
     * KeyTranslate returns two character codes.  We only worry about
     * one.  Yes, this is slightly bogus, but it makes life less
     * painful.
     */
    return oldmessage & ~charCodeMask | transresult & 0xff;
}


static int mac_keytrans(struct mac_session *s, EventRecord *event,
			unsigned char *output) {
    unsigned char *p = output;
    int code;

    /* No meta key yet -- that'll be rather fun. */

    /* Check if the meta "key" was held down */

    if ((event->modifiers & cfg.meta_modifiers) == cfg.meta_modifiers) {
	*p++ = '\033';
	event->modifiers &= ~cfg.meta_modifiers;
	event->message = mac_rekey(event->modifiers, event->message);
    }

    /* Keys that we handle locally */
    if (event->modifiers & shiftKey) {
	switch (event->message & keyCodeMask) {
	  case K_PRIOR: /* shift-pageup */
	    term_scroll(0, -(rows - 1));
	    return 0;
	  case K_NEXT:  /* shift-pagedown */
	    term_scroll(0, +(rows - 1));
	    return 0;
	}
    }

    /*
     * Control-2 should return ^@ (0x00), Control-6 should return
     * ^^ (0x1E), and Control-Minus should return ^_ (0x1F). Since
     * the DOS keyboard handling did it, and we have nothing better
     * to do with the key combo in question, we'll also map
     * Control-Backquote to ^\ (0x1C).
     */

    if (event->modifiers & controlKey) {
	switch (event->message & charCodeMask) {
	  case ' ': case '2':
	    *p++ = 0x00;
	    return p - output;
	  case '`':
	    *p++ = 0x1c;
	    return p - output;
	  case '6':
	    *p++ = 0x1e;
	    return p - output;
	  case '/':
	    *p++ = 0x1f;
	    return p - output;
	}
    }

    /*
     * First, all the keys that do tilde codes. (ESC '[' nn '~',
     * for integer decimal nn.)
     *
     * We also deal with the weird ones here. Linux VCs replace F1
     * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
     * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
     * respectively.
     */
    code = 0;
    switch (event->message & keyCodeMask) {
      case K_F1: code = (event->modifiers & shiftKey ? 23 : 11); break;
      case K_F2: code = (event->modifiers & shiftKey ? 24 : 12); break;
      case K_F3: code = (event->modifiers & shiftKey ? 25 : 13); break;
      case K_F4: code = (event->modifiers & shiftKey ? 26 : 14); break;
      case K_F5: code = (event->modifiers & shiftKey ? 28 : 15); break;
      case K_F6: code = (event->modifiers & shiftKey ? 29 : 17); break;
      case K_F7: code = (event->modifiers & shiftKey ? 31 : 18); break;
      case K_F8: code = (event->modifiers & shiftKey ? 32 : 19); break;
      case K_F9: code = (event->modifiers & shiftKey ? 33 : 20); break;
      case K_F10: code = (event->modifiers & shiftKey ? 34 : 21); break;
      case K_F11: code = 23; break;
      case K_F12: code = 24; break;
      case K_HOME: code = 1; break;
      case K_INSERT: code = 2; break;
      case K_DELETE: code = 3; break;
      case K_END: code = 4; break;
      case K_PRIOR: code = 5; break;
      case K_NEXT: code = 6; break;
    }
    if (cfg.linux_funkeys && code >= 11 && code <= 15) {
	p += sprintf((char *)p, "\x1B[[%c", code + 'A' - 11);
	return p - output;
    }
    if (cfg.rxvt_homeend && (code == 1 || code == 4)) {
	p += sprintf((char *)p, code == 1 ? "\x1B[H" : "\x1BOw");
	return p - output;
    }
    if (code) {
	p += sprintf((char *)p, "\x1B[%d~", code);
	return p - output;
    }

    if (app_keypad_keys) {
	switch (event->message & keyCodeMask) {
	  case KP_ENTER: p += sprintf((char *)p, "\x1BOM"); return p - output;
	  case KP_CLEAR: p += sprintf((char *)p, "\x1BOP"); return p - output;
	  case KP_EQUAL: p += sprintf((char *)p, "\x1BOQ"); return p - output;
	  case KP_SLASH: p += sprintf((char *)p, "\x1BOR"); return p - output;
	  case KP_STAR:  p += sprintf((char *)p, "\x1BOS"); return p - output;
	  case KP_PLUS:  p += sprintf((char *)p, "\x1BOl"); return p - output;
	  case KP_MINUS: p += sprintf((char *)p, "\x1BOm"); return p - output;
	  case KP_DOT:   p += sprintf((char *)p, "\x1BOn"); return p - output;
	  case KP_0:     p += sprintf((char *)p, "\x1BOp"); return p - output;
	  case KP_1:     p += sprintf((char *)p, "\x1BOq"); return p - output;
	  case KP_2:     p += sprintf((char *)p, "\x1BOr"); return p - output;
	  case KP_3:     p += sprintf((char *)p, "\x1BOs"); return p - output;
	  case KP_4:     p += sprintf((char *)p, "\x1BOt"); return p - output;
	  case KP_5:     p += sprintf((char *)p, "\x1BOu"); return p - output;
	  case KP_6:     p += sprintf((char *)p, "\x1BOv"); return p - output;
	  case KP_7:     p += sprintf((char *)p, "\x1BOw"); return p - output;
	  case KP_8:     p += sprintf((char *)p, "\x1BOx"); return p - output;
	  case KP_9:     p += sprintf((char *)p, "\x1BOy"); return p - output;
	}
    }

    switch (event->message & keyCodeMask) {
      case K_UP:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOA" : "\x1B[A");
	return p - output;
      case K_DOWN:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOB" : "\x1B[B");
	return p - output;
      case K_RIGHT:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOC" : "\x1B[C");
	return p - output;
      case K_LEFT:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOD" : "\x1B[D");
	return p - output;
      case K_BS:
	*p++ = (cfg.bksp_is_delete ? 0x7f : 0x08);
	return p - output;
      default:
	*p++ = event->message & charCodeMask;
	return p - output;
    }
}

void mac_growterm(WindowPtr window, EventRecord *event) {
    Rect limits;
    long grow_result;
    int newrows, newcols;
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    SetRect(&limits, font_width + 15, font_height, SHRT_MAX, SHRT_MAX);
    grow_result = GrowWindow(window, event->where, &limits);
    if (grow_result != 0) {
	newrows = HiWord(grow_result) / font_height;
	newcols = (LoWord(grow_result) - 15) / font_width;
	mac_adjustsize(s, newrows, newcols);
	term_size(newrows, newcols, cfg.savelines);
    }
}

void mac_activateterm(WindowPtr window, Boolean active) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    has_focus = active;
    term_update();
    if (active)
	ShowControl(s->scrollbar);
    else {
	PmBackColor(DEFAULT_BG); /* HideControl clears behind the control */
	HideControl(s->scrollbar);
    }
    mac_drawgrowicon(s);
}

void mac_updateterm(WindowPtr window) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    BeginUpdate(window);
    get_ctx();
    term_paint(s,
	       (*window->visRgn)->rgnBBox.left,
	       (*window->visRgn)->rgnBBox.top,
	       (*window->visRgn)->rgnBBox.right,
	       (*window->visRgn)->rgnBBox.bottom);
    /* Restore default colours in case the Window Manager uses them */
    PmForeColor(DEFAULT_FG);
    PmBackColor(DEFAULT_BG);
    if (FrontWindow() != window)
	EraseRect(&(*s->scrollbar)->contrlRect);
    UpdateControls(window, window->visRgn);
    mac_drawgrowicon(s);
    free_ctx(NULL);
    EndUpdate(window);
}

static void mac_drawgrowicon(struct mac_session *s) {
    Rect clip;

    SetPort(s->window);
    /* Stop DrawGrowIcon giving us space for a horizontal scrollbar */
    SetRect(&clip, s->window->portRect.right - 15, SHRT_MIN,
	    SHRT_MAX, SHRT_MAX);
    ClipRect(&clip);
    DrawGrowIcon(s->window);
    clip.left = SHRT_MIN;
    ClipRect(&clip);
}    

struct do_text_args {
    struct mac_session *s;
    Rect textrect;
    Rect leadrect;
    char *text;
    int len;
    unsigned long attr;
};

/*
 * Call from the terminal emulator to draw a bit of text
 *
 * x and y are text row and column (zero-based)
 */
void do_text(struct mac_session *s, int x, int y, char *text, int len,
	     unsigned long attr) {
    int style = 0;
    struct do_text_args a;
    RgnHandle textrgn;

    SetPort(s->window);
    
    /* First check this text is relevant */
    a.textrect.top = y * font_height;
    a.textrect.bottom = (y + 1) * font_height;
    a.textrect.left = x * font_width;
    a.textrect.right = (x + len) * font_width;
    if (!RectInRgn(&a.textrect, s->window->visRgn))
	return;

    a.s = s;
    a.text = text;
    a.len = len;
    a.attr = attr;
    if (s->font_leading > 0)
	SetRect(&a.leadrect,
		a.textrect.left, a.textrect.bottom - s->font_leading,
		a.textrect.right, a.textrect.bottom);
    else
	SetRect(&a.leadrect, 0, 0, 0, 0);
    SetPort(s->window);
    TextFont(s->fontnum);
    if (cfg.fontisbold || (attr & ATTR_BOLD) && !cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextSize(cfg.fontheight);
    SetFractEnable(FALSE); /* We want characters on pixel boundaries */
    textrgn = NewRgn();
    RectRgn(textrgn, &a.textrect);
    DeviceLoop(textrgn, &do_text_for_device_upp, (long)&a, 0);
    DisposeRgn(textrgn);
    /* Tell the window manager about it in case this isn't an update */
    ValidRect(&a.textrect);
}

static pascal void do_text_for_device(short depth, short devflags,
				      GDHandle device, long cookie) {
    struct do_text_args *a;
    int bgcolour, fgcolour, bright;

    a = (struct do_text_args *)cookie;

    bright = (a->attr & ATTR_BOLD) && cfg.bold_colour;

    TextMode(a->attr & ATTR_REVERSE ? notSrcCopy : srcCopy);

    switch (depth) {
      case 1:
	/* XXX This should be done with a _little_ more configurability */
	ForeColor(whiteColor);
	BackColor(blackColor);
	if (a->attr & ATTR_ACTCURS)
	    TextMode(a->attr & ATTR_REVERSE ? srcCopy : notSrcCopy);
	break;
      case 2:
	if (a->attr & ATTR_ACTCURS) {
	    PmForeColor(bright ? CURSOR_FG_BOLD : CURSOR_FG);
	    PmBackColor(CURSOR_BG);
	    TextMode(srcCopy);
	} else {
	    PmForeColor(bright ? DEFAULT_FG_BOLD : DEFAULT_FG);
	    PmBackColor(DEFAULT_BG);
	}
	break;
      default:
	if (a->attr & ATTR_ACTCURS) {
	    fgcolour = bright ? CURSOR_FG_BOLD : CURSOR_FG;
	    bgcolour = CURSOR_BG;
	    TextMode(srcCopy);
	} else {
	    fgcolour = ((a->attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	    bgcolour = ((a->attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
	    if (bright)
		if (a->attr & ATTR_REVERSE)
		    bgcolour++;
		else
		    fgcolour++;
	}
	PmForeColor(fgcolour);
	PmBackColor(bgcolour);
	break;
    }

    if (a->attr & ATTR_REVERSE)
	PaintRect(&a->leadrect);
    else
	EraseRect(&a->leadrect);
    MoveTo(a->textrect.left, a->textrect.top + a->s->font_ascent);
    DrawText(a->text, 0, a->len);

    if (a->attr & ATTR_PASCURS) {
	PenNormal();
	switch (depth) {
	  case 1:
	    PenMode(patXor);
	    break;
	  default:
	    PmForeColor(CURSOR_BG);
	    break;
	}
	FrameRect(&a->textrect);
    }
}

/*
 * Call from the terminal emulator to get its graphics context.
 * Should probably be called start_redraw or something.
 */
struct mac_session *get_ctx(void) {
    struct mac_session *s = onlysession;

    attr_mask = ATTR_INVALID;
    DeviceLoop(s->window->visRgn, &mac_set_attr_mask_upp, (long)s, 0);
    return s;
}

static pascal void mac_set_attr_mask(short depth, short devflags,
				     GDHandle device, long cookie) {

    switch (depth) {
      default:
	attr_mask |= ATTR_FGMASK | ATTR_BGMASK;
	/* FALLTHROUGH */
      case 2:
	attr_mask |= ATTR_BOLD;
	/* FALLTHROUGH */
      case 1:
	attr_mask |= ATTR_UNDER | ATTR_REVERSE | ATTR_ACTCURS |
	    ATTR_PASCURS | ATTR_ASCII | ATTR_GBCHR | ATTR_LINEDRW |
	    (cfg.bold_colour ? 0 : ATTR_BOLD); 
	break;
    }
}

/*
 * Presumably this does something in Windows
 */
void free_ctx(struct mac_session *ctx) {

}

/*
 * Set the scroll bar position
 *
 * total is the line number of the bottom of the working screen
 * start is the line number of the top of the display
 * page is the length of the displayed page
 */
void set_sbar(int total, int start, int page) {
    struct mac_session *s = onlysession;

    /* We don't redraw until we've set everything up, to avoid glitches */
    (*s->scrollbar)->contrlMin = 0;
    (*s->scrollbar)->contrlMax = total - page;
    SetControlValue(s->scrollbar, start);
#if 0
    /* XXX: This doesn't link for me. */
    if (mac_gestalts.cntlattr & gestaltControlMgrPresent)
	SetControlViewSize(s->scrollbar, page);
#endif
}

/*
 * Beep
 */
void beep(void) {

    SysBeep(30);
    /*
     * XXX We should indicate the relevant window and/or use the
     * Notification Manager
     */
}

/*
 * Set icon string -- a no-op here (Windowshade?)
 */
void set_icon(char *icon) {

}

/*
 * Set the window title
 */
void set_title(char *title) {
    Str255 mactitle;
    struct mac_session *s = onlysession;

    mactitle[0] = sprintf((char *)&mactitle[1], "%s", title);
    SetWTitle(s->window, mactitle);
}

/*
 * Resize the window at the emulator's request
 */
void request_resize(int w, int h) {

    cols = w;
    rows = h;
    mac_initfont(onlysession);
}

/*
 * Set the logical palette
 */
void palette_set(int n, int r, int g, int b) {
    RGBColor col;
    struct mac_session *s = onlysession;
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 22
    };
    
    if (mac_gestalts.qdvers == gestaltOriginalQD)
      return;
    col.red   = r * 0x0101;
    col.green = g * 0x0101;
    col.blue  = b * 0x0101;
    SetEntryColor(s->palette, first[n], &col);
    if (first[n] >= 18)
	SetEntryColor(s->palette, first[n]+1, &col);
    if (first[n] == DEFAULT_BG)
	mac_adjustwinbg(s);
    ActivatePalette(s->window);
}

/*
 * Reset to the default palette
 */
void palette_reset(void) {
    struct mac_session *s = onlysession;

    if (mac_gestalts.qdvers == gestaltOriginalQD)
	return;
    CopyPalette(cfg.colours, s->palette, 0, 0, (*cfg.colours)->pmEntries);
    mac_adjustwinbg(s);
    ActivatePalette(s->window);
    /* Palette Manager will generate update events as required. */
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.)
 */
void do_scroll(int topline, int botline, int lines) {
    struct mac_session *s = onlysession;
    Rect r;
    RgnHandle update;

    SetPort(s->window);
    PmBackColor(DEFAULT_BG);
    update = NewRgn();
    SetRect(&r, 0, topline * font_height,
	    cols * font_width, (botline + 1) * font_height);
    ScrollRect(&r, 0, - lines * font_height, update);
    /* XXX: move update region? */
    InvalRgn(update);
    DisposeRgn(update);
}

/*
 * Emacs magic:
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */


/* $Id: mac_res.r,v 1.1.2.17 1999/04/02 12:58:02 ben Exp $ */
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

/* PuTTY resources */

#include "Types.r"
#include "Dialogs.r"
#include "Palettes.r"

/* Get resource IDs we share with C code */
#include "macresid.h"

/*
 * Finder-related resources
 */

/* 'pTTY' is now registered with Apple as PuTTY's signature */

type 'pTTY' as 'STR ';

resource 'pTTY' (0, purgeable) {
    "PuTTY experimental Mac port"
};

resource 'SIZE' (-1) {
    reserved,
    ignoreSuspendResumeEvents,
    reserved,
    cannotBackground,
    needsActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreAppDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    useTextEditServices,
    reserved,
    reserved,
    reserved,
    1024 * 1024,	/* Minimum size */
    1024 * 1024,	/* Preferred size */
};

resource 'FREF' (128, purgeable) {
    /* The application itself */
    'APPL', 128, ""
};

resource 'FREF' (129, purgeable) {
    /* Saved session */
    'Sess', 129, ""
    };

resource 'FREF' (130, purgeable) {
    /* SSH host keys database */
    'HKey', 130, ""
};

resource 'BNDL' (128, purgeable) {
    'pTTY', 0,
    {
	'ICN#', {
	    128, 128,
	    129, 129,
	    130, 130
	},
	'FREF', {
	    128, 128,
	    129, 129,
	    130, 130
	};
    };
};

/* Icons, courtesy of DeRez */

/* Application icon */
resource 'ICN#' (128, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"00003FFE 00004001 00004FF9 00005005"
		$"00005355 00004505 00005A05 00002405"
		$"00004A85 00019005 000223F9 00047C01"
		$"00180201 7FA00C7D 801F1001 9FE22001"
		$"A00CDFFE AA892002 A0123FFE A82C0000"
		$"A0520000 AA6A0000 A00A0000 9FF20000"
		$"80020000 80020000 90FA0000 80020000"
		$"80020000 7FFC0000 40040000 7FFC",
		/* [2] */
		$"00003FFE 00007FFF 00007FFF 00007FFF"
		$"00007FFF 00007FFF 00007FFF 00007FFF"
		$"00007FFF 0001FFFF 0003FFFF 0007FFFF"
		$"001FFFFF 7FFFFFFF FFFFFFFF FFFFFFFF"
		$"FFFFFFFE FFFF3FFE FFFE3FFE FFFE0000"
		$"FFFE0000 FFFE0000 FFFE0000 FFFE0000"
		$"FFFE0000 FFFE0000 FFFE0000 FFFE0000"
		$"FFFE0000 7FFC0000 7FFC0000 7FFC"
	}
};

resource 'icl4' (128, purgeable) {
	$"000000000000000000FFFFFFFFFFFFF0"
	$"00000000000000000FCCCCCCCCCCCCCF"
	$"00000000000000000FCEEEEEEEEEEECF"
	$"00000000000000000FCE0D0D0D0D0CCF"
	$"00000000000000000FCED0FFD0D0D0CF"
	$"00000000000000000FCE0F1F0D0D0CCF"
	$"00000000000000000FCFF1F0D0D0D0CF"
	$"00000000000000000FF11F0D0D0D0CCF"
	$"00000000000000000F11F0D0D0D0D0CF"
	$"000000000000000FF11F0D0D0D0D0CCF"
	$"00000000000000F111FEC0C0C0C0C0CF"
	$"0000000000000F111FFFFFCCCCCCCCCF"
	$"00000000000FF111111111FCCCCCCCCF"
	$"0FFFFFFFFFF111111111FFCCCFFFFFCF"
	$"FCCCCCCCCCCFFFFF111F3CCCCCCCCCCF"
	$"FCEEEEEEEEEEECF111FCCCCCCCCCCCCF"
	$"FCE0D0D0D0D0FF11FFFFFFFFFFFFFFF0"
	$"FCED0D0D0D0DF11F00FCCCDDDEEEEAF0"
	$"FCE0D0D0D0DF11F000FFFFFFFFFFFFF0"
	$"FCED0D0D0DF1FFF00000000000000000"
	$"FCE0D0D0DF1FCCF00000000000000000"
	$"FCED0D0D0FFD0CF00000000000000000"
	$"FCE0D0D0D0D0CCF00000000000000000"
	$"FCEC0C0C0C0C0CF00000000000000000"
	$"FCCCCCCCCCCCCCF00000000000000000"
	$"FCCCCCCCCCCCCCF00000000000000000"
	$"FC88CCCCFFFFFCF00000000000000000"
	$"FC33CCCCCCCCCCF00000000000000000"
	$"FCCCCCCCCCCCCCF00000000000000000"
	$"0FFFFFFFFFFFFF000000000000000000"
	$"0FCCCDDDEEEEAF000000000000000000"
	$"0FFFFFFFFFFFFF"
};

resource 'icl8' (128, purgeable) {
	$"000000000000000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFFFF00"
	$"0000000000000000000000000000000000FF2B2B2B2B2B2B2B2B2B2B2B2B2BFF"
	$"0000000000000000000000000000000000FF2BFCFCFCFCFCFCFCFCFCFCFC2BFF"
	$"0000000000000000000000000000000000FF2BFC2A2A2A2A2A2A2A2A2A002BFF"
	$"0000000000000000000000000000000000FF2BFC2A2AFFFF2A2A2A2A2A002BFF"
	$"0000000000000000000000000000000000FF2BFC2AFF05FF2A2A2A2A2A002BFF"
	$"0000000000000000000000000000000000FF2BFFFF05FF2A2A2A2A2A2A002BFF"
	$"0000000000000000000000000000000000FFFF0505FF2A2A2A2A2A2A2A002BFF"
	$"0000000000000000000000000000000000FF0505FF2A2A2A2A2A2A2A2A002BFF"
	$"000000000000000000000000000000FFFF0505FF2A2A2A2A2A2A2A2A2A002BFF"
	$"0000000000000000000000000000FF050505FFFC000000000000000000002BFF"
	$"00000000000000000000000000FF050505FFFFFFFFFF2B2B2B2B2B2B2B2B2BFF"
	$"0000000000000000000000FFFF050505050505050505FF2B2B2B2B2B2B2B2BFF"
	$"00FFFFFFFFFFFFFFFFFFFF050505050505050505FFFF2B2B2BFFFFFFFFFF2BFF"
	$"FF2B2B2B2B2B2B2B2B2B2BFFFFFFFFFF050505FFD82B2B2B2B2B2B2B2B2B2BFF"
	$"FF2BFCFCFCFCFCFCFCFCFCFCFC2BFF050505FF2B2B2B2B2B2B2B2B2B2B2B2BFF"
	$"FF2BFC2A2A2A2A2A2A2A2A2AFFFF0505FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00"
	$"FF2BFC2A2A2A2A2A2A2A2A2AFF0505FF0000FF2BF7F8F9FAFAFBFBFCFCFDFF00"
	$"FF2BFC2A2A2A2A2A2A2A2AFF0505FF000000FFFFFFFFFFFFFFFFFFFFFFFFFF00"
	$"FF2BFC2A2A2A2A2A2A2AFF05FFFFFF0000000000000000000000000000000000"
	$"FF2BFC2A2A2A2A2A2AFF05FF002BFF0000000000000000000000000000000000"
	$"FF2BFC2A2A2A2A2A2AFFFF2A002BFF0000000000000000000000000000000000"
	$"FF2BFC2A2A2A2A2A2A2A2A2A002BFF0000000000000000000000000000000000"
	$"FF2BFC000000000000000000002BFF0000000000000000000000000000000000"
	$"FF2B2B2B2B2B2B2B2B2B2B2B2B2BFF0000000000000000000000000000000000"
	$"FF2B2B2B2B2B2B2B2B2B2B2B2B2BFF0000000000000000000000000000000000"
	$"FF2BE3E32B2B2B2BFFFFFFFFFF2BFF0000000000000000000000000000000000"
	$"FF2BD8D82B2B2B2B2B2B2B2B2B2BFF0000000000000000000000000000000000"
	$"FF2B2B2B2B2B2B2B2B2B2B2B2B2BFF0000000000000000000000000000000000"
	$"00FFFFFFFFFFFFFFFFFFFFFFFFFF000000000000000000000000000000000000"
	$"00FF2BF7F8F9FAFAFBFBFCFCFDFF000000000000000000000000000000000000"
	$"00FFFFFFFFFFFFFFFFFFFFFFFFFF"
};
resource 'ics#' (128, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"00FF 0081 00BD 00A5 00A5 00BD FF81 818D"
		$"BD81 A57E A500 BD00 8100 8D00 8100 7E",
		/* [2] */
		$"00FF 00FF 00FF 00FF 00FF 00FF FFFF FFFF"
		$"FFFF FF7E FF00 FF00 FF00 FF00 FF00 7E"
	}
};

/* Known hosts icon */
resource 'ICN#' (130, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"1FFFFC00 10000600 10000500 1FFFFC80"
		$"10000440 10000420 1FFFFFF0 10000010"
		$"13FC0F90 1C03F0F0 15FA8090 150A8090"
		$"1D0B80F0 150A8050 15FA8050 1C038070"
		$"143A8050 14028050 1FFFABF0 12048110"
		$"13FCFF10 1AAAAAB0 10000010 17FFFFD0"
		$"14000050 15252250 15555550 15252250"
		$"14000050 17FFFFD0 10000010 1FFFFFF0",
		/* [2] */
		$"1FFFFC00 1FFFFE00 1FFFFF00 1FFFFF80"
		$"1FFFFFC0 1FFFFFE0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
	}
};

resource 'icl4' (130, purgeable) {
	$"000FFFFFFFFFFFFFFFFFFF0000000000"
	$"000F00000000000000000FF000000000"
	$"000F00000000000000000FCF00000000"
	$"000FFFFFFFFFFFFFFFFFFFCCF0000000"
	$"000F00000000000000000FCCCF000000"
	$"000F00000000000000000FCCCCF00000"
	$"000FFFFFFFFFFFFFFFFFFFFFFFFF0000"
	$"000F00000000000000000000000F0000"
	$"000F00FFFFFFFF000000FFFFF00F0000"
	$"000FFFCCCCCCCCFFFFFFCCCCFFFF0000"
	$"000F0FCEEEEECCF0FCCCCCCCF00F0000"
	$"000F0FCE0D0D0CF0FCCCCCCCF00F0000"
	$"000FFFCED0D0CCFFFCCCCCCCFFFF0000"
	$"000F0FCE0D0D0CF0FCCCCCCCCF0F0000"
	$"000F0FCCC0C0CCF0FCCCCCCCCF0F0000"
	$"000FFFCCCCCCCCFFFCCCCCCCCFFF0000"
	$"000F0FCCCCFFFCF0FCCCCCCCCF0F0000"
	$"000F0FCCCCCCCCF0FCCCCCCCCF0F0000"
	$"000FFFFFFFFFFFFFFDDDDDDFFFFF0000"
	$"000F00FCCDDEEF00FDDDDDDF000F0000"
	$"000F00FFFFFFFF00FFFFFFFF000F0000"
	$"000F0C0C0C0C0C0C0C0C0C0C0C0F0000"
	$"000FC0C0C0C0C0C0C0C0C0C0C0CF0000"
	$"000F0FFFFFFFFFFFFFFFFFFFFF0F0000"
	$"000FCF0000000000000000000FCF0000"
	$"000F0F0F00F00F0F00F000F00F0F0000"
	$"000FCF0F0F0F0F0F0F0F0F0F0FCF0000"
	$"000F0F0F00F00F0F00F000F00F0F0000"
	$"000FCF0000000000000000000FCF0000"
	$"000F0FFFFFFFFFFFFFFFFFFFFF0F0000"
	$"000FC0C0C0C0C0C0C0C0C0C0C0CF0000"
	$"000FFFFFFFFFFFFFFFFFFFFFFFFF"
};
resource 'icl8' (130, purgeable) {
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000000000000000"
	$"000000FF0000000000000000000000000000000000FFFF000000000000000000"
	$"000000FF0000000000000000000000000000000000FFF6FF0000000000000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF6F6FF00000000000000"
	$"000000FF0000000000000000000000000000000000FFF6F6F6FF000000000000"
	$"000000FF0000000000000000000000000000000000FFF6F6F6F6FF0000000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000"
	$"000000FF0000000000000000000000000000000000000000000000FF00000000"
	$"000000FF0000FFFFFFFFFFFFFFFF000000000000FFFFFFFFFF0000FF00000000"
	$"000000FFFFFF2B2B2B2B2B2B2B2BFFFFFFFFFFFF2B2B2B2BFFFFFFFF00000000"
	$"000000FF00FF2BFCFCFCFCFCF82BFF00FF2B2B2B2B2B2B2BFF0000FF00000000"
	$"000000FF00FF2BFC2A2A2A2A002BFF00FF2B2B2B2B2B2B2BFF0000FF00000000"
	$"000000FFFFFF2BFC2A2A2A2A002BFFFFFF2B2B2B2B2B2B2BFFFFFFFF00000000"
	$"000000FF00FF2BFC2A2A2A2A002BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FF00FF2BF800000000002BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FFFFFF2B2B2B2B2B2B2B2BFFFFFF2B2B2B2B2B2B2B2BFFFFFF00000000"
	$"000000FF00FF2B2B2B2BFFFFFF2BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FF00FF2B2B2B2B2B2B2B2BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFF9F9F9F9F9F9FFFFFFFFFF00000000"
	$"000000FF0000FFF7F8F9FAFBFCFF0000FFF9F9F9F9F9F9FF000000FF00000000"
	$"000000FF0000FFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFF000000FF00000000"
	$"000000FFF5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5FF00000000"
	$"000000FFF5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5FF00000000"
	$"000000FFF5FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5FF00000000"
	$"000000FFF5FF00000000000000000000000000000000000000FFF5FF00000000"
	$"000000FFF5FF00FF0000FF0000FF00FF0000FF000000FF0000FFF5FF00000000"
	$"000000FFF5FF00FF00FF00FF00FF00FF00FF00FF00FF00FF00FFF5FF00000000"
	$"000000FFF5FF00FF0000FF0000FF00FF0000FF000000FF0000FFF5FF00000000"
	$"000000FFF5FF00000000000000000000000000000000000000FFF5FF00000000"
	$"000000FFF5FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5FF00000000"
	$"000000FFF5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5FF00000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
};


/*
 * Internal resources
 */

/* Menu bar */

resource 'MBAR' (MBAR_Main, preload) {
    { mApple, mFile, mEdit }
};

resource 'MENU' (mApple, preload) {
    mApple,
    textMenuProc,
    0b11111111111111111111111111111101,
    enabled,
    apple,
    {
	"About PuTTY�",		noicon, nokey, nomark, plain,
	"-",			noicon, nokey, nomark, plain,
    }
};

resource 'MENU' (mFile, preload) {
    mFile,
    textMenuProc,
    0b11111111111111111111111111111011,
    enabled,
    "File",
    {
	"New Session",		noicon, "N",   nomark, plain,
	"Close",		noicon, "W",   nomark, plain,
	"-",			noicon, nokey, nomark, plain,
	"Quit",			noicon, "Q",   nomark, plain,
    }
};

resource 'MENU' (mEdit, preload) {
    mEdit,
    textMenuProc,
    0b11111111111111111111111111111101,
    enabled,
    "Edit",
    {
	"Undo",			noicon, "Z",   nomark, plain,
	"-",			noicon, nokey, nomark, plain,
	"Cut",			noicon, "X",   nomark, plain,
	"Copy",			noicon, "C",   nomark, plain,
	"Paste",		noicon, "V",   nomark, plain,
	"Clear",		noicon, nokey, nomark, plain,
	"Select All",		noicon, "A",   nomark, plain,
    }
};

/* Fatal error box.  Stolen from the Finder. */

resource 'ALRT' (wFatal, "fatalbox", purgeable) {
	{54, 67, 152, 435},
	wFatal,
	beepStages,
	alertPositionMainScreen
};

resource 'DITL' (wFatal, "fatalbox", purgeable) {
	{	/* array DITLarray: 3 elements */
		/* [1] */
		{68, 299, 88, 358},
		Button {
			enabled,
			"OK"
		},
		/* [2] */
		{68, 227, 88, 286},
		StaticText {
			disabled,
			""
		},
		/* [3] */
		{7, 74, 55, 358},
		StaticText {
			disabled,
			"^0"
		}
	}
};

/* Terminal window */

resource 'WIND' (wTerminal, "terminal", purgeable) {
    { 0, 0, 200, 200 },
    zoomDocProc,
    invisible,
    goAway,
    0x0,
    "untitled",
    staggerParentWindowScreen
};

resource 'CNTL' (cVScroll, "vscroll", purgeable) {
    { 0, 0, 48, 16 },
    0, invisible, 0, 0,
    scrollBarProc, 0, ""
};

/* "About" box */

resource 'DLOG' (wAbout, "about", purgeable) {
    { 0, 0, 120, 240 },
    noGrowDocProc,
    invisible,
    goAway,
    wAbout,		/* RefCon -- identifies the window to PuTTY */
    wAbout,		/* DITL ID */
    "About PuTTY",
    alertPositionMainScreen
};

resource 'dlgx' (wAbout, "about", purgeable) {
    versionZero {
	kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

resource 'DITL' (wAbout, "about", purgeable) {
    {
	{ 87, 13, 107, 227 },
	Button { enabled, "View Licence" },
	{ 13, 13, 29, 227 },
	StaticText { disabled, "PuTTY"},
	{ 42, 13, 74, 227 },
	StaticText { disabled, "Some version or other\n"
			       "Copyright � 1997-9 Simon Tatham"},
    }
};

/* Licence box */

resource 'WIND' (wLicence, "licence", purgeable) {
    { 0, 0, 300, 300 },
    noGrowDocProc,
    visible,
    goAway,
    wLicence,
    "PuTTY Licence",
    alertPositionParentWindowScreen
};

type 'TEXT' {
    string;
};

resource 'TEXT' (wLicence, "licence", purgeable) {
    "Copyright � 1997-9 Simon Tatham\n"
    "Portions copyright � 1999 Ben Harris\n"
    "Portions copyright � 1993 Eric Young\n"
    "Portions copyright � 1986 Gary S. Brown\n"
    "\n"    
    "Permission is hereby granted, free of charge, to any person "
    "obtaining a copy of this software and associated documentation "
    "files (the \"Software\"), to deal in the Software without "
    "restriction, including without limitation the rights to use, "
    "copy, modify, merge, publish, distribute, sublicense, and/or "
    "sell copies of the Software, and to permit persons to whom the "
    "Software is furnished to do so, subject to the following "
    "conditions:\n\n"
    
    "The above copyright notice and this permission notice shall be "
    "included in all copies or substantial portions of the Software.\n\n"
    
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
    "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
    "NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR "
    "ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF "
    "CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN "
    "CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    "SOFTWARE."
};

/* Default Preferences */

type PREF_wordness_type {
    wide array [256] {
        integer;
    };
};

/*
 * This resource collects together the various short settings we need.
 * Each area of the system gets its own longword for flags, and then
 * we have the other settings.  Strings are stored as two shorts --
 * the id of a STR# or STR resource, and the index if it's a STR# (0
 * for STR).
 */

type 'pSET' {
    /* Basic boolean options */
    boolean dont_close_on_exit, close_on_exit;
    align long;
    /* SSH options */
    boolean use_pty, no_pty;
    align long;
    /* Telnet options */
    boolean bsd_environ, rfc_environ;
    align long;
    /* Keyboard options */
    boolean backspace, delete;
    boolean std_home_end, rxvt_home_end;
    boolean std_funkeys, linux_funkeys;
    boolean normal_cursor, app_cursor;
    boolean normal_keypad, app_keypad;
    align long;
    /* Terminal options */
    boolean no_dec_om, dec_om;
    boolean no_auto_wrap, auto_wrap;
    boolean no_auto_cr, auto_cr;
    boolean use_icon_name, win_name_always;
    align long;
    /* Colour options */
    boolean bold_font, bold_colour;
    align long;
    /* Selection options */
    boolean no_implicit_copy, implicit_copy;
    align long;
    /* Non-boolean options */
    integer; integer;		/* host */
    longint;			/* port */
    longint prot_telnet = 0, prot_ssh = 1; /* protocol */
    integer; integer;		/* termtype */
    integer; integer;		/* termspeed */
    integer; integer;		/* environmt */
    integer; integer;		/* username */
    longint;			/* width */
    longint;			/* height */
    longint;			/* save_lines */
    integer; unsigned integer;	/* font */
    longint;			/* font_height */
    integer;			/* 'pltt' for colours */
    integer;			/* 'wORD' for wordness */
    integer;			/* meta modifiers */
};

resource 'pSET' (PREF_settings, "settings", purgeable) {
    close_on_exit,
    use_pty,
    rfc_environ,
    delete,
    std_home_end,
    std_funkeys,
    normal_cursor,
    normal_keypad,
    no_dec_om,
    auto_wrap,
    no_auto_cr,
    use_icon_name,
    bold_font,
    no_implicit_copy,
#define PREF_strings 1024
    PREF_strings, 1,		/* host 'STR#' */
    23, prot_telnet,		/* port, protocol */
    PREF_strings, 2,		/* termtype 'STR#' */
    PREF_strings, 3,		/* termspeed 'STR#' */
    PREF_strings, 4,		/* environmt 'STR#' */
    PREF_strings, 5,		/* username */
    80, 24, 200,    		/* width, height, save_lines */
    PREF_strings, 6,		/* font 'STR#' */
    9,				/* font_height */
#define PREF_pltt 1024
    PREF_pltt,			/* colours 'pltt' */
#define PREF_wordness 1024
    PREF_wordness,		/* wordness 'wORD */
    0x900,			/* meta modifiers (cmd+option) */
};

resource 'STR#' (PREF_strings, "strings", purgeable) {
    {
	"nowhere.loopback.edu",
	"xterm",
	"38400,38400",
	"\000",
	"",
	"Monaco",
    }
};

resource PREF_wordness_type (PREF_wordness, "wordness", purgeable) {
    {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,
	1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2,
	1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2
    }
};

/*
 * and _why_ isn't this provided for us?
 */
type 'TMPL' {
    array {
	pstring;		/* Item name */
	literal longint;	/* Item type */
    };
};

resource 'TMPL' (128, "pSET") {
    {
	"Close on exit", 'BBIT',
	"", 'BBIT', /* Must pad to a multiple of 8 */
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"No PTY", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"RFC OLD-ENVIRON", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Delete key sends delete", 'BBIT',
	"rxvt home/end", 'BBIT',
	"Linux function keys", 'BBIT',
	"Application cursor keys", 'BBIT',
	"Application keypad", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Use colour for bold", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Implicit copy", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Host STR# ID", 'DWRD',
	"Host STR# index", 'DWRD',
	"Port", 'DLNG',
	"Protocol", 'DLNG',
	"Termtype STR# ID", 'DWRD',
	"Termtype STR# index", 'DWRD',
	"Termspeed STR# ID", 'DWRD',
	"Termspeed STR# index", 'DWRD',
	"Environ STR# ID", 'DWRD',
	"Environ STR# index", 'DWRD',
	"Username STR# ID", 'DWRD',
	"Username STR# index", 'DWRD',
	"Terminal width", 'DLNG',
	"Terminal height", 'DLNG',
	"Save lines", 'DLNG',
	"Font STR# ID", 'DWRD',
	"Font STR# index", 'DWRD',
	"Font size", 'DLNG',
	"pltt ID", 'DWRD',
	"wORD ID", 'DWRD',
	"meta modifiers", 'HWRD',
    };
};


/*
 * *mutter*  It might be nice if Apple could actually put all the flags in
 * Palettes.r.
 */

#define pmCourteous	0x0000
#define pmDithered	0x0001
#define pmTolerant	0x0002
#define pmAnimated	0x0004
#define pmExplicit	0x0008
#define pmWhite		0x0010
#define pmBlack		0x0020
#define pmInhibitG2	0x0100
#define pmInhibitC2	0x0200
#define pmInhibitG4	0x0400
#define pmInhibitC4	0x0800
#define pmInhibitG8	0x1000
#define pmInhibitC8	0x2000

#define PM_BASIC (pmTolerant | pmInhibitG4 | pmInhibitG8)
#define PM_NORMAL (PM_BASIC | pmInhibitG2 | pmInhibitC2)

resource 'pltt' (PREF_pltt, purgeable) {
    {
	0x0000, 0x0000, 0x0000, PM_NORMAL, 0x2000,	/* black */
	0x5555, 0x5555, 0x5555, PM_NORMAL, 0x2000,	/* bright black */
	0xbbbb, 0x0000, 0x0000, PM_NORMAL, 0x2000,	/* red */
	0xffff, 0x5555, 0x5555, PM_NORMAL, 0x2000,	/* bright red */
	0x0000, 0xbbbb, 0x0000, PM_NORMAL, 0x2000,	/* green */
	0x5555, 0xffff, 0x5555, PM_NORMAL, 0x2000,	/* bright green */
	0xbbbb, 0xbbbb, 0x0000, PM_NORMAL, 0x2000,	/* yellow */
	0xffff, 0xffff, 0x0000, PM_NORMAL, 0x2000,	/* bright yellow */
	0x0000, 0x0000, 0xbbbb, PM_NORMAL, 0x2000,	/* blue */
	0x5555, 0x5555, 0xffff, PM_NORMAL, 0x2000,	/* bright blue */
	0xbbbb, 0x0000, 0xbbbb, PM_NORMAL, 0x2000,	/* magenta */
	0xffff, 0x5555, 0xffff, PM_NORMAL, 0x2000,	/* bright magenta */
	0x0000, 0xbbbb, 0xbbbb, PM_NORMAL, 0x2000,	/* cyan */
	0x5555, 0xffff, 0xffff, PM_NORMAL, 0x2000,	/* bright cyan */
	0xbbbb, 0xbbbb, 0xbbbb, PM_NORMAL, 0x2000,	/* white */
	0xffff, 0xffff, 0xffff, PM_NORMAL, 0x2000,	/* bright white */
	0xbbbb, 0xbbbb, 0xbbbb, PM_BASIC,  0x2000,	/* default fg */
	0xffff, 0xffff, 0xffff, PM_BASIC,  0x2000,	/* default bold fg */
	0x0000, 0x0000, 0x0000, PM_BASIC,  0x2000,	/* default bg */
	0x5555, 0x5555, 0x5555, PM_NORMAL, 0x2000,	/* default bold bg */
	0x0000, 0x0000, 0x0000, PM_NORMAL, 0x2000,	/* cursor bg */
	0x0000, 0x0000, 0x0000, PM_NORMAL, 0x2000,	/* bold cursor bg */
	0x0000, 0xffff, 0x0000, PM_BASIC,  0x2000,	/* cursor fg */
	0x0000, 0xffff, 0x0000, PM_NORMAL, 0x2000,	/* bold cursor fg */
    }
};

read 'pTST' (128, "test data", purgeable) "fragment";

type 'pMAP' {
    hex string;
};

resource 'pMAP' (128, "Latin-1 G1 -> Mac OS Roman") {
    $"20 c1 a2 a3 db b4 00 a4 ac a9 bb c7 c2 00 a8 f8"
    $"a1 b1 00 00 ab b5 a6 e1 fc 00 bc c8 00 00 00 c0"
    $"cb e7 e5 cc 80 81 ae 82 e9 83 e6 e8 ed ea eb ec"
    $"00 84 f1 ee ef cd 85 00 af f4 f2 f3 86 00 00 a7"
    $"88 87 89 8b 8a 8c be 8d 8f 8e 90 91 93 92 94 95"
    $"00 96 98 97 99 9b 9a d6 bf 9d 9c 9e 9f 00 00 D8"
};

resource 'pMAP' (129, "DEC line drawing -> Mac OS VT100") {
    $"d7 bd 09 0c 0d 0a a1 b1 00 0b d2 d3 d4 d5 da e2"
    $"e3 e4 f5 f6 f7 f8 f9 fa fb b2 b3 b9 ad a3 e1"
};
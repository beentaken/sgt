/*
 * WinURL: a small System tray application which will, on request,
 * convert the Clipboard contents into a URL and launch it with the
 * Windows default browser.
 * 
 * Copyright 2002 Simon Tatham. All rights reserved.
 * 
 * You may copy and use this file under the terms of the MIT
 * Licence. For details, see the file LICENCE provided in the
 * WinURL distribution archive. At the time of writing, a copy of
 * the licence is also available at
 * 
 *   http://www.opensource.org/licenses/mit-license.html
 * 
 * but this cannot be guaranteed not to have changed in the future.
 */

#include <windows.h>

#include <stdlib.h>
#include <ctype.h>

#define IDI_MAINICON 200

#define WM_XUSER     (WM_USER + 0x2000)
#define WM_SYSTRAY   (WM_XUSER + 6)
#define WM_SYSTRAY2  (WM_XUSER + 7)

HINSTANCE winurl_instance;
HWND winurl_hwnd;
char const *winurl_appname = "WinURL";

#define IDM_CLOSE    0x0010
#define IDM_ABOUT    0x0020
#define IDM_LAUNCH   0x0030

#define IDHOT_LAUNCH 0x0010

static HMENU systray_menu;

static int CALLBACK AboutProc(HWND hwnd, UINT msg,
			      WPARAM wParam, LPARAM lParam);
static int CALLBACK LicenceProc(HWND hwnd, UINT msg,
				WPARAM wParam, LPARAM lParam);
HWND aboutbox = NULL;

extern int systray_init(void) {
    BOOL res;
    NOTIFYICONDATA tnid;
    HICON hicon;

#ifdef NIM_SETVERSION
    tnid.uVersion = 0;
    res = Shell_NotifyIcon(NIM_SETVERSION, &tnid);
#endif

    tnid.cbSize = sizeof(NOTIFYICONDATA); 
    tnid.hWnd = winurl_hwnd; 
    tnid.uID = 1;                      /* unique within this systray use */
    tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; 
    tnid.uCallbackMessage = WM_SYSTRAY;
    tnid.hIcon = hicon = LoadIcon (winurl_instance, MAKEINTRESOURCE(201));
    strcpy(tnid.szTip, "WinURL (URL launcher)");

    res = Shell_NotifyIcon(NIM_ADD, &tnid); 

    if (hicon) 
        DestroyIcon(hicon); 

    systray_menu = CreatePopupMenu();
    AppendMenu (systray_menu, MF_ENABLED, IDM_LAUNCH, "Launch URL");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    AppendMenu (systray_menu, MF_ENABLED, IDM_ABOUT, "About");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    AppendMenu (systray_menu, MF_ENABLED, IDM_CLOSE, "Close WinURL");

    return res; 
}

extern void systray_shutdown(void) {
    BOOL res; 
    NOTIFYICONDATA tnid; 
 
    tnid.cbSize = sizeof(NOTIFYICONDATA); 
    tnid.hWnd = winurl_hwnd; 
    tnid.uID = 1;

    res = Shell_NotifyIcon(NIM_DELETE, &tnid); 

    DestroyMenu(systray_menu);
}

int hotkey_init(void)
{
    return RegisterHotKey(winurl_hwnd, IDHOT_LAUNCH, MOD_WIN, 'W');
}

void hotkey_shutdown(void)
{
    UnregisterHotKey(winurl_hwnd, IDHOT_LAUNCH);
}

void do_launch_url(void)
{
    HGLOBAL clipdata;
    char *s = NULL, *t = NULL, *p, *q;
    int len, ret, i;

    if (!OpenClipboard(NULL)) {
	goto error; /* unable to read clipboard */
    }
    clipdata = GetClipboardData(CF_TEXT);
    CloseClipboard();
    if (!clipdata) {
	goto error; /* clipboard contains no text */
    }
    s = GlobalLock(clipdata);
    if (!s) {
        goto error; /* unable to lock clipboard memory */
    }

    /*
     * Now strip each \n and any following spaces from the URL
     * text. In a future version this might be made configurable.
     */
    len = strlen(s);
    t = malloc(len+8);                 /* leading "http://" plus trailing \0 */
    if (!t) {
	GlobalUnlock(s);
	goto error;		       /* out of memory */
    }

    p = s;
    while (p[0] && (isspace((unsigned char)(p[0])) || p[0] == '\xA0'))
	p++;
    q = t + 7;

    for (; *p; p++) {
	if (*p != '\n')
	    *q++ = *p;
	else {
	    while (p[1] && (isspace((unsigned char)(p[1])) || p[1] == '\xA0'))
		p++;
	}
    }

    *q = '\0';

    /*
     * Now we have the URL starting at t+7, with space to add a
     * protocol prefix in case it's required. See if it is
     * required.
     */
    i = strspn(t+7, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    if (t[7+i] == ':') {
        p = t+7;                       /* we have our own protocol prefix */
    } else if (t[7+i] == '@') {
	/*
	 * This URL doesn't contain a protocol, and its first
	 * punctuation character is an @. That makes it likely to
	 * be an email address, so we'll prefix `mailto:'.
	 */
	strncpy(t, "mailto:", 7);
	p = t;
    } else {
	/*
	 * This URL doesn't contain a protocol, and doesn't look
	 * like an email address, so we'll prefix `http://' on the
	 * assumption that it's a truncated URL of the form
	 * `www.thingy.com/wossname' or just `www.thingy.com'.
	 */
	strncpy(t, "http://", 7);
        p = t;                         /* add the standard one */
    }

    ret = (int)ShellExecute(winurl_hwnd, "open", p, NULL, NULL, SW_SHOWNORMAL);

    free(t);

    GlobalUnlock(s);

    error:
    /*
     * Not entirely sure what we should do in case of error here.
     * Perhaps a simple beep might be suitable. Then again, `out of
     * memory' is a bit scarier. FIXME: should probably do
     * different error handling depending on context.
     */
    ;
}

static int CALLBACK AboutProc(HWND hwnd, UINT msg,
			      WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
	/*
	 * May wish to SetDlgItemText() items 100, 101 and 102 to
	 * some version-number text.
	 */
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    aboutbox = NULL;
	    DestroyWindow(hwnd);
	    return 0;
	  case 112:
	    EnableWindow(hwnd, 0);
	    DialogBox(winurl_instance, MAKEINTRESOURCE(301),
		      NULL, LicenceProc);
	    EnableWindow(hwnd, 1);
	    SetActiveWindow(hwnd);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	aboutbox = NULL;
	DestroyWindow(hwnd);
	return 0;
    }
    return 0;
}

/*
 * Dialog-box function for the Licence box.
 */
static int CALLBACK LicenceProc(HWND hwnd, UINT msg,
				WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    EndDialog(hwnd, 1);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

static LRESULT CALLBACK WndProc (HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    int ret;
    POINT cursorpos;                   /* cursor position */
    static int menuinprogress;

    switch (message) {
      case WM_SYSTRAY:
	if (lParam == WM_RBUTTONUP) {
	    GetCursorPos(&cursorpos);
	    PostMessage(hwnd, WM_SYSTRAY2, cursorpos.x, cursorpos.y);
	} else if (lParam == WM_LBUTTONUP) {
	    do_launch_url();
	}
	break;
      case WM_SYSTRAY2:
        if (!menuinprogress) {
            menuinprogress = 1;
            SetForegroundWindow(hwnd);
            ret = TrackPopupMenu(systray_menu,
                                 TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                                 TPM_RIGHTBUTTON,
                                 wParam, lParam, 0, winurl_hwnd, NULL);
            menuinprogress = 0;
        }
	break;
      case WM_HOTKEY:
	if (wParam == IDHOT_LAUNCH)
	    do_launch_url();
	return 0;
      case WM_COMMAND:
	if ((wParam & ~0xF) == IDM_CLOSE) {
	    SendMessage(hwnd, WM_CLOSE, 0, 0);
	} else if ((wParam & ~0xF) == IDM_LAUNCH) {
	    do_launch_url();
	} else if ((wParam & ~0xF) == IDM_ABOUT) {
	    if (!aboutbox) {
		aboutbox = CreateDialog(winurl_instance,
					MAKEINTRESOURCE(300),
					NULL, AboutProc);
		ShowWindow(aboutbox, SW_SHOWNORMAL);
		/*
		 * Sometimes the window comes up minimised / hidden
		 * for no obvious reason. Prevent this.
		 */
		SetForegroundWindow(aboutbox);
		SetWindowPos(aboutbox, HWND_TOP, 0, 0, 0, 0,
			     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	    }
	}
	break;
      case WM_DESTROY:
	PostQuitMessage (0);
	return 0;
    }

    return DefWindowProc (hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    WNDCLASS wndclass;
    MSG msg;

    winurl_instance = inst;

    if (!prev) {
	wndclass.style         = 0;
	wndclass.lpfnWndProc   = WndProc;
	wndclass.cbClsExtra    = 0;
	wndclass.cbWndExtra    = 0;
	wndclass.hInstance     = inst;
	wndclass.hIcon         = LoadIcon (inst,
					   MAKEINTRESOURCE(IDI_MAINICON));
	wndclass.hCursor       = LoadCursor (NULL, IDC_IBEAM);
	wndclass.hbrBackground = GetStockObject (BLACK_BRUSH);
	wndclass.lpszMenuName  = NULL;
	wndclass.lpszClassName = winurl_appname;

	RegisterClass (&wndclass);
    }

    winurl_hwnd = NULL;

    winurl_hwnd = CreateWindow (winurl_appname, winurl_appname,
				WS_OVERLAPPEDWINDOW | WS_VSCROLL,
				CW_USEDEFAULT, CW_USEDEFAULT,
				100, 100, NULL, NULL, inst, NULL);

    if (systray_init()) {

	ShowWindow (winurl_hwnd, SW_HIDE);

	if (!hotkey_init()) {
	    MessageBox(winurl_hwnd, "Unable to register hot key",
		       "WinURL Warning", MB_ICONWARNING | MB_OK);
	}

	while (GetMessage (&msg, NULL, 0, 0) == 1) {
	    TranslateMessage (&msg);
	    DispatchMessage (&msg);
	}
	
	hotkey_shutdown();
	systray_shutdown();
    }

    return msg.wParam;
}

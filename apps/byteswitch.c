#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_WINS 64
#define ITEM_H   30
#define WIN_W    500
#define BORDER_W 2

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int sw, sh, win_h;
static unsigned long c_bg, c_fg, c_hi, c_border, c_dim;

static Window windows[MAX_WINS];
static char *labels[MAX_WINS];
static int count, sel;

static unsigned long getcol(const char *s) {
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	XParseColor(dpy, cmap, s, &xc);
	XAllocColor(dpy, cmap, &xc);
	return xc.pixel;
}

static void draw(void) {
	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, WIN_W, win_h);

	const char *hdr = "s w i t c h   w i n d o w";
	int tw = XTextWidth(xfont, hdr, strlen(hdr));
	XSetForeground(dpy, gc, c_dim);
	XDrawString(dpy, win, gc, (WIN_W - tw) / 2, 28, hdr, strlen(hdr));

	XSetForeground(dpy, gc, c_border);
	XFillRectangle(dpy, win, gc, 20, 42, WIN_W - 40, 2);
	XFillRectangle(dpy, win, gc, 0, 0, WIN_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, win_h - BORDER_W, WIN_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, 0, BORDER_W, win_h);
	XFillRectangle(dpy, win, gc, WIN_W - BORDER_W, 0, BORDER_W, win_h);

	int y = 56;
	for (int i = 0; i < count; i++) {
		if (i == sel) {
			XSetForeground(dpy, gc, c_hi);
			XDrawString(dpy, win, gc, 16, y + 20, ">", 1);
		}
		XSetForeground(dpy, gc, i == sel ? c_hi : c_fg);
		XDrawString(dpy, win, gc, 32, y + 20, labels[i], strlen(labels[i]));
		y += ITEM_H;
	}

	const char *ftr = "j/k  enter  esc";
	tw = XTextWidth(xfont, ftr, strlen(ftr));
	XSetForeground(dpy, gc, c_dim);
	XDrawString(dpy, win, gc, (WIN_W - tw)/2, win_h - 14, ftr, strlen(ftr));

	XSync(dpy, False);
}

static char *get_window_title(Window w) {
	Atom netname = XInternAtom(dpy, "_NET_WM_NAME", False);
	Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	Atom type; int fmt;
	unsigned long n, extra;
	unsigned char *prop = NULL;
	char *title = strdup("(untitled)");

	if (XGetWindowProperty(dpy, w, netname, 0, 256, False, utf8,
	    &type, &fmt, &n, &extra, &prop) == Success && prop) {
		free(title);
		title = strdup((char *)prop);
		XFree(prop);
	}
	if (strlen(title) > 60) title[60] = '\0';
	return title;
}

static int is_ignored(Window w) {
	XWindowAttributes wa;
	if (!XGetWindowAttributes(dpy, w, &wa)) return 1;
	if (wa.override_redirect) return 1;
	return 0;
}

int main(void) {
	dpy = XOpenDisplay(NULL);
	if (!dpy) return 1;
	int scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	sw = DisplayWidth(dpy, scr);
	sh = DisplayHeight(dpy, scr);

	xfont = XLoadQueryFont(dpy, "fixed");
	if (!xfont) return 1;

	c_bg     = getcol("#282828");
	c_fg     = getcol("#ebdbb2");
	c_hi     = getcol("#d65d0e");
	c_border = getcol("#689d6a");
	c_dim    = getcol("#a89984");

	/* get client list */
	Atom netcl = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	Atom type; int fmt;
	unsigned long n, extra;
	unsigned char *data = NULL;
	if (XGetWindowProperty(dpy, root, netcl, 0, MAX_WINS, False,
	    XA_WINDOW, &type, &fmt, &n, &extra, &data) == Success && data) {
		Window *wins = (Window *)data;
		for (unsigned long i = 0; i < n && count < MAX_WINS; i++) {
			if (is_ignored(wins[i])) continue;
			windows[count] = wins[i];
			labels[count] = get_window_title(wins[i]);
			count++;
		}
		XFree(data);
	}

	if (!count) { XCloseDisplay(dpy); return 0; }

	win_h = 56 + count * ITEM_H + 24;
	if (win_h < 200) win_h = 200;
	if (win_h > sh - 40) win_h = sh - 40;

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = c_bg,
		.event_mask = ExposureMask | KeyPressMask
	};
	win = XCreateWindow(dpy, root,
		(sw - WIN_W) / 2, (sh - win_h) / 2,
		WIN_W, win_h, 0,
		DefaultDepth(dpy, scr), CopyFromParent,
		DefaultVisual(dpy, scr),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	gc = XCreateGC(dpy, root, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);

	XMapRaised(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	XFlush(dpy);

	XEvent ev;
	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			draw();
		else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Escape || ks == XK_q) break;
			if (ks == XK_Return) {
				XEvent cev = {0};
				cev.xclient.type = ClientMessage;
				cev.xclient.window = windows[sel];
				cev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
				cev.xclient.format = 32;
				cev.xclient.data.l[0] = 2;
				cev.xclient.data.l[1] = CurrentTime;
				XSendEvent(dpy, root, False, SubstructureRedirectMask, &cev);
				break;
			}
			if ((ks == XK_j || ks == XK_Down) && sel < count - 1)
				{ sel++; draw(); }
			if ((ks == XK_k || ks == XK_Up) && sel > 0)
				{ sel--; draw(); }
		}
	}

	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	for (int i = 0; i < count; i++) free(labels[i]);
	return 0;
}

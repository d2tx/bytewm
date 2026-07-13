/* bytevol - minimal volume OSD */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int bh, sw, sh;

static const char *font = "fixed";
static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *barcolor = "#689d6a";

static unsigned long fgcol, bgcol, barcol;

static unsigned long
getcol(const char *c)
{
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	if (!XParseColor(dpy, cmap, c, &xc)) return 0;
	if (!XAllocColor(dpy, cmap, &xc)) return 0;
	return xc.pixel;
}

static void
show_volume(int vol)
{
	char label[64];
	snprintf(label, sizeof(label), "VOL %d%%", vol);

	int tw = XTextWidth(xfont, label, strlen(label)) + 16;
	int bw = 100;
	int w = tw + bw + 24;
	int x = sw - w - 8;
	int y = 24;

	XMoveResizeWindow(dpy, win, x, y, w, bh);
	XMapRaised(dpy, win);

	XSetForeground(dpy, gc, bgcol);
	XFillRectangle(dpy, win, gc, 0, 0, w, bh);
	XSetForeground(dpy, gc, fgcol);
	int tx = 8;
	int ty = (bh - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;
	XDrawString(dpy, win, gc, tx, ty, label, strlen(label));

	int barx = tw + 8;
	int bary = bh / 3;
	int barw = bw;
	int barh = bh / 3;
	XSetForeground(dpy, gc, bgcol);
	XFillRectangle(dpy, win, gc, barx, bary, barw, barh);
	XSetForeground(dpy, gc, barcol);
	XFillRectangle(dpy, win, gc, barx, bary, barw * vol / 100, barh);

	XSync(dpy, 0);
}

int
main(int argc, char *argv[])
{
	int vol = 50;
	if (argc > 1) vol = atoi(argv[1]);
	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;

	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	int screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	xfont = XLoadQueryFont(dpy, font);
	if (!xfont) xfont = XLoadQueryFont(dpy, "fixed");
	if (!xfont) return 1;
	bh = xfont->ascent + xfont->descent + 8;

	gc = XCreateGC(dpy, root, 0, NULL);

	bgcol = getcol(bg);
	fgcol = getcol(fg);
	barcol = getcol(barcolor);

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = bgcol,
		.event_mask = ExposureMask
	};
	win = XCreateWindow(dpy, root, 0, 0, 1, bh, 1,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XStoreName(dpy, win, "bytevol");

	show_volume(vol);
	{
		XEvent ev;
		int xfd = ConnectionNumber(dpy);
		fd_set fds;
		struct timeval tv = { 2, 0 };
		while (1) {
			FD_ZERO(&fds);
			FD_SET(xfd, &fds);
			if (select(xfd + 1, &fds, NULL, NULL, &tv) <= 0)
				break;
			while (XPending(dpy)) {
				XNextEvent(dpy, &ev);
				if (ev.type == Expose && ev.xexpose.count == 0)
					show_volume(vol);
			}
		}
	}

	XUnmapWindow(dpy, win);
	XDestroyWindow(dpy, win);
	XFreeFont(dpy, xfont);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}

/* bytify - minimal notification daemon */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int bh, sw;
static int running = 1;
static char msg[512] = "";
static int msg_time = 0;

static const char *font = "fixed";
static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *border = "#689d6a";

static unsigned long
getcol(const char *c)
{
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	if (!XParseColor(dpy, cmap, c, &xc)) return 0;
	if (!XAllocColor(dpy, cmap, &xc)) return 0;
	return xc.pixel;
}

static unsigned long cbg, cfg, cborder;

static void
draw(void)
{
	if (!msg[0]) {
		XUnmapWindow(dpy, win);
		return;
	}
	int tw = XTextWidth(xfont, msg, strlen(msg)) + 16;
	int x = sw - tw - 8;
	int y = 24;
	XMoveResizeWindow(dpy, win, x, y, tw, bh);
	XMapRaised(dpy, win);

	XSetForeground(dpy, gc, cborder);
	XDrawRectangle(dpy, win, gc, 0, 0, tw - 1, bh - 1);
	XSetForeground(dpy, gc, cbg);
	XFillRectangle(dpy, win, gc, 1, 1, tw - 2, bh - 2);
	XSetForeground(dpy, gc, cfg);
	int tx = 8;
	int ty = (bh - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;
	XDrawString(dpy, win, gc, tx, ty, msg, strlen(msg));
	XSync(dpy, 0);
}

static void
notify(const char *text)
{
	strncpy(msg, text, sizeof(msg) - 1);
	msg[sizeof(msg) - 1] = '\0';
	msg_time = 5;
	draw();
}

int
main(void)
{
	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	int screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = DisplayWidth(dpy, screen);

	xfont = XLoadQueryFont(dpy, font);
	if (!xfont) xfont = XLoadQueryFont(dpy, "fixed");
	if (!xfont) return 1;
	bh = xfont->ascent + xfont->descent + 8;

	gc = XCreateGC(dpy, root, 0, NULL);

	cbg = getcol(bg);
	cfg = getcol(fg);
	cborder = getcol(border);

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = cbg,
		.event_mask = ExposureMask
	};
	win = XCreateWindow(dpy, root, 0, 0, 1, bh, 1,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XStoreName(dpy, win, "bytify");
	XSelectInput(dpy, win, ExposureMask);

		/* read from stdin with 1s timeout for auto-hide */
	char buf[512];
	fd_set fds;
	int fd = fileno(stdin);
	int xfd = ConnectionNumber(dpy);
	int flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	while (running) {
		while (fgets(buf, sizeof(buf), stdin)) {
			buf[strcspn(buf, "\n")] = 0;
			notify(buf);
		}
		if (feof(stdin))
			break;
		if (msg_time > 0) {
			msg_time--;
			if (msg_time == 0) {
				msg[0] = '\0';
				draw();
			}
		}
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		FD_SET(xfd, &fds);
		int nfds = fd > xfd ? fd : xfd;
		struct timeval tv = { 1, 0 };
		if (select(nfds + 1, &fds, NULL, NULL, &tv) < 0)
			break;
		if (FD_ISSET(xfd, &fds)) {
			while (XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);
				if (ev.type == Expose && ev.xexpose.count == 0)
					draw();
			}
		}
	}

	XDestroyWindow(dpy, win);
	XFreeFont(dpy, xfont);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}

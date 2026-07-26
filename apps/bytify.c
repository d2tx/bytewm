/* bytify - minimal notification daemon */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <time.h>

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int bh, sw;
static char msg[512] = "";
static time_t notify_time = 0;

static const char *font = "fixed";
static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *border = "#689d6a";
static const char *fifo_path = "/tmp/bytify.fifo";

#define POPUP_W  280
#define POPUP_MARGIN  12
#define HIDE_AFTER  3

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
		XSync(dpy, False);
		return;
	}
	int tw = XTextWidth(xfont, msg, strlen(msg));
	int x = sw - POPUP_W - POPUP_MARGIN;
	int y = 24;

	XMoveResizeWindow(dpy, win, x, y, POPUP_W, bh);
	XMapRaised(dpy, win);

	XSetForeground(dpy, gc, cborder);
	XDrawRectangle(dpy, win, gc, 0, 0, POPUP_W - 1, bh - 1);
	XSetForeground(dpy, gc, cbg);
	XFillRectangle(dpy, win, gc, 1, 1, POPUP_W - 2, bh - 2);
	XSetForeground(dpy, gc, cfg);
	int tx = (POPUP_W - tw) / 2;
	int ty = (bh - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;
	XDrawString(dpy, win, gc, tx, ty, msg, strlen(msg));
	XSync(dpy, 0);
}

static void
notify(const char *text)
{
	strncpy(msg, text, sizeof(msg) - 1);
	msg[sizeof(msg) - 1] = '\0';
	notify_time = time(NULL);
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
	win = XCreateWindow(dpy, root, 0, 0, POPUP_W, bh, 1,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XStoreName(dpy, win, "bytify");

	char buf[512];
	fd_set fds;
	int xfd = ConnectionNumber(dpy);
	int fd = open(fifo_path, O_RDWR);
	if (fd < 0)
		return 1;

	while (1) {
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		if (!msg[0]) FD_SET(fd, &fds);
		int nfds = (fd > xfd ? fd : xfd);
		struct timeval tv = { 1, 0 };
		int n = select(nfds + 1, &fds, NULL, NULL, &tv);
		if (n < 0)
			break;

		if (FD_ISSET(xfd, &fds)) {
			while (XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);
				if (ev.type == Expose && ev.xexpose.count == 0)
					draw();
			}
		}

		if (FD_ISSET(fd, &fds)) {
			ssize_t nr = read(fd, buf, sizeof(buf) - 1);
			if (nr > 0) {
				buf[nr] = '\0';
				char *nl = strchr(buf, '\n');
				if (nl) *nl = '\0';
				if (buf[0]) notify(buf);
			}
		}

		if (msg[0] && time(NULL) - notify_time >= HIDE_AFTER) {
			msg[0] = '\0';
			draw();
		}
	}

	close(fd);
	XDestroyWindow(dpy, win);
	XFreeFont(dpy, xfont);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}

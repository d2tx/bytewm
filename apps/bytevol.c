/* bytevol - volume OSD daemon */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int bh, sw, sh;
static int vol = -1;
static int muted = 0;
static time_t shown_time = 0;

static const char *font = "fixed";
static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *barcolor = "#689d6a";
static const char *bordercolor = "#689d6a";
static const char *fifo_path = "/tmp/bytevol.fifo";

#define HIDE_AFTER  2

static unsigned long fgcol, bgcol, barcol, bordercol;

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
draw(void)
{
	if (vol < 0) {
		XUnmapWindow(dpy, win);
		XSync(dpy, 0);
		return;
	}

	char label[64];
	if (muted)
		snprintf(label, sizeof(label), "VOL MUTE");
	else
		snprintf(label, sizeof(label), "VOL %d%%", vol);

	int tw = XTextWidth(xfont, label, strlen(label)) + 16;
	int bw = 100;
	int w = tw + bw + 24;
	int x = (sw - w) / 2;
	int y = (sh - bh) / 2;

	XMoveResizeWindow(dpy, win, x, y, w, bh);
	XMapRaised(dpy, win);

	XSetForeground(dpy, gc, bordercol);
	XDrawRectangle(dpy, win, gc, 0, 0, w - 1, bh - 1);
	XSetForeground(dpy, gc, bgcol);
	XFillRectangle(dpy, win, gc, 1, 1, w - 2, bh - 2);
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
	XFillRectangle(dpy, win, gc, barx, bary,
		muted ? 0 : barw * vol / 100, barh);

	XSync(dpy, 0);
}

static void
get_volume(void)
{
	FILE *fp = popen("amixer get Master 2>/dev/null", "r");
	if (!fp) return;
	char line[256];
	int v = -1;
	int m = 0;
	while (fgets(line, sizeof(line), fp)) {
		char *p = strstr(line, "[");
		if (!p) continue;
		char *q = strchr(p, '%');
		if (!q) continue;
		if (q - p > 4) continue;
		char buf[8];
		int n = (int)(q - p - 1);
		if (n < 1 || n > 3) continue;
		memcpy(buf, p + 1, (size_t)n);
		buf[n] = '\0';
		v = atoi(buf);
		if (strstr(line, "[off]")) m = 1;
		break;
	}
	pclose(fp);
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	vol = v;
	muted = m;
}

static void
show_volume(void)
{
	get_volume();
	shown_time = time(NULL);
	draw();
}

int
main(int argc, char *argv[])
{
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
	bordercol = getcol(bordercolor);

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

	/* legacy: one-shot display */
	if (argc > 1) {
		vol = atoi(argv[1]);
		if (vol < 0) vol = 0;
		if (vol > 100) vol = 100;
		shown_time = time(NULL);
		draw();
		XEvent ev;
		while (1) {
			struct timeval tv = { 0, 500000 };
			fd_set fds;
			int xfd = ConnectionNumber(dpy);
			FD_ZERO(&fds);
			FD_SET(xfd, &fds);
			if (select(xfd + 1, &fds, NULL, NULL, &tv) <= 0)
				break;
			while (XPending(dpy)) {
				XNextEvent(dpy, &ev);
				if (ev.type == Expose && ev.xexpose.count == 0)
					draw();
			}
		}
		XUnmapWindow(dpy, win);
		XDestroyWindow(dpy, win);
		XFreeFont(dpy, xfont);
		XFreeGC(dpy, gc);
		XCloseDisplay(dpy);
		return 0;
	}

	/* daemon mode */
	char buf[512];
	fd_set fds;
	int xfd = ConnectionNumber(dpy);
	int fd = open(fifo_path, O_RDWR);
	if (fd < 0) {
		XDestroyWindow(dpy, win);
		XFreeFont(dpy, xfont);
		XFreeGC(dpy, gc);
		XCloseDisplay(dpy);
		return 1;
	}

	while (1) {
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		FD_SET(fd, &fds);
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
			if (nr > 0)
				show_volume();
		}

		if (vol >= 0 && time(NULL) - shown_time >= HIDE_AFTER) {
			vol = -1;
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

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
#include <errno.h>
#include <time.h>
#include "appfont.h"

static Display *dpy;
static Window root, win;
static GC gc;
static AppFont *afont;
static int bh, sw;
static char msg[512] = "";
static time_t notify_time = 0;

static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *border = "#689d6a";
static const char *fifo_path = "/tmp/bytify.fifo";

#define POPUP_W  300
#define POPUP_MARGIN  12
#define HIDE_AFTER  3
#define MAXLINES  5
#define MY_LINE_MAX 256
#define TXT_PAD  8

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
static XftColor fc_fg;

static void
draw(void)
{
	if (!msg[0]) {
		XUnmapWindow(dpy, win);
		XSync(dpy, False);
		return;
	}

	/* wrap message into at most MAXLINES centered lines */
	char lines[MAXLINES][MY_LINE_MAX];
	int nlines = 0;
	{
		int maxpx = POPUP_W - 2 * TXT_PAD - 2;
		char line[MY_LINE_MAX];
		size_t linelen = 0;
		const char *p = msg;
		int more = 0;
		while (*p && nlines < MAXLINES) {
			while (*p == ' ') p++;
			if (!*p) break;
			const char *start = p;
			while (*p && *p != ' ') p++;
			int wlen = (int)(p - start);
			/* clamp word to remaining room in line (byte bound) */
			if (wlen > (int)(MY_LINE_MAX - 1) - (int)linelen - (linelen ? 1 : 0))
				wlen = (int)(MY_LINE_MAX - 1) - (int)linelen - (linelen ? 1 : 0);
			if (wlen < 0) wlen = 0;
			int ww = appfont_width(afont, start, wlen);
			int sepw = linelen ? appfont_width(afont, " ", 1) : 0;
			if (linelen && (int)linelen + sepw + ww > maxpx) {
				line[linelen] = '\0';
				strncpy(lines[nlines], line, MY_LINE_MAX - 1);
				lines[nlines][MY_LINE_MAX - 1] = '\0';
				nlines++;
				linelen = 0;
				if (nlines == MAXLINES) {
					while (*p == ' ') p++;
					more = *p != '\0';
					break;
				}
			}
			if (linelen) line[linelen++] = ' ';
			memcpy(line + linelen, start, (size_t)wlen);
			linelen += (size_t)wlen;
		}
		if (nlines < MAXLINES && linelen) {
			line[linelen] = '\0';
			strncpy(lines[nlines], line, MY_LINE_MAX - 1);
			lines[nlines][MY_LINE_MAX - 1] = '\0';
			nlines++;
		}
		if (nlines == MAXLINES && more) {
			/* trim last line to fit "..." */
			char *l = lines[nlines - 1];
			int ellw = appfont_width(afont, "...", 3);
			int lw = appfont_width(afont, l, (int)strlen(l));
			while (lw + ellw > maxpx && strlen(l) > 0) {
				l[strlen(l) - 1] = '\0';
				lw = appfont_width(afont, l, (int)strlen(l));
			}
			if (strlen(l) > (size_t)(MY_LINE_MAX - 4))
				l[MY_LINE_MAX - 4] = '\0';
			strcat(l, "...");
		}
	}

	int x = sw - POPUP_W - POPUP_MARGIN;
	int y = 36;

	XMoveResizeWindow(dpy, win, x, y, POPUP_W, bh);
	XMapRaised(dpy, win);

	XSetForeground(dpy, gc, cborder);
	XDrawRectangle(dpy, win, gc, 0, 0, POPUP_W - 1, bh - 1);
	XSetForeground(dpy, gc, cbg);
	XFillRectangle(dpy, win, gc, 1, 1, POPUP_W - 2, bh - 2);
	XSetForeground(dpy, gc, cfg);

	int lineh = afont->height;
	int ty = (bh - nlines * lineh) / 2 + afont->ascent;
	for (int i = 0; i < nlines; i++) {
		int tw = appfont_width(afont, lines[i], (int)strlen(lines[i]));
		int tx = (POPUP_W - tw) / 2;
		appfont_draw(afont, win, gc, &fc_fg, cfg, tx, ty + i * lineh, lines[i], (int)strlen(lines[i]));
	}
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

	afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) return 1;
	bh = afont->height * MAXLINES + 12;

	gc = XCreateGC(dpy, root, 0, NULL);

	cbg = getcol(bg);
	cfg = getcol(fg);
	cborder = getcol(border);
	appfont_alloccolor(dpy, fg, &fc_fg);

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

	char buf[1024];
	size_t buflen = 0;
	fd_set fds;
	int xfd = ConnectionNumber(dpy);
	int fd = open(fifo_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return 1;

	while (1) {
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		if (!msg[0]) FD_SET(fd, &fds);
		int nfds = (fd > xfd ? fd : xfd);
		struct timeval tv = { 1, 0 };
		int n = select(nfds + 1, &fds, NULL, NULL, &tv);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}

		if (FD_ISSET(xfd, &fds)) {
			while (XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);
				if (ev.type == Expose && ev.xexpose.count == 0)
					draw();
			}
		}

		if (FD_ISSET(fd, &fds)) {
			for (;;) {
				ssize_t nr = read(fd, buf + buflen, sizeof(buf) - 1 - buflen);
				if (nr <= 0) break;
				buflen += (size_t)nr;
				if (buflen >= sizeof(buf) - 1) {
					/* drop oversized message */
					buflen = 0;
					continue;
				}
				for (;;) {
					char *nl = memchr(buf, '\n', buflen);
					if (!nl) break;
					*nl = '\0';
					if (buf[0]) notify(buf);
					buflen -= (size_t)(nl + 1 - buf);
					memmove(buf, nl + 1, buflen);
				}
			}
		}

		if (msg[0] && time(NULL) - notify_time >= HIDE_AFTER) {
			msg[0] = '\0';
			draw();
		}
	}

	close(fd);
	XDestroyWindow(dpy, win);
	appfont_close(afont);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}

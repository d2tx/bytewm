/* bytevol - volume OSD daemon with perceptual loudness curve */
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
#include <errno.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include "appfont.h"

static Display *dpy;
static Window root, win;
static GC gc;
static AppFont *afont;
static int bh, sw, sh;
static int level = -1;   /* displayed volume 0-100, -1 = hidden */
static int cur = 50;     /* ground-truth level 0-100 from amixer */
static int muted = 0;
static time_t shown_time = 0;

static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *barcolor = "#689d6a";
static const char *bordercolor = "#689d6a";
static const char *fifo_path = "/tmp/bytevol.fifo";
static const char *level_file = "/tmp/bytevol_level";

#define HIDE_AFTER  2
#define STEP        10

/* logarithmic loudness curve: 100%->0dB, 90%->-2dB, 50%->-13.5dB, 20%->-31.5dB */
#define CURVE_MAXDB 90.0

static unsigned long fgcol, bgcol, barcol, bordercol;
static XftColor fc_fg;

static unsigned long
getcol(const char *c)
{
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	if (!XParseColor(dpy, cmap, c, &xc)) return 0;
	if (!XAllocColor(dpy, cmap, &xc)) return 0;
	return xc.pixel;
}

static double
db_from_level(int lvl)
{
	if (lvl <= 0) return -CURVE_MAXDB;
	if (lvl >= 100) return 0.0;
	return CURVE_MAXDB * log10(lvl / 100.0) / 2.0;
}

static int
level_from_db(double db)
{
	if (db >= 0) return 100;
	if (db <= -CURVE_MAXDB) return 0;
	return (int)lround(100.0 * pow(10.0, db * 2.0 / CURVE_MAXDB));
}

static void
run_cmd(const char *fmt, double a)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), fmt, a);
	FILE *fp = popen(cmd, "w");
	if (fp) pclose(fp);
}

/* read current volume/dB/mute from amixer */
static void
get_volume(void)
{
	FILE *fp = popen("amixer get Master 2>/dev/null", "r");
	if (!fp) return;
	char line[256];
	int v = -1;
	int m = 0;
	int have_db = 0;
	double db = 0.0;
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
		char *dbp = strstr(line, "dB]");
		if (dbp && dbp - line >= 2) {
			/* [X.XXdB] */
			char *sp = dbp - 2;
			while (sp > line && *sp != '[') sp--;
			if (*sp == '[') { db = atof(sp + 1); have_db = 1; }
		}
		break;
	}
	pclose(fp);
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	/* prefer dB->level; fall back to raw % if dB unavailable */
	if (have_db)
		cur = level_from_db(db);
	else
		cur = v;
	if (level < 0) level = cur;
	muted = m;
}

static void
write_level_file(void)
{
	int fd = open(level_file,
		O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd >= 0) {
		char buf[16];
		int n;
		if (muted)
			n = snprintf(buf, sizeof(buf), "MUTE\n");
		else
			n = snprintf(buf, sizeof(buf), "%d\n", level);
		if (n > 0 && (size_t)n < sizeof(buf)) {
			ssize_t r = write(fd, buf, (size_t)n);
			(void)r;
		}
		close(fd);
	}
}

static void
draw(void)
{
	if (level < 0) {
		XUnmapWindow(dpy, win);
		XSync(dpy, 0);
		return;
	}

	char label[64];
	if (muted)
		snprintf(label, sizeof(label), "VOL MUTE");
	else
		snprintf(label, sizeof(label), "VOL %d%%", level);

	int tw = appfont_width(afont, label, (int)strlen(label)) + 16;
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
	int ty = (bh - afont->height) / 2 + afont->ascent;
	appfont_draw(afont, win, gc, &fc_fg, fgcol, tx, ty, label, (int)strlen(label));

	int barx = tw + 8;
	int bary = bh / 3;
	int barw = bw;
	int barh = bh / 3;
	XSetForeground(dpy, gc, bgcol);
	XFillRectangle(dpy, win, gc, barx, bary, barw, barh);
	XSetForeground(dpy, gc, barcol);
	XFillRectangle(dpy, win, gc, barx, bary,
		muted ? 0 : barw * level / 100, barh);

	XSync(dpy, 0);
}

static void
show(void)
{
	get_volume();
	shown_time = time(NULL);
	draw();
}

/* apply target perceptual level via relative dB deltas */
static void
set_level(int newlevel)
{
	if (newlevel < 0) newlevel = 0;
	if (newlevel > 100) newlevel = 100;
	cur = newlevel;
	level = newlevel;

	/* publish intent to the bar immediately (real-time), then apply */
	muted = 0;
	write_level_file();

	double target = db_from_level(level);
	FILE *fp = popen("amixer get Master 2>/dev/null", "r");
	double curdb = 0.0;
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			char *dbp = strstr(line, "dB]");
			if (dbp && dbp - line >= 2) {
				char *sp = dbp - 2;
				while (sp > line && *sp != '[') sp--;
				if (*sp == '[') curdb = atof(sp + 1);
			}
		}
		pclose(fp);
	}

	double delta = target - curdb;
	run_cmd("amixer set Master unmute >/dev/null 2>&1", 0);
	if (delta > 0.5)
		run_cmd("amixer set Master %.1fdB+ >/dev/null 2>&1", delta);
	else if (delta < -0.5)
		run_cmd("amixer set Master %.1fdB- >/dev/null 2>&1", -delta);

	shown_time = time(NULL);
	draw();
}

static void
toggle_mute(void)
{
	run_cmd("amixer set Master toggle >/dev/null 2>&1", 0);
	get_volume();
	write_level_file();
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

	afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) return 1;
	bh = afont->height + 8;

	gc = XCreateGC(dpy, root, 0, NULL);

	bgcol = getcol(bg);
	fgcol = getcol(fg);
	barcol = getcol(barcolor);
	bordercol = getcol(bordercolor);
	appfont_alloccolor(dpy, fg, &fc_fg);

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
		level = atoi(argv[1]);
		if (level < 0) level = 0;
		if (level > 100) level = 100;
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
		appfont_close(afont);
		XFreeGC(dpy, gc);
		XCloseDisplay(dpy);
		return 0;
	}

	/* daemon mode */
	fd_set fds;
	int xfd = ConnectionNumber(dpy);
	int fd = open(fifo_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		XDestroyWindow(dpy, win);
		appfont_close(afont);
		XFreeGC(dpy, gc);
		XCloseDisplay(dpy);
		return 1;
	}
	{
		struct stat st;
		if (fstat(fd, &st) == 0 && !S_ISFIFO(st.st_mode)) {
			close(fd);
			XDestroyWindow(dpy, win);
			appfont_close(afont);
			XFreeGC(dpy, gc);
			XCloseDisplay(dpy);
			return 1;
		}
	}

	get_volume();
	write_level_file();

	while (1) {
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		FD_SET(fd, &fds);
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
			/* persistent line buffer: only \n-terminated tokens
			   are dispatched; carry leftovers across reads */
			static char cmdbuf[1024];
			static size_t cmdlen = 0;
			ssize_t nrr;
			while ((nrr = read(fd, cmdbuf + cmdlen,
				sizeof(cmdbuf) - 1 - cmdlen)) > 0) {
				cmdlen += (size_t)nrr;
				if (cmdlen >= sizeof(cmdbuf) - 1) {
					/* oversized unterminated line: drop it */
					cmdlen = 0;
					continue;
				}
				cmdbuf[cmdlen] = '\0';
				for (;;) {
					char *nl = memchr(cmdbuf, '\n', cmdlen);
					if (!nl) break;
					size_t toklen = (size_t)(nl - cmdbuf);
					if (toklen) {
						cmdbuf[toklen] = '\0';
						if (!strcmp(cmdbuf, "+"))
							set_level(cur + STEP);
						else if (!strcmp(cmdbuf, "-"))
							set_level(cur - STEP);
						else if (!strcmp(cmdbuf, "toggle") ||
						         !strcmp(cmdbuf, "t"))
							toggle_mute();
						else if (cmdbuf[0])
							show();
					}
					memmove(cmdbuf, nl + 1, cmdlen - toklen - 1);
					cmdlen -= toklen + 1;
				}
			}
		}

		if (level >= 0 && time(NULL) - shown_time >= HIDE_AFTER) {
			level = -1;
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

/* bytelaunch - dmenu-style X11 application launcher */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include "appfont.h"

#define MAX_ITEMS 16384
#define INPUT_MAX  255
#define PAPER_W    360
#define PAPER_H    420
#define BORDER_W   2

static Display *dpy;
static Window  win;
static Pixmap  buf;
static GC      gc;
static AppFont *afont;
static int    sw, sh, scr, win_h;
static int    itemh, per_page, page, page_max;

static char   input[INPUT_MAX + 1];
static int    ipos;

static char  *items[MAX_ITEMS];
static int    n_items;
static char  *matches[MAX_ITEMS];
static int    n_matches;
static int    selected;

static unsigned long bgcol, fgcol, c_hi, c_dim, bordercol, fieldcol;
static XftColor fc_fg, fc_hi, fc_dim;
static int cursor_on = 1;

static long
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static unsigned long
getcol(const char *s)
{
	XColor c;
	Colormap cm = DefaultColormap(dpy, scr);
	if (!XParseColor(dpy, cm, s, &c)) return 0;
	if (!XAllocColor(dpy, cm, &c))   return 0;
	return c.pixel;
}

static int
strpcmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static int
match(const char *hay, const char *ndl)
{
	if (!*ndl) return 1;
	while (*hay) {
		const char *h = hay, *n = ndl;
		while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n))
			h++, n++;
		if (!*n) return 1;
		hay++;
	}
	return 0;
}

static void
scan_path(void)
{
	const char *path = getenv("PATH");
	char *cpy, *dir, *save, full[1024];
	DIR  *d;
	struct dirent *ent;

	if (!path) path = "/usr/local/bin:/usr/bin:/bin";
	cpy = strdup(path);
	if (!cpy) return;
	for (dir = strtok_r(cpy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		d = opendir(dir);
		if (!d) continue;
		while ((ent = readdir(d))) {
			if (ent->d_name[0] == '.') continue;
			snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
			struct stat st;
			if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
			if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) continue;
			if (n_items >= MAX_ITEMS - 1) break;
			int dup = 0;
			for (int k = 0; k < n_items; k++)
				if (!strcmp(items[k], ent->d_name)) { dup = 1; break; }
			if (!dup) {
				char *itm = strdup(ent->d_name);
				if (itm)
					items[n_items++] = itm;
			}
		}
		closedir(d);
	}
	free(cpy);
	if (n_items) qsort(items, n_items, sizeof(char *), strpcmp);
}

static void
filter(void)
{
	n_matches = 0;
	for (int i = 0; i < n_items; i++) {
		if (n_matches >= MAX_ITEMS - 1) break;
		if (match(items[i], input))
			matches[n_matches++] = items[i];
	}
	if (selected >= n_matches && n_matches > 0)
		selected = n_matches - 1;
	if (!n_matches) selected = 0;
}

static int
pmax(void)
{
	return n_matches > per_page ? (n_matches - 1) / per_page : 0;
}

/* truncate label to maxw pixels, appending "..." if needed */
static void
truncate_label(const char *in, char *out, size_t outsz, int maxw)
{
	size_t len = strlen(in);
	if (len + 1 > outsz) len = outsz - 1;
	memcpy(out, in, len);
	out[len] = '\0';
	if (appfont_width(afont, out, (int)len) <= maxw)
		return;
	while (len > 3) {
		len--;
		memcpy(out + len - 3, "...", 3);
		out[len] = '\0';
		if (appfont_width(afont, out, (int)len) <= maxw)
			return;
	}
	out[0] = '.'; out[1] = '.'; out[2] = '.'; out[3] = '\0';
}

static void
draw(void)
{
	int hdr_y = itemh + 6;
	int rule_y = itemh + 18;
	int base_off = itemh - 8;
	int ftr_y = win_h - (itemh / 2 + 7);
	int input_y = rule_y + itemh;

	XSetForeground(dpy, gc, bgcol);
	XFillRectangle(dpy, buf, gc, 0, 0, PAPER_W, win_h);

	const char *hdr = "b y t e l a u n c h";
	int tw = appfont_width(afont, hdr, (int)strlen(hdr));
	appfont_draw(afont, buf, gc, &fc_dim, c_dim, (PAPER_W - tw) / 2, hdr_y, hdr, (int)strlen(hdr));

	XSetForeground(dpy, gc, bordercol);
	XFillRectangle(dpy, buf, gc, 20, rule_y, PAPER_W - 40, 2);
	XFillRectangle(dpy, buf, gc, 0, 0, PAPER_W, BORDER_W);
	XFillRectangle(dpy, buf, gc, 0, win_h - BORDER_W, PAPER_W, BORDER_W);
	XFillRectangle(dpy, buf, gc, 0, 0, BORDER_W, win_h);
	XFillRectangle(dpy, buf, gc, PAPER_W - BORDER_W, 0, BORDER_W, win_h);

	/* input field */
	int fw = PAPER_W - 80;
	if (fw < 160) fw = 160;
	int fx = (PAPER_W - fw) / 2;
	int fy = input_y - itemh / 2 + 2;
	int fh = itemh - 4;
	int field_base = fy + (fh - afont->height) / 2 + afont->ascent;
	XSetForeground(dpy, gc, fieldcol);
	XFillRectangle(dpy, buf, gc, fx, fy, fw, fh);
	XSetForeground(dpy, gc, bordercol);
	XDrawRectangle(dpy, buf, gc, fx, fy, fw - 1, fh - 1);

	/* input text, keeping "> " and the visible tail of the input */
	char disp[512];
	const char *tail = input;
	int maxw = fw - 24;
	while (*tail && appfont_width(afont, tail, (int)strlen(tail)) > maxw - 20)
		tail++;
	snprintf(disp, sizeof(disp), "> %s", tail);
	tw = appfont_width(afont, disp, (int)strlen(disp));
	int tx = fx + 10;
	appfont_draw(afont, buf, gc, &fc_fg, fgcol, tx, field_base, disp, (int)strlen(disp));

	/* block cursor at the end of the text */
	if (cursor_on) {
		XSetForeground(dpy, gc, c_hi);
		XFillRectangle(dpy, buf, gc, tx + tw + 1, fy + (fh - 12) / 2, 7, 12);
	}

	/* matching items, centered in the space below the input */
	int start = page * per_page;
	int pc = n_matches - start;
	if (pc > per_page) pc = per_page;
	if (pc < 0) pc = 0;
	int area_top = input_y + itemh;
	int area_bot = ftr_y - itemh;
	int block = pc * itemh;
	int top = area_top + ((area_bot - area_top) - block) / 2;
	char tmp[256];
	for (int i = 0; i < pc; i++) {
		int idx = start + i;
		int y = top + i * itemh;
		int is_sel = (idx == selected);
		truncate_label(matches[idx], tmp, sizeof(tmp), PAPER_W - 32);
		tw = appfont_width(afont, tmp, (int)strlen(tmp));
		if (is_sel)
			appfont_draw(afont, buf, gc, &fc_hi, c_hi, (PAPER_W - tw) / 2 - 20, y + base_off, ">", 1);
		appfont_draw(afont, buf, gc, is_sel ? &fc_hi : &fc_fg,
		             is_sel ? c_hi : fgcol, (PAPER_W - tw) / 2, y + base_off, tmp, (int)strlen(tmp));
	}

	/* page indicator, bottom-right */
	if (page_max > 0) {
		char pg[32];
		snprintf(pg, sizeof(pg), "[%d/%d]", page + 1, page_max + 1);
		int pw = appfont_width(afont, pg, (int)strlen(pg));
		appfont_draw(afont, buf, gc, &fc_dim, c_dim, PAPER_W - pw - 12, ftr_y, pg, (int)strlen(pg));
	}

	XCopyArea(dpy, buf, win, gc, 0, 0, PAPER_W, win_h, 0, 0);
	XSync(dpy, 0);
}

static void
run_cmd(const char *cmd)
{
	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execlp(cmd, cmd, NULL);
		_exit(1);
	}
}

static int lockfd = -1;

static void
sigcleanup(int unused)
{
	(void)unused;
	_exit(1);
}

int
main(void)
{
	/* single-instance lock: flock releases automatically on exit,
	   no stale-lock cleanup or signal-handler races */
	char lockfile[256];
	snprintf(lockfile, sizeof(lockfile), "/tmp/bytelaunch-%u.lock",
	         (unsigned int)getuid());
	lockfd = open(lockfile, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lockfd < 0)
		return 0;
	if (flock(lockfd, LOCK_EX | LOCK_NB) != 0)
		return 0;
	signal(SIGTERM, sigcleanup);
	signal(SIGINT, sigcleanup);
	signal(SIGHUP, sigcleanup);
	signal(SIGQUIT, sigcleanup);
	signal(SIGPIPE, sigcleanup);
	setbuf(stdout, NULL);

	if (!(dpy = XOpenDisplay(NULL))) return 1;
	scr = DefaultScreen(dpy);
	sw  = DisplayWidth(dpy, scr);
	sh  = DisplayHeight(dpy, scr);
	Window root = RootWindow(dpy, scr);

	afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) { XCloseDisplay(dpy); return 1; }

	win_h = PAPER_H;
	if (win_h > sh - 40) win_h = sh - 40;
	if (win_h < 240) win_h = 240;
	itemh = afont->height + 14;
	per_page = (win_h - itemh * 3 - 20) / itemh;
	if (per_page < 1) per_page = 1;
	if (per_page > 6) per_page = 6;

	bgcol = getcol("#282828");
	fgcol = getcol("#ebdbb2");
	c_hi  = getcol("#d65d0e");
	c_dim = getcol("#a89984");
	bordercol = getcol("#689d6a");
	fieldcol  = getcol("#3c3836");
	appfont_alloccolor(dpy, "#ebdbb2", &fc_fg);
	appfont_alloccolor(dpy, "#d65d0e", &fc_hi);
	appfont_alloccolor(dpy, "#a89984", &fc_dim);

	scan_path();
	if (!n_items) { appfont_close(afont); XCloseDisplay(dpy); return 1; }

	gc = XCreateGC(dpy, root, 0, NULL);
	buf = XCreatePixmap(dpy, root, PAPER_W, win_h, DefaultDepth(dpy, scr));
	XSetWindowAttributes wa = { .override_redirect = True, .background_pixel = bgcol };
	win = XCreateWindow(dpy, root,
		(sw - PAPER_W) / 2, (sh - win_h) / 2,
		PAPER_W, win_h, 0,
		DefaultDepth(dpy, scr), CopyFromParent, DefaultVisual(dpy, scr),
		CWOverrideRedirect | CWBackPixel, &wa);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask);
	XMapWindow(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

	selected = 0;
	filter();
	page = 0;
	page_max = pmax();
	draw();

	XEvent ev;
	int fd = ConnectionNumber(dpy);
	long last_blink = 0;
	int done = 0;
	while (!done) {
		fd_set rfds;
		struct timeval tv;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 50000;
		int r = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (r > 0) {
			while (XPending(dpy) && !done) {
				XNextEvent(dpy, &ev);
				if (ev.type == Expose) {
					if (ev.xexpose.count == 0) draw();
					continue;
				}
				if (ev.type != KeyPress) continue;
				cursor_on = 1;
				last_blink = now_ms();

				char kbuf[32];
				KeySym ks;
				int n = XLookupString(&ev.xkey, kbuf, sizeof kbuf, &ks, NULL);

				if (ks == XK_Return) {
					if (selected >= 0 && selected < n_matches)
						run_cmd(matches[selected]);
					else if (input[0])
						run_cmd(input);
					done = 1;
					break;
				}
				if (ks == XK_Escape) { input[0] = 0; ipos = 0; done = 1; break; }

				if (ks == XK_BackSpace) {
					if (ipos > 0) { input[--ipos] = 0; filter(); }
					selected = 0;
					page = 0;
					page_max = pmax();
					draw();
					continue;
				}
				if (ks == XK_Tab && n_matches > 0) {
					snprintf(input, sizeof(input), "%s", matches[selected]);
					ipos = strlen(input);
					filter();
					selected = 0;
					page = 0;
					page_max = pmax();
					draw();
					continue;
				}
				if (ks == XK_Down || ks == XK_j) {
					int pc = n_matches - page * per_page;
					if (pc > per_page) pc = per_page;
					if (pc > 0) {
						selected = page * per_page + (selected - page * per_page + 1) % pc;
						draw();
					}
					continue;
				}
				if (ks == XK_Up || ks == XK_k) {
					int pc = n_matches - page * per_page;
					if (pc > per_page) pc = per_page;
					if (pc > 0) {
						selected = page * per_page + (selected - page * per_page - 1 + pc) % pc;
						draw();
					}
					continue;
				}
				if (ks == XK_bracketright) {
					page_max = pmax();
					if (page < page_max) { page++; selected = page * per_page; draw(); }
					continue;
				}
				if (ks == XK_bracketleft) {
					if (page > 0) { page--; selected = page * per_page; draw(); }
					continue;
				}
				if (n == 1 && isprint((unsigned char)kbuf[0]) && ipos < INPUT_MAX) {
					input[ipos++] = kbuf[0]; input[ipos] = 0;
					filter();
					selected = 0;
					page = 0;
					page_max = pmax();
					draw();
				}
			}
		} else {
			/* blink tick: slowly flash the cursor */
			long now = now_ms();
			if (now - last_blink >= 500) {
				cursor_on = !cursor_on;
				last_blink = now;
				draw();
			}
		}
	}

	appfont_close(afont);
	XDestroyWindow(dpy, win);
	XFreePixmap(dpy, buf);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	for (int i = 0; i < n_items; i++) free(items[i]);
	return 0;
}

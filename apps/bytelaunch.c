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
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ITEMS 16384
#define MAX_VISIBLE 16
#define INPUT_MAX  255

static Display *dpy;
static Window  win;
static GC      gc;
static XFontStruct *font;
static int    bh, sw, sh, scr;

static char   input[INPUT_MAX + 1];
static int    ipos;

static char  *items[MAX_ITEMS];
static int    n_items;
static char  *matches[MAX_ITEMS];
static int    n_matches;
static int    selected;

static unsigned long bgcol, fgcol, selcol;

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

static void
draw(void)
{
	int vis = n_matches < MAX_VISIBLE ? n_matches : MAX_VISIBLE;
	if (vis < 0) vis = 0;
	int height = bh + vis * bh;
	if (height < bh) height = bh;

	int winw = sw / 2;
	if (winw < 320) winw = 320;
	if (winw > 640) winw = 640;
	int winx = (sw - winw) / 2;
	int winy = 24;

	XMoveResizeWindow(dpy, win, winx, winy, winw, height);
	XSetForeground(dpy, gc, bgcol);
	XFillRectangle(dpy, win, gc, 0, 0, winw, height);

	int ty = (bh - (font->ascent + font->descent)) / 2 + font->ascent;

	char prompt[512];
	snprintf(prompt, sizeof(prompt), "> %s", input);
	XSetForeground(dpy, gc, fgcol);
	XDrawString(dpy, win, gc, 8, ty, prompt, strlen(prompt));

	int start = 0;
	if (selected >= vis) start = selected - vis + 1;
	if (start < 0) start = 0;

	for (int i = 0; i < vis && (start + i) < n_matches; i++) {
		int idx = start + i;
		int y   = (i + 1) * bh + ty;
		if (idx == selected) {
			XSetForeground(dpy, gc, selcol);
			XFillRectangle(dpy, win, gc, 0, (i + 1) * bh, winw, bh);
			XSetForeground(dpy, gc, bgcol);
		} else {
			XSetForeground(dpy, gc, fgcol);
		}
		XDrawString(dpy, win, gc, 8, y, matches[idx], strlen(matches[idx]));
	}
	XSync(dpy, 0);
}

static void
run_cmd(const char *cmd)
{
	if (!fork()) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execlp(cmd, cmd, NULL);
		_exit(1);
	}
}

static char lockpath[256];
static int lockacquired;

static void
cleanup_lock(void)
{
	if (lockacquired) {
		char pidfile[512];
		snprintf(pidfile, sizeof(pidfile), "%s/pid", lockpath);
		unlink(pidfile);
		rmdir(lockpath);
		lockacquired = 0;
	}
}

static void
sigcleanup(int unused)
{
	(void)unused;
	cleanup_lock();
	_exit(1);
}

int
main(void)
{
	snprintf(lockpath, sizeof(lockpath), "/tmp/bytelaunch-%u.lock",
	         (unsigned int)getuid());
	if (mkdir(lockpath, 0700) != 0) {
		if (errno == EEXIST) {
			char pidfile[512];
			snprintf(pidfile, sizeof(pidfile), "%s/pid", lockpath);
			FILE *pf = fopen(pidfile, "r");
			if (pf) {
				pid_t oldpid = 0;
				if (fscanf(pf, "%d", &oldpid) == 1 && oldpid > 0
				    && kill(oldpid, 0) != 0 && errno == ESRCH) {
					fclose(pf);
					unlink(pidfile);
					rmdir(lockpath);
					if (mkdir(lockpath, 0700) != 0)
						return 0;
				} else {
					fclose(pf);
					return 0;
				}
			} else {
				return 0;
			}
		} else {
			return 0;
		}
	}
	lockacquired = 1;
	{
		char pidfile[512];
		snprintf(pidfile, sizeof(pidfile), "%s/pid", lockpath);
		FILE *pf = fopen(pidfile, "w");
		if (pf) {
			fprintf(pf, "%d\n", (int)getpid());
			fclose(pf);
		}
	}
	atexit(cleanup_lock);
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

	font = XLoadQueryFont(dpy, "fixed");
	if (!font) { XCloseDisplay(dpy); return 1; }
	bh = font->ascent + font->descent + 8;

	bgcol = getcol("#282828");
	fgcol = getcol("#ebdbb2");
	selcol= getcol("#689d6a");

	scan_path();
	if (!n_items) { XFreeFont(dpy, font); XCloseDisplay(dpy); return 1; }

	gc = XCreateGC(dpy, root, 0, NULL);
	XSetWindowAttributes wa = { .override_redirect = True, .background_pixel = bgcol };
	win = XCreateWindow(dpy, root, 0, 0, 1, bh, 0,
		DefaultDepth(dpy, scr), CopyFromParent, DefaultVisual(dpy, scr),
		CWOverrideRedirect | CWBackPixel, &wa);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask);
	XMapWindow(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

	selected = 0;
	filter();
	draw();

	XEvent ev;
	while (XNextEvent(dpy, &ev) >= 0) {
		if (ev.type == Expose) {
			if (ev.xexpose.count == 0) draw();
			continue;
		}
		if (ev.type != KeyPress) continue;

		char kbuf[32];
		KeySym ks;
		int n = XLookupString(&ev.xkey, kbuf, sizeof kbuf, &ks, NULL);

		if (ks == XK_Return) {
			if (selected >= 0 && selected < n_matches)
				run_cmd(matches[selected]);
			else if (input[0])
				run_cmd(input);
			break;
		}
		if (ks == XK_Escape) { input[0] = 0; ipos = 0; break; }

		if (ks == XK_BackSpace) {
			if (ipos > 0) { input[--ipos] = 0; filter(); }
			selected = 0;
			draw();
			continue;
		}
		if (ks == XK_Tab && n_matches > 0) {
			snprintf(input, sizeof(input), "%s", matches[selected]);
			ipos = strlen(input);
			filter();
			selected = 0;
			draw();
			continue;
		}
		if (ks == XK_Down || ks == XK_j) {
			if (n_matches > 0) { selected = (selected + 1) % n_matches; draw(); }
			continue;
		}
		if (ks == XK_Up || ks == XK_k) {
			if (n_matches > 0) { selected = (selected - 1 + n_matches) % n_matches; draw(); }
			continue;
		}
		if (n == 1 && isprint((unsigned char)kbuf[0]) && ipos < INPUT_MAX) {
			input[ipos++] = kbuf[0]; input[ipos] = 0;
			filter();
			selected = 0;
			draw();
		}
	}

	XDestroyWindow(dpy, win);
	XFreeGC(dpy, gc);
	XFreeFont(dpy, font);
	XCloseDisplay(dpy);
	for (int i = 0; i < n_items; i++) free(items[i]);
	return 0;
}

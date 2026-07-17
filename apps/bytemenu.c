#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ITEMS  24
#define ITEM_H     30
#define WIN_W      420
#define BORDER_W   2

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int sw, sh, win_h;
static unsigned long c_bg, c_fg, c_hi, c_border, c_dim;
static char *labels[MAX_ITEMS], *commands[MAX_ITEMS];
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

	const char *hdr = "b y t e m e n u";
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
			XDrawString(dpy, win, gc, 12, y + 20, ">", 1);
		}
		XSetForeground(dpy, gc, i == sel ? c_hi : c_fg);
		XDrawString(dpy, win, gc, 28, y + 20, labels[i], strlen(labels[i]));
		y += ITEM_H;
	}

	const char *ftr = "j/k  enter  esc";
	tw = XTextWidth(xfont, ftr, strlen(ftr));
	XSetForeground(dpy, gc, c_dim);
	XDrawString(dpy, win, gc, (WIN_W - tw)/2, win_h - 14, ftr, strlen(ftr));

	XSync(dpy, False);
}

static void read_config(void) {
	char *home = getenv("HOME");
	if (!home) return;
	char path[1024];
	snprintf(path, sizeof(path), "%s/.config/bytemenu/menu.conf", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f) && count < MAX_ITEMS) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		char *p = strchr(line, '|');
		if (!p) continue;
		*p++ = 0;
		while (*p == ' ') p++;
		if (!line[0] || !*p) continue;
		labels[count] = strdup(line);
		commands[count] = strdup(p);
		count++;
	}
	fclose(f);
}

static void launch(void) {
	if (!commands[sel]) return;
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", commands[sel], NULL);
		_exit(1);
	}
}

int main(void) {
	read_config();
	if (!count) {
		labels[0] = strdup("Terminal");   commands[0] = strdup("st");
		labels[1] = strdup("Browser");    commands[1] = strdup("firefox");
		labels[2] = strdup("Files");      commands[2] = strdup("st -e ranger");
		count = 3;
	}

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
	c_border = getcol("#504945");
	c_dim    = getcol("#a89984");

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
	XGrabPointer(dpy, win, True,
		ButtonPressMask, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime);

	XEvent ev;
	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			draw();
		else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Escape || ks == XK_q) break;
			if (ks == XK_Return)            { launch(); break; }
			if ((ks == XK_j || ks == XK_Down) && sel < count-1)  { sel++; draw(); }
			if ((ks == XK_k || ks == XK_Up)   && sel > 0)        { sel--; draw(); }
		}
	}

	XUngrabPointer(dpy, CurrentTime);
	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	for (int i = 0; i < count; i++) { free(labels[i]); free(commands[i]); }
	return 0;
}

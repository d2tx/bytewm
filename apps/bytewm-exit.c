/* bytewm-exit - logout/restart/shutdown dialog */
#define _POSIX_C_SOURCE 200809L
#define BORDER_W 2
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "appfont.h"

static Display *dpy;
static Window win;
static GC gc;
static AppFont *afont;
static int bh, sw;
static int selected = 0;

static const char *bg = "#282828";
static const char *fg = "#ebdbb2";
static const char *selbg = "#689d6a";
static const char *selfg = "#282828";

static const char *items[] = { "Lock", "Logout", "Restart", "Shutdown" };
static const char *cmds[] = {
	"bytelock",
	"pkill -x -u \"$USER\" bytewm",
	"st -e sh -c \"sudo systemctl reboot\"",
	"st -e sh -c \"sudo systemctl poweroff\""
};
static int nitems = 4;

static unsigned long
getcol(const char *c)
{
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	if (!XParseColor(dpy, cmap, c, &xc)) return 0;
	if (!XAllocColor(dpy, cmap, &xc)) return 0;
	return xc.pixel;
}

static unsigned long cbg, cfg, cselbg, cselfg, cborder;
static XftColor fc_fg, fc_selfg;

static void
redraw(void)
{
	int pad = 10;
	int itemh = bh + 4;
	int total = nitems * itemh;
	int winw = sw; /* will be resized below */
	int winh = total + 8;

	/* find widest item */
	int maxw = 0;
	for (int i = 0; i < nitems; i++) {
		int tw = appfont_width(afont, items[i], (int)strlen(items[i]));
		if (tw > maxw) maxw = tw;
	}
	winw = maxw + pad * 4;
	winh = total + 8;

	int sh = DisplayHeight(dpy, DefaultScreen(dpy));
	int x = (sw - winw) / 2;
	int y = (sh - winh) / 2;
	XMoveResizeWindow(dpy, win, x, y, winw, winh);

	XSetForeground(dpy, gc, cbg);
	XFillRectangle(dpy, win, gc, 0, 0, winw, winh);

	XSetForeground(dpy, gc, cborder);
	XFillRectangle(dpy, win, gc, 0, 0, winw, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, winh - BORDER_W, winw, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, 0, BORDER_W, winh);
	XFillRectangle(dpy, win, gc, winw - BORDER_W, 0, BORDER_W, winh);

	for (int i = 0; i < nitems; i++) {
		int tw = appfont_width(afont, items[i], (int)strlen(items[i]));
		int ix = (winw - tw) / 2;
		int iy = 4 + i * itemh + bh / 2;

		if (i == selected) {
			XSetForeground(dpy, gc, cselbg);
			XFillRectangle(dpy, win, gc, ix - pad, iy - bh / 2, tw + pad * 2, bh);
		}
		appfont_draw(afont, win, gc, i == selected ? &fc_selfg : &fc_fg,
		             i == selected ? cselfg : cfg,
		             ix, iy + bh / 2 - 2, items[i], (int)strlen(items[i]));
	}
	XFlush(dpy);
}

static void
run_cmd(int idx)
{
	XUnmapWindow(dpy, win);
	XFlush(dpy);
	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		/* detach and drop inherited descriptors (incl. X socket) */
		for (int fd = 3; fd < 1024; fd++)
			close(fd);
		setsid();
		execl("/bin/sh", "sh", "-c", cmds[idx], NULL);
		_exit(1);
	}
}

int
main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (!dpy) return 1;

	int screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	Window root = RootWindow(dpy, screen);

	afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) { XCloseDisplay(dpy); return 1; }
	bh = afont->height + 4;

	cbg = getcol(bg);
	cfg = getcol(fg);
	cselbg = getcol(selbg);
	cselfg = getcol(selfg);
	cborder = getcol("#689d6a");
	appfont_alloccolor(dpy, fg, &fc_fg);
	appfont_alloccolor(dpy, selfg, &fc_selfg);

	int maxw = 0;
	for (int i = 0; i < nitems; i++) {
		int tw = appfont_width(afont, items[i], (int)strlen(items[i]));
		if (tw > maxw) maxw = tw;
	}

	int pad = 10;
	int winw = maxw + pad * 4;
	int winh = nitems * (bh + 4) + 8;
	int sh = DisplayHeight(dpy, screen);
	int winx = (sw - winw) / 2;
	int winy = (sh - winh) / 2;

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = cbg,
	};
	win = XCreateWindow(dpy, root, winx, winy, winw, winh, 0,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel, &wa);

	gc = XCreateGC(dpy, win, 0, NULL);

	XSelectInput(dpy, win, ExposureMask | KeyPressMask |
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

	XMapWindow(dpy, win);
	XRaiseWindow(dpy, win);
	XSync(dpy, False);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

	XEvent ev;
	redraw();

	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose) {
			if (ev.xexpose.count == 0) redraw();
		} else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Escape || ks == XK_q) break;
			if (ks == XK_Up || ks == XK_k) { selected = (selected - 1 + nitems) % nitems; redraw(); }
			if (ks == XK_Down || ks == XK_j) { selected = (selected + 1) % nitems; redraw(); }
			if (ks == XK_Return || ks == XK_space) { run_cmd(selected); break; }
		} else if (ev.type == ButtonPress) {
			int idx = ev.xbutton.y / (bh + 4);
			if (idx >= 0 && idx < nitems) {
				selected = idx;
				redraw();
			}
		} else if (ev.type == MotionNotify) {
			int idx = ev.xmotion.y / (bh + 4);
			if (idx >= 0 && idx < nitems && idx != selected) {
				selected = idx;
				redraw();
			}
		}
	}

	XDestroyWindow(dpy, win);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}

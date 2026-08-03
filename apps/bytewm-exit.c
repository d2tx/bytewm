/* bytewm-exit - logout/restart/shutdown dialog */
#define _POSIX_C_SOURCE 200809L
#define PAPER_W 324
#define PAPER_H 378
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
static Pixmap buf;
static GC gc;
static AppFont *afont;
static int sw, sh, win_h, itemh;
static int selected = 0;

static const char *items[] = { "lock", "logout", "suspend", "restart", "shutdown" };
static const char *cmds[] = {
	"bytelock",
	"pkill -x -u \"$USER\" bytewm",
	"sudo -n systemctl suspend",
	"sudo -n systemctl reboot",
	"sudo -n systemctl poweroff"
};
static int nitems = 5;

static unsigned long
getcol(const char *c)
{
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	if (!XParseColor(dpy, cmap, c, &xc)) return 0;
	if (!XAllocColor(dpy, cmap, &xc)) return 0;
	return xc.pixel;
}

static unsigned long cbg, cfg, chi, cdim, cborder;
static XftColor fc_fg, fc_hi, fc_dim;

static int
item_top(void)
{
	int ftr_y = win_h - (itemh / 2 + 7);
	int block = nitems * itemh;
	int area_top = itemh * 2;
	int area_bot = ftr_y - itemh;
	return area_top + ((area_bot - area_top) - block) / 2;
}

static void
redraw(void)
{
	int hdr_y = itemh + 6;
	int rule_y = itemh + 18;
	int base_off = itemh - 8;

	XSetForeground(dpy, gc, cbg);
	XFillRectangle(dpy, buf, gc, 0, 0, PAPER_W, win_h);

	const char *hdr = "e x i t";
	int tw = appfont_width(afont, hdr, (int)strlen(hdr));
	appfont_draw(afont, buf, gc, &fc_dim, cdim, (PAPER_W - tw) / 2, hdr_y, hdr, (int)strlen(hdr));

	XSetForeground(dpy, gc, cborder);
	XFillRectangle(dpy, buf, gc, 20, rule_y, PAPER_W - 40, 2);
	XFillRectangle(dpy, buf, gc, 0, 0, PAPER_W, BORDER_W);
	XFillRectangle(dpy, buf, gc, 0, win_h - BORDER_W, PAPER_W, BORDER_W);
	XFillRectangle(dpy, buf, gc, 0, 0, BORDER_W, win_h);
	XFillRectangle(dpy, buf, gc, PAPER_W - BORDER_W, 0, BORDER_W, win_h);

	int top = item_top();
	for (int i = 0; i < nitems; i++) {
		int y = top + i * itemh;
		tw = appfont_width(afont, items[i], (int)strlen(items[i]));
		if (i == selected)
			appfont_draw(afont, buf, gc, &fc_hi, chi, (PAPER_W - tw) / 2 - 20, y + base_off, ">", 1);
		appfont_draw(afont, buf, gc, i == selected ? &fc_hi : &fc_fg,
		             i == selected ? chi : cfg, (PAPER_W - tw) / 2, y + base_off, items[i], (int)strlen(items[i]));
	}

	XCopyArea(dpy, buf, win, gc, 0, 0, PAPER_W, win_h, 0, 0);
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
	sh = DisplayHeight(dpy, screen);
	Window root = RootWindow(dpy, screen);

	afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) { XCloseDisplay(dpy); return 1; }

	win_h = PAPER_H;
	if (win_h > sh - 40) win_h = sh - 40;
	if (win_h < 240) win_h = 240;
	itemh = afont->height + 14;

	cbg = getcol("#282828");
	cfg = getcol("#ebdbb2");
	chi = getcol("#d65d0e");
	cdim = getcol("#a89984");
	cborder = getcol("#689d6a");
	appfont_alloccolor(dpy, "#ebdbb2", &fc_fg);
	appfont_alloccolor(dpy, "#d65d0e", &fc_hi);
	appfont_alloccolor(dpy, "#a89984", &fc_dim);

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = cbg,
	};
	win = XCreateWindow(dpy, root,
		(sw - PAPER_W) / 2, (sh - win_h) / 2,
		PAPER_W, win_h, 0,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel, &wa);

	gc = XCreateGC(dpy, win, 0, NULL);
	buf = XCreatePixmap(dpy, root, PAPER_W, win_h, DefaultDepth(dpy, screen));

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
			int top = item_top();
			int idx = (ev.xbutton.y - top) / itemh;
			if (idx >= 0 && idx < nitems) {
				selected = idx;
				redraw();
			}
		} else if (ev.type == MotionNotify) {
			int top = item_top();
			int idx = (ev.xmotion.y - top) / itemh;
			if (idx >= 0 && idx < nitems && idx != selected) {
				selected = idx;
				redraw();
			}
		}
	}

	appfont_close(afont);
	XDestroyWindow(dpy, win);
	XFreePixmap(dpy, buf);
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}

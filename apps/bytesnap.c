#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "appfont.h"

#define PAPER_W   320
#define PAPER_H   360
#define BORDER_W  2

static unsigned long getcol(Display *dpy, const char *s) {
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	XParseColor(dpy, cmap, s, &xc);
	XAllocColor(dpy, cmap, &xc);
	return xc.pixel;
}

static int bitpos(unsigned long mask) {
	if (!mask) return 0;
	for (int i = 0; i < (int)(sizeof(mask) * 8); i++)
		if (mask & (1UL << i)) return i;
	return 0;
}

static void notify(void) {
	int fd = open("/tmp/bytify.fifo", O_WRONLY | O_NONBLOCK);
	if (fd >= 0) { dprintf(fd, "screenshot saved\n"); close(fd); }
}

static void save_ppm(Display *dpy, XImage *img, int w, int h) {
	char *home = getenv("HOME");
	char path[512];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	if (!tm) { XDestroyImage(img); return; }
	if (home) snprintf(path, sizeof(path), "%s/screenshots", home);
	else snprintf(path, sizeof(path), "/tmp/screenshots");
	mkdir(path, 0755);
	char filename[64];
	strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S", tm);
	char fullpath[1024];
	snprintf(fullpath, sizeof(fullpath), "%s/%s.ppm", path, filename);
	FILE *fp = fopen(fullpath, "wb");
	if (!fp) { fprintf(stderr, "bytesnap: could not write %s\n", fullpath); XDestroyImage(img); return; }
	fprintf(fp, "P6\n%d %d\n255\n", w, h);
	int rshift = bitpos(img->red_mask);
	int gshift = bitpos(img->green_mask);
	int bshift = bitpos(img->blue_mask);
	int rmax = (int)(img->red_mask >> rshift);
	int gmax = (int)(img->green_mask >> gshift);
	int bmax = (int)(img->blue_mask >> bshift);
	if (rmax == 0) rmax = 1;
	if (gmax == 0) gmax = 1;
	if (bmax == 0) bmax = 1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			unsigned long p = XGetPixel(img, x, y);
			unsigned char r = (unsigned char)(((p & img->red_mask) >> rshift) * 255 / rmax);
			unsigned char g = (unsigned char)(((p & img->green_mask) >> gshift) * 255 / gmax);
			unsigned char b = (unsigned char)(((p & img->blue_mask) >> bshift) * 255 / bmax);
			fwrite(&r, 1, 1, fp); fwrite(&g, 1, 1, fp); fwrite(&b, 1, 1, fp);
		}
	fclose(fp);
	XDestroyImage(img);
	notify();
	printf("%s\n", fullpath);
}

static void countdown(Display *dpy, Window root, int screen) {
	int sw = DisplayWidth(dpy, screen);
	int sh = DisplayHeight(dpy, screen);
	AppFont *afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) return;
	unsigned long c_bg = getcol(dpy, "#282828");
	XSetWindowAttributes cwa = { .override_redirect = True, .background_pixel = c_bg };
	Window cwin = XCreateWindow(dpy, root, (sw-40)/2, (sh-30)/2, 40, 30, 2,
		DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
		CWOverrideRedirect | CWBackPixel, &cwa);
	XSetWindowBorder(dpy, cwin, getcol(dpy, "#689d6a"));
	GC cgc = XCreateGC(dpy, cwin, 0, NULL);
	XMapRaised(dpy, cwin);
	XSetInputFocus(dpy, cwin, RevertToParent, CurrentTime);
	const char *cols[] = { "#cc241d", "#d65d0e", "#689d6a" };
	XftColor fc;
	for (int i = 3; i > 0; i--) {
		char buf[2] = { '0' + i, '\0' };
		XSetForeground(dpy, cgc, c_bg);
		XFillRectangle(dpy, cwin, cgc, 0, 0, 40, 30);
		appfont_alloccolor(dpy, cols[3-i], &fc);
		int tw = appfont_width(afont, buf, 1);
		appfont_draw(afont, cwin, cgc, &fc, getcol(dpy, cols[3-i]),
			(40-tw)/2, (30+afont->height)/2, buf, 1);
		XFlush(dpy);
		sleep(1);
	}
	XSetForeground(dpy, cgc, c_bg);
	XFillRectangle(dpy, cwin, cgc, 0, 0, 40, 30);
	XFlush(dpy);
	sleep(1);
	appfont_close(afont);
	XDestroyWindow(dpy, cwin);
	XFreeGC(dpy, cgc);
	XSync(dpy, False);
}

static void draw_menu(Display *dpy, Window win, GC gc, AppFont *afont,
	unsigned long c_bg, unsigned long c_fg, unsigned long c_hi,
	unsigned long c_dim, unsigned long c_border,
	XftColor *fc_fg, XftColor *fc_hi, XftColor *fc_dim,
	char **labels, int count, int sel, int win_h)
{
	int itemh = afont->height + 14;
	int hdr_y = itemh + 6;
	int rule_y = itemh + 18;
	int top_margin = itemh * 2;
	int bot_margin = itemh + 20;
	int base_off = itemh - 8;
	int ftr_y = win_h - (itemh / 2 + 7);

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, PAPER_W, win_h);

	const char *hdr = "b y t e s n a p";
	int tw = appfont_width(afont, hdr, (int)strlen(hdr));
	appfont_draw(afont, win, gc, fc_dim, c_dim, (PAPER_W - tw) / 2, hdr_y, hdr, (int)strlen(hdr));

	XSetForeground(dpy, gc, c_border);
	XFillRectangle(dpy, win, gc, 20, rule_y, PAPER_W - 40, 2);
	XFillRectangle(dpy, win, gc, 0, 0, PAPER_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, win_h - BORDER_W, PAPER_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, 0, BORDER_W, win_h);
	XFillRectangle(dpy, win, gc, PAPER_W - BORDER_W, 0, BORDER_W, win_h);

	int block = count * itemh;
	int top = top_margin + ((win_h - top_margin - bot_margin) - block) / 2;
	for (int i = 0; i < count; i++) {
		int y = top + i * itemh;
		tw = appfont_width(afont, labels[i], (int)strlen(labels[i]));
		if (i == sel)
			appfont_draw(afont, win, gc, fc_hi, c_hi, (PAPER_W - tw) / 2 - 20, y + base_off, ">", 1);
		appfont_draw(afont, win, gc, i == sel ? fc_hi : fc_fg,
		             i == sel ? c_hi : c_fg, (PAPER_W - tw) / 2, y + base_off, labels[i], (int)strlen(labels[i]));
	}

	const char *ftr = "j/k  enter  esc";
	tw = appfont_width(afont, ftr, (int)strlen(ftr));
	appfont_draw(afont, win, gc, fc_dim, c_dim, (PAPER_W - tw) / 2, ftr_y, ftr, (int)strlen(ftr));

	XSync(dpy, False);
}

static int show_menu(Display *dpy, int screen, int sw, int sh) {
	Window root = RootWindow(dpy, screen);
	char *labels[] = { "fullscreen", "region", "window" };
	int count = 3;
	int win_h = PAPER_H;
	if (win_h > sh - 40) win_h = sh - 40;
	if (win_h < 240) win_h = 240;

	AppFont *afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) return -1;

	unsigned long c_bg     = getcol(dpy, "#282828");
	unsigned long c_fg     = getcol(dpy, "#ebdbb2");
	unsigned long c_hi     = getcol(dpy, "#d65d0e");
	unsigned long c_dim    = getcol(dpy, "#a89984");
	unsigned long c_border = getcol(dpy, "#689d6a");
	XftColor fc_fg, fc_hi, fc_dim;
	appfont_alloccolor(dpy, "#ebdbb2", &fc_fg);
	appfont_alloccolor(dpy, "#d65d0e", &fc_hi);
	appfont_alloccolor(dpy, "#a89984", &fc_dim);

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = c_bg,
		.event_mask = ExposureMask | KeyPressMask
	};
	Window win = XCreateWindow(dpy, root,
		(sw - PAPER_W) / 2, (sh - win_h) / 2,
		PAPER_W, win_h, 0,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	GC gc = XCreateGC(dpy, root, 0, NULL);

	XMapRaised(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	XGrabPointer(dpy, win, True,
		ButtonPressMask, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime);

	int sel = 0;
	XEvent ev;
	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			draw_menu(dpy, win, gc, afont, c_bg, c_fg, c_hi, c_dim, c_border,
				&fc_fg, &fc_hi, &fc_dim, labels, count, sel, win_h);
		else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_j || ks == XK_Down) { sel++; if (sel >= count) sel = 0; draw_menu(dpy, win, gc, afont, c_bg, c_fg, c_hi, c_dim, c_border, &fc_fg, &fc_hi, &fc_dim, labels, count, sel, win_h); }
			else if (ks == XK_k || ks == XK_Up) { sel--; if (sel < 0) sel = count - 1; draw_menu(dpy, win, gc, afont, c_bg, c_fg, c_hi, c_dim, c_border, &fc_fg, &fc_hi, &fc_dim, labels, count, sel, win_h); }
			else if (ks == XK_Return) { appfont_close(afont); XFreeGC(dpy, gc); XDestroyWindow(dpy, win); XUngrabPointer(dpy, CurrentTime); return sel; }
			else if (ks == XK_Escape || ks == XK_q) { appfont_close(afont); XFreeGC(dpy, gc); XDestroyWindow(dpy, win); XUngrabPointer(dpy, CurrentTime); return -1; }
		}
	}
}

static void capture_fullscreen(Display *dpy, int screen) {
	Window root = RootWindow(dpy, screen);
	int w = DisplayWidth(dpy, screen);
	int h = DisplayHeight(dpy, screen);
	countdown(dpy, root, screen);
	XImage *img = XGetImage(dpy, root, 0, 0, w, h, AllPlanes, ZPixmap);
	if (!img) { fprintf(stderr, "bytesnap: XGetImage failed\n"); return; }
	save_ppm(dpy, img, w, h);
}

static void capture_region(Display *dpy, int screen, Window root) {
	int sw = DisplayWidth(dpy, screen);
	int sh = DisplayHeight(dpy, screen);

	XSetWindowAttributes attrs;
	attrs.override_redirect = True;
	attrs.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask;
	attrs.cursor = XCreateFontCursor(dpy, XC_crosshair);
	Window overlay = XCreateWindow(dpy, root, 0, 0, sw, sh, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask | CWCursor, &attrs);

	XMapRaised(dpy, overlay);
	XFlush(dpy);

	/* copy screen content to overlay so it appears transparent */
	GC copy_gc = XCreateGC(dpy, overlay, 0, NULL);
	XCopyArea(dpy, root, overlay, copy_gc, 0, 0, sw, sh, 0, 0);
	XFreeGC(dpy, copy_gc);
	XFlush(dpy);

	if (XGrabPointer(dpy, overlay, True,
			ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			GrabModeAsync, GrabModeAsync, overlay, None, CurrentTime) != GrabSuccess)
		{ XDestroyWindow(dpy, overlay); return; }
	if (XGrabKeyboard(dpy, overlay, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		{ XUngrabPointer(dpy, CurrentTime); XDestroyWindow(dpy, overlay); return; }

	GC gc = XCreateGC(dpy, overlay, 0, NULL);
	XSetFunction(dpy, gc, GXxor);
	XSetForeground(dpy, gc, ~0);
	XSetSubwindowMode(dpy, gc, IncludeInferiors);

	int rx = 0, ry = 0, rw = 0, rh = 0;
	int ox = 0, oy = 0, ow = 0, oh = 0;
	int dragging = 0, done = 0;
	XEvent ev;

	while (!done && XNextEvent(dpy, &ev) >= 0) {
		if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
			rx = ev.xbutton.x; ry = ev.xbutton.y;
			ox = rx; oy = ry; ow = 0; oh = 0;
			dragging = 1;
		} else if (ev.type == MotionNotify && dragging) {
			XDrawRectangle(dpy, overlay, gc, ox, oy, ow, oh);
			int cx = ev.xmotion.x, cy = ev.xmotion.y;
			ox = rx < cx ? rx : cx;
			oy = ry < cy ? ry : cy;
			ow = rx < cx ? cx - rx : rx - cx;
			oh = ry < cy ? cy - ry : ry - cy;
			XDrawRectangle(dpy, overlay, gc, ox, oy, ow, oh);
			XFlush(dpy);
		} else if (ev.type == ButtonRelease && ev.xbutton.button == Button1 && dragging) {
			XDrawRectangle(dpy, overlay, gc, ox, oy, ow, oh);
			rx = ox; ry = oy; rw = ow; rh = oh;
			done = 1;
		} else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Escape) { rx = ry = rw = rh = 0; done = 1; }
		}
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, overlay);
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XFlush(dpy);

	if (rw < 5 || rh < 5) return;
	countdown(dpy, root, screen);
	XImage *img = XGetImage(dpy, root, rx, ry, rw, rh, AllPlanes, ZPixmap);
	if (!img) { fprintf(stderr, "bytesnap: XGetImage failed\n"); return; }
	save_ppm(dpy, img, rw, rh);
}

static Window get_toplevel(Display *dpy, Window root, Window w) {
	Window *children, parent;
	unsigned int n;
	while (w != root && w != None) {
		if (!XQueryTree(dpy, w, &root, &parent, &children, &n)) break;
		if (children) XFree(children);
		if (parent == root || parent == None) return w;
		w = parent;
	}
	return w;
}

static void capture_window(Display *dpy, int screen, Window root) {
	int sw = DisplayWidth(dpy, screen);
	int sh = DisplayHeight(dpy, screen);

	XSetWindowAttributes attrs;
	attrs.override_redirect = True;
	attrs.event_mask = ButtonPressMask | KeyPressMask;
	attrs.cursor = XCreateFontCursor(dpy, XC_crosshair);
	Window overlay = XCreateWindow(dpy, root, 0, 0, sw, sh, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask | CWCursor, &attrs);

	XMapRaised(dpy, overlay);
	XFlush(dpy);

	/* copy screen content so overlay appears transparent */
	GC copy_gc = XCreateGC(dpy, overlay, 0, NULL);
	XCopyArea(dpy, root, overlay, copy_gc, 0, 0, sw, sh, 0, 0);
	XFreeGC(dpy, copy_gc);
	XFlush(dpy);

	if (XGrabPointer(dpy, overlay, True,
			ButtonPressMask | ButtonReleaseMask,
			GrabModeAsync, GrabModeAsync, overlay, None, CurrentTime) != GrabSuccess)
		{ XDestroyWindow(dpy, overlay); return; }
	if (XGrabKeyboard(dpy, overlay, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		{ XUngrabPointer(dpy, CurrentTime); XDestroyWindow(dpy, overlay); return; }

	int rx = 0, ry = 0, rw = 0, rh = 0, done = 0;
	XEvent ev;

	while (!done && XNextEvent(dpy, &ev) >= 0) {
		if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
			/* the overlay is topmost, so hide it before resolving the
			   window under the cursor */
			XUnmapWindow(dpy, overlay);
			XSync(dpy, False);
			Window rootret = None, childret = None;
			int wx, wy;
			XQueryPointer(dpy, root, &rootret, &childret,
				&wx, &wy, &wx, &wy, &ev.xbutton.state);
			Window target = get_toplevel(dpy, root, childret);
			if (target && target != root) {
				XWindowAttributes a;
				if (XGetWindowAttributes(dpy, target, &a)) {
					int tx = 0, ty = 0, sx = 0, sy = 0;
					Window dummy;
					XTranslateCoordinates(dpy, target, root, 0, 0, &tx, &ty, &dummy);
					XTranslateCoordinates(dpy, target, root, a.width, a.height, &sx, &sy, &dummy);
					rx = tx; ry = ty;
					rw = sx - tx; rh = sy - ty;
					if (rx < 0) { rw += rx; rx = 0; }
					if (ry < 0) { rh += ry; ry = 0; }
					if (rw > sw - rx) rw = sw - rx;
					if (rh > sh - ry) rh = sh - ry;
				}
			}
			done = 1;
		} else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Escape) done = 1;
		}
	}

	XDestroyWindow(dpy, overlay);
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XFlush(dpy);

	if (rw < 5 || rh < 5) return;
	countdown(dpy, root, screen);
	XImage *img = XGetImage(dpy, root, rx, ry, rw, rh, AllPlanes, ZPixmap);
	if (!img) { fprintf(stderr, "bytesnap: XGetImage failed\n"); return; }
	save_ppm(dpy, img, rw, rh);
}

int main(int argc, char **argv) {
	int mode = -1;
	if (argc > 1) {
		if (!strcmp(argv[1], "-f") || !strcmp(argv[1], "--fullscreen")) mode = 0;
		else if (!strcmp(argv[1], "-r") || !strcmp(argv[1], "--region")) mode = 1;
		else if (!strcmp(argv[1], "-w") || !strcmp(argv[1], "--window")) mode = 2;
	}

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "bytesnap: could not open display\n"); return 1; }
	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	int sw = DisplayWidth(dpy, screen);
	int sh = DisplayHeight(dpy, screen);

	if (mode < 0) mode = show_menu(dpy, screen, sw, sh);
	if (mode < 0) { XCloseDisplay(dpy); return 0; }

	XSync(dpy, False);

	switch (mode) {
	case 0: capture_fullscreen(dpy, screen); break;
	case 1: capture_region(dpy, screen, root); break;
	case 2: capture_window(dpy, screen, root); break;
	}

	XCloseDisplay(dpy);
	return 0;
}

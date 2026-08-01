#define _POSIX_C_SOURCE 200809L
#include "appfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char shared_buf[128];
static char path_buf[512];

const char *appfont_sharedname(void) {
	const char *home = getenv("HOME");
	if (home) {
		snprintf(path_buf, sizeof(path_buf), "%s/.config/bytewm/font", home);
		FILE *f = fopen(path_buf, "r");
		if (f) {
			if (fgets(shared_buf, sizeof(shared_buf), f)) {
				size_t n = strlen(shared_buf);
				while (n && (shared_buf[n-1] == '\n' || shared_buf[n-1] == ' '))
					shared_buf[--n] = '\0';
				fclose(f);
				if (shared_buf[0]) return shared_buf;
			}
			fclose(f);
		}
	}
	return "fixed";
}

AppFont *appfont_open(Display *dpy, const char *name) {
	AppFont *af = calloc(1, sizeof(*af));
	if (!af) return NULL;
	af->dpy = dpy;
	if (!name || !*name) name = "fixed";

	af->core = XLoadQueryFont(dpy, name);
	if (af->core) {
		af->height = af->core->ascent + af->core->descent;
		af->ascent = af->core->ascent;
		return af;
	}

	int scr = DefaultScreen(dpy);
	af->xft = XftFontOpenName(dpy, scr, name);
	if (af->xft) {
		af->xft_mode = 1;
		af->height = af->xft->ascent + af->xft->descent;
		af->ascent = af->xft->ascent;
		af->draw = XftDrawCreate(dpy, DefaultRootWindow(dpy),
		                         DefaultVisual(dpy, scr),
		                         DefaultColormap(dpy, scr));
		return af;
	}

	af->core = XLoadQueryFont(dpy, "fixed");
	if (af->core) {
		af->height = af->core->ascent + af->core->descent;
		af->ascent = af->core->ascent;
	}
	return af;
}

int appfont_width(AppFont *af, const char *s, int len) {
	if (!af) return 0;
	if (af->xft_mode && af->xft) {
		XGlyphInfo ext;
		XftTextExtentsUtf8(af->dpy, af->xft, (const XftChar8 *)s, len, &ext);
		return ext.xOff;
	}
	if (af->core)
		return XTextWidth(af->core, s, len);
	return 0;
}

void appfont_draw(AppFont *af, Drawable d, GC gc, XftColor *fc,
                  unsigned long pixel, int x, int y, const char *s, int len) {
	if (!af) return;
	if (af->xft_mode && af->xft && af->draw && fc) {
		XftDrawChange(af->draw, d);
		XftDrawStringUtf8(af->draw, fc, af->xft, x, y, (const XftChar8 *)s, len);
		return;
	}
	if (af->core) {
		XSetFont(af->dpy, gc, af->core->fid);
		XSetForeground(af->dpy, gc, pixel);
		XDrawString(af->dpy, d, gc, x, y, s, len);
	}
}

int appfont_alloccolor(Display *dpy, const char *name, XftColor *fc) {
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	Visual *vis = DefaultVisual(dpy, DefaultScreen(dpy));
	if (!XParseColor(dpy, cmap, name, &xc)) return 0;
	XRenderColor rc = { xc.red, xc.green, xc.blue, 0xffff };
	return XftColorAllocValue(dpy, vis, cmap, &rc, fc);
}

void appfont_close(AppFont *af) {
	if (!af) return;
	if (af->core) XFreeFont(af->dpy, af->core);
	if (af->xft) {
		if (af->draw) {
			/* the drawable may already be destroyed; reset to root first */
			XftDrawChange(af->draw, DefaultRootWindow(af->dpy));
			XftDrawDestroy(af->draw);
		}
		XftFontClose(af->dpy, af->xft);
	}
	free(af);
}

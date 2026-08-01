#ifndef APPFONT_H
#define APPFONT_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

typedef struct {
	Display *dpy;
	XFontStruct *core;   /* valid when xft_mode == 0 */
	XftFont *xft;        /* valid when xft_mode == 1 */
	XftDraw *draw;
	int xft_mode;
	int height;
	int ascent;
} AppFont;

/* shared font name from ~/.config/bytewm/font (or "fixed") */
const char *appfont_sharedname(void);

/* open a font, trying core X first, then Xft, falling back to "fixed" */
AppFont *appfont_open(Display *dpy, const char *name);

/* text width in pixels */
int appfont_width(AppFont *af, const char *s, int len);

/* draw text; uses XftColor when xft, otherwise gc+pixel */
void appfont_draw(AppFont *af, Drawable d, GC gc, XftColor *fc,
                  unsigned long pixel, int x, int y, const char *s, int len);

/* allocate an XftColor from a color name */
int appfont_alloccolor(Display *dpy, const char *name, XftColor *fc);

void appfont_close(AppFont *af);

#endif

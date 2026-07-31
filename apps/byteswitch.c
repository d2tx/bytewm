#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_WINS 64
#define ITEM_H   30
#define WIN_W    500
#define BORDER_W 2

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int sw, sh, win_h;
static unsigned long c_bg, c_fg, c_hi, c_border, c_dim;

static Window windows[MAX_WINS];
static char *labels[MAX_WINS];
static int count, sel;

/* ignore transient X errors (windows destroyed while we run) */
static int
xerror_ignore(Display *d, XErrorEvent *e)
{
    (void)d;
    if (e->error_code == BadWindow || e->error_code == BadMatch)
        return 0;
    fprintf(stderr, "byteswitch: X error %d on request %u\n",
        e->error_code, e->request_code);
    return 0;
}

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

    const char *hdr = "s w i t c h   w i n d o w";
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
            XDrawString(dpy, win, gc, 16, y + 20, ">", 1);
        }
        XSetForeground(dpy, gc, i == sel ? c_hi : c_fg);
        XDrawString(dpy, win, gc, 32, y + 20, labels[i], strlen(labels[i]));
        y += ITEM_H;
    }

    const char *ftr = "tab:cycle  release-alt/enter:select  esc:cancel";
    tw = XTextWidth(xfont, ftr, strlen(ftr));
    XSetForeground(dpy, gc, c_dim);
    XDrawString(dpy, win, gc, (WIN_W - tw)/2, win_h - 14, ftr, strlen(ftr));

    XSync(dpy, False);
}

static char *get_window_title(Window w) {
    Atom netname = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom type; int fmt;
    unsigned long n, extra;
    unsigned char *prop = NULL;
    char *title = strdup("(untitled)");

    if (XGetWindowProperty(dpy, w, netname, 0, 256, False, utf8,
        &type, &fmt, &n, &extra, &prop) == Success && prop) {
        /* property data may not be NUL-terminated: bound the copy */
        unsigned long slen = 0;
        if (fmt == 8 && (type == utf8 || type == XA_STRING)) {
            while (slen < n && prop[slen]) slen++;
        }
        if (slen > 60) slen = 60;
        char *ntitle = malloc(slen + 1);
        if (ntitle) {
            memcpy(ntitle, prop, slen);
            ntitle[slen] = '\0';
            free(title);
            title = ntitle;
        }
        XFree(prop);
    }
    if (title && strlen(title) > 60) title[60] = '\0';
    return title;
}

static int is_ignored(Window w) {
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa)) return 1;
    if (wa.override_redirect) return 1;
    if (wa.map_state != IsViewable) return 1;
    return 0;
}

static void activate_window(Window w) {
    Atom netactive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    XEvent cev = {0};
    cev.xclient.type = ClientMessage;
    cev.xclient.window = w;
    cev.xclient.message_type = netactive;
    cev.xclient.format = 32;
    cev.xclient.data.l[0] = 1;          /* source: application */
    cev.xclient.data.l[1] = CurrentTime;
    cev.xclient.data.l[2] = 0;
    XSendEvent(dpy, root, False,
        SubstructureRedirectMask | SubstructureNotifyMask, &cev);
    XRaiseWindow(dpy, w);
    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
}


/*
 * When XkbSetDetectableAutoRepeat is not supported, fall back to
 * the classic trick: a KeyRelease followed immediately (same time,
 * same keycode) by a KeyPress is an auto-repeat pair, not a real
 * release.
 */
static int is_autorepeat_release(XEvent *ev) {
    if (ev->type != KeyRelease) return 0;
    if (XEventsQueued(dpy, QueuedAfterFlush) == 0) return 0;
    XEvent next;
    XPeekEvent(dpy, &next);
    if (next.type == KeyPress &&
        next.xkey.keycode == ev->xkey.keycode &&
        next.xkey.time == ev->xkey.time)
        return 1;
    return 0;
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    XSetErrorHandler(xerror_ignore);
    int scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    sw = DisplayWidth(dpy, scr);
    sh = DisplayHeight(dpy, scr);

    xfont = XLoadQueryFont(dpy, "fixed");
    if (!xfont) { XCloseDisplay(dpy); return 1; }

    c_bg     = getcol("#282828");
    c_fg     = getcol("#ebdbb2");
    c_hi     = getcol("#d65d0e");
    c_border = getcol("#689d6a");
    c_dim    = getcol("#a89984");

    /* collect client list */
    Atom netcl = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom type; int fmt;
    unsigned long n, extra;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, root, netcl, 0, 1024, False,
        XA_WINDOW, &type, &fmt, &n, &extra, &data) == Success &&
        data && type == XA_WINDOW && fmt == 32) {
        Window *wins = (Window *)data;
        for (unsigned long i = 0; i < n && count < MAX_WINS; i++) {
            if (is_ignored(wins[i])) continue;
            windows[count] = wins[i];
            labels[count] = get_window_title(wins[i]);
            count++;
        }
        XFree(data);
    }

    if (!count) { XFreeFont(dpy, xfont); XCloseDisplay(dpy); return 0; }

    /* find the currently active window so we can start on the NEXT one */
    Atom netactive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    Window active = None;
    unsigned char *adata = NULL;
    if (XGetWindowProperty(dpy, root, netactive, 0, 1, False, XA_WINDOW,
        &type, &fmt, &n, &extra, &adata) == Success && adata) {
        active = *(Window *)adata;
        XFree(adata);
    }
    sel = 0;
    if (active != None) {
        for (int i = 0; i < count; i++) {
            if (windows[i] == active) {
                sel = (i + 1) % count;   /* start after active window */
                break;
            }
        }
    }

    win_h = 56 + count * ITEM_H + 24;
    if (win_h < 200) win_h = 200;
    if (win_h > sh - 40) win_h = sh - 40;
    if (win_h < 100) win_h = 100;

    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixel  = c_bg,
        .event_mask        = ExposureMask | KeyPressMask | KeyReleaseMask
    };
    win = XCreateWindow(dpy, root,
        (sw - WIN_W) / 2, (sh - win_h) / 2,
        WIN_W, win_h, 0,
        DefaultDepth(dpy, scr), CopyFromParent,
        DefaultVisual(dpy, scr),
        CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
    gc = XCreateGC(dpy, win, 0, NULL);
    XSetFont(dpy, gc, xfont->fid);

    XMapRaised(dpy, win);
    XFlush(dpy);
    draw();                         /* draw UI immediately so the window
                                       isn't empty during the grab retry */

    /* Ask the server for real KeyRelease events only (no auto-repeat
       fakes).  Fall back to manual detection if unsupported. */
    Bool ar_supported = False;
    XkbSetDetectableAutoRepeat(dpy, True, &ar_supported);

    /* Grab the keyboard so we reliably receive every key event,
       regardless of what the WM does with focus. */
    XSync(dpy, False);
    int grabbed = 0;
    for (int tries = 0; tries < 300; tries++) {
        if (XGrabKeyboard(dpy, win, True,
            GrabModeAsync, GrabModeAsync,
            CurrentTime) == GrabSuccess) {
            grabbed = 1;
            break;
        }
        usleep(1000);
    }
    if (!grabbed)
        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

    /* Discard any events that arrived before the grab (e.g. the
       Alt+Tab keystroke that launched us). */
    XSync(dpy, True);

    draw();

    int done = 0, cancelled = 0;
    XEvent ev;
    while (!done) {
        XNextEvent(dpy, &ev);

        if (ev.type == Expose && ev.xexpose.count == 0) {
            draw();
            continue;
        }

        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Escape || ks == XK_q) {
                cancelled = 1;
                done = 1;
            } else if (ks == XK_Tab) {
                if (ev.xkey.state & ShiftMask)
                    sel = (sel - 1 + count) % count;
                else
                    sel = (sel + 1) % count;
                draw();
            } else if (ks == XK_j || ks == XK_Down) {
                sel = (sel + 1) % count;
                draw();
            } else if (ks == XK_k || ks == XK_Up) {
                sel = (sel - 1 + count) % count;
                draw();
            } else if (ks == XK_Return) {
                done = 1;
            }
            continue;
        }

        if (ev.type == KeyRelease) {
            /* Filter auto-repeat when XKB detectable autorepeat
               is unavailable. */
            if (!ar_supported && is_autorepeat_release(&ev))
                continue;

            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            /* Releasing Alt confirms the selection (classic
               Alt+Tab behaviour). */
            if (ks == XK_Alt_L || ks == XK_Alt_R)
                done = 1;
        }
    }

    /* --- teardown: make sure the window really disappears --- */
    if (grabbed)
        XUngrabKeyboard(dpy, CurrentTime);
    XUnmapWindow(dpy, win);
    XSync(dpy, False);          /* flush unmap+ungrab to server */

    if (!cancelled)
        activate_window(windows[sel]);

    XSync(dpy, False);          /* flush activate before destroy */
    XFreeFont(dpy, xfont);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XSync(dpy, False);          /* ensure server processes destroy */
    XCloseDisplay(dpy);
    for (int i = 0; i < count; i++) free(labels[i]);
    return 0;
}
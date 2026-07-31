#define _POSIX_C_SOURCE 200809L

/* See LICENSE file for copyright and license details.
 * bytewm - a minimal, gruvbox-themed tiling window manager for X11
 *
 * features:
 *  - master-stack and binary tree (bsp) tiling layouts
 *  - configurable gaps
 *  - scratchpad (dropdown terminal)
 *  - multi-tag workspaces
 *  - gruvbox dark color scheme
 *  - core X11 bitmap fonts (retro)
 *  - EWMH/NetWM hints
 */

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(c,t)          (isvisible((c), (t)))
#define MIN(a,b)                ((a) < (b) ? (a) : (b))
#define MAX(a,b)                ((a) > (b) ? (a) : (b))
#define LENGTH(x)               (sizeof(x) / sizeof(x[0]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(x)                ((x).w + 2 * borderpx)
#define HEIGHT(x)               ((x).h + 2 * borderpx)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(w)                (int)(textwidth((w)))
#define TAGW                    32

enum { CurNormal, CurResize, CurMove, CurLast };
enum { ColFG, ColBG, ColLast };
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetClientList,
       NetWMDesktop, NetNumberOfDesktops, NetCurrentDesktop, NetLast };
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast };
enum { SchemeNorm, SchemeSel, SchemeTag, SchemeUrg, SchemeLast };

/* Motif WM hints for borderless windows (MWM_DECOR_NONE) */
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_DECOR_NONE          0
typedef struct {
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
	long input_mode;
	unsigned long status;
} MotifWmHints;

/* type definitions */
typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

struct Layout;

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
} Rule;

typedef struct Client {
	struct Client *next;
	struct Client *snext;
	Window win;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags, oldtags;
	int isfixed, isfloating, isurgent, isfullscreen;
	int neverfocus;
	int oldstate;
	int autofloat;
	float saved_ratio;
	pid_t pid;
	struct Node *node;
	struct Monitor *mon;
} Client;

typedef struct Node {
	int isleaf;
	int dir;
	double ratio;
	Client *client;
	struct Node *parent;
	struct Node *a, *b;
} Node;

typedef struct Monitor {
	struct Monitor *next;
	Window barwin;
	Client *clients;
	Client *sel;
	Node *root;
	int x, y, w, h;
	int mx, my, mw, mh;
	int wx, wy, ww, wh;
	int gappx, gappoh, gappoi;
	unsigned int tags, oldtags;
	int nmaster;
	int num;
	int by, bh;
	int topbar;
	float mfact;
	int layout;
	struct Layout *lt[2];
	int taglayout[8];
	struct Layout *taglt[8][2];
	Client *stack;
} Monitor;

typedef struct Layout {
	const char *name;
	void (*arrange)(Monitor *, int);
} Layout;

/* function prototypes */
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void autostart(void);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void clientmessage(XEvent *e);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static int countclients(Monitor *m);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static void die(const char *fmt, ...) __attribute__((noreturn));
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void drawtext(Pixmap pmap, int x, int y, XftColor *fg, unsigned long bg, const char *text, int w);
static void *ecalloc(size_t nmemb, size_t size);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c, int raise);
static void focusstack(const Arg *arg);
static Atom getatom(const char *name);
static unsigned long getcolor(const char *col);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void initfont(void);
static int isvisible(Client *c, unsigned int tag);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m, int n);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void movearrow(const Arg *arg);
static void propertynotify(XEvent *e);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizearrow(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static void scratchpadhide(void);
static void scratchpadshow(void);
static int sendevent(Client *c, Atom proto);
static void setcfact(const Arg *arg);
static void setborder(Client *c, int focused);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void showhide(Client *c);
static void sigchld(int unused);
static void sighup(int unused);
static void spawn(const Arg *arg);
static void swapclients(const Arg *arg);
static unsigned int textwidth(const char *s);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m, int n);
static void bsp(Monitor *m, int n);
static void bsp_arrange(Node *n, int x, int y, int w, int h, int gx, int goh, int goi);
static Node *bsp_build(Client **list, int start, int end, int depth);
static void bspnode_destroy(Node *n);
static void togglefloating(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void toggletag(const Arg *arg);
static void togglescratch(const Arg *arg);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebars(void);
static void updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void view(const Arg *arg);
static void viewprevtag(const Arg *arg);
static int tagidx(unsigned int tag);
static Client *wintoclient(Window w);
static int wintitlematch(Client *c, const char *title);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* globals */
Display *dpy;
Window root;
Monitor *mons = NULL, *selmon = NULL;
Cursor cursor[CurLast];
Atom netatom[NetLast], wmatom[WMLast];
int screen, sw, sh, bh;
unsigned int numlockmask = 0;
int (*xerrorxlib)(Display *, XErrorEvent *);
volatile sig_atomic_t running = 1;
static volatile sig_atomic_t restart = 0;
static char crash_logpath[512];
static int showdate = 0;
XftFont *xfont;
XFontStruct *xfont_core;
int xft_font;  /* 1 = using Xft, 0 = using core X11 font */

unsigned long normfg, normbg, selfg, selbg, tagfg, tagbg, urgfg, urgbg, unfgborder;
XftColor xft_normfg, xft_selfg, xft_tagfg, xft_urgfg;
XftColor xft_cpu, xft_mem, xft_temp, xft_gpu, xft_date, xft_seltag, xft_vol;
GC bargc;
static char status[512] = "";
static char status_cache[512] = "";

#include "config.h"

/* ---- utility functions ---- */

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if (!(p = calloc(nmemb, size)))
		die("bytewm: calloc failed\n");
	return p;
}

void
die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

int
wintitlematch(Client *c, const char *title)
{
	if (title == NULL) return 0;
	char buf[256];
	if (!gettextprop(c->win, netatom[NetWMName], buf, sizeof(buf)))
		return 0;
	return !strcmp(title, buf);
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	(void)dpy; (void)ee;
	return 0;
}

int
xerror(Display *dpy, XErrorEvent *ee)
{
	static time_t last_xerr = 0;
	time_t now = time(NULL);
	if (now != last_xerr) {
		last_xerr = now;
		char msg[256];
		XGetErrorText(dpy, ee->error_code, msg, sizeof(msg));
		char *home = getenv("HOME");
		char logpath[512];
		if (home)
			snprintf(logpath, sizeof(logpath),
			         "%s/.cache/bytewm/crash.log", home);
		else
			snprintf(logpath, sizeof(logpath), "/tmp/bytewm_crash.log");
		int fd = open(logpath,
		              O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
		if (fd >= 0) {
			dprintf(fd, "X error: %s (code %d) on 0x%lx req %d.%d\n",
			        msg, ee->error_code, ee->resourceid,
			        ee->request_code, ee->minor_code);
			close(fd);
		}
	}
	return 0;
}

int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	(void)dpy; (void)ee;
	die("bytewm: another window manager is already running\n");
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	XSelectInput(dpy, root, SubstructureRedirectMask);
	XSync(dpy, 0);
	XSetErrorHandler(xerrordummy);
}

Atom
getatom(const char *name)
{
	return XInternAtom(dpy, name, False);
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;
	XErrorHandler prev;

	if (!text || size == 0) return 0;
	text[0] = '\0';
	prev = XSetErrorHandler(xerrordummy);
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems) {
		XSync(dpy, False);
		XSetErrorHandler(prev);
		return 0;
	}
	XSync(dpy, False);
	XSetErrorHandler(prev);
	if (name.encoding == XA_STRING) {
		size_t len = MIN((size_t)name.nitems, (size_t)size - 1);
		memcpy(text, name.value, len);
		text[len] = '\0';
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success
	           && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		text[size - 1] = '\0';
		XFreeStringList(list);
	} else {
		text[0] = '\0';
	}
	XFree(name.value);
	return 1;
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols = NULL;
	int exists = 0;
	XEvent ev;
	XErrorHandler prev;

	prev = XSetErrorHandler(xerrordummy);
	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	XSync(dpy, False);
	XSetErrorHandler(prev);
	return exists;
}

/* ---- window / client management ---- */

Client *
wintoclient(Window w)
{
	for (Monitor *m = mons; m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c->win == w) return c;
	return NULL;
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = selmon;
	if (dir == -1) {
		for (m = mons; m && m->next; m = m->next)
			if (m->next == selmon) return m;
	} else if (dir == 1) {
		if (selmon->next) return selmon->next;
	}
	return mons;
}

int
isvisible(Client *c, unsigned int tag)
{
	if (!c || !c->win) return 0;
	return c->tags & tag;
}

void
updatenumlockmask(void)
{
	XModifierKeymap *modmap;
	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	if (!modmap)
		return;
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
			    == XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask |= (1 << i);
	XFreeModifiermap(modmap);
}

void
grabkeys(void)
{
	KeyCode code;
	unsigned int mods[] = { 0, LockMask, numlockmask, numlockmask | LockMask };
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int i = 0; i < LENGTH(keys); i++) {
		if ((code = XKeysymToKeycode(dpy, keys[i].keysym))) {
			for (int j = 0; j < LENGTH(mods); j++)
				XGrabKey(dpy, code, keys[i].mod | mods[j], root, True,
					GrabModeAsync, GrabModeAsync);
		}
	}
}

void
grabbuttons(Client *c, int focused)
{
	unsigned int mods[] = { 0, LockMask, numlockmask, numlockmask | LockMask };
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (focused)
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	else
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			BUTTONMASK, GrabModeSync, GrabModeAsync, None, None);
	for (int i = 0; i < LENGTH(buttons); i++) {
		if (buttons[i].func && buttons[i].button <= Button5) {
			for (int j = 0; j < LENGTH(mods); j++)
				XGrabButton(dpy, buttons[i].button, buttons[i].mod | mods[j], c->win, False,
					BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
		}
	}
}

void
setfocus(Client *c)
{
	if (!c) return;
	if (c->isfullscreen) {
		XWindowChanges wc = {
			.x = c->mon->mx, .y = c->mon->my,
			.width = c->mon->mw, .height = c->mon->mh,
			.border_width = c->bw
		};
		c->x = wc.x; c->y = wc.y;
		c->w = wc.width; c->h = wc.height;
		XConfigureWindow(dpy, c->win,
			CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
		XSync(dpy, False);
	}
	XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&c->win, 1);
	sendevent(c, wmatom[WMTakeFocus]);
}

static void
setborder(Client *c, int focused)
{
	XSetWindowBorder(dpy, c->win,
		focused ? selbg : unfgborder);
}

void
focus(Client *c, int raise)
{
	if (!c) {
		for (c = selmon->stack; c && !ISVISIBLE(c, selmon->tags); c = c->snext);
		if (!c) {
			selmon->sel = NULL;
			XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
			XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
			drawbars();
			return;
		}
	}
	if (!ISVISIBLE(c, c->mon->tags)) return;
	Monitor *m = c->mon;
	if (selmon != m) {
		if (selmon && selmon->sel)
			setborder(selmon->sel, 0);
		selmon = m;
		drawbars();
	}
	if (m->sel && m->sel != c)
		setborder(m->sel, 0);
	if (m->sel != c) {
		detachstack(c);
		attachstack(c);
		m->sel = c;
		c->isurgent = 0;
		drawbars();
	}
	setborder(c, 1);
	grabbuttons(c, 1);
	if (raise) {
		restack(m);
	}
	setfocus(c);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
detach(Client *c)
{
	Monitor *m = c->mon;
	Client **tp;
	for (tp = &m->clients; *tp && *tp != c; tp = &(*tp)->next);
	if (*tp)
		*tp = c->next;
	c->next = NULL;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
detachstack(Client *c)
{
	Monitor *mon = c->mon;
	Client **tp;
	for (tp = &mon->stack; *tp && *tp != c; tp = &(*tp)->snext);
	if (*tp)
		*tp = c->snext;
	c->snext = NULL;

	if (mon->sel == c) {
		Client *t;
		for (t = mon->stack; t && !ISVISIBLE(t, mon->tags); t = t->snext);
		mon->sel = t;
	}
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;
	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		size.flags = 0;
	if (size.flags & PSize) {
		c->w = size.width;
		c->h = size.height;
	}
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else {
		c->basew = c->baseh = 0;
	}
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else {
		c->incw = c->inch = 0;
	}
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else {
		c->maxw = c->maxh = 0;
	}
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else {
		c->minw = c->minh = 2;
	}
	if (size.flags & PPosition)
		c->isfixed = 1;
}

void
applyrules(Client *c)
{
	const char *cls, *inst;
	XClassHint ch;
	cls = inst = NULL;
	if (XGetClassHint(dpy, c->win, &ch)) {
		cls = ch.res_class;
		inst = ch.res_name;
	}
	for (int i = 0; i < LENGTH(rules); i++) {
		if ((!rules[i].title || wintitlematch(c, rules[i].title))
		    && (!rules[i].class || (cls && !strcmp(rules[i].class, cls)))
		    && (!rules[i].instance || (inst && !strcmp(rules[i].instance, inst)))) {
			if (rules[i].isfloating)
				c->isfloating = 1;
			c->tags |= rules[i].tags;
		}
	}
	if (cls) XFree(ch.res_class);
	if (inst) XFree(ch.res_name);
	if (!c->tags) c->tags = selmon->tags;
}

int
countclients(Monitor *m)
{
	int n = 0;
	for (Client *c = m->clients; c; c = c->next)
		if (ISVISIBLE(c, m->tags) && !c->isfloating && !c->isfullscreen)
			n++;
	return n;
}

void
showhide(Client *c)
{
	for (Client *walk = c; walk; walk = walk->snext) {
		if (ISVISIBLE(walk, walk->mon->tags))
			XMoveWindow(dpy, walk->win, walk->x, walk->y);
		else
			XMoveWindow(dpy, walk->win, WIDTH(*walk) * -2, walk->y);
	}
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
	wc.x = x;
	wc.y = y;
	wc.width = MAX(1, w);
	wc.height = MAX(1, h);
	wc.border_width = c->bw;

	c->oldx = c->x; c->x = wc.x;
	c->oldy = c->y; c->y = wc.y;
	c->oldw = c->w; c->w = wc.width;
	c->oldh = c->h; c->h = wc.height;

	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	XSync(dpy, False);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (interact) {
		if (c->minw > 0 && w < c->minw) w = c->minw;
		if (c->minh > 0 && h < c->minh) h = c->minh;
		if (c->maxw > 0 && w > c->maxw) w = c->maxw;
		if (c->maxh > 0 && h > c->maxh) h = c->maxh;
		if (c->incw) w = c->basew + ((w - c->basew) / c->incw) * c->incw;
		if (c->inch) h = c->baseh + ((h - c->baseh) / c->inch) * c->inch;
	}
	resizeclient(c, x, y, w, h);
}

void
restack(Monitor *m)
{
	if (!m->sel) return;

	if (m->lt[m->layout]->arrange && m->barwin) {
		XWindowChanges wc = { .stack_mode = Below, .sibling = m->barwin };
		for (Client *c = m->stack; c; c = c->snext)
			if (ISVISIBLE(c, m->tags) && c != m->sel)
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
	}
	XRaiseWindow(dpy, m->sel->win);
}

void
arrange(Monitor *m)
{
	if (m) {
		XSetErrorHandler(xerrordummy);
		showhide(m->stack);
		if (m->lt[m->layout]->arrange)
			m->lt[m->layout]->arrange(m, countclients(m));
		restack(m);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
	}
}

/* ---- layouts ---- */

void
tile(Monitor *m, int n)
{
	if (n == 0) return;

	int gapx = m->gappx;
	int goh = m->gappoh;
	int goi = m->gappoi;

	int mx = m->wx + goh;
	int my = m->wy + goh;
	int mw = m->ww - 2 * goh;
	int mh = m->wh - 2 * goh;

	if (n == 1) {
		for (Client *c = m->clients; c; c = c->next) {
			if (!ISVISIBLE(c, m->tags) || c->isfloating || c->isfullscreen) continue;
			resize(c, mx, my, mw - 2 * borderpx, mh - 2 * borderpx, 0);
		}
		return;
	}

	int master_n = MIN(n, m->nmaster);
	int stack_n = n - master_n;
	int mw_calc = master_n > 0 ? (int)(mw * m->mfact) - (master_n - 1) * gapx : 0;

	int i;
	Client *c;

	/* master area */
	for (i = 0, c = m->clients; c; c = c->next) {
		if (!ISVISIBLE(c, m->tags) || c->isfloating || c->isfullscreen) continue;
		if (i < master_n) {
			int mh_each = (mh - (master_n - 1) * gapx) / master_n;
			resize(c, mx, my + i * (mh_each + gapx),
				mw_calc - 2 * borderpx,
				mh_each - 2 * borderpx, 0);
		}
		i++;
	}

	/* stack area */
	if (stack_n > 0) {
		int sx = mx + (master_n > 0 ? mw_calc + gapx : 0);
		int sw = mw - (master_n > 0 ? mw_calc + gapx : 0) - (master_n > 0 ? goi : 0);
		int sh_each = (mh - (stack_n - 1) * gapx) / stack_n;

		for (i = 0, c = m->clients; c; c = c->next) {
			if (!ISVISIBLE(c, m->tags) || c->isfloating || c->isfullscreen) continue;
			if (i >= master_n) {
				int idx = i - master_n;
				resize(c, sx + (master_n > 0 ? goi : 0),
					my + idx * (sh_each + gapx),
					sw - (master_n > 0 ? goi : 0) - 2 * borderpx,
					sh_each - 2 * borderpx, 0);
			}
			i++;
		}
	}
}

void
bsp_arrange(Node *n, int x, int y, int w, int h, int gx, int goh, int goi)
{
	if (!n) return;

	if (n->isleaf && n->client) {
		Client *c = n->client;
		if (!c->isfloating && !c->isfullscreen) {
			int cw = MAX(1, w - 2 * goh - 2 * borderpx);
			int ch = MAX(1, h - 2 * goh - 2 * borderpx);
			resize(c, x + goh, y + goh, cw, ch, 0);
		}
		return;
	}

	if (!n->isleaf && n->a && n->b) {
		double ratio = n->ratio;
		if (n->dir == 0) {
			int sw = MAX(1, (int)((w - goi - 2 * goh) * ratio));
			bsp_arrange(n->a, x, y, sw + goh, h, gx, goh, goi);
			bsp_arrange(n->b, x + sw + goi + goh, y,
			           MAX(1, w - sw - goi - goh), h, gx, goh, goi);
		} else {
			int sh = MAX(1, (int)((h - goi - 2 * goh) * ratio));
			bsp_arrange(n->a, x, y, w, sh + goh, gx, goh, goi);
			bsp_arrange(n->b, x, y + sh + goi + goh, w,
			           MAX(1, h - sh - goi - goh), gx, goh, goi);
		}
	}
}

static Node *
bsp_build(Client **list, int start, int end, int depth)
{
	if (start > end) return NULL;
	if (start == end) {
		Node *n = ecalloc(1, sizeof(Node));
		n->isleaf = 1;
		n->client = list[start];
		n->ratio = 0.5;
		n->parent = NULL;
		n->a = n->b = NULL;
		list[start]->node = n;
		return n;
	}
	int mid = (start + end) / 2;
	Node *n = ecalloc(1, sizeof(Node));
	n->isleaf = 0;
	n->dir = depth % 2;
	n->ratio = 0.5;
	n->parent = NULL;
	n->a = bsp_build(list, start, mid, depth + 1);
	n->b = bsp_build(list, mid + 1, end, depth + 1);
	if (n->a) n->a->parent = n;
	if (n->b) n->b->parent = n;
	return n;
}

void
bsp(Monitor *m, int n)
{
	/* count visible non-floating clients */
	int nclients = 0;
	for (Client *c = m->clients; c; c = c->next)
		if (ISVISIBLE(c, m->tags) && !c->isfloating && !c->isfullscreen)
			nclients++;
	if (nclients == 0) {
		if (m->root) {
			for (Client *c = m->clients; c; c = c->next)
				c->node = NULL;
			bspnode_destroy(m->root);
			m->root = NULL;
		}
		return;
	}

	/* collect visible non-floating clients */
	Client **list = ecalloc(nclients, sizeof(Client *));
	{
		int i = 0;
		for (Client *c = m->clients; c; c = c->next)
			if (ISVISIBLE(c, m->tags) && !c->isfloating && !c->isfullscreen)
				list[i++] = c;
	}

	/* save per-client ratios from old tree */
	for (Client *c = m->clients; c; c = c->next)
		if (ISVISIBLE(c, m->tags) && !c->isfloating && !c->isfullscreen
		    && c->node && c->node->parent)
			c->saved_ratio = c->node->parent->ratio;

	/* destroy old tree */
	if (m->root) {
		for (Client *c = m->clients; c; c = c->next)
			c->node = NULL;
		bspnode_destroy(m->root);
	}
	m->root = NULL;

	/* rebuild balanced tree with alternating splits */
	m->root = bsp_build(list, 0, nclients - 1, 0);

	/* restore saved ratios */
	for (Client *c = m->clients; c; c = c->next)
		if (ISVISIBLE(c, m->tags) && !c->isfloating && !c->isfullscreen
		    && c->node && c->node->parent && c->saved_ratio != 0.0)
			c->node->parent->ratio = c->saved_ratio;

	bsp_arrange(m->root, m->wx, m->wy, m->ww, m->wh,
	            m->gappx, m->gappoh, m->gappoi);
	free(list);
}

void
monocle(Monitor *m, int n)
{
	if (n == 0) return;
	int goh = m->gappoh;

	for (Client *c = m->clients; c; c = c->next) {
		if (!ISVISIBLE(c, m->tags)) continue;
		if (c->isfloating || c->isfullscreen) continue;
		resize(c, m->wx + goh, m->wy + goh,
		       m->ww - 2 * goh - 2 * borderpx,
		       m->wh - 2 * goh - 2 * borderpx, 0);
	}
}

/* ---- binary tree node management ---- */

void
bspnode_destroy(Node *n)
{
	if (!n) return;
	if (n->isleaf) {
		if (n->client) n->client->node = NULL;
		free(n);
		return;
	}
	if (n->a) bspnode_destroy(n->a);
	if (n->b) bspnode_destroy(n->b);
	free(n);
}

/* ---- bar ---- */

unsigned int
textwidth(const char *s)
{
	if (!s || !*s) return 0;
	if (xft_font) {
		if (!xfont) return 0;
		XGlyphInfo ext;
		XftTextExtentsUtf8(dpy, xfont, (XftChar8 *)s, (int)strlen(s), &ext);
		return (unsigned int)ext.xOff;
	} else {
		if (!xfont_core) return 0;
		return XTextWidth(xfont_core, s, (int)strlen(s));
	}
}

void
initfont(void)
{
	xfont_core = XLoadQueryFont(dpy, font);
	if (xfont_core) {
		xft_font = 0;
		bh = MAX(barheight, (unsigned int)(xfont_core->ascent + xfont_core->descent + 4));
		return;
	}
	xfont = XftFontOpenName(dpy, screen, font);
	if (xfont) {
		xft_font = 1;
		bh = MAX(barheight, (unsigned int)(xfont->ascent + xfont->descent + 4));
		return;
	}
	die("bytewm: could not load font '%s'\n", font);
}

unsigned long
getcolor(const char *colstr)
{
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;
	if (!XParseColor(dpy, cmap, colstr, &color))
		die("bytewm: could not parse color '%s'\n", colstr);
	if (!XAllocColor(dpy, cmap, &color))
		die("bytewm: could not allocate color '%s'\n", colstr);
	return color.pixel;
}

void
drawtext(Pixmap pmap, int x, int y, XftColor *fg, unsigned long bg, const char *text, int w)
{
	XSetForeground(dpy, bargc, bg);
	XFillRectangle(dpy, pmap, bargc, x, y, w, bh);

	int tx = x + 2;
	if (xft_font && xfont) {
		XftDraw *xftdraw = XftDrawCreate(dpy, pmap,
			DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
		if (xftdraw) {
			int ty = y + (bh - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;
			XftDrawStringUtf8(xftdraw, fg, xfont, tx, ty,
				(XftChar8 *)text, (int)strnlen(text, 256));
			XftDrawDestroy(xftdraw);
		}
	} else if (xfont_core) {
		XSetFont(dpy, bargc, xfont_core->fid);
		XSetForeground(dpy, bargc, fg->pixel);
		int ty = y + (bh - (xfont_core->ascent + xfont_core->descent)) / 2 + xfont_core->ascent;
		XDrawString(dpy, pmap, bargc, tx, ty, text, (int)strnlen(text, 256));
	}
}

void
drawbar(Monitor *m)
{
	if (!m->barwin || !showbar) return;

	int w = m->mw;
	int rx = 0;

	/* double-buffer: draw to pixmap then copy */
	Pixmap pmap = XCreatePixmap(dpy, m->barwin, w, bh, DefaultDepth(dpy, screen));

	/* background */
	XSetForeground(dpy, bargc, normbg);
	XFillRectangle(dpy, pmap, bargc, 0, 0, w, bh);

	/* tags */
	for (int i = 0; i < LENGTH(tags); i++) {
		int occupied = 0, urgent = 0;
		for (Client *c = m->clients; c; c = c->next) {
			if (c->tags & (1 << i)) {
				occupied = 1;
				if (c->isurgent) urgent = 1;
			}
		}
		int seltag = (m->tags & (1 << i)) != 0;
		char label[8];
		snprintf(label, sizeof(label), " %s ", tags[i]);
		unsigned long bg;
		XftColor *fg;
		if (seltag)       { fg = &xft_seltag;  bg = normbg; }
		else if (urgent)  { fg = &xft_urgfg; bg = urgbg; }
		else if (occupied){ fg = &xft_tagfg; bg = normbg; }
		else              { fg = &xft_normfg; bg = normbg; }
		int tw = textwidth(label) + 4;
		if (rx + tw > w) tw = w - rx;
		if (tw <= 0) break;
		drawtext(pmap, rx, 0, fg, bg, label, tw);
		rx += tw;
	}

	/* layout symbol */
	{
		const char *sym = m->lt[m->layout]->name;
		int lw = textwidth(sym) + 6;
		drawtext(pmap, rx + 2, 0, &xft_tagfg, normbg, sym, lw);
		rx += lw + 4;
	}

	/* window title */
	if (m->sel) {
		char winname[256];
		if (gettextprop(m->sel->win, netatom[NetWMName], winname, sizeof(winname))) {
			int avail = 140;
			if (avail > 20 && textwidth(winname) > avail) {
				int n = (int)strlen(winname);
				if (n > 252) n = 252;
				while (n > 4 && textwidth(winname) + 14 > avail) {
					winname[--n] = '\0';
				}
				strcpy(winname + n, "...");
			}
			drawtext(pmap, rx, 0, &xft_normfg, normbg, winname, avail);
		}
	}

	/* status modules (right side), time/date (center) */
	if (status[0]) {
		char buf[512];
		strncpy(buf, status, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		/* find last segment (time) */
		char *last_seg = NULL;
		{
			char *q = buf;
			while (*q) {
				char *s = strstr(q, " | ");
				if (!s) { last_seg = q; break; }
				q = s + 3;
			}
		}

		/* time/date in center */
		if (last_seg) {
			time_t t = time(NULL);
			struct tm *tm = localtime(&t);
			char timebuf[32], datebuf[32];
			strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", tm);
			strftime(timebuf, sizeof(timebuf), "%I:%M %p", tm);

			char *centext = showdate ? datebuf : timebuf;
			int ctw = textwidth(centext);
			int cmid = (w - ctw - 4) / 2;
			if (cmid < rx) cmid = rx;
			drawtext(pmap, cmid, 0, &xft_date, normbg, centext, ctw + 4);
		}

		/* right-side modules: render all segments with separators */
		int sepw = textwidth(" | ");
		int right_total = 0, nseg = 0;
		{
			char *q = buf;
			while (*q) {
				char *s = strstr(q, " | ");
				int len = s ? (int)(s - q) : (int)strlen(q);
				char sv = q[len]; q[len] = '\0';
				right_total += textwidth(q) + 4;
				if (s) right_total += sepw;
				q[len] = sv;
				nseg++;
				if (!s) break;
				q = s + 3;
			}
		}
		/* subtract last segment (time) from right side */
		if (last_seg && nseg > 0) {
			right_total -= textwidth(last_seg) + 4;
			if (nseg > 1) right_total -= sepw;
		}
		int pos = w - right_total - 6;
		if (pos < rx) pos = rx;

		char *p = buf;
		while (*p) {
			char *sep = strstr(p, " | ");
			int seg_len = sep ? (int)(sep - p) : (int)strlen(p);
			if (!sep && last_seg && p == last_seg) break; /* skip time */
			char saved = p[seg_len];
			p[seg_len] = '\0';
			int tw = textwidth(p);

			XftColor *fc = &xft_normfg;
			if (strstr(p, "CPU") && strstr(p, "°")) fc = &xft_temp;
			else if (strstr(p, "VOL")) fc = &xft_vol;
			else if (strstr(p, "GPU")) fc = &xft_gpu;
			else if (strstr(p, "CPU")) fc = &xft_cpu;
			else if (strstr(p, "MEM"))  fc = &xft_mem;
			else if (strstr(p, "°") || strstr(p, "C"))
			                            fc = &xft_temp;
			else if (strstr(p, "↑") || strstr(p, "↓"))
			                            fc = &xft_temp;
			else if (strstr(p, "-"))    fc = &xft_date;

			drawtext(pmap, pos, 0, fc, normbg, p, tw + 4);
			pos += tw + 2;
			p[seg_len] = saved;
			if (!sep) break;
			if (sep + 3 != last_seg) {
				drawtext(pmap, pos, 0, &xft_date, normbg, " | ", sepw);
				pos += sepw;
			}
			p = sep + 3;
		}
	}

	/* underline */
	XSetForeground(dpy, bargc, selbg);
	XFillRectangle(dpy, pmap, bargc, 0, bh - 2, w, 2);

	/* flip pixmap to window */
	XCopyArea(dpy, pmap, m->barwin, bargc, 0, 0, w, bh, 0, 0);
	XFreePixmap(dpy, pmap);
}

void
drawbars(void)
{
	for (Monitor *m = mons; m; m = m->next)
		drawbar(m);
}

void
updatebars(void)
{
	if (!showbar) return;

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = normbg,
		.border_pixel = 0,
		.colormap = DefaultColormap(dpy, screen),
		.event_mask = ButtonPressMask|ExposureMask
	};

	for (Monitor *m = mons; m; m = m->next) {
		if (m->barwin) continue;
		m->barwin = XCreateWindow(dpy, root,
			m->mx, m->topbar ? m->my : m->my + m->mh - bh,
			m->mw, bh, 0, DefaultDepth(dpy, screen),
			CopyFromParent, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask,
			&wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]);
		XMapRaised(dpy, m->barwin);
	}
}

/* ---- scratchpad ---- */

static Client *scratchpad = NULL;

void
scratchpadshow(void)
{
	if (scratchpad) {
		scratchpad->tags = selmon->tags;
		selmon->sel = scratchpad;
		focus(scratchpad, 1);
		arrange(selmon);
		XMapWindow(dpy, scratchpad->win);
		drawbars();
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)scratchpadcmd)[0], (char **)scratchpadcmd);
		_exit(1);
	} else if (pid < 0) {
		fprintf(stderr, "bytewm: fork failed: %s\n", strerror(errno));
	}
}

void
scratchpadhide(void)
{
	if (!scratchpad) return;
	XUnmapWindow(dpy, scratchpad->win);
	scratchpad->tags = 0;
	if (selmon->sel == scratchpad)
		focus(NULL, 0);
	arrange(selmon);
	drawbars();
}

void
togglescratch(const Arg *arg)
{
	(void)arg;
	if (scratchpad && ISVISIBLE(scratchpad, selmon->tags))
		scratchpadhide();
	else
		scratchpadshow();
}

/* ---- commands ---- */

void
spawn(const Arg *arg)
{
	pid_t pid = fork();
	if (pid == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "bytewm: execvp %s failed: %s\n",
			((char **)arg->v)[0], strerror(errno));
		_exit(1);
	} else if (pid < 0) {
		fprintf(stderr, "bytewm: fork failed: %s\n", strerror(errno));
	}
}

void
killclient(const Arg *arg)
{
	(void)arg;
	if (!selmon || !selmon->sel) return;
	Client *c = selmon->sel;
	if (!sendevent(c, wmatom[WMDelete]))
		XKillClient(dpy, c->win);
}

void
focusstack(const Arg *arg)
{
	if (!selmon || !selmon->sel) return;
	Client *c, *i;

	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c, selmon->tags); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c, selmon->tags); c = c->next);
	} else {
		/* find last visible before sel, or wrap to last visible overall */
		for (c = NULL, i = selmon->clients; i && i != selmon->sel; i = i->next)
			if (ISVISIBLE(i, selmon->tags))
				c = i;
		if (!c)
			for (i = selmon->sel->next; i; i = i->next)
				if (ISVISIBLE(i, selmon->tags))
					c = i;
	}
	if (c) {
		focus(c, 1);
		selmon->sel = c;
		arrange(selmon);
		drawbars();
	}
}

void
zoom(const Arg *arg)
{
	(void)arg;
	Client *c = selmon->sel;
	if (!c || !ISVISIBLE(c, selmon->tags) || c->isfloating) return;
	Client *next = c->next;
	if (!next || !ISVISIBLE(next, selmon->tags)) return;
	detach(c);
	c->next = selmon->clients;
	selmon->clients = c;
	focus(c, 1);
	arrange(selmon);
}

void
swapclients(const Arg *arg)
{
	if (!selmon || !selmon->sel) return;
	Client *c = selmon->sel;

	if (arg->i > 0) {
		Client *n;
		for (n = c->next; n && !ISVISIBLE(n, selmon->tags); n = n->next);
		if (!n) return;
		detach(c);
		c->next = n->next;
		n->next = c;
	} else {
		Client *p, *prev = NULL;
		for (p = selmon->clients; p && p != c; p = p->next)
			if (ISVISIBLE(p, selmon->tags))
				prev = p;
		if (!prev) return;
		detach(c);
		/* find slot before prev in modified list */
		if (selmon->clients == prev) {
			c->next = selmon->clients;
			selmon->clients = c;
		} else {
			Client *q;
			for (q = selmon->clients; q && q->next != prev; q = q->next);
			c->next = q->next;
			q->next = c;
		}
	}
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w / 2, c->h / 2);
	XSync(dpy, False);
	focus(c, 1);
	arrange(selmon);
	drawbars();
}

static void
setwmdesktop(Client *c)
{
	if (!c) return;
	unsigned long idx = (c->tags == TAGMASK) ? 0xFFFFFFFFUL : tagidx(c->tags);
	XChangeProperty(dpy, c->win, netatom[NetWMDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&idx, 1);
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		setwmdesktop(selmon->sel);
		focus(NULL, 0);
		arrange(selmon);
		drawbars();
	}
}

void
toggletag(const Arg *arg)
{
	if (!selmon || !selmon->sel) return;
	Client *c = selmon->sel;
	unsigned int mask = arg->ui & TAGMASK;

	if (mask == TAGMASK) {
		if (c->tags == TAGMASK) {
			c->tags = c->oldtags;
		} else {
			c->oldtags = c->tags;
			c->tags = TAGMASK;
		}
	} else {
		unsigned int newtags = c->tags ^ mask;
		if (newtags) {
			c->tags = newtags;
		} else {
			return;
		}
	}
	setwmdesktop(c);
	focus(NULL, 0);
	arrange(selmon);
	drawbars();
}

static int
tagidx(unsigned int tag)
{
	int i;
	for (i = 0; i < LENGTH(tags); i++)
		if (tag & (1 << i))
			return i;
	return 0;
}

void
view(const Arg *arg)
{
	unsigned int tag = arg->ui & TAGMASK;
	if (!selmon) return;
	if (tag && tag != selmon->tags) {
		selmon->oldtags = selmon->tags;
		selmon->tags = tag;

		if (tag != TAGMASK) {
			int oi = tagidx(selmon->oldtags);
			selmon->taglayout[oi] = selmon->layout;
			selmon->taglt[oi][0] = selmon->lt[0];
			selmon->taglt[oi][1] = selmon->lt[1];

			int ni = tagidx(selmon->tags);
			selmon->layout = selmon->taglayout[ni];
			selmon->lt[0] = selmon->taglt[ni][0];
			selmon->lt[1] = selmon->taglt[ni][1];
		}

		focus(NULL, 1);
		arrange(selmon);
		drawbars();

		if (selmon->tags != TAGMASK) {
			unsigned long cdt = tagidx(selmon->tags);
			XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)&cdt, 1);
		}
	}
}

void
viewprevtag(const Arg *arg)
{
	(void)arg;
	unsigned int tag = selmon->oldtags ? selmon->oldtags : 0;
	if (tag)
		view(&(Arg){.ui = tag});
}

void
togglefloating(const Arg *arg)
{
	(void)arg;
	if (!selmon->sel) return;
	Client *c = selmon->sel;
	c->isfloating = !c->isfloating;
	c->autofloat = 0;
	c->bw = c->isfloating ? 1 : borderpx;
	XSetWindowBorderWidth(dpy, c->win, c->bw);
	if (c->isfloating) {
		c->x = selmon->wx + selmon->ww / 4;
		c->y = selmon->wy + selmon->wh / 4;
		c->w = selmon->ww / 2;
		c->h = selmon->wh / 2;
		resize(c, c->x, c->y, c->w, c->h, 0);
	}
	arrange(selmon);
	drawbars();
}

void
togglefullscr(const Arg *arg)
{
	(void)arg;
	if (selmon->sel)
		setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || !selmon) return;
	Layout *lt = (Layout *)arg->v;
	if (lt == selmon->lt[selmon->layout]) {
		selmon->layout = (selmon->layout + 1) % 2;
		selmon->lt[selmon->layout] = lt;
	} else {
		selmon->lt[selmon->layout] = lt;
	}
	if (lt->arrange) {
		for (Client *c = selmon->clients; c; c = c->next)
			if (c->autofloat) {
				c->isfloating = 0;
				c->autofloat = 0;
				c->bw = borderpx;
				XSetWindowBorderWidth(dpy, c->win, c->bw);
			}
		arrange(selmon);
	}
	drawbars();
}

void
setmfact(const Arg *arg)
{
	float f = arg->f + selmon->mfact;
	if (f < 0.1 || f > 0.9) return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setcfact(const Arg *arg)
{
	/* only works in BSP layout */
	if (!selmon || !selmon->lt[selmon->layout]->arrange
	    || selmon->lt[selmon->layout]->arrange != bsp)
		return;
	Node *n = (selmon->sel && selmon->sel->node)
	          ? selmon->sel->node->parent
	          : NULL;
	if (!n || n->isleaf) n = selmon->root;
	if (!n || n->isleaf) return;
	float f = arg->f + n->ratio;
	if (f < 0.1 || f > 0.9) return;
	n->ratio = f;
	if (selmon->sel) selmon->sel->saved_ratio = f;
	/* arrange in-place to preserve modified ratios */
	XSetErrorHandler(xerrordummy);
	if (selmon->lt[selmon->layout]->arrange)
		bsp_arrange(selmon->root, selmon->wx, selmon->wy,
		            selmon->ww, selmon->wh,
		            selmon->gappx, selmon->gappoh, selmon->gappoi);
	restack(selmon);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	drawbars();
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(1, selmon->nmaster + arg->i);
	arrange(selmon);
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel) return;
	Monitor *m = dirtomon(arg->i);
	if (m) {
		Client *c = selmon->sel;
		c->node = NULL;  /* arrange() will rebuild the BSP tree */
		detach(c);
		detachstack(c);
		c->mon = m;
		attach(c);
		attachstack(c);
		arrange(selmon);
		arrange(m);
		/* reconcile focus/border state: tagmon reorders the client
		   list, so the previously-selected window's border must be
		   cleared and c must become the selected client again,
		   otherwise multiple windows end up with the focused border */
		focus(c, 1);
		drawbars();
	}
}

/* ---- manage / unmanage windows ---- */

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->x = wa->x;
	c->y = wa->y;
	c->w = wa->width;
	c->h = wa->height;
	c->oldw = wa->width;
	c->oldh = wa->height;
	c->oldbw = wa->border_width;
	c->bw = borderpx;
	c->mon = selmon;
	c->node = NULL;
	c->isfloating = 0;

	updatesizehints(c);
	applyrules(c);

	c->oldtags = c->tags;

	/* detect transient windows (dialogs) and float them */
	{
		Window trans;
		if (XGetTransientForHint(dpy, w, &trans) && trans != None) {
			c->isfloating = 1;
		}
	}

	/* check for MWM hints (borderless windows like feh fullscreen) */
	{
		Atom mwm = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
		unsigned char *data = NULL;
		unsigned long n;
		Atom real;
		int fmt;
		if (mwm != None && XGetWindowProperty(dpy, w, mwm, 0L, sizeof(MotifWmHints)/4,
		    False, AnyPropertyType, &real, &fmt, &n, &(unsigned long){0},
		    &data) == Success && data && n >= 4) {
			MotifWmHints *hints = (MotifWmHints *)data;
			if (hints->flags & MWM_HINTS_DECORATIONS
			    && hints->decorations == MWM_DECOR_NONE) {
				c->isfloating = 1;
				c->bw = 0;
			}
			XFree(data);
		}
	}

	/* in floating layout, auto-float new windows and center them */
	if (!selmon->lt[selmon->layout]->arrange) {
		c->isfloating = 1;
		c->autofloat = 1;
		c->bw = 1;
		c->x = selmon->wx + (selmon->ww - c->w) / 2;
		c->y = selmon->wy + (selmon->wh - c->h) / 3;
		c->x = MAX(selmon->wx, c->x);
		c->y = MAX(selmon->wy, c->y);
		resizeclient(c, c->x, c->y, c->w, c->h);
	}

	/* get PID */
	Atom pidatom = XInternAtom(dpy, "_NET_WM_PID", False);
	if (pidatom != None) {
		unsigned long n;
		Atom real;
		int fmt;
		unsigned char *p = NULL;
		if (XGetWindowProperty(dpy, w, pidatom, 0L, 1L, False,
			XA_CARDINAL, &real, &fmt, &n, &(unsigned long){0},
			&p) == Success && p && n >= 1 && fmt == 32) {
			c->pid = *(pid_t *)p;
			XFree(p);
		} else if (p) {
			XFree(p);
		}
	}

	attach(c);
	attachstack(c);

	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		PropModeAppend, (unsigned char *)&w, 1);
	XSelectInput(dpy, w,
		EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);

	/* check for scratchpad (st -c scratchpad sets res_name) */
	XClassHint ch;
	if (XGetClassHint(dpy, w, &ch)) {
		if ((ch.res_class && strcmp(ch.res_class, "scratchpad") == 0)
		    || (ch.res_name && strcmp(ch.res_name, "scratchpad") == 0)) {
			if (scratchpad && scratchpad != c) {
				Client *old = scratchpad;
				scratchpad = NULL;
				unmanage(old, 0);
			}
			scratchpad = c;
			scratchpad->tags = 0;
			scratchpad->isfloating = 1;
			scratchpad->bw = 2;
			scratchpad->x = selmon->wx + selmon->ww * 0.39;
			scratchpad->y = selmon->wy + selmon->wh * 0.15;
			scratchpad->w = selmon->ww * 0.22;
			scratchpad->h = selmon->wh * 0.2;
			resizeclient(c, scratchpad->x, scratchpad->y,
			             scratchpad->w, scratchpad->h);
		}
		if (ch.res_class) XFree(ch.res_class);
		if (ch.res_name) XFree(ch.res_name);
	}

	setwmdesktop(c);

	/* map and focus the new window */
	setborder(c, c->mon == selmon && ISVISIBLE(c, selmon->tags));
	XMapWindow(dpy, w);
	XSync(dpy, False);
	if (c->mon == selmon && ISVISIBLE(c, selmon->tags))
		focus(c, 1);
	arrange(c->mon);
	drawbars();
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;

	if (c == scratchpad) scratchpad = NULL;

	detach(c);
	detachstack(c);

	/* rebuild client list after removing this window */
	{
		int n = 0;
		for (Monitor *om = mons; om; om = om->next)
			for (Client *walk = om->clients; walk; walk = walk->next)
				n++;
		Window *wins = ecalloc(n, sizeof(Window));
		int i = 0;
		for (Monitor *om = mons; om; om = om->next)
			for (Client *walk = om->clients; walk; walk = walk->next)
				wins[i++] = walk->win;
		XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)wins, n);
		free(wins);
	}

	/* remove from bsp tree */
	if (c->node) {
		Node *n = c->node;
		Node *parent = n->parent;
		n->client = NULL;
		if (parent) {
			Node *sibling = parent->a == n ? parent->b : parent->a;
			if (sibling) {
				sibling->parent = parent->parent;
				if (parent->a == sibling) parent->a = NULL;
				else parent->b = NULL;
			}
			if (sibling) {
				if (parent->parent) {
					if (parent->parent->a == parent)
						parent->parent->a = sibling;
					else
						parent->parent->b = sibling;
				} else {
					m->root = sibling;
				}
			} else {
				if (parent->parent) {
					if (parent->parent->a == parent)
						parent->parent->a = NULL;
					else
						parent->parent->b = NULL;
				} else {
					m->root = NULL;
				}
			}
			bspnode_destroy(parent);
		} else {
			if (m->root == n) m->root = NULL;
			bspnode_destroy(n);
		}
		c->node = NULL;
	}

	if (!destroyed) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth,
			&(XWindowChanges){.border_width = c->oldbw});
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);

	Client *t;
	for (t = m->stack; t && !ISVISIBLE(t, m->tags); t = t->snext);
	m->sel = t;
	if (m->sel)
		focus(m->sel, 1);
	else if (m == selmon)
		focus(NULL, 0);
	arrange(m);
	drawbars();
}

/* ---- event handlers ---- */

void
buttonpress(XEvent *e)
{
	XButtonPressedEvent *ev = &e->xbutton;

	/* bar clicks */
	for (Monitor *m = mons; m; m = m->next) {
		if (m->barwin != ev->window) continue;
		int x = 0;
		/* tags (must match drawbar layout) */
		for (int i = 0; i < LENGTH(tags); i++) {
			char label[8];
			snprintf(label, sizeof(label), " %s ", tags[i]);
			int tw = textwidth(label) + 4;
			x += tw;
			if (ev->x < x) {
				if (ev->state & ShiftMask)
					tag(&(Arg){.ui = 1 << i});
				else
					view(&(Arg){.ui = 1 << i});
				return;
			}
		}
		/* click on right side (status area) — toggle time/date */
		showdate = !showdate;
		drawbar(m);
		return;
	}

	Client *c = wintoclient(ev->window);
	if (!c) return;
	focus(c, 1);
	selmon = c->mon;

	/* dispatch from buttons[] array */
	for (int i = 0; i < LENGTH(buttons); i++) {
		if (buttons[i].func && buttons[i].button == ev->button
		    && CLEANMASK(buttons[i].mod) == CLEANMASK(ev->state)) {
			buttons[i].func(&(buttons[i].arg));
			return;
		}
	}

	/* pass unhandled clicks through to the client */
	XAllowEvents(dpy, ReplayPointer, CurrentTime);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *ev = &e->xclient;
	Client *c;

	/* handle _NET_ACTIVE_WINDOW from byteswitch */
	if (ev->message_type == netatom[NetActiveWindow]) {
		c = wintoclient(ev->window);
		if (c) {
			if (!(c->tags & c->mon->tags)) {
				c->mon->tags = c->tags;
				focus(NULL, 0);
				arrange(c->mon);
			}
			focus(c, 1);
		}
		return;
	}

	c = wintoclient(ev->window);
	if (!c) return;

	if (ev->message_type == netatom[NetWMState]) {
		if (ev->data.l[1] == (long)netatom[NetWMFullscreen]
		    || ev->data.l[2] == (long)netatom[NetWMFullscreen]) {
			if (ev->data.l[0] == 0)
				setfullscreen(c, 0);
			else if (ev->data.l[0] == 1)
				setfullscreen(c, 1);
			else if (ev->data.l[0] == 2)
				setfullscreen(c, !c->isfullscreen);
		}
	}
}

void
configurenotify(XEvent *e)
{
	XConfigureEvent *ev = &e->xconfigure;
	if (ev->window == root) {
		sw = ev->width;
		sh = ev->height;
		updategeom();
	}
}

void
configurerequest(XEvent *e)
{
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	Client *c = wintoclient(ev->window);
	XWindowChanges wc;
	wc.x = ev->x; wc.y = ev->y;
	wc.width = ev->width; wc.height = ev->height;
	wc.border_width = ev->border_width;
	wc.sibling = ev->above;
	wc.stack_mode = ev->detail;

	if (c) {
		if (c->isfloating || !c->mon->lt[c->mon->layout]->arrange) {
			c->x = wc.x; c->y = wc.y;
			c->w = wc.width; c->h = wc.height;
			c->oldw = wc.width; c->oldh = wc.height;
			XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
		} else {
			XConfigureEvent ce = {
				.type = ConfigureNotify,
				.display = dpy,
				.event = c->win,
				.window = c->win,
				.x = c->x,
				.y = c->y,
				.width = c->w,
				.height = c->h,
				.border_width = c->bw,
				.above = None,
				.override_redirect = False,
			};
			XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
		}
	} else {
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

void
destroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	Client *c = wintoclient(ev->window);
	if (c)
		unmanage(c, 1);
}

void
enternotify(XEvent *e)
{
	static Time lasttime = 0;
	XCrossingEvent *ev = &e->xcrossing;
	if (ev->mode != NotifyNormal || ev->detail == NotifyInferior
	    || ev->detail == NotifyNonlinearVirtual)
		return;
	if (ev->time == lasttime)
		return;
	Client *c = wintoclient(ev->window);
	if (c && c != selmon->sel) {
		focus(c, 0);
		selmon = c->mon;
		drawbars();
		lasttime = ev->time;
	}
}

void
 expose(XEvent *e)
{
	if (e->xexpose.count == 0) {
		for (Monitor *m = mons; m; m = m->next)
			if (m->barwin == e->xexpose.window)
				drawbar(m);
	}
}

void
keypress(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym ksym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);
	for (int i = 0; i < LENGTH(keys); i++) {
		if (ksym == keys[i].keysym
		    && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		    && keys[i].func) {
			keys[i].func(&(keys[i].arg));
			return;
		}
	}
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;
	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	XMapRequestEvent *ev = &e->xmaprequest;
	if (!wintoclient(ev->window)) {
		XWindowAttributes wa;
		if (XGetWindowAttributes(dpy, ev->window, &wa))
			manage(ev->window, &wa);
	}
}

void
propertynotify(XEvent *e)
{
	XPropertyEvent *ev = &e->xproperty;
	Client *c = wintoclient(ev->window);
	if (!c) return;

	XSetErrorHandler(xerrordummy);
	if (ev->atom == XA_WM_TRANSIENT_FOR) {
		Window trans;
		if (XGetTransientForHint(dpy, c->win, &trans) && trans != None) {
			if (!c->isfloating) {
				c->isfloating = 1;
				c->bw = borderpx;
				XSetWindowBorderWidth(dpy, c->win, c->bw);
			}
			arrange(c->mon);
		}
	}
	if (ev->atom == netatom[NetWMName]) {
		unsigned int oldtags = c->tags;
		int oldfloating = c->isfloating;
		applyrules(c);
		if (c->tags != oldtags || c->isfloating != oldfloating) {
			setwmdesktop(c);
			arrange(c->mon);
		}
		drawbars();
	}
	if (ev->atom == XA_WM_NORMAL_HINTS)
		updatesizehints(c);
	{
		Atom mwm = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
		if (mwm != None && ev->atom == mwm) {
			unsigned char *data = NULL;
			unsigned long n;
			Atom real;
			int fmt;
			if (XGetWindowProperty(dpy, c->win, mwm, 0L, sizeof(MotifWmHints)/4,
			    False, AnyPropertyType, &real, &fmt, &n, &(unsigned long){0},
			    &data) == Success && data && n >= 4) {
				MotifWmHints *hints = (MotifWmHints *)data;
				if (hints->flags & MWM_HINTS_DECORATIONS
				    && hints->decorations == MWM_DECOR_NONE) {
					if (!c->isfloating) {
						c->isfloating = 1;
						c->bw = 0;
						XSetWindowBorderWidth(dpy, c->win, 0);
						arrange(c->mon);
						drawbars();
					}
				} else {
					if (c->isfloating && c->bw == 0) {
						c->bw = borderpx;
						XSetWindowBorderWidth(dpy, c->win, c->bw);
						arrange(c->mon);
						drawbars();
					}
				}
				XFree(data);
			}
		}
	}
	if (ev->atom == XA_WM_HINTS) {
		XWMHints *wmh = XGetWMHints(dpy, c->win);
		if (wmh) {
			if (wmh->flags & XUrgencyHint) {
				c->isurgent = 1;
				drawbars();
			}
			XFree(wmh);
		}
	}
	XSync(dpy, False);
	XSetErrorHandler(xerror);
}

void
motionnotify(XEvent *e)
{
	(void)e;
}

void
movemouse(const Arg *arg)
{
	(void)arg;
	if (!selmon->sel) return;
	Client *c = selmon->sel;
	if (!c->isfloating) { togglefloating(NULL); c = selmon->sel; if (!c) return; }
	int ox, oy, nx, ny;
	int di;
	unsigned int dui;
	Window dummy;
	XEvent ev;

	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove], CurrentTime) != GrabSuccess) return;

	XQueryPointer(dpy, root, &dummy, &dummy, &nx, &ny, &di, &di, &dui);
	ox = c->x - nx; oy = c->y - ny;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|KeyPressMask, &ev);
		switch (ev.type) {
		case MotionNotify:
			XQueryPointer(dpy, root, &dummy, &dummy, &nx, &ny, &di, &di, &dui);
			resize(c, nx + ox, ny + oy, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease &&
	         !(ev.type == KeyPress &&
	           XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0) == XK_Escape));
	XUngrabPointer(dpy, CurrentTime);
}

void
movearrow(const Arg *arg)
{
	int step = 10;
	if (!selmon->sel || !selmon->sel->isfloating) return;
	Client *c = selmon->sel;
	switch (arg->i) {
	case 0: resize(c, c->x - step, c->y,     c->w, c->h, 0); break;
	case 1: resize(c, c->x + step, c->y,     c->w, c->h, 0); break;
	case 2: resize(c, c->x,     c->y - step, c->w, c->h, 0); break;
	case 3: resize(c, c->x,     c->y + step, c->w, c->h, 0); break;
	}
}

void
resizemouse(const Arg *arg)
{
	(void)arg;
	if (!selmon->sel) return;
	Client *c = selmon->sel;
	if (!c->isfloating) { togglefloating(NULL); c = selmon->sel; if (!c) return; }
	int ox, oy, nx, ny;
	int di;
	unsigned int dui;
	Window dummy;
	XEvent ev;

	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize], CurrentTime) != GrabSuccess) return;

	XQueryPointer(dpy, root, &dummy, &dummy, &nx, &ny, &di, &di, &dui);
	ox = c->w - nx;
	oy = c->h - ny;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|KeyPressMask, &ev);
		switch (ev.type) {
		case MotionNotify:
			XQueryPointer(dpy, root, &dummy, &dummy, &nx, &ny, &di, &di, &dui);
			resize(c, c->x, c->y, MAX(ox + nx, MAX(50, c->minw)),
				MAX(oy + ny, MAX(50, c->minh)), 1);
			break;
		}
	} while (ev.type != ButtonRelease &&
	         !(ev.type == KeyPress &&
	           XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0) == XK_Escape));
	XUngrabPointer(dpy, CurrentTime);
}

void
resizearrow(const Arg *arg)
{
	int step = 10;
	if (!selmon->sel || !selmon->sel->isfloating) return;
	Client *c = selmon->sel;
	switch (arg->i) {
	case 0: resize(c, c->x, c->y, MAX(c->minw, c->w - step), c->h, 1); break;
	case 1: resize(c, c->x, c->y, MAX(c->minw, c->w + step), c->h, 1); break;
	case 2: resize(c, c->x, c->y, c->w, MAX(c->minh, c->h - step), 1); break;
	case 3: resize(c, c->x, c->y, c->w, MAX(c->minh, c->h + step), 1); break;
	}
}

void
unmapnotify(XEvent *e)
{
	XUnmapEvent *ev = &e->xunmap;
	Client *c = wintoclient(ev->window);
	if (!c || c == scratchpad) return;
	if (!ev->send_event)
		unmanage(c, 0);
}

/* ---- monitor management ---- */

static void
createmon(int num, int x, int y, int w, int h)
{
	Monitor *m = ecalloc(1, sizeof(Monitor));
	m->num = num;
	m->x = x; m->y = y;
	m->w = w; m->h = h;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->gappx = gappx;
	m->gappoh = gappoh;
	m->gappoi = gappoi;
	m->topbar = topbar;
	m->tags = 1 << (num % LENGTH(tags));
	m->lt[0] = (Layout *)&layouts[0];
	m->lt[1] = (Layout *)&layouts[1];
	m->layout = 0;
	for (unsigned int i = 0; i < LENGTH(tags); i++) {
		m->taglayout[i] = 0;
		m->taglt[i][0] = (Layout *)&layouts[0];
		m->taglt[i][1] = (Layout *)&layouts[1];
	}
	m->mx = x; m->my = y;
	m->mw = w; m->mh = h;
	m->wx = x;
	m->ww = w;
	if (showbar) {
		m->wy = y + (topbar ? bh : 0);
		m->wh = h - bh;
	} else {
		m->wy = y;
		m->wh = h;
	}
	m->next = mons;
	mons = m;
}

void
updategeom(void)
{
	/* save per-monitor state */
	unsigned int saved_tags = 0, saved_oldtags = 0;
	int saved_taglayout[8] = {0};
	struct Layout *saved_taglt[8][2] = {{NULL}};
	float saved_mfact = mfact;
	int saved_nmaster = nmaster;
	int saved_layout = 0;
	struct Layout *saved_lt[2] = { NULL, NULL };
	int saved_gappx = gappx, saved_gappoh = gappoh, saved_gappoi = gappoi;
	int saved_topbar = topbar;
	int had_monitor = 0;

	if (mons) {
		Monitor *m = mons;
		had_monitor = 1;
		saved_tags = m->tags;
		saved_oldtags = m->oldtags;
		saved_mfact = m->mfact;
		saved_nmaster = m->nmaster;
		saved_layout = m->layout;
		saved_lt[0] = m->lt[0];
		saved_lt[1] = m->lt[1];
		saved_gappx = m->gappx;
		saved_gappoh = m->gappoh;
		saved_gappoi = m->gappoi;
		saved_topbar = m->topbar;
		for (int i = 0; i < LENGTH(tags); i++) {
			saved_taglayout[i] = m->taglayout[i];
			saved_taglt[i][0] = m->taglt[i][0];
			saved_taglt[i][1] = m->taglt[i][1];
		}
	}

	/* save existing clients */
	Client *allclients = NULL;
	Client **tail = &allclients;
	for (Monitor *om = mons; om; ) {
		Monitor *next = om->next;
		Client *c;
		while ((c = om->clients)) {
			om->clients = c->next;
			c->next = NULL;
			*tail = c;
			tail = &c->next;
		}
		if (om->barwin) XDestroyWindow(dpy, om->barwin);
		if (om->root) bspnode_destroy(om->root);
		free(om);
		om = next;
	}
	mons = NULL;

	/* single monitor (multi-monitor via Xinerama would need the lib) */
	createmon(0, 0, 0, sw, sh);

	if (!selmon) selmon = mons;
	selmon = mons;  /* unconditionally update after freeing old monitors */

	/* restore per-monitor state */
	if (had_monitor && selmon) {
		selmon->tags = saved_tags;
		selmon->oldtags = saved_oldtags;
		selmon->mfact = saved_mfact;
		selmon->nmaster = saved_nmaster;
		selmon->layout = saved_layout;
		selmon->lt[0] = saved_lt[0];
		selmon->lt[1] = saved_lt[1];
		selmon->gappx = saved_gappx;
		selmon->gappoh = saved_gappoh;
		selmon->gappoi = saved_gappoi;
		selmon->topbar = saved_topbar;
		for (int i = 0; i < LENGTH(tags); i++) {
			selmon->taglayout[i] = saved_taglayout[i];
			selmon->taglt[i][0] = saved_taglt[i][0];
			selmon->taglt[i][1] = saved_taglt[i][1];
		}
	}

	/* reassign clients */
	for (Client *c = allclients; c; ) {
		Client *next = c->next;
		c->next = NULL;
		c->snext = NULL;
		c->node = NULL;
		c->mon = selmon;
		attach(c);
		attachstack(c);
		c = next;
	}

	for (Client *t = selmon->stack; t; t = t->snext)
		if (ISVISIBLE(t, selmon->tags)) {
			selmon->sel = t;
			break;
		}

	updatebars();
	for (Monitor *m = mons; m; m = m->next)
		arrange(m);
	drawbars();
}

/* ---- setup / main loop ---- */

void
scan(void)
{
	Window dw1, dw2, *wins;
	unsigned int nw;

	if (XQueryTree(dpy, root, &dw1, &dw2, &wins, &nw)) {
		for (unsigned int i = 0; i < nw; i++) {
			XWindowAttributes wa;
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			    || wa.override_redirect
			    || wa.map_state != IsViewable)
				continue;
			manage(wins[i], &wa);
		}
		if (wins) XFree(wins);
	}
}

void
autostart(void)
{
	char *home = getenv("HOME");
	if (!home) return;

	char dir[512];
	snprintf(dir, sizeof(dir), "%s/.config", home);
	mkdir(dir, 0755);
	snprintf(dir, sizeof(dir), "%s/.config/bytewm", home);
	mkdir(dir, 0755);

	char path[512];
	snprintf(path, sizeof(path), "%s/.config/bytewm/autostart.sh", home);

	struct stat st;
	if (stat(path, &st) == 0 && (st.st_mode & S_IXUSR)) {
		pid_t pid = fork();
		if (pid == 0) {
			if (dpy) close(ConnectionNumber(dpy));
			setsid();
			execl("/bin/sh", "sh", path, NULL);
			_exit(1);
		} else if (pid < 0) {
			fprintf(stderr, "bytewm: fork failed: %s\n", strerror(errno));
		}
	}
}

void
setfullscreen(Client *c, int fs)
{
	if (fs && !c->isfullscreen) {
		c->isfullscreen = 1;
		c->oldx = c->x; c->oldy = c->y;
		c->oldw = c->w; c->oldh = c->h;
		c->oldstate = c->bw;
		c->bw = 0;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fs && c->isfullscreen) {
		c->isfullscreen = 0;
		c->bw = c->oldstate;
		resizeclient(c, c->oldx, c->oldy, c->oldw, c->oldh);
		arrange(c->mon);
	}
	XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		PropModeReplace, (unsigned char *)&netatom[NetWMFullscreen],
		fs ? 1 : 0);
}

void
updatestatus(void)
{
	char *home = getenv("HOME");
	if (!home) return;

	char path[512];
	snprintf(path, sizeof(path), "%s/.config/bytewm/status.sh", home);

	int pipefd[2];
	if (pipe(pipefd) == -1)
		goto fallback;

	pid_t status_pid = fork();
	switch (status_pid) {
	case -1:
		close(pipefd[0]);
		close(pipefd[1]);
		goto fallback;
	case 0:
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		if (dpy) close(ConnectionNumber(dpy));
		execl("/bin/sh", "sh", path, NULL);
		_exit(1);
	default:
		break;
	}

	close(pipefd[1]);
	{
		FILE *fp = fdopen(pipefd[0], "r");
		if (fp) {
			struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
			if (poll(&pfd, 1, 100) > 0) {
				if (fgets(status, sizeof(status), fp)) {
					status[strcspn(status, "\n")] = '\0';
					strncpy(status_cache, status, sizeof(status_cache) - 1);
				} else {
					strncpy(status, status_cache, sizeof(status) - 1);
				}
			} else {
				kill(status_pid, SIGKILL);
				waitpid(status_pid, NULL, WNOHANG);
				strncpy(status, status_cache, sizeof(status) - 1);
			}
			fclose(fp);
		} else {
			close(pipefd[0]);
		}
	}

fallback:
	if (!status[0]) {
		if (status_cache[0])
			strncpy(status, status_cache, sizeof(status) - 1);
		else
			snprintf(status, sizeof(status), "bytewm %s", VERSION);
	}
	drawbars();
}

/* signal handlers */

void
exitlog(const char *why)
{
	int fd = open(crash_logpath[0] ? crash_logpath : "/tmp/bytewm_crash.log",
	              O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
	if (fd >= 0) {
		char msg[256];
		int n = snprintf(msg, sizeof(msg), "exit: %s\n", why);
		if (n > 0 && (size_t)n < sizeof(msg))
			write(fd, msg, (size_t)n);
		close(fd);
	}
}

void
sigsegv(int sig)
{
	int fd = open(crash_logpath[0] ? crash_logpath : "/tmp/bytewm_crash.log",
	              O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
	if (fd >= 0) {
		char msg[64];
		char *p = msg;
		char *end = msg + sizeof(msg) - 1;
		const char *prefix = "crash signal ";
		while (*prefix && p < end) *p++ = *prefix++;
		if (sig < 0 && p < end) { *p++ = '-'; sig = -sig; }
		if (sig >= 100 && p < end) *p++ = '0' + (sig / 100);
		if (sig >= 10 && p < end)  *p++ = '0' + ((sig / 10) % 10);
		if (p < end) *p++ = '0' + (sig % 10);
		if (p < end) *p++ = '\n';
		write(fd, msg, (size_t)(p - msg));
		close(fd);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

void
sigchld(int unused)
{
	(void)unused;
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void
sighup(int unused)
{
	(void)unused;
	restart = 1;
	running = 0;
}

static char *trim(char *s)
{
	if (!*s) return s;
	while (*s == ' ' || *s == '\t') s++;
	char *end = s + strlen(s) - 1;
	while (end > s && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
	*(end + 1) = '\0';
	return s;
}

static void strip_comment(char *s)
{
	char *t = s;
	while (*t == ' ' || *t == '\t') t++;
	if (*t == '#') *s = '\0';
}

static char *config_path(char *buf, size_t size, const char *file)
{
	char *home = getenv("HOME");
	if (!home) {
		snprintf(buf, size, "/tmp/bytewm/%s", file);
		return buf;
	}
	snprintf(buf, size, "%s/.config/bytewm/%s", home, file);
	return buf;
}

static void config_parse(void)
{
	char path[512];
	config_path(path, sizeof(path), "config");

	/* generate default if missing */
	struct stat st;
	if (stat(path, &st) != 0) {
		FILE *def = fopen(path, "w");
		if (def) {
			fprintf(def,
				"# bytewm config\n"
				"color.normfg = %s\n"
				"color.normbg = %s\n"
				"color.selfg = %s\n"
				"color.selbg = %s\n"
				"color.tagfg = %s\n"
				"color.tagbg = %s\n"
				"color.urgfg = %s\n"
				"color.urgbg = %s\n"
				"color.unfgborder = %s\n"
				"font = %s\n"
				"barheight = %u\n"
				"borderpx = %u\n"
				"gappx = %u\n"
				"gappoh = %u\n"
				"gappoi = %u\n"
				"showbar = %d\n"
				"topbar = %d\n",
				colors[SchemeNorm][ColFG],
				colors[SchemeNorm][ColBG],
				colors[SchemeSel][ColFG],
				colors[SchemeSel][ColBG],
				colors[SchemeTag][ColFG],
				colors[SchemeTag][ColBG],
				colors[SchemeUrg][ColFG],
				colors[SchemeUrg][ColBG],
				col_dimbg,
				font,
				barheight,
				borderpx,
				gappx,
				gappoh,
				gappoi,
				showbar,
				topbar);
			fclose(def);
		}
		return;
	}

	FILE *fp = fopen(path, "r");
	if (!fp) return;

	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		strip_comment(line);
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = trim(line);
		char *val = trim(eq + 1);
		if (!*key || !*val) continue;

		if (!strcmp(key, "font")) {
			strncpy(font, val, sizeof(font) - 1);
			font[sizeof(font) - 1] = '\0';
		} else 		if (!strcmp(key, "color.normfg")) {
			strncpy(colors[SchemeNorm][ColFG], val, 15);
			colors[SchemeNorm][ColFG][15] = '\0';
		} else if (!strcmp(key, "color.normbg")) {
			strncpy(colors[SchemeNorm][ColBG], val, 15);
			colors[SchemeNorm][ColBG][15] = '\0';
		} else if (!strcmp(key, "color.selfg")) {
			strncpy(colors[SchemeSel][ColFG], val, 15);
			colors[SchemeSel][ColFG][15] = '\0';
		} else if (!strcmp(key, "color.selbg")) {
			strncpy(colors[SchemeSel][ColBG], val, 15);
			colors[SchemeSel][ColBG][15] = '\0';
		} else if (!strcmp(key, "color.tagfg")) {
			strncpy(colors[SchemeTag][ColFG], val, 15);
			colors[SchemeTag][ColFG][15] = '\0';
		} else if (!strcmp(key, "color.tagbg")) {
			strncpy(colors[SchemeTag][ColBG], val, 15);
			colors[SchemeTag][ColBG][15] = '\0';
		} else if (!strcmp(key, "color.urgfg")) {
			strncpy(colors[SchemeUrg][ColFG], val, 15);
			colors[SchemeUrg][ColFG][15] = '\0';
		} else if (!strcmp(key, "color.urgbg")) {
			strncpy(colors[SchemeUrg][ColBG], val, 15);
			colors[SchemeUrg][ColBG][15] = '\0';
		} else if (!strcmp(key, "color.unfgborder")) {
			strncpy(col_dimbg, val, 15);
			col_dimbg[15] = '\0';
		} else if (!strcmp(key, "barheight")) {
			barheight = (unsigned int)atoi(val);
		} else if (!strcmp(key, "borderpx")) {
			borderpx = (unsigned int)atoi(val);
		} else if (!strcmp(key, "gappx")) {
			gappx = (unsigned int)atoi(val);
		} else if (!strcmp(key, "gappoh")) {
			gappoh = (unsigned int)atoi(val);
		} else if (!strcmp(key, "gappoi")) {
			gappoi = (unsigned int)atoi(val);
		} else if (!strcmp(key, "showbar")) {
			showbar = atoi(val);
		} else if (!strcmp(key, "topbar")) {
			topbar = atoi(val);
		}
	}
	fclose(fp);
}

void
	setup(void)
{
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	{
		char *home = getenv("HOME");
		if (home) {
			char dir[512];
			snprintf(dir, sizeof(dir), "%s/.config", home);
			mkdir(dir, 0755);
			snprintf(dir, sizeof(dir), "%s/.config/bytewm", home);
			mkdir(dir, 0755);
			snprintf(dir, sizeof(dir), "%s/.cache", home);
			mkdir(dir, 0755);
			snprintf(dir, sizeof(dir), "%s/.cache/bytewm", home);
			mkdir(dir, 0755);
			snprintf(crash_logpath, sizeof(crash_logpath),
			         "%s/.cache/bytewm/crash.log", home);
		}
	}

	config_parse();

	initfont();

	bargc = XCreateGC(dpy, root, 0, NULL);

	/* parse & allocate colors */
	normfg = getcolor(colors[SchemeNorm][ColFG]);
	normbg = getcolor(colors[SchemeNorm][ColBG]);
	selfg  = getcolor(colors[SchemeSel][ColFG]);
	selbg  = getcolor(colors[SchemeSel][ColBG]);
	tagfg  = getcolor(colors[SchemeTag][ColFG]);
	tagbg  = getcolor(colors[SchemeTag][ColBG]);
	urgfg  = getcolor(colors[SchemeUrg][ColFG]);
	urgbg  = getcolor(colors[SchemeUrg][ColBG]);
	unfgborder = getcolor(col_dimbg);

	{
		Visual *vis = DefaultVisual(dpy, screen);
		Colormap cmap = DefaultColormap(dpy, screen);
		XftColorAllocName(dpy, vis, cmap,
			colors[SchemeNorm][ColFG], &xft_normfg);
		XftColorAllocName(dpy, vis, cmap,
			colors[SchemeSel][ColFG], &xft_selfg);
		XftColorAllocName(dpy, vis, cmap,
			colors[SchemeTag][ColFG], &xft_tagfg);
		XftColorAllocName(dpy, vis, cmap,
			colors[SchemeUrg][ColFG], &xft_urgfg);
		XftColorAllocName(dpy, vis, cmap,
			"#83a598", &xft_cpu);
		XftColorAllocName(dpy, vis, cmap,
			"#b8bb26", &xft_mem);
		XftColorAllocName(dpy, vis, cmap,
			"#fabd2f", &xft_temp);
		XftColorAllocName(dpy, vis, cmap,
			"#d3869b", &xft_gpu);
		XftColorAllocName(dpy, vis, cmap,
			"#689d6a", &xft_seltag);
		XftColorAllocName(dpy, vis, cmap,
			"#fe8019", &xft_vol);
		XftColorAllocName(dpy, vis, cmap,
			"#a89984", &xft_date);
	}

	/* atoms */
	netatom[NetSupported]     = getatom("_NET_SUPPORTED");
	netatom[NetWMName]        = getatom("_NET_WM_NAME");
	netatom[NetWMState]       = getatom("_NET_WM_STATE");
	netatom[NetWMCheck]       = getatom("_NET_SUPPORTING_WM_CHECK");
	netatom[NetWMFullscreen]  = getatom("_NET_WM_STATE_FULLSCREEN");
	netatom[NetActiveWindow]  = getatom("_NET_ACTIVE_WINDOW");
	netatom[NetClientList]    = getatom("_NET_CLIENT_LIST");
	netatom[NetWMDesktop]     = getatom("_NET_WM_DESKTOP");
	netatom[NetNumberOfDesktops] = getatom("_NET_NUMBER_OF_DESKTOPS");
	netatom[NetCurrentDesktop]   = getatom("_NET_CURRENT_DESKTOP");

	wmatom[WMProtocols]       = getatom("WM_PROTOCOLS");
	wmatom[WMDelete]          = getatom("WM_DELETE_WINDOW");
	wmatom[WMState]           = getatom("WM_STATE");
	wmatom[WMTakeFocus]       = getatom("WM_TAKE_FOCUS");

	/* cursors */
	cursor[CurNormal] = XCreateFontCursor(dpy, XC_left_ptr);
	cursor[CurResize] = XCreateFontCursor(dpy, XC_sizing);
	cursor[CurMove]   = XCreateFontCursor(dpy, XC_fleur);
	XDefineCursor(dpy, root, cursor[CurNormal]);

	/* EWMH hints */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *)netatom, NetLast);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&(Window){0}, 0);
	XSetSelectionOwner(dpy, netatom[NetWMCheck], root, CurrentTime);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&root, 1);
	XChangeProperty(dpy, root, netatom[NetWMName], XInternAtom(dpy, "UTF8_STRING", False), 8,
		PropModeReplace, (unsigned char *)"bytewm", 6);
	{
		unsigned long ndesks = LENGTH(tags);
		XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&ndesks, 1);
	}
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&(unsigned long){0}, 1);

	updatenumlockmask();
	grabkeys();
	updategeom();
	scan();
	updatestatus();
	autostart();
}

void
run(void)
{
	signal(SIGHUP, sighup);

	int xfd = ConnectionNumber(dpy);
	time_t last_update = 0;
	XEvent ev;
	XSync(dpy, False);

	/* status fifo: instant refresh on volume change etc. */
	int sfifo = open("/tmp/bytewm_status.fifo", O_RDWR | O_NONBLOCK);
	if (sfifo < 0) {
		(void)unlink("/tmp/bytewm_status.fifo");
		if (mkfifo("/tmp/bytewm_status.fifo", 0600) == 0)
			sfifo = open("/tmp/bytewm_status.fifo", O_RDWR | O_NONBLOCK);
	}

	while (running) {
		time_t now = time(NULL);
		if (!XPending(dpy)) {
			struct pollfd pfd[2] = {
				{ .fd = xfd, .events = POLLIN },
				{ .fd = sfifo, .events = POLLIN }
			};
			int nfds = 1;
			if (sfifo >= 0) nfds = 2;
			if (poll(pfd, nfds, 1000) < 0) {
				if (errno == EINTR) continue;
				exitlog("run: poll failed");
				break;
			}
			now = time(NULL);
		}
		while (running && XPending(dpy)) {
			XNextEvent(dpy, &ev);
			switch (ev.type) {
			case ButtonPress:      buttonpress(&ev);      break;
			case ClientMessage:    clientmessage(&ev);    break;
			case ConfigureNotify:  configurenotify(&ev);  break;
			case ConfigureRequest: configurerequest(&ev); break;
			case DestroyNotify:    destroynotify(&ev);    break;
			case EnterNotify:      enternotify(&ev);      break;
			case Expose:           expose(&ev);           break;
			case KeyPress:         keypress(&ev);         break;
			case MappingNotify:    mappingnotify(&ev);    break;
			case MapRequest:       maprequest(&ev);       break;
			case MotionNotify:     motionnotify(&ev);     break;
			case PropertyNotify:   propertynotify(&ev);   break;
			case UnmapNotify:      unmapnotify(&ev);      break;
			}
		}
		if (sfifo >= 0) {
			struct pollfd pf = { .fd = sfifo, .events = POLLIN };
			if (poll(&pf, 1, 0) > 0) {
				char drain[256];
				while (read(sfifo, drain, sizeof(drain)) > 0);
				updatestatus();
			}
		}
		if (now - last_update >= 2) {
			last_update = now;
			updatestatus();
		}
	}

	if (sfifo >= 0) close(sfifo);
}

void
cleanup(void)
{
	while (mons) {
		Monitor *m = mons;
		mons = m->next;
		while (m->clients) {
			Client *c = m->clients;
			m->clients = c->next;
			XConfigureWindow(dpy, c->win, CWBorderWidth,
				&(XWindowChanges){.border_width = c->oldbw});
			XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
			free(c);
		}
		if (m->barwin) XDestroyWindow(dpy, m->barwin);
		if (m->root) bspnode_destroy(m->root);
		free(m);
	}
	if (xfont_core)
		XFreeFont(dpy, xfont_core);
	if (xfont)
		XftFontClose(dpy, xfont);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_normfg);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_selfg);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_tagfg);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_urgfg);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_cpu);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_mem);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_temp);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_gpu);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_date);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_seltag);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &xft_vol);
	XFreeGC(dpy, bargc);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XFreeCursor(dpy, cursor[CurNormal]);
	XFreeCursor(dpy, cursor[CurResize]);
	XFreeCursor(dpy, cursor[CurMove]);
	XSync(dpy, 0);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp(argv[1], "-v")) {
		puts("bytewm-" VERSION);
		return 0;
	}

	if (!(dpy = XOpenDisplay(NULL)))
		die("bytewm: could not open display\n");

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	{
		struct sigaction sa = {0};
		sa.sa_handler = sigchld;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
		sigaction(SIGCHLD, &sa, NULL);
	}

	signal(SIGSEGV, sigsegv);
	signal(SIGABRT, sigsegv);

	checkotherwm();
	setup();
	run();

	if (restart) {
		cleanup();
		XCloseDisplay(dpy);
		execvp(argv[0], argv);
		fprintf(stderr, "bytewm: execvp failed: %s\n", strerror(errno));
		return 1;
	}

	cleanup();
	XCloseDisplay(dpy);

	return 0;
}

#define _POSIX_C_SOURCE 200809L

/* See LICENSE file for copyright and license details.
 * bytewm - a retro, gruvbox-themed tiling window manager for X11
 *
 * features:
 *  - master-stack and binary tree (bsp) tiling layouts
 *  - configurable gaps
 *  - scratchpad (dropdown terminal)
 *  - window swallow (terminal replaced by launched GUI app)
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
       NetWMFullscreen, NetActiveWindow, NetClientList, NetLast };
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast };
enum { SchemeNorm, SchemeSel, SchemeTag, SchemeUrg, SchemeLast };

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
	int isterminal;
} Rule;

typedef struct Client {
	struct Client *next;
	struct Client *snext;
	Window win;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, isfullscreen, isterminal;
	int neverfocus;
	int oldstate;
	pid_t pid;
	struct Client *swallowing;
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
static void drawtext(Pixmap pmap, int x, int y, unsigned long fg, unsigned long bg, const char *text, int w);
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
static void swallow(Client *c, Client *owner);
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
static void unswallow(Client *c);
static void updatebars(void);
static void updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void view(const Arg *arg);
static void viewprevtag(const Arg *arg);
static Client *wintoclient(Window w);
static int wintitlematch(Client *c, const char *title);
static int xerror(Display *dpy, XErrorEvent *ee);
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

XFontStruct *xfont;
unsigned long normfg, normbg, selfg, selbg, tagfg, tagbg, urgfg, urgbg, unfgborder;
GC bargc;
static char status[512] = "";

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
	char msg[256];
	XGetErrorText(dpy, ee->error_code, msg, sizeof(msg));
	int fd = open("/tmp/bytewm_crash.log",
	              O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		dprintf(fd, "X error: %s (code %d) on 0x%lx req %d.%d serial %lu\n",
		        msg, ee->error_code, ee->resourceid,
		        ee->request_code, ee->minor_code, ee->serial);
		void *buf[32];
		int n = backtrace(buf, sizeof(buf)/sizeof(buf[0]));
		dprintf(fd, "backtrace:\n");
		backtrace_symbols_fd(buf, n, fd);
		close(fd);
	}
	if (ee->error_code == BadAccess)
		die("bytewm: another window manager is already running\n");
	return xerrorxlib(dpy, ee);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerror);
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
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success
	           && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
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
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
			    == XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
grabkeys(void)
{
	KeyCode code;
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int i = 0; i < LENGTH(keys); i++) {
		if ((code = XKeysymToKeycode(dpy, keys[i].keysym))) {
			XGrabKey(dpy, code, keys[i].mod, root, True,
				GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, code, keys[i].mod | numlockmask, root, True,
				GrabModeAsync, GrabModeAsync);
		}
	}
}

void
grabbuttons(Client *c, int focused)
{
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (!focused) return;
	XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
		BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	for (int i = 0; i < LENGTH(buttons); i++)
		if (buttons[i].func && buttons[i].button <= Button5)
			XGrabButton(dpy, buttons[i].button, buttons[i].mod, c->win, False,
				BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
}

void
setfocus(Client *c)
{
	if (!c) return;
	if (c->isfullscreen)
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
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
	if (!c || !ISVISIBLE(c, c->mon->tags)) return;
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
	if (raise) {
		grabbuttons(c, 1);
		XRaiseWindow(dpy, c->win);
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
	c->isterminal = 0;
	for (int i = 0; i < LENGTH(rules); i++) {
		if ((!rules[i].title || wintitlematch(c, rules[i].title))
		    && (!rules[i].class || (cls && !strcmp(rules[i].class, cls)))
		    && (!rules[i].instance || (inst && !strcmp(rules[i].instance, inst)))) {
			c->isfloating = rules[i].isfloating;
			c->tags |= rules[i].tags;
			c->isterminal = rules[i].isterminal;
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
			XMapWindow(dpy, walk->win);
		else
			XUnmapWindow(dpy, walk->win);
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

	XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->layout]->arrange) {
		XWindowChanges wc = { .stack_mode = Below, .sibling = m->barwin };
		for (Client *c = m->stack; c; c = c->snext)
			if (ISVISIBLE(c, m->tags) && c != m->sel)
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
	}
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
					sw - (master_n > 0 ? goi * 2 : 0) - 2 * borderpx,
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
	if (n == 0) return;

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

	/* destroy old tree */
	if (m->root) {
		for (Client *c = m->clients; c; c = c->next)
			c->node = NULL;
		bspnode_destroy(m->root);
	}
	m->root = NULL;

	/* rebuild balanced tree with alternating splits */
	m->root = bsp_build(list, 0, nclients - 1, 0);

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
	if (!xfont || !s || !*s) return 0;
	return XTextWidth(xfont, s, strlen(s));
}

void
initfont(void)
{
	if (!(xfont = XLoadQueryFont(dpy, font)))
		die("bytewm: could not load font '%s'\n", font);
	bh = MAX(barheight, xfont->ascent + xfont->descent + 4);
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
drawtext(Pixmap pmap, int x, int y, unsigned long fg, unsigned long bg, const char *text, int w)
{
	XSetForeground(dpy, bargc, bg);
	XFillRectangle(dpy, pmap, bargc, x, y, w, bh);
	XSetForeground(dpy, bargc, fg);
	int tx = x + 2;
	int ty = y + (bh - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;
	XDrawString(dpy, pmap, bargc, tx, ty, text, (int)strnlen(text, 256));
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

	/* tags with retro [N] style for active, N for inactive */
	for (int i = 0; i < LENGTH(tags); i++) {
		int occupied = 0, urgent = 0;
		for (Client *c = m->clients; c; c = c->next) {
			if (c->tags & (1 << i)) {
				occupied = 1;
				if (c->isurgent) urgent = 1;
			}
		}
		int sel = (m->tags & (1 << i)) != 0;
		char label[8];
		if (sel)
			snprintf(label, sizeof(label), "[%s]", tags[i]);
		else
			snprintf(label, sizeof(label), " %s ", tags[i]);
		unsigned long fg, bg;
		if (sel)       { fg = selfg;  bg = selbg; }
		else if (urgent) { fg = urgfg; bg = urgbg; }
		else if (occupied) { fg = tagfg; bg = tagbg; }
		else           { fg = normfg; bg = normbg; }
		int tw = textwidth(label) + 4;
		if (rx + tw > w) tw = w - rx;
		if (tw <= 0) break;
		drawtext(pmap, rx, 0, fg, bg, label, tw);
		rx += tw;
	}

	/* separator */
	{
		drawtext(pmap, rx, 0, tagfg, normbg, " | ", 14);
		rx += 14;
	}

	/* layout symbol */
	{
		const char *sym = m->lt[m->layout]->name;
		int lw = textwidth(sym) + 8;
		drawtext(pmap, rx, 0, tagfg, normbg, sym, lw);
		rx += lw;
	}

	/* separator */
	{
		drawtext(pmap, rx, 0, tagfg, normbg, " | ", 14);
		rx += 14;
	}

	/* window title */
	if (m->sel) {
		char winname[256];
		if (gettextprop(m->sel->win, netatom[NetWMName], winname, sizeof(winname))) {
			int tw = MAX(w - rx, 0);
			drawtext(pmap, rx, 0, normfg, normbg, winname, tw);
		}
	}

	/* status text with orange | separators */
	if (status[0]) {
		int sepw = textwidth(" | ");
		int sw = textwidth(status) + 8;
		int sx = w - sw;
		if (sx < 0) sx = 0;
		int pos = sx;
		char buf[512];
		strncpy(buf, status, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		char *p = buf;
		while (*p) {
			char *sep = strstr(p, " | ");
			int seg_len = sep ? (int)(sep - p) : (int)strlen(p);
			char saved = p[seg_len];
			p[seg_len] = '\0';
			int tw = textwidth(p);
			drawtext(pmap, pos, 0, normfg, normbg, p, tw + 4);
			pos += tw + 2;
			p[seg_len] = saved;
			if (!sep) break;
			drawtext(pmap, pos, 0, tagfg, normbg, " | ", sepw);
			pos += sepw;
			p = sep + 3;
		}
	}

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

/* ---- window swallow ---- */

void
swallow(Client *c, Client *owner)
{
	if (c->swallowing || owner->swallowing) return;
	owner->swallowing = c;
	c->swallowing = owner;
	XUnmapWindow(dpy, owner->win);
	XMapWindow(dpy, c->win);
	arrange(c->mon);
}

void
unswallow(Client *c)
{
	if (!c->swallowing) return;
	Client *owner = c->swallowing;
	owner->swallowing = NULL;
	c->swallowing = NULL;
	XUnmapWindow(dpy, c->win);
	XMapWindow(dpy, owner->win);
	focus(owner, 1);
	arrange(c->mon);
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
	if (fork() == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)scratchpadcmd)[0], (char **)scratchpadcmd);
		_exit(1);
	}
}

void
scratchpadhide(void)
{
	if (!scratchpad) return;
	XUnmapWindow(dpy, scratchpad->win);
	scratchpad->tags = 0;
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
	if (fork() == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "bytewm: execvp %s failed: %s\n",
			((char **)arg->v)[0], strerror(errno));
		_exit(1);
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
	Client **where = NULL;

	if (arg->i > 0) {
		/* find position after the next visible client */
		Client *n;
		for (n = c->next; n && !ISVISIBLE(n, selmon->tags); n = n->next);
		if (!n) return;
		where = &n->next;
	} else {
		/* find position of the previous visible client */
		Client *p, *prev = NULL;
		for (p = selmon->clients; p && p != c; p = p->next)
			if (ISVISIBLE(p, selmon->tags))
				prev = p;
		if (!prev) return;
		for (where = &selmon->clients; *where && *where != prev; where = &(*where)->next);
	}

	detach(c);
	c->next = *where;
	*where = c;
	arrange(selmon);
	drawbars();
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL, 0);
		arrange(selmon);
		drawbars();
	}
}

void
toggletag(const Arg *arg)
{
	if (!selmon || !selmon->sel) return;
	unsigned int newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL, 0);
		arrange(selmon);
	}
	drawbars();
}

void
view(const Arg *arg)
{
	unsigned int tag = arg->ui & TAGMASK;
	if (!selmon) return;
	if (tag && tag != selmon->tags) {
		selmon->oldtags = selmon->tags;
		selmon->tags = tag;
		focus(NULL, 1);
		arrange(selmon);
		drawbars();
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
	if (lt->arrange)
		selmon->lt[selmon->layout]->arrange(selmon, countclients(selmon));
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
	c->swallowing = NULL;
	c->isterminal = 0;
	c->isfloating = 0;

	updatesizehints(c);
	applyrules(c);

	/* detect transient windows (dialogs) and float them */
	{
		Window trans;
		if (XGetTransientForHint(dpy, w, &trans) && trans != None) {
			c->isfloating = 1;
		}
	}

	/* in floating layout, auto-float new windows and center them */
	if (!selmon->lt[selmon->layout]->arrange) {
		c->isfloating = 1;
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
			&p) == Success && p) {
			c->pid = *(pid_t *)p;
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

	/* tree-based layouts build on arrange; no incremental building here */

	/* check for scratchpad (st -c scratchpad sets res_name) */
	XClassHint ch;
	if (XGetClassHint(dpy, w, &ch)) {
		if ((ch.res_class && strcmp(ch.res_class, "scratchpad") == 0)
		    || (ch.res_name && strcmp(ch.res_name, "scratchpad") == 0)) {
			if (scratchpad) XUnmapWindow(dpy, scratchpad->win);
			scratchpad = c;
			scratchpad->tags = 0;
			scratchpad->isfloating = 1;
			scratchpad->bw = 0;
			scratchpad->x = selmon->wx + selmon->ww / 4;
			scratchpad->y = selmon->wy + selmon->wh * 0.1;
			scratchpad->w = selmon->ww / 2;
			scratchpad->h = selmon->wh * 0.5;
			resizeclient(c, scratchpad->x, scratchpad->y,
			             scratchpad->w, scratchpad->h);
		}
		if (ch.res_class) XFree(ch.res_class);
		if (ch.res_name) XFree(ch.res_name);
	}

	/* swallow check */
	if (!c->isfloating || swallowfloating) {
		for (Monitor *m = mons; m; m = m->next)
			for (Client *owner = m->stack; owner && owner != c; owner = owner->snext)
				if (owner->isterminal && owner->pid == c->pid)
					swallow(c, owner);
	}

	/* set border and map before arranging so XRaiseWindow works */
	setborder(c, c->mon == selmon && ISVISIBLE(c, selmon->tags));
	XMapWindow(dpy, w);
	XSync(dpy, False);
	if (c->mon == selmon && ISVISIBLE(c, selmon->tags)) {
		focus(c, 1);
	}
	arrange(c->mon);
	drawbars();
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;

	if (c == scratchpad) scratchpad = NULL;
	if (c->swallowing) unswallow(c);

	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		PropModePrepend, (unsigned char *)&(Window){0}, 0);
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
	if (m->sel) focus(m->sel, 1);
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
			int sel = (m->tags & (1 << i)) != 0;
			if (sel)
				snprintf(label, sizeof(label), "[%s]", tags[i]);
			else
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
	Client *c = wintoclient(ev->window);
	if (!c) return;

	if (ev->message_type == netatom[NetWMState]) {
		if (ev->data.l[1] == (long)netatom[NetWMFullscreen]
		    || ev->data.l[2] == (long)netatom[NetWMFullscreen])
			setfullscreen(c, ev->data.l[0] == 1);
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
		}
	}
	XSetErrorHandler(xerrordummy);
	XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
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
	XCrossingEvent *ev = &e->xcrossing;
	if (ev->mode != NotifyNormal || ev->detail == NotifyInferior) return;
	Client *c = wintoclient(ev->window);
	if (c && c != selmon->sel) {
		focus(c, 1);
		selmon = c->mon;
		drawbars();
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
	if (ev->atom == netatom[NetWMName])
		drawbars();
	if (ev->atom == XA_WM_NORMAL_HINTS)
		updatesizehints(c);
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
		while (!XCheckMaskEvent(dpy, ButtonReleaseMask, &ev)) {
		XQueryPointer(dpy, root, &dummy, &dummy, &nx, &ny, &di, &di, &dui);
		resize(c, nx + ox, ny + oy, c->w, c->h, 1);
		while (XCheckMaskEvent(dpy, PointerMotionMask, &ev));
	}
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
	while (!XCheckMaskEvent(dpy, ButtonReleaseMask, &ev)) {
		XQueryPointer(dpy, root, &dummy, &dummy, &nx, &ny, &di, &di, &dui);
		resize(c, c->x, c->y, MAX(ox + nx, MAX(50, c->minw)),
			MAX(oy + ny, MAX(50, c->minh)), 1);
		while (XCheckMaskEvent(dpy, PointerMotionMask, &ev));
	}
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
	if (c && ev->send_event) {
		if (c == scratchpad)
			scratchpadhide();
		unmanage(c, 0);
	}
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
	m->mx = x; m->my = y;
	m->mw = w; m->mh = h;
	m->wx = x;
	m->wy = y + (topbar ? bh : 0);
	m->ww = w;
	m->wh = h - (topbar ? bh : 0);
	m->next = mons;
	mons = m;
}

void
updategeom(void)
{
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
	if (!home) home = "/tmp";

	char dir[512];
	snprintf(dir, sizeof(dir), "%s/.config/bytewm", home);
	mkdir(dir, 0755);

	char path[512];
	snprintf(path, sizeof(path), "%s/.config/bytewm/autostart.sh", home);

	struct stat st;
	if (stat(path, &st) == 0 && (st.st_mode & S_IXUSR)) {
		if (fork() == 0) {
			if (dpy) close(ConnectionNumber(dpy));
			setsid();
			execl("/bin/sh", "sh", path, NULL);
			_exit(1);
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
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fs && c->isfullscreen) {
		c->isfullscreen = 0;
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
	if (!home) home = "/tmp";

	char path[512];
	snprintf(path, sizeof(path), "%s/.config/bytewm/status.sh", home);

	int pipefd[2];
	if (pipe(pipefd) == -1)
		goto fallback;

	switch (fork()) {
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
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(pipefd[0], &fds);
			struct timeval tv = {2, 0};
			if (select(pipefd[0] + 1, &fds, NULL, NULL, &tv) > 0) {
				if (fgets(status, sizeof(status), fp))
					status[strcspn(status, "\n")] = '\0';
				else
					status[0] = '\0';
			} else {
				status[0] = '\0';
			}
			fclose(fp);
		} else {
			close(pipefd[0]);
		}
	}

fallback:
	if (!status[0])
		snprintf(status, sizeof(status), "bytewm %s", VERSION);
	drawbars();
}

/* signal handlers */

void
sigsegv(int sig)
{
	int fd = open("/tmp/bytewm_crash.log",
	              O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		void *buf[32];
		int n = backtrace(buf, sizeof(buf)/sizeof(buf[0]));
		dprintf(fd, "crash signal %d\n", sig);
		backtrace_symbols_fd(buf, n, fd);
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

void
setup(void)
{
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

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

	/* atoms */
	netatom[NetSupported]     = getatom("_NET_SUPPORTED");
	netatom[NetWMName]        = getatom("_NET_WM_NAME");
	netatom[NetWMState]       = getatom("_NET_WM_STATE");
	netatom[NetWMCheck]       = getatom("_NET_SUPPORTING_WM_CHECK");
	netatom[NetWMFullscreen]  = getatom("_NET_WM_STATE_FULLSCREEN");
	netatom[NetActiveWindow]  = getatom("_NET_ACTIVE_WINDOW");
	netatom[NetClientList]    = getatom("_NET_CLIENT_LIST");

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
	signal(SIGCHLD, sigchld);
	signal(SIGHUP, sighup);
	signal(SIGSEGV, sigsegv);
	signal(SIGABRT, sigsegv);

	int xfd = ConnectionNumber(dpy);
	time_t last_update = 0;
	XEvent ev;
	XSync(dpy, False);
	while (running) {
		time_t now = time(NULL);
		if (!XPending(dpy)) {
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(xfd, &fds);
			struct timeval tv = {1, 0};
			if (select(xfd + 1, &fds, NULL, NULL, &tv) < 0) {
				if (errno == EINTR) continue;
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
		if (now - last_update >= 2) {
			last_update = now;
			updatestatus();
		}
	}
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
			if (c->swallowing) unswallow(c);
			XDestroyWindow(dpy, c->win);
			free(c);
		}
		if (m->barwin) XDestroyWindow(dpy, m->barwin);
		if (m->root) bspnode_destroy(m->root);
		free(m);
	}
	XFreeFont(dpy, xfont);
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
	if (argc == 2 && !strcmp(argv[1], "-v"))
		die("bytewm-" VERSION "\n");

	if (!(dpy = XOpenDisplay(NULL)))
		die("bytewm: could not open display\n");

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

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

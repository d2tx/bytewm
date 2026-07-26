/* bytewdm - minimal X11 login manager */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/XKBlib.h>
#include <security/pam_appl.h>

static Display *dpy;
static int screen, sw, sh, bh, display_num;
static Window root, win;
static GC gc;
static XFontStruct *font;
static unsigned long bg, fg, selcol, red, dimcol, lockcol;
static char display_str[8];

static char username[64], password[256];
static int ulen, plen;
static int state; /* 0=username, 1=password */
static int failed, failed_count;

static pid_t xpid;
static pam_handle_t *pamh_global;

#define MAX_SESSIONS 16
static char *session_names[MAX_SESSIONS];
static char *session_bins[MAX_SESSIONS];
static int n_sessions = 0;
static int session_idx = 0;

static void
add_session(const char *name, const char *bin)
{
	char *ncpy, *bcpy;
	if (n_sessions >= MAX_SESSIONS) return;
	if (!(ncpy = strdup(name)) || !(bcpy = strdup(bin))) {
		free(ncpy);
		return;
	}
	session_names[n_sessions] = ncpy;
	session_bins[n_sessions] = bcpy;
	n_sessions++;
}

static void
scan_sessions(void)
{
	DIR *dir;
	struct dirent *ent;
	char path[512];
	FILE *f;
	char line[512];

	add_session("bytewm", "/usr/local/bin/bytewm");

	dir = opendir("/usr/share/xsessions");
	if (!dir) return;
	while ((ent = readdir(dir))) {
		int len = strlen(ent->d_name);
		if (len < 9 || strcmp(ent->d_name + len - 8, ".desktop") != 0)
			continue;
		snprintf(path, sizeof(path), "/usr/share/xsessions/%s", ent->d_name);
		f = fopen(path, "r");
		if (!f) continue;
		char *name = NULL, *exec = NULL;
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (!strncmp(line, "Name=", 5)) {
				free(name);
				name = strdup(line + 5);
			} else if (!strncmp(line, "Exec=", 5)) {
				free(exec);
				exec = strdup(line + 5);
			}
		}
		fclose(f);
		if (name && exec) {
			char *space = strchr(exec, ' ');
			if (space) *space = '\0';
			struct stat st;
			if (exec[0] == '/' && stat(exec, &st) == 0)
				add_session(name, exec);
			free(name);
			free(exec);
		} else {
			free(name);
			free(exec);
		}
	}
	closedir(dir);
}

static void (*volatile secure_memset)(void *, int, size_t) = (void (*)(void *, int, size_t))memset;

static void
secure_wipe(void *buf, size_t len)
{
	secure_memset(buf, 0, len);
}

static void
reap_children(int unused)
{
	(void)unused;
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

static unsigned long
getcol(const char *s)
{
	XColor c;
	Colormap cm = DefaultColormap(dpy, DefaultScreen(dpy));
	return XParseColor(dpy, cm, s, &c) && XAllocColor(dpy, cm, &c) ? c.pixel : 0;
}

static int
textw(const char *s)
{
	return XTextWidth(font, s, strlen(s));
}

static void
redraw(void)
{
	char line[512];
	int x, y;

	XClearWindow(dpy, win);

	/* centered login box */
	int boxw = sw * 2 / 3;
	if (boxw > 480) boxw = 480;
	int boxh = bh * 5 + 40;
	int boxx = (sw - boxw) / 2;
	int boxy = (sh - boxh) / 2;

	XSetForeground(dpy, gc, bg);
	XFillRectangle(dpy, win, gc, boxx, boxy, boxw, boxh);

	y = boxy + bh + 8;

	/* title */
	snprintf(line, sizeof line, "bytewm");
	XSetForeground(dpy, gc, selcol);
	XDrawString(dpy, win, gc, (sw - textw(line)) / 2, y, line, strlen(line));

	y += bh + 12;

	/* prompt */
	if (state == 0)
		snprintf(line, sizeof line, "login: %s_", username);
	else {
		char stars[256];
		int i;
		for (i = 0; i < plen; i++) stars[i] = '*';
		stars[plen] = 0;
		snprintf(line, sizeof line, "password: %s_", stars);
	}
	XSetForeground(dpy, gc, fg);
	XDrawString(dpy, win, gc, (sw - textw(line)) / 2, y, line, strlen(line));

	y += bh + 8;

	/* error */
	if (failed) {
		snprintf(line, sizeof line, "Login incorrect");
		XSetForeground(dpy, gc, red);
		XDrawString(dpy, win, gc, (sw - textw(line)) / 2, y, line, strlen(line));
		y += bh + 4;
	}

	/* session selector */
	snprintf(line, sizeof line, "[ %s ] (Tab to change)",
		session_names[session_idx]);
	XSetForeground(dpy, gc, dimcol);
	XDrawString(dpy, win, gc, (sw - textw(line)) / 2, y, line, strlen(line));

	/* clock — top right */
	{
		time_t t = time(NULL);
		struct tm *tm = localtime(&t);
		char clockstr[64];
		strftime(clockstr, sizeof clockstr, "%H:%M", tm);
		snprintf(line, sizeof line, "%s", clockstr);
		x = sw - textw(line) - 16;
		y = 16;
		XSetForeground(dpy, gc, dimcol);
		XDrawString(dpy, win, gc, x, y + bh - 4, line, strlen(line));
	}

	/* date — under clock */
	{
		time_t t = time(NULL);
		struct tm *tm = localtime(&t);
		char datestr[64];
		strftime(datestr, sizeof datestr, "%a %d %b", tm);
		snprintf(line, sizeof line, "%s", datestr);
		x = sw - textw(line) - 16;
		y = 16 + bh;
		XSetForeground(dpy, gc, dimcol);
		XDrawString(dpy, win, gc, x, y + bh - 4, line, strlen(line));
	}

	/* lock indicators — top left */
	{
		unsigned int leds = 0;
		XkbGetIndicatorState(dpy, XkbUseCoreKbd, &leds);
		y = 16;
		x = 16;
		if (leds & 0x1) {
			XSetForeground(dpy, gc, lockcol);
			XDrawString(dpy, win, gc, x, y + bh - 4, "CAPS", 4);
			x += textw("CAPS") + 12;
		}
		if (leds & 0x2) {
			XSetForeground(dpy, gc, lockcol);
			XDrawString(dpy, win, gc, x, y + bh - 4, "NUM", 3);
		}
	}

	XSync(dpy, 0);
}

/* --- PAM --- */
struct pam_data {
	const char *user;
	const char *pass;
};

static int
pam_conv_cb(int num, const struct pam_message **msg,
            struct pam_response **resp, void *appdata)
{
	struct pam_data *pd = appdata;
	int i;
	*resp = calloc(num, sizeof(struct pam_response));
	if (!*resp) return PAM_BUF_ERR;
	for (i = 0; i < num; i++) {
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
			(*resp)[i].resp = strdup(pd->user);
		else if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)
			(*resp)[i].resp = strdup(pd->pass);
		else
			(*resp)[i].resp = NULL;
		if ((*resp)[i].resp == NULL &&
		    (msg[i]->msg_style == PAM_PROMPT_ECHO_ON ||
		     msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)) {
			int j;
			for (j = 0; j < i; j++)
				free((*resp)[j].resp);
			free(*resp);
			*resp = NULL;
			return PAM_BUF_ERR;
		}
	}
	return PAM_SUCCESS;
}

static int
authenticate(void)
{
	struct pam_data pd = { username, password };
	struct pam_conv conv = { pam_conv_cb, &pd };
	pam_handle_t *pamh = NULL;
	int ret;
	ret = pam_start("bytewdm", username, &conv, &pamh);
	if (ret != PAM_SUCCESS) return 0;
	ret = pam_authenticate(pamh, 0);
	if (ret != PAM_SUCCESS) { pam_end(pamh, ret); return 0; }
	ret = pam_acct_mgmt(pamh, 0);
	if (ret != PAM_SUCCESS) { pam_end(pamh, ret); return 0; }
	ret = pam_setcred(pamh, PAM_ESTABLISH_CRED);
	if (ret != PAM_SUCCESS) { pam_end(pamh, ret); return 0; }
	ret = pam_open_session(pamh, 0);
	if (ret != PAM_SUCCESS) { pam_end(pamh, ret); return 0; }
	pamh_global = pamh;
	return 1;
}

static void
close_session(void)
{
	if (pamh_global) {
		pam_close_session(pamh_global, 0);
		pam_setcred(pamh_global, PAM_DELETE_CRED);
		pam_end(pamh_global, PAM_SUCCESS);
		pamh_global = NULL;
	}
}

/* --- X server --- */
static void
startx(void)
{
	snprintf(display_str, sizeof display_str, ":%d", display_num);

	xpid = fork();
	if (xpid == 0) {
		close(0); close(1); close(2);
		if (open("/dev/null", O_RDONLY) < 0) _exit(1);
		if (open("/dev/null", O_WRONLY) < 0) _exit(1);
		if (open("/dev/null", O_WRONLY) < 0) _exit(1);
		setsid();
		execlp("X", "X", display_str, "-keeptty", "-novtswitch",
		       "-logfile", "/dev/null", NULL);
		execlp("Xorg", "Xorg", display_str, "-keeptty", "-novtswitch",
		       "-logfile", "/dev/null", NULL);
		_exit(1);
	}
	if (xpid < 0) {
		fprintf(stderr, "bytewdm: fork failed\n");
		exit(1);
	}

	usleep(500000);
	for (int i = 0; i < 100; i++) {
		dpy = XOpenDisplay(display_str);
		if (dpy) return;
		usleep(100000);
	}
	fprintf(stderr, "bytewdm: X server did not start on %s\n", display_str);
	exit(1);
}

static void
stopx(void)
{
	if (xpid > 0) {
		kill(xpid, SIGTERM);
		waitpid(xpid, NULL, 0);
		xpid = 0;
	}
}

/* --- greeter window --- */
static void
creategreeter(void)
{
	Cursor cur;

	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);

	font = XLoadQueryFont(dpy, "fixed");
	if (!font) { fprintf(stderr, "bytewdm: no font\n"); exit(1); }
	bh = font->ascent + font->descent + 4;

	gc = XCreateGC(dpy, root, 0, NULL);

	bg    = getcol("#282828");
	fg    = getcol("#ebdbb2");
	selcol = getcol("#fabd2f");
	red   = getcol("#fb4934");
	dimcol = getcol("#665c54");
	lockcol = getcol("#d65d0e");

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = bg,
		.event_mask = ExposureMask | KeyPressMask,
	};
	win = XCreateWindow(dpy, root, 0, 0, sw, sh, 0,
		DefaultDepth(dpy, screen), CopyFromParent,
		DefaultVisual(dpy, screen),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XMapWindow(dpy, win);
	XRaiseWindow(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	cur = XCreateFontCursor(dpy, XC_xterm);
	XDefineCursor(dpy, win, cur);
	XFreeCursor(dpy, cur);
}

static void
destroygreeter(void)
{
	XDestroyWindow(dpy, win);
	XFreeGC(dpy, gc);
	XFreeFont(dpy, font);
	XCloseDisplay(dpy);
	dpy = NULL;
}

/* --- session --- */
static void
runsession(void)
{
	struct passwd *pw = getpwnam(username);
	if (!pw) return;

	destroygreeter();

	pid_t child = fork();
	if (child == 0) {
		close(0); close(1); close(2);
		if (open("/dev/null", O_RDONLY) < 0) _exit(1);
		if (open("/dev/null", O_WRONLY) < 0) _exit(1);
		if (open("/dev/null", O_WRONLY) < 0) _exit(1);
		setsid();

		setenv("DISPLAY", display_str, 1);
		setenv("HOME", pw->pw_dir, 1);
		setenv("USER", pw->pw_name, 1);
		setenv("LOGNAME", pw->pw_name, 1);
		setenv("SHELL", pw->pw_shell, 1);
		chdir(pw->pw_dir);

		/* drop root privileges */
		setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
		if (initgroups(pw->pw_name, pw->pw_gid) != 0)
			_exit(1);
		if (setgid(pw->pw_gid) != 0)
			_exit(1);
		if (setuid(pw->pw_uid) != 0)
			_exit(1);

		/* try ~/.xinitrc or ~/.xsession, else bytewm directly */
		struct stat st;
		const char *session = NULL;
		char xinitrc[512], xsession[512];
		snprintf(xinitrc, sizeof xinitrc, "%s/.xinitrc", pw->pw_dir);
		snprintf(xsession, sizeof xsession, "%s/.xsession", pw->pw_dir);
		if (stat(xinitrc, &st) == 0 && S_ISREG(st.st_mode) &&
		    st.st_mode & S_IXUSR)
			session = xinitrc;
		else if (stat(xsession, &st) == 0 && S_ISREG(st.st_mode) &&
		         st.st_mode & S_IXUSR)
			session = xsession;
		else
			session = session_bins[session_idx];

		execl(pw->pw_shell, pw->pw_shell, "-c",
		      session, NULL);
		execl(session, session, NULL);
		_exit(1);
	}

	while (waitpid(child, NULL, 0) < 0 && errno == EINTR);
	close_session();
	stopx();
}

/* --- main loop --- */
int
main(void)
{
	setbuf(stdout, NULL);
	signal(SIGCHLD, reap_children);

	display_num = 0;
	startx();
	creategreeter();
	scan_sessions();
	redraw();

	XEvent ev;
	while (XNextEvent(dpy, &ev) >= 0) {
		if (ev.type == Expose) {
			if (ev.xexpose.count == 0) redraw();
		} else if (ev.type == KeyPress) {
			char tmp[32];
			KeySym ks;
			int n = XLookupString(&ev.xkey, tmp, sizeof tmp, &ks, NULL);

			if (ks == XK_Return) {
				if (state == 0) {
					if (ulen > 0) { state = 1; redraw(); }
				} else {
					if (authenticate()) {
						failed_count = 0;
						secure_wipe(password, sizeof(password));
						plen = 0;
						runsession();
						close_session();
						startx();
						creategreeter();
						redraw();
					} else {
						failed = 1;
						failed_count++;
						plen = 0;
						secure_wipe(password, sizeof(password));
						redraw();
						if (failed_count > 3) {
							sleep(2 + failed_count);
							failed = 0;
							failed_count = 0;
							state = 0;
							ulen = 0;
							redraw();
						}
					}
				}
			} else if (ks == XK_Escape) {
				state = 0;
				ulen = 0;
				plen = 0;
				failed = 0;
				secure_wipe(username, sizeof(username));
				secure_wipe(password, sizeof(password));
				redraw();
			} else if (ks == XK_BackSpace) {
				if (state == 0 && ulen > 0) username[--ulen] = 0;
				if (state == 1 && plen > 0) password[--plen] = 0;
				redraw();
			} else if (ks == XK_Tab || ks == XK_ISO_Left_Tab) {
				if (state == 0) {
					session_idx = (session_idx + 1) % n_sessions;
					redraw();
				}
			} else if (ks == XK_Left) {
				if (state == 0) {
					session_idx = (session_idx - 1 + n_sessions) % n_sessions;
					redraw();
				}
			} else if (ks == XK_Right) {
				if (state == 0) {
					session_idx = (session_idx + 1) % n_sessions;
					redraw();
				}
			} else if (n == 1 && isprint((unsigned char)tmp[0])) {
				if (state == 0 && ulen < (int)sizeof(username) - 1) {
					username[ulen++] = tmp[0];
					username[ulen] = 0;
				} else if (state == 1 && plen < (int)sizeof(password) - 1) {
					password[plen++] = tmp[0];
					password[plen] = 0;
				}
				redraw();
			}
		}
	}
	return 0;
}

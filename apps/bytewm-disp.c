/* bytewm-disp - monitor hotplug daemon (Windows-style external display takeover)
 *
 * Watches RandR for output hotplug events. When an external monitor connects,
 * the internal panel is turned off and the external becomes primary. When it
 * disconnects, the internal panel comes back. A fifo allows manual overrides:
 *
 *   /tmp/bytewm-disp.fifo  external  -> external only (default behavior)
 *                          both      -> internal + external together
 *                          internal  -> internal only
 *                          cycle     -> external <-> internal (native res)
 *                          status    -> print current mode to stderr
 *
 * Internal panel is identified by output name (eDP / LVDS / IDP prefix). Everything
 * else connected is treated as external. GPU backing (iGPU vs dGPU) is
 * irrelevant - RandR exposes outputs regardless of which GPU drives them.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/stat.h>

#define FIFO_PATH "/tmp/bytewm-disp.fifo"
#define LOG_PATH  "/tmp/bytewm-disp.log"
#define MAX_OUT  32

static FILE *logf = NULL;

static void
logmsg(const char *fmt, ...)
{
	if (!logf) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(logf, fmt, ap);
	va_end(ap);
	fflush(logf);
}

static Display *dpy;
static int rrev_base = 0;     /* RandR event base */
static int have_rr = 0;
static int curmode = 0;       /* 0=external, 1=both, 2=internal */
static char last_conn[MAX_OUT][64];  /* last known connection state per output */
static int last_nconn = 0;
static char known_internal[64] = ""; /* internal panel learned at startup */

/* true if the set of connected outputs changed since we last recorded it
   (hotplug). Pure mode/resolution changes (e.g. bytemenu picking a new res)
   must NOT trigger a re-apply or they'd be instantly reverted. */
static int
connection_changed(void)
{
	Window root = RootWindow(dpy, DefaultScreen(dpy));
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) return 0;

	char cur[MAX_OUT][64];
	int ncur = 0;
	for (int i = 0; i < res->noutput && ncur < MAX_OUT; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi) continue;
		if (oi->connection == RR_Connected) {
			int nlen = oi->nameLen;
			if (nlen < 0) nlen = 0;
			if (nlen > 63) nlen = 63;
			memcpy(cur[ncur], oi->name, (size_t)nlen);
			cur[ncur][nlen] = '\0';
			ncur++;
		}
		XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);

	int changed = (ncur != last_nconn);
	if (!changed) {
		for (int i = 0; i < ncur; i++)
			if (strcmp(cur[i], last_conn[i]) != 0) { changed = 1; break; }
	}

	if (changed) {
		for (int i = 0; i < ncur; i++)
			snprintf(last_conn[i], sizeof(last_conn[0]), "%s", cur[i]);
		last_nconn = ncur;
		logmsg("connection set changed: %d outputs\n", ncur);
		for (int i = 0; i < ncur; i++)
			logmsg("  connected: %s\n", cur[i]);
	}
	return changed;
}

static int
is_internal(const char *name)
{
	if (!name) return 0;
	/* if we learned the panel name at startup, that wins */
	if (known_internal[0] && !strcmp(known_internal, name))
		return 1;
	/* explicitly external connector types - never the internal panel */
	if (strncmp(name, "HDMI", 4) == 0 || strncmp(name, "DVI", 3) == 0 ||
	    strncmp(name, "VGA", 3) == 0 ||
	    strncmp(name, "DisplayPort", 11) == 0 ||
	    strncmp(name, "DP-", 3) == 0)
		return 0;
	/* laptop panels are usually eDP/LVDS/LCD/IDP; anything without an
	   external-connector prefix defaults to internal */
	return 1;
}

/* apply saved resolution (~/.config/bytewm/resolution: "OUTPUT MODE RATE")
   but only if the line is for OUT - resolutions are per-output so the
   laptop panel never inherits the external monitor's saved mode */
static void
apply_saved_resolution(const char *out)
{
	const char *home = getenv("HOME");
	if (!home || !out) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/.config/bytewm/resolution", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char sout[64] = "", mode[64] = "", rate[32] = "";
	if (fscanf(f, "%63s %63s %31s", sout, mode, rate) == 3 &&
	    !strcmp(sout, out) && mode[0]) {
		char cmd[512];
		snprintf(cmd, sizeof(cmd),
			"xrandr --output %.63s --mode %.63s --rate %.31s",
			out, mode, rate);
		if (system(cmd) == -1) { /* noop */ }
	}
	fclose(f);
}

static void
run_xrandr(const char *fmt, const char *name)
{
	char cmd[256];
	/* %.63s bounds the name so GCC knows it can't overflow cmd */
	snprintf(cmd, sizeof(cmd), fmt, name);
	int rc = system(cmd);
	if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
		logmsg("xrandr FAILED rc=%d: %s\n", rc, cmd);
}

/* active mode width of output NAME; 0 if not active/unknown */
static int
output_active_width(const char *name)
{
	if (!name || !name[0]) return 0;
	Window root = RootWindow(dpy, DefaultScreen(dpy));
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) return 0;
	int w = 0;
	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi) continue;
		if (oi->nameLen == (int)strlen(name) &&
		    memcmp(oi->name, name, (size_t)oi->nameLen) == 0 && oi->crtc) {
			XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
			if (ci && ci->mode) {
				for (int k = 0; k < res->nmode; k++)
					if (res->modes[k].id == ci->mode) {
						w = res->modes[k].width;
						break;
					}
			}
			if (ci) XRRFreeCrtcInfo(ci);
			break;
		}
		XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);
	return w;
}

/* append the full current RandR state to the log (diagnostics) */
static void
log_xrandr_state(void)
{
	FILE *p = popen("xrandr --current 2>&1", "r");
	if (!p) return;
	char buf[256];
	while (fgets(buf, sizeof(buf), p))
		logmsg("%s", buf);
	pclose(p);
}

/* internal panel name that should be active right now, or NULL */
static const char *
active_panel(void)
{
	return known_internal[0] ? known_internal : NULL;
}

/* current root window width (== xrandr screen width), fresh round trip */
static int
root_width(void)
{
	Window r = RootWindow(dpy, DefaultScreen(dpy));
	Window junkw;
	int x, y;
	unsigned int w, h, bw, depth;
	if (!XGetGeometry(dpy, r, &junkw, &x, &y, &w, &h, &bw, &depth))
		return 0;
	return (int)w;
}

static void
msleep(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

static void
learn_internal(void)
{
	/* if exactly one output is connected at startup (no external yet),
	   it must be the laptop panel - remember it as internal */
	Window root = RootWindow(dpy, DefaultScreen(dpy));
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) return;
	int nconnected = 0;
	char name[64] = "";
	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi) continue;
		if (oi->connection == RR_Connected) {
			int nlen = oi->nameLen;
			if (nlen < 0) nlen = 0;
			if (nlen > 63) nlen = 63;
			memcpy(name, oi->name, (size_t)nlen);
			name[nlen] = '\0';
			nconnected++;
		}
		XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);
	if (nconnected == 1 && name[0]) {
		snprintf(known_internal, sizeof(known_internal), "%s", name);
		logmsg("learned internal panel: %s\n", name);
	}
}

static void
apply_mode(void)
{
	Window root = RootWindow(dpy, DefaultScreen(dpy));
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) { fprintf(stderr, "bytewm-disp: XRRGetScreenResources failed\n"); return; }

	/* collect output names + connection. Also remember EVERY output
	   (connected or not): a disconnected external can keep a stale CRTC
	   with its mode, which holds the whole screen at that size - it must
	   be explicitly turned off by name. */
	char internal[MAX_OUT][64];
	char external[MAX_OUT][64];
	char allouts[MAX_OUT][64];
	int ninternal = 0, nexternal = 0, nall = 0;

	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi) continue;
		/* bound the copy by namelen so GCC knows the name is short */
		int nlen = oi->nameLen;
		if (nlen < 0) nlen = 0;
		if (nlen > 63) nlen = 63;
		if (nall < MAX_OUT) {
			memcpy(allouts[nall], oi->name, (size_t)nlen);
			allouts[nall][nlen] = '\0';
			nall++;
		}
		if (oi->connection == RR_Connected) {
			if (ninternal < MAX_OUT && is_internal(oi->name)) {
				memcpy(internal[ninternal], oi->name, (size_t)nlen);
				internal[ninternal][nlen] = '\0';
				ninternal++;
			} else if (nexternal < MAX_OUT) {
				memcpy(external[nexternal], oi->name, (size_t)nlen);
				external[nexternal][nlen] = '\0';
				nexternal++;
			}
		}
		XRRFreeOutputInfo(oi);
	}

 	logmsg("apply_mode: internal=%d external=%d mode=%d\n",
 		ninternal, nexternal, curmode);
 	for (int i = 0; i < ninternal; i++)
 		logmsg("  internal: %s\n", internal[i]);
 	for (int i = 0; i < nexternal; i++)
 		logmsg("  external: %s\n", external[i]);
 	for (int i = 0; i < nall; i++)
 		logmsg("  all: %s\n", allouts[i]);

 	int want_both = (curmode == 1);
 	int want_internal_only = (curmode == 2);
 	int have_external = (nexternal > 0);
 	const char *primary = NULL;
 	int primary_is_internal = 0;

 	/* decide what to light up */
	if (want_internal_only || (!have_external && !want_both)) {
		/* internal only (or no external present) */
		for (int i = 0; i < ninternal; i++) {
			run_xrandr("xrandr --output %.63s --auto --primary", internal[i]);
			apply_saved_resolution(internal[i]);
			if (!primary) { primary = internal[i]; primary_is_internal = 1; }
		}
		/* if the panel is momentarily not reported as connected (driver
		   reconfiguration during the unplug), force it on anyway - we
		   know its name from startup */
		if (ninternal == 0 && known_internal[0]) {
			run_xrandr("xrandr --output %.63s --auto --primary", known_internal);
			apply_saved_resolution(known_internal);
			primary = known_internal;
			primary_is_internal = 1;
		}
		/* turn off EVERY external output by name - including
		   disconnected ones that still hold a stale CRTC, which keeps
		   the screen/framebuffer at the external monitor's size */
		for (int i = 0; i < nall; i++)
			if (!is_internal(allouts[i]))
				run_xrandr("xrandr --output %.63s --off", allouts[i]);
 	} else if (want_both) {
 		/* both on; external primary */
 		for (int i = 0; i < nexternal; i++) {
 			run_xrandr("xrandr --output %.63s --auto --primary", external[i]);
 			if (!primary) primary = external[i];
 		}
 		for (int i = 0; i < ninternal; i++)
 			run_xrandr("xrandr --output %.63s --auto", internal[i]);
 	} else {
 		/* external only (default) */
 		for (int i = 0; i < nexternal; i++) {
 			run_xrandr("xrandr --output %.63s --auto --primary", external[i]);
 			if (!primary) primary = external[i];
 		}
 		for (int i = 0; i < nall; i++)
 			if (is_internal(allouts[i]))
 				run_xrandr("xrandr --output %.63s --off", allouts[i]);
 	}

 	/* never force the saved (external) resolution onto the laptop panel -
 	   the internal display uses its native mode via --auto above */
 	if (primary && !primary_is_internal)
 		apply_saved_resolution(primary);

 	XRRFreeScreenResources(res);

 	/* verify + self-heal: after an unplug the modesetting driver can
 	   leave the screen/framebuffer stuck at the external monitor's
 	   size (desktop stays 1440p, resolution menu breaks). Detect the
 	   mismatch between the screen size and the panel's active mode and
 	   force a full CRTC reset until they agree. */
	if (primary_is_internal) {
		const char *panel = active_panel();
		if (panel) {
			int pw = output_active_width(panel);
			int sw = root_width();
			for (int attempt = 0; attempt < 3 && pw > 0 && sw > pw; attempt++) {
				logmsg("WARN: screen=%dpx panel=%dpx (attempt %d) - forcing reset\n",
					sw, pw, attempt + 1);
				msleep(400);
				/* release every external CRTC first (a disconnected
				   output can keep its stale CRTC and hold the screen
				   size), then bounce the panel */
				for (int i = 0; i < nall; i++)
					if (!is_internal(allouts[i]))
						run_xrandr("xrandr --output %.63s --off", allouts[i]);
				msleep(400);
				run_xrandr("xrandr --output %.63s --off", panel);
				msleep(400);
				run_xrandr("xrandr --output %.63s --auto --primary", panel);
				apply_saved_resolution(panel);
				pw = output_active_width(panel);
				sw = root_width();
			}
			if (pw > 0 && sw > pw) {
				logmsg("FAILED to restore screen size: %d != %d\n", sw, pw);
				log_xrandr_state();
			} else {
				logmsg("verify ok: screen=%dpx panel=%dpx\n", sw, pw);
			}
		}
	}
}

static void
handle_command(const char *buf)
{
	if (!strcmp(buf, "external")) curmode = 0;
	else if (!strcmp(buf, "both"))   curmode = 1;
	else if (!strcmp(buf, "internal")) curmode = 2;
	else if (!strcmp(buf, "cycle")) curmode = (curmode == 0) ? 2 : 0;
	else if (!strcmp(buf, "status")) {
		fprintf(stderr, "bytewm-disp: mode=%s\n",
			curmode == 0 ? "external" : curmode == 1 ? "both" : "internal");
		return;
	} else {
		return;
	}
	apply_mode();
}

int
main(void)
{
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "bytewm-disp: could not open display\n");
		return 1;
	}
	Window root = RootWindow(dpy, DefaultScreen(dpy));

	int evb, errb, majv, minv;
	if (!XRRQueryExtension(dpy, &evb, &errb) ||
	    !XRRQueryVersion(dpy, &majv, &minv)) {
		fprintf(stderr, "bytewm-disp: RandR not available\n");
		XCloseDisplay(dpy);
		return 1;
	}
	rrev_base = evb;
	have_rr = 1;

	/* subscribe to output-change events */
	XRRSelectInput(dpy, root,
		RROutputChangeNotifyMask | RRScreenChangeNotifyMask);

	/* file log (stderr is lost when started from autostart) */
	logf = fopen(LOG_PATH, "a");
	if (logf) {
		time_t t = time(NULL);
		char ts[64];
		strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&t));
		logmsg("--- bytewm-disp start %s ---\n", ts);
	}

	/* initial application of policy */
	connection_changed();   /* seed the recorded connection state */
	learn_internal();
	apply_mode();

	/* open fifo (create if missing) */
	if (access(FIFO_PATH, F_OK) != 0)
		mkfifo(FIFO_PATH, 0644);
	int fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "bytewm-disp: cannot open %s: %s\n",
			FIFO_PATH, strerror(errno));
		XCloseDisplay(dpy);
		return 1;
	}

	fd_set fds;
	int xfd = ConnectionNumber(dpy);
	while (1) {
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		FD_SET(fd, &fds);
		int nfds = (fd > xfd ? fd : xfd) + 1;
		/* 1.5s timeout: periodic re-check as a safety net. If a hotplug
		   event is missed (some drivers only send a screen-size change,
		   or the event races the connection state update), the poll
		   still re-applies the correct mode. connection_changed() makes
		   sure pure resolution changes are never reverted. */
		struct timeval tv = { 1, 500000 };
		int n = select(nfds, &fds, NULL, NULL, &tv);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}

		if (FD_ISSET(xfd, &fds)) {
			while (XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);
				if (!have_rr) continue;
				if (ev.type == rrev_base + RRNotify) {
					XRRNotifyEvent *rne = (XRRNotifyEvent *)&ev;
					logmsg("event: RRNotify subtype=%d\n", rne->subtype);
					XRRUpdateConfiguration(&ev);
					if (connection_changed())
						apply_mode();
				} else if (ev.type == rrev_base + RRScreenChangeNotify) {
					logmsg("event: RRScreenChangeNotify\n");
					XRRUpdateConfiguration(&ev);
					if (connection_changed())
						apply_mode();
				}
			}
		}

		if (FD_ISSET(fd, &fds)) {
			char cmdbuf[256];
			ssize_t nrr = read(fd, cmdbuf, sizeof(cmdbuf) - 1);
			if (nrr > 0) {
				cmdbuf[nrr] = '\0';
				/* one command per line */
				char *save = NULL;
				for (char *tok = strtok_r(cmdbuf, "\n", &save);
				     tok; tok = strtok_r(NULL, "\n", &save)) {
					if (*tok) handle_command(tok);
				}
			}
		}

		/* periodic self-heal: re-apply whenever the set of connected
		   outputs changed since the last check, even if no RandR event
		   was ever delivered */
		if (n == 0 && connection_changed()) {
			logmsg("poll detected connection change\n");
			apply_mode();
		}
	}

	close(fd);
	XCloseDisplay(dpy);
	return 0;
}

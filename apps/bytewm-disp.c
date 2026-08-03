/* bytewm-disp - monitor hotplug daemon (Windows-style external display takeover)
 *
 * Watches RandR for output hotplug events. When an external monitor connects,
 * the internal panel is turned off and the external becomes primary. When it
 * disconnects, the internal panel comes back. A fifo allows manual overrides:
 *
 *   /tmp/bytewm-disp.fifo  external  -> external only (default behavior)
 *                          both      -> internal + external together
 *                          internal  -> internal only
 *                          cycle     -> external -> both -> external
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
#include <sys/select.h>
#include <sys/stat.h>

#define FIFO_PATH "/tmp/bytewm-disp.fifo"
#define MAX_OUT  32

static Display *dpy;
static int rrev_base = 0;     /* RandR event base */
static int have_rr = 0;
static int curmode = 0;       /* 0=external, 1=both, 2=internal */

static int
is_internal(const char *name)
{
	return name &&
		(strncmp(name, "eDP", 3) == 0 ||
		 strncmp(name, "EDP", 3) == 0 ||
		 strncmp(name, "LVDS", 4) == 0 ||
		 strncmp(name, "IDP", 3) == 0);
}

/* apply saved resolution (~/.config/bytewm/resolution: "MODE RATE") to OUT */
static void
apply_saved_resolution(const char *out)
{
	const char *home = getenv("HOME");
	if (!home || !out) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/.config/bytewm/resolution", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char mode[64] = "", rate[32] = "";
	if (fscanf(f, "%63s %31s", mode, rate) == 2 && mode[0]) {
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
	if (system(cmd) == -1) { /* noop */ }
}

static void
apply_mode(void)
{
	Window root = RootWindow(dpy, DefaultScreen(dpy));
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) { fprintf(stderr, "bytewm-disp: XRRGetScreenResources failed\n"); return; }

	/* collect output names + connection */
	char internal[MAX_OUT][64];
	char external[MAX_OUT][64];
	int ninternal = 0, nexternal = 0;

	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi) continue;
		if (oi->connection == RR_Connected) {
			/* bound the copy by namelen so GCC knows the name is short */
			int nlen = oi->nameLen;
			if (nlen < 0) nlen = 0;
			if (nlen > 63) nlen = 63;
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

	fprintf(stderr, "bytewm-disp: connected internal=%d external=%d\n",
		ninternal, nexternal);
	for (int i = 0; i < ninternal; i++)
		fprintf(stderr, "  internal: %s\n", internal[i]);
	for (int i = 0; i < nexternal; i++)
		fprintf(stderr, "  external: %s\n", external[i]);

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
 			if (!primary) { primary = internal[i]; primary_is_internal = 1; }
 		}
 		for (int i = 0; i < nexternal; i++)
 			run_xrandr("xrandr --output %.63s --off", external[i]);
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
 		for (int i = 0; i < ninternal; i++)
 			run_xrandr("xrandr --output %.63s --off", internal[i]);
 	}

 	/* never force the saved (external) resolution onto the laptop panel -
	   the internal display uses its native mode via --auto above */
 	if (primary && !primary_is_internal)
 		apply_saved_resolution(primary);

 	XRRFreeScreenResources(res);
 }

static void
handle_command(const char *buf)
{
	if (!strcmp(buf, "external")) curmode = 0;
	else if (!strcmp(buf, "both"))   curmode = 1;
	else if (!strcmp(buf, "internal")) curmode = 2;
	else if (!strcmp(buf, "cycle")) curmode = (curmode == 0) ? 1 : 0;
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

	/* initial application of policy */
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
		int n = select(nfds, &fds, NULL, NULL, NULL);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}

		if (FD_ISSET(xfd, &fds)) {
			while (XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);
				if (have_rr && ev.type == rrev_base + RRNotify) {
					XRRUpdateConfiguration(&ev);
					XRRNotifyEvent *rne = (XRRNotifyEvent *)&ev;
					if (rne->subtype == RRNotify_OutputChange)
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
	}

	close(fd);
	XCloseDisplay(dpy);
	return 0;
}

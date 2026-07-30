#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#define MAX_ITEMS  24
#define MAX_GAMES  256
#define PER_PAGE   12
#define PAGE_COL   6
#define ITEM_H     30
#define WIN_W      420
#define GAME_W     520
#define BORDER_W   2

static Display *dpy;
static Window root, win;
static GC gc;
static XFontStruct *xfont;
static int sw, sh, win_h;
static unsigned long c_bg, c_fg, c_hi, c_border, c_dim;

static char *labels[MAX_ITEMS], *commands[MAX_ITEMS];
static int count, sel;

static int submenu;
static char *game_labels[MAX_GAMES];
static char *game_exe[MAX_GAMES];
static char *game_proton[MAX_GAMES];
static char *game_extra[MAX_GAMES];
static char *game_commands[MAX_GAMES];
static char game_cmd_buf[MAX_GAMES][4096];
static int game_count, game_sel, game_col, game_page, game_page_max;

static char lockpath[256];
static int lockacquired;

static void cleanup_lock(void) {
	if (lockacquired) {
		char pidfile[512];
		snprintf(pidfile, sizeof(pidfile), "%s/pid", lockpath);
		unlink(pidfile);
		rmdir(lockpath);
		lockacquired = 0;
	}
}

static void sigcleanup(int unused) {
	(void)unused;
	cleanup_lock();
	_exit(1);
}

static unsigned long getcol(const char *s) {
	XColor xc;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
	XParseColor(dpy, cmap, s, &xc);
	XAllocColor(dpy, cmap, &xc);
	return xc.pixel;
}

static void sort_games(void) {
	for (int i = 0; i < game_count - 1; i++) {
		for (int j = i + 1; j < game_count; j++) {
			if (strcmp(game_labels[i], game_labels[j]) > 0) {
				char *tl = game_labels[i]; game_labels[i] = game_labels[j]; game_labels[j] = tl;
				char *te = game_exe[i]; game_exe[i] = game_exe[j]; game_exe[j] = te;
				char *tp = game_proton[i]; game_proton[i] = game_proton[j]; game_proton[j] = tp;
				char *tx = game_extra[i]; game_extra[i] = game_extra[j]; game_extra[j] = tx;
				char *tc = game_commands[i]; game_commands[i] = game_commands[j]; game_commands[j] = tc;
			}
		}
	}
}

static void build_game_command(int idx) {
	char *home = getenv("HOME");
	if (!home) return;

	char wineprefix[1024];
	char exe[1024];
	char proton[1024];

	snprintf(exe, sizeof(exe), "%s", game_exe[idx]);
	snprintf(proton, sizeof(proton), "%s", game_proton[idx]);

	/* extract parent dir from exe path for WINEPREFIX */
	char *last_slash = strrchr(exe, '/');
	if (last_slash) {
		int len = last_slash - exe;
		strncpy(wineprefix, exe, len);
		wineprefix[len] = '\0';
	} else {
		snprintf(wineprefix, sizeof(wineprefix), "%s/Games/default", home);
	}

	/* expand ~ to $HOME */
	char exe_expanded[1024];
	char proton_expanded[1024];
	char prefix_expanded[1024];

	if (exe[0] == '~' && exe[1] == '/')
		snprintf(exe_expanded, sizeof(exe_expanded), "%s%s", home, exe + 1);
	else
		snprintf(exe_expanded, sizeof(exe_expanded), "%s", exe);

	if (proton[0] == '~' && proton[1] == '/')
		snprintf(proton_expanded, sizeof(proton_expanded), "%s%s", home, proton + 1);
	else
		snprintf(proton_expanded, sizeof(proton_expanded), "%s", proton);

	if (wineprefix[0] == '~' && wineprefix[1] == '/')
		snprintf(prefix_expanded, sizeof(prefix_expanded), "%s%s", home, wineprefix + 1);
	else
		snprintf(prefix_expanded, sizeof(prefix_expanded), "%s", wineprefix);

	snprintf(game_cmd_buf[idx], 4096,
		"%s WINEPREFIX=%s PROTONPATH=%s umu-run %s",
		game_extra[idx] ? game_extra[idx] : "",
		prefix_expanded, proton_expanded, exe_expanded);
	game_commands[idx] = game_cmd_buf[idx];
}

static void read_games_config(void) {
	char *home = getenv("HOME");
	if (!home) return;
	char path[1024];
	snprintf(path, sizeof(path), "%s/.config/bytemenu/games.conf", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f) && game_count < MAX_GAMES) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		char *p = strchr(line, '|');
		if (!p) continue;
		*p++ = 0;
		char *exe_path = strchr(p, '|');
		if (!exe_path) continue;
		*exe_path++ = 0;
		char *proton_path = strchr(exe_path, '|');
		char *extra = NULL;
		if (!proton_path) {
			/* 3-field format: label|exe|proton_path */
			proton_path = exe_path;
		} else {
			*proton_path++ = 0;
			char *ep = strchr(proton_path, '|');
			if (ep) {
				*ep++ = 0;
				while (*ep == ' ') ep++;
				if (*ep) extra = ep;
			}
		}
		/* trim spaces */
		while (*p == ' ') p++;
		while (*exe_path == ' ') exe_path++;
		while (*proton_path == ' ') proton_path++;
		if (!line[0] || !*p || !*exe_path) continue;
		game_labels[game_count] = strdup(line);
		game_exe[game_count] = strdup(p);
		game_proton[game_count] = strdup(proton_path);
		game_extra[game_count] = extra ? strdup(extra) : NULL;
		game_commands[game_count] = game_cmd_buf[game_count];
		if (!game_labels[game_count] || !game_exe[game_count] ||
		    !game_proton[game_count]) {
			free(game_labels[game_count]);
			free(game_exe[game_count]);
			free(game_proton[game_count]);
			continue;
		}
		build_game_command(game_count);
		game_count++;
	}
	fclose(f);
	sort_games();
	game_page_max = game_count > PER_PAGE ? (game_count - 1) / PER_PAGE : 0;
}

static void draw_main(void) {
	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, WIN_W, win_h);

	const char *hdr = "b y t e m e n u";
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
			XDrawString(dpy, win, gc, 12, y + 20, ">", 1);
		}
		XSetForeground(dpy, gc, i == sel ? c_hi : c_fg);
		XDrawString(dpy, win, gc, 28, y + 20, labels[i], strlen(labels[i]));
		y += ITEM_H;
	}

	const char *ftr = "j/k  enter  esc";
	tw = XTextWidth(xfont, ftr, strlen(ftr));
	XSetForeground(dpy, gc, c_dim);
	XDrawString(dpy, win, gc, (WIN_W - tw)/2, win_h - 20, ftr, strlen(ftr));

	XSync(dpy, False);
}

static void draw_games(void) {
	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, GAME_W, win_h);

	const char *hdr = "b y t e g a m e s";
	int tw = XTextWidth(xfont, hdr, strlen(hdr));
	XSetForeground(dpy, gc, c_dim);
	XDrawString(dpy, win, gc, (GAME_W - tw) / 2, 28, hdr, strlen(hdr));

	XSetForeground(dpy, gc, c_border);
	XFillRectangle(dpy, win, gc, 20, 42, GAME_W - 40, 2);
	XFillRectangle(dpy, win, gc, 0, 0, GAME_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, win_h - BORDER_W, GAME_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, 0, BORDER_W, win_h);
	XFillRectangle(dpy, win, gc, GAME_W - BORDER_W, 0, BORDER_W, win_h);

	/* draw column separator - spans exactly the items area */
	XSetForeground(dpy, gc, c_border);
	XFillRectangle(dpy, win, gc, GAME_W / 2 - 1, 56, 2, PAGE_COL * ITEM_H);

	/* calculate which games to show */
	int start = game_page * PER_PAGE;
	int left_count = 0, right_count = 0;
	for (int i = 0; i < PER_PAGE && (start + i) < game_count; i++) {
		if (i < PAGE_COL) left_count++;
		else right_count++;
	}

	/* draw left column */
	int y = 56;
	for (int i = 0; i < left_count; i++) {
		int idx = start + i;
		int is_sel = (game_col == 0 && game_sel == i);
		if (is_sel) {
			XSetForeground(dpy, gc, c_hi);
			XDrawString(dpy, win, gc, 12, y + 20, ">", 1);
		}
		XSetForeground(dpy, gc, is_sel ? c_hi : c_fg);
		XDrawString(dpy, win, gc, 28, y + 20, game_labels[idx], strlen(game_labels[idx]));
		y += ITEM_H;
	}

	/* draw right column */
	y = 56;
	for (int i = 0; i < right_count; i++) {
		int idx = start + PAGE_COL + i;
		int is_sel = (game_col == 1 && game_sel == i);
		if (is_sel) {
			XSetForeground(dpy, gc, c_hi);
			XDrawString(dpy, win, gc, GAME_W / 2 + 12, y + 20, ">", 1);
		}
		XSetForeground(dpy, gc, is_sel ? c_hi : c_fg);
		XDrawString(dpy, win, gc, GAME_W / 2 + 28, y + 20, game_labels[idx], strlen(game_labels[idx]));
		y += ITEM_H;
	}

	/* page indicator */
	if (game_page_max > 0) {
		char page_str[32];
		snprintf(page_str, sizeof(page_str), "[%d/%d]", game_page + 1, game_page_max + 1);
		int page_tw = XTextWidth(xfont, page_str, strlen(page_str));
		XSetForeground(dpy, gc, c_dim);
		XDrawString(dpy, win, gc, GAME_W - page_tw - 12, win_h - 20, page_str, strlen(page_str));
	}

	XSync(dpy, False);
}

static void draw(void) {
	if (submenu)
		draw_games();
	else
		draw_main();
}

static void read_config(void) {
	char *home = getenv("HOME");
	if (!home) return;
	char path[1024];
	snprintf(path, sizeof(path), "%s/.config/bytemenu/menu.conf", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f) && count < MAX_ITEMS) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		char *p = strchr(line, '|');
		if (!p) continue;
		*p++ = 0;
		while (*p == ' ') p++;
		if (!line[0] || !*p) continue;
		labels[count] = strdup(line);
		commands[count] = strdup(p);
		count++;
	}
	fclose(f);
}

static void launch(void) {
	if (!commands[sel]) return;
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", commands[sel], NULL);
		_exit(1);
	}
}

static void launch_game(int idx) {
	if (idx < 0 || idx >= game_count || !game_commands[idx]) return;
	if (fork() == 0) {
		setsid();
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
		execl("/bin/sh", "/bin/sh", "-c", game_commands[idx], NULL);
		_exit(1);
	}
}

int main(void) {
	snprintf(lockpath, sizeof(lockpath), "/tmp/bytemenu-%u.lock",
	         (unsigned int)getuid());
	if (mkdir(lockpath, 0700) != 0) {
		if (errno == EEXIST) {
			char pidfile[512];
			snprintf(pidfile, sizeof(pidfile), "%s/pid", lockpath);
			FILE *pf = fopen(pidfile, "r");
			if (pf) {
				pid_t oldpid = 0;
				if (fscanf(pf, "%d", &oldpid) == 1 && oldpid > 0
				    && kill(oldpid, 0) != 0 && errno == ESRCH) {
					fclose(pf);
					unlink(pidfile);
					rmdir(lockpath);
					if (mkdir(lockpath, 0700) != 0) return 0;
				} else {
					fclose(pf);
					return 0;
				}
			} else {
				return 0;
			}
		} else {
			return 0;
		}
	}
	lockacquired = 1;
	{
		char pidfile[512];
		snprintf(pidfile, sizeof(pidfile), "%s/pid", lockpath);
		FILE *pf = fopen(pidfile, "w");
		if (pf) {
			fprintf(pf, "%d\n", (int)getpid());
			fclose(pf);
		}
	}
	atexit(cleanup_lock);
	signal(SIGTERM, sigcleanup);
	signal(SIGINT, sigcleanup);
	signal(SIGHUP, sigcleanup);
	signal(SIGQUIT, sigcleanup);
	signal(SIGPIPE, sigcleanup);

	read_config();
	if (!count) {
		labels[0] = strdup("Terminal");   commands[0] = strdup("st");
		labels[1] = strdup("Browser");    commands[1] = strdup("firefox");
		labels[2] = strdup("Files");      commands[2] = strdup("st -e ranger");
		count = 3;
	}

	read_games_config();
	if (game_count > 0) {
		labels[count] = strdup("Games");
		commands[count] = strdup("__GAMES__");
		count++;
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) return 1;
	int scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	sw = DisplayWidth(dpy, scr);
	sh = DisplayHeight(dpy, scr);

	xfont = XLoadQueryFont(dpy, "fixed");
	if (!xfont) return 1;

	c_bg     = getcol("#282828");
	c_fg     = getcol("#ebdbb2");
	c_hi     = getcol("#d65d0e");
	c_border = getcol("#689d6a");
	c_dim    = getcol("#a89984");

	win_h = 44 + count * ITEM_H + 34;
	if (win_h < 200) win_h = 200;
	if (win_h > sh - 40) win_h = sh - 40;

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = c_bg,
		.event_mask = ExposureMask | KeyPressMask
	};
	win = XCreateWindow(dpy, root,
		(sw - WIN_W) / 2, (sh - win_h) / 2,
		WIN_W, win_h, 0,
		DefaultDepth(dpy, scr), CopyFromParent,
		DefaultVisual(dpy, scr),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	gc = XCreateGC(dpy, root, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);

	XMapRaised(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	XGrabPointer(dpy, win, True,
		ButtonPressMask, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime);

	XEvent ev;
	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			draw();
		else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);

			if (submenu) {
				/* games submenu */
				int page_count = game_count - game_page * PER_PAGE;
				if (page_count > PER_PAGE) page_count = PER_PAGE;
				int left_count = page_count < PAGE_COL ? page_count : PAGE_COL;
				int right_count = page_count - left_count;

				if (ks == XK_Escape || ks == XK_q) {
					submenu = 0;
					/* resize window back to main menu size */
					win_h = 44 + count * ITEM_H + 28;
					if (win_h < 200) win_h = 200;
					if (win_h > sh - 40) win_h = sh - 40;
					XMoveResizeWindow(dpy, win,
						(sw - WIN_W) / 2, (sh - win_h) / 2,
						WIN_W, win_h);
					draw();
					continue;
				}
				if (ks == XK_Return) {
					int idx = game_page * PER_PAGE + game_col * PAGE_COL + game_sel;
					if (idx < game_count) {
						launch_game(idx);
						break;
					}
				}
				if (ks == XK_j || ks == XK_Down) {
					int max_sel = game_col == 0 ? left_count - 1 : right_count - 1;
					if (max_sel < 0) max_sel = 0;
					if (game_sel < max_sel) { game_sel++; draw(); }
				}
				if (ks == XK_k || ks == XK_Up) {
					if (game_sel > 0) { game_sel--; draw(); }
				}
				if (ks == XK_l || ks == XK_Right) {
					if (right_count > 0) {
						game_col = 1;
						if (game_sel >= right_count) game_sel = right_count - 1;
						if (game_sel < 0) game_sel = 0;
						draw();
					}
				}
				if (ks == XK_h || ks == XK_Left) {
					if (game_col == 1) {
						game_col = 0;
						if (game_sel >= left_count) game_sel = left_count - 1;
						if (game_sel < 0) game_sel = 0;
						draw();
					}
				}
				if (ks == XK_bracketright || ks == XK_Tab) {
					if (game_page < game_page_max) {
						game_page++;
						game_col = 0;
						game_sel = 0;
						draw();
					}
				}
				if (ks == XK_bracketleft) {
					if (game_page > 0) {
						game_page--;
						game_col = 0;
						game_sel = 0;
						draw();
					}
				}
			} else {
				/* main menu */
				if (ks == XK_Escape || ks == XK_q) break;
				if (ks == XK_Return) {
					if (!strcmp(commands[sel], "__GAMES__")) {
						submenu = 1;
						game_col = 0;
						game_sel = 0;
						game_page = 0;

					/* resize window for games submenu */
					win_h = 44 + PAGE_COL * ITEM_H + 28;
					XMoveResizeWindow(dpy, win,
						(sw - GAME_W) / 2, (sh - win_h) / 2,
						GAME_W, win_h);
						draw();
					} else {
						launch();
						break;
					}
				}
				if ((ks == XK_j || ks == XK_Down) && sel < count-1) { sel++; draw(); }
				if ((ks == XK_k || ks == XK_Up)   && sel > 0)        { sel--; draw(); }
			}
		}
	}

	XUngrabPointer(dpy, CurrentTime);
	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	for (int i = 0; i < count; i++) { free(labels[i]); free(commands[i]); }
	for (int i = 0; i < game_count; i++) {
		free(game_labels[i]);
		free(game_exe[i]);
		free(game_proton[i]);
	}
	return 0;
}

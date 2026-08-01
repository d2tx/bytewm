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
#include <ctype.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include "appfont.h"

#define MAX_CATS     8
#define MAX_SUBITEMS 256
#define MAX_NAV      16
#define PAPER_W      432
#define PAPER_H      540
#define BORDER_W     2

static Display *dpy;
static Window root, win;
static GC gc;
static AppFont *afont;
static int sw, sh, win_h;
static int itemh, per_page;
static unsigned long c_bg, c_fg, c_hi, c_border, c_dim, c_field;
static XftColor fc_fg, fc_hi, fc_dim;
static int cursor_on = 1;

static long
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

struct submenu {
	char *label;
	char title[64];
	char path[1024];
	char *labels[MAX_SUBITEMS];
	char *commands[MAX_SUBITEMS];   /* NULL -> child submenu */
	struct submenu *children[MAX_SUBITEMS];
	int count;
	int sel, page, page_max;
	int games;
	int generated;                  /* 1 -> run gen_cmd before showing */
	char *gen_cmd;
	char filter[64];                /* active search text */
	int fmap[MAX_SUBITEMS];         /* displayed index -> real index */
	int fmatch;                     /* number of matching items */
};

struct catdef {
	const char *label;
	const char *title;
	const char *file;
	int games;
};

static const struct catdef catdefs[] = {
	{ "Softwares", "s o f t w a r e s", "softwares.conf", 0 },
	{ "Games",     "g a m e s",         "games.conf",     1 },
	{ "Emulators", "e m u l a t o r s",  "emulators.conf", 0 },
	{ "Settings",  "s e t t i n g s",   "settings.conf",  0 },
};

static struct submenu cats[MAX_CATS];
static int cat_count;
static struct submenu mainmenu;
static struct submenu *nav[MAX_NAV];
static int nav_depth;

static char *game_exe[MAX_SUBITEMS];
static char *game_proton[MAX_SUBITEMS];
static char *game_extra[MAX_SUBITEMS];
static char game_cmd_buf[MAX_SUBITEMS][4096];

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

static void spaced_title(char *out, size_t n, const char *s) {
	size_t o = 0;
	for (const char *p = s; *p && o + 2 < n; p++) {
		char c = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
		out[o++] = c;
		if (p[1] && o + 2 < n) out[o++] = ' ';
	}
	out[o] = '\0';
}

static struct submenu *new_submenu(const char *label) {
	struct submenu *s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	s->label = strdup(label);
	spaced_title(s->title, sizeof(s->title), label);
	return s;
}

static int contains_ci(const char *h, const char *n) {
	for (; *h; h++) {
		const char *a = h, *b = n;
		while (*a && *b) {
			char x = *a; if (x >= 'A' && x <= 'Z') x += 32;
			char y = *b; if (y >= 'A' && y <= 'Z') y += 32;
			if (x != y) break;
			a++; b++;
		}
		if (!*b) return 1;
	}
	return 0;
}

static void recompute_filter(struct submenu *s) {
	s->fmatch = 0;
	if (!s->filter[0]) {
		for (int i = 0; i < s->count; i++) s->fmap[i] = i;
		s->fmatch = s->count;
	} else {
		for (int i = 0; i < s->count && s->fmatch < MAX_SUBITEMS; i++)
			if (contains_ci(s->labels[i], s->filter))
				s->fmap[s->fmatch++] = i;
	}
	s->page_max = s->fmatch > per_page ? (s->fmatch - 1) / per_page : 0;
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
}

static void sort_games(struct submenu *s) {
	for (int i = 0; i < s->count - 1; i++) {
		for (int j = i + 1; j < s->count; j++) {
			if (strcmp(s->labels[i], s->labels[j]) > 0) {
				char *t;
				t = s->labels[i]; s->labels[i] = s->labels[j]; s->labels[j] = t;
				t = s->commands[i]; s->commands[i] = s->commands[j]; s->commands[j] = t;
				t = game_exe[i]; game_exe[i] = game_exe[j]; game_exe[j] = t;
				t = game_proton[i]; game_proton[i] = game_proton[j]; game_proton[j] = t;
				t = game_extra[i]; game_extra[i] = game_extra[j]; game_extra[j] = t;
			}
		}
	}
}

/* parse "label|command" lines from f into s; __GEN__ items become child submenus */
static void parse_items(struct submenu *s, FILE *f) {
	char line[512];
	while (fgets(line, sizeof(line), f) && s->count < MAX_SUBITEMS) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		char *p = strchr(line, '|');
		if (!p) continue;
		*p++ = 0;
		/* trim trailing whitespace from the label */
		{
			size_t llen = strlen(line);
			while (llen > 0 && (line[llen-1] == ' ' || line[llen-1] == '\t'))
				line[--llen] = 0;
		}
		while (*p == ' ') p++;
		if (!line[0] || !*p) continue;
		s->labels[s->count] = strdup(line);
		if (!strncmp(p, "__GEN__ ", 8)) {
			s->commands[s->count] = NULL;
			struct submenu *c = new_submenu(line);
			if (c) {
				c->gen_cmd = strdup(p + 8);
				c->generated = 1;
			}
			s->children[s->count] = c;
		} else {
			s->commands[s->count] = strdup(p);
			s->children[s->count] = NULL;
		}
		s->count++;
	}
}

static void load_simple(struct submenu *s) {
	FILE *f = fopen(s->path, "r");
	if (!f) return;
	parse_items(s, f);
	fclose(f);
	s->page_max = s->count > per_page ? (s->count - 1) / per_page : 0;
}

static void load_games(struct submenu *s) {
	FILE *f = fopen(s->path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f) && s->count < MAX_SUBITEMS) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		char *p = strchr(line, '|');
		if (!p) continue;
		*p++ = 0;
		/* trim trailing whitespace from the label */
		{
			size_t llen = strlen(line);
			while (llen > 0 && (line[llen-1] == ' ' || line[llen-1] == '\t'))
				line[--llen] = 0;
		}
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
		int idx = s->count;
		s->labels[idx] = strdup(line);
		game_exe[idx] = strdup(p);
		game_proton[idx] = strdup(proton_path);
		game_extra[idx] = extra ? strdup(extra) : NULL;
		s->commands[idx] = game_cmd_buf[idx];
		s->children[idx] = NULL;
		if (!s->labels[idx] || !game_exe[idx] || !game_proton[idx]) {
			free(s->labels[idx]);
			free(game_exe[idx]);
			free(game_proton[idx]);
			free(game_extra[idx]);
			continue;
		}
		build_game_command(idx);
		s->count++;
	}
	fclose(f);
	sort_games(s);
	s->page_max = s->count > per_page ? (s->count - 1) / per_page : 0;
}

static void build_categories(void) {
	char *home = getenv("HOME");
	if (!home) return;
	for (int i = 0; i < (int)(sizeof(catdefs) / sizeof(catdefs[0])); i++) {
		struct submenu *s = &cats[cat_count];
		snprintf(s->path, sizeof(s->path), "%s/.config/bytemenu/%s", home, catdefs[i].file);
		struct stat st;
		if (stat(s->path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		s->label = strdup(catdefs[i].label);
		snprintf(s->title, sizeof(s->title), "%s", catdefs[i].title);
		s->games = catdefs[i].games;
		cat_count++;
	}
}

static void load_categories(void) {
	for (int i = 0; i < cat_count; i++) {
		if (cats[i].games) load_games(&cats[i]);
		else load_simple(&cats[i]);
	}
}

static void build_main_menu(void) {
	memset(&mainmenu, 0, sizeof(mainmenu));
	snprintf(mainmenu.title, sizeof(mainmenu.title), "b y t e m e n u");
	if (cat_count > 0) {
		for (int i = 0; i < cat_count; i++) {
			mainmenu.labels[i] = cats[i].label;
			mainmenu.commands[i] = NULL;
			mainmenu.children[i] = &cats[i];
		}
		mainmenu.count = cat_count;
	} else {
		mainmenu.labels[0] = "Terminal"; mainmenu.commands[0] = "st";
		mainmenu.labels[1] = "Browser";  mainmenu.commands[1] = "firefox";
		mainmenu.labels[2] = "Files";    mainmenu.commands[2] = "st -e ranger";
		mainmenu.count = 3;
	}
	mainmenu.page_max = mainmenu.count > per_page ? (mainmenu.count - 1) / per_page : 0;
}

static void gen_submenu(struct submenu *s) {
	if (!s->generated || !s->gen_cmd) return;
	FILE *p = popen(s->gen_cmd, "r");
	if (p) {
		parse_items(s, p);
		pclose(p);
		s->page_max = s->count > per_page ? (s->count - 1) / per_page : 0;
	}
	s->generated = 0;
}

static void draw(void);

/* the search field only makes sense on long, searchable lists */
static int show_field(const struct submenu *s) {
	return s->label && !strcmp(s->label, "Font");
}

/* truncate label to maxw pixels, appending "..." if needed */
static void truncate_label(const char *in, char *out, size_t outsz, int maxw) {
	size_t len = strlen(in);
	if (len + 1 > outsz) len = outsz - 1;
	memcpy(out, in, len);
	out[len] = '\0';
	if (appfont_width(afont, out, (int)len) <= maxw)
		return;
	while (len > 3) {
		len--;
		memcpy(out + len - 3, "...", 3);
		out[len] = '\0';
		if (appfont_width(afont, out, (int)len) <= maxw)
			return;
	}
	out[0] = '.'; out[1] = '.'; out[2] = '.'; out[3] = '\0';
}

static void enter_submenu(struct submenu *s) {
	if (nav_depth >= MAX_NAV) return;
	if (s->generated) gen_submenu(s);
	s->filter[0] = 0;
	recompute_filter(s);
	s->sel = 0;
	s->page = 0;
	nav[nav_depth++] = s;
	draw();
}

static void draw_submenu(struct submenu *s) {
	int hdr_y = itemh + 6;          /* header baseline */
	int rule_y = itemh + 18;        /* rule under header */
	int base_off = itemh - 8;       /* baseline offset within a row */
	int ftr_y = win_h - (itemh / 2 + 7);

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, PAPER_W, win_h);

	const char *hdr = s->title[0] ? s->title : "b y t e m e n u";
	char hbuf[128];
	truncate_label(hdr, hbuf, sizeof(hbuf), PAPER_W - 16);
	int tw = appfont_width(afont, hbuf, (int)strlen(hbuf));
	appfont_draw(afont, win, gc, &fc_dim, c_dim, (PAPER_W - tw) / 2, hdr_y, hbuf, (int)strlen(hbuf));

	XSetForeground(dpy, gc, c_border);
	XFillRectangle(dpy, win, gc, 20, rule_y, PAPER_W - 40, 2);
	XFillRectangle(dpy, win, gc, 0, 0, PAPER_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, win_h - BORDER_W, PAPER_W, BORDER_W);
	XFillRectangle(dpy, win, gc, 0, 0, BORDER_W, win_h);
	XFillRectangle(dpy, win, gc, PAPER_W - BORDER_W, 0, BORDER_W, win_h);

	/* search field with blinking cursor */
	int input_y = rule_y + itemh;
	int area_top, area_bot;
	if (show_field(s)) {
		int fw = PAPER_W - 80;
		if (fw < 160) fw = 160;
		int fx = (PAPER_W - fw) / 2;
		int fy = input_y - itemh / 2 + 2;
		int fh = itemh - 4;
		int field_base = fy + (fh - afont->height) / 2 + afont->ascent;
		XSetForeground(dpy, gc, c_field);
		XFillRectangle(dpy, win, gc, fx, fy, fw, fh);
		XSetForeground(dpy, gc, c_border);
		XDrawRectangle(dpy, win, gc, fx, fy, fw - 1, fh - 1);

		char disp[512];
		const char *tail = s->filter;
		int maxw = fw - 24;
		while (*tail && appfont_width(afont, tail, (int)strlen(tail)) > maxw - 20)
			tail++;
		snprintf(disp, sizeof(disp), "> %s", tail);
		tw = appfont_width(afont, disp, (int)strlen(disp));
		appfont_draw(afont, win, gc, &fc_fg, c_fg, fx + 10, field_base, disp, (int)strlen(disp));
		if (cursor_on) {
			XSetForeground(dpy, gc, c_hi);
			XFillRectangle(dpy, win, gc, fx + 10 + tw + 1, fy + (fh - 12) / 2, 7, 12);
		}
		area_top = input_y + itemh;
	} else {
		area_top = itemh * 2;
	}
	area_bot = ftr_y - itemh;

	int page_count = s->fmatch - s->page * per_page;
	if (page_count > per_page) page_count = per_page;
	if (page_count < 0) page_count = 0;
	int block = page_count * itemh;
	int top = area_top + ((area_bot - area_top) - block) / 2;
	char tmp[256];
	for (int i = 0; i < page_count; i++) {
		int idx = s->fmap[s->page * per_page + i];
		int y = top + i * itemh;
		int is_sel = (i == s->sel);
		truncate_label(s->labels[idx], tmp, sizeof(tmp), PAPER_W - 32);
		tw = appfont_width(afont, tmp, (int)strlen(tmp));
		if (is_sel)
			appfont_draw(afont, win, gc, &fc_hi, c_hi, (PAPER_W - tw) / 2 - 20, y + base_off, ">", 1);
		appfont_draw(afont, win, gc, is_sel ? &fc_hi : &fc_fg,
		             is_sel ? c_hi : c_fg, (PAPER_W - tw) / 2, y + base_off, tmp, (int)strlen(tmp));
	}

	if (s->fmatch == 0) {
		const char *empty = s->filter[0] ? "no match" : "(empty)";
		tw = appfont_width(afont, empty, (int)strlen(empty));
		appfont_draw(afont, win, gc, &fc_dim, c_dim, (PAPER_W - tw) / 2, win_h / 2, empty, (int)strlen(empty));
	}

	char ftr[96];
	if (s->page_max > 0) {
		snprintf(ftr, sizeof(ftr), "j/k  ]/[  esc");
	} else {
		snprintf(ftr, sizeof(ftr), s == &mainmenu ? "j/k  enter  esc" : "j/k  esc");
	}
	/* reserve room for the page indicator in the bottom-right corner */
	int ftr_w = 0;
	if (s->page_max > 0) {
		char pg[32];
		snprintf(pg, sizeof(pg), "[%d/%d]", s->page + 1, s->page_max + 1);
		ftr_w = appfont_width(afont, pg, (int)strlen(pg));
		appfont_draw(afont, win, gc, &fc_dim, c_dim,
			PAPER_W - ftr_w - 12, ftr_y, pg, (int)strlen(pg));
	}
	tw = appfont_width(afont, ftr, (int)strlen(ftr));
	if (tw > PAPER_W - 16 - ftr_w) {
		/* trim from the front to fit */
		size_t n = strlen(ftr);
		while (tw > PAPER_W - 16 - ftr_w && n > 0) {
			memmove(ftr, ftr + 1, n);
			n--;
			tw = appfont_width(afont, ftr, (int)n);
		}
	}
	appfont_draw(afont, win, gc, &fc_dim, c_dim, (PAPER_W - tw) / 2, ftr_y, ftr, (int)strlen(ftr));

	XSync(dpy, False);
}

static void draw(void) {
	draw_submenu(nav[nav_depth - 1]);
}

static void launch_cmd(const char *cmd) {
	if (!cmd) return;
	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
		_exit(1);
	}
}

static void launch_game(struct submenu *s, int idx) {
	if (idx < 0 || idx >= s->count || !s->commands[idx]) return;
	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		setsid();
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
		execl("/bin/sh", "/bin/sh", "-c", s->commands[idx], NULL);
		_exit(1);
	}
}

static void free_tree(struct submenu *s) {
	for (int j = 0; j < s->count; j++) {
		if (s->children[j]) {
			free_tree(s->children[j]);
			free(s->children[j]);
		} else if (s->games) {
			free(game_exe[j]);
			free(game_proton[j]);
			free(game_extra[j]);
		} else if (s->commands[j]) {
			free(s->commands[j]);
		}
		free(s->labels[j]);
	}
	free(s->gen_cmd);
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

	dpy = XOpenDisplay(NULL);
	if (!dpy) return 1;
	int scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	sw = DisplayWidth(dpy, scr);
	sh = DisplayHeight(dpy, scr);

	afont = appfont_open(dpy, appfont_sharedname());
	if (!afont) return 1;

	win_h = PAPER_H;
	if (win_h > sh - 40) win_h = sh - 40;
	if (win_h < 240) win_h = 240;

	/* adaptive layout: row height + rows/page derive from the font */
	itemh = afont->height + 14;
	per_page = (win_h - itemh * 3 - 20) / itemh;
	if (per_page < 1) per_page = 1;
	if (per_page > 6) per_page = 6;

	c_bg     = getcol("#282828");
	c_fg     = getcol("#ebdbb2");
	c_hi     = getcol("#d65d0e");
	c_border = getcol("#689d6a");
	c_dim    = getcol("#a89984");
	c_field  = getcol("#3c3836");
	appfont_alloccolor(dpy, "#ebdbb2", &fc_fg);
	appfont_alloccolor(dpy, "#d65d0e", &fc_hi);
	appfont_alloccolor(dpy, "#a89984", &fc_dim);

	build_categories();
	load_categories();
	build_main_menu();
	recompute_filter(&mainmenu);

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = c_bg,
		.event_mask = ExposureMask | KeyPressMask
	};
	win = XCreateWindow(dpy, root,
		(sw - PAPER_W) / 2, (sh - win_h) / 2,
		PAPER_W, win_h, 0,
		DefaultDepth(dpy, scr), CopyFromParent,
		DefaultVisual(dpy, scr),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	gc = XCreateGC(dpy, root, 0, NULL);

	nav[0] = &mainmenu;
	nav_depth = 1;

	XMapRaised(dpy, win);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	XGrabPointer(dpy, win, True,
		ButtonPressMask, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime);

	XEvent ev;
	int fd = ConnectionNumber(dpy);
	long last_blink = 0;
	int done = 0;
	while (!done) {
		fd_set rfds;
		struct timeval tv;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 50000;
		int r = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (r > 0) {
			while (XPending(dpy) && !done) {
				XNextEvent(dpy, &ev);
				if (ev.type == Expose && ev.xexpose.count == 0) {
					draw();
					continue;
				}
				if (ev.type != KeyPress) continue;
				cursor_on = 1;
				last_blink = now_ms();
				KeySym ks = XLookupKeysym(&ev.xkey, 0);
				char lbuf[32];
				int llen = XLookupString(&ev.xkey, lbuf, sizeof(lbuf), NULL, NULL);
				struct submenu *s = nav[nav_depth - 1];

			if (ks == XK_Escape || ks == XK_q) {
				if (s->filter[0]) {
					s->filter[0] = 0;
					recompute_filter(s);
					s->sel = 0;
					s->page = 0;
					draw();
				} else if (nav_depth > 1) {
					nav_depth--;
					draw();
				} else {
					done = 1;
				}
			} else if (ks == XK_Return) {
				int idx = s->page * per_page + s->sel;
				if (idx >= 0 && idx < s->fmatch) {
					int real = s->fmap[idx];
					if (s->children[real]) {
						enter_submenu(s->children[real]);
					} else if (s->games) {
						launch_game(s, real);
						done = 1;
					} else if (s->commands[real]) {
						launch_cmd(s->commands[real]);
						done = 1;
					}
				}
			} else if (ks == XK_BackSpace) {
				if (s->filter[0]) {
					s->filter[strlen(s->filter) - 1] = 0;
					recompute_filter(s);
					if (s->sel >= s->fmatch) s->sel = s->fmatch ? s->fmatch - 1 : 0;
					if (s->page > s->page_max) s->page = s->page_max;
					if (s->sel < 0) s->sel = 0;
					if (s->page < 0) s->page = 0;
					draw();
				}
			} else if (ks == XK_Down) {
				int pc = s->fmatch - s->page * per_page;
				if (pc > per_page) pc = per_page;
				if (pc > 0) { s->sel = (s->sel + 1) % pc; draw(); }
			} else if (ks == XK_Up) {
				int pc = s->fmatch - s->page * per_page;
				if (pc > per_page) pc = per_page;
				if (pc > 0) { s->sel = (s->sel - 1 + pc) % pc; draw(); }
			} else if (!s->filter[0] && ks == XK_j) {
				int pc = s->fmatch - s->page * per_page;
				if (pc > per_page) pc = per_page;
				if (pc > 0) { s->sel = (s->sel + 1) % pc; draw(); }
			} else if (!s->filter[0] && ks == XK_k) {
				int pc = s->fmatch - s->page * per_page;
				if (pc > per_page) pc = per_page;
				if (pc > 0) { s->sel = (s->sel - 1 + pc) % pc; draw(); }
			} else if (ks == XK_bracketright || ks == XK_Tab ||
			           (!s->filter[0] && (ks == XK_l || ks == XK_Right))) {
				if (s->page < s->page_max) { s->page++; s->sel = 0; draw(); }
			} else if (ks == XK_bracketleft ||
			           (!s->filter[0] && (ks == XK_h || ks == XK_Left))) {
				if (s->page > 0) { s->page--; s->sel = 0; draw(); }
			} else if (llen == 1 && (isprint((unsigned char)lbuf[0]) || lbuf[0] == ' ')) {
				if (strlen(s->filter) < sizeof(s->filter) - 1) {
					size_t n = strlen(s->filter);
					s->filter[n] = lbuf[0];
					s->filter[n + 1] = 0;
					recompute_filter(s);
					if (s->sel >= s->fmatch) s->sel = s->fmatch ? s->fmatch - 1 : 0;
					if (s->page > s->page_max) s->page = s->page_max;
					if (s->sel < 0) s->sel = 0;
					if (s->page < 0) s->page = 0;
					draw();
				}
			}
			}
		} else {
			long now = now_ms();
			if (now - last_blink >= 500) {
				cursor_on = !cursor_on;
				last_blink = now;
				draw();
			}
		}
	}

	XUngrabPointer(dpy, CurrentTime);
	XFreeGC(dpy, gc);
	appfont_close(afont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	for (int i = 0; i < cat_count; i++)
		free_tree(&cats[i]);
	return 0;
}

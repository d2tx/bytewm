/* gruvbox dark theme (overridable via ~/.config/bytewm/config) */
static char col_bg[16]       = "#282828";
static char col_fg[16]       = "#ebdbb2";
static char col_black[16]    = "#1d2021";
static char col_red[16]      = "#cc241d";
static char col_green[16]    = "#98971a";
static char col_yellow[16]   = "#d79921";
static char col_blue[16]     = "#458588";
static char col_purple[16]   = "#b16286";
static char col_aqua[16]     = "#689d6a";
static char col_orange[16]   = "#d65d0e";
static char col_gray[16]     = "#a89984";
static char col_dimbg[16]    = "#3c3836";

static char *colors[SchemeLast][2] = {
	[SchemeNorm] = { col_fg, col_bg },
	[SchemeSel]  = { col_bg, col_aqua },
	[SchemeTag]  = { col_orange, col_bg },
	[SchemeUrg]  = { col_bg, col_red },
};

static char font[64] = "monospace:size=10";

static unsigned int borderpx  = 6;
static unsigned int gappx     = 6;
static unsigned int gappoh    = 6;
static unsigned int gappoi    = 6;
static unsigned int barheight = 20;
static int            showbar  = 1;
static int            topbar   = 1;

static const int swallowfloating = 0;  /* 1 = swallow even floating terminals */

#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggletag,      {.ui = 1 << TAG} },

static const char *tags[] = { "I", "II", "III", "IV", "V" };

static const Rule rules[] = {
	{ "st",          NULL,       NULL,        0,         0 },
	{ "st-256color", NULL,       NULL,        0,         0 },
};

static const float mfact     = 0.55;
static const int   nmaster   = 1;

static const Layout layouts[] = {
	{ "[=]",      tile },
	{ "[#]",      bsp },
	{ "[O]",      monocle },
	{ "[~]",      NULL },
};

static const char *termcmd[]  = { "st", NULL };
static const char *menucmd[]  = { "bytelaunch", NULL };
static const char *snapcmd[]  = { "bytesnap", NULL };
static const char *pickcmd[]  = { "bytepick", NULL };
static const char *menucmd2[] = { "bytemenu", NULL };
static const char *lockcmd[]  = { "bytelock", NULL };

static const char *scratchpadcmd[] = { "st", "-t", "scratchpad", "-c", "scratchpad", NULL };

static const Key keys[] = {
	{ MODKEY,                       XK_p,      spawn,          {.v = menucmd } },
	{ MODKEY|ShiftMask,             XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY,                       XK_Return, togglescratch,  {.v = NULL } },
	{ MODKEY,                       XK_s,      spawn,          {.v = snapcmd } },
	{ MODKEY,                       XK_c,      spawn,          {.v = pickcmd } },
	{ MODKEY|ShiftMask,             XK_m,      spawn,          {.v = menucmd2 } },
	{ MODKEY|ControlMask,           XK_l,      spawn,          {.v = lockcmd } },

	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_j,      swapclients,    {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_k,      swapclients,    {.i = -1 } },
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05 } },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05 } },
	{ MODKEY,                       XK_g,      setmfact,       {.f = +0.05 } },
	{ MODKEY|ControlMask,          XK_h,      setcfact,       {.f = -0.05 } },
	{ MODKEY|ControlMask,          XK_g,      setcfact,       {.f = +0.05 } },

	{ MODKEY,                       XK_z,      zoom,           {.v = NULL } },
	{ MODKEY,                       XK_Left,   movearrow,      {.i = 0 } },
	{ MODKEY,                       XK_Right,  movearrow,      {.i = 1 } },
	{ MODKEY,                       XK_Up,     movearrow,      {.i = 2 } },
	{ MODKEY,                       XK_Down,   movearrow,      {.i = 3 } },
	{ MODKEY|ShiftMask,             XK_Left,   resizearrow,    {.i = 0 } },
	{ MODKEY|ShiftMask,             XK_Right,  resizearrow,    {.i = 1 } },
	{ MODKEY|ShiftMask,             XK_Up,     resizearrow,    {.i = 2 } },
	{ MODKEY|ShiftMask,             XK_Down,   resizearrow,    {.i = 3 } },
	{ MODKEY,                       XK_Tab,    viewprevtag,    {.v = NULL } },
	{ MODKEY,                       XK_w,      killclient,     {.v = NULL } },

	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0] } },
	{ MODKEY,                       XK_b,      setlayout,      {.v = &layouts[1] } },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[2] } },
	{ MODKEY,                       XK_space,  setlayout,      {.v = &layouts[3] } },

	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {.v = NULL } },
	{ MODKEY,                       XK_f,      togglefullscr,  {.v = NULL } },

	{ MODKEY,                       XK_F10,    spawn,          {.v = (char *[]){"/bin/sh","-c","amixer set Master 5%- >/dev/null 2>&1",NULL} } },
	{ MODKEY,                       XK_F11,    spawn,          {.v = (char *[]){"/bin/sh","-c","amixer set Master 5%+ >/dev/null 2>&1",NULL} } },
	{ MODKEY,                       XK_F12,    spawn,          {.v = (char *[]){"/bin/sh","-c","amixer set Master toggle >/dev/null 2>&1",NULL} } },
	{ MODKEY|ShiftMask,             XK_v,      spawn,          {.v = (char *[]){"/bin/sh","-c","bytevol $(amixer get Master | tail -1 | sed 's/.*\\[\\([0-9]*\\)%\\].*/\\1/')",NULL} } },
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },

	{ MODKEY,                       XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY,                       XK_period, tagmon,         {.i = +1 } },

	{ MODKEY|ShiftMask,             XK_q,      spawn,          {.v = (char *[]){"bytewm-exit", NULL} } },
	{ MODKEY|ShiftMask,             XK_r,      spawn,          {.v = (char *[]){"pkill", "-HUP", "bytewm", NULL} } },

	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)

};

static const Button buttons[] = {
	{ MODKEY, Button1, movemouse,     {0} },
	{ MODKEY, Button2, zoom,          {0} },
	{ MODKEY, Button3, resizemouse,   {0} },
	{ MODKEY, Button4, focusstack,    {.i = +1 } },
	{ MODKEY, Button5, focusstack,    {.i = -1 } },
	{ MODKEY|ShiftMask, Button4, setcfact, {.f = -0.05 } },
	{ MODKEY|ShiftMask, Button5, setcfact, {.f = +0.05 } },
};

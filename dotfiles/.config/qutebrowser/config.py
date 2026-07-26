# bytewm gruvbox dark theme for qutebrowser
config.load_autoconfig(False)
c.colors.webpage.darkmode.enabled = True

# ── gruvbox palette ──────────────────────────────────────
bg0     = "#282828"
bg1     = "#3c3836"
bg2     = "#504945"
fg0     = "#ebdbb2"
fg1     = "#a89984"
aqua    = "#689d6a"
blue    = "#458588"
orange  = "#d65d0e"
red     = "#cc241d"
green   = "#98971a"
yellow  = "#d79921"
purple  = "#b16286"

# ── tabs ─────────────────────────────────────────────────
c.tabs.position = "top"
c.tabs.show = "multiple"
c.tabs.favicons.scale = 1.0
c.colors.tabs.bar.bg = bg0
c.colors.tabs.even.bg = bg0
c.colors.tabs.even.fg = fg1
c.colors.tabs.odd.bg = bg1
c.colors.tabs.odd.fg = fg1
c.colors.tabs.selected.even.bg = aqua
c.colors.tabs.selected.even.fg = bg0
c.colors.tabs.selected.odd.bg = aqua
c.colors.tabs.selected.odd.fg = bg0

# ── status bar ───────────────────────────────────────────
c.colors.statusbar.normal.bg = bg0
c.colors.statusbar.normal.fg = fg0
c.colors.statusbar.command.bg = bg1
c.colors.statusbar.command.fg = fg0
c.colors.statusbar.url.fg = fg1

# ── completion ───────────────────────────────────────────
c.colors.completion.fg = fg1
c.colors.completion.even.bg = bg0
c.colors.completion.odd.bg = bg1
c.colors.completion.category.fg = orange
c.colors.completion.category.bg = bg0
c.colors.completion.item.selected.fg = bg0
c.colors.completion.item.selected.bg = aqua
c.colors.completion.match.fg = yellow

# ── hints ────────────────────────────────────────────────
c.colors.hints.bg = aqua
c.colors.hints.fg = bg0
c.colors.hints.match.fg = orange

# ── keybindings ──────────────────────────────────────────
config.bind(',v', 'spawn mpv {url}')  # open video in mpv
config.bind(',y', 'yank')
config.bind(',d', 'scroll-page 0 0.5')
config.bind(',u', 'scroll-page 0 -0.5')

# ── settings ─────────────────────────────────────────────
c.auto_save.session = True
c.url.start_pages = ["https://duckduckgo.com"]
c.url.default_page = "https://duckduckgo.com"
c.fonts.default_family = "Terminus"
c.fonts.default_size = "12pt"
c.fonts.web.family.fixed = "DejaVu Sans Mono"
c.fonts.web.family.standard = "DejaVu Sans Mono"
c.fonts.web.family.sans_serif = "DejaVu Sans Mono"
c.fonts.web.family.serif = "DejaVu Sans Mono"
c.fonts.web.size.default = 12
c.fonts.web.size.default_fixed = 12
c.fonts.web.size.minimum = 8
c.editor.command = ["st", "-e", "nvim", "{}"]

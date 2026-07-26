from ranger.gui.colorscheme import ColorScheme
from ranger.gui.color import *

class Scheme(ColorScheme):
    def use(self, context):
        fg, bg, attr = default_colors

        if context.reset:
            return default_colors

        elif context.in_browser:
            if context.selected:
                fg = 235  # bg1
                bg = 142  # aqua/green
            elif context.directory:
                fg = 142  # aqua
            elif context.executable and not context.media:
                fg = 208  # orange
            elif context.media:
                fg = 132  # purple
            elif context.link:
                fg = 109  # blue
            elif context.tag_marker and not context.selected:
                fg = 178  # yellow
                attr |= bold

        elif context.in_titlebar:
            fg = 223
            bg = 237

        elif context.in_statusbar:
            if context.selected:
                fg = 235
                bg = 142
            else:
                fg = 246
                bg = 237

        return fg, bg, attr

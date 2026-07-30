from ranger.gui.colorscheme import ColorScheme
from ranger.gui.color import *

class Scheme(ColorScheme):
    def use(self, context):
        fg, bg, attr = default_colors

        if context.reset:
            return default_colors

        elif context.in_browser:
            if context.selected:
                fg = 235  # bg0
                bg = 108  # aqua
            elif context.directory:
                fg = 108  # aqua
            elif context.executable and not context.media:
                fg = 172  # orange
            elif context.media:
                fg = 139  # purple
            elif context.link:
                fg = 73   # blue
            elif context.tag_marker and not context.selected:
                fg = 179  # yellow
                attr |= bold

        elif context.in_titlebar:
            fg = 223  # fg
            bg = 237  # bg1

        elif context.in_statusbar:
            if context.selected:
                fg = 235  # bg0
                bg = 108  # aqua
            else:
                fg = 244  # gray
                bg = 237  # bg1

        return fg, bg, attr

if status is-interactive
end

set -gx PATH /home/xmr/.opencode/bin $PATH

# suppress oh-my-fish / bobthefish right-hand clutter
function fish_right_prompt; end
function fish_mode_prompt; end

# ── retro gruvbox prompt ──────────────────────────────────────────────

function fish_greeting
    /home/xmr/bytewm/apps/bytefetch
end

function fish_prompt
    set -l last_status $status
    set -l sep  (set_color brblack)'│'(set_color normal)

    # top line: pwd + git
    set_color brblack;  printf '╭─'
    set_color brblue;   printf '%s' (whoami)
    set_color brblack;  printf '@'
    set_color bryellow; printf '%s' (prompt_hostname)
    set_color brblack;  printf ' '
    set_color f90;      printf '%s' (prompt_pwd)
    set_color normal

    # git branch
    if set -l branch (git branch --show-current 2>/dev/null)
        set_color brblack; printf ' '
        set_color brgreen; printf '<%s>' $branch
    end

    set_color normal; printf '\n'

    # bottom line: status-aware arrow
    if test $last_status -eq 0
        set_color brblack;  printf '╰─'
        set_color brgreen;  printf '>'
    else
        set_color brblack;  printf '╰─'
        set_color red;      printf '>'
    end
    set_color normal; printf ' '
end

# ── settings ───────────────────────────────────────────────────────────

set -gx EDITOR nvim
set -gx VISUAL nvim

set -g fish_key_bindings fish_default_key_bindings
set -g fish_pager_color_progress brblack
set -g fish_color_command brgreen
set -g fish_color_param bryellow
set -g fish_color_error red
set -g fish_color_autosuggestion brblack

# ── aliases ────────────────────────────────────────────────────────────

alias ls='ls --color=auto'
alias ll='ls -lh'
alias la='ls -lah'
alias grep='grep --color=auto'
alias ..='cd ..'
alias ...='cd ../..'

# dotfiles bare repo
alias dotfiles='git --git-dir=$HOME/.dotfiles --work-tree=$HOME'

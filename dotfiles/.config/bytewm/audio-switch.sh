#!/bin/sh
# bytewm helper: switch the default PipeWire audio sink
#   audio-switch.sh next          cycle to the next sink
#   audio-switch.sh list          list sinks (id | name)
#   audio-switch.sh to NAME       set NAME (substring match on sink name)
#   audio-switch.sh notify        popup the current sink via bytify

# raw sink lines, box-drawing stripped, "*" kept as the default marker
sink_raw() {
	wpctl status 2>/dev/null | sed -n '/Sinks:/,/Sources:/p' |
		sed 's/[│├└─]//g; s/^ *//' | grep -E '^(\* *)?[0-9]+\.'
}

# "ID" list (current default first)
sink_ids() {
	sink_raw | sed -n 's/^\* *\([0-9]*\)\..*/\1/p; s/^\([0-9]*\)\..*/\1/p'
}

current_id() {
	sink_raw | sed -n 's/^\* *\([0-9]*\)\..*/\1/p' | head -1
}

sink_name() {
	id="$1"
	sink_raw | sed -n "s/^\* *$id\.//p; s/^$id\.//p" | sed 's/ *$//' | head -1
}

# short label for a sink id: "device 1", "device 2", ... by its position
sink_short() {
	id="$1"
	n=0
	for sid in $(sink_ids); do
		n=$((n + 1))
		if [ "$sid" = "$id" ]; then
			echo "device $n"
			return
		fi
	done
	echo "device"
}

case "$1" in
	list)
		sink_raw | sed 's/\* *//'
		;;
	next)
		ids=$(sink_ids)
		cur=$(current_id)
		next=""
		found=""
		for id in $ids; do
			if [ "$id" = "$cur" ]; then found=1; continue; fi
			if [ -n "$found" ]; then next="$id"; break; fi
		done
		[ -z "$next" ] && next=$(printf '%s\n' "$ids" | head -1)
		[ -n "$next" ] && wpctl set-default "$next" 2>/dev/null
		[ -n "$next" ] && echo "output: $(sink_short "$next")" > /tmp/bytify.fifo 2>/dev/null
		echo 1 > /tmp/bytewm_status.fifo 2>/dev/null
		;;
	to)
		name="$2"
		[ -z "$name" ] && exit 1
		target=$(sink_raw | sed 's/\* *//' | while read -r line; do
			id=${line%%.*}
			nm=${line#*.}
			case "$nm" in *"$name"*) echo "$id"; break;; esac
		done)
		[ -n "$target" ] && wpctl set-default "$target" 2>/dev/null
		[ -n "$target" ] && echo "output: $(sink_short "$target")" > /tmp/bytify.fifo 2>/dev/null
		echo 1 > /tmp/bytewm_status.fifo 2>/dev/null
		;;
	notify)
		id=$(current_id)
		echo "output: $(sink_short "$id")" > /tmp/bytify.fifo 2>/dev/null
		;;
	*)
		echo "usage: audio-switch.sh {next|list|to NAME|notify}"
		exit 1
		;;
esac

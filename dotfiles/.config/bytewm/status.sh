#!/bin/sh
# bytewm status bar - cpu, mem, temp, time

cpu=$(top -bn1 2>/dev/null | awk '/^%Cpu/ {printf "%d", int(100-$8)}')
[ -z "$cpu" ] && cpu=0

mem_total=$(free -m 2>/dev/null | awk '/^Mem:/ {print $2}')
mem_used=$(free -m 2>/dev/null | awk '/^Mem:/ {print $3}')
[ -z "$mem_used" ] && mem_used=0 && mem_total=0

temp=$(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | head -1)
[ -n "$temp" ] && temp_val=$((temp/1000)) || temp_val=""

datetime=$(date '+%Y-%m-%d %H:%M')

printf "CPU %d%% | MEM %s" "$cpu" "${mem_used}/${mem_total}M"
[ -n "$temp_val" ] && printf " | %3d°C" "$temp_val"
printf " | %s" "$datetime"

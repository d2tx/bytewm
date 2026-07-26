#!/bin/sh
# bytewm status bar - cpu, mem, cpu/gpu temp, nvme, time

# CPU delta — compare with previous values
read p_u p_n p_s p_i < /tmp/bytewm_cpu_prev 2>/dev/null || true
read cpu_label u n s i _ <<< $(awk '/^cpu /{print $1,$2,$3,$4,$5}' /proc/stat)
[ -z "$u" ] && u=0 && n=0 && s=0 && i=0
du=$((u - p_u)); dn=$((n - p_n)); ds=$((s - p_s)); di=$((i - p_i))
total=$((du + dn + ds + di))
[ "$total" -gt 0 ] && cpu=$(( (du + ds) * 100 / total )) || cpu=0
echo "$u $n $s $i" > /tmp/bytewm_cpu_prev

mem=$(free -m 2>/dev/null | awk '/^Mem:/ {print $2, $3}')
mem_total=${mem%% *}
mem_used=${mem##* }
[ -z "$mem_used" ] && mem_used=0 && mem_total=0

# CPU temp
cputemp=$(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | head -1)
[ -n "$cputemp" ] && cputemp=$((cputemp/1000)) || cputemp=""

# GPU temp (AMD)
gputemp=$(cat /sys/class/drm/card*/device/hwmon/hwmon*/temp1_input 2>/dev/null | head -1)
[ -n "$gputemp" ] && gputemp=$((gputemp/1000)) || gputemp=""

# NVMe info
# NVMe disk usage
nvme=""
for n in /dev/nvme?n1; do
  [ -e "$n" ] || continue
  mnt=$(lsblk -nlo MOUNTPOINT "$n" 2>/dev/null | grep -v '^$' | head -1)
  [ -z "$mnt" ] && mnt="/"
  df_out=$(df -h "$mnt" 2>/dev/null | awk 'NR==2{print $3"/"$2}')
  [ -n "$df_out" ] && nvme="$nvme${nvme:+ }${n##*/}: ${df_out}"
done

# Volume
vol=$(amixer get Master 2>/dev/null | awk -F'[][]' '/%/ {print $2, $4}' | head -1)
[ -z "$vol" ] && vol=""

datetime=$(date +"%I:%M %p")

printf "CPU %d%% | MEM %s" "$cpu" "${mem_used}/${mem_total}M"
[ -n "$cputemp" ] && printf " | CPU %d°C" "$cputemp"
[ -n "$gputemp" ] && printf " | GPU %d°C" "$gputemp"
[ -n "$nvme" ] && printf " | %s" "$nvme"
[ -n "$vol" ] && printf " | VOL %s" "$vol"
printf " | %s" "$datetime"

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

# Volume (from bytevol daemon's perceptual level)
vol=$(cat /tmp/bytevol_level 2>/dev/null)
[ -z "$vol" ] && vol="0"
[ "$vol" != "MUTE" ] && vol="${vol}%"

# VPN (Windscribe) - lightweight: utun check for on/off, cached country
city_to_code() {
	case "$1" in
		Vienna) echo AT;; Berlin|Frankfurt) echo DE;; Paris|Bordeaux|Marseille) echo FR;;
		Amsterdam) echo NL;; London|Manchester|Edinburgh) echo GB;;
		Atlanta|Ashburn|Bend|Boston|Buffalo|Charlotte|Chicago|Cleveland|Dallas|Denver|Detroit|Honolulu|Houston|Kansas\ City|Las\ Vegas|Los\ Angeles|Miami|New\ Jersey|New\ York|Orlando|Philadelphia|Phoenix|Raleigh|Salt\ Lake\ City|San\ Francisco|San\ Jose|Santa\ Clara|Seattle|South\ Bend|Tampa|Washington\ DC) echo US;;
		Toronto|Montreal|Calgary|Vancouver|Winnipeg|Halifax) echo CA;;
		Sydney|Melbourne|Brisbane|Perth|Adelaide) echo AU;;
		Milan|Palermo) echo IT;; Madrid|Barcelona) echo ES;;
		Oslo) echo NO;; Stockholm) echo SE;; Helsinki) echo FI;;
		Copenhagen) echo DK;; Warsaw|Gdansk) echo PL;;
		Prague) echo CZ;; Bratislava) echo SK;; Budapest) echo HU;;
		Zagreb) echo HR;; Ljubljana) echo SI;; Belgrade) echo RS;;
		Sofia) echo BG;; Bucharest) echo RO;; Skopje) echo MK;;
		Tirana) echo AL;; Sarajevo|Novi\ Travnik) echo BA;;
		Athens) echo GR;; Nicosia|Limassol) echo CY;;
		Istanbul) echo TR;; Tbilisi) echo GE;; Chisinau) echo MD;;
		Kyiv) echo UA;; Moscow|Fake\ St\ Petersburg) echo RU;;
		Riga) echo LV;; Vilnius) echo LT;; Tallinn) echo EE;;
		Luxembourg) echo LU;; Brussels) echo BE;; Dublin) echo IE;;
		Zurich) echo CH;; Reykjavik) echo IS;;
		Tokyo) echo JP;; Seoul) echo KR;; Taipei) echo TW;;
		Bangkok) echo TH;; Hanoi) echo VN;; Singapore) echo SG;;
		Kuala\ Lumpur) echo MY;; Jakarta|Surabaya|Bali) echo ID;;
		Manila) echo PH;; Hong\ Kong) echo HK;;
		Mumbai|New\ Delhi|Fake\ Mumbai) echo IN;;
		Tel\ Aviv|Ashdod) echo IL;; Dubai) echo AE;;
		Johannesburg) echo ZA;; Nairobi) echo KE;; Lagos) echo NG;;
		Buenos\ Aires) echo AR;; Santiago) echo CL;; Bogota) echo CO;;
		Lima) echo PE;; Quito) echo EC;; Asuncion) echo PY;;
		Montevideo) echo UY;; Panama\ City) echo PA;;
		Sao\ Paulo) echo BR;; Mexico\ City|Queretaro) echo MX;;
		Guatemala\ City) echo GT;; San\ Salvador) echo SV;;
		Bali) echo ID;; Queretaro) echo MX;;
		Troll) echo AQ;;
	esac
}

vpn_country() {
	cache=/tmp/bytewm_vpn_country
	if [ -f "$cache" ] && [ $(( $(date +%s) - $(stat -c %Y "$cache") )) -lt 30 ]; then
		cat "$cache"
		return
	fi
	city=$(timeout 8 windscribe-cli status 2>/dev/null \
		| sed -n 's/^Connect state: Connected: //p' | sed 's/ - .*//')
	code=$(city_to_code "$city")
	[ -n "$code" ] && printf '%s' "$code" > "$cache"
	echo "${code:-?}"
}

vpn="OFF"
if ip link show 2>/dev/null | grep -q "utun"; then
	vpn=$(vpn_country)
fi

datetime=$(date +"%I:%M %p")

printf "VOL %s" "$vol"
printf " | VPN %s" "$vpn"
printf " | CPU %d%% | MEM %s" "$cpu" "${mem_used}/${mem_total}M"
[ -n "$cputemp" ] && printf " | CPU %d°C" "$cputemp"
[ -n "$nvme" ] && printf " | %s" "$nvme"
printf " | %s" "$datetime"

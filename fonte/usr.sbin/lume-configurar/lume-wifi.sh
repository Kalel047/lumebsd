#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-wifi - conecta a uma rede Wi-Fi pelo terminal e guarda a escolha.
# No desktop, use o ícone de rede (networkmgr).
#

[ "$(id -u)" -eq 0 ] || { echo "lume wifi: precisa ser root (su -)" >&2; exit 1; }

placa=$(sysctl -n net.wlan.devices 2>/dev/null | awk '{print $1}')
if [ -z "$placa" ]; then
	echo "  Nenhuma placa Wi-Fi com driver. Veja: lume hardware"
	exit 1
fi

if ! ifconfig wlan0 >/dev/null 2>&1; then
	ifconfig wlan0 create wlandev "$placa" || exit 1
fi
ifconfig wlan0 up

echo "  Procurando redes ($placa)..."
ifconfig wlan0 scan >/dev/null 2>&1
sleep 3
redes=$(ifconfig wlan0 list scan 2>/dev/null | awk 'NR > 1 {
	ssid = substr($0, 1, 33); sub(/ +$/, "", ssid)
	if (ssid != "" && !(ssid in visto)) { visto[ssid] = 1; print ssid }
}')
if [ -z "$redes" ]; then
	echo "  Nenhuma rede encontrada."
	exit 1
fi
echo "$redes" | awk '{ printf "    %2d) %s\n", NR, $0 }'
printf "  Número da rede: "
read -r n
ssid=$(echo "$redes" | sed -n "${n}p")
[ -n "$ssid" ] || { echo "  Opção inválida."; exit 1; }

printf "  Senha de \"%s\" (vazio se aberta): " "$ssid"
stty -echo 2>/dev/null; read -r senha; stty echo 2>/dev/null; echo

conf=/etc/wpa_supplicant.conf
touch "$conf"; chmod 600 "$conf"
grep -q '^ctrl_interface=' "$conf" ||
    printf 'ctrl_interface=/var/run/wpa_supplicant\nctrl_interface_group=wheel\nupdate_config=1\n' >> "$conf"
if [ -n "$senha" ]; then
	wpa_passphrase "$ssid" "$senha" | grep -v '#psk=' >> "$conf"
else
	printf 'network={\n\tssid="%s"\n\tkey_mgmt=NONE\n}\n' "$ssid" >> "$conf"
fi
senha=

sysrc -q wlans_"$placa"="wlan0" >/dev/null
sysrc -q ifconfig_wlan0="WPA DHCP" >/dev/null
echo "  Conectando..."
service netif restart wlan0 >/dev/null 2>&1
i=0
while [ $i -lt 20 ]; do
	ip=$(ifconfig wlan0 | awk '/inet / {print $2}')
	[ -n "$ip" ] && break
	sleep 1; i=$((i + 1))
done
if [ -n "$ip" ]; then
	echo "  ✓ Conectado a \"$ssid\" com o endereço $ip."
else
	echo "  ✗ Não conectou. Confira a senha e tente de novo."
	exit 1
fi

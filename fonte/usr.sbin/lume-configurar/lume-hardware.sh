#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-hardware - diz o que funciona neste computador no LumeBSD.
#
# Pensado para rodar no pendrive "ao vivo" antes de instalar: cada item
# recebe ok, atenção ou não funciona, com uma explicação curta.
#

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	VERDE='\033[32;1m'; AMARELO='\033[33;1m'; VERMELHO='\033[31;1m'
	FRACO='\033[2m'; NEGRITO='\033[1m'; FIM='\033[0m'
else
	VERDE=; AMARELO=; VERMELHO=; FRACO=; NEGRITO=; FIM=
fi

OKS=0; ATENCAO=0; FALHAS=0

item()
{
	# item ok|atencao|falha "nome" "detalhe" ["explicação"]
	case "$1" in
	ok)	printf "  ${VERDE}✓${FIM} %-14s %s\n" "$2" "$3"; OKS=$((OKS + 1)) ;;
	atencao) printf "  ${AMARELO}!${FIM} %-14s %s\n" "$2" "$3"
		ATENCAO=$((ATENCAO + 1)) ;;
	falha)	printf "  ${VERMELHO}✗${FIM} %-14s %s\n" "$2" "$3"
		FALHAS=$((FALHAS + 1)) ;;
	esac
	[ -n "${4:-}" ] && printf "  ${FRACO}  %-14s %s${FIM}\n" "" "$4"
	return 0
}

pci()
{
	# pci <classe-regex>: linhas "nome@... vendor device" do pciconf
	pciconf -lv 2>/dev/null | awk -v re="$1" '
		/^[a-z]/ { dev = $1 }
		/device *=/ { sub(/.*device *= */, ""); gsub(/\x27/, ""); desc = $0 }
		/class *=/ && $0 ~ re { print dev " " desc }'
}

modelo=$(kenv -q smbios.system.version 2>/dev/null)
produto=$(kenv -q smbios.system.product 2>/dev/null)
fabricante=$(kenv -q smbios.system.maker 2>/dev/null)

printf "\n  ${NEGRITO}LumeBSD - diagnóstico de hardware${FIM}\n"
printf "  %s %s (%s)\n" "$fabricante" "${modelo:-?}" "${produto:-?}"
printf "  ------------------------------------------------------------\n"

# Processador
item ok "processador" "$(sysctl -n hw.model) ($(sysctl -n hw.ncpu) núcleos)"
if sysctl -n dev.hwpstate_intel.0.epp >/dev/null 2>&1; then
	item ok "energia CPU" "Intel Speed Shift (hwpstate_intel) ativo"
elif sysctl -n dev.cpu.0.freq_levels >/dev/null 2>&1; then
	item ok "energia CPU" "controle de frequência ativo"
else
	item atencao "energia CPU" "sem controle de frequência"
fi

# Memória e disco
# Arredonda para cima: parte da RAM fica reservada e 16 GiB viram 15,8.
mem=$((($(sysctl -n hw.physmem) + 1024 * 1024 * 1024 - 1) / 1024 / 1024 / 1024))
item ok "memória" "${mem} GiB"
discos=$(sysctl -n kern.disks)
for d in $discos; do
	case "$d" in cd[0-9]*) continue ;; esac
	desc=$(geom disk list "$d" 2>/dev/null | awk -F': ' '/descr:/ {print $2; exit}')
	tam=$(geom disk list "$d" 2>/dev/null | awk '/Mediasize:/ {print $2; exit}')
	[ -n "$tam" ] && tam="$((tam / 1000000000)) GB"
	item ok "disco $d" "${desc:-?} ${tam}"
done

# Vídeo
eval "$(/usr/sbin/lume-video detectar 2>/dev/null)"
estado_video=$(cat /var/db/lume/video-estado 2>/dev/null)
case "$estado_video" in
nvidia|intel|amd)
	item ok "vídeo" "acelerado com o driver $estado_video" ;;
travou)	item falha "vídeo" "o driver ${escolha:-} travou o computador numa tentativa" \
	    "o desktop usa o modo sem aceleração (scfb); veja: lume video" ;;
falhou)	item falha "vídeo" "o driver ${escolha:-} não reconheceu a placa" \
	    "o desktop usa o modo sem aceleração (scfb); veja: lume video" ;;
*)	case "${escolha:-scfb}" in
	nvidia|intel|amd)
		item atencao "vídeo" "driver $escolha ainda não testado" \
		    "teste com: lume video testar" ;;
	*)	item atencao "vídeo" "sem placa NVIDIA, Intel ou AMD: modo sem aceleração" ;;
	esac ;;
esac
[ -n "${nvidia:-}" ] && item ok "placa NVIDIA" "$nvidia" "driver: $nvidia_pacote"
[ -n "${intel:-}" ] && item ok "placa Intel" "$intel"
[ -n "${amd:-}" ] && item ok "placa AMD" "$amd" "driver: ${amd_driver:-amdgpu}"
if [ -n "${nvidia:-}" ] && [ -n "${intel:-}" ] && [ "${bateria:-}" = sim ]; then
	item atencao "híbrido" "notebook Intel + NVIDIA: a tela usa a Intel" \
	    "o FreeBSD não alterna entre as placas em notebooks"
fi
if [ -n "${nvidia:-}" ] && [ -n "${amd:-}" ] && [ "${bateria:-}" = sim ]; then
	item atencao "híbrido" "notebook Ryzen + NVIDIA: a tela usa o vídeo AMD" \
	    "o FreeBSD não alterna entre as placas em notebooks"
fi

# Rede
wifi=$(sysctl -n net.wlan.devices 2>/dev/null)
placawifi=$(pci 'network' | grep -iE 'wi-?fi|wireless|802\.11|ax2|be2' | head -1)
if [ -n "$wifi" ]; then
	item ok "Wi-Fi" "$wifi ${placawifi:+- ${placawifi#* }}" \
	    "conecte com: lume wifi"
elif [ -n "$placawifi" ]; then
	item falha "Wi-Fi" "${placawifi#* } sem driver carregado" \
	    "falta o firmware? veja dmesg | grep -i iwl"
else
	item atencao "Wi-Fi" "nenhuma placa encontrada"
fi
for i in $(ifconfig -l ether 2>/dev/null); do
	case "$i" in wlan*) continue ;; esac
	st=$(ifconfig "$i" | awk '/status:/ {print $2}')
	item ok "rede $i" "cabo ${st:-desconhecido}"
done

# Som
if grep -q '^pcm' /dev/sndstat 2>/dev/null; then
	item ok "som" "$(grep '^pcm' /dev/sndstat | head -1 | cut -c1-60)" \
	    "alto-falantes devem funcionar; o microfone embutido talvez não"
else
	item falha "som" "nenhuma placa de som reconhecida"
fi

notebook=nao
acpiconf -i 0 >/dev/null 2>&1 && notebook=sim

# Entrada
if [ "$notebook" = sim ]; then
	if devinfo 2>/dev/null | grep -qE '^ *(hmt|hms|psm)[0-9]'; then
		item ok "touchpad" "$(devinfo | grep -oE '(hmt|hms|psm)[0-9]+' | sort -u | tr '\n' ' ')"
	else
		item atencao "touchpad" "não encontrado"
	fi
fi
if sysctl -n dev.acpi_ibm.0.%desc >/dev/null 2>&1; then
	item ok "teclas ThinkPad" "acpi_ibm ativo (volume, brilho, mute)"
elif [ "$fabricante" = "LENOVO" ] && [ "$notebook" = sim ]; then
	item atencao "teclas ThinkPad" "acpi_ibm não carregado (kldload acpi_ibm)"
fi

# Bateria e suspender (notebooks)
if [ "$notebook" = sim ]; then
	cap=$(acpiconf -i 0 | awk -F':\t*' '/Remaining capacity/ {print $2}')
	est=$(acpiconf -i 0 | awk -F':\t*' '/^State/ {print $2}')
	item ok "bateria" "${cap:-?} (${est:-?})"
	estados=$(sysctl -n hw.acpi.supported_sleep_state 2>/dev/null)
	case "$estados" in
	*S3*)	item ok "suspender" "S3 disponível ($estados)" \
		    "fechar a tampa suspende" ;;
	*)	item atencao "suspender" "sem S3 (${estados:-nenhum}); só Modern Standby" \
		    "suspender ao fechar a tampa pode não funcionar neste modelo" ;;
	esac
fi

# USB: câmera, Bluetooth, leitor de digital
usb=$(usbconfig list 2>/dev/null)
if echo "$usb" | grep -qiE 'camera|webcam|video'; then
	item ok "câmera" "$(echo "$usb" | grep -iE 'camera|webcam|video' | head -1 | sed 's/.*: <//; s/>.*//')" \
	    "funciona com o webcamd (lume desktop instala)"
else
	[ "$notebook" = sim ] &&
	    item atencao "câmera" "não encontrada no USB (pode ser MIPI, sem suporte)"
fi
if echo "$usb" | grep -qi 'bluetooth'; then
	item atencao "Bluetooth" "encontrado; suporte limitado no FreeBSD"
fi
if echo "$usb" | grep -qiE 'fingerprint|synaptics.*fp|goodix|validity'; then
	item falha "digital" "leitor de digital sem suporte no FreeBSD"
fi

printf "  ------------------------------------------------------------\n"
printf "  ${VERDE}%d ok${FIM}, ${AMARELO}%d atenção${FIM}, ${VERMELHO}%d não funciona${FIM}\n\n" \
    "$OKS" "$ATENCAO" "$FALHAS"
if [ -f /etc/lume-ao-vivo ]; then
	printf "  Está tudo certo para você? Instale com: lume instalar\n\n"
fi

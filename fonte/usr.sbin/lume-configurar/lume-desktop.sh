#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-desktop - instala o desktop KDE Plasma e os drivers do notebook.
#
# Usa os pacotes oficiais do FreeBSD (precisa de internet: cabo ou
# "lume wifi"). Os pacotes essenciais que vêm no pendrive (driver de vídeo,
# firmwares) são instalados a partir dele mesmo, sem internet.
#

LISTA=/usr/share/lume/desktop.txt
LOCAL=/usr/share/lume/pacotes

[ "$(id -u)" -eq 0 ] || { echo "lume desktop: precisa ser root (su -)" >&2; exit 1; }

pacotes=$(sed 's/#.*//' "$LISTA" | tr -s ' \n' ' ')

# Espelho local (opcional): LUME_REPO_URL=http://servidor/caminho usa um
# repositório próprio no lugar dos servidores do FreeBSD.
if [ -n "${LUME_REPO_URL:-}" ]; then
	mkdir -p /usr/local/etc/pkg/repos
	cat > /usr/local/etc/pkg/repos/lume-espelho.conf <<-EOF
	FreeBSD-ports: { enabled: no }
	FreeBSD-ports-kmods: { enabled: no }
	Lume-espelho: { url: "$LUME_REPO_URL", signature_type: "none", enabled: yes }
	EOF
fi

export ASSUME_ALWAYS_YES=yes
if ! pkg -N >/dev/null 2>&1; then
	echo "  Preparando o pkg..."
	if ls "$LOCAL"/pkg-*.pkg >/dev/null 2>&1; then
		pkg add -q "$(ls "$LOCAL"/pkg-*.pkg | head -1)" || exit 1
	else
		pkg bootstrap -f -q || exit 1
	fi
fi

if [ -d "$LOCAL" ]; then
	echo "  Instalando drivers e firmwares do pendrive..."
	for p in "$LOCAL"/*.pkg; do
		case "$p" in */pkg-[0-9]*) continue ;; esac
		pkg add -q -M "$p" >/dev/null 2>&1 || pkg add -q "$p" >/dev/null 2>&1
	done
fi

echo "  Baixando e instalando o desktop (KDE Plasma, Firefox e cia.)..."
echo "  Isso baixa uns 1,8 GB; pode levar um tempo."
# Driver de vídeo conforme a placa (veja lume video).
eval "$(/usr/sbin/lume-video detectar)"
case "$escolha" in
nvidia)	video="$nvidia_pacote"
	[ "$nvidia_pacote" = nvidia-driver ] && video="$video nvidia-settings"
	echo "  Placa NVIDIA: $nvidia -> $nvidia_pacote" ;;
intel)	video="drm-612-kmod libva-intel-media-driver"
	echo "  Vídeo Intel: $intel" ;;
amd)	video="drm-612-kmod"
	echo "  Vídeo AMD: $amd -> $amd_driver" ;;
*)	video=
	echo "  Sem placa NVIDIA, Intel ou AMD: o desktop usa o modo sem aceleração." ;;
esac

if ! pkg update -q || ! pkg install -q $pacotes $video; then
	echo "  ✗ Não deu para baixar os pacotes. Tem internet? (cabo ou: lume wifi)"
	exit 1
fi
if [ "$escolha" = intel ]; then
	pkg install -q -g 'gpu-firmware-intel-kmod-*' ||
	    echo "  ! Firmwares Intel não instalados; tente: pkg install -g 'gpu-firmware-intel-kmod-*'"
fi
if [ "$escolha" = amd ]; then
	pkg install -q -g 'gpu-firmware-amd-kmod-*' 'gpu-firmware-radeon-kmod-*' ||
	    echo "  ! Firmwares AMD não instalados; tente: pkg install -g 'gpu-firmware-amd-kmod-*'"
fi

sysrc -q dbus_enable=YES sddm_enable=YES webcamd_enable=YES cupsd_enable=YES \
    lume_video_enable=YES >/dev/null
sysrc -q kld_list+="cuse" >/dev/null
# Quem é do grupo wheel ganha vídeo e câmera.
for u in $(pw groupshow wheel | awk -F: '{print $4}' | tr ',' ' '); do
	[ "$u" = root ] && continue
	pw groupmod video -m "$u" 2>/dev/null
	pw groupmod webcamd -m "$u" 2>/dev/null
done

# O indexador de arquivos do KDE (Baloo) sem pasta nenhuma para indexar: o
# lume tema (abaixo) grava o baloofilerc.

# Visual "Papel" (e-ink) como padrão; lume tema padrao volta ao do KDE.
/usr/sbin/lume-tema papel

echo "  ✓ Desktop instalado. Ele abre no próximo boot (ou: service sddm start)."

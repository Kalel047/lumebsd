#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-video - detecta a placa de vídeo, escolhe e testa o driver.
#
#   lume video              mostra as placas, o modo e o estado
#   lume video testar       carrega o driver escolhido agora e diz se deu certo
#   lume video detectar     só a detecção, em linhas chave=valor (para scripts)
#   lume video auto         escolhe sozinho a cada boot (padrão)
#   lume video nvidia       sempre o driver NVIDIA
#   lume video intel        sempre o driver Intel (i915, drm 6.12)
#   lume video scfb         sempre o modo sem aceleração (framebuffer UEFI)
#
# No modo auto: placa NVIDIA num desktop (ou sem vídeo Intel) usa o driver
# NVIDIA; notebook híbrido Intel + NVIDIA usa a Intel, que é quem liga na
# tela; só Intel usa a Intel; o resto usa o scfb.
#
# Proteção contra travamento: antes de carregar o driver o boot grava
# /var/db/lume/video-tentando. Se o computador travar, no boot seguinte o
# arquivo ainda existe, o modo cai para scfb e o estado fica "travou".
#

DB=/var/db/lume
XCONF=/usr/local/etc/X11/xorg.conf.d/10-lume-video.conf

uso()
{
	echo "uso: lume video [testar | detectar | auto | nvidia | intel | scfb]" >&2
	exit 2
}

# --- detecção -------------------------------------------------------------

# placas: "vendor|nome" de cada controladora de vídeo PCI
placas()
{
	pciconf -lv 2>/dev/null | awk '
		/^[a-z]/ {
			video = ($0 ~ /class=0x03/)
			v = ""
			if (match($0, / vendor=0x[0-9a-f]+/))
				v = substr($0, RSTART + 8, RLENGTH - 8)
			next
		}
		video && /device *=/ {
			d = $0; sub(/.*device *= */, "", d); gsub(/[\047]/, "", d)
			print v "|" d
			video = 0
		}'
}

# Geração do driver NVIDIA pelo nome do chip: Turing em diante usa o atual;
# Maxwell, Pascal e Volta param no 580; Kepler no 470; Fermi no 390.
nvidia_pacote()
{
	case "$1" in
	GP*|GM*|GV*)	echo nvidia-driver-580 ;;
	GK*)		echo nvidia-driver-470 ;;
	GF*)		echo nvidia-driver-390 ;;
	*)		echo nvidia-driver ;;
	esac
}

detectar()
{
	nvidia=$(placas | awk -F'|' '$1 == "0x10de" {print $2; exit}')
	intel=$(placas | awk -F'|' '$1 == "0x8086" {print $2; exit}')
	bateria=nao
	acpiconf -i 0 >/dev/null 2>&1 && bateria=sim
	if [ -n "$nvidia" ] && { [ -z "$intel" ] || [ "$bateria" = nao ]; }; then
		escolha=nvidia
	elif [ -n "$intel" ]; then
		escolha=intel
	else
		escolha=scfb
	fi
	echo "escolha=$escolha"
	echo "nvidia=$nvidia"
	[ -n "$nvidia" ] && echo "nvidia_pacote=$(nvidia_pacote "$nvidia")"
	echo "intel=$intel"
	echo "bateria=$bateria"
}

# --- configuração do X ------------------------------------------------------

xorg()
{
	# xorg intel|nvidia|scfb: o Intel usa o "modesetting" que o X acha sozinho
	mkdir -p "$(dirname "$XCONF")"
	case "$1" in
	intel)	rm -f "$XCONF"; return ;;
	esac
	cat > "$XCONF" <<-EOF
	# Criado por lume video ($1). Para mudar: lume video auto|nvidia|intel|scfb
	Section "Device"
	    Identifier "Tela"
	    Driver "$1"
	EndSection
	EOF
}

# --- carregar drivers --------------------------------------------------------

esperar_dispositivo()
{
	i=0
	while [ ! -c "$1" ] && [ $i -lt 10 ]; do
		sleep 1; i=$((i + 1))
	done
	[ -c "$1" ]
}

carregar_intel()
{
	[ -f /boot/modules/i915kms.ko ] || return 2
	# Algumas gerações novas (Meteor Lake) pedem "force_probe".
	kenv -q compat.linuxkpi.i915_force_probe >/dev/null ||
	    kenv compat.linuxkpi.i915_force_probe='*' >/dev/null
	kldstat -q -m i915kms || kldload i915kms 2>/dev/null || return 1
	esperar_dispositivo /dev/dri/card0
}

carregar_nvidia()
{
	[ -f /boot/modules/nvidia-modeset.ko ] || return 2
	kldstat -q -m nvidia_modeset || kldload nvidia-modeset 2>/dev/null ||
	    return 1
	esperar_dispositivo /dev/nvidiactl && [ -c /dev/nvidia0 ]
}

modo_atual()
{
	sysrc -n lume_video 2>/dev/null || echo auto
}

# alvo: o driver que o modo atual manda usar neste computador
alvo()
{
	modo=$(modo_atual)
	case "$modo" in
	auto)	detectar | awk -F= '$1 == "escolha" {print $2}' ;;
	*)	echo "$modo" ;;
	esac
}

# tentar intel|nvidia: carrega com a proteção contra travamento.
# Grava o estado: intel, nvidia, falhou ou sem-driver.
tentar()
{
	mkdir -p "$DB"
	echo "$1 $(date)" > "$DB/video-tentando"
	sync
	carregar_"$1"
	r=$?
	rm -f "$DB/video-tentando"
	case $r in
	0)	echo "$1" > "$DB/video-estado"; xorg "$1" ;;
	2)	echo sem-driver > "$DB/video-estado"; xorg scfb ;;
	*)	echo falhou > "$DB/video-estado"; xorg scfb ;;
	esac
	sync
	return $r
}

# Chamado por /etc/rc.d/lume_video no boot.
boot()
{
	mkdir -p "$DB"
	quero=$(alvo)
	if [ -f "$DB/video-tentando" ]; then
		echo "lume video: o driver $(cut -d' ' -f1 "$DB/video-tentando") travou o último boot; usando scfb."
		rm -f "$DB/video-tentando"
		echo travou > "$DB/video-estado"
		[ "$(modo_atual)" = auto ] && { xorg scfb; return 0; }
	elif [ "$(modo_atual)" = auto ] &&
	    [ "$(cat "$DB/video-estado" 2>/dev/null)" = travou ]; then
		xorg scfb
		return 0
	fi
	case "$quero" in
	intel|nvidia)
		tentar "$quero" ||
		    echo "lume video: o driver $quero não subiu; usando scfb." ;;
	*)	xorg scfb
		echo scfb > "$DB/video-estado" ;;
	esac
}

mostrar()
{
	eval "$(detectar)"
	estado=$(cat "$DB/video-estado" 2>/dev/null || echo "nunca testado")
	[ -n "$nvidia" ] && echo "  NVIDIA: $nvidia (pacote $(nvidia_pacote "$nvidia"))"
	[ -n "$intel" ] && echo "  Intel:  $intel"
	[ -z "$nvidia$intel" ] && echo "  Nenhuma placa NVIDIA ou Intel; o desktop usa o scfb."
	echo "  modo:   $(modo_atual) -> $(alvo)"
	echo "  estado: $estado"
	case "$estado" in
	intel|nvidia) echo "  O driver $estado está ativo: vídeo acelerado." ;;
	travou)	echo "  Um driver travou o computador; o desktop usa scfb."
		echo "  Para tentar de novo: lume video auto" ;;
	falhou)	echo "  O driver não reconheceu a placa; o desktop usa scfb." ;;
	sem-driver) echo "  O driver não está instalado: lume desktop" ;;
	esac
}

testar()
{
	quero=$(alvo)
	case "$quero" in
	intel|nvidia) ;;
	*)	echo "  Nada para testar: este computador usa o modo scfb."
		return 0 ;;
	esac
	echo "  Carregando o driver $quero..."
	echo "  Se a tela travar, desligue no botão e ligue de novo:"
	echo "  o LumeBSD vai lembrar e usar o modo sem aceleração."
	tentar "$quero"
	case $? in
	0)	echo "  ✓ Funcionou: vídeo acelerado com o driver $quero." ;;
	2)	echo "  ! O driver $quero não está instalado (lume desktop instala)." ;;
	*)	echo "  ✗ O driver não reconheceu a placa. O desktop usará scfb."
		if [ "$quero" = nvidia ]; then
			eval "$(detectar)"
			[ "$nvidia_pacote" != nvidia-driver ] &&
			    echo "    Esta placa precisa do $nvidia_pacote; lume desktop instala o certo."
		fi ;;
	esac
	dmesg | grep -iE 'i915|drm|nvidia|nvrm' | tail -4 | sed 's/^/    /'
}

case "${1:-}" in
""|detectar|--boot) ;;
*)	[ "$(id -u)" -eq 0 ] || { echo "lume video: precisa ser root (su -)" >&2
	    exit 1; } ;;
esac

case "${1:-}" in
"")		mostrar ;;
detectar)	detectar ;;
--boot)		boot ;;
testar)		testar ;;
auto|nvidia|intel|scfb)
		sysrc -q lume_video="$1" >/dev/null
		[ "$1" = auto ] && rm -f "$DB/video-estado"
		[ "$1" = scfb ] && xorg scfb
		echo "  modo de vídeo: $1 (vale a partir do próximo boot)" ;;
*)		uso ;;
esac

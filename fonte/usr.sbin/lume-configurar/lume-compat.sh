#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-compat - programas de outros sistemas no LumeBSD.
#
#   Windows: o Wine roda muitos .exe; dois cliques num .exe (ou .msi) no
#            Dolphin passam por "lume-compat abrir", que oferece instalar o
#            Wine na primeira vez.
#   Linux:   a camada Linux do FreeBSD (linux64.ko e as montagens de
#            /compat/linux) roda programas feitos para Linux, como o Chrome
#            e o Discord; os pacotes linux-* dependem dela.
#

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export PATH

[ "$(id -u)" -eq 0 ] && root=s || root=n

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
erro() { printf '  \033[31m✗\033[0m %s\n' "$*" >&2; exit 1; }
precisa_root() { [ $root = s ] || erro "precisa ser root (su -)"; }

tem_wine()  { [ -x /usr/local/bin/wine ]; }
linux_on()  { kldstat -q -m linuxelf 2>/dev/null || kldstat -q -n linux64 2>/dev/null; }
linux_cfg() { [ "$(sysrc -n linux_enable 2>/dev/null)" = YES ]; }

estado() {
	echo
	if tem_wine; then
		ok "Windows: Wine $(wine --version 2>/dev/null | sed 's/^wine-//') instalado (dois cliques num .exe)"
	else
		echo "  · Windows: desligado (lume loja instalar windows)"
	fi
	if linux_on && linux_cfg; then
		ok "Linux: camada ligada$( [ -d /compat/linux/usr ] && echo ', com o Rocky Linux 9 em /compat/linux')"
	elif linux_cfg; then
		echo "  · Linux: ligada no boot, ainda não carregada (lume compat linux ligar)"
	else
		echo "  · Linux: desligada (lume compat linux ligar)"
	fi
	echo
	echo "  Apps da web (Office 365, Teams, Overleaf...): lume loja lista web"
	echo
}

linux() {
	case "${1:-}" in
	ligar)
		precisa_root
		sysrc -q linux_enable=YES >/dev/null
		# Carrega o módulo e monta /compat/linux/{proc,sys,dev}.
		service linux start >/dev/null 2>&1 || service linux restart >/dev/null 2>&1
		linux_on || erro "não deu para carregar a camada Linux (kldload linux64)"
		ok "Camada Linux ligada (e no boot)."
		[ -d /compat/linux/usr ] ||
		    echo "  Para os programas: lume loja instalar linux (ou chrome, discord)"
		;;
	desligar)
		precisa_root
		sysrc -q linux_enable=NO >/dev/null
		ok "Camada Linux desligada a partir do próximo boot."
		;;
	*)	estado ;;
	esac
}

# Abre um .exe/.msi/.bat com o Wine (o atalho "Programa do Windows" do
# Dolphin chama isto). Na primeira vez, oferece instalar o Wine pelo app
# Programas do LumeBSD, que pede a senha e mostra o progresso.
abrir() {
	f=$1
	[ -f "$f" ] || erro "arquivo não encontrado: $f"
	if ! tem_wine; then
		if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v kdialog >/dev/null; then
			kdialog --title "Programa do Windows" --yes-label "Instalar" \
			    --no-label "Cancelar" --yesno "<b>${f##*/}</b> é um programa do Windows.<br><br>O LumeBSD abre muitos deles com o <b>Wine</b>, que ainda não está instalado (baixa cerca de 700 MB).<br><br>Instalar agora?" ||
			    exit 0
			/usr/bin/lume-programas --instalar windows
			tem_wine || exit 0
		else
			erro "o Wine não está instalado: lume loja instalar windows"
		fi
	fi
	# A primeira vez cria a "unidade C:" em ~/.wine e demora um pouco.
	if [ ! -d "${WINEPREFIX:-$HOME/.wine}" ] && command -v kdialog >/dev/null; then
		kdialog --title "Programa do Windows" --passivepopup \
		    "Preparando o Wine na primeira vez; leva cerca de um minuto." 20 &
	fi
	cd "$(dirname "$f")" || exit 1
	case "$f" in
	*.[mM][sS][iI])	exec wine msiexec /i "$f" ;;
	*.[bB][aA][tT]|*.[cC][mM][dD])	exec wine cmd /c "$f" ;;
	*)		exec wine start /unix "$f" ;;
	esac
}

case "${1:-}" in
""|estado)	estado ;;
linux)		shift; linux "$@" ;;
windows)	if tem_wine; then ok "Wine instalado: dois cliques num .exe abrem o programa."
		else echo "  Para programas do Windows: lume loja instalar windows"; fi ;;
abrir)		shift; abrir "$1" ;;
*)	echo "uso: lume compat [estado | linux [ligar | desligar] | windows | abrir ARQUIVO]" >&2
	exit 1 ;;
esac

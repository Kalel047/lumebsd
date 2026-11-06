#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-compat - programas de outros sistemas no LumeBSD.
#
#   Windows: o Wine roda muitos .exe; dois cliques num .exe (ou .msi) no
#            Dolphin passam por "lume-compat abrir", que oferece instalar o
#            Wine na primeira vez. Programas de 64 bits vão pelo wine64.bin;
#            os de 32 bits, pela parte de 32 bits do Wine, que cada usuário
#            baixa uma vez para a pasta pessoal (o pkg32.sh do Wine, com os
#            pacotes i386 do FreeBSD 14: o FreeBSD 15 não tem mais pacotes
#            i386) e que usa as bibliotecas de 32 bits do sistema (lib32).
#   Linux:   a camada Linux do FreeBSD (linux64.ko e as montagens de
#            /compat/linux) roda programas feitos para Linux, como o Chrome
#            e o Discord; os pacotes linux-* dependem dela.
#   Mac:     nada roda programas do Mac no FreeBSD ainda (docs/MACOS.md).
#            Dois cliques num .dmg explicam e abrem a loja procurando o
#            programa: muitos têm versão para Windows ou Linux, ou um
#            substituto no catálogo.
#
# Para testes: LUME_PKG32_REPOS (uma pasta de configurações de repositório
# do pkg) e LUME_PKG32_REPO (o nome do repositório nela) trocam de onde vem a
# parte de 32 bits.
#

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export PATH

[ "$(id -u)" -eq 0 ] && root=s || root=n

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
erro() { printf '  \033[31m✗\033[0m %s\n' "$*" >&2; exit 1; }
precisa_root() { [ $root = s ] || erro "precisa ser root (su -)"; }

WINE32=${WINE_i386_ROOT:-$HOME/.i386-wine-pkg}
PKG32=/usr/local/share/wine/pkg32.sh

tem_wine()   { [ -x /usr/local/bin/wine64.bin ]; }
tem_wine32() { [ -x "$WINE32/usr/local/bin/wine" ]; }
tem_lib32()  { [ -f /usr/lib32/libc.so.7 ]; }

# 64 ou 32: o tipo de máquina no cabeçalho PE de um .exe ("?" se não for).
pe_bits()
{
	off=$(od -An -tu4 -j60 -N4 "$1" 2>/dev/null | tr -d ' ')
	[ -n "$off" ] || { echo "?"; return; }
	case $(od -An -tx2 -j$((off + 4)) -N2 "$1" 2>/dev/null | tr -d ' ') in
	8664|aa64)	echo 64 ;;
	014c)		echo 32 ;;
	*)		echo "?" ;;
	esac
}
linux_on()  { kldstat -q -m linuxelf 2>/dev/null || kldstat -q -n linux64 2>/dev/null; }
linux_cfg() { [ "$(sysrc -n linux_enable 2>/dev/null)" = YES ]; }

grafico=n
[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v kdialog >/dev/null && grafico=s

aviso() {
	# aviso TÍTULO TEXTO: uma janela (no desktop) ou o terminal.
	if [ $grafico = s ]; then
		kdialog --title "$1" --sorry "$2"
	else
		printf '  %s\n' "$2" | sed 's/<br>/\n  /g; s/<[^>]*>//g' >&2
	fi
}

estado() {
	echo
	if tem_wine; then
		ok "Windows: Wine $(pkg query %v wine 2>/dev/null) (dois cliques num .exe)"
		if tem_wine32; then
			ok "Windows 32 bits: pronto para $(id -un)"
		elif tem_lib32; then
			echo "  · Windows 32 bits: baixado na primeira vez que abrir um (lume compat windows32)"
		else
			echo "  · Windows 32 bits: este sistema não tem as bibliotecas de 32 bits"
		fi
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
	echo "  · Mac: ainda não (dois cliques num .dmg mostram o que usar no lugar)"
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

# A parte de 32 bits do Wine, para o usuário que roda (sem senha: vai para
# ~/.i386-wine-pkg). Também atualiza, quando o Wine do sistema mudou de
# versão. Devolve 0 se ficou pronta.
wine32() {
	[ $root = n ] || erro "a parte de 32 bits do Wine é de cada usuário: rode sem ser root"
	tem_wine || erro "o Wine não está instalado: lume loja instalar windows"
	tem_lib32 || erro "este sistema não tem as bibliotecas de 32 bits (/usr/lib32)"
	acao=install
	if tem_wine32; then
		v32=$(env -u WINELOADERNOEXEC "$WINE32/usr/local/bin/wine" --version 2>/dev/null)
		v64=$(env -u WINELOADERNOEXEC /usr/local/bin/wine64 --version 2>/dev/null)
		[ "$v32" = "$v64" ] && return 0
		acao=upgrade
	fi
	set --
	[ -n "${LUME_PKG32_REPOS:-}" ] && set -- -R "$LUME_PKG32_REPOS"
	repo=${LUME_PKG32_REPO:-FreeBSD-ports}
	mkdir -p "$HOME/.cache"
	log=$HOME/.cache/lume-wine32.log
	ref=
	if [ $grafico = s ]; then
		ref=$(kdialog --title "Programa do Windows" --progressbar \
		    "Baixando a parte de 32 bits do Wine (uma vez só). Leva alguns minutos." 0 2>/dev/null)
	else
		echo "  Baixando a parte de 32 bits do Wine (uma vez só)..."
	fi
	if [ $acao = install ]; then
		ASSUME_ALWAYS_YES=yes "$PKG32" --old "$@" install -r "$repo" wine mesa-dri > "$log" 2>&1
	else
		ASSUME_ALWAYS_YES=yes "$PKG32" --old "$@" upgrade -r "$repo" > "$log" 2>&1
	fi
	rc=$?
	if [ -n "$ref" ]; then
		for q in qdbus6 qdbus; do
			command -v $q >/dev/null 2>&1 && { $q $ref org.kde.kdialog.ProgressDialog.close >/dev/null 2>&1; break; }
		done
	fi
	if [ $rc -ne 0 ] || ! tem_wine32; then
		aviso "Programa do Windows" "Não deu para baixar a parte de 32 bits do Wine. Tem internet?<br><br>Detalhes em $log"
		return 1
	fi
	return 0
}

# Abre um .exe/.msi/.bat com o Wine (o atalho "Programa do Windows" do
# Dolphin chama isto). Na primeira vez, oferece instalar o Wine pelo app
# Programas do LumeBSD, que pede a senha e mostra o progresso.
abrir_windows() {
	f=$1
	bits=64
	case "$f" in
	*.[eE][xX][eE]) bits=$(pe_bits "$f") ;;
	esac
	if ! tem_wine; then
		if [ $grafico = s ]; then
			kdialog --title "Programa do Windows" --yes-label "Instalar" \
			    --no-label "Cancelar" --yesno "<b>${f##*/}</b> é um programa do Windows.<br><br>O LumeBSD abre muitos deles com o <b>Wine</b>, que ainda não está instalado (baixa cerca de 700 MB).<br><br>Instalar agora?" ||
			    exit 0
			/usr/bin/lume-programas --instalar windows
			tem_wine || exit 0
		else
			erro "o Wine não está instalado: lume loja instalar windows"
		fi
	fi
	if [ "$bits" = 32 ] && ! tem_wine32; then
		if ! tem_lib32; then
			aviso "Programa do Windows" "<b>${f##*/}</b> é um programa do Windows de <b>32 bits</b>, e este sistema não tem as bibliotecas de 32 bits que o Wine usa para eles.<br><br>Procure a versão de 64 bits no site do programa, a versão para a web, ou um substituto no app Programas do LumeBSD."
			exit 0
		fi
		if [ $grafico = s ]; then
			kdialog --title "Programa do Windows" --yes-label "Baixar" \
			    --no-label "Cancelar" --yesno "<b>${f##*/}</b> é um programa do Windows de <b>32 bits</b> (como muitos instaladores e programas mais antigos).<br><br>Para ele, o Wine precisa da parte de 32 bits, que vai para a sua pasta pessoal: baixa cerca de 480 MB e ocupa uns 2,6 GB. Não pede senha.<br><br>Baixar agora?" ||
			    exit 0
		fi
		wine32 || exit 1
	fi
	# A primeira vez cria a "unidade C:" em ~/.wine e demora um pouco.
	if [ ! -d "${WINEPREFIX:-$HOME/.wine}" ] && [ $grafico = s ]; then
		kdialog --title "Programa do Windows" --passivepopup \
		    "Preparando o Wine na primeira vez; leva cerca de um minuto." 20 &
	fi
	cd "$(dirname "$f")" || exit 1
	if tem_wine32 && [ "$bits" != 64 ]; then
		# 32 bits (e .msi/.bat com a parte de 32 bits pronta): o script
		# wine do FreeBSD, que junta o Wine de 32 e o de 64 bits.
		case "$f" in
		*.[mM][sS][iI])	exec wine msiexec /i "$f" ;;
		*.[bB][aA][tT]|*.[cC][mM][dD])	exec wine cmd /c "$f" ;;
		*)		exec wine "$f" ;;
		esac
	fi
	# Só 64 bits: tudo pelo wine64.bin, também os processos que o Wine abre
	# sozinho (o wineboot da primeira vez): os scripts wine e wine64 do
	# FreeBSD exigem a parte de 32 bits. O LD_BIND_NOW é o do script wine64
	# (bug 50257 do Wine). Sem WINESERVER: com ele, o Wine volta a procurar
	# o script wine.
	WINELOADER=/usr/local/bin/wine64.bin
	LD_BIND_NOW=1
	export WINELOADER LD_BIND_NOW
	case "$f" in
	*.[mM][sS][iI])	exec "$WINELOADER" msiexec /i "$f" ;;
	*.[bB][aA][tT]|*.[cC][mM][dD])	exec "$WINELOADER" cmd /c "$f" ;;
	*)		exec "$WINELOADER" "$f" ;;
	esac
}

# O nome do programa a partir do arquivo do Mac: "Spotify-1.2.3.dmg",
# "Microsoft_Excel_16.89_Installer.pkg" e "VLC-3.0.21-universal.dmg" viram
# "Spotify", "Microsoft Excel" e "VLC".
nome_mac() {
	n=${1##*/}
	n=${n%.*}
	echo "$n" | tr '_-' '  ' | awk '{
	    s = ""
	    for (i = 1; i <= NF; i++) {
		w = tolower($i)
		if ($i ~ /[0-9]/ || w ~ /^(universal|arm64|x64|x86|intel|apple|silicon|mac|macos|osx|installer|install|setup|latest|beta|dmg|app)$/)
			continue
		s = s (s == "" ? "" : " ") $i
	    }
	    print s
	}'
}

abrir_mac() {
	f=$1
	n=$(nome_mac "$f")
	[ -n "$n" ] || n=${f##*/}
	txt="<b>${f##*/}</b> é um programa do <b>Mac</b>.<br><br>O LumeBSD ainda não roda programas do Mac. Mas muitos deles têm uma versão para Windows (que abre com o Wine) ou para Linux, ou um substituto que faz o mesmo trabalho.<br><br>Procurar <b>$n</b> nos Programas do LumeBSD?"
	if [ $grafico = s ]; then
		kdialog --title "Programa do Mac" --yes-label "Procurar" \
		    --no-label "Ler a Ajuda" --cancel-label "Fechar" --yesnocancel "$txt"
		case $? in
		0)	exec /usr/bin/lume-programas --procurar "$n" ;;
		1)	exec /usr/sbin/lume-ajuda mac ;;
		esac
		exit 0
	fi
	echo "  ${f##*/} é um programa do Mac; o LumeBSD ainda não roda programas do Mac."
	echo "  O que usar no lugar: lume loja procurar \"$n\"   (e lume ajuda mac)"
}

abrir() {
	f=$1
	[ -e "$f" ] || erro "arquivo não encontrado: $f"
	case "$f" in
	*.[dD][mM][gG]|*.[aA][pP][pP]|*.[aA][pP][pP]/|*.[iI][pP][aA])
		abrir_mac "$f" ;;
	*.[pP][kK][gG])
		# .pkg do Mac (xar) ou do FreeBSD (tar)? Os do Mac começam com "xar!".
		if [ "$(head -c 4 "$f" 2>/dev/null)" = "xar!" ]; then
			abrir_mac "$f"
		else
			aviso "Pacote do FreeBSD" "<b>${f##*/}</b> é um pacote do FreeBSD. Para instalar: <b>pkg add ${f##*/}</b>, como root."
		fi ;;
	*)	[ -f "$f" ] || erro "arquivo não encontrado: $f"
		abrir_windows "$f" ;;
	esac
}

case "${1:-}" in
""|estado)	estado ;;
linux)		shift; linux "$@" ;;
windows)	if tem_wine; then ok "Wine instalado: dois cliques num .exe abrem o programa."
		else echo "  Para programas do Windows: lume loja instalar windows"; fi ;;
windows32)	wine32 && ok "A parte de 32 bits do Wine está pronta para $(id -un)." ;;
mac)		shift; [ -n "${1:-}" ] && abrir_mac "$1" || estado ;;
abrir)		shift; abrir "$1" ;;
*)	echo "uso: lume compat [estado | linux [ligar | desligar] | windows | windows32 | abrir ARQUIVO]" >&2
	exit 1 ;;
esac

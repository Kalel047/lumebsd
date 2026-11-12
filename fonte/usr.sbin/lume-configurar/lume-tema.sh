#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-tema - o visual "Papel" do LumeBSD para o KDE Plasma (estética e-ink).
#
#   lume tema           mostra qual tema está em uso
#   lume tema papel     aplica o Papel (o lume desktop já faz isso); com os
#                       dois jeitos dele: o claro e o Papel noite (escuro)
#   lume tema papel noite
#                       o mesmo, com o Papel noite como padrão do sistema
#   lume tema escuro    passa o seu usuário para o Papel noite, na hora
#   lume tema claro     e de volta para o Papel
#   lume tema padrao    volta ao visual original do KDE
#   lume tema integrar  põe no KDE o que é do sistema: os apps Atualizações
#                       e Programas do LumeBSD (menu, aviso na sessão,
#                       polkit), a Ajuda, os dois cliques num .exe (lume
#                       compat) e o "Versões anteriores" do Dolphin (o tema
#                       chama isto, e o lume_saude também, depois de uma
#                       atualização), os efeitos do KWin do LumeBSD e os
#                       atalhos de teclado do KWin (o kglobalacceld no D-Bus)
#   lume tema tinta [liga | desliga | NÚMERO | grao N | viscosidade MS | noite M]
#                       o filtro e-ink (efeito lume_tinta do KWin): a tela em
#                       papel e tinta; fotos, vídeos e tela cheia ficam com
#                       cor. NÚMERO é a intensidade, de 0 a 100; grao, as
#                       fibras do papel (0 a 100); viscosidade, quanto a tinta
#                       demora para assentar (ms; 0 desliga); noite, o papel e
#                       a tinta do Papel noite (auto: segue o tema)
#   lume tema animacao [refresh | esmaecer | nenhuma]
#                       como as janelas abrem e fecham
#
# escuro, claro, tinta e animacao valem para o usuário que roda (sem root),
# na hora.
#
# Papel, tinta e cinzas; sem sombras nem transparência; fontes
# IBM Plex; ícones Breeze convertidos para tons de cinza; papel de parede
# com o farol em traço e, no login e no bloqueio, a versão "noite".
#
# Tudo vira padrão do sistema (/usr/local/etc/xdg): cada usuário pode mudar
# o que quiser nas Configurações do Sistema.
#

T=/usr/share/lume/tema
P=/usr/local
XDG=$P/etc/xdg
LNF=$P/share/plasma/look-and-feel/org.lumebsd.papel
LNF_NOITE=$P/share/plasma/look-and-feel/org.lumebsd.papel-noite
ICONES=$P/share/icons/lume-papel
ICONES_NOITE=$P/share/icons/lume-papel-noite
MARCA=$P/etc/xdg/.lume-tema-papel

# Arquivos que o tema coloca, para poder tirar depois.
CONFS="kdeglobals kwinrc breezerc ksplashrc plasmarc konsolerc kscreenlockerrc kcm-about-distrorc"
M=/usr/share/lume/marca

instalar()
{
	# instalar origem destino [modo]
	mkdir -p "$(dirname "$2")"
	install -m "${3:-0444}" "$1" "$2"
}

papel_de_parede()
{
	# papel_de_parede Id Nome imagem
	d=$P/share/wallpapers/$1
	instalar "$3" "$d/contents/images/3840x2160.png"
	cat > "$d/metadata.json" <<-EOF
	{ "KPlugin": { "Id": "$1", "Name": "$2", "License": "BSD-2-Clause",
	  "Authors": [ { "Name": "LumeBSD" } ] } }
	EOF
}

# Ícones Breeze em tons de cinza: troca cada cor #rrggbb (ou #rgb) dos SVG
# pela luminância dela. Os ícones simbólicos já seguem as cores do tema.
#   icones_cinza ORIGEM DESTINO NOME (o Papel sai do Breeze; o Papel noite,
#   do Breeze Dark, que tem os traços claros para o fundo escuro). O corpo
#   das pastas é a cor de destaque do esquema (ColorScheme-Accent): a tinta
#   no Papel, um cinza médio no Papel noite.
icones_cinza()
{
	origem=$P/share/icons/$1
	destino=$2
	[ -d "$origem" ] || { echo "  (sem os ícones $1; pulei os ícones $3)"; return; }
	rm -rf "$destino"
	cp -RP "$origem" "$destino"
	sed -i '' -e "s/^Name=.*/Name=$3 (LumeBSD)/" \
	    -e "s/^Comment=.*/Comment=$1 em tons de cinza (LumeBSD)/" \
	    "$destino/index.theme"
	# O Breeze Dark herda do Breeze o que não tem; aqui, do Papel.
	sed -i '' -e 's/^Inherits=breeze$/Inherits=lume-papel/' "$destino/index.theme"
	find "$destino" -type f -name '*.svg' -print0 |
	    xargs -0 -n 400 awk '
	function hx(c) { return index("0123456789abcdef", c) - 1 }
	function cinza(h,   r, g, b, y) {
		h = tolower(h)
		if (length(h) == 3)
			h = substr(h,1,1) substr(h,1,1) substr(h,2,1) substr(h,2,1) \
			    substr(h,3,1) substr(h,3,1)
		r = hx(substr(h,1,1)) * 16 + hx(substr(h,2,1))
		g = hx(substr(h,3,1)) * 16 + hx(substr(h,4,1))
		b = hx(substr(h,5,1)) * 16 + hx(substr(h,6,1))
		y = int(0.299 * r + 0.587 * g + 0.114 * b + 0.5)
		return sprintf("#%02x%02x%02x", y, y, y)
	}
	FNR == 1 { if (saida != "") close(saida); saida = FILENAME ".papel" }
	{
		linha = $0; feito = ""
		while (match(linha, /#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]([0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f])?/)) {
			cor = substr(linha, RSTART + 1, RLENGTH - 1)
			depois = substr(linha, RSTART + RLENGTH, 1)
			if (depois ~ /[0-9A-Za-z]/) {
				feito = feito substr(linha, 1, RSTART + RLENGTH - 1)
			} else {
				feito = feito substr(linha, 1, RSTART - 1) cinza(cor)
			}
			linha = substr(linha, RSTART + RLENGTH)
		}
		print feito linha > saida
	}'
	find "$destino" -type f -name '*.svg.papel' | while read -r f; do
		mv "$f" "${f%.papel}"
	done
	# Programas que não têm ícone no Breeze (o Firefox traz o próprio, em cores).
	for app in firefox org.mozilla.firefox; do
		for t in 32 48 64; do
			mkdir -p "$destino/apps/$t"
			cp "$T/navegador.svg" "$destino/apps/$t/$app.svg"
		done
	done
	command -v gtk-update-icon-cache >/dev/null &&
	    gtk-update-icon-cache -q -f "$destino" 2>/dev/null
	echo "  ícones $3: $(find "$destino" -type f -name '*.svg' | wc -l | tr -d ' ') em tons de cinza"
}

aplicar()
{
	[ -x $P/bin/plasmashell ] || {
		echo "  O KDE Plasma não está instalado: rode lume desktop." >&2
		exit 1
	}
	# O padrão do sistema (quem entrar pela primeira vez): o claro ou o noite.
	case "${1:-}" in
	noite|escuro)	sufixo=-noite; nome="Papel noite" ;;
	*)		sufixo=; nome=Papel ;;
	esac
	echo "  Aplicando o tema $nome..."
	instalar $T/Papel.colors $P/share/color-schemes/Papel.colors
	instalar $T/PapelNoite.colors $P/share/color-schemes/PapelNoite.colors
	for k in Papel PapelNoite; do
		instalar $T/$k.colorscheme $P/share/konsole/$k.colorscheme
		instalar $T/$k.profile $P/share/konsole/$k.profile
	done
	# Os dois temas globais: aparecem em Configurações do Sistema >
	# Aparência > Tema global, e o lume tema escuro/claro troca entre eles.
	instalar $T/metadata.json $LNF/metadata.json
	instalar $T/defaults $LNF/contents/defaults
	instalar $T/layout.js $LNF/contents/layouts/org.kde.plasma.desktop-layout.js
	instalar $T/papel.png $LNF/contents/previews/preview.png
	instalar $T/metadata-noite.json $LNF_NOITE/metadata.json
	instalar $T/defaults-noite $LNF_NOITE/contents/defaults
	instalar $T/layout-noite.js $LNF_NOITE/contents/layouts/org.kde.plasma.desktop-layout.js
	instalar $T/papel-noite.png $LNF_NOITE/contents/previews/preview.png
	papel_de_parede LumePapel "Papel (LumeBSD)" $T/papel.png
	papel_de_parede LumePapelNoite "Papel noite (LumeBSD)" $T/papel-noite.png
	for c in $CONFS; do
		if [ -n "$sufixo" ] && [ -f $T/$c$sufixo ]; then
			instalar $T/$c$sufixo $XDG/$c 0644
		else
			instalar $T/$c $XDG/$c 0644
		fi
	done
	# Fontes: IBM Plex como padrão para sans, serif e mono.
	instalar $T/fontes.conf $P/etc/fonts/conf.d/60-lume-papel.conf 0644
	# Programas GTK (Firefox e cia.) com o mesmo jeito.
	instalar $T/gtk-settings$sufixo.ini $XDG/gtk-3.0/settings.ini 0644
	instalar $T/gtk-settings$sufixo.ini $XDG/gtk-4.0/settings.ini 0644
	integrar
	# Tela de login
	instalar $T/sddm.conf $P/etc/sddm.conf.d/lume.conf 0644
	[ -d $P/share/sddm/themes/breeze ] &&
	    instalar $T/sddm-breeze.conf.user \
	    $P/share/sddm/themes/breeze/theme.conf.user 0644
	icones_cinza breeze "$ICONES" Papel
	icones_cinza breeze-dark "$ICONES_NOITE" "Papel noite"
	marca
	echo "${nome}" > "$MARCA.padrao"
	date > "$MARCA"
	echo "  ✓ Tema $nome aplicado. Vale para quem entrar a partir de agora;"
	echo "    quem já usa o KDE escolhe \"$nome (LumeBSD)\" em"
	echo "    Configurações do Sistema > Aparência > Tema global, ou roda"
	echo "    lume tema escuro / lume tema claro."
}

integrar()
{
	[ -x $P/bin/plasmashell ] || return 0
	# Com o Papel aplicado, o papel de parede acompanha a versão do sistema.
	if [ -f "$MARCA" ]; then
		papel_de_parede LumePapel "Papel (LumeBSD)" $T/papel.png
		papel_de_parede LumePapelNoite "Papel noite (LumeBSD)" $T/papel-noite.png
		# E o kwinrc do tema: o da 1.4 liga o compositor, o filtro e-ink e
		# as animações (quem atualizou de antes tinha o compositor
		# desligado). O que cada usuário mudou no próprio kwinrc vale mais.
		instalar $T/kwinrc $XDG/kwinrc 0644
		# O Papel noite (1.5.1): quem atualizou de antes ganha o tema escuro
		# junto, sem mudar o que usa.
		instalar $T/PapelNoite.colors $P/share/color-schemes/PapelNoite.colors
		instalar $T/PapelNoite.colorscheme $P/share/konsole/PapelNoite.colorscheme
		instalar $T/PapelNoite.profile $P/share/konsole/PapelNoite.profile
		instalar $T/metadata-noite.json $LNF_NOITE/metadata.json
		instalar $T/defaults-noite $LNF_NOITE/contents/defaults
		instalar $T/layout-noite.js $LNF_NOITE/contents/layouts/org.kde.plasma.desktop-layout.js
		instalar $T/papel-noite.png $LNF_NOITE/contents/previews/preview.png
		[ -d "$ICONES_NOITE" ] || icones_cinza breeze-dark "$ICONES_NOITE" "Papel noite" >/dev/null
		# A identidade visual (2.0): o farol no lugar dos logos do KDE, o
		# splash, o SDDM e o Sobre o sistema; o menu de quem já usa, também.
		instalar $T/ksplashrc $XDG/ksplashrc 0644
		instalar $T/kcm-about-distrorc $XDG/kcm-about-distrorc 0644
		instalar $T/sddm-breeze.conf.user $P/share/sddm/themes/breeze/theme.conf.user 0644
		instalar $T/layout.js $LNF/contents/layouts/org.kde.plasma.desktop-layout.js
		instalar $T/layout-noite.js $LNF_NOITE/contents/layouts/org.kde.plasma.desktop-layout.js
		marca >/dev/null
		# (sobrescreve o conteúdo, sem recriar o arquivo: ele continua do usuário)
		for rc in /home/*/.config/plasma-org.kde.plasma.desktop-appletsrc; do
			[ -f "$rc" ] && grep -q '^icon=start-here-kde-symbolic$' "$rc" || continue
			tmp=$(mktemp)
			sed -e 's/^icon=start-here-kde-symbolic$/icon=start-here-lume-symbolic/' "$rc" > "$tmp" &&
			    cat "$tmp" > "$rc"
			rm -f "$tmp"
		done
	fi
	# "Versões anteriores" no botão direito do Dolphin (lume tempo).
	instalar $T/lume-tempo.desktop $P/share/kio/servicemenus/lume-tempo.desktop 0755
	# O app de atualizações: no menu, o aviso na entrada da sessão e a
	# permissão (polkit) para instalar com a senha do usuário.
	A=/usr/share/lume/app
	if [ -x /usr/bin/lume-atualizacoes ]; then
		instalar $A/lume-atualizacoes.desktop \
		    $P/share/applications/lume-atualizacoes.desktop 0644
		instalar $A/lume-atualizacoes-bandeja.desktop \
		    $XDG/autostart/lume-atualizacoes-bandeja.desktop 0644
		instalar $A/org.lumebsd.atualizar.policy \
		    $P/share/polkit-1/actions/org.lumebsd.atualizar.policy 0644
	fi
	# A loja de programas.
	if [ -x /usr/bin/lume-programas ]; then
		instalar $A/lume-programas.desktop \
		    $P/share/applications/lume-programas.desktop 0644
		instalar $A/org.lumebsd.programas.policy \
		    $P/share/polkit-1/actions/org.lumebsd.programas.policy 0644
	fi
	# O ícone de rede (cabo e Wi-Fi), no lugar do networkmgr: na entrada da
	# sessão e a permissão para preparar a placa. O networkmgr sai (é Python,
	# e dois ícones de rede na bandeja confundem).
	if [ -x /usr/bin/lume-rede ]; then
		instalar $A/lume-rede.desktop $XDG/autostart/lume-rede.desktop 0644
		instalar $A/org.lumebsd.rede.policy \
		    $P/share/polkit-1/actions/org.lumebsd.rede.policy 0644
		if pkg info -e networkmgr 2>/dev/null; then
			pkg delete -y networkmgr >/dev/null 2>&1 &&
			    logger -t lume "networkmgr removido: o ícone de rede agora é o lume-rede"
		fi
	fi
	# Os atributos dos arquivos (como no BeOS): "Atributos…" no botão
	# direito do Dolphin, a procura no menu e as consultas salvas
	# (.consulta), que abrem com dois cliques.
	if [ -x /usr/bin/lume-atributos ]; then
		instalar $A/lume-atributos.desktop \
		    $P/share/applications/lume-atributos.desktop 0644
		instalar $A/lume-atributos-menu.desktop \
		    $P/share/kio/servicemenus/lume-atributos-menu.desktop 0755
		instalar $A/lume-atributos-pasta.desktop \
		    $P/share/kio/servicemenus/lume-atributos-pasta.desktop 0755
		instalar $A/lume-consulta.xml $P/share/mime/packages/lume-consulta.xml 0644
		command -v update-mime-database >/dev/null 2>&1 &&
		    update-mime-database $P/share/mime >/dev/null 2>&1
		padrao_mime "application/x-lume-consulta" lume-atributos.desktop
	fi
	# A Ajuda do LumeBSD, no menu.
	instalar /usr/share/lume/ajuda/lume-ajuda.desktop \
	    $P/share/applications/lume-ajuda.desktop 0644
	# Dois cliques num .exe: lume-compat abrir (oferece o Wine na primeira vez).
	instalar /usr/share/lume/loja/lume-windows.desktop \
	    $P/share/applications/lume-windows.desktop 0644
	padrao_mime "application/x-ms-dos-executable application/x-msdownload application/vnd.microsoft.portable-executable application/x-msi application/x-ms-installer" \
	    lume-windows.desktop
	# Dois cliques num .dmg: o que usar no lugar do programa do Mac.
	instalar /usr/share/lume/loja/lume-mac.desktop \
	    $P/share/applications/lume-mac.desktop 0644
	padrao_mime "application/x-apple-diskimage" lume-mac.desktop
	efeitos
	atalhos
	# Impressoras: o CUPS vem com o desktop; quem atualizou de antes da 1.3
	# não o tinha ligado (vale a partir do próximo boot).
	if [ -x $P/sbin/cupsd ] && [ -z "$(sysrc -qn cupsd_enable 2>/dev/null)" ]; then
		sysrc -q cupsd_enable=YES >/dev/null 2>&1
	fi
	# Os atalhos dos programas da loja que precisam de um (o JupyterLab).
	[ -x /usr/sbin/lume-loja ] && /usr/sbin/lume-loja atalhos >/dev/null 2>&1
	command -v update-desktop-database >/dev/null 2>&1 &&
	    update-desktop-database -q $P/share/applications 2>/dev/null
	return 0
}

# Os atalhos de teclado do KDE (Alt+Tab, Alt+F4, Ctrl+Meta+T...). O KWin
# registra os dele assim que a sessão começa, antes de o kglobalacceld subir
# pelo autostart; sem o arquivo do D-Bus que o chama na hora (o pacote do
# FreeBSD não traz), o registro falhava e todos os atalhos do KWin ficavam
# sem tecla.
atalhos()
{
	[ -x $P/lib/libexec/kglobalacceld ] || return 0
	s=$P/share/dbus-1/services/org.kde.kglobalaccel.service
	if [ ! -f $s ] || grep -q LumeBSD $s; then
		instalar $T/org.kde.kglobalaccel.service $s 0644
	fi
	# Quem já entrou numa sessão ficou com os atalhos do KWin gravados sem
	# tecla ("none,none"): sai a linha, e na próxima sessão o padrão volta.
	# Uma linha "none,none" de verdade (ação sem atalho padrão) o KDE
	# escreve de novo igual.
	for k in /home/*/.config/kglobalshortcutsrc; do
		[ -f "$k" ] || continue
		grep -q '=none,none,' "$k" || continue
		awk '/^\[/ { g = $0 } !(g == "[kwin]" && /=none,none,/)' "$k" \
		    > "$k.lume" && cat "$k.lume" > "$k"
		rm -f "$k.lume"
	done
}

# Os efeitos do KWin do LumeBSD (/usr/share/lume/kwin): o filtro e-ink
# (lume_tinta, um plugin em C++) e as animações (lume_refresh e
# lume_esmaecer, em JavaScript), para o KWin do X11 e o do Wayland. O
# plugin só vai se foi compilado para a versão do KWin instalada (o KWin
# recusa outro); depois de uma atualização do KWin pelo pkg, fica de fora
# até a próxima versão do LumeBSD.
efeitos()
{
	K=/usr/share/lume/kwin
	[ -d $K ] || return 0
	# A decoração de janelas "Papel" (a aba como a do BeOS, com as abas das
	# janelas empilhadas): só usa a KDecoration3, serve aos dois KWin.
	if [ -f $K/decoracao/org.lumebsd.papel.so ]; then
		instalar $K/decoracao/org.lumebsd.papel.so \
		    $P/lib/qt6/plugins/org.kde.kdecoration3/org.lumebsd.papel.so 0555
	fi
	for v in x11 wayland; do
		case $v in
		x11)	pkg=plasma6-kwin-x11; plug=$P/lib/qt6/plugins/kwin-x11/effects/plugins
			share=$P/share/kwin-x11/effects ;;
		*)	pkg=plasma6-kwin; plug=$P/lib/qt6/plugins/kwin/effects/plugins
			share=$P/share/kwin/effects ;;
		esac
		tem=$(pkg query %v $pkg 2>/dev/null | sed 's/_[0-9]*$//; s/,[0-9]*$//')
		[ -n "$tem" ] || continue
		# O filtro e-ink e as abas de janelas (lume_abas): os dois em C++,
		# presos à versão do KWin.
		for so in lume_tinta lume_abas; do
			if [ -f $K/$v/$so.so ] && [ "$(cat $K/$v/versao 2>/dev/null)" = "$tem" ]; then
				instalar $K/$v/$so.so $plug/$so.so 0555
			else
				rm -f $plug/$so.so
			fi
		done
		for e in $K/efeitos/*/; do
			[ -d "$e" ] || continue
			n=$(basename "$e")
			mkdir -p $share/$n/contents/code
			instalar $e/metadata.json $share/$n/metadata.json 0644
			instalar $e/contents/code/main.js $share/$n/contents/code/main.js 0644
		done
	done
}

# Para o usuário da sessão: o kwinrc dele e o KWin recarrega.
kwin_recarregar()
{
	for q in qdbus6 qdbus; do
		command -v $q >/dev/null 2>&1 &&
		    { $q org.kde.KWin /KWin reconfigure >/dev/null 2>&1; return 0; }
	done
	dbus-send --session --type=method_call --dest=org.kde.KWin /KWin \
	    org.kde.KWin.reconfigure >/dev/null 2>&1
}

kwin_config()
{
	for k in kwriteconfig6 kwriteconfig5; do
		command -v $k >/dev/null 2>&1 && { $k --file kwinrc "$@"; return; }
	done
	echo "lume tema: kwriteconfig6 não encontrado (o desktop está instalado?)" >&2
	exit 1
}

kwin_ler()
{
	for k in kreadconfig6 kreadconfig5; do
		command -v $k >/dev/null 2>&1 && { $k --file kwinrc "$@"; return; }
	done
}

tinta()
{
	case "${1:-}" in
	"")	if [ "$(kwin_ler --group Plugins --key lume_tintaEnabled --default true)" = true ]; then
			echo "  filtro e-ink: ligado, intensidade $(kwin_ler --group Effect-lume_tinta --key Intensidade --default 100)"
		else
			echo "  filtro e-ink: desligado   (lume tema tinta liga)"
		fi
		echo "  papel: grão $(kwin_ler --group Effect-lume_tinta --key Grao --default 40), viscosidade da tinta $(kwin_ler --group Effect-lume_tinta --key Viscosidade --default 35) ms"
		echo "  noite: $(kwin_ler --group Effect-lume_tinta --key Noite --default auto)   (auto segue o tema: no Papel noite, a tinta mais funda e o papel menos claro)"
		echo "  Atalhos: Ctrl+Meta+T liga e desliga; Ctrl+Meta+Y só na janela ativa." ;;
	liga|ligar|on)	kwin_config --group Plugins --key lume_tintaEnabled true ;;
	desliga|desligar|off)	kwin_config --group Plugins --key lume_tintaEnabled false ;;
	grao|grão)
		case "${2:-}" in ""|*[!0-9]*) echo "uso: lume tema tinta grao 0-100" >&2; exit 2 ;; esac
		[ "$2" -le 100 ] || { echo "lume tema: grão de 0 a 100" >&2; exit 2; }
		kwin_config --group Effect-lume_tinta --key Grao "$2" ;;
	noite)
		case "${2:-}" in
		auto|sim|nao)	kwin_config --group Effect-lume_tinta --key Noite "$2" ;;
		*)	echo "uso: lume tema tinta noite auto | sim | nao" >&2; exit 2 ;;
		esac ;;
	viscosidade)
		case "${2:-}" in ""|*[!0-9]*) echo "uso: lume tema tinta viscosidade MS (0 desliga)" >&2; exit 2 ;; esac
		[ "$2" -le 500 ] || { echo "lume tema: viscosidade de 0 a 500 ms" >&2; exit 2; }
		kwin_config --group Effect-lume_tinta --key Viscosidade "$2" ;;
	*[!0-9]*)	echo "uso: lume tema tinta [liga | desliga | 0-100 | grao 0-100 | viscosidade MS | noite auto|sim|nao]" >&2; exit 2 ;;
	*)	[ "$1" -le 100 ] || { echo "lume tema: intensidade de 0 a 100" >&2; exit 2; }
		kwin_config --group Effect-lume_tinta --key Intensidade "$1"
		kwin_config --group Plugins --key lume_tintaEnabled true ;;
	esac
	[ -n "${1:-}" ] && kwin_recarregar
	return 0
}

animacao()
{
	case "${1:-}" in
	"")	a=nenhuma
		[ "$(kwin_ler --group Plugins --key lume_esmaecerEnabled --default false)" = true ] && a=esmaecer
		[ "$(kwin_ler --group Plugins --key lume_refreshEnabled --default true)" = true ] && a=refresh
		echo "  animação das janelas: $a   (lume tema animacao refresh | esmaecer | nenhuma)"
		return ;;
	refresh|eink|e-ink)	r=true e=false ;;
	esmaecer|fade)		r=false e=true ;;
	nenhuma|nada|off)	r=false e=false ;;
	*)	echo "uso: lume tema animacao [refresh | esmaecer | nenhuma]" >&2; exit 2 ;;
	esac
	kwin_config --group Plugins --key lume_refreshEnabled $r
	kwin_config --group Plugins --key lume_esmaecerEnabled $e
	kwin_config --group Plugins --key fadeEnabled false
	kwin_config --group Plugins --key scaleEnabled false
	kwin_recarregar
}

# A identidade visual do LumeBSD no KDE: o farol no lugar dos logos do KDE.
#   - ícones lumebsd e start-here-lume (o botão do menu), no hicolor;
#   - nos temas de ícones Papel e Papel noite, os logos do KDE e do Plasma
#     (start-here-kde, kde, plasma, distributor-logo) viram o farol;
#   - o splash nos dois temas globais (o do Papel noite com as cores dele).
marca()
{
	[ -d $M ] || return 0
	hi=$P/share/icons/hicolor
	instalar $M/lumebsd.svg $hi/scalable/apps/lumebsd.svg 0644
	instalar $M/lumebsd.svg $hi/scalable/apps/start-here-lume.svg 0644
	instalar $M/lumebsd-symbolic.svg $hi/symbolic/apps/lumebsd-symbolic.svg 0644
	instalar $M/lumebsd-symbolic.svg $hi/symbolic/apps/start-here-lume-symbolic.svg 0644
	command -v gtk-update-icon-cache >/dev/null &&
	    gtk-update-icon-cache -q -f "$hi" 2>/dev/null
	for tema in "$ICONES" "$ICONES_NOITE"; do
		[ -d "$tema" ] || continue
		find "$tema" -type f \( -name 'start-here*.svg' -o -name 'kde.svg' \
		    -o -name 'kde-symbolic.svg' -o -name 'plasma.svg' \
		    -o -name 'plasma-symbolic.svg' -o -name 'distributor-logo*.svg' \) |
		while read -r f; do
			# Os pequenos (até 22) e os simbólicos: o farol em silhueta.
			case "$f" in
			*-symbolic.svg|*/16/*|*/22/*|*/24/*)	cp $M/lumebsd-symbolic.svg "$f" ;;
			*)	cp $M/lumebsd.svg "$f" ;;
			esac
		done
		mkdir -p "$tema/apps/scalable" "$tema/apps/symbolic"
		cp $M/lumebsd.svg "$tema/apps/scalable/start-here-lume.svg"
		cp $M/lumebsd-symbolic.svg "$tema/apps/symbolic/start-here-lume-symbolic.svg"
		command -v gtk-update-icon-cache >/dev/null &&
		    gtk-update-icon-cache -q -f "$tema" 2>/dev/null
	done
	instalar $M/Splash.qml $LNF/contents/splash/Splash.qml
	instalar $M/lumebsd-logo.svg $LNF/contents/splash/images/lumebsd-logo.svg
	if [ -d "$LNF_NOITE" ]; then
		mkdir -p $LNF_NOITE/contents/splash/images
		sed -e 's/color: "#efeadf"/color: "#1e1d1b"/' \
		    -e 's/tinta: "#1c1b19"/tinta: "#dedad0"/' \
		    -e 's|images/lumebsd-logo.svg|images/lumebsd-logo-claro.svg|' \
		    $M/Splash.qml > $LNF_NOITE/contents/splash/Splash.qml
		instalar $M/lumebsd-logo-claro.svg $LNF_NOITE/contents/splash/images/lumebsd-logo-claro.svg
	fi
	echo "  marca: o farol do LumeBSD no menu, no splash, no SDDM e em Sobre o sistema"
}

# lume tema escuro | claro: o seu usuário no Papel noite ou no Papel, na
# hora (sem root; dentro da sessão do KDE). O tema global troca cores,
# ícones e papel de parede; o Konsole, o GTK e o filtro e-ink acompanham.
escuro_claro()
{
	if [ "$1" = escuro ]; then
		lnf=org.lumebsd.papel-noite; cores=PapelNoite; perfil=PapelNoite.profile
		fundo=LumePapelNoite; gtk=true; gtktema=Breeze-Dark; icones=lume-papel-noite
		nome="Papel noite"; destaque=120,116,108
	else
		lnf=org.lumebsd.papel; cores=Papel; perfil=Papel.profile
		fundo=LumePapel; gtk=false; gtktema=Breeze; icones=lume-papel
		nome=Papel; destaque=26,26,26
	fi
	[ -d $P/share/plasma/look-and-feel/$lnf ] || {
		echo "  O tema $nome não está instalado: rode, como root, lume tema papel." >&2
		exit 1
	}
	if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ] || ! command -v plasma-apply-lookandfeel >/dev/null 2>&1; then
		echo "  lume tema $1: rode dentro da sessão do KDE (no Konsole, por exemplo)." >&2
		exit 1
	fi
	plasma-apply-lookandfeel -a $lnf >/dev/null 2>&1
	# O tema global nem sempre troca o esquema de cores de quem já mexeu nele.
	command -v plasma-apply-colorscheme >/dev/null 2>&1 &&
	    plasma-apply-colorscheme $cores >/dev/null 2>&1
	command -v plasma-apply-wallpaperimage >/dev/null 2>&1 &&
	    plasma-apply-wallpaperimage $P/share/wallpapers/$fundo/contents/images/3840x2160.png >/dev/null 2>&1
	for k in kwriteconfig6 kwriteconfig5; do
		command -v $k >/dev/null 2>&1 || continue
		$k --file kdeglobals --group Icons --key Theme $icones
		# A cor de destaque pinta o corpo das pastas e as seleções: a
		# tinta no Papel; no escuro, um cinza médio (tinta sumiria no fundo).
		$k --file kdeglobals --group General --key AccentColor $destaque
		$k --file konsolerc --group "Desktop Entry" --key DefaultProfile $perfil
		for g in gtk-3.0 gtk-4.0; do
			mkdir -p "$HOME/.config/$g"
			$k --file "$HOME/.config/$g/settings.ini" --group Settings \
			    --key gtk-application-prefer-dark-theme $gtk
			$k --file "$HOME/.config/$g/settings.ini" --group Settings \
			    --key gtk-theme-name $gtktema
			$k --file "$HOME/.config/$g/settings.ini" --group Settings \
			    --key gtk-icon-theme-name $icones
		done
		break
	done
	# O filtro e-ink troca o papel e a tinta sozinho (Noite=auto, segue o
	# esquema de cores); o reconfigure é para valer já.
	kwin_recarregar
	echo "  ✓ $nome. Programas abertos antes podem precisar ser reabertos."
}

# padrao_mime "tipos" app.desktop: o app padrão desses tipos, para todos (o
# mimeapps.list do sistema), sem mexer no que já estiver lá.
padrao_mime()
{
	f=$XDG/mimeapps.list
	mkdir -p $XDG
	[ -f $f ] || printf '[Default Applications]\n' > $f
	grep -q '^\[Default Applications\]' $f || printf '\n[Default Applications]\n' >> $f
	for t in $1; do
		grep -q "^$t=" $f && continue
		awk -v l="$t=$2" '{ print } /^\[Default Applications\]$/ { print l }' $f > $f.novo &&
		    mv $f.novo $f
	done
	chmod 0644 $f
}

padrao()
{
	for c in $CONFS; do rm -f "$XDG/$c"; done
	rm -f $XDG/gtk-3.0/settings.ini $XDG/gtk-4.0/settings.ini \
	    $P/etc/fonts/conf.d/60-lume-papel.conf $P/etc/sddm.conf.d/lume.conf \
	    $P/share/sddm/themes/breeze/theme.conf.user "$MARCA" "$MARCA.padrao"
	echo "  ✓ Padrões do KDE de volta (o tema Papel continua disponível"
	echo "    em Configurações do Sistema > Aparência)."
}

case "${1:-}" in
"")	if [ -f "$MARCA" ]; then
		echo "  tema do sistema: $(cat "$MARCA.padrao" 2>/dev/null || echo Papel) (desde $(cat "$MARCA"))"
		k=$(command -v kreadconfig6 || command -v kreadconfig5)
		if [ -n "$k" ] && [ "$(id -u)" -ne 0 ]; then
			case "$($k --file kdeglobals --group General --key ColorScheme)" in
			PapelNoite)	echo "  o seu: Papel noite   (lume tema claro para o Papel)" ;;
			Papel|"")	echo "  o seu: Papel   (lume tema escuro para o Papel noite)" ;;
			*)		echo "  o seu: outro esquema de cores" ;;
			esac
		fi
	else echo "  tema: padrão do KDE   (lume tema papel para o visual e-ink)"; fi ;;
papel|aplicar)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root" >&2; exit 1; }
	aplicar "${2:-}" ;;
noite)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root (para você: lume tema escuro)" >&2; exit 1; }
	aplicar noite ;;
escuro|dark)	escuro_claro escuro ;;
claro|light)	escuro_claro claro ;;
padrao|padrão)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root" >&2; exit 1; }
	padrao ;;
integrar)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root" >&2; exit 1; }
	integrar ;;
tinta)	shift; tinta "$@" ;;
animacao|animação|animacoes|animações)	shift; animacao "$@" ;;
*)	echo "uso: lume tema [papel [noite] | escuro | claro | padrao | integrar | tinta | animacao]" >&2; exit 2 ;;
esac

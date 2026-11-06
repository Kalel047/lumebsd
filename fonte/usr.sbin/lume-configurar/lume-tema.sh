#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-tema - o visual "Papel" do LumeBSD para o KDE Plasma (estética e-ink).
#
#   lume tema           mostra qual tema está em uso
#   lume tema papel     aplica o Papel (o lume desktop já faz isso)
#   lume tema padrao    volta ao visual original do KDE
#   lume tema integrar  põe no KDE o que é do sistema: os apps Atualizações
#                       e Programas do LumeBSD (menu, aviso na sessão,
#                       polkit), a Ajuda, os dois cliques num .exe (lume
#                       compat) e o "Versões anteriores" do Dolphin (o tema
#                       chama isto, e o lume_saude também, depois de uma
#                       atualização), e os efeitos do KWin do LumeBSD
#   lume tema tinta [liga | desliga | NÚMERO]
#                       o filtro e-ink (efeito lume_tinta do KWin): a tela em
#                       papel e tinta; fotos, vídeos e tela cheia ficam com
#                       cor. NÚMERO é a intensidade, de 0 a 100
#   lume tema animacao [refresh | esmaecer | nenhuma]
#                       como as janelas abrem e fecham
#
# Os dois últimos valem para o usuário que roda (sem root), na hora.
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
ICONES=$P/share/icons/lume-papel
MARCA=$P/etc/xdg/.lume-tema-papel

# Arquivos que o tema coloca, para poder tirar depois.
CONFS="kdeglobals kwinrc breezerc ksplashrc plasmarc konsolerc kscreenlockerrc"

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
icones_cinza()
{
	origem=$P/share/icons/breeze
	[ -d "$origem" ] || { echo "  (sem ícones Breeze; pulei os ícones)"; return; }
	rm -rf "$ICONES"
	cp -RP "$origem" "$ICONES"
	sed -i '' -e 's/^Name=.*/Name=Papel (LumeBSD)/' \
	    -e 's/^Comment=.*/Comment=Breeze em tons de cinza (LumeBSD)/' \
	    "$ICONES/index.theme"
	find "$ICONES" -type f -name '*.svg' -print0 |
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
	find "$ICONES" -type f -name '*.svg.papel' | while read -r f; do
		mv "$f" "${f%.papel}"
	done
	# Programas que não têm ícone no Breeze (o Firefox traz o próprio, em cores).
	for app in firefox org.mozilla.firefox; do
		for t in 32 48 64; do
			mkdir -p "$ICONES/apps/$t"
			cp "$T/navegador.svg" "$ICONES/apps/$t/$app.svg"
		done
	done
	command -v gtk-update-icon-cache >/dev/null &&
	    gtk-update-icon-cache -q -f "$ICONES" 2>/dev/null
	echo "  ícones: $(find "$ICONES" -type f -name '*.svg' | wc -l | tr -d ' ') em tons de cinza"
}

aplicar()
{
	[ -x $P/bin/plasmashell ] || {
		echo "  O KDE Plasma não está instalado: rode lume desktop." >&2
		exit 1
	}
	echo "  Aplicando o tema Papel..."
	instalar $T/Papel.colors $P/share/color-schemes/Papel.colors
	instalar $T/Papel.colorscheme $P/share/konsole/Papel.colorscheme
	instalar $T/Papel.profile $P/share/konsole/Papel.profile
	instalar $T/metadata.json $LNF/metadata.json
	instalar $T/defaults $LNF/contents/defaults
	instalar $T/layout.js $LNF/contents/layouts/org.kde.plasma.desktop-layout.js
	instalar $T/papel.png $LNF/contents/previews/preview.png
	papel_de_parede LumePapel "Papel (LumeBSD)" $T/papel.png
	papel_de_parede LumePapelNoite "Papel noite (LumeBSD)" $T/papel-noite.png
	for c in $CONFS; do
		instalar $T/$c $XDG/$c 0644
	done
	# Fontes: IBM Plex como padrão para sans, serif e mono.
	instalar $T/fontes.conf $P/etc/fonts/conf.d/60-lume-papel.conf 0644
	# Programas GTK (Firefox e cia.) com o mesmo jeito.
	instalar $T/gtk-settings.ini $XDG/gtk-3.0/settings.ini 0644
	instalar $T/gtk-settings.ini $XDG/gtk-4.0/settings.ini 0644
	integrar
	# Tela de login
	instalar $T/sddm.conf $P/etc/sddm.conf.d/lume.conf 0644
	[ -d $P/share/sddm/themes/breeze ] &&
	    instalar $T/sddm-breeze.conf.user \
	    $P/share/sddm/themes/breeze/theme.conf.user 0644
	icones_cinza
	date > "$MARCA"
	echo "  ✓ Tema Papel aplicado. Vale para quem entrar a partir de agora;"
	echo "    quem já usa o KDE escolhe \"Papel (LumeBSD)\" em"
	echo "    Configurações do Sistema > Aparência > Tema global."
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
	for v in x11 wayland; do
		case $v in
		x11)	pkg=plasma6-kwin-x11; plug=$P/lib/qt6/plugins/kwin-x11/effects/plugins
			share=$P/share/kwin-x11/effects ;;
		*)	pkg=plasma6-kwin; plug=$P/lib/qt6/plugins/kwin/effects/plugins
			share=$P/share/kwin/effects ;;
		esac
		tem=$(pkg query %v $pkg 2>/dev/null | sed 's/_[0-9]*$//; s/,[0-9]*$//')
		[ -n "$tem" ] || continue
		if [ -f $K/$v/lume_tinta.so ] && [ "$(cat $K/$v/versao 2>/dev/null)" = "$tem" ]; then
			instalar $K/$v/lume_tinta.so $plug/lume_tinta.so 0555
		else
			rm -f $plug/lume_tinta.so
		fi
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
		echo "  Atalhos: Ctrl+Meta+T liga e desliga; Ctrl+Meta+Y só na janela ativa." ;;
	liga|ligar|on)	kwin_config --group Plugins --key lume_tintaEnabled true ;;
	desliga|desligar|off)	kwin_config --group Plugins --key lume_tintaEnabled false ;;
	*[!0-9]*)	echo "uso: lume tema tinta [liga | desliga | 0-100]" >&2; exit 2 ;;
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
	    $P/share/sddm/themes/breeze/theme.conf.user "$MARCA"
	echo "  ✓ Padrões do KDE de volta (o tema Papel continua disponível"
	echo "    em Configurações do Sistema > Aparência)."
}

case "${1:-}" in
"")	if [ -f "$MARCA" ]; then echo "  tema: Papel (desde $(cat "$MARCA"))"
	else echo "  tema: padrão do KDE   (lume tema papel para o visual e-ink)"; fi ;;
papel|aplicar)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root" >&2; exit 1; }
	aplicar ;;
padrao|padrão)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root" >&2; exit 1; }
	padrao ;;
integrar)
	[ "$(id -u)" -eq 0 ] || { echo "lume tema: precisa ser root" >&2; exit 1; }
	integrar ;;
tinta)	shift; tinta "$@" ;;
animacao|animação|animacoes|animações)	shift; animacao "$@" ;;
*)	echo "uso: lume tema [papel | padrao | integrar | tinta | animacao]" >&2; exit 2 ;;
esac

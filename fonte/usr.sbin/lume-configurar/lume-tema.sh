#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-tema - o visual "Papel" do LumeBSD para o KDE Plasma (estética e-ink).
#
#   lume tema           mostra qual tema está em uso
#   lume tema papel     aplica o Papel (o lume desktop já faz isso)
#   lume tema padrao    volta ao visual original do KDE
#
# Papel, tinta e cinzas; sem animações, sombras nem transparência; fontes
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
*)	echo "uso: lume tema [papel | padrao]" >&2; exit 2 ;;
esac

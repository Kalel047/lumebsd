#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-loja - os programas do LumeBSD: um catálogo em português, por
# categoria e por kit (Uso geral, Exatas, Sociais aplicadas), com os
# programas que cada um substitui ("parecido com o MATLAB").
#
# Instala pelos pacotes oficiais do FreeBSD; os apps da web viram atalhos no
# menu. O app "Programas do LumeBSD" (lume-programas) é a cara disto; ele
# chama este script com --maquina (linhas "@chave valor").
#

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export PATH

CAT=${LUME_LOJA_CATALOGO:-/usr/share/lume/loja/catalogo}
ATALHOS=/usr/share/lume/loja/atalhos
DESKTOP=/usr/share/lume/desktop.txt
KITS=/var/db/lume/kits
SISTEMA=/usr/local/share/applications
[ "$(id -u)" -eq 0 ] && root=s || root=n

MAQUINA=n
if [ "${1:-}" = --maquina ]; then
	MAQUINA=s
	shift
fi

app()   { [ $MAQUINA = s ] && echo "@$*"; return 0; }
msg()   { [ $MAQUINA = s ] && echo "$*" || echo "  $*"; }
ok()    { [ $MAQUINA = s ] && echo "$*" || printf '  \033[32m✓\033[0m %s\n' "$*"; }
erro()  {
	if [ $MAQUINA = s ]; then echo "@erro $*"
	else printf '  \033[31m✗\033[0m %s\n' "$*" >&2; fi
	exit 1
}
precisa_root() { [ $root = s ] || erro "precisa ser root (su -), ou use o app Programas do LumeBSD"; }

[ -r "$CAT" ] || erro "catálogo não encontrado: $CAT"

# --- o catálogo ------------------------------------------------------------

# Linhas de programa (sem comentários e sem @).
itens() { grep -v '^#' "$CAT" | grep -v '^@' | grep '|'; }

# campo ID N: o campo N (1..9) do programa ID.
campo() { itens | awk -F'|' -v id="$1" -v n="$2" '$1 == id { print $n; exit }'; }

existe() { itens | awk -F'|' -v id="$1" '$1 == id { e = 1 } END { exit !e }'; }

# Os pacotes de verdade do programa (sem +linux e web:).
pacotes_de() { for p in $(campo "$1" 4); do case "$p" in +*|web:*) ;; *) echo "$p" ;; esac; done; }

url_de() { for p in $(campo "$1" 4); do case "$p" in web:*) echo "${p#web:}" ;; esac; done; }

e_web() { [ -n "$(url_de "$1")" ]; }

# Programas de um kit, com os kits que ele inclui.
kit_nome() { grep "^@kit $1|" "$CAT" | cut -d'|' -f2; }
kit_inclui() { grep "^@kit $1|" "$CAT" | cut -d'|' -f4; }
programas_do_kit() {
	ks="$1 $(kit_inclui "$1")"
	itens | awk -F'|' -v ks="$ks" '
	    BEGIN { n = split(ks, k, " ") }
	    { split($5, m, " "); for (i in m) for (j = 1; j <= n; j++)
	        if (m[i] == k[j]) { print $1; next } }'
}

# --- o que já está instalado -----------------------------------------------

INSTALADOS=
carregar_instalados() { INSTALADOS=$(pkg query '%n' 2>/dev/null | tr '\n' ' '); }
pkg_ok() { case " $INSTALADOS " in *" $1 "*) return 0 ;; esac; return 1; }

atalho_web() {
	for d in "$SISTEMA" "$HOME/.local/share/applications"; do
		[ -f "$d/lume-web-$1.desktop" ] && { echo "$d/lume-web-$1.desktop"; return 0; }
	done
	return 1
}

instalado() {
	if e_web "$1"; then
		atalho_web "$1" >/dev/null
		return
	fi
	for p in $(pacotes_de "$1"); do
		pkg_ok "$p" || return 1
	done
	return 0
}

# --- abrir apps da web -----------------------------------------------------

# Abre o site como um app: no Chromium (ou Chrome) em janela própria; sem
# eles, numa janela nova do Firefox.
web() {
	url=$(url_de "$1")
	[ -n "$url" ] || erro "não é um app da web: $1"
	for b in chrome chromium linux-chrome; do
		if command -v $b >/dev/null 2>&1; then
			exec $b --app="$url" --class="lume-web-$1"
		fi
	done
	exec firefox --new-window "$url"
}

criar_atalho_web() {
	id=$1
	if [ $root = s ]; then d=$SISTEMA; else d=$HOME/.local/share/applications; fi
	mkdir -p "$d"
	cat > "$d/lume-web-$id.desktop" <<-EOF
	[Desktop Entry]
	Type=Application
	Name=$(campo "$id" 3)
	Comment=$(campo "$id" 7)
	Exec=/usr/sbin/lume-loja web $id
	Icon=$(campo "$id" 8)
	Categories=Network;WebBrowser;
	Keywords=$(campo "$id" 6 | tr ',' ';');web;
	StartupWMClass=lume-web-$id
	EOF
	chmod 0644 "$d/lume-web-$id.desktop"
}

# Atalhos de menu que o LumeBSD traz para programas sem um (o JupyterLab).
por_atalhos() {
	for id in $(itens | awk -F'|' '$9 != "" && $9 != "-" { print $1 }'); do
		a=$(campo "$id" 9)
		[ -f "$ATALHOS/$a" ] || continue
		if instalado "$id"; then
			install -m 0644 "$ATALHOS/$a" "$SISTEMA/lume-$a"
		fi
	done
	# Atalhos de programas que saíram.
	for f in "$SISTEMA"/lume-*.desktop; do
		[ -f "$f" ] || continue
		a=${f##*/lume-}
		[ -f "$ATALHOS/$a" ] || continue
		usa=n
		for id in $(itens | awk -F'|' -v a="$a" '$9 == a { print $1 }'); do
			instalado "$id" && usa=s
		done
		[ $usa = n ] && rm -f "$f"
	done
	command -v update-desktop-database >/dev/null 2>&1 &&
	    update-desktop-database -q "$SISTEMA" 2>/dev/null
	return 0
}

# --- instalar e remover ----------------------------------------------------

# Roda o pkg e traduz o "[3/40] Fetching ..." / "[3/40] Installing ..." em
# progresso: baixar é a primeira metade, instalar a segunda.
pkg_com_progresso() {
	pkg "$@" 2>&1 | awk -v maq=$MAQUINA '
	    function pct(p) { if (maq == "s") { printf "@progresso %d\n", p; fflush() } }
	    /^\[[0-9]+\/[0-9]+\] / {
		split(substr($1, 2, length($1) - 2), a, "/")
		if ($2 == "Fetching") { pct(a[1] * 50 / a[2]); fase = "Baixando" }
		else if ($2 ~ /^(Installing|Upgrading|Reinstalling|Extracting)/) {
			pct(50 + a[1] * 50 / a[2]); fase = "Instalando" }
		else if ($2 ~ /^Deinstalling/) { pct(a[1] * 100 / a[2]); fase = "Removendo" }
		nome = $3; sub(/:$/, "", nome); sub(/\.\.\.$/, "", nome)
		# O pkg escreve "Installing" e "Extracting" do mesmo pacote.
		if (fase nome == ultimo) next
		ultimo = fase nome
		if (maq == "s") printf "@pacote %s %s\n", fase, nome
		else printf "    %s %s (%s)\n", fase, nome, $1
		fflush(); next
	    }
	    { print; fflush() }'
	# O status do pkg, não o do awk.
	return 0
}

garantir_pkg() {
	export ASSUME_ALWAYS_YES=yes
	if ! pkg -N >/dev/null 2>&1; then
		msg "Preparando o pkg..."
		pkg bootstrap -f -q >/dev/null 2>&1 || erro "não deu para preparar o pkg (tem internet?)"
	fi
}

instalar() {
	[ $# -gt 0 ] || erro "uso: lume loja instalar PROGRAMA..."
	for id in "$@"; do existe "$id" || erro "programa desconhecido: $id (veja: lume loja lista)"; done
	carregar_instalados
	pks='' web_ids='' linux=n
	for id in "$@"; do
		if e_web "$id"; then web_ids="$web_ids $id"; continue; fi
		for p in $(pacotes_de "$id"); do pks="$pks $p"; done
		case " $(campo "$id" 4) " in *" +linux "*) linux=s ;; esac
	done
	[ -n "$pks" ] && precisa_root

	if [ -n "$pks" ]; then
		garantir_pkg
		# Uma foto do sistema antes, para voltar (lume tempo voltar).
		[ -x /usr/sbin/lume-tempo ] && [ -z "${LUME_SEM_FOTO:-}" ] &&
		    /usr/sbin/lume-tempo antes "lume loja instalar $*" >/dev/null 2>&1
		if [ $linux = s ]; then
			app "passo 0 3 Ligando a camada Linux"
			/usr/sbin/lume-compat linux ligar >/dev/null 2>&1 ||
			    erro "não deu para ligar a camada Linux (lume compat linux ligar)"
		fi
		app "passo 1 3 Procurando os programas"
		msg "Atualizando a lista de pacotes..."
		pkg update -q >/dev/null 2>&1 || erro "não deu para baixar a lista de pacotes. Tem internet?"
		app "passo 2 3 Baixando e instalando"
		msg "Instalando:$pks"
		pkg_com_progresso install -y $pks
		carregar_instalados
		falta=
		for p in $pks; do pkg_ok "$p" || falta="$falta $p"; done
		[ -z "$falta" ] || erro "não deu para instalar:$falta"
		app "passo 3 3 Arrumando o menu"
		[ $root = s ] && por_atalhos
	fi
	for id in $web_ids; do
		criar_atalho_web "$id"
	done
	app "progresso 100"
	ok "Instalado: $*"
	app "pronto"
}

remover() {
	[ $# -gt 0 ] || erro "uso: lume loja remover PROGRAMA..."
	for id in "$@"; do existe "$id" || erro "programa desconhecido: $id"; done
	carregar_instalados
	pks=
	for id in "$@"; do
		if e_web "$id"; then
			f=$(atalho_web "$id") && { rm -f "$f" 2>/dev/null || erro "sem permissão para apagar $f"; }
			continue
		fi
		pks="$pks $(pacotes_de "$id")"
	done
	if [ -n "$(echo $pks)" ]; then
		precisa_root
		# Ficam: os do desktop e os de outros programas instalados (o Python
		# do "Python científico" não sai se o "Python para dados" usa).
		fica=$(sed 's/#.*//' "$DESKTOP" 2>/dev/null | tr -s ' \n' ' ')
		for id in $(itens | cut -d'|' -f1); do
			case " $* " in *" $id "*) continue ;; esac
			e_web "$id" && continue
			instalado "$id" && fica="$fica $(pacotes_de "$id" | tr '\n' ' ')"
		done
		sai=
		for p in $pks; do
			case " $fica " in *" $p "*) continue ;; esac
			pkg_ok "$p" && sai="$sai $p"
		done
		if [ -n "$sai" ]; then
			[ -x /usr/sbin/lume-tempo ] &&
			    /usr/sbin/lume-tempo antes "lume loja remover $*" >/dev/null 2>&1
			app "passo 1 1 Removendo"
			# Marca como "automático" e deixa o pkg tirar só o que nada mais
			# usa: nunca leva junto um programa de que outro depende.
			pkg set -y -A 1 $sai >/dev/null 2>&1
			pkg_com_progresso autoremove -y
		fi
		por_atalhos
	fi
	app "progresso 100"
	ok "Removido: $*"
	app "pronto"
}

# --- kits ------------------------------------------------------------------

kit() {
	if [ $# -eq 0 ]; then
		grep '^@kit ' "$CAT" | sed 's/^@kit //' | while IFS='|' read -r id nome desc _; do
			marca=" "
			grep -qx "$id" "$KITS" 2>/dev/null && marca="✓"
			printf '  %s %-8s %s\n' "$marca" "$id" "$nome"
			echo "$desc" | fold -s -w 66 | sed 's/^/             /'
		done
		echo
		echo "  Instalar um kit: lume loja kit exatas"
		return
	fi
	kid=$1
	grep -q "^@kit $kid|" "$CAT" || erro "kit desconhecido: $kid (veja: lume loja kit)"
	msg "Kit $(kit_nome "$kid"):"
	# shellcheck disable=SC2046
	instalar $(programas_do_kit "$kid")
	mkdir -p "${KITS%/*}"
	{ cat "$KITS" 2>/dev/null; echo "$kid"; for k in $(kit_inclui "$kid"); do echo "$k"; done; } |
	    sort -u > "$KITS.novo" && mv "$KITS.novo" "$KITS"
}

# --- consultar -------------------------------------------------------------

lista() {
	carregar_instalados
	grep '^@categoria ' "$CAT" | sed 's/^@categoria //' | while IFS='|' read -r cid cnome _; do
		[ -n "${1:-}" ] && [ "$1" != "$cid" ] && continue
		printf '\n  \033[1m%s\033[0m (%s)\n' "$cnome" "$cid"
		itens | awk -F'|' -v c="$cid" '$2 == c { print $1 }' | while read -r id; do
			marca=" "
			instalado "$id" && marca="✓"
			p=$(campo "$id" 6)
			printf '  %s %-16s %s%s\n' "$marca" "$id" "$(campo "$id" 3)" "${p:+  (parecido: $p)}"
		done
	done
	echo
}

procurar() {
	[ $# -gt 0 ] || erro "uso: lume loja procurar TEXTO"
	carregar_instalados
	achou=n
	for id in $(itens | grep -i -- "$*" | cut -d'|' -f1); do
		achou=s
		marca=" "
		instalado "$id" && marca="✓"
		printf '  %s %-16s %s\n' "$marca" "$id" "$(campo "$id" 3)"
		p=$(campo "$id" 6)
		[ -n "$p" ] && printf '      parecido com: %s\n' "$p"
		campo "$id" 7 | fold -s -w 66 | sed 's/^/      /'
	done
	[ $achou = s ] || msg "Nada encontrado para \"$*\"."
}

info() {
	existe "$1" || erro "programa desconhecido: $1"
	carregar_instalados
	printf '  %s\n' "$(campo "$1" 3)"
	campo "$1" 7 | fold -s -w 70 | sed 's/^/  /'
	p=$(campo "$1" 6)
	[ -n "$p" ] && echo "  Parecido com: $p"
	if e_web "$1"; then
		echo "  Site: $(url_de "$1")"
	else
		echo "  Pacotes: $(pacotes_de "$1" | tr '\n' ' ')"
		t=$(tamanho "$1")
		[ -n "$t" ] && [ "$t" -gt 0 ] && echo "  Ocupa: cerca de $((t / 1048576)) MB a mais no disco"
	fi
	if instalado "$1"; then echo "  ✓ Instalado"; else echo "  Instalar: lume loja instalar $1"; fi
}

# Quanto os programas ocupam a mais no disco, com as dependências que ainda
# não estão instaladas (pelo catálogo do repositório, sem precisar de root).
tamanho() {
	pks=
	for id in "$@"; do pks="$pks $(pacotes_de "$id" | tr '\n' ' ')"; done
	[ -n "$(echo $pks)" ] || { echo 0; return; }
	{ pkg rquery -a 'T %n %sb' 2>/dev/null; pkg rquery -a 'D %n %dn' 2>/dev/null; } |
	    awk -v pedidos="$pks" -v inst="$INSTALADOS" '
	    $1 == "T" { tam[$2] = $3; next }
	    $1 == "D" { deps[$2] = deps[$2] " " $3 }
	    END {
		n = split(inst, a, " "); for (i = 1; i <= n; i++) tem[a[i]] = 1
		n = split(pedidos, f, " ")
		while (n > 0) {
			p = f[n--]
			if (p in visto || p in tem) continue
			visto[p] = 1; total += tam[p]
			m = split(deps[p], d, " ")
			for (i = 1; i <= m; i++) f[++n] = d[i]
		}
		print total + 0
	    }'
}

# Para o app: o que está instalado, os kits e a camada de compatibilidade.
estado() {
	carregar_instalados
	# Num awk só (o app chama a cada mudança).
	itens | awk -F'|' -v inst="$INSTALADOS" -v sis="$SISTEMA" \
	    -v casa="$HOME/.local/share/applications" '
	    BEGIN { n = split(inst, a, " "); for (i = 1; i <= n; i++) tem[a[i]] = 1 }
	    {
		ok = 1; web = 0; m = split($4, p, " ")
		for (i = 1; i <= m; i++) {
			if (p[i] ~ /^web:/) web = 1
			else if (p[i] !~ /^\+/ && !(p[i] in tem)) ok = 0
		}
		if (web) ok = (system("test -f " sis "/lume-web-" $1 ".desktop -o -f \"" \
		    casa "/lume-web-" $1 ".desktop\"") == 0)
		if (ok) print "@instalado " $1
	    }'
	[ -f "$KITS" ] && while read -r k; do echo "@kit $k"; done < "$KITS"
	if pkg -N >/dev/null 2>&1 && pkg rquery '%n' pkg >/dev/null 2>&1; then
		echo "@repo ok"
	else
		echo "@repo vazio"
	fi
	echo "@fim"
}

# O atalho (.desktop) que abre o programa.
abrir() {
	existe "$1" || erro "programa desconhecido: $1"
	e_web "$1" && web "$1"
	a=$(campo "$1" 9)
	[ "$a" = - ] && erro "$(campo "$1" 3) não tem janela: é usado por outros programas ou pelo terminal"
	f=
	if [ -n "$a" ] && [ -f "$SISTEMA/lume-$a" ]; then
		f=$SISTEMA/lume-$a
	else
		for p in $(pacotes_de "$1"); do
			f=$(pkg info -l "$p" 2>/dev/null | awk '/\/share\/applications\/.*\.desktop$/ { print $1; exit }')
			[ -n "$f" ] && break
		done
	fi
	[ -n "$f" ] || erro "$(campo "$1" 3) não tem atalho no menu; use pelo terminal"
	if command -v kioclient >/dev/null 2>&1; then
		exec kioclient exec "$f"
	fi
	cmd=$(awk -F= '/^Exec=/ { sub(/^Exec=/, ""); print; exit }' "$f" | sed 's/ *%[a-zA-Z]//g')
	exec sh -c "$cmd"
}

resumo() {
	echo
	echo "  Programas do LumeBSD: $(itens | wc -l | tr -d ' ') programas, em português, por área."
	echo
	kit
	echo
	echo "  lume loja lista [categoria]   tudo, por categoria"
	echo "  lume loja procurar TEXTO      acha também pelo nome do programa"
	echo "                                que você usava (ex.: matlab, excel)"
	echo "  lume loja instalar PROGRAMA   lume loja remover PROGRAMA"
	echo
	echo "  No menu: Programas do LumeBSD."
	echo
}

case "${1:-}" in
"")		resumo ;;
lista)		shift; lista "$@" ;;
procurar)	shift; procurar "$@" ;;
info)		shift; info "$@" ;;
instalar)	shift; instalar "$@" ;;
remover)	shift; remover "$@" ;;
kit)		shift; kit "$@" ;;
web)		shift; web "$1" ;;
abrir)		shift; abrir "$1" ;;
estado)		estado ;;
tamanho)	shift; carregar_instalados; echo "@tamanho $(tamanho "$@")" ;;
atalhos)	precisa_root; por_atalhos ;;
*)	echo "uso: lume loja [lista | procurar | info | instalar | remover | kit]" >&2
	exit 1 ;;
esac

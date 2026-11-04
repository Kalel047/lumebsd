#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# forja - pacotes do LumeBSD por código-fonte ou binário, no estilo Portage.
#
# Cada pacote vem pronto (binário oficial do FreeBSD) até você mudar uma
# opção dele; aí a forja o compila da árvore de ports com as suas opções e
# o protege das atualizações binárias. As opções são as OPTIONS dos ports,
# o equivalente às USE flags do Gentoo:
#
#   /etc/forja/forja.conf   opções globais e preferências
#   /etc/forja/opcoes       opções por pacote: "editors/nano -NLS +DOCS"
#   /var/db/forja/mundo     o que você pediu explicitamente (o @world)
#
# Veja forja(8).
#

CONF=/etc/forja/forja.conf
OPCOES=/etc/forja/opcoes
DB=/var/db/forja
MUNDO=$DB/mundo
MAKECONF=$DB/make.conf
LOGS=/var/log/forja
PORTS=${PORTSDIR:-/usr/ports}

# Para a árvore de ports e os programas que ela compila, o sistema é o
# FreeBSD por baixo do LumeBSD: o uname diz "LumeBSD 1.2-FAROL", que o
# configure dos programas não conhece. A versão vem do kern.osreldate
# (1501000 -> 15.1-RELEASE); o uname(1) respeita UNAME_s e UNAME_r, e os
# ports repassam essas variáveis para cada compilação.
if [ -z "${_OSRELEASE:-}" ]; then
	_rel=$(sysctl -n kern.osreldate)
	_OSRELEASE="$((_rel / 100000)).$((_rel / 1000 % 100))-RELEASE"
fi
OPSYS=FreeBSD UNAME_s=FreeBSD UNAME_r=$_OSRELEASE
export _OSRELEASE OPSYS UNAME_s UNAME_r

# Padrões; o forja.conf pode mudar qualquer um.
opcoes_globais=""
ramo=auto
jobs=auto
snapshot=sim
sempre_fonte=nao
arvore_url=
[ -f "$CONF" ] && . "$CONF"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	VERDE='\033[32;1m'; AMARELO='\033[33;1m'; VERMELHO='\033[31;1m'
	AZUL='\033[34;1m'; FRACO='\033[2m'; NEGRITO='\033[1m'; FIM='\033[0m'
else
	VERDE=; AMARELO=; VERMELHO=; AZUL=; FRACO=; NEGRITO=; FIM=
fi

msg()	{ printf "  %b\n" "$*"; }
ok()	{ printf "  ${VERDE}✓${FIM} %b\n" "$*"; }
aviso()	{ printf "  ${AMARELO}!${FIM} %b\n" "$*"; }
erro()	{ printf "  ${VERMELHO}✗${FIM} %b\n" "$*" >&2; exit 1; }

raiz()
{
	[ "$(id -u)" -eq 0 ] || erro "a forja precisa ser rodada como root (su -)"
	mkdir -p "$DB" "$LOGS" "$(dirname "$OPCOES")"
	[ -f "$MUNDO" ] || : > "$MUNDO"
	[ -f "$OPCOES" ] || cat > "$OPCOES" <<-'EOF'
	# Opções por pacote (como o package.use do Gentoo).
	# Uma linha por pacote: origem do port, depois +OPÇÃO ou -OPÇÃO.
	# Exemplo:
	#   editors/nano    -NLS +DOCS
	# Veja as opções de um pacote com: forja opcoes nano
	EOF
}

# --- árvore de ports ---------------------------------------------------------

ramo_trimestral()
{
	if [ "$ramo" != auto ]; then
		echo "$ramo"
		return
	fi
	# O mesmo trimestre dos pacotes binários (repositório "quarterly").
	ano=$(date +%Y)
	mes=$(date +%m)
	echo "${ano}Q$(( (${mes#0} - 1) / 3 + 1 ))"
}

tem_arvore()
{
	[ -f "$PORTS/Mk/bsd.port.mk" ]
}

baixar_tarball()
{
	url=${FORJA_ARVORE_URL:-${arvore_url:-https://codeload.github.com/freebsd/freebsd-ports/tar.gz/refs/heads/$1}}
	msg "Baixando a árvore de ports ($1)..."
	if ! tem_arvore; then
		mkdir -p "$PORTS"
		fetch -q -o - "$url" | tar -xzf - -C "$PORTS" --strip-components 1 &&
		    [ -f "$PORTS/Mk/bsd.port.mk" ] ||
		    erro "não deu para baixar a árvore ($url)"
		return 0
	fi
	rm -rf "$PORTS.nova" && mkdir -p "$PORTS.nova" &&
	    fetch -q -o - "$url" | tar -xzf - -C "$PORTS.nova" --strip-components 1 &&
	    [ -f "$PORTS.nova/Mk/bsd.port.mk" ] || {
		rm -rf "$PORTS.nova"
		erro "não deu para baixar a árvore ($url)"
	}
	# Troca o conteúdo (o $PORTS pode ser um dataset do ZFS).
	mkdir -p "$PORTS"
	find "$PORTS" -mindepth 1 -maxdepth 1 ! -name distfiles ! -name packages \
	    -exec rm -rf {} +
	(cd "$PORTS.nova" && tar -cf - .) | tar -xf - -C "$PORTS"
	rm -rf "$PORTS.nova"
}

cmd_arvore()
{
	raiz
	r=$(ramo_trimestral)
	# Com FORJA_ARVORE_URL (um espelho) vai direto no tarball; senão tenta
	# o git e, se ele falhar, o tarball do GitHub.
	if [ -n "${FORJA_ARVORE_URL:-}${arvore_url:-}" ] || ! command -v git >/dev/null; then
		baixar_tarball "$r"
	elif [ -d "$PORTS/.git" ]; then
		msg "Atualizando a árvore de ports ($r) com git..."
		{ git -C "$PORTS" fetch -q --depth 1 origin "$r" &&
		    git -C "$PORTS" checkout -q -B "$r" FETCH_HEAD; } ||
		    erro "não deu para atualizar a árvore com git"
	else
		msg "Baixando a árvore de ports ($r) com git..."
		rm -rf "$PORTS.git"
		if git clone -q --depth 1 -b "$r" https://git.FreeBSD.org/ports.git \
		    "$PORTS.git"; then
			mkdir -p "$PORTS"
			(cd "$PORTS.git" && tar -cf - .) | tar -xf - -C "$PORTS"
			rm -rf "$PORTS.git"
		else
			rm -rf "$PORTS.git"
			aviso "o git falhou; tentando o tarball"
			baixar_tarball "$r"
		fi
	fi
	ok "árvore de ports pronta em $PORTS ($(find "$PORTS" -mindepth 3 -maxdepth 3 -name Makefile | wc -l | tr -d ' ') ports)"
}

# --- nomes, origens e opções -----------------------------------------------

# origem nome|categoria/port -> categoria/port
origem()
{
	case "$1" in
	*/*)	echo "$1"; return 0 ;;
	esac
	o=$(pkg query '%o' "$1" 2>/dev/null)
	[ -n "$o" ] || o=$(pkg rquery -U '%o' "$1" 2>/dev/null | head -1)
	if [ -z "$o" ] && tem_arvore; then
		o=$(cd "$PORTS" && ls -d */"$1" 2>/dev/null | head -1)
	fi
	[ -n "$o" ] && echo "$o"
}

# opcoes_de categoria/port -> "+A -B" configuradas para ele
opcoes_de()
{
	[ -f "$OPCOES" ] || return 0
	awk -v o="$1" '!/^#/ && $1 == o { $1 = ""; sub(/^ +/, ""); print }' "$OPCOES"
}

normalizar()
{
	echo "$*" | tr ' ' '\n' | grep -v '^$' | sort -u | tr '\n' ' ' | sed 's/ $//'
}

# Precisa compilar? (tem opções próprias, ou o usuário pediu tudo da fonte)
compilar()
{
	[ "$sempre_fonte" = sim ] && return 0
	[ -n "$(opcoes_de "$1")" ]
}

gerar_makeconf()
{
	{
		echo "# Gerado pela forja a partir de $CONF e $OPCOES."
		echo '.if exists(/etc/make.conf)'
		echo '.include "/etc/make.conf"'
		echo '.endif'
		echo "WRKDIRPREFIX=	/var/tmp/forja"
		[ "$jobs" != auto ] && echo "MAKE_JOBS_NUMBER=	$jobs"
		for o in $opcoes_globais; do
			case "$o" in
			+*) echo "OPTIONS_SET+=	${o#+}" ;;
			-*) echo "OPTIONS_UNSET+=	${o#-}" ;;
			esac
		done
		grep -v '^#' "$OPCOES" 2>/dev/null | while read -r orig resto; do
			[ -n "$orig" ] || continue
			nome=$(make -C "$PORTS/$orig" -V OPTIONS_NAME 2>/dev/null)
			[ -n "$nome" ] || continue
			for o in $resto; do
				case "$o" in
				+*) echo "${nome}_SET+=	${o#+}" ;;
				-*) echo "${nome}_UNSET+=	${o#-}" ;;
				esac
			done
		done
	} > "$MAKECONF"
}

# portmake categoria/port [argumentos do make...]
portmake()
{
	local o
	o=$1
	shift
	make -C "$PORTS/$o" __MAKE_CONF="$MAKECONF" BATCH=yes "$@"
}

instalado()
{
	pkg info -e "$1" 2>/dev/null
}

# nome do pacote instalado a partir de uma origem
pacote_da_origem()
{
	pkg query -e "%o == $1" '%n' 2>/dev/null | head -1
}

# Opções com que a forja compilou o pacote (guardadas como anotação).
anotacao()
{
	pkg query '%At\t%Av' "$1" 2>/dev/null |
	    awk -F'\t' '$1 == "forja_opcoes" { print $2 }'
}

anotar()
{
	pkg annotate -q -y -D "$1" forja_opcoes >/dev/null 2>&1
	pkg annotate -q -y -A "$1" forja_opcoes "$2" >/dev/null 2>&1
}

compilado()
{
	[ -n "$(pkg query '%At' "$1" 2>/dev/null | grep -x forja_opcoes)" ]
}

no_mundo()
{
	grep -qx "$1" "$MUNDO" 2>/dev/null
}

por_no_mundo()
{
	no_mundo "$1" || echo "$1" >> "$MUNDO"
}

# --- compilar e instalar ---------------------------------------------------

# construir categoria/port: compila e instala da árvore com as opções.
construir()
{
	# Recursiva (dependências com opções): nada de variáveis globais aqui.
	local orig nome versao escolhidas ativas log inicio dep dnome faltam
	orig=$1
	tem_arvore || erro "falta a árvore de ports: forja arvore"
	[ -f "$PORTS/$orig/Makefile" ] || erro "port não encontrado: $orig"
	gerar_makeconf
	nome=$(portmake "$orig" -V PKGBASE)
	versao=$(portmake "$orig" -V PKGVERSION)
	escolhidas=$(normalizar "$opcoes_globais $(opcoes_de "$orig")")
	ativas=$(portmake "$orig" -V SELECTED_OPTIONS | tr ' ' '\n' | sort |
	    tr '\n' ' ' | sed 's/^ *//; s/ *$//')
	log="$LOGS/$nome.log"
	msg "${AZUL}compilando${FIM} $orig ($versao)"
	msg "${FRACO}  opções: ${ativas:-nenhuma}${FIM}"

	# Dependências com opções próprias também são compiladas; o resto vem
	# em binário, que é muito mais rápido.
	for dep in $(portmake "$orig" all-depends-list 2>/dev/null |
	    sed "s|^$PORTS/||"); do
		if compilar "$dep" && [ "$sempre_fonte" != sim ]; then
			dnome=$(portmake "$dep" -V PKGBASE)
			instalado "$dnome" || construir "$dep"
		fi
	done
	# Só as dependências diretas; o pkg traz as de cada binário. (O
	# install-missing-packages dos ports desce nas dependências de
	# compilação das dependências, que um binário não precisa.)
	msg "${FRACO}  dependências em binário...${FIM}"
	faltam=
	for dep in $(portmake "$orig" build-depends-list run-depends-list \
	    2>/dev/null | sed "s|^$PORTS/||" | sort -u); do
		# (pkg info -O devolve 0 mesmo sem o pacote; pkg query não.)
		[ -n "$(pkg query -e "%o == $dep" '%n' 2>/dev/null)" ] ||
		    faltam="$faltam $dep"
	done
	: > "$log"
	if [ -n "$faltam" ]; then
		ASSUME_ALWAYS_YES=yes pkg install -q $faltam >> "$log" 2>&1 ||
		    { tail -20 "$log"; erro "falhou ao instalar as dependências:$faltam (log: $log)"; }
	fi

	inicio=$(date +%s)
	instalado "$nome" && pkg unlock -q -y "$nome" >/dev/null 2>&1
	if ! portmake "$orig" reinstall clean >> "$log" 2>&1; then
		tail -25 "$log" | sed 's/^/    /'
		erro "a compilação de $orig falhou (log completo: $log)"
	fi
	# Protege das atualizações binárias e guarda com que opções foi feito.
	anotar "$nome" "${escolhidas:-padrão}"
	pkg lock -q -y "$nome" >/dev/null 2>&1
	ok "$nome-$versao compilado em $(( $(date +%s) - inicio )) s"
}

instalar_um()
{
	alvo=$1
	orig=$(origem "$alvo") || orig=
	if [ -n "$orig" ] && compilar "$orig"; then
		construir "$orig"
	else
		msg "${AZUL}binário${FIM} $alvo"
		ASSUME_ALWAYS_YES=yes pkg install -q "$alvo" ||
		    erro "não deu para instalar $alvo"
		ok "$alvo instalado (pacote oficial)"
	fi
	por_no_mundo "${orig:-$alvo}"
}

# --- comandos ------------------------------------------------------------------

# Foto do disco antes de mexer nos pacotes (lume tempo): se der problema,
# "lume tempo voltar" devolve o sistema como estava.
foto_antes()
{
	[ "$snapshot" = sim ] && [ -x /usr/sbin/lume-tempo ] || return 0
	/usr/sbin/lume-tempo antes "forja $*"
}

cmd_instalar()
{
	raiz
	[ $# -gt 0 ] || erro "diga o que instalar: forja instalar nano"
	foto_antes instalar "$@"
	for a in "$@"; do
		instalar_um "$a"
	done
}

cmd_remover()
{
	raiz
	[ $# -gt 0 ] || erro "diga o que remover"
	foto_antes remover "$@"
	for a in "$@"; do
		orig=$(origem "$a") || orig=$a
		nome=$(pacote_da_origem "$orig")
		nome=${nome:-$a}
		pkg unlock -q -y "$nome" >/dev/null 2>&1
		ASSUME_ALWAYS_YES=yes pkg delete -q "$nome" || erro "não deu para remover $nome"
		grep -vx "$orig" "$MUNDO" > "$MUNDO.tmp"; mv "$MUNDO.tmp" "$MUNDO"
		ok "$nome removido"
	done
	msg "Dependências que sobraram (como o depclean):"
	ASSUME_ALWAYS_YES=yes pkg autoremove -q
}

cmd_opcoes()
{
	alvo=$1
	[ -n "$alvo" ] || erro "uso: forja opcoes <pacote> [+OPÇÃO -OPÇÃO ...]"
	shift
	tem_arvore || erro "falta a árvore de ports: forja arvore"
	orig=$(origem "$alvo") || erro "não achei o port de $alvo"
	if [ $# -gt 0 ]; then
		raiz
		todas=$(make -C "$PORTS/$orig" -V COMPLETE_OPTIONS_LIST)
		for o in "$@"; do
			case "$o" in
			+*|-*) ;;
			*) erro "use +OPÇÃO para ligar e -OPÇÃO para desligar: $o" ;;
			esac
			echo " $todas " | grep -q " ${o#?} " ||
			    erro "$orig não tem a opção ${o#?} (tem: $todas)"
		done
		atuais=$(opcoes_de "$orig")
		# A nova escolha de cada opção substitui a anterior.
		novas=$atuais
		for o in "$@"; do
			novas=$(echo "$novas" | tr ' ' '\n' | grep -v "^[+-]${o#?}\$" |
			    tr '\n' ' ')
			novas="$novas $o"
		done
		novas=$(normalizar "$novas")
		grep -v "^${orig}[[:space:]]" "$OPCOES" > "$OPCOES.tmp" || true
		[ -n "$novas" ] && printf '%-24s %s\n' "$orig" "$novas" >> "$OPCOES.tmp"
		mv "$OPCOES.tmp" "$OPCOES"
		ok "$orig: $novas"
		msg "Aplique com: forja instalar $alvo   (ou forja atualizar)"
		return
	fi
	gerar_makeconf 2>/dev/null || true
	printf "\n  ${NEGRITO}%s${FIM}  %s\n" "$orig" \
	    "$(make -C "$PORTS/$orig" -V COMMENT)"
	minhas=" $(opcoes_de "$orig") "
	ligadas=" $(make -C "$PORTS/$orig" __MAKE_CONF="$MAKECONF" -V SELECTED_OPTIONS) "
	for o in $(make -C "$PORTS/$orig" -V COMPLETE_OPTIONS_LIST); do
		desc=$(make -C "$PORTS/$orig" -V "${o}_DESC")
		case "$ligadas" in *" $o "*) m="${VERDE}+${FIM}" ;; *) m="${FRACO}-${FIM}" ;; esac
		case "$minhas" in *" +$o "*|*" -$o "*) eu=" ${AMARELO}(sua escolha)${FIM}" ;; *) eu= ;; esac
		printf "    %b %-14s %s%b\n" "$m" "$o" "$desc" "$eu"
	done
	echo
	msg "Mude com: forja opcoes $alvo +OPÇÃO -OPÇÃO"
}

cmd_mundo()
{
	[ -s "$MUNDO" ] || { msg "O mundo está vazio. Instale algo: forja instalar nano"; return; }
	printf "\n  ${NEGRITO}%-26s %-20s %-8s %s${FIM}\n" PACOTE VERSÃO TIPO OPÇÕES
	while read -r o; do
		nome=$(pacote_da_origem "$o")
		[ -n "$nome" ] || nome=$(pkg query '%n' "$o" 2>/dev/null)
		if [ -z "$nome" ]; then
			printf "  %-26s ${VERMELHO}%-20s${FIM}\n" "$o" "não instalado"
			continue
		fi
		v=$(pkg query '%v' "$nome")
		feitas=$(anotacao "$nome")
		if compilado "$nome"; then
			tipo=fonte
		else
			tipo=binário
		fi
		printf "  %-26s %-20s %-8s %s\n" "$o" "$v" "$tipo" "$feitas"
	done < "$MUNDO"
	echo
}

cmd_atualizar()
{
	raiz
	if [ "$snapshot" = sim ] && [ -x /usr/sbin/lume-tempo ]; then
		foto_antes atualizar
	elif [ "$snapshot" = sim ] && command -v bectl >/dev/null &&
	    bectl list >/dev/null 2>&1; then
		be="forja-$(date +%Y%m%d-%H%M)"
		bectl create "$be" &&
		    ok "ponto de volta criado: $be (escolha no menu de boot se precisar)"
	fi
	if tem_arvore; then
		( cmd_arvore ) ||
		    aviso "não deu para atualizar a árvore; sigo com a que já existe"
	fi
	msg "Atualizando os pacotes binários..."
	ASSUME_ALWAYS_YES=yes pkg upgrade -q || aviso "pkg upgrade teve problemas"
	tem_arvore || { ok "pronto (sem árvore de ports, só binários)"; return; }
	gerar_makeconf
	# Compilados: refaz se a versão do port mudou ou se as opções mudaram
	# (o "--newuse" do Gentoo). Pacotes que ganharam opções no arquivo
	# também passam a ser compilados.
	refazer=
	for nome in $(pkg query '%n'); do
		orig=$(pkg query '%o' "$nome")
		compilar "$orig" || compilado "$nome" || continue
		[ -f "$PORTS/$orig/Makefile" ] || continue
		quero=$(normalizar "$opcoes_globais $(opcoes_de "$orig")")
		tenho=$(anotacao "$nome")
		[ "$tenho" = padrão ] && tenho=
		nova=$(portmake "$orig" -V PKGVERSION)
		velha=$(pkg query '%v' "$nome")
		if [ "$nova" != "$velha" ]; then
			msg "  $nome: $velha -> $nova"
			refazer="$refazer $orig"
		elif [ "$quero" != "$tenho" ]; then
			msg "  $nome: opções mudaram (${tenho:-nenhuma} -> $quero)"
			refazer="$refazer $orig"
		fi
	done
	for orig in $refazer; do
		construir "$orig"
	done
	[ -n "$refazer" ] || ok "os pacotes compilados estão em dia"
}

cmd_buscar()
{
	[ -n "$1" ] || erro "uso: forja buscar <termo>"
	pkg search -q "$1" 2>/dev/null | head -30 | sed 's/^/    /'
}

cmd_info()
{
	orig=$(origem "$1") || erro "não achei $1"
	nome=$(pacote_da_origem "$orig")
	printf "\n  ${NEGRITO}%s${FIM}\n" "$orig"
	if [ -n "$nome" ]; then
		msg "instalado: $nome-$(pkg query '%v' "$nome")"
		feitas=$(anotacao "$nome")
		if compilado "$nome"; then
			msg "tipo: compilado pela forja (opções: ${feitas:-padrão})"
		else
			msg "tipo: pacote binário oficial"
		fi
	else
		msg "não instalado"
	fi
	no_mundo "$orig" && msg "no mundo: sim" || msg "no mundo: não (veio como dependência)"
	[ -n "$(opcoes_de "$orig")" ] && msg "suas opções: $(opcoes_de "$orig")"
	tem_arvore && [ -f "$PORTS/$orig/Makefile" ] &&
	    msg "versão na árvore: $(make -C "$PORTS/$orig" -V PKGVERSION)"
	echo
}

uso()
{
	cat >&2 <<-EOF
	uso: forja instalar <pacote...>     binário, ou compila se tiver opções
	     forja remover <pacote...>      remove e limpa dependências órfãs
	     forja opcoes <pacote>          mostra as opções (as "USE flags")
	     forja opcoes <pacote> +A -B    liga A e desliga B para o pacote
	     forja atualizar                árvore, binários e recompilações
	     forja mundo                    o que você instalou de propósito
	     forja info <pacote>            detalhes de um pacote
	     forja buscar <termo>           procura pacotes
	     forja arvore                   baixa ou atualiza a árvore de ports
	EOF
	exit 2
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
instalar|install)	cmd_instalar "$@" ;;
remover|remove)		cmd_remover "$@" ;;
opcoes|opções|use)	cmd_opcoes "$@" ;;
atualizar|update)	cmd_atualizar ;;
mundo|world)		cmd_mundo ;;
info)			cmd_info "$@" ;;
buscar|search)		cmd_buscar "$@" ;;
arvore|árvore|sync)	cmd_arvore ;;
*)			uso ;;
esac

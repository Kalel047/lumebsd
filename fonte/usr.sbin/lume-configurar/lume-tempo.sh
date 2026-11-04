#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-tempo - a máquina do tempo do LumeBSD: fotos automáticas do disco
# (snapshots do ZFS) e o caminho de volta para qualquer uma delas.
#
#   lume tempo                    resumo e as últimas fotos
#   lume tempo linha [N]          a linha do tempo (as N fotos mais novas)
#   lume tempo versoes ARQUIVO    as versões diferentes de um arquivo ou pasta
#   lume tempo recuperar ARQUIVO [QUANDO] [--substituir]
#                                 traz de volta (por padrão, ao lado do atual)
#   lume tempo foto [DESCRIÇÃO]   tira uma foto agora (root)
#   lume tempo mudou [QUANDO]     o que mudou no disco desde a foto (root)
#   lume tempo voltar QUANDO      o sistema inteiro como estava (root; as
#                                 pastas pessoais não mudam)
#   lume tempo apagar QUANDO      apaga uma foto (root)
#   lume tempo ligar|desligar [PASTA]
#                                 escolhe o que entra nas fotos (root)
#   lume tempo automatico         o que o cron roda a cada 15 minutos
#   lume tempo antes MOTIVO       foto antes de uma mudança (forja, atualizar)
#   lume tempo gui ARQUIVO        a janela "Versões anteriores" do Dolphin
#
# QUANDO: o número na linha do tempo (1 é a mais nova), o nome da foto,
# "hoje", "ontem", uma data (2026-11-03), data e hora (2026-11-03 14:00) ou
# só a hora de hoje (14:00). Vale a última foto até aquele momento.
#
# As fotos são instantâneas e só ocupam o que mudou depois delas. O que entra:
# os sistemas de arquivos com a propriedade lume:tempo=on (no padrão, as
# pastas pessoais e o sistema em uso). Quanto tempo cada uma fica:
#   automáticas  todas das últimas 24 horas, uma por dia por 30 dias e uma
#                por semana por 26 semanas
#   antes de     (forja, lume atualizar, recuperar) 30 dias
#   manuais      até você apagar
# Com o disco acima de 85% cheio, as automáticas mais velhas saem primeiro.
#
# Configurações (/etc/rc.conf): lume_tempo_enable (YES), lume_tempo_horas
# (24), lume_tempo_dias (30), lume_tempo_semanas (26), lume_tempo_antes_dias
# (30), lume_tempo_limite (85, em % do disco).
#

# Chamado também pelo Dolphin e pelo cron, com PATH curto.
PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export PATH

PREFIXO=lume-
DB=/var/db/lume
ESTADO=$DB/tempo-estado
LOG=/var/log/lume-tempo.log
MENU_DOLPHIN=/usr/share/lume/tema/lume-tempo.desktop
MENU_DESTINO=/usr/local/share/kio/servicemenus/lume-tempo.desktop

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	VERDE='\033[32;1m'; AMARELO='\033[33;1m'; VERMELHO='\033[31;1m'
	FRACO='\033[2m'; NEGRITO='\033[1m'; FIM='\033[0m'
else
	VERDE=; AMARELO=; VERMELHO=; FRACO=; NEGRITO=; FIM=
fi
msg()	{ printf "  %b\n" "$*"; }
ok()	{ printf "  ${VERDE}✓${FIM} %b\n" "$*"; }
aviso()	{ printf "  ${AMARELO}!${FIM} %b\n" "$*"; }
erro()	{ printf "  ${VERMELHO}✗${FIM} %b\n" "$*" >&2; exit 1; }
# (Quem não é root também recupera arquivos; o registro fica só para o root.)
registrar() { [ -w "$LOG" ] || [ "$(id -u)" -eq 0 ] || return 0
	echo "$(date '+%F %T') $*" >> "$LOG"; }

raiz()	{ [ "$(id -u)" -eq 0 ] || erro "precisa ser root (su - ou doas)"; }

conf()
{
	v=$(sysrc -n "lume_tempo_$1" 2>/dev/null)
	echo "$v" | grep -Eqx '[0-9]+' && echo "$v" || echo "$2"
}

# --- o que entra nas fotos --------------------------------------------------

# O pool do sistema em uso (vazio se a raiz não for ZFS: pendrive, UFS).
pool()
{
	ds=$(zfs list -H -o name / 2>/dev/null) || return 1
	echo "${ds%%/*}"
}

tem_zfs()
{
	[ ! -f /etc/lume-ao-vivo ] && command -v zfs >/dev/null && [ -n "$(pool)" ]
}

exige_zfs()
{
	[ -f /etc/lume-ao-vivo ] &&
	    erro "no pendrive não há máquina do tempo: ela vem com o sistema instalado (lume instalar)"
	tem_zfs || erro "a máquina do tempo precisa do sistema instalado com ZFS"
}

# Na primeira vez, liga as fotos nas pastas pessoais e no sistema (quem
# atualizou da 1.1 não tem a propriedade).
padrao_ligado()
{
	p=$(pool)
	if ! zfs get -H -r -s local -o value lume:tempo "$p" 2>/dev/null |
	    grep -q .; then
		zfs list -H "$p/home" >/dev/null 2>&1 &&
		    zfs set lume:tempo=on "$p/home"
		zfs list -H "$p/ROOT" >/dev/null 2>&1 &&
		    zfs set lume:tempo=on "$p/ROOT"
		registrar "ligado no padrão: $p/home e $p/ROOT"
	fi
}

# Os sistemas de arquivos que entram nas fotos: lume:tempo=on e montados.
# Dos ambientes de boot, só o que está em uso.
alvos()
{
	raiz_ds=$(zfs list -H -o name /)
	p=$(pool)
	# Sem a propriedade em lugar nenhum (atualizado da 1.1, antes da
	# primeira execução do cron): o padrão, pastas pessoais e sistema.
	if ! zfs get -H -r -s local -o value lume:tempo "$p" 2>/dev/null | grep -q .; then
		for nome in "$p/home" "$raiz_ds"; do
			[ "$(zfs get -H -o value mounted "$nome" 2>/dev/null)" = yes ] &&
			    echo "$nome"
		done
		return
	fi
	zfs get -H -r -t filesystem -o name,value lume:tempo "$p" |
	    while read -r nome valor; do
		[ "$valor" = on ] || continue
		case "$nome" in
		*/ROOT/*) [ "$nome" = "$raiz_ds" ] || continue ;;
		*/ROOT) continue ;;
		esac
		[ "$(zfs get -H -o value mounted "$nome")" = yes ] && echo "$nome"
	done
}

# Nome amigável de um sistema de arquivos: a pasta, ou "sistema".
area()
{
	case "$1" in
	*/ROOT/*) echo sistema ;;
	*) zfs get -H -o value mountpoint "$1" ;;
	esac
}

# --- as fotos -----------------------------------------------------------------

# Todas as fotos do LumeBSD, uma linha por foto e sistema de arquivos:
# conjunto@nome criação(segundos) usada tipo descrição
fotos()
{
	zfs list -H -p -t snapshot -o name,creation,used,lume:tipo,lume:descricao \
	    -s creation -r "$(pool)" 2>/dev/null |
	    awk -F'\t' -v p="@$PREFIXO" 'index($1, p)'
}

# A linha do tempo: uma linha por foto (as mais novas primeiro).
# número nome criação usada(soma) tipo áreas descrição
linha_do_tempo()
{
	# Fotos do sistema de outros ambientes de boot (de antes de uma
	# atualização) também contam: "lume tempo voltar" sabe usá-las.
	fotos | awk -F'\t' '
	{
		split($1, a, "@"); ds = a[1]; n = a[2]
		if (!(n in cria)) { ordem[++k] = n; cria[n] = $2 }
		usada[n] += $3; tipo[n] = $4; desc[n] = $5
		if (ds ~ /\/ROOT\//) {
			ar = "sistema"
		} else {
			ar = ds; sub(/^[^\/]*/, "", ar); if (ar == "") ar = "/"
		}
		areas[n] = areas[n] (areas[n] == "" ? "" : ",") ar
	}
	END {
		for (i = k; i >= 1; i--) {
			n = ordem[i]
			printf "%d\t%s\t%s\t%s\t%s\t%s\t%s\n", k - i + 1, n,
			    cria[n], usada[n], tipo[n], areas[n], desc[n]
		}
	}'
}

# pad TEXTO LARGURA: completa com espaços contando caracteres, não bytes
# (o printf conta "automática" como 11).
pad()
{
	n=$(printf %s "$1" | wc -m | tr -d ' ')
	printf '%s' "$1"
	while [ "$n" -lt "$2" ]; do printf ' '; n=$((n + 1)); done
}

# O espaço que só as fotos ocupam: o que sairia do disco apagando todas
# (zfs destroy -n, sem apagar). Para quem não é root, a soma do "used".
ocupado()
{
	fotos > "$TMP/todas"
	total=0
	for ds in $(cut -f1 "$TMP/todas" | sed 's/@.*//' | sort -u); do
		lista=$(awk -F'\t' -v d="$ds" '{ split($1, a, "@") } a[1] == d { printf "%s%s", s, a[2]; s = "," }' "$TMP/todas")
		b=$(zfs destroy -nvp "$ds@$lista" 2>/dev/null | awk -F'\t' '$1 == "reclaim" { print $2 }')
		[ -n "$b" ] || b=$(awk -F'\t' -v d="$ds" '{ split($1, a, "@") } a[1] == d { s += $3 } END { print s + 0 }' "$TMP/todas")
		total=$((total + b))
	done
	echo "$total"
}

humano()
{
	awk -v b="$1" 'BEGIN {
		split("B KB MB GB TB", u, " "); i = 1
		while (b >= 1024 && i < 5) { b /= 1024; i++ }
		s = (i == 1 || b >= 10) ? sprintf("%d %s", b, u[i]) : sprintf("%.1f %s", b, u[i])
		sub(/\./, ",", s); print s
	}'
}

# quando SEGUNDOS: "hoje 14:00", "ontem 09:00", "seg 02/11 18:00"
quando()
{
	hoje=$(date +%Y-%m-%d)
	ontem=$(date -v-1d +%Y-%m-%d)
	dia=$(date -r "$1" +%Y-%m-%d)
	hora=$(date -r "$1" +%H:%M)
	if [ "$dia" = "$hoje" ]; then echo "hoje $hora"
	elif [ "$dia" = "$ontem" ]; then echo "ontem $hora"
	else echo "$(LC_TIME=pt_BR.UTF-8 date -r "$1" '+%a %d/%m/%Y') $hora"
	fi
}

nome_tipo()
{
	case "$1" in
	auto) echo "automática" ;;
	manual) echo "manual" ;;
	antes) echo "antes de mudança" ;;
	*) echo "$1" ;;
	esac
}

# escolher QUANDO -> nome da foto (a última até aquele momento)
escolher()
{
	q=$*
	[ -n "$q" ] || erro "diga qual foto: o número (lume tempo linha), uma data ou hora"
	linha_do_tempo > "${TMP:?}/linha"
	case "$q" in
	"$PREFIXO"*)
		awk -F'\t' -v n="$q" '$2 == n { print $2; f = 1 } END { exit !f }' \
		    "$TMP/linha" || erro "não há foto chamada $q"
		return ;;
	*[!0-9]*) ;;
	*)
		awk -F'\t' -v n="$q" '$1 == n { print $2; f = 1 } END { exit !f }' \
		    "$TMP/linha" || erro "não há foto número $q (veja: lume tempo linha)"
		return ;;
	esac
	case "$q" in
	hoje) ate=$(date -j -f '%Y-%m-%d %H:%M:%S' "$(date +%Y-%m-%d) 23:59:59" +%s) ;;
	ontem) ate=$(date -j -f '%Y-%m-%d %H:%M:%S' "$(date -v-1d +%Y-%m-%d) 23:59:59" +%s) ;;
	[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])
		ate=$(date -j -f '%Y-%m-%d %H:%M:%S' "$q 23:59:59" +%s 2>/dev/null) ;;
	[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]\ [0-9]*:[0-9][0-9])
		ate=$(date -j -f '%Y-%m-%d %H:%M:%S' "$q:59" +%s 2>/dev/null) ;;
	[0-9]*:[0-9][0-9])
		ate=$(date -j -f '%Y-%m-%d %H:%M:%S' "$(date +%Y-%m-%d) $q:59" +%s 2>/dev/null) ;;
	*) ate= ;;
	esac
	[ -n "$ate" ] || erro "não entendi \"$q\": use o número da foto, 2026-11-03, \"2026-11-03 14:00\", 14:00, hoje ou ontem"
	awk -F'\t' -v t="$ate" '$3 <= t { print $2; f = 1; exit } END { exit !f }' \
	    "$TMP/linha" || erro "não há foto até $q"
}

# criacao NOME -> segundos da foto
criacao()
{
	fotos | awk -F'\t' -v n="@$1" 'substr($1, length($1) - length(n) + 1) == n { print $2; exit }'
}

# foto_do DS NOME -> a foto de DS mais nova até o momento da foto NOME (um
# sistema de arquivos que não mudou naquela hora não entrou nela)
foto_do()
{
	t=$(criacao "$2")
	[ -n "$t" ] || return 1
	zfs list -H -p -t snapshot -o name,creation -S creation "$1" 2>/dev/null |
	    awk -F'\t' -v p="@$PREFIXO" -v t="$t" '
		index($1, p) && $2 <= t { sub(/^[^@]*@/, "", $1); print $1; f = 1; exit }
		END { exit !f }'
}

# tirar TIPO DESCRIÇÃO [conjuntos...]: uma foto atômica de vários sistemas
# de arquivos de uma vez; imprime o nome.
tirar()
{
	tipo=$1; desc=$2; shift 2
	[ $# -gt 0 ] || return 1
	nome=$PREFIXO$(date +%Y-%m-%d_%H.%M.%S)
	set -- $(for ds; do echo "$ds@$nome"; done)
	if [ -n "$desc" ] && [ "$desc" != - ]; then
		zfs snapshot -o lume:tipo="$tipo" -o lume:descricao="$desc" "$@"
	else
		zfs snapshot -o lume:tipo="$tipo" "$@"
	fi || return 1
	registrar "foto $nome ($tipo${desc:+: $desc}): $*"
	echo "$nome"
}

# Guarda um resumo para o painel do "lume" (que não roda zfs).
gravar_estado()
{
	mkdir -p "$DB"
	linha_do_tempo > "$TMP/linha"
	n=$(wc -l < "$TMP/linha" | tr -d ' ')
	ultima=$(awk -F'\t' 'NR == 1 { print $3 }' "$TMP/linha")
	ocupa=$(ocupado)
	printf 'fotos=%s\nultima=%s\nocupa=%s\n' "$n" "${ultima:-0}" "$ocupa" \
	    > "$ESTADO.tmp" && mv "$ESTADO.tmp" "$ESTADO"
}

# --- limpeza ------------------------------------------------------------------

# Quais fotos saem pela idade (uma por linha: conjunto@nome).
vencidas()
{
	agora=$(date +%s)
	fuso=$(date +%z | awk '{ s = substr($1, 1, 1) == "-" ? -1 : 1
		print s * (substr($1, 2, 2) * 3600 + substr($1, 4, 2) * 60) }')
	fotos | awk -F'\t' -v agora="$agora" -v fuso="$fuso" \
	    -v horas="$(conf horas 24)" -v dias="$(conf dias 30)" \
	    -v semanas="$(conf semanas 26)" -v antes="$(conf antes_dias 30)" '
	{
		split($1, a, "@"); ds = a[1]; idade = agora - $2
		dia = int(($2 + fuso) / 86400)
		semana = int((dia + 3) / 7)
		if ($4 == "manual") next
		if ($4 == "antes") {
			if (idade > antes * 86400) print $1
			next
		}
		# Automáticas: a primeira de cada dia e de cada semana fica.
		primeira_dia = !((ds, dia) in vd); vd[ds, dia] = 1
		primeira_sem = !((ds, semana) in vs); vs[ds, semana] = 1
		if (idade <= horas * 3600) next
		if (idade <= dias * 86400 && primeira_dia) next
		if (idade <= semanas * 7 * 86400 && primeira_sem) next
		print $1
	}'
}

podar()
{
	vencidas | while read -r f; do
		zfs destroy "$f" 2>/dev/null && registrar "apagada pela idade: $f"
	done
	# Disco quase cheio: as automáticas mais velhas saem primeiro.
	limite=$(conf limite 85)
	p=$(pool)
	i=0
	while [ "$i" -lt 200 ]; do
		cheio=$(zpool list -H -o capacity "$p" | tr -d '%')
		[ "$cheio" -ge "$limite" ] || break
		f=$(fotos | awk -F'\t' '$4 == "auto" { print $1; exit }')
		[ -n "$f" ] && [ "$f" != "${ultima_tentada:-}" ] || break
		ultima_tentada=$f
		n=${f#*@}
		fotos | awk -F'\t' -v n="@$n" 'substr($1, length($1) - length(n) + 1) == n { print $1 }' |
		    while read -r g; do
			zfs destroy "$g" 2>/dev/null &&
			    registrar "apagada por falta de espaço ($cheio%): $g"
		done
		i=$((i + 1))
	done
}

# --- comandos -----------------------------------------------------------------

cmd_resumo()
{
	exige_zfs
	linha_do_tempo > "$TMP/linha"
	echo
	if [ "$(sysrc -n lume_tempo_enable 2>/dev/null)" = NO ]; then
		msg "${NEGRITO}Máquina do tempo${FIM}: ${AMARELO}desligada${FIM} (sysrc lume_tempo_enable=YES liga)"
	else
		msg "${NEGRITO}Máquina do tempo${FIM}: ${VERDE}ligada${FIM}, uma foto por hora quando algo muda"
	fi
	areas=$(alvos | while read -r ds; do area "$ds"; done | paste -sd, - |
	    sed 's/,/, /g')
	msg "guarda: ${areas:-nada (lume tempo ligar /home)}"
	n=$(wc -l < "$TMP/linha" | tr -d ' ')
	if [ "$n" -eq 0 ]; then
		msg "nenhuma foto ainda; a primeira sai em até 15 minutos (ou: lume tempo foto)"
		echo
		return
	fi
	ocupa=$(ocupado)
	velha=$(awk -F'\t' 'END { print $3 }' "$TMP/linha")
	msg "$n fotos, desde $(quando "$velha"); ocupam $(humano "$ocupa") a mais no disco"
	echo
	cmd_linha 8
	msg "${FRACO}lume tempo versoes ARQUIVO   lume tempo recuperar ARQUIVO [QUANDO]${FIM}"
	msg "${FRACO}No Dolphin: botão direito > Versões anteriores. Mais: man lume-tempo${FIM}"
	echo
}

cmd_linha()
{
	exige_zfs
	max=${1:-1000000}
	linha_do_tempo > "$TMP/linha"
	[ -s "$TMP/linha" ] || { msg "nenhuma foto ainda"; return; }
	head -n "$max" "$TMP/linha" | while IFS='	' read -r num nome cria usada tipo areas desc; do
		[ "$desc" = - ] && desc=
		printf "  ${AMARELO}%3s${FIM}  %s %s ${FRACO}%s %8s${FIM}  %s\n" \
		    "$num" "$(pad "$(quando "$cria")" 22)" \
		    "$(pad "$(nome_tipo "$tipo")" 17)" \
		    "$(pad "$(echo "$areas" | sed 's/,/, /g')" 16)" \
		    "$(humano "$usada")" "$desc"
	done
	total=$(wc -l < "$TMP/linha" | tr -d ' ')
	[ "$total" -gt "$max" ] &&
	    msg "${FRACO}... e mais $((total - max)) (lume tempo linha)${FIM}"
	echo
}

# onde CAMINHO -> define ABS, DS, MP, REL (o arquivo pode não existir mais)
onde()
{
	[ -n "$1" ] || erro "diga qual arquivo ou pasta"
	case "$1" in
	/*) c=$1 ;;
	*) c=$PWD/$1 ;;
	esac
	c=$(echo "$c" | sed 's|/*$||')
	[ -n "$c" ] || c=/
	pai=$(dirname "$c")
	# A pasta pode também ter sumido: sobe até achar uma que existe.
	resto=$(basename "$c")
	while [ ! -d "$pai" ]; do
		resto="$(basename "$pai")/$resto"
		pai=$(dirname "$pai")
	done
	pai=$(realpath "$pai")
	if [ "$c" = / ]; then ABS=/; else ABS=${pai%/}/$resto; fi
	# O sistema de arquivos de montagem mais longa que contém o caminho.
	set -- $(zfs list -H -t filesystem -o name,mountpoint,mounted -r "$(pool)" |
	    awk -F'\t' -v c="$ABS" '
		$3 == "yes" && $2 ~ /^\// {
			m = $2
			if (c == m || m == "/" || index(c, m "/") == 1)
				if (length(m) > length(melhor)) { melhor = m; ds = $1 }
		}
		END { if (ds != "") print ds, melhor }')
	DS=$1; MP=$2
	[ -n "$DS" ] || erro "$ABS não está num sistema de arquivos ZFS"
	if [ "$ABS" = "$MP" ]; then REL=; else REL=${ABS#"${MP%/}"/}; fi
}

# caminho_na FOTO -> onde o arquivo estava naquela foto
caminho_na()
{
	echo "${MP%/}/.zfs/snapshot/$1${REL:+/$REL}"
}

# igual A B: mesmo conteúdo (arquivos) ou mesma data e tamanho (pastas)
igual()
{
	[ -e "$1" ] && [ -e "$2" ] || return 1
	if [ -d "$1" ] || [ -d "$2" ]; then
		[ -d "$1" ] && [ -d "$2" ] &&
		    [ "$(stat -f '%m %z' "$1")" = "$(stat -f '%m %z' "$2")" ]
	else
		cmp -s "$1" "$2"
	fi
}

# lista_versoes -> "nome criação caminho" de cada versão diferente da
# seguinte (das mais novas para as mais velhas), em $TMP/versoes
lista_versoes()
{
	: > "$TMP/versoes"
	anterior=$ABS
	zfs list -H -p -t snapshot -o name,creation -S creation "$DS" 2>/dev/null |
	    awk -F'\t' -v p="@$PREFIXO" 'index($1, p) { sub(/^[^@]*@/, "", $1); print $1 "\t" $2 }' |
	    while IFS='	' read -r nome cria; do
		c=$(caminho_na "$nome")
		[ -e "$c" ] || { anterior=; continue; }
		if [ -n "$anterior" ] && igual "$c" "$anterior"; then
			continue
		fi
		printf '%s\t%s\t%s\n' "$nome" "$cria" "$c" >> "$TMP/versoes"
		anterior=$c
	done
}

cmd_versoes()
{
	exige_zfs
	onde "$1"
	lista_versoes
	echo
	if [ -e "$ABS" ]; then
		msg "${NEGRITO}$ABS${FIM}"
	else
		msg "${NEGRITO}$ABS${FIM} ${AMARELO}(apagado; dá para recuperar)${FIM}"
	fi
	if [ ! -s "$TMP/versoes" ]; then
		if zfs list -H -t snapshot -o name "$DS" 2>/dev/null | grep -q "@$PREFIXO"; then
			msg "nenhuma versão diferente da atual nas fotos"
		else
			msg "a pasta $MP não entra nas fotos (root: lume tempo ligar $MP)"
		fi
		echo
		return
	fi
	while IFS='	' read -r nome cria c; do
		if [ -d "$c" ]; then
			info="pasta, $(ls -A "$c" 2>/dev/null | wc -l | tr -d ' ') itens"
		else
			info=$(humano "$(stat -f %z "$c")")
		fi
		printf "  %s %10s   ${FRACO}%s${FIM}\n" "$(pad "$(quando "$cria")" 24)" "$info" "$nome"
	done < "$TMP/versoes"
	echo
	msg "${FRACO}lume tempo recuperar \"$1\" [QUANDO]   (ou abra direto: ${MP%/}/.zfs/snapshot/FOTO/)${FIM}"
	echo
}

# destino_lado CAMINHO CRIAÇÃO -> "nome (de 03-11 14.00).ext"
destino_lado()
{
	d=$(dirname "$1"); b=$(basename "$1")
	rotulo="de $(date -r "$2" '+%d-%m %H.%M')"
	case "$b" in
	?*.*) base=${b%.*}; ext=.${b##*.} ;;
	*) base=$b; ext= ;;
	esac
	[ -d "$1" ] && { base=$b; ext=; }
	alvo="$d/$base ($rotulo)$ext"
	n=2
	while [ -e "$alvo" ]; do
		alvo="$d/$base ($rotulo, $n)$ext"; n=$((n + 1))
	done
	echo "$alvo"
}

# recuperar_de NOME CRIAÇÃO ORIGEM [substituir] -> imprime para onde foi
recuperar_de()
{
	nome=$1; cria=$2; origem=$3; modo=$4
	if [ ! -e "$ABS" ] && [ ! -L "$ABS" ]; then
		mkdir -p "$(dirname "$ABS")" || return 1
		destino=$ABS
	elif [ "$modo" = substituir ]; then
		guardado=$(destino_lado "$ABS" "$(date +%s)" | sed 's/(de /(atual, /')
		if [ "$(id -u)" -eq 0 ] && tem_zfs; then
			tirar antes "recuperar $ABS" "$DS" > /dev/null
			rm -rf "$ABS" || return 1
		else
			mv "$ABS" "$guardado" || return 1
			echo "$guardado" > "$TMP/guardado"
		fi
		destino=$ABS
	else
		destino=$(destino_lado "$ABS" "$cria")
	fi
	cp -Rp "$origem" "$destino" || return 1
	registrar "recuperado ($nome): $ABS -> $destino"
	echo "$destino"
}

cmd_recuperar()
{
	exige_zfs
	alvo=; quando_q=; modo=
	for a; do
		case "$a" in
		--substituir) modo=substituir ;;
		*) if [ -z "$alvo" ]; then alvo=$a; else quando_q="${quando_q:+$quando_q }$a"; fi ;;
		esac
	done
	onde "$alvo"
	lista_versoes
	if [ -n "$quando_q" ]; then
		escolhida=$(escolher "$quando_q") || exit 1
		nome=$(foto_do "$DS" "$escolhida") ||
		    erro "a pasta $MP não está em nenhuma foto até $(quando "$(criacao "$escolhida")")"
		cria=$(zfs get -H -p -o value creation "$DS@$nome")
		c=$(caminho_na "$nome")
		[ -e "$c" ] || erro "$ABS não existia em $(quando "$cria")"
	else
		# Sem QUANDO: a versão mais nova que é diferente da de agora.
		IFS='	' read -r nome cria c < "$TMP/versoes" ||
		    erro "não há versão anterior de $ABS nas fotos"
	fi
	if [ -e "$ABS" ] && igual "$c" "$ABS"; then
		ok "a versão de $(quando "$cria") é igual à de agora; nada a fazer"
		return
	fi
	destino=$(recuperar_de "$nome" "$cria" "$c" "$modo") ||
	    erro "não deu para recuperar $ABS"
	ok "versão de $(quando "$cria") recuperada em:\n    $destino"
	[ -f "$TMP/guardado" ] && msg "a versão de agora ficou em: $(cat "$TMP/guardado")"
}

cmd_foto()
{
	raiz; exige_zfs; padrao_ligado
	set -- "$(echo "$*" | tr -d '\t\n' | cut -c1-120)"
	nome=$(tirar manual "${1:--}" $(alvos)) ||
	    erro "não deu para tirar a foto (lume tempo ligar /home?)"
	gravar_estado
	ok "foto ${NEGRITO}$nome${FIM} tirada; fica até você apagar (lume tempo apagar 1)"
}

cmd_antes()
{
	raiz
	tem_zfs || return 0
	[ "$(sysrc -n lume_tempo_enable 2>/dev/null)" = NO ] && return 0
	padrao_ligado
	nome=$(tirar antes "$(echo "$*" | tr -d '\t\n' | cut -c1-120)" $(alvos)) ||
	    return 0
	gravar_estado
	msg "${FRACO}foto de antes: $nome (lume tempo)${FIM}"
}

cmd_mudou()
{
	raiz; exige_zfs
	escolhida=$(escolher "${*:-1}") || exit 1
	echo
	msg "${NEGRITO}O que mudou desde $(quando "$(criacao "$escolhida")")${FIM} ${FRACO}($escolhida)${FIM}"
	nada=sim
	for ds in $(alvos); do
		nome=$(foto_do "$ds" "$escolhida") || continue
		# Sem os arquivos da própria máquina do tempo; nomes com espaço e
		# acento vêm como \0NNN (octal), que o printf %b desfaz.
		zfs diff -FH "$ds@$nome" "$ds" 2>/dev/null |
		    awk -F'\t' '
			$3 ~ /\/\.zfs(\/|$)/ || $3 ~ /^\/var\/db\/lume\/tempo/ { next }
			# O que todo boot recria (sockets, pids, entropia) é ruído.
			$3 ~ /^\/(var\/run|var\/spool\/lock|var\/db\/entropy|tmp)(\/|$)/ { next }
			$3 ~ /^\/(boot\/)?entropy$/ { next }
			# Pasta "mudou" só porque algo dentro dela mudou: já aparece.
			$1 == "M" && $2 == "/" { next }
			{ print }' | sort -t '	' -k3 |
		    while IFS= read -r l; do printf '%b\n' "$l"; done > "$TMP/diff"
		[ -s "$TMP/diff" ] || continue
		nada=
		echo
		msg "${AMARELO}$(area "$ds")${FIM}"
		# Apagado e criado de novo no mesmo caminho (salvar por cima com
		# arquivo novo) é só "mudou".
		awk -F'\t' '
			NR == FNR { if ($1 == "-") apag[$3] = 1; if ($1 == "+") novo[$3] = 1; next }
			($1 == "-" || $1 == "+") && ($3 in apag) && ($3 in novo) {
				if ($3 in visto) next
				visto[$3] = 1; $1 = "M"
			}
			$1 == "+" { s = "novo     " }
			$1 == "-" { s = "apagado  " }
			$1 == "M" { s = "mudou    " }
			$1 == "R" { s = "renomeado"; $3 = $3 " -> " $4 }
			$2 == "/" { $3 = $3 "/" }
			{ print "    " s "  " $3 }' "$TMP/diff" "$TMP/diff" > "$TMP/mudancas"
		head -40 "$TMP/mudancas"
		total=$(wc -l < "$TMP/mudancas" | tr -d ' ')
		[ "$total" -gt 40 ] && msg "${FRACO}  ... e mais $((total - 40)) mudanças${FIM}"
	done
	[ -n "$nada" ] && { echo; msg "nada mudou"; }
	echo
}

cmd_voltar()
{
	raiz; exige_zfs
	[ $# -gt 0 ] || erro "diga para quando: lume tempo voltar 3 (veja: lume tempo linha)"
	command -v bectl >/dev/null && bectl list >/dev/null 2>&1 ||
	    erro "o sistema não está em ambientes de boot"
	nome=$(escolher "$*") || exit 1
	raiz_ds=$(zfs list -H -o name /)
	be=${raiz_ds##*/}
	cria=$(criacao "$nome")
	# A foto do sistema mais nova até aquele momento, de qualquer ambiente
	# de boot (o de antes de uma atualização guarda as fotos de antes dela).
	f=$(zfs list -H -p -t snapshot -o name,creation -S creation -r "${raiz_ds%/*}" |
	    awk -F'\t' -v p="@$PREFIXO" -v t="$cria" 'index($1, p) && $2 <= t { print $1; exit }')
	[ -n "$f" ] ||
	    erro "o sistema não está em nenhuma foto até $(quando "$cria")"
	snap=${f#*@}; de=${f%@*}; de=${de##*/}
	novo=$(date -r "$cria" '+volta-%Y%m%d-%H%M')
	n=2; base=$novo
	while bectl list -H | awk '{ print $1 }' | grep -qx "$novo"; do
		novo="$base-$n"; n=$((n + 1))
	done
	echo
	msg "O ${NEGRITO}sistema${FIM} volta a ser como estava em ${NEGRITO}$(quando "$cria")${FIM}:"
	msg "programas, /etc e o que mais estiver fora das suas pastas pessoais."
	msg "As pastas pessoais (/home) ${NEGRITO}não mudam${FIM}. O sistema de agora fica"
	msg "guardado como ambiente de boot ${NEGRITO}$be${FIM}, para desfazer."
	echo
	if [ -t 0 ] && [ -z "${LUME_SIM:-}" ]; then
		printf "  Voltar e usar no próximo boot? [s/N] "
		read -r r
		case "$r" in [sS]*) ;; *) msg "nada mudou"; return ;; esac
	fi
	bectl create -e "$de@$snap" "$novo" || erro "não deu para criar o ambiente $novo"
	# Como no lume atualizar: o "zfs mount -a" não pode montar outro
	# ambiente por cima, e nada de raiz fixa no loader.conf.
	for d in $(zfs list -H -o name -r "${raiz_ds%/*}" | sed 1d); do
		zfs set canmount=noauto "$d"
	done
	mnt=$(mktemp -d /tmp/lume-tempo.XXXXXX)
	if bectl mount "$novo" "$mnt" >/dev/null; then
		[ -f "$mnt/boot/loader.conf" ] &&
		    sed -i '' '/^vfs\.root\.mountfrom=/d' "$mnt/boot/loader.conf"
		bectl umount "$novo"
	fi
	rmdir "$mnt" 2>/dev/null
	bectl activate "$novo" >/dev/null || erro "não deu para ativar $novo"
	registrar "voltar: $novo (de $de@$snap) ativado"
	ok "pronto: reinicie para usar o sistema de $(quando "$cria") (${NEGRITO}shutdown -r now${FIM})"
	msg "${FRACO}Para desfazer: bectl activate $be${FIM}"
}

cmd_apagar()
{
	raiz; exige_zfs
	nome=$(escolher "$*") || exit 1
	n=0
	for f in $(fotos | awk -F'\t' -v n="@$nome" 'substr($1, length($1) - length(n) + 1) == n { print $1 }'); do
		if zfs destroy "$f" 2>/dev/null; then
			n=$((n + 1))
		else
			aviso "$f está em uso (um ambiente de boot saiu dela?) e ficou"
		fi
	done
	registrar "apagada a pedido: $nome ($n)"
	gravar_estado
	ok "foto $nome apagada"
}

cmd_ligar()
{
	raiz; exige_zfs
	valor=$1; shift
	onde "${1:-/home}"
	case "$DS" in */ROOT/*) DS=${DS%/*} ;; esac
	zfs set lume:tempo="$valor" "$DS" || erro "não deu para mudar $DS"
	if [ "$valor" = on ]; then
		ok "$(area "$DS" | sed 's|^sistema$|o sistema|') entra nas fotos"
	else
		ok "$(area "$DS" | sed 's|^sistema$|o sistema|') sai das próximas fotos (as que já existem ficam)"
	fi
}

# O menu do Dolphin vai junto do KDE (quem atualizou da 1.1 também ganha).
integrar_kde()
{
	[ -f "$MENU_DOLPHIN" ] && [ -x /usr/local/bin/plasmashell ] || return 0
	app=/usr/share/lume/app/lume-atualizacoes.desktop
	cmp -s "$MENU_DOLPHIN" "$MENU_DESTINO" &&
	    { [ ! -f "$app" ] || cmp -s "$app" /usr/local/share/applications/lume-atualizacoes.desktop; } &&
	    return 0
	/usr/sbin/lume-tema integrar > /dev/null 2>&1
}

cmd_automatico()
{
	raiz
	tem_zfs || return 0
	[ "$(sysrc -n lume_tempo_enable 2>/dev/null)" = NO ] && return 0
	# Um de cada vez (o cron chama a cada 15 minutos).
	mkdir -p "$DB"
	[ -n "${LUME_TEMPO_TRAVA:-}" ] ||
	    LUME_TEMPO_TRAVA=1 exec lockf -s -t 0 "$DB/tempo.trava" "$0" automatico
	padrao_ligado
	integrar_kde
	# Uma por hora: a cada 15 minutos o cron confere se já passou uma hora
	# desde a última automática (computador desligado não perde a vez).
	agora=$(date +%s)
	ultima=$(fotos | awk -F'\t' '$4 == "auto" { u = $2 } END { print u + 0 }')
	if [ $((agora - ultima)) -ge 3500 ]; then
		mudaram=
		for ds in $(alvos); do
			# "written": o que mudou desde a foto anterior (tudo, se não há).
			[ "$(zfs get -H -p -o value written "$ds")" != 0 ] &&
			    mudaram="$mudaram $ds"
		done
		[ -n "$mudaram" ] && tirar auto - $mudaram > /dev/null
	fi
	podar
	gravar_estado
}

# --- a janela do Dolphin ------------------------------------------------------

cmd_gui()
{
	command -v kdialog >/dev/null || {
		xmessage -center "Falta o kdialog: pkg install kdialog" 2>/dev/null
		erro "falta o kdialog (pkg install kdialog)"
	}
	titulo="Versões anteriores"
	tem_zfs || { kdialog --title "$titulo" --sorry "A máquina do tempo precisa do LumeBSD instalado com ZFS."; exit 1; }
	(onde "$1") 2>/dev/null || { kdialog --title "$titulo" --sorry "$1 não está num disco com fotos."; exit 1; }
	onde "$1"
	lista_versoes
	b=$(basename "$ABS")
	if [ ! -s "$TMP/versoes" ]; then
		kdialog --title "$titulo" --msgbox "Não há versões anteriores de “$b”.\n\nA máquina do tempo tira uma foto por hora quando algo muda; a primeira versão diferente aparece aqui depois disso."
		return
	fi
	# Uma opção por versão: "hoje 14:00 · 12 KB"
	set --
	i=0
	while IFS='	' read -r nome cria c; do
		i=$((i + 1))
		if [ -d "$c" ]; then
			info="$(ls -A "$c" 2>/dev/null | wc -l | tr -d ' ') itens"
		else
			info=$(humano "$(stat -f %z "$c")")
		fi
		estado=off; [ $i -eq 1 ] && estado=on
		set -- "$@" "$i" "$(quando "$cria")   ·   $info" "$estado"
	done < "$TMP/versoes"
	cabeca="“$b”"
	[ -e "$ABS" ] || cabeca="“$b” (apagado)"
	esc=$(kdialog --title "$titulo" --geometry 460x380 \
	    --radiolist "$cabeca\nem $(dirname "$ABS")\n\nEscolha a versão:" "$@") || return 0
	linha=$(sed -n "${esc}p" "$TMP/versoes")
	nome=$(echo "$linha" | cut -f1); cria=$(echo "$linha" | cut -f2)
	c=$(echo "$linha" | cut -f3-)
	if [ -e "$ABS" ]; then
		acao=$(kdialog --title "$titulo" --geometry 460x240 \
		    --menu "Versão de $(quando "$cria"):" \
		    abrir "Abrir esta versão (só leitura)" \
		    lado "Recuperar ao lado da atual" \
		    substituir "Substituir a atual (que fica guardada ao lado)") ||
		    return 0
	else
		acao=$(kdialog --title "$titulo" --geometry 460x240 \
		    --menu "Versão de $(quando "$cria"):" \
		    restaurar "Recuperar no lugar de onde foi apagado" \
		    abrir "Abrir esta versão (só leitura)") || return 0
	fi
	case "$acao" in
	abrir)
		xdg-open "$c" >/dev/null 2>&1 &
		return ;;
	lado|restaurar) modo= ;;
	substituir) modo=substituir ;;
	*) return 0 ;;
	esac
	rm -f "$TMP/guardado"
	if destino=$(recuperar_de "$nome" "$cria" "$c" "$modo" 2> "$TMP/aviso"); then
		extra=
		[ -f "$TMP/guardado" ] &&
		    extra="\n\nA versão de agora ficou guardada como “$(basename "$(cat "$TMP/guardado")")”."
		kdialog --title "$titulo" --msgbox "“$(basename "$destino")” recuperado (versão de $(quando "$cria")).$extra"
		command -v dolphin >/dev/null && [ -n "$DISPLAY$WAYLAND_DISPLAY" ] &&
		    dolphin --select "$destino" >/dev/null 2>&1 &
	else
		kdialog --title "$titulo" --error "Não deu para recuperar “$b”.\n\n$(cat "$TMP/aviso")"
	fi
}

# --- início -------------------------------------------------------------------

TMP=$(mktemp -d "${TMPDIR:-/tmp}/lume-tempo.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT
trap 'exit 130' INT TERM

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
"")			cmd_resumo ;;
linha|lista)		cmd_linha "$@" ;;
versoes|versões)	cmd_versoes "$@" ;;
recuperar)		cmd_recuperar "$@" ;;
foto)			cmd_foto "$@" ;;
antes)			cmd_antes "$@" ;;
mudou)			cmd_mudou "$@" ;;
voltar)			cmd_voltar "$@" ;;
apagar)			cmd_apagar "$@" ;;
ligar)			cmd_ligar on "$@" ;;
desligar)		cmd_ligar off "$@" ;;
automatico|automático)	cmd_automatico ;;
gui)			cmd_gui "$@" ;;
-h|--help|ajuda)	sed -n '5,40p' "$0" | sed 's/^# \{0,1\}//' ;;
*)			erro "comando desconhecido: $cmd (lume tempo ajuda)" ;;
esac

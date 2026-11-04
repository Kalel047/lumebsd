#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-atualizar - atualiza o sistema do LumeBSD sem risco.
#
#   lume atualizar             procura, pergunta, instala e pede para reiniciar
#   lume atualizar verificar   só diz se há versão nova (não precisa de root)
#   lume atualizar aplicar [ÍNDICE|ARQUIVO]
#                              instala (de um índice .txt ou de um .tar.zst)
#   lume atualizar estado      versão, base, ambientes de boot, o que está em
#                              teste e se a última atualização falhou
#   lume atualizar confirmar   torna permanente a atualização em teste
#   lume atualizar voltar      volta para o sistema de antes da atualização
#   lume atualizar descartar   apaga o ambiente de uma atualização que falhou
#   lume atualizar limpar      apaga ambientes de atualizações antigas
#   lume atualizar reiniciar   reinicia o computador (o app usa este, com a
#                              mesma permissão do polkit que a atualização)
#
#   --maquina (antes do comando): saída em linhas "@chave ...", para o app
#   Atualizações do LumeBSD (lume-atualizacoes).
#
# Qualquer versão nova serve: 1.1.1 -> 1.1.2 (correção), 1.1 -> 1.2 (versão
# nova), 1 -> 2 (versão principal). O que decide o trabalho é a base FreeBSD
# por baixo, que o índice informa (base=15.1, abi=, osversion=):
#   - mesma base: só o sistema troca;
#   - outra base, mesma série (15.1 -> 15.2): os drivers do kernel de
#     terceiros (vídeo, Wi-Fi, firmwares) são baixados para o kernel novo;
#   - outra série (15 -> 16): todos os programas são baixados de novo.
# Os pacotes são baixados antes, ainda no sistema atual, e instalados no
# primeiro boot da versão nova (rc.d/lume_pacotes), já com o kernel novo.
#
# Como funciona: a versão nova é instalada num ambiente de boot novo (uma
# cópia do sistema atual no ZFS), nunca no sistema em uso. Esse ambiente é
# ativado só para o próximo boot (bectl activate -t). Se o boot terminar e
# a checagem de saúde passar (rc.d/lume_saude), ele vira o padrão. Se o
# computador travar, entrar em pânico ou a checagem falhar, o boot seguinte
# volta sozinho para o sistema anterior, intacto.
#
# Configurações (/etc/rc.conf): lume_atualizar_url, o índice das versões.
#

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export PATH

INDICE_PADRAO="https://github.com/Kalel047/lumebsd/releases/latest/download/atualizacao.txt"
DB=/var/db/lume
PENDENTE=$DB/atualizacao-pendente
ANTERIOR=$DB/ambiente-anterior
PACOTES_PEND=$DB/pacotes-pendentes
PACOTES_FALHA=$DB/pacotes-falharam
RECOMPILAR=$DB/recompilar
VERSAO_ARQ=/usr/share/lume/versao
LOG=/var/log/lume-atualizar.log
if [ "$(id -u)" -eq 0 ]; then
	CACHE=/var/tmp/lume-atualizar
else
	CACHE=${TMPDIR:-/tmp}/lume-atualizar-$(id -u)
fi

MAQ=
if [ "${1:-}" = --maquina ]; then
	MAQ=1; shift
fi

if [ -z "$MAQ" ] && [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	VERDE='\033[32;1m'; AMARELO='\033[33;1m'; VERMELHO='\033[31;1m'
	FRACO='\033[2m'; NEGRITO='\033[1m'; FIM='\033[0m'
else
	VERDE=; AMARELO=; VERMELHO=; FRACO=; NEGRITO=; FIM=
fi
# Mensagens: para gente, com cores; para o app, uma linha "@tipo texto".
saida()
{
	tipo=$1; shift
	if [ -n "$MAQ" ]; then
		printf '@%s %b\n' "$tipo" "$*" | tr '\n' ' ' | sed 's/ *$//'
		echo
		return
	fi
	case "$tipo" in
	msg)	printf "  %b\n" "$*" ;;
	ok)	printf "  ${VERDE}✓${FIM} %b\n" "$*" ;;
	aviso)	printf "  ${AMARELO}!${FIM} %b\n" "$*" ;;
	erro)	printf "  ${VERMELHO}✗${FIM} %b\n" "$*" >&2 ;;
	esac
}
msg()	{ saida msg "$*"; }
ok()	{ saida ok "$*"; }
aviso()	{ saida aviso "$*"; }
erro()	{ saida erro "$*"; exit 1; }
# Linhas só para o app (o terminal já mostra o seu próprio progresso).
app()	{ [ -n "$MAQ" ] && echo "@$*"; return 0; }
passo()
{
	if [ -n "$MAQ" ]; then echo "@passo $1 $2 $3"
	else printf "  ${FRACO}[%s/%s]${FIM} %s\n" "$1" "$2" "$3"; fi
}
registrar() { [ "$(id -u)" -eq 0 ] && echo "$(date '+%F %T') $*" >> "$LOG"; return 0; }

raiz()	{ [ "$(id -u)" -eq 0 ] || erro "precisa ser root (su -), ou use o app Atualizações do LumeBSD"; }

# Gerados a partir de configurações (pwd_mkdb, cap_mkdb, services_mkdb):
# nunca vêm do pacote, para não desencontrar das configurações mantidas.
DERIVADOS="etc/pwd.db etc/spwd.db etc/passwd etc/login.conf.db var/db/services.db"

# Condição do awk para "arquivo de configuração do usuário" numa linha do
# METALOG: a tag config sozinha (não "bsdconfig"), fora dos scripts do sistema.
E_CONFIG='(/ tags=([^ ]*,)?config(,| |$)/ && $1 !~ /^\.\/etc\/(rc\.d|defaults|periodic)\//)'

# --- versões e base -----------------------------------------------------------

versao_atual()
{
	v=$(head -1 "$VERSAO_ARQ" 2>/dev/null)
	# O 1.1 lançado antes deste arquivo existir é o 1.1.0.
	echo "${v:-1.1.0}"
}

# A base FreeBSD do sistema em uso, do __FreeBSD_version da userland
# (1501000 -> 15.1).
osversion_atual() { uname -U; }
base_de()	{ echo "$(($1 / 100000)).$(($1 / 1000 % 100))"; }
base_atual()	{ base_de "$(osversion_atual)"; }

# maior A B: verdadeiro se a versão A (x.y.z) é maior que B
maior()
{
	echo "$1 $2" | awk '{
		n = split($1, a, "."); split($2, b, ".")
		for (i = 1; i <= 3; i++) {
			if (a[i] + 0 > b[i] + 0) exit 0
			if (a[i] + 0 < b[i] + 0) exit 1
		}
		exit 1
	}'
}

# tipo NOVA ATUAL: correcao (x.y.Z), versao (x.Y.z) ou principal (X.y.z)
tipo()
{
	if [ "${1%%.*}" != "${2%%.*}" ]; then echo principal
	elif [ "${1%.*}" != "${2%.*}" ]; then echo versao
	else echo correcao
	fi
}

nome_tipo()
{
	case "$1" in
	correcao) echo "correção" ;;
	versao) echo "versão nova" ;;
	principal) echo "versão principal" ;;
	esac
}

# troca BASE_NOVA BASE_ATUAL: nenhuma, drivers (15.1 -> 15.2: os módulos do
# kernel de terceiros) ou programas (15 -> 16: tudo o que veio do pkg)
troca()
{
	if [ "$1" = "$2" ]; then echo nenhuma
	elif [ "${1%%.*}" = "${2%%.*}" ]; then echo drivers
	else echo programas
	fi
}

texto_troca()
{
	case "$1" in
	drivers) echo "Troca a base para o FreeBSD $2: os drivers (vídeo, Wi-Fi, firmwares) são baixados de novo para o kernel novo." ;;
	programas) echo "Troca a base para o FreeBSD $2: todos os programas são baixados de novo para ela (pode levar um bom tempo e alguns GB)." ;;
	esac
}

# --- ambientes de boot -------------------------------------------------------

raiz_zfs()
{
	m=$(kenv -q vfs.root.mountfrom)
	case "$m" in zfs:*) ;; *) return 1 ;; esac
	command -v bectl >/dev/null && bectl list >/dev/null 2>&1
}

# ambiente em uso agora (N) e o padrão do próximo boot (R)
be_agora()	{ bectl list -H | awk '$2 ~ /N/ { print $1 }'; }
be_padrao()	{ bectl list -H | awk '$2 ~ /R/ { print $1 }'; }
be_existe()	{ bectl list -H | awk '{print $1}' | grep -qx "$1"; }
# o ativado só para o próximo boot (bectl activate -t), se há
be_proximo()	{ bectl list -H | awk '$2 ~ /T/ { print $1 }'; }

be_livre()
{
	nome=$1; n=2
	while be_existe "$nome"; do
		nome="$1-$n"; n=$((n + 1))
	done
	echo "$nome"
}

# --- índice ------------------------------------------------------------------

# campo ARQUIVO CHAVE: valor de "chave=valor" (sem executar nada do arquivo)
campo()
{
	awk -F= -v k="$2" '$1 == k { sub(/^[^=]*=/, ""); print; exit }' "$1"
}

url_indice()
{
	u=${LUME_ATUALIZAR_URL:-$(sysrc -n lume_atualizar_url 2>/dev/null)}
	echo "${u:-$INDICE_PADRAO}"
}

# baixar_indice URL -> grava $CACHE/indice.txt e confere os campos
baixar_indice()
{
	mkdir -p "$CACHE"
	fetch -q -o "$CACHE/indice.txt" "$1" 2>/dev/null ||
	    erro "não deu para ler o índice de atualizações: $1. Tem internet? (cabo ou: lume wifi)"
	I_VERSAO=$(campo "$CACHE/indice.txt" versao)
	I_ARQUIVO=$(campo "$CACHE/indice.txt" arquivo)
	I_SHA256=$(campo "$CACHE/indice.txt" sha256)
	I_TAMANHO=$(campo "$CACHE/indice.txt" tamanho)
	I_BASE=$(campo "$CACHE/indice.txt" base)
	I_ABI=$(campo "$CACHE/indice.txt" abi)
	I_OSVERSION=$(campo "$CACHE/indice.txt" osversion)
	I_NOTAS=$(campo "$CACHE/indice.txt" notas)
	I_MINIMO=$(campo "$CACHE/indice.txt" minimo)
	I_INTERMEDIARIO=$(campo "$CACHE/indice.txt" intermediario)
	echo "$I_VERSAO" | grep -Eqx '[0-9]+\.[0-9]+\.[0-9]+' &&
	    echo "$I_ARQUIVO" | grep -Eqx '[A-Za-z0-9._-]+\.tar\.zst' &&
	    echo "$I_SHA256" | grep -Eqx '[0-9a-f]{64}' ||
	    erro "índice de atualizações inválido ($1)"
	echo "${I_TAMANHO:-0}" | grep -Eqx '[0-9]+' || I_TAMANHO=0
	# Índices antigos (1.1.x) não dizem a base: era a 15.1.
	if [ -z "$I_BASE" ]; then
		I_BASE=15.1; I_ABI=FreeBSD:15:amd64; I_OSVERSION=1501000
	fi
	echo "$I_BASE" | grep -Eqx '[0-9]+\.[0-9]+' &&
	    echo "$I_ABI" | grep -Eqx 'FreeBSD:[0-9]+:[a-z0-9_]+' &&
	    echo "$I_OSVERSION" | grep -Eqx '[0-9]{7}' ||
	    erro "índice de atualizações inválido: base ($1)"
	echo "${I_NOTAS:-}" | grep -Eqx '([A-Za-z0-9._-]+)?' || I_NOTAS=
	I_URL="${1%/*}/$I_ARQUIVO"
	I_NOTAS_URL=
	[ -n "$I_NOTAS" ] && I_NOTAS_URL="${1%/*}/$I_NOTAS"
}

# resolver_indice URL: segue o índice; se a versão instalada é antiga demais
# para pular direto para a nova (minimo=), vai para a intermediária.
resolver_indice()
{
	u=$1; atual=$(versao_atual); n=0
	while :; do
		baixar_indice "$u"
		[ -n "$I_MINIMO" ] && maior "$I_MINIMO" "$atual" || return 0
		n=$((n + 1))
		[ "$n" -lt 5 ] && [ -n "$I_INTERMEDIARIO" ] ||
		    erro "da $atual não dá para ir direto à $I_VERSAO, e o índice não diz o caminho"
		case "$I_INTERMEDIARIO" in
		https://*|http://*) u=$I_INTERMEDIARIO ;;
		*) erro "índice intermediário inválido: $I_INTERMEDIARIO" ;;
		esac
	done
}

# --- aplicar -----------------------------------------------------------------

# baixar_pacote: o .tar.zst do índice em $CACHE, conferido pelo SHA256
baixar_pacote()
{
	pacote=$CACHE/$I_ARQUIVO
	if [ -f "$pacote" ] && [ "$(sha256 -q "$pacote")" = "$I_SHA256" ]; then
		return 0
	fi
	if [ -n "$MAQ" ]; then
		# O app mostra a porcentagem: o fetch roda e o tamanho é medido.
		fetch -q -r -o "$pacote.parcial" "$I_URL" 2>> "$LOG" &
		pid=$!
		while kill -0 "$pid" 2>/dev/null; do
			if [ "$I_TAMANHO" -gt 0 ] && [ -f "$pacote.parcial" ]; then
				echo "@progresso $(($(stat -f %z "$pacote.parcial") * 100 / I_TAMANHO))"
			fi
			sleep 1
		done
		wait "$pid" || erro "o download falhou; tente de novo (ele continua de onde parou)"
	else
		msg "Baixando ${NEGRITO}$I_ARQUIVO${FIM}..."
		fetch -r -o "$pacote.parcial" "$I_URL" ||
		    erro "o download falhou; tente de novo (ele continua de onde parou)"
	fi
	mv "$pacote.parcial" "$pacote"
	[ "$(sha256 -q "$pacote")" = "$I_SHA256" ] ||
	    { rm -f "$pacote"; erro "o arquivo baixado não confere com o índice (SHA256)"; }
	app progresso 100
}

# instalar_em PACOTE MONTAGEM: põe o sistema novo dentro do ambiente montado
instalar_em()
{
	pacote=$1; mnt=$2
	novo_mtree=$CACHE/sistema-novo.mtree
	: > "$CACHE/derivados.txt"
	for rel in $DERIVADOS; do echo "./$rel"; done > "$CACHE/derivados-pontos.txt"
	velho_mtree=$CACHE/sistema-velho.mtree
	bsdtar -xOf "$pacote" ./usr/share/lume/sistema.mtree > "$novo_mtree" ||
	    erro "o pacote não tem a lista do sistema"
	cp "$mnt/usr/share/lume/sistema.mtree" "$velho_mtree" 2>/dev/null ||
	    : > "$velho_mtree"

	# Configurações (tag "config" do METALOG): a sua versão fica; a nova, se
	# for diferente, vai ao lado como .lume-novo. Os scripts de rc.d,
	# periodic e defaults também têm a tag, mas são do sistema: atualizam.
	awk "$E_CONFIG"' && / type=file/ { sub(/^\.\//, "", $1); print $1 }' \
	    "$novo_mtree" > "$CACHE/config.txt"
	# Desde a 1.2, a lista traz o SHA256 de cada configuração como veio no
	# sistema: uma que ninguém mexeu é trocada pela nova.
	awk "$E_CONFIG"' && / type=file/ { for (i = 2; i <= NF; i++)
		if ($i ~ /^sha256digest=/) { sub(/^\.\//, "", $1); sub(/^sha256digest=/, "", $i); print $1, $i } }' \
	    "$velho_mtree" > "$CACHE/config-digests.txt"
	: > "$CACHE/manter.txt"
	while read -r rel; do
		[ -e "$mnt/$rel" ] || continue
		antigo=$(awk -v r="$rel" '$1 == r { print $2; exit }' "$CACHE/config-digests.txt")
		[ -n "$antigo" ] && [ "$(sha256 -q "$mnt/$rel")" = "$antigo" ] && continue
		echo "$rel" >> "$CACHE/manter.txt"
	done < "$CACHE/config.txt"
	# Bancos gerados a partir das configurações (usuários e senhas, classes
	# de login, serviços): ficam os do sistema, que batem com as suas.
	for rel in $DERIVADOS; do
		[ -e "$mnt/$rel" ] && echo "$rel" >> "$CACHE/derivados.txt"
	done

	# Arquivos protegidos (schg) do sistema antigo não deixariam o tar
	# gravar por cima; o pacote traz as proteções de volta.
	chflags -R noschg "$mnt" 2>/dev/null

	sed 's|^|./|' "$CACHE/manter.txt" > "$CACHE/excluir.txt"
	cat "$CACHE/excluir.txt" > "$CACHE/nao-extrair.txt"
	sed 's|^|./|' "$CACHE/derivados.txt" >> "$CACHE/nao-extrair.txt"
	bsdtar -xpf "$pacote" -C "$mnt" --fflags -X "$CACHE/nao-extrair.txt" \
	    >> "$LOG" 2>&1 || return 1

	# As versões novas das configurações mantidas, de uma vez só.
	rm -rf "$CACHE/configs" && mkdir -p "$CACHE/configs"
	if [ -s "$CACHE/excluir.txt" ]; then
		bsdtar -xpf "$pacote" -C "$CACHE/configs" -T "$CACHE/excluir.txt" \
		    >> "$LOG" 2>&1
	fi
	while read -r rel; do
		[ -f "$CACHE/configs/$rel" ] || continue
		cmp -s "$mnt/$rel" "$CACHE/configs/$rel" && continue
		cp -p "$CACHE/configs/$rel" "$mnt/$rel.lume-novo"
		echo "/$rel.lume-novo" >> "$CACHE/configs-novas.txt"
	done < "$CACHE/manter.txt"
	rm -rf "$CACHE/configs"

	# O que saiu do sistema na versão nova (fora as configurações) é apagado.
	awk '/ type=(file|link)/ && !('"$E_CONFIG"') { print $1 }' "$velho_mtree" |
	    grep -vxF -f "$CACHE/derivados-pontos.txt" | sort > "$CACHE/velhos.txt"
	awk '/ type=(file|link)/ { print $1 }' "$novo_mtree" | sort > "$CACHE/novos.txt"
	comm -23 "$CACHE/velhos.txt" "$CACHE/novos.txt" | while read -r rel; do
		rm -f "$mnt/${rel#./}"
	done

	# O ambiente de boot escolhido no loader tem que valer: nada de raiz fixa.
	[ -f "$mnt/boot/loader.conf" ] &&
	    sed -i '' '/^vfs\.root\.mountfrom=/d' "$mnt/boot/loader.conf"
	kldxref "$mnt/boot/kernel" 2>/dev/null
	return 0
}

# preparar_pacotes MONTAGEM TROCA: com outra base, baixa agora (no sistema
# atual, com o pkg atual) os pacotes para a base nova, para o cache do
# ambiente novo; o rc.d/lume_pacotes instala no primeiro boot dele.
preparar_pacotes()
{
	mnt=$1; modo=$2
	[ "$modo" = nenhuma ] && return 0
	[ -f /var/db/pkg/local.sqlite ] && command -v pkg >/dev/null || return 0
	# Catálogo e cache do ambiente novo, com a ABI e a versão do sistema
	# novo; os repositórios são os do sistema novo (/etc/pkg) e os seus.
	set -- -o ABI="$I_ABI" -o OSVERSION="$I_OSVERSION" \
	    -o PKG_DBDIR="$mnt/var/db/pkg" -o PKG_CACHEDIR="$mnt/var/cache/pkg" \
	    -o REPOS_DIR="$mnt/etc/pkg/,$mnt/usr/local/etc/pkg/repos/" \
	    -o ASSUME_ALWAYS_YES=yes
	echo "--- pacotes para o FreeBSD $I_BASE ($modo)" >> "$LOG"
	pkg "$@" update -f >> "$LOG" 2>&1 ||
	    { aviso "não deu para ler o catálogo de pacotes do FreeBSD $I_BASE"; return 1; }
	: > "$mnt$RECOMPILAR"
	case "$modo" in
	drivers)
		# Os pacotes donos dos módulos de /boot/modules (drm, nvidia,
		# firmwares): são compilados para um kernel exato.
		lista=$(for k in "$mnt"/boot/modules/*.ko; do
			[ -e "$k" ] || continue
			pkg -o PKG_DBDIR="$mnt/var/db/pkg" which -qo "/boot/modules/${k##*/}" 2>/dev/null
		done | grep -v 'not found' | sort -u | tr '\n' ' ')
		[ -n "$lista" ] || return 0
		# shellcheck disable=SC2086
		pkg "$@" install -F -f $lista >> "$LOG" 2>&1 ||
		    { aviso "não deu para baixar os drivers para o FreeBSD $I_BASE"; return 1; }
		;;
	programas)
		# Pacotes travados (compilados pelo forja) também voltam a ser
		# binários; a lista fica para recompilar.
		pkg -o PKG_DBDIR="$mnt/var/db/pkg" query -e '%k = 1' '%o' > "$mnt$RECOMPILAR"
		pkg -o PKG_DBDIR="$mnt/var/db/pkg" unlock -a -y >> "$LOG" 2>&1
		pkg "$@" upgrade -F -f >> "$LOG" 2>&1 ||
		    { aviso "não deu para baixar os programas para o FreeBSD $I_BASE"; return 1; }
		lista=
		;;
	esac
	cat > "$mnt$PACOTES_PEND" <<-EOF
	modo=$modo
	base=$I_BASE
	lista=$lista
	EOF
	return 0
}

cmd_aplicar()
{
	raiz
	[ -f /etc/lume-ao-vivo ] && erro "no pendrive não há o que atualizar: use lume instalar"
	raiz_zfs || erro "a atualização precisa do sistema instalado com ZFS (lume instalar)"
	[ -f "$PENDENTE" ] && erro "já há uma atualização em teste: reinicie, ou rode lume atualizar confirmar"
	mkdir -p "$CACHE" "$DB"
	rm -f "$CACHE/configs-novas.txt"
	registrar "--- aplicar"

	passos=5
	alvo=${1:-$(url_indice)}
	case "$alvo" in
	*.tar.zst)
		pacote=$alvo
		[ -f "$pacote" ] || erro "não achei $pacote"
		I_BASE=$(base_atual); I_ABI="FreeBSD:${I_BASE%%.*}:amd64"
		I_OSVERSION=$(osversion_atual)
		;;
	*)
		passo 1 $passos "Baixando a versão nova"
		resolver_indice "$alvo"
		baixar_pacote
		ok "pacote conferido (SHA256)"
		;;
	esac

	nova=$(bsdtar -xOf "$pacote" ./usr/share/lume/versao 2>/dev/null | head -1)
	echo "$nova" | grep -Eqx '[0-9]+\.[0-9]+\.[0-9]+' ||
	    erro "isto não é um pacote de atualização do LumeBSD"
	atual=$(versao_atual)
	maior "$nova" "$atual" || [ -n "${LUME_ATUALIZAR_REINSTALAR:-}" ] ||
	    erro "a versão $nova não é mais nova que a $atual"
	base=$(base_atual)
	t=$(troca "$I_BASE" "$base")
	maior "$I_BASE.0" "$base.0" || [ "$t" = nenhuma ] ||
	    erro "a $nova é de uma base FreeBSD mais velha ($I_BASE) que a sua ($base)"

	agora=$(be_agora)
	pool=$(kenv -q vfs.root.mountfrom | sed 's|^zfs:||; s|/.*||')
	# Espaço: o pacote, o sistema extraído e, trocando a base, os programas.
	livre=$(zpool list -Hp -o free "$pool")
	precisa=$(( $(stat -f %z "$pacote") * 4 ))
	case "$t" in
	drivers) precisa=$((precisa + 1073741824)) ;;
	programas) precisa=$((precisa + 6442450944)) ;;
	esac
	[ "$livre" -gt "$precisa" ] ||
	    erro "falta espaço no disco: são precisos uns $((precisa / 1073741824 + 1)) GB livres (lume tempo e bectl list mostram o que dá para apagar)"

	passo 2 $passos "Preparando um ambiente de boot novo"
	# Ambientes de boot precisam de canmount=noauto: com "on", o
	# "zfs mount -a" do boot monta o ambiente antigo por cima do novo. (O
	# instalador do LumeBSD 1.1.0 criava o default assim; isto conserta.)
	for ds in $(zfs list -H -o name -d 1 "$pool/ROOT" | tail -n +2); do
		[ "$(zfs get -H -o value canmount "$ds")" = noauto ] ||
		    zfs set canmount=noauto "$ds"
	done
	be=$(be_livre "lume-$nova")
	msg "Instalando o LumeBSD $nova no ambiente de boot ${NEGRITO}$be${FIM}"
	msg "${FRACO}(o sistema em uso, $agora, não é tocado)${FIM}"
	registrar "aplicar $atual -> $nova em $be (a partir de $agora; base $base -> $I_BASE, $t)"
	# Foto das pastas pessoais e do sistema antes (lume tempo).
	if [ -x /usr/sbin/lume-tempo ]; then
		if [ -n "$MAQ" ]; then
			/usr/sbin/lume-tempo antes "lume atualizar $atual -> $nova" > /dev/null
		else
			/usr/sbin/lume-tempo antes "lume atualizar $atual -> $nova"
		fi
	fi
	bectl create -e "$agora" "$be" || erro "não deu para criar o ambiente $be"
	mnt=$(mktemp -d /tmp/lume-atualizar.XXXXXX)
	if ! bectl mount "$be" "$mnt" >/dev/null; then
		bectl destroy -F "$be"
		erro "não deu para montar o ambiente $be"
	fi
	desfazer() { bectl umount -f "$be"; bectl destroy -F "$be"; rmdir "$mnt" 2>/dev/null; }

	passo 3 $passos "Instalando o sistema novo"
	if ! instalar_em "$pacote" "$mnt"; then
		desfazer
		erro "a instalação falhou; nada mudou (detalhes em $LOG)"
	fi

	rm -f "$mnt$PACOTES_PEND" "$mnt$PACOTES_FALHA"
	if [ "$t" != nenhuma ]; then
		passo 4 $passos "Baixando os programas para o FreeBSD $I_BASE"
		aviso "$(texto_troca "$t" "$I_BASE")"
		if ! preparar_pacotes "$mnt" "$t"; then
			desfazer
			erro "a atualização foi cancelada e nada mudou (detalhes em $LOG)"
		fi
	fi

	passo 5 $passos "Ativando para o próximo boot"
	mkdir -p "$mnt$DB"
	cat > "$mnt$PENDENTE" <<-EOF
	versao=$nova
	ambiente=$be
	anterior=$agora
	base=$I_BASE
	data=$(date '+%F %T')
	EOF
	echo "$agora" > "$mnt$ANTERIOR"
	bectl umount "$be"
	rmdir "$mnt" 2>/dev/null

	# Só para o próximo boot: se ele falhar, o seguinte volta para o atual.
	bectl activate -t "$be" >/dev/null || erro "não deu para ativar $be"
	registrar "pronto: $be ativado para o próximo boot"
	ok "LumeBSD $nova instalado em $be"
	if [ -f "$CACHE/configs-novas.txt" ]; then
		aviso "configurações suas foram mantidas; as versões novas ficaram ao lado:"
		if [ -n "$MAQ" ]; then
			sed 's/^/@config /' "$CACHE/configs-novas.txt"
		else
			sed 's/^/      /' "$CACHE/configs-novas.txt"
		fi
	fi
	app "pronto ambiente=$be anterior=$agora versao=$nova"
	[ -n "$MAQ" ] && return 0
	echo
	msg "Reinicie para entrar nele (${NEGRITO}shutdown -r now${FIM})."
	[ "$t" != nenhuma ] &&
	    msg "No primeiro boot, os programas baixados são instalados (alguns minutos)."
	msg "Se o boot falhar, o computador volta sozinho para o $agora."
	msg "Se der tudo certo, a atualização é confirmada no fim do boot."
}

# --- outros comandos ---------------------------------------------------------

cmd_verificar()
{
	atual=$(versao_atual); base=$(base_atual)
	app "atual versao=$atual base=$base"
	resolver_indice "$(url_indice)"
	if maior "$I_VERSAO" "$atual"; then
		tp=$(tipo "$I_VERSAO" "$atual")
		t=$(troca "$I_BASE" "$base")
		if [ -n "$MAQ" ]; then
			echo "@nova versao=$I_VERSAO base=$I_BASE tipo=$tp troca=$t tamanho=$I_TAMANHO notas=${I_NOTAS_URL:--}"
			return 0
		fi
		ok "$(nome_tipo "$tp"): ${NEGRITO}$I_VERSAO${FIM} (você tem a $atual)"
		[ "$t" != nenhuma ] && aviso "$(texto_troca "$t" "$I_BASE")"
		[ -n "$I_NOTAS_URL" ] && msg "${FRACO}novidades: $I_NOTAS_URL${FIM}"
		return 0
	fi
	app "em_dia versao=$atual"
	[ -n "$MAQ" ] || ok "o sistema está em dia ($atual)"
	return 1
}

# A última atualização ativada que não foi confirmada nem desfeita, e que
# não é o sistema em uso: o boot dela falhou e o computador voltou sozinho.
# Imprime "ambiente motivo".
falhou()
{
	[ -f "$LOG" ] || return 1
	raiz_zfs || return 1
	r=$(awk '
		/ pronto: .* ativado para o próximo boot/ { be = $4; motivo = "" }
		/ não confirmado: / { m = $0; sub(/^.* não confirmado: [^ ]* \(/, "", m); sub(/\)$/, "", m); motivo = m }
		/ confirmado: / && !/ não confirmado: / { if ($4 == be) be = "" }
		/ descartado: / { if ($4 == be) be = "" }
		END { if (be != "") print be, motivo }' "$LOG" 2>/dev/null)
	fbe=${r%% *}; fmotivo=
	[ "$r" != "$fbe" ] && fmotivo=${r#* }
	[ -n "$fbe" ] && [ "$fbe" != "$(be_agora)" ] && be_existe "$fbe" || return 1
	# Ativada para o próximo boot e ainda não reiniciou: não falhou.
	[ "$fbe" = "$(be_proximo)" ] && return 1
	[ -n "$fmotivo" ] || fmotivo="o boot da versão nova não chegou ao fim (travou, deu pânico ou foi desligado)"
	echo "$fbe $fmotivo"
}

cmd_estado()
{
	atual=$(versao_atual); base=$(base_atual)
	if [ -n "$MAQ" ]; then
		agora=-; padrao=-
		raiz_zfs && { agora=$(be_agora); padrao=$(be_padrao); }
		pend=-; [ -f "$PENDENTE" ] && pend=$(campo "$PENDENTE" versao)
		ant=$(cat "$ANTERIOR" 2>/dev/null); [ -n "$ant" ] && be_existe "$ant" || ant=-
		vivo=nao; [ -f /etc/lume-ao-vivo ] && vivo=sim
		echo "@estado versao=$atual base=$base ambiente=$agora padrao=$padrao pendente=$pend anterior=$ant ao_vivo=$vivo"
		raiz_zfs && prox=$(be_proximo) && [ -n "$prox" ] && [ "$prox" != "$agora" ] &&
		    echo "@reiniciar ambiente=$prox"
		f=$(falhou) && echo "@falhou ambiente=${f%% *} motivo=${f#* }"
		[ -f "$PACOTES_FALHA" ] && echo "@pacotes_falharam $(head -1 "$PACOTES_FALHA")"
		[ -s "$RECOMPILAR" ] && echo "@recompilar $(tr '\n' ' ' < "$RECOMPILAR")"
		return 0
	fi
	msg "versão: ${NEGRITO}$atual${FIM} (base FreeBSD $base)"
	if raiz_zfs; then
		msg "ambiente em uso: $(be_agora)   padrão no boot: $(be_padrao)"
	fi
	if raiz_zfs; then
		prox=$(be_proximo)
		[ -n "$prox" ] && [ "$prox" != "$(be_agora)" ] &&
		    aviso "a versão nova está instalada em $prox: reinicie para entrar nela"
	fi
	if [ -f "$PENDENTE" ]; then
		aviso "atualização em teste: $(campo "$PENDENTE" versao), ainda não confirmada"
		msg "  confirme com ${NEGRITO}lume atualizar confirmar${FIM}, ou reinicie para voltar a $(campo "$PENDENTE" anterior)"
	fi
	if f=$(falhou); then
		aviso "a atualização em ${f%% *} não deu certo, e o sistema voltou sozinho: ${f#* }"
		msg "  ${FRACO}lume atualizar descartar apaga o ambiente dela${FIM}"
	fi
	[ -s "$RECOMPILAR" ] &&
	    aviso "programas que eram compilados pelo forja voltaram a ser binários: $(tr '\n' ' ' < "$RECOMPILAR")(forja instalar NOME recompila)"
	if raiz_zfs; then
		echo
		bectl list | sed 's/^/    /'
	fi
	[ -f "$LOG" ] && { echo; msg "${FRACO}últimos registros ($LOG):${FIM}"; tail -5 "$LOG" | sed 's/^/    /'; }
}

cmd_confirmar()
{
	raiz
	[ -f "$PENDENTE" ] || { ok "nada em teste"; return 0; }
	be=$(campo "$PENDENTE" ambiente)
	[ "$(be_agora)" = "$be" ] ||
	    erro "a atualização em teste é a $be, mas o sistema em uso é $(be_agora)"
	bectl activate "$be" >/dev/null || erro "não deu para tornar $be o padrão"
	rm -f "$PENDENTE"
	registrar "confirmado: $be agora é o padrão"
	ok "LumeBSD $(versao_atual) confirmado ($be é o padrão)"
}

cmd_voltar()
{
	raiz
	raiz_zfs || erro "sem ambientes de boot (precisa do sistema instalado com ZFS)"
	if [ -f "$PENDENTE" ]; then
		ant=$(campo "$PENDENTE" anterior)
	else
		ant=$(cat "$ANTERIOR" 2>/dev/null)
	fi
	[ -n "$ant" ] && be_existe "$ant" ||
	    erro "não sei para onde voltar (veja: bectl list)"
	bectl activate "$ant" >/dev/null || erro "não deu para ativar $ant"
	registrar "voltar: $ant é o padrão de novo"
	ok "$ant é o padrão de novo. Reinicie para voltar (${NEGRITO}shutdown -r now${FIM})."
	app "pronto ambiente=$ant"
}

cmd_descartar()
{
	raiz
	f=$(falhou) || { ok "nenhuma atualização falha para descartar"; return 0; }
	be=${f%% *}
	bectl destroy -o "$be" || erro "não deu para apagar $be"
	registrar "descartado: $be"
	ok "o ambiente $be, da atualização que falhou, foi apagado"
}

cmd_limpar()
{
	raiz
	raiz_zfs || erro "sem ambientes de boot"
	agora=$(be_agora); padrao=$(be_padrao); ant=$(cat "$ANTERIOR" 2>/dev/null)
	n=0
	for be in $(bectl list -H | awk '$1 ~ /^lume-/ { print $1 }'); do
		case "$be" in "$agora"|"$padrao"|"$ant") continue ;; esac
		bectl destroy -o "$be" && n=$((n + 1)) && msg "apagado: $be"
	done
	ok "$n ambiente(s) antigo(s) apagado(s); o atual e o anterior ficaram"
}

cmd_reiniciar()
{
	raiz
	registrar "reiniciar"
	shutdown -r now
}

cmd_padrao()
{
	raiz
	cmd_verificar || return 0
	printf "\n  Instalar agora? O sistema atual fica guardado. [S/n]: "
	read -r r
	case "$r" in [nN]*) return 0 ;; esac
	cmd_aplicar "$(url_indice)"
	printf "\n  Reiniciar agora? [s/N]: "
	read -r r
	case "$r" in [sS]*) shutdown -r now ;; esac
}

case "${1:-}" in
"")		cmd_padrao ;;
verificar)	cmd_verificar ;;
aplicar)	shift; cmd_aplicar "$@" ;;
estado)		cmd_estado ;;
confirmar)	cmd_confirmar ;;
voltar)		cmd_voltar ;;
descartar)	cmd_descartar ;;
limpar)		cmd_limpar ;;
reiniciar)	cmd_reiniciar ;;
*)		echo "uso: lume atualizar [--maquina] [verificar | aplicar [ÍNDICE|ARQUIVO] | estado | confirmar | voltar | descartar | limpar | reiniciar]" >&2
		exit 2 ;;
esac

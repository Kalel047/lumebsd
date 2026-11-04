#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-atualizar - atualiza o sistema base do LumeBSD sem risco.
#
#   lume atualizar             procura, pergunta, instala e pede para reiniciar
#   lume atualizar verificar   só diz se há versão nova
#   lume atualizar aplicar [ÍNDICE|ARQUIVO]
#                              instala (de um índice .txt ou de um .tar.zst)
#   lume atualizar estado      versão, ambiente de boot, atualização em teste
#   lume atualizar confirmar   torna permanente a atualização em teste
#   lume atualizar voltar      volta para o sistema de antes da atualização
#   lume atualizar limpar      apaga ambientes de atualizações antigas
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

INDICE_PADRAO="https://github.com/Kalel047/lumebsd/releases/latest/download/atualizacao.txt"
DB=/var/db/lume
PENDENTE=$DB/atualizacao-pendente
ANTERIOR=$DB/ambiente-anterior
VERSAO_ARQ=/usr/share/lume/versao
CACHE=/var/tmp/lume-atualizar
LOG=/var/log/lume-atualizar.log

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
registrar() { echo "$(date '+%F %T') $*" >> "$LOG" 2>/dev/null; }

raiz()	{ [ "$(id -u)" -eq 0 ] || erro "precisa ser root (su -)"; }

# Gerados a partir de configurações (pwd_mkdb, cap_mkdb, services_mkdb):
# nunca vêm do pacote, para não desencontrar das configurações mantidas.
DERIVADOS="etc/pwd.db etc/spwd.db etc/passwd etc/login.conf.db var/db/services.db"

# Condição do awk para "arquivo de configuração do usuário" numa linha do
# METALOG: a tag config sozinha (não "bsdconfig"), fora dos scripts do sistema.
E_CONFIG='(/ tags=([^ ]*,)?config(,| |$)/ && $1 !~ /^\.\/etc\/(rc\.d|defaults|periodic)\//)'

# --- versões ---------------------------------------------------------------

versao_atual()
{
	v=$(head -1 "$VERSAO_ARQ" 2>/dev/null)
	# O 1.1 lançado antes deste arquivo existir é o 1.1.0.
	echo "${v:-1.1.0}"
}

# maior A B: verdadeiro se a versão A (x.y.z) é maior que B
maior()
{
	echo "$1 $2" | awk '{
		split($1, a, "."); split($2, b, ".")
		for (i = 1; i <= 3; i++) {
			if (a[i] + 0 > b[i] + 0) exit 0
			if (a[i] + 0 < b[i] + 0) exit 1
		}
		exit 1
	}'
}

# mesma_serie A B: 1.1.x só atualiza para 1.1.y (mesmo FreeBSD por baixo)
mesma_serie()
{
	[ "${1%.*}" = "${2%.*}" ]
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

be_livre()
{
	nome=$1; n=2
	while bectl list -H | awk '{print $1}' | grep -qx "$nome"; do
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
	    erro "não deu para ler o índice de atualizações:\n    $1\n    Tem internet? (cabo ou: lume wifi)"
	I_VERSAO=$(campo "$CACHE/indice.txt" versao)
	I_ARQUIVO=$(campo "$CACHE/indice.txt" arquivo)
	I_SHA256=$(campo "$CACHE/indice.txt" sha256)
	echo "$I_VERSAO" | grep -Eqx '[0-9]+\.[0-9]+\.[0-9]+' &&
	    echo "$I_ARQUIVO" | grep -Eqx '[A-Za-z0-9._-]+\.tar\.zst' &&
	    echo "$I_SHA256" | grep -Eqx '[0-9a-f]{64}' ||
	    erro "índice de atualizações inválido ($1)"
	I_URL="${1%/*}/$I_ARQUIVO"
}

# --- aplicar -----------------------------------------------------------------

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
	: > "$CACHE/manter.txt"
	while read -r rel; do
		[ -e "$mnt/$rel" ] && echo "$rel" >> "$CACHE/manter.txt"
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

cmd_aplicar()
{
	raiz
	[ -f /etc/lume-ao-vivo ] && erro "no pendrive não há o que atualizar: use lume instalar"
	raiz_zfs || erro "a atualização precisa do sistema instalado com ZFS (lume instalar)"
	[ -f "$PENDENTE" ] && erro "já há uma atualização em teste: reinicie, ou rode lume atualizar confirmar"
	mkdir -p "$CACHE" "$DB"
	rm -f "$CACHE/configs-novas.txt"

	alvo=${1:-$(url_indice)}
	case "$alvo" in
	*.tar.zst)
		pacote=$alvo
		[ -f "$pacote" ] || erro "não achei $pacote"
		;;
	*)
		baixar_indice "$alvo"
		pacote=$CACHE/$I_ARQUIVO
		if [ ! -f "$pacote" ] || [ "$(sha256 -q "$pacote")" != "$I_SHA256" ]; then
			msg "Baixando ${NEGRITO}$I_ARQUIVO${FIM}..."
			fetch -o "$pacote" "$I_URL" || erro "o download falhou"
		fi
		[ "$(sha256 -q "$pacote")" = "$I_SHA256" ] ||
		    { rm -f "$pacote"; erro "o arquivo baixado não confere com o índice (SHA256)"; }
		ok "pacote conferido (SHA256)"
		;;
	esac

	nova=$(bsdtar -xOf "$pacote" ./usr/share/lume/versao 2>/dev/null | head -1)
	echo "$nova" | grep -Eqx '[0-9]+\.[0-9]+\.[0-9]+' ||
	    erro "isto não é um pacote de atualização do LumeBSD"
	atual=$(versao_atual)
	mesma_serie "$nova" "$atual" ||
	    erro "$atual -> $nova muda a versão do FreeBSD por baixo; isso pede uma instalação nova"
	maior "$nova" "$atual" || [ -n "${LUME_ATUALIZAR_REINSTALAR:-}" ] ||
	    erro "a versão $nova não é mais nova que a $atual"

	agora=$(be_agora)
	# Ambientes de boot precisam de canmount=noauto: com "on", o
	# "zfs mount -a" do boot monta o ambiente antigo por cima do novo. (O
	# instalador do LumeBSD 1.1.0 criava o default assim; isto conserta.)
	pool=$(kenv -q vfs.root.mountfrom | sed 's|^zfs:||; s|/.*||')
	for ds in $(zfs list -H -o name -d 1 "$pool/ROOT" | tail -n +2); do
		[ "$(zfs get -H -o value canmount "$ds")" = noauto ] ||
		    zfs set canmount=noauto "$ds"
	done
	be=$(be_livre "lume-$nova")
	msg "Instalando o LumeBSD $nova no ambiente de boot ${NEGRITO}$be${FIM}"
	msg "${FRACO}(o sistema em uso, $agora, não é tocado)${FIM}"
	registrar "aplicar $atual -> $nova em $be (a partir de $agora)"
	# Foto das pastas pessoais e do sistema antes (lume tempo).
	[ -x /usr/sbin/lume-tempo ] &&
	    /usr/sbin/lume-tempo antes "lume atualizar $atual -> $nova"
	bectl create -e "$agora" "$be" || erro "não deu para criar o ambiente $be"
	mnt=$(mktemp -d /tmp/lume-atualizar.XXXXXX)
	if ! bectl mount "$be" "$mnt" >/dev/null; then
		bectl destroy -F "$be"
		erro "não deu para montar o ambiente $be"
	fi
	if ! instalar_em "$pacote" "$mnt"; then
		bectl umount -f "$be"; bectl destroy -F "$be"
		erro "a instalação falhou; nada mudou (detalhes em $LOG)"
	fi
	mkdir -p "$mnt$DB"
	cat > "$mnt$PENDENTE" <<-EOF
	versao=$nova
	ambiente=$be
	anterior=$agora
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
		sed 's/^/      /' "$CACHE/configs-novas.txt"
	fi
	echo
	msg "Reinicie para entrar nele (${NEGRITO}shutdown -r now${FIM})."
	msg "Se o boot falhar, o computador volta sozinho para o $agora."
	msg "Se der tudo certo, a atualização é confirmada no fim do boot."
}

# --- outros comandos ---------------------------------------------------------

cmd_verificar()
{
	baixar_indice "$(url_indice)"
	atual=$(versao_atual)
	if maior "$I_VERSAO" "$atual"; then
		if mesma_serie "$I_VERSAO" "$atual"; then
			ok "versão nova: ${NEGRITO}$I_VERSAO${FIM} (você tem a $atual)"
			return 0
		fi
		aviso "saiu o LumeBSD $I_VERSAO; da $atual para ela é preciso instalar de novo"
		return 1
	fi
	ok "o sistema está em dia ($atual)"
	return 1
}

cmd_estado()
{
	msg "versão: ${NEGRITO}$(versao_atual)${FIM}"
	if raiz_zfs; then
		msg "ambiente em uso: $(be_agora)   padrão no boot: $(be_padrao)"
	fi
	if [ -f "$PENDENTE" ]; then
		aviso "atualização em teste: $(campo "$PENDENTE" versao), ainda não confirmada"
		msg "  confirme com ${NEGRITO}lume atualizar confirmar${FIM}, ou reinicie para voltar a $(campo "$PENDENTE" anterior)"
	fi
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
	[ -n "$ant" ] && bectl list -H | awk '{print $1}' | grep -qx "$ant" ||
	    erro "não sei para onde voltar (veja: bectl list)"
	bectl activate "$ant" >/dev/null || erro "não deu para ativar $ant"
	registrar "voltar: $ant é o padrão de novo"
	ok "$ant é o padrão de novo. Reinicie para voltar (${NEGRITO}shutdown -r now${FIM})."
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
limpar)		cmd_limpar ;;
*)		echo "uso: lume atualizar [verificar | aplicar [ÍNDICE|ARQUIVO] | estado | confirmar | voltar | limpar]" >&2
		exit 2 ;;
esac

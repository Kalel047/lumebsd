#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-configurar - assistente de configuração do LumeBSD.
#
# Define a senha do root, cria um usuário administrador e ajusta nome da
# máquina, fuso horário, teclado e idioma. Roda no primeiro boot (via
# rc.d/lume_configurar, com --boot) e a qualquer momento com
# "lume configurar".
#

MARCA=/var/db/lume/configurado
MODO=manual
[ "$1" = "--boot" ] && MODO=boot

if [ "$(id -u)" -ne 0 ]; then
	echo "lume-configurar: precisa ser executado como root (use: su -)." >&2
	exit 1
fi

trap 'stty echo 2>/dev/null; echo; echo "  Configuração cancelada."; exit 130' INT TERM

cabecalho()
{
	echo
	echo "  ============================================================"
	echo "     LumeBSD 1.1 \"Farol\"  -  assistente de configuração"
	echo "  ============================================================"
	echo
}

secao()
{
	echo
	echo "  [$1] $2"
}

# perguntar "texto" "padrão" -> RESP
perguntar()
{
	if [ -n "$2" ]; then
		printf '      %s [%s]: ' "$1" "$2"
	else
		printf '      %s: ' "$1"
	fi
	read -r RESP || RESP=""
	[ -z "$RESP" ] && RESP=$2
}

# ler_senha "texto" -> SENHA
ler_senha()
{
	while :; do
		printf '      %s: ' "$1"
		stty -echo 2>/dev/null
		read -r _s1
		stty echo 2>/dev/null
		echo
		if [ ${#_s1} -lt 6 ]; then
			echo "      A senha precisa ter pelo menos 6 caracteres."
			continue
		fi
		printf '      Repita a senha: '
		stty -echo 2>/dev/null
		read -r _s2
		stty echo 2>/dev/null
		echo
		[ "$_s1" = "$_s2" ] && break
		echo "      As senhas não conferem. Tente de novo."
	done
	SENHA=$_s1
}

coletar()
{
	secao 1/6 "Senha do administrador (root)"
	ler_senha "Nova senha do root"
	SENHA_ROOT=$SENHA

	secao 2/6 "Seu usuário"
	echo "      Login em minúsculas (ex.: fabiano). Deixe vazio para pular."
	while :; do
		perguntar "Login" ""
		USUARIO=$RESP
		[ -z "$USUARIO" ] && break
		if ! echo "$USUARIO" | grep -Eq '^[a-z_][a-z0-9_-]{0,15}$'; then
			echo "      Use até 16 letras minúsculas, números, - ou _."
		elif id "$USUARIO" >/dev/null 2>&1; then
			echo "      O usuário '$USUARIO' já existe."
		else
			break
		fi
	done
	if [ -n "$USUARIO" ]; then
		perguntar "Nome completo" "$USUARIO"
		NOME=$(echo "$RESP" | tr -d ':')
		ler_senha "Senha de $USUARIO"
		SENHA_USUARIO=$SENHA
	fi

	secao 3/6 "Nome desta máquina"
	while :; do
		perguntar "Nome" "$(hostname)"
		MAQUINA=$RESP
		echo "$MAQUINA" | grep -Eq '^[A-Za-z0-9]([A-Za-z0-9.-]{0,62})$' && break
		echo "      Use letras, números, ponto e hífen."
	done

	secao 4/6 "Fuso horário"
	echo "      Exemplos: America/Sao_Paulo (Brasília), America/Manaus,"
	echo "      America/Rio_Branco, America/Noronha, Europe/Lisbon, UTC"
	while :; do
		perguntar "Fuso" "America/Sao_Paulo"
		FUSO=$RESP
		case "$FUSO" in
		*..*|/*) ;;
		*)	[ -f "/usr/share/zoneinfo/$FUSO" ] && break ;;
		esac
		echo "      Fuso '$FUSO' não encontrado em /usr/share/zoneinfo."
	done

	secao 5/6 "Teclado"
	echo "      1) ABNT2 (Brasil)   2) US internacional   3) US   4) Portugal"
	while :; do
		perguntar "Opção" "1"
		case "$RESP" in
		1) TECLADO=br;     TECLADO_NOME="ABNT2 (Brasil)"; break ;;
		2) TECLADO=us.acc; TECLADO_NOME="US internacional"; break ;;
		3) TECLADO=us;     TECLADO_NOME="US"; break ;;
		4) TECLADO=pt;     TECLADO_NOME="Portugal"; break ;;
		esac
		echo "      Escolha de 1 a 4."
	done

	secao 6/6 "Idioma do sistema"
	echo "      1) Português (Brasil)   2) English (US)"
	while :; do
		perguntar "Opção" "1"
		case "$RESP" in
		1) IDIOMA=pt_BR.UTF-8; break ;;
		2) IDIOMA=en_US.UTF-8; break ;;
		esac
		echo "      Escolha 1 ou 2."
	done
}

resumo()
{
	echo
	echo "  Resumo"
	echo "  ------------------------------------------------------------"
	echo "      root           senha definida"
	if [ -n "$USUARIO" ]; then
		echo "      usuário        $USUARIO ($NOME), administrador (wheel)"
	else
		echo "      usuário        nenhum"
	fi
	echo "      máquina        $MAQUINA"
	echo "      fuso           $FUSO"
	echo "      teclado        $TECLADO_NOME"
	echo "      idioma         $IDIOMA"
	echo "  ------------------------------------------------------------"
}

aplicar()
{
	echo
	printf '      senha do root ......... '
	printf '%s\n' "$SENHA_ROOT" | pw usermod root -h 0 && echo ok

	if [ -n "$USUARIO" ]; then
		printf '      usuário %s ' "$USUARIO"
		# wheel: su -; operator: desligar e montar; video: aceleração.
		grupos=wheel,operator,video
		pw groupshow webcamd >/dev/null 2>&1 && grupos=$grupos,webcamd
		printf '%s\n' "$SENHA_USUARIO" | pw useradd -n "$USUARIO" \
		    -c "$NOME" -m -G "$grupos" -s /bin/sh -h 0 && echo "......... ok"
	fi

	printf '      nome da máquina ....... '
	sysrc -q hostname="$MAQUINA" >/dev/null && hostname "$MAQUINA" && echo ok

	printf '      fuso horário .......... '
	(cd / && tzsetup "$FUSO") >/dev/null 2>&1 && echo ok || echo falhou

	printf '      teclado ............... '
	sysrc -q keymap="$TECLADO" >/dev/null
	if [ -c /dev/ttyv0 ]; then
		kbdcontrol -l "$TECLADO" < /dev/ttyv0 >/dev/null 2>&1
	fi
	# O mesmo teclado no modo gráfico (tela de login e KDE).
	case "$TECLADO" in
	br)	k=br; v=abnt2 ;;
	us.acc)	k=us; v=intl ;;
	pt)	k=pt; v= ;;
	*)	k=us; v= ;;
	esac
	xkb="\"$k\""; xvar="\"$v\""
	mkdir -p /usr/local/etc/X11/xorg.conf.d
	cat > /usr/local/etc/X11/xorg.conf.d/00-lume-teclado.conf <<-EOF
	# Gerado pelo lume configurar.
	Section "InputClass"
	    Identifier "Teclado do LumeBSD"
	    MatchIsKeyboard "on"
	    Option "XkbLayout" $xkb
	    Option "XkbVariant" $xvar
	EndSection
	EOF
	# A tela de login (o Xsetup do SDDM usaria "us,<idioma>") e o KDE.
	sysrc -q sddm_xkb_layout="$k${v:+ -variant $v}" >/dev/null
	mkdir -p /usr/local/etc/xdg
	cat > /usr/local/etc/xdg/kxkbrc <<-EOF
	[Layout]
	LayoutList=$k
	VariantList=$v
	Use=true
	EOF
	echo ok

	printf '      idioma ................ '
	awk -v l="$IDIOMA" '
		/^default:/ { d = 1 }
		d && /:lang=/ { sub(/:lang=[^:]*:/, ":lang=" l ":") }
		d && !/\\$/ { d = 0 }
		{ print }
	' /etc/login.conf > /etc/login.conf.lume &&
	    mv /etc/login.conf.lume /etc/login.conf &&
	    cap_mkdb /etc/login.conf && echo ok
	# Tela de login gráfica no mesmo idioma.
	sysrc -q sddm_lang="${IDIOMA%.UTF-8}" >/dev/null

	# O syslogd guarda nome e fuso de quando iniciou; reinicia para usar
	# os novos.
	if service syslogd onestatus >/dev/null 2>&1; then
		service syslogd restart >/dev/null 2>&1
	fi

	mkdir -p "$(dirname "$MARCA")"
	date > "$MARCA"

	SENHA=; SENHA_ROOT=; SENHA_USUARIO=; _s1=; _s2=
}

# --- início ---------------------------------------------------------------

if [ "$MODO" = boot ]; then
	[ -e "$MARCA" ] && exit 0
	[ -t 0 ] || exit 0
	cabecalho
	echo "  Este é o primeiro boot. Vamos deixar o sistema pronto:"
	echo "  senha, seu usuário, nome da máquina, fuso, teclado e idioma."
	echo
	# Com o desktop instalado não há como entrar sem um usuário: espera
	# o tempo que for. Sem desktop (servidor, VM), pula em 120 s.
	if [ "$(sysrc -n sddm_enable 2>/dev/null)" = YES ]; then
		echo "  Pressione Enter para começar."
		espera=
	else
		echo "  Pressione Enter para começar (pula sozinho em 120 s)."
		espera="-t 120"
	fi
	if ! read $espera -r _; then
		echo
		echo "  Configuração adiada. Rode 'lume configurar' como root."
		exit 0
	fi
else
	cabecalho
fi

while :; do
	coletar
	resumo
	perguntar "Aplicar estas configurações? (s/n)" "s"
	case "$RESP" in
	[sS]*) break ;;
	esac
	echo "      Tudo bem, vamos de novo."
done

aplicar

echo
if [ -n "$USUARIO" ]; then
	echo "  Pronto! Entre como '$USUARIO' e use 'su -' para administrar."
else
	echo "  Pronto! O sistema está configurado."
fi
echo "  Para refazer depois: lume configurar"
echo
exit 0

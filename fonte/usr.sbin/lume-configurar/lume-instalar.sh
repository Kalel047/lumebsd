#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-instalar - instala o LumeBSD num disco inteiro, a partir do pendrive.
#
# Layout (o mesmo do instalador do FreeBSD com ZFS + criptografia):
#   p1  efi          260 MB  FAT32, carregador UEFI
#   p2  swap           8 GB  criptografada com chave descartável a cada boot
#   p3  zfs         o resto  GELI (AES-256-XTS) pedindo a senha no boot,
#                            pool "zroot" com ambientes de boot (bectl)
#
# Variáveis para testes automatizados (todas opcionais):
#   LUME_DISCO, LUME_SENHA_DISCO, LUME_CONFIRMA=APAGAR, LUME_DESKTOP=s|n,
#   LUME_SWAP (padrão 8g), LUME_CONSOLE_SERIAL=1 (console também na serial),
#   LUME_REPO_URL (espelho de pacotes, veja lume-desktop),
#   LUME_KIT=geral|exatas|sociais|"" (os programas da área; vazio: nenhum)
#

# Lista oficial dos arquivos do sistema (METALOG do build), com dono,
# permissões e flags. O instalador copia exatamente esses arquivos do
# pendrive para o disco, então o sistema instalado sai limpo.
SISTEMA=/usr/share/lume/sistema.mtree
ALVO=/mnt/lume
POOL=zroot
SWAP=${LUME_SWAP:-8g}
LOG=/tmp/lume-instalar.log

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	VERMELHO='\033[31;1m'; VERDE='\033[32;1m'; NEGRITO='\033[1m'; FIM='\033[0m'
else
	VERMELHO=; VERDE=; NEGRITO=; FIM=
fi

morrer()
{
	printf "\n  ${VERMELHO}✗ %s${FIM}\n" "$1"
	echo "    Detalhes em $LOG"
	stty echo 2>/dev/null
	exit 1
}

passo()
{
	printf "  • %s..." "$1"
}

feito()
{
	printf " ok\n"
}

roda()
{
	echo "+ $*" >> "$LOG"
	"$@" >> "$LOG" 2>&1
}

[ "$(id -u)" -eq 0 ] || { echo "lume instalar: precisa ser root" >&2; exit 1; }
[ -f "$SISTEMA" ] && [ -f /etc/lume-ao-vivo ] ||
    morrer "lume instalar roda a partir do pendrive do LumeBSD"
: > "$LOG"

printf "\n  ${NEGRITO}LumeBSD 1.3 \"Farol\" - instalação${FIM}\n\n"

# --- 1. escolher o disco ------------------------------------------------

# O disco do próprio pendrive fica de fora.
vivo=$(glabel status 2>/dev/null | awk '$1 == "gpt/lumeroot" {print $3}' |
    sed -E 's/p[0-9]+$//')
discos=
for d in $(sysctl -n kern.disks); do
	case "$d" in cd*) continue ;; esac
	[ "$d" = "$vivo" ] && continue
	discos="$discos $d"
done
discos=${discos# }
[ -n "$discos" ] || morrer "nenhum disco para instalar (o pendrive não conta)"

descrever()
{
	desc=$(geom disk list "$1" | awk -F': ' '/descr:/ {print $2; exit}')
	bytes=$(geom disk list "$1" | awk '/Mediasize:/ {print $2; exit}')
	printf "%s - %s, %d GB" "$1" "${desc:-sem nome}" "$((bytes / 1000000000))"
}

if [ -n "${LUME_DISCO:-}" ]; then
	disco=$LUME_DISCO
else
	echo "  Discos encontrados:"
	n=0
	for d in $discos; do
		n=$((n + 1))
		printf "    %d) %s\n" "$n" "$(descrever "$d")"
	done
	printf "  Número do disco onde instalar [1]: "
	read -r esc
	disco=$(echo "$discos" | tr ' ' '\n' | sed -n "${esc:-1}p")
fi
case " $discos " in
*" $disco "*) ;;
*) morrer "disco inválido: $disco" ;;
esac

# Partições que já existem, para o aviso.
existentes=$(gpart show -l "$disco" 2>/dev/null | awk 'NR > 1 && $4 != "-" &&
    $3 ~ /^[0-9]+$/ { printf "%s ", $4 }')

printf "\n  ${VERMELHO}${NEGRITO}ATENÇÃO: TUDO em %s será apagado.${FIM}\n" \
    "$(descrever "$disco")"
[ -n "$existentes" ] && printf "  Partições atuais: %s\n" "$existentes"
echo "  Isso inclui o Windows e todos os arquivos desse disco."
printf "  Para continuar, digite APAGAR: "
if [ -n "${LUME_CONFIRMA:-}" ]; then
	conf=$LUME_CONFIRMA; echo "(automático)"
else
	read -r conf
fi
[ "$conf" = "APAGAR" ] || { echo "  Nada foi alterado."; exit 1; }

# --- 2. senha do disco --------------------------------------------------

echo
echo "  O disco será criptografado. A senha é pedida toda vez que o"
echo "  computador ligar, antes do sistema. Sem ela, os dados não voltam."
if [ -n "${LUME_SENHA_DISCO:-}" ]; then
	senha=$LUME_SENHA_DISCO
else
	while :; do
		printf "  Senha do disco (mínimo 8 caracteres): "
		stty -echo; read -r senha; stty echo; echo
		[ ${#senha} -ge 8 ] || { echo "  Curta demais."; continue; }
		printf "  Repita a senha: "
		stty -echo; read -r senha2; stty echo; echo
		[ "$senha" = "$senha2" ] && break
		echo "  As senhas não conferem."
	done
	senha2=
fi

# --- 3. desktop -----------------------------------------------------------

internet=n
fetch -q -T 8 -o /dev/null https://pkg.freebsd.org/ 2>/dev/null && internet=s
if [ -n "${LUME_DESKTOP:-}" ]; then
	desktop=$LUME_DESKTOP
elif [ "$internet" = s ]; then
	printf "\n  Instalar o desktop KDE Plasma agora (baixa ~1,8 GB)? [S/n]: "
	read -r r
	case "$r" in [nN]*) desktop=n ;; *) desktop=s ;; esac
else
	echo
	echo "  Sem internet agora: o desktop pode ser instalado depois com"
	echo "  'lume desktop' (cabo de rede ou 'lume wifi')."
	desktop=n
fi

# Os programas da área de quem vai usar (lume loja kit), junto com o desktop.
kit=
if [ "$desktop" = s ]; then
	if [ -n "${LUME_KIT+x}" ]; then
		kit=$LUME_KIT
	else
		echo
		echo "  Para que você vai usar o computador? Os programas da área vêm junto."
		echo "    1) Uso geral: escritório em português, vídeo, apps da web (~400 MB)"
		echo "    2) Exatas e engenharia: Octave (como o MATLAB), Python, GeoGebra (~850 MB)"
		echo "    3) Sociais aplicadas: R, estatística, Zotero (~1,2 GB)"
		echo "    4) Só o desktop; escolho depois no app Programas do LumeBSD"
		printf "  Opção [1]: "
		read -r r
		case "$r" in 2) kit=exatas ;; 3) kit=sociais ;; 4) kit= ;; *) kit=geral ;; esac
	fi
fi

# --- 4. instalar ----------------------------------------------------------

echo
passo "Preparando o disco $disco"
for m in zfs aesni geom_eli; do kldload -n $m >> "$LOG" 2>&1; done
# Sem ZFS e GELI não há instalação: confere antes de mexer no disco.
for m in zfs geom_eli; do
	kldstat -q -n $m || morrer "o módulo $m do kernel não carregou (nada no disco foi alterado)"
done
zpool export -f "$POOL" >> "$LOG" 2>&1
for g in $(geli status -s 2>/dev/null | awk '{print $1}'); do
	case "$g" in gpt/zfs0.eli|${disco}p*) geli detach -f "$g" >> "$LOG" 2>&1 ;; esac
done
gpart destroy -F "$disco" >> "$LOG" 2>&1
roda gpart create -s gpt "$disco" || morrer "não deu para criar a tabela GPT"
roda gpart add -a 1m -s 260m -t efi -l efiboot0 "$disco" &&
roda gpart add -a 1m -s "$SWAP" -t freebsd-swap -l swap0 "$disco" &&
roda gpart add -a 1m -t freebsd-zfs -l zfs0 "$disco" ||
    morrer "não deu para criar as partições"
roda newfs_msdos -F 32 -c 1 -L EFISYS "/dev/gpt/efiboot0" ||
    morrer "não deu para formatar a partição EFI"
feito

passo "Criptografando (AES-256-XTS)"
printf '%s' "$senha" | roda geli init -bg -e AES-XTS -J - -l 256 -s 4096 \
    /dev/gpt/zfs0 || morrer "geli init falhou"
printf '%s' "$senha" | roda geli attach -j - /dev/gpt/zfs0 ||
    morrer "geli attach falhou"
senha=
feito

passo "Criando o ZFS"
mkdir -p "$ALVO"
roda zpool create -f -o altroot="$ALVO" -O compress=zstd -O atime=off \
    -O mountpoint=none "$POOL" /dev/gpt/zfs0.eli || morrer "zpool create falhou"
roda zfs create -o mountpoint=none -o lume:tempo=on "$POOL/ROOT" &&
# canmount=noauto: cada ambiente de boot só é montado quando é o escolhido
# no boot (com "on", o zfs mount -a montaria um por cima do outro).
roda zfs create -o mountpoint=/ -o canmount=noauto "$POOL/ROOT/default" &&
roda zfs mount "$POOL/ROOT/default" &&
roda zfs create -o mountpoint=/home -o lume:tempo=on "$POOL/home" &&
roda zfs create -o mountpoint=/tmp -o exec=on -o setuid=off "$POOL/tmp" &&
roda zfs create -o mountpoint=/usr -o canmount=off "$POOL/usr" &&
roda zfs create "$POOL/usr/src" &&
roda zfs create -o setuid=off "$POOL/usr/ports" &&
roda zfs create -o mountpoint=/var -o canmount=off "$POOL/var" &&
roda zfs create -o exec=off -o setuid=off "$POOL/var/audit" &&
roda zfs create -o exec=off -o setuid=off "$POOL/var/crash" &&
roda zfs create -o exec=off -o setuid=off "$POOL/var/log" &&
roda zfs create -o atime=on "$POOL/var/mail" &&
roda zfs create -o setuid=off "$POOL/var/tmp" ||
    morrer "não deu para criar os datasets"
roda zpool set bootfs="$POOL/ROOT/default" "$POOL"
feito

passo "Copiando o sistema"
echo "+ tar @$SISTEMA" >> "$LOG"
tar -cf - -C / "@$SISTEMA" 2>> "$LOG" | tar -xpf - -C "$ALVO" 2>> "$LOG" ||
    morrer "falha ao copiar o sistema"
# O pendrive mostra instruções próprias no login; o sistema instalado não.
cp /usr/share/lume/motd.template "$ALVO/etc/motd.template"
chmod 1777 "$ALVO/tmp" "$ALVO/var/tmp"
mkdir -p "$ALVO/boot/efi"
feito

passo "Configurando o boot UEFI"
mkdir -p /tmp/lume-efi
roda mount_msdosfs /dev/gpt/efiboot0 /tmp/lume-efi || morrer "mount EFI"
mkdir -p /tmp/lume-efi/EFI/BOOT /tmp/lume-efi/EFI/LumeBSD
cp "$ALVO/boot/loader.efi" /tmp/lume-efi/EFI/BOOT/BOOTX64.EFI &&
cp "$ALVO/boot/loader.efi" /tmp/lume-efi/EFI/LumeBSD/loader.efi ||
    morrer "não deu para copiar o loader.efi"
roda efibootmgr -a -c -l /tmp/lume-efi/EFI/LumeBSD/loader.efi -L LumeBSD
roda umount /tmp/lume-efi

cat > "$ALVO/etc/fstab" <<EOF
# Dispositivo		Ponto	Tipo	Opções	Dump	Pass
/dev/gpt/efiboot0	/boot/efi	msdosfs	rw	2	2
/dev/gpt/swap0.eli	none	swap	sw	0	0
fdesc			/dev/fd	fdescfs	rw	0	0
proc			/proc	procfs	rw	0	0
EOF
cat > "$ALVO/boot/loader.conf" <<EOF
# LumeBSD 1.3 - notebook
zfs_load="YES"
aesni_load="YES"
geom_eli_load="YES"
kern.geom.label.disk_ident.enable="0"
kern.geom.label.gptid.enable="0"
autoboot_delay="3"
# Microcódigo da CPU (pacote cpu-microcode-intel)
cpu_microcode_load="YES"
cpu_microcode_name="/boot/firmware/intel-ucode.bin"
# Wi-Fi Intel: liga 802.11n/ac e a economia de energia
compat.linuxkpi.iwlwifi_11n_disable="0"
compat.linuxkpi.iwlwifi_disable_11ac="0"
compat.linuxkpi.iwlwifi_power_save="1"
EOF
if [ -n "${LUME_CONSOLE_SERIAL:-}" ]; then
	printf 'boot_multicons="YES"\nboot_serial="YES"\nconsole="comconsole,efi"\n' >> "$ALVO/boot/loader.conf"
fi
# Programas que caem não deixam arquivos .core na pasta do usuário;
# só os do root vão para /var/crash.
echo 'kern.corefile=/var/crash/%N.%P.core' >> "$ALVO/etc/sysctl.conf"
# Ports usados direto (sem a forja): a versão do FreeBSD por baixo.
cat >> "$ALVO/etc/make.conf" <<'EOF'
# LumeBSD: para a árvore de ports, o sistema é o FreeBSD por baixo. O uname
# diz "LumeBSD 1.3-FAROL", que o configure dos programas não conhece; a
# versão vem do kern.osreldate (1501000 -> 15.1-RELEASE).
.if !defined(_OSRELEASE)
_LUME_OSREL!=	/sbin/sysctl -n kern.osreldate
_OSRELEASE=	${_LUME_OSREL:C/^([0-9]+)([0-9][0-9])[0-9][0-9][0-9]$/\1.\2/:C/\.0([0-9])$/.\1/}-RELEASE
.endif
OPSYS?=		FreeBSD
UNAME_s?=	FreeBSD
UNAME_r?=	${_OSRELEASE}
EOF
cat > "$ALVO/etc/rc.conf" <<EOF
hostname="farol"
zfs_enable="YES"
ifconfig_DEFAULT="DHCP"
dumpdev="AUTO"
powerd_enable="YES"
powerd_flags="-a hiadaptive -b adaptive"
performance_cx_lowest="Cmax"
economy_cx_lowest="Cmax"
kld_list="acpi_ibm coretemp cpuctl"
devmatch_blocklist="if_iwx if_iwm"
microcode_update_enable="YES"
lume_farol_enable="YES"
lume_configurar_enable="YES"
lume_notebook_enable="YES"
lume_video_enable="YES"
lume_video="auto"
vigia_enable="YES"
sshd_enable="NO"
EOF
# Wi-Fi configurado no pendrive vai junto.
if [ -f /etc/wpa_supplicant.conf ] && grep -q network= /etc/wpa_supplicant.conf; then
	cp /etc/wpa_supplicant.conf "$ALVO/etc/" && chmod 600 "$ALVO/etc/wpa_supplicant.conf"
	for v in $(sysrc -a 2>/dev/null | awk -F: '/^wlans_|^ifconfig_wlan0/ {print $1}'); do
		sysrc -f "$ALVO/etc/rc.conf" -q "$v=$(sysrc -n "$v")" >/dev/null
	done
fi
# O resultado do teste de vídeo feito no pendrive vai junto.
mkdir -p "$ALVO/var/db/lume"
[ -f /var/db/lume/video-estado ] && cp /var/db/lume/video-estado "$ALVO/var/db/lume/"
feito

passo "Instalando drivers e firmwares do pendrive"
mount -t devfs devfs "$ALVO/dev"
cp /etc/resolv.conf "$ALVO/etc/" 2>/dev/null
mkdir -p "$ALVO/usr/share/lume/pacotes"
cp /usr/share/lume/pacotes/*.pkg "$ALVO/usr/share/lume/pacotes/" 2>/dev/null
pk=$(ls "$ALVO"/usr/share/lume/pacotes/pkg-*.pkg 2>/dev/null | head -1)
eval "$(/usr/sbin/lume-video detectar)"
if [ -n "$pk" ]; then
	roda chroot "$ALVO" env ASSUME_ALWAYS_YES=yes pkg add "${pk#$ALVO}"
	for p in "$ALVO"/usr/share/lume/pacotes/*.pkg; do
		case "$p" in */pkg-[0-9]*) continue ;; esac
		# O driver NVIDIA do pendrive é o atual; placas antigas recebem
		# outra geração pelo lume desktop (os dois não convivem).
		case "$p" in */nvidia-kmod-[0-9]*)
			[ "$escolha" = nvidia ] &&
			    [ "${nvidia_pacote:-}" = nvidia-driver ] || continue ;;
		esac
		roda chroot "$ALVO" env ASSUME_ALWAYS_YES=yes pkg add -M "${p#$ALVO}"
	done
fi
feito

if [ "$desktop" = s ]; then
	echo "  • Instalando o desktop (pode levar vários minutos)..."
	chroot "$ALVO" env LUME_REPO_URL="${LUME_REPO_URL:-}" \
	    /usr/sbin/lume-desktop 2>&1 | tee -a "$LOG" |
	    sed 's/^/    /' || echo "    (o desktop pode ser instalado depois com: lume desktop)"
	if [ -n "$kit" ] && [ -x "$ALVO/usr/local/bin/plasmashell" ]; then
		echo "  • Instalando os programas do kit $kit..."
		chroot "$ALVO" env LUME_SEM_FOTO=1 /usr/sbin/lume-loja kit "$kit" 2>&1 |
		    tee -a "$LOG" | sed 's/^/    /' ||
		    echo "    (o kit pode ser instalado depois no app Programas do LumeBSD)"
	fi
fi

passo "Finalizando"
umount "$ALVO/dev" >> "$LOG" 2>&1
roda zfs umount -a
roda zpool export "$POOL"
roda geli detach /dev/gpt/zfs0.eli
feito

printf "\n  ${VERDE}✓ LumeBSD instalado em %s.${FIM}\n" "$disco"
echo "  Tire o pendrive e reinicie (shutdown -r now)."
echo "  No boot: digite a senha do disco; depois o assistente pede a senha"
echo "  do root e cria o seu usuário."
echo

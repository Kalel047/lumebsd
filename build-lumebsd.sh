#!/bin/sh
# Constrói o LumeBSD 1.0 "Farol" a partir do código-fonte do FreeBSD 15.0.
# Roda em Linux (cross-build) ou FreeBSD. Uso: sh build-lumebsd.sh
set -e

TOP=$(pwd)
SRC=$TOP/src
OBJ=$TOP/obj
STAGE=$TOP/stage
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}

# 1. Código-fonte + identidade LumeBSD
[ -d "$SRC" ] || git clone --depth 1 --branch releng/15.0 \
    https://github.com/freebsd/freebsd-src.git "$SRC"
(cd "$SRC" && git apply --check "$TOP/patches/0001-lumebsd-1.0-farol.patch" 2>/dev/null &&
    git apply "$TOP/patches/0001-lumebsd-1.0-farol.patch") || true
# src.conf enxuto: veja o arquivo src.conf ao lado deste script.

mkdir -p "$OBJ" "$STAGE"
MK="$SRC/tools/build/make.py --cross-bindir=${LLVM_BIN:-/usr/lib/llvm-18/bin} --host-bindir=/usr/bin \
    -j$JOBS SRCCONF=$TOP/conf/src.conf TARGET=amd64 TARGET_ARCH=amd64"
export MAKEOBJDIRPREFIX=$OBJ

# 2. Userland e kernel
$MK buildworld
$MK buildkernel KERNCONF=LUME

# 3. Instalação em diretório (sem root, gera METALOG)
$MK installworld distribution installkernel KERNCONF=LUME \
    -DNO_ROOT DESTDIR="$STAGE"

# 4. Configuração padrão da imagem
cat > "$STAGE/etc/rc.conf" <<EOF
hostname="farol"
ifconfig_DEFAULT="DHCP"
lume_farol_enable="YES"
EOF
cat > "$STAGE/etc/fstab" <<EOF
/dev/gpt/lumeroot  /  ufs  rw  1  1
EOF
cat > "$STAGE/boot/loader.conf" <<EOF
boot_multicons="YES"
console="comconsole,vidconsole"
autoboot_delay="5"
EOF
cat >> "$STAGE/METALOG" <<EOF
./etc/rc.conf type=file uname=root gname=wheel mode=0644
./etc/fstab type=file uname=root gname=wheel mode=0644
./boot/loader.conf type=file uname=root gname=wheel mode=0644
EOF

# 5. Imagem de disco GPT bootável (BIOS + UEFI)
TOOLS=$(ls -d "$OBJ"/*/amd64.amd64/tmp/legacy/bin | head -1)
"$TOOLS/makefs" -t ffs -B little -o version=2,label=lumeroot -s 2g \
    -M 2g -F "$STAGE/METALOG" -N "$STAGE/etc" "$TOP/root.ufs" "$STAGE"
mkdir -p "$TOP/efi/EFI/BOOT"
cp "$STAGE/boot/loader.efi" "$TOP/efi/EFI/BOOT/BOOTX64.EFI"
"$TOOLS/makefs" -t msdos -o fat_type=32,sectors_per_cluster=1 -s 64m \
    "$TOP/efi.fat" "$TOP/efi"
"$TOOLS/mkimg" -s gpt -b "$STAGE/boot/pmbr" \
    -p freebsd-boot/bootfs:="$STAGE/boot/gptboot" \
    -p efi:="$TOP/efi.fat" \
    -p freebsd-ufs/lumeroot:="$TOP/root.ufs" \
    -o "$TOP/LumeBSD-1.0-Farol-amd64.img"

echo "Pronto: $TOP/LumeBSD-1.0-Farol-amd64.img"
echo "Teste: qemu-system-x86_64 -m 1024 -drive file=LumeBSD-1.0-Farol-amd64.img,format=raw -nographic"

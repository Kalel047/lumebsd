#!/bin/sh
# Constrói o LumeBSD 1.0 "Farol" a partir do código-fonte do FreeBSD 15.0.
# Roda em Linux (cross-build com LLVM) ou FreeBSD. Uso: sh build-lumebsd.sh
set -e

TOP=$(pwd)
SRC=$TOP/src
OBJ=$TOP/obj
STAGE=$TOP/stage
IMG=$TOP/LumeBSD-1.0-Farol-amd64.img
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}
MODULES="tmpfs nullfs fusefs if_bridge if_epair pf pflog acpi/acpi_video"

# 1. Código-fonte do FreeBSD + identidade LumeBSD
[ -d "$SRC" ] || git clone --depth 1 --branch releng/15.0 \
    https://github.com/freebsd/freebsd-src.git "$SRC"
if [ ! -d "$SRC/usr.bin/lume" ]; then
	(cd "$SRC" && git apply "$TOP/patches/0001-lumebsd-1.0-farol.patch")
fi

mkdir -p "$OBJ"
export MAKEOBJDIRPREFIX=$OBJ
mk() {
	"$SRC/tools/build/make.py" \
	    --cross-bindir="${LLVM_BIN:-/usr/lib/llvm-18/bin}" \
	    --host-bindir="${HOST_BIN:-/usr/bin}" -j"$JOBS" \
	    SRCCONF="$TOP/conf/src.conf" TARGET=amd64 TARGET_ARCH=amd64 \
	    KERNCONF=LUME MODULES_OVERRIDE="$MODULES" "$@"
}

# 2. Userland e kernel
mk buildworld
mk buildkernel

# 3. Instalação em diretório de staging, sem root (gera METALOG)
rm -rf "$STAGE" && mkdir -p "$STAGE"
mk -DNO_ROOT DESTDIR="$STAGE" installworld distribution installkernel

# 4. Configuração padrão da imagem
cat > "$STAGE/etc/rc.conf" <<EOF
hostname="farol"
ifconfig_DEFAULT="DHCP"
lume_farol_enable="YES"
dumpdev="NO"
EOF
printf '/dev/gpt/lumeroot\t/\tufs\trw\t1\t1\n' > "$STAGE/etc/fstab"
cat > "$STAGE/boot/loader.conf" <<EOF
boot_multicons="YES"
console="comconsole,vidconsole"
autoboot_delay="3"
EOF

# METALOG: registra os arquivos acima e cria entradas para diretórios
# que o installworld deixou sem registro (ex.: /usr/share/pkg).
python3 - "$STAGE/METALOG" <<'PY'
import sys
p = sys.argv[1]
extra = {"./etc/rc.conf", "./etc/fstab", "./boot/loader.conf"}
L = [l for l in open(p).read().splitlines() if l.split()[:1] not in [[e] for e in extra]]
L += [f"{e} type=file uname=root gname=wheel mode=0644" for e in sorted(extra)]
seen = {l.split()[0] for l in L if l.startswith(".")}
out = []
for l in L:
    if l.startswith("./"):
        parts = l.split()[0].split("/")
        for i in range(2, len(parts)):
            d = "/".join(parts[:i])
            if d not in seen:
                seen.add(d)
                out.append(f"{d} type=dir uname=root gname=wheel mode=0755")
    out.append(l)
open(p, "w").write("\n".join(out) + "\n")
PY

# 5. Imagem de disco GPT bootável (BIOS + UEFI)
TOOLS=$(ls -d "$OBJ"/*/amd64.amd64/tmp/legacy/bin | head -1)
rm -f "$TOP/root.ufs" "$TOP/efi.fat" "$IMG"
(cd "$STAGE" && "$TOOLS/makefs" -t ffs -B little \
    -o version=2,label=lumeroot,softupdates=1 -s 3g \
    -F METALOG -N etc "$TOP/root.ufs" .)
rm -rf "$TOP/efi" && mkdir -p "$TOP/efi/EFI/BOOT"
cp "$STAGE/boot/loader.efi" "$TOP/efi/EFI/BOOT/BOOTX64.EFI"
"$TOOLS/makefs" -t msdos -o fat_type=32,sectors_per_cluster=1,volume_label=EFISYS \
    -s 64m "$TOP/efi.fat" "$TOP/efi"
"$TOOLS/mkimg" -s gpt -f raw -b "$STAGE/boot/pmbr" \
    -p freebsd-boot/bootfs:="$STAGE/boot/gptboot" \
    -p efi/efiboot:="$TOP/efi.fat" \
    -p freebsd-swap/swap::512M \
    -p freebsd-ufs/lumeroot:="$TOP/root.ufs" \
    -o "$IMG"

echo "Pronto: $IMG"
echo "Teste: qemu-system-x86_64 -m 1024 -nographic -drive file=$(basename "$IMG"),format=raw,if=virtio"

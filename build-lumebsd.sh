#!/bin/sh
# Constrói o pendrive do LumeBSD 1.1 "Farol" (ao vivo + instalador) a partir
# do código-fonte do FreeBSD 15.1.
# Roda em Linux (cross-build com LLVM) ou FreeBSD. Uso: sh build-lumebsd.sh
#
# Precisa de: git, python3, bsdtar (libarchive-tools); no Linux também
# clang/lld/llvm 19 (o mesmo LLVM do FreeBSD 15.1), libarchive-dev,
# libbz2-dev, zlib1g-dev, bison, flex e time. Veja docs/ARQUITETURA.md.
set -e

TOP=$(cd "$(dirname "$0")" && pwd)
SRC=$TOP/src
OBJ=$TOP/obj
STAGE=$TOP/stage
PACOTES=$TOP/pacotes
IMG=$TOP/LumeBSD-1.1-Farol-pendrive.img
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}
VERSAO=$(cat "$TOP/VERSAO")
ATUALIZACAO=$TOP/atualizacao

# Commit do releng/15.1 em que os patches foram testados (FREEBSD_COMMIT=...
# para outro; o CI semanal confere o topo do ramo).
FREEBSD_COMMIT=${FREEBSD_COMMIT:-95719d8a895bf8170851e9844d9b4092b1d2b6fe}

# 1. Código-fonte do FreeBSD 15.1 + mudanças do LumeBSD
if [ ! -d "$SRC" ]; then
	git init -q "$SRC"
	git -C "$SRC" fetch -q --depth 1 \
	    https://github.com/freebsd/freebsd-src.git "$FREEBSD_COMMIT"
	git -C "$SRC" checkout -q FETCH_HEAD
fi
if [ ! -d "$SRC/usr.sbin/vigia" ]; then
	for p in "$TOP"/patches/*.patch; do
		(cd "$SRC" && git apply "$p")
	done
fi

mkdir -p "$OBJ"
export MAKEOBJDIRPREFIX=$OBJ
mk() {
	if [ "$(uname -s)" = FreeBSD ]; then
		make -C "$SRC" -j"$JOBS" SRCCONF="$TOP/conf/src.conf" \
		    KERNCONF=LUME "$@"
	else
		"$SRC/tools/build/make.py" \
		    --cross-bindir="${LLVM_BIN:-/usr/lib/llvm-19/bin}" \
		    --host-bindir="${HOST_BIN:-/usr/bin}" -j"$JOBS" \
		    SRCCONF="$TOP/conf/src.conf" TARGET=amd64 TARGET_ARCH=amd64 \
		    KERNCONF=LUME "$@"
	fi
}

# 2. Userland (com clang, ZFS e DTrace) e kernel LUME com todos os módulos
mk buildworld
mk buildkernel

# 3. Instalação num diretório, sem root (gera o METALOG)
rm -rf "$STAGE" && mkdir -p "$STAGE"
mk -DNO_ROOT DESTDIR="$STAGE" installworld distribution installkernel

# 4. Pacotes que vão no pendrive: gerenciador de pacotes, vídeo Intel e NVIDIA,
#    firmwares de vídeo e Wi-Fi e microcódigo da CPU
python3 "$TOP/ferramentas/pacotes.py" "$PACOTES" pkg drm-612-kmod nvidia-kmod \
    'gpu-firmware-intel-kmod-*' wifi-firmware-iwlwifi-kmod \
    cpu-microcode-intel

# 5. Imagem do pendrive (UEFI e BIOS)
python3 "$TOP/ferramentas/montar-pendrive.py" "$STAGE" "$PACOTES" \
    "$OBJ$SRC/amd64.amd64/tmp/legacy/bin" "$IMG" "$VERSAO"

# 6. Pacote de atualização (lume atualizar): o sistema base inteiro e o índice
python3 "$TOP/ferramentas/montar-atualizacao.py" "$STAGE" "$VERSAO" "$ATUALIZACAO"

echo
echo "Pronto: $IMG (LumeBSD $VERSAO)"
echo "Atualização: $ATUALIZACAO/ (publique atualizacao.txt e o .tar.zst juntos)"
echo "Grave num pendrive de 8 GB ou mais (no Windows: Rufus, modo DD;"
echo "no Linux/FreeBSD: dd if=$(basename "$IMG") of=/dev/SEU_PENDRIVE bs=1m)."

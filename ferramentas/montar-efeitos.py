"""Compila os efeitos do KWin do LumeBSD e os põe no stage do sistema.

uso: montar-efeitos.py STAGE PACOTES
     montar-efeitos.py --so-compilar RAIZ PACOTES PASTA
                  (o CI: compila contra um base.txz do FreeBSD, sem stage)

PACOTES é uma pasta com os .pkg do FreeBSD de onde vêm os cabeçalhos e as
bibliotecas (a lista PACOTES abaixo; pacotes.py --sem-dependencias).

- lume_tinta (app/lume-tinta): o filtro e-ink, em C++. Compila duas vezes:
  contra o KWin do X11 (kwin-x11) e o do Wayland (kwin). Sem moc: os
  metadados do plugin (o que o Q_PLUGIN_METADATA geraria) saem daqui, do
  metadata.json convertido para CBOR, no formato do moc do Qt 6.
- lume_refresh e lume_esmaecer (app/lume-animacoes): as animações de abrir e
  fechar janelas, em JavaScript (pacotes KWin/Effect).

Tudo vai para /usr/share/lume/kwin; o "lume tema integrar" instala no KWin
(em /usr/local) o que bate com a versão do KWin instalada.
"""
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sistema import RAIZ, linhas_do_build  # noqa: E402

TOPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LLVM = os.environ.get("LLVM_BIN", "/usr/lib/llvm-19/bin")
DESTINO = "usr/share/lume/kwin"
PACOTES = ["qt6-base", "kf6-kcoreaddons", "kf6-kconfig", "kf6-kglobalaccel",
           "plasma6-kwin", "plasma6-kwin-x11", "libepoxy", "libxcb", "xorgproto",
           "libdrm", "wayland", "libxkbcommon", "libglvnd"]
# Os dois KWin: pasta dos cabeçalhos, biblioteca, pasta dos plugins.
VARIANTES = {
    "x11": ("kwin-x11", "kwin-x11"),
    "wayland": ("kwin", "kwin"),
}


# --- CBOR (RFC 8949), só o que os metadados usam -------------------------

def _cabeca(tipo, n):
    if n < 24:
        return bytes([tipo << 5 | n])
    for extra, fmt in ((24, 1), (25, 2), (26, 4), (27, 8)):
        if n < 1 << (8 * fmt):
            return bytes([tipo << 5 | extra]) + n.to_bytes(fmt, "big")
    raise ValueError(n)


def cbor(v):
    if isinstance(v, bool):
        return bytes([0xf5 if v else 0xf4])
    if v is None:
        return bytes([0xf6])
    if isinstance(v, int):
        return _cabeca(0, v) if v >= 0 else _cabeca(1, -1 - v)
    if isinstance(v, str):
        b = v.encode("utf-8")
        return _cabeca(3, len(b)) + b
    if isinstance(v, list):
        return _cabeca(4, len(v)) + b"".join(cbor(x) for x in v)
    if isinstance(v, dict):
        return _cabeca(5, len(v)) + b"".join(cbor(k) + cbor(x) for k, x in v.items())
    raise TypeError(type(v))


def metadados(iid, classe, json_meta):
    """O array qt_pluginMetaDataV2_<classe>, como o moc do Qt 6 escreve:
    um mapa CBOR indefinido com IID (2), ClassName (3) e MetaData (4)."""
    corpo = (bytes([0xbf]) + cbor(2) + cbor(iid) + cbor(3) + cbor(classe)
             + cbor(4) + cbor(json_meta) + bytes([0xff]))
    linhas = []
    for i in range(0, len(corpo), 12):
        linhas.append("    " + ", ".join(f"0x{b:02x}" for b in corpo[i:i + 12]) + ",")
    return (f"static constexpr unsigned char qt_pluginMetaDataV2_{classe}[] = {{\n"
            + "\n".join(linhas) + "\n};\n")


# --- compilação ------------------------------------------------------------

def achar_pacote(pasta, nome):
    for p in sorted(glob.glob(os.path.join(pasta, f"{nome}-[0-9]*.pkg"))):
        return p
    sys.exit(f"montar-efeitos: falta o pacote {nome} em {pasta}")


def versao_kwin(raiz_local, variante):
    pasta = VARIANTES[variante][0]
    conf = os.path.join(raiz_local, "include", pasta, "config-kwin.h")
    for linha in open(conf):
        if "KWIN_PLUGIN_VERSION_STRING" in linha:
            return linha.split('"')[1]
    sys.exit(f"montar-efeitos: sem a versão em {conf}")


def compilar_tinta(stage, pacotes, saida_dir):
    """Compila o lume_tinta para as duas variantes; devolve {variante: (so, versão)}."""
    fonte = os.path.join(TOPO, "app", "lume-tinta")
    meta = json.load(open(os.path.join(fonte, "metadata.json"), encoding="utf-8"))
    feitos = {}
    with tempfile.TemporaryDirectory() as tmp:
        local = os.path.join(tmp, "local")
        os.makedirs(local)
        for nome in PACOTES:
            # Nem todo pacote tem as duas pastas (xorgproto só tem
            # include/): o bsdtar reclama da que falta, sem problema.
            subprocess.run(["bsdtar", "-xf", achar_pacote(pacotes, nome), "-C", local,
                            "--strip-components", "3", "/usr/local/include", "/usr/local/lib"],
                           stderr=subprocess.DEVNULL)
        raiz = os.path.join(tmp, "raiz")
        os.makedirs(os.path.join(raiz, "usr"))
        for d in ("lib", "usr/lib", "usr/include"):
            os.symlink(os.path.join(stage, d), os.path.join(raiz, d))
        os.symlink(local, os.path.join(raiz, "usr/local"))
        inc = os.path.join(local, "include")
        qt = os.path.join(inc, "qt6")
        for variante, (pasta, lib) in VARIANTES.items():
            versao = versao_kwin(local, variante)
            gerado = os.path.join(tmp, f"gerado-{variante}")
            os.makedirs(gerado)
            with open(os.path.join(gerado, "metadados.inc"), "w") as f:
                f.write(metadados("org.kde.kwin.EffectPluginFactory" + versao,
                                  "TintaFabrica", meta))
            so = os.path.join(saida_dir, variante, "lume_tinta.so")
            os.makedirs(os.path.dirname(so), exist_ok=True)
            cmd = [os.path.join(LLVM, "clang++"), "--target=x86_64-unknown-freebsd15.1",
                   f"--sysroot={raiz}", "-std=c++20", "-O2", "-fPIC", "-shared",
                   "-Wall", "-Wno-deprecated-declarations",
                   "-DQT_NO_KEYWORDS", "-DQT_NO_DEBUG", "-I", gerado, "-isystem", os.path.join(inc, pasta)]
            for d in [qt] + [os.path.join(qt, m) for m in ("QtCore", "QtGui", "QtDBus")] + \
                     [os.path.join(inc, "KF6", m) for m in ("KConfigCore", "KConfig",
                                                            "KCoreAddons", "KGlobalAccel")] + [inc]:
                cmd += ["-isystem", d]
            cmd += [os.path.join(fonte, "tinta.cpp"), "-o", so, "-fuse-ld=lld",
                    f"-L{local}/lib", f"-L{local}/lib/qt6", f"-l{lib}",
                    "-lKF6GlobalAccel", "-lKF6ConfigCore", "-lQt6Gui", "-lQt6DBus", "-lQt6Core",
                    "-Wl,-rpath,/usr/local/lib/qt6:/usr/local/lib",
                    "-Wl,--allow-shlib-undefined", "-Wl,--no-undefined-version"]
            subprocess.run(cmd, check=True)
            subprocess.run([os.path.join(LLVM, "llvm-strip"), so], check=True)
            # A versão do KWin contra a qual compilou: o KWin só carrega um
            # plugin da mesma versão (está no IID).
            with open(os.path.join(saida_dir, variante, "versao"), "w") as f:
                f.write(versao + "\n")
            feitos[variante] = (so, versao)
            print(f"efeito: lume_tinta ({variante}, KWin {versao}, "
                  f"{os.path.getsize(so) // 1024} KiB)")
    return feitos


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--so-compilar":
        raiz, pacotes, pasta = (os.path.abspath(a) for a in sys.argv[2:5])
        compilar_tinta(raiz, pacotes, pasta)
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    stage, pacotes = (os.path.abspath(a) for a in sys.argv[1:3])
    destino = os.path.join(stage, DESTINO)
    if os.path.isdir(destino):
        shutil.rmtree(destino)
    os.makedirs(destino)
    compilar_tinta(stage, pacotes, destino)
    # As animações (JS): copiadas como estão.
    anim = os.path.join(TOPO, "app", "lume-animacoes")
    for d in sorted(os.listdir(anim)):
        if os.path.isdir(os.path.join(anim, d)):
            shutil.copytree(os.path.join(anim, d), os.path.join(destino, "efeitos", d))
    # METALOG.build: diretórios e arquivos, sem repetir se rodar de novo.
    novas = []
    for base, dirs, arqs in os.walk(destino):
        rel = "./" + os.path.relpath(base, stage)
        novas.append(f"{rel} type=dir {RAIZ} mode=0755 tags=package=lume")
        for a in sorted(arqs):
            modo = "0555" if a.endswith(".so") else "0444"
            novas.append(f"{rel}/{a} type=file {RAIZ} mode={modo} tags=package=lume")
    linhas_do_build(stage)
    build = os.path.join(stage, "METALOG.build")
    caminhos = {l.split()[0] for l in novas}
    linhas = [l for l in open(build).read().splitlines()
              if l.strip() and l.split()[0] not in caminhos
              and not l.split()[0].startswith(f"./{DESTINO}/")]
    with open(build, "w") as f:
        f.write("\n".join(linhas + novas) + "\n")


if __name__ == "__main__":
    main()

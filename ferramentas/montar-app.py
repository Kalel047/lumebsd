"""Compila os apps do LumeBSD e os põe no stage do sistema.

uso: montar-app.py STAGE QT6BASE.pkg
     montar-app.py --so-compilar RAIZ QT6BASE.pkg PASTA
                  (o CI: compila contra um base.txz do FreeBSD, sem stage)

Os apps: "Atualizações do LumeBSD" (app/lume-atualizacoes), "Programas do
LumeBSD" (app/lume-programas), o ícone de rede (app/lume-rede) e os
atributos dos arquivos (app/lume-atributos).

STAGE        saída de "installworld distribution installkernel -DNO_ROOT"
QT6BASE.pkg  o pacote qt6-base do FreeBSD (só os cabeçalhos e as
             bibliotecas, para compilar e ligar; no sistema, o Qt vem com o
             desktop)

Cada app (app/NOME/main.cpp) é Qt 6 sem moc, então compila com o clang de
fora do FreeBSD: o stage dá a libc e a libc++ do FreeBSD, o pacote dá o Qt.
Vai para /usr/bin/NOME (e o manual NOME.1), com o ícone, os atalhos do menu
e a política do polkit em /usr/share/lume/app (o "lume tema integrar"
instala esses no KDE). As linhas vão para o
METALOG.build, para entrarem no pendrive e no pacote de atualização.

Variáveis: LLVM_BIN (padrão /usr/lib/llvm-19/bin).
"""
import gzip
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sistema import RAIZ, linhas_do_build  # noqa: E402

TOPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LLVM = os.environ.get("LLVM_BIN", "/usr/lib/llvm-19/bin")
APP = "usr/share/lume/app"
APPS = {
    "lume-atualizacoes": ["lume-atualizacoes.svg", "lume-atualizacoes.desktop",
                          "lume-atualizacoes-bandeja.desktop",
                          "org.lumebsd.atualizar.policy"],
    "lume-programas": ["lume-programas.svg", "lume-programas.desktop",
                       "org.lumebsd.programas.policy"],
    "lume-rede": ["lume-rede.desktop", "org.lumebsd.rede.policy"],
    "lume-atributos": ["lume-atributos.desktop", "lume-atributos-menu.desktop",
                       "lume-atributos-pasta.desktop", "lume-consulta.xml"],
}
# Bibliotecas além do Qt (do FreeBSD, no stage).
LIBS = {"lume-rede": ["-lcrypto"]}


def compilar(stage, qtpkg, saida, nome):
    with tempfile.TemporaryDirectory() as tmp:
        qt = os.path.join(tmp, "qt")
        os.makedirs(qt)
        # (No pacote, os caminhos são absolutos: /usr/local/...)
        subprocess.run(["bsdtar", "-xf", qtpkg, "-C", qt, "/usr/local/include",
                        "/usr/local/lib"], check=True)
        # A raiz de compilação: o FreeBSD do stage e o Qt do pacote.
        raiz = os.path.join(tmp, "raiz")
        os.makedirs(os.path.join(raiz, "usr"))
        for d in ("lib", "usr/lib", "usr/include"):
            os.symlink(os.path.join(stage, d), os.path.join(raiz, d))
        os.symlink(os.path.join(qt, "usr/local"), os.path.join(raiz, "usr/local"))
        inc = os.path.join(raiz, "usr/local/include/qt6")
        cmd = [os.path.join(LLVM, "clang++"), "--target=x86_64-unknown-freebsd15.1",
               f"--sysroot={raiz}", "-std=c++17", "-O2", "-fPIC", "-Wall",
               "-isystem", inc]
        for m in ("QtCore", "QtGui", "QtWidgets"):
            cmd += ["-isystem", os.path.join(inc, m)]
        cmd += [os.path.join(TOPO, "app", nome, "main.cpp"), "-o", saida,
                "-fuse-ld=lld", f"-L{raiz}/usr/local/lib/qt6",
                "-lQt6Widgets", "-lQt6Gui", "-lQt6Core", *LIBS.get(nome, []),
                "-Wl,-rpath,/usr/local/lib/qt6:/usr/local/lib",
                "-Wl,--allow-shlib-undefined"]
        subprocess.run(cmd, check=True)
    subprocess.run([os.path.join(LLVM, "llvm-strip"), saida], check=True)


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--so-compilar":
        raiz, qtpkg, pasta = (os.path.abspath(a) for a in sys.argv[2:5])
        os.makedirs(pasta, exist_ok=True)
        for nome in APPS:
            saida = os.path.join(pasta, nome)
            compilar(raiz, qtpkg, saida, nome)
            print(f"app: {saida} ({os.path.getsize(saida) // 1024} KiB)")
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    stage, qtpkg = (os.path.abspath(a) for a in sys.argv[1:3])
    os.makedirs(os.path.join(stage, APP), exist_ok=True)
    novas = [f"./{APP} type=dir {RAIZ} mode=0755 tags=package=lume"]
    for nome, arquivos in APPS.items():
        fonte = os.path.join(TOPO, "app", nome)
        binario = os.path.join(stage, "usr/bin", nome)
        compilar(stage, qtpkg, binario, nome)
        os.chmod(binario, 0o555)
        for a in arquivos:
            shutil.copy(os.path.join(fonte, a), os.path.join(stage, APP, a))
        man = os.path.join(stage, f"usr/share/man/man1/{nome}.1.gz")
        with open(os.path.join(fonte, f"{nome}.1"), "rb") as o, \
                gzip.GzipFile(man, "wb", mtime=0) as d:
            d.write(o.read())
        novas += [f"./usr/bin/{nome} type=file {RAIZ} mode=0555 tags=package=lume",
                  f"./usr/share/man/man1/{nome}.1.gz type=file {RAIZ} mode=0444 tags=package=lume"]
        novas += [f"./{APP}/{a} type=file {RAIZ} mode=0444 tags=package=lume"
                  for a in arquivos]
        print(f"app: {binario} ({os.path.getsize(binario) // 1024} KiB)")
    # No METALOG do build (o original), sem repetir se rodar de novo.
    linhas_do_build(stage)
    build = os.path.join(stage, "METALOG.build")
    caminhos = {l.split()[0] for l in novas}
    linhas = [l for l in open(build).read().splitlines()
              if l.strip() and l.split()[0] not in caminhos]
    with open(build, "w") as f:
        f.write("\n".join(linhas + novas) + "\n")


if __name__ == "__main__":
    main()

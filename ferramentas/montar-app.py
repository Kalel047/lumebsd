"""Compila o app "Atualizações do LumeBSD" e o põe no stage do sistema.

uso: montar-app.py STAGE QT6BASE.pkg
     montar-app.py --so-compilar RAIZ QT6BASE.pkg SAIDA
                  (o CI: compila contra um base.txz do FreeBSD, sem stage)

STAGE        saída de "installworld distribution installkernel -DNO_ROOT"
QT6BASE.pkg  o pacote qt6-base do FreeBSD (só os cabeçalhos e as
             bibliotecas, para compilar e ligar; no sistema, o Qt vem com o
             desktop)

O app (app/lume-atualizacoes/main.cpp) é Qt 6 sem moc, então compila com o
clang de fora do FreeBSD: o stage dá a libc e a libc++ do FreeBSD, o pacote
dá o Qt. Vai para /usr/bin/lume-atualizacoes (e o manual), com o ícone, os atalhos do
menu e da sessão e a política do polkit em /usr/share/lume/app (o
"lume tema integrar" instala esses no KDE). As linhas vão para o
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
FONTE = os.path.join(TOPO, "app", "lume-atualizacoes")
LLVM = os.environ.get("LLVM_BIN", "/usr/lib/llvm-19/bin")
APP = "usr/share/lume/app"
ARQUIVOS = ["lume-atualizacoes.svg", "lume-atualizacoes.desktop",
            "lume-atualizacoes-bandeja.desktop", "org.lumebsd.atualizar.policy"]


def compilar(stage, qtpkg, saida):
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
        cmd += [os.path.join(FONTE, "main.cpp"), "-o", saida,
                "-fuse-ld=lld", f"-L{raiz}/usr/local/lib/qt6",
                "-lQt6Widgets", "-lQt6Gui", "-lQt6Core",
                "-Wl,-rpath,/usr/local/lib/qt6:/usr/local/lib",
                "-Wl,--allow-shlib-undefined"]
        subprocess.run(cmd, check=True)
    subprocess.run([os.path.join(LLVM, "llvm-strip"), saida], check=True)


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--so-compilar":
        raiz, qtpkg, saida = (os.path.abspath(a) for a in sys.argv[2:5])
        compilar(raiz, qtpkg, saida)
        print(f"app: {saida} ({os.path.getsize(saida) // 1024} KiB)")
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    stage, qtpkg = (os.path.abspath(a) for a in sys.argv[1:3])
    binario = os.path.join(stage, "usr/bin/lume-atualizacoes")
    compilar(stage, qtpkg, binario)
    os.chmod(binario, 0o555)
    os.makedirs(os.path.join(stage, APP), exist_ok=True)
    for a in ARQUIVOS:
        shutil.copy(os.path.join(FONTE, a), os.path.join(stage, APP, a))

    man = os.path.join(stage, "usr/share/man/man1/lume-atualizacoes.1.gz")
    with open(os.path.join(FONTE, "lume-atualizacoes.1"), "rb") as o, \
            gzip.GzipFile(man, "wb", mtime=0) as d:
        d.write(o.read())

    novas = [f"./usr/bin/lume-atualizacoes type=file {RAIZ} mode=0555 tags=package=lume",
             f"./usr/share/man/man1/lume-atualizacoes.1.gz type=file {RAIZ} mode=0444 tags=package=lume",
             f"./{APP} type=dir {RAIZ} mode=0755 tags=package=lume"]
    novas += [f"./{APP}/{a} type=file {RAIZ} mode=0444 tags=package=lume"
              for a in ARQUIVOS]
    # No METALOG do build (o original), sem repetir se rodar de novo.
    linhas_do_build(stage)
    build = os.path.join(stage, "METALOG.build")
    caminhos = {l.split()[0] for l in novas}
    linhas = [l for l in open(build).read().splitlines()
              if l.strip() and l.split()[0] not in caminhos]
    with open(build, "w") as f:
        f.write("\n".join(linhas + novas) + "\n")
    print(f"app: {binario} ({os.path.getsize(binario) // 1024} KiB)")


if __name__ == "__main__":
    main()

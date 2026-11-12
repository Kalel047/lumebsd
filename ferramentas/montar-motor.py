"""Compila o lume-motor (app/lume-motor) para o FreeBSD e o põe no stage.

uso: montar-motor.py STAGE PACOTES
     montar-motor.py --so-compilar RAIZ PACOTES PASTA
                  (o CI: compila contra um base.txz do FreeBSD, sem stage;
                  a biblioteca e o programa ficam em PASTA)

PACOTES é uma pasta com os .pkg do FreeBSD de onde vêm os cabeçalhos e as
bibliotecas (a lista PACOTES abaixo). O programa usa o Mesa (EGL, GLES 3.1,
GBM), o libdrm, o FreeType e o libpng, que chegam com o desktop.

O motor é uma biblioteca, /usr/lib/liblume-motor.so.1, com os cabeçalhos em
/usr/include/lume (motor.h, saida.h, papel.h); a demonstração,
/usr/bin/lume-motor, é ligada a ela.
"""
import glob
import gzip
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sistema import RAIZ, embutir_papel, linhas_do_build  # noqa: E402

TOPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LLVM = os.environ.get("LLVM_BIN", "/usr/lib/llvm-19/bin")
FONTE = os.path.join(TOPO, "app", "lume-motor")
PACOTES = ["libglvnd", "mesa-libs", "libdrm", "freetype2", "png"]
BIBLIOTECA = ["motor.c", "cena.c", "saida.c"]
CABECALHOS = ["motor.h", "saida.h", "papel.h"]
SONAME = "liblume-motor.so.1"


def achar_pacote(pasta, nome):
    for p in sorted(glob.glob(os.path.join(pasta, f"{nome}-[0-9]*.pkg"))):
        return p
    sys.exit(f"montar-motor: falta o pacote {nome} em {pasta}")


def compilar(raiz_base, pacotes, pasta):
    with tempfile.TemporaryDirectory() as tmp:
        local = os.path.join(tmp, "local")
        os.makedirs(local)
        for nome in PACOTES:
            subprocess.run(["bsdtar", "-xf", achar_pacote(pacotes, nome), "-C", local,
                            "--strip-components", "3", "/usr/local/include", "/usr/local/lib"],
                           stderr=subprocess.DEVNULL)
        raiz = os.path.join(tmp, "raiz")
        os.makedirs(os.path.join(raiz, "usr"))
        for d in ("lib", "usr/lib", "usr/include"):
            os.symlink(os.path.join(raiz_base, d), os.path.join(raiz, d))
        os.symlink(local, os.path.join(raiz, "usr/local"))
        inc = os.path.join(local, "include")
        # As contas do papel (papel.glsl), comuns com o filtro do KWin.
        gerado = os.path.join(tmp, "gerado")
        embutir_papel(gerado)
        base = [os.path.join(LLVM, "clang"), "--target=x86_64-unknown-freebsd15.1",
                f"--sysroot={raiz}", "-std=c11", "-O2", "-Wall", "-Wextra",
                "-Wno-unused-parameter", "-D_XOPEN_SOURCE=700", "-D__BSD_VISIBLE=1",
                "-I", gerado, "-isystem", inc, "-isystem", os.path.join(inc, "libdrm"),
                "-isystem", os.path.join(inc, "freetype2"), "-fuse-ld=lld"]
        os.makedirs(pasta, exist_ok=True)
        lib = os.path.join(pasta, SONAME)
        subprocess.run(base + ["-fPIC", "-shared", f"-Wl,-soname,{SONAME}"]
                       + [os.path.join(FONTE, a) for a in BIBLIOTECA]
                       + ["-o", lib, f"-L{local}/lib",
                          "-lEGL", "-lGLESv2", "-lgbm", "-ldrm", "-lfreetype", "-lpng16", "-lm",
                          "-Wl,-rpath,/usr/local/lib", "-Wl,--allow-shlib-undefined"],
                       check=True)
        curto = os.path.join(pasta, "liblume-motor.so")
        if os.path.lexists(curto):
            os.unlink(curto)
        os.symlink(SONAME, curto)
        prog = os.path.join(pasta, "lume-motor")
        subprocess.run(base + [os.path.join(FONTE, "demo.c"), "-o", prog,
                               f"-L{pasta}", "-llume-motor", f"-L{local}/lib", "-lGLESv2",
                               "-lm", "-Wl,--allow-shlib-undefined"], check=True)
        for f in (lib, prog):
            subprocess.run([os.path.join(LLVM, "llvm-strip"), "--strip-unneeded", f], check=True)
    print(f"motor: {SONAME} ({os.path.getsize(lib) // 1024} KiB), "
          f"lume-motor ({os.path.getsize(prog) // 1024} KiB)")


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--so-compilar":
        raiz, pacotes, pasta = (os.path.abspath(a) for a in sys.argv[2:5])
        compilar(raiz, pacotes, pasta)
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    stage, pacotes = (os.path.abspath(a) for a in sys.argv[1:3])
    with tempfile.TemporaryDirectory() as tmp:
        compilar(stage, pacotes, tmp)
        shutil.copy(os.path.join(tmp, "lume-motor"), os.path.join(stage, "usr/bin/lume-motor"))
        shutil.copy(os.path.join(tmp, SONAME), os.path.join(stage, "usr/lib", SONAME))
    curto = os.path.join(stage, "usr/lib/liblume-motor.so")
    if os.path.lexists(curto):
        os.unlink(curto)
    os.symlink(SONAME, curto)
    inc = os.path.join(stage, "usr/include/lume")
    os.makedirs(inc, exist_ok=True)
    for h in CABECALHOS:
        shutil.copy(os.path.join(FONTE, h), os.path.join(inc, h))
    man = os.path.join(stage, "usr/share/man/man1/lume-motor.1.gz")
    with open(os.path.join(FONTE, "lume-motor.1"), "rb") as f, gzip.open(man, "wb") as g:
        shutil.copyfileobj(f, g)
    novas = [f"./usr/bin/lume-motor type=file {RAIZ} mode=0555 tags=package=lume",
             f"./usr/lib/{SONAME} type=file {RAIZ} mode=0444 tags=package=lume",
             f"./usr/lib/liblume-motor.so type=link {RAIZ} mode=0755 link={SONAME} tags=package=lume",
             f"./usr/include/lume type=dir {RAIZ} mode=0755 tags=package=lume"]
    novas += [f"./usr/include/lume/{h} type=file {RAIZ} mode=0444 tags=package=lume"
              for h in CABECALHOS]
    novas.append(f"./usr/share/man/man1/lume-motor.1.gz type=file {RAIZ} mode=0444 tags=package=lume")
    linhas_do_build(stage)
    build = os.path.join(stage, "METALOG.build")
    caminhos = {l.split()[0] for l in novas}
    linhas = [l for l in open(build).read().splitlines()
              if l.strip() and l.split()[0] not in caminhos]
    with open(build, "w") as f:
        f.write("\n".join(linhas + novas) + "\n")


if __name__ == "__main__":
    main()

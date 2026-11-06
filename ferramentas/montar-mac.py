"""Compila o lume-macho (mac/lume-macho) para o FreeBSD e o põe no stage,
com o programa de teste do macOS (mac/teste/ola-mac.c, um Mach-O feito
aqui mesmo com o clang e o ld64.lld).

uso: montar-mac.py STAGE
     montar-mac.py --so-compilar RAIZ SAIDA
                  (o CI: compila contra um base.txz do FreeBSD, sem stage)

Vai para /usr/libexec/lume-macho e /usr/share/lume/mac/ola-mac; o
lume-compat usa os dois (lume compat mac testar; dois cliques num
executável do Mac).
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
MAC = os.path.join(TOPO, "mac")


def compilar(raiz_base, saida):
    with tempfile.TemporaryDirectory() as tmp:
        raiz = os.path.join(tmp, "raiz")
        os.makedirs(os.path.join(raiz, "usr"))
        for d in ("lib", "usr/lib", "usr/include"):
            os.symlink(os.path.join(raiz_base, d), os.path.join(raiz, d))
        subprocess.run([os.path.join(LLVM, "clang"), "--target=x86_64-unknown-freebsd15.1",
                        f"--sysroot={raiz}", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-D__BSD_VISIBLE=1", "-fuse-ld=lld",
                        os.path.join(MAC, "lume-macho", "lume-macho.c"),
                        "-o", os.path.join(saida, "lume-macho")], check=True)
        subprocess.run([os.path.join(LLVM, "llvm-strip"), os.path.join(saida, "lume-macho")],
                       check=True)
        # O programa de teste: um Mach-O de verdade, sem a libSystem.
        obj = os.path.join(tmp, "ola-mac.o")
        subprocess.run([os.path.join(LLVM, "clang"), "--target=x86_64-apple-macos11", "-O2",
                        "-fno-stack-protector", "-fno-builtin", "-ffreestanding", "-c",
                        os.path.join(MAC, "teste", "ola-mac.c"), "-o", obj], check=True)
        subprocess.run([os.path.join(LLVM, "ld64.lld"), "-arch", "x86_64", "-platform_version",
                        "macos", "11.0", "11.0", "-e", "_start", "-o",
                        os.path.join(saida, "ola-mac"), obj], check=True)
    print(f"mac: lume-macho ({os.path.getsize(os.path.join(saida, 'lume-macho')) // 1024} KiB), "
          f"ola-mac ({os.path.getsize(os.path.join(saida, 'ola-mac'))} bytes)")


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--so-compilar":
        raiz, saida = (os.path.abspath(a) for a in sys.argv[2:4])
        os.makedirs(saida, exist_ok=True)
        compilar(raiz, saida)
        return
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    stage = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory() as saida:
        compilar(stage, saida)
        os.makedirs(os.path.join(stage, "usr/libexec"), exist_ok=True)
        os.makedirs(os.path.join(stage, "usr/share/lume/mac"), exist_ok=True)
        shutil.copy(os.path.join(saida, "lume-macho"), os.path.join(stage, "usr/libexec/lume-macho"))
        shutil.copy(os.path.join(saida, "ola-mac"), os.path.join(stage, "usr/share/lume/mac/ola-mac"))
    man = os.path.join(stage, "usr/share/man/man1/lume-macho.1.gz")
    with open(os.path.join(MAC, "lume-macho", "lume-macho.1"), "rb") as f, gzip.open(man, "wb") as g:
        shutil.copyfileobj(f, g)
    novas = [f"./usr/libexec/lume-macho type=file {RAIZ} mode=0555 tags=package=lume",
             f"./usr/share/lume/mac type=dir {RAIZ} mode=0755 tags=package=lume",
             f"./usr/share/lume/mac/ola-mac type=file {RAIZ} mode=0555 tags=package=lume",
             f"./usr/share/man/man1/lume-macho.1.gz type=file {RAIZ} mode=0444 tags=package=lume"]
    linhas_do_build(stage)
    build = os.path.join(stage, "METALOG.build")
    caminhos = {l.split()[0] for l in novas}
    linhas = [l for l in open(build).read().splitlines()
              if l.strip() and l.split()[0] not in caminhos]
    with open(build, "w") as f:
        f.write("\n".join(linhas + novas) + "\n")


if __name__ == "__main__":
    main()

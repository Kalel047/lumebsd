"""Mantém fonte/ igual ao que os patches criam.

Os patches em patches/ são a fonte de verdade (é deles que o build parte),
mas código dentro de um .patch é ruim de ler. fonte/ traz uma cópia, com os
mesmos caminhos da árvore do FreeBSD, de cada arquivo que o LumeBSD
acrescenta (forja, lume, vigia, lume-configurar, rc.d, tema...).

uso: fonte.py gerar   SRC    copia de SRC (FreeBSD com os patches) para fonte/
     fonte.py conferir SRC   falha se fonte/ estiver diferente de SRC

SRC é uma árvore releng/15.1 com patches/*.patch aplicados em ordem.
"""
import filecmp
import os
import re
import shutil
import sys

TOPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHES = os.path.join(TOPO, "patches")
FONTE = os.path.join(TOPO, "fonte")


def novos_e_alterados():
    """Arquivos criados e alterados pela série de patches, em ordem."""
    novos, alterados = [], []
    for nome in sorted(os.listdir(PATCHES)):
        if not nome.endswith(".patch"):
            continue
        atual = None
        for linha in open(os.path.join(PATCHES, nome), encoding="utf-8",
                          errors="replace"):
            m = re.match(r"diff --git a/(\S+) b/(\S+)", linha)
            if m:
                atual = m.group(2)
                if atual not in novos and atual not in alterados:
                    alterados.append(atual)
            elif linha.startswith("new file mode") and atual:
                if atual in alterados:
                    alterados.remove(atual)
                if atual not in novos:
                    novos.append(atual)
            elif linha.startswith("deleted file mode") and atual:
                for lista in (novos, alterados):
                    if atual in lista:
                        lista.remove(atual)
    return sorted(novos), sorted(alterados)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("gerar", "conferir"):
        sys.exit(__doc__)
    modo, src = sys.argv[1], os.path.abspath(sys.argv[2])
    novos, alterados = novos_e_alterados()
    erros = []
    if modo == "gerar":
        shutil.rmtree(FONTE, ignore_errors=True)
    for rel in novos:
        origem = os.path.join(src, rel)
        destino = os.path.join(FONTE, rel)
        if not os.path.isfile(origem):
            erros.append(f"falta em SRC: {rel}")
        elif modo == "gerar":
            os.makedirs(os.path.dirname(destino), exist_ok=True)
            shutil.copy2(origem, destino)
        elif not os.path.isfile(destino):
            erros.append(f"falta em fonte/: {rel}")
        elif not filecmp.cmp(origem, destino, shallow=False):
            erros.append(f"diferente: {rel}")
    if modo == "conferir":
        for raiz, _, arquivos in os.walk(FONTE):
            for a in arquivos:
                rel = os.path.relpath(os.path.join(raiz, a), FONTE)
                if rel != "LEIAME.md" and rel not in novos:
                    erros.append(f"sobrando em fonte/: {rel}")
    if modo == "gerar":
        with open(os.path.join(FONTE, "LEIAME.md"), "w") as f:
            f.write("# Código do LumeBSD\n\n"
                    "Cópia legível dos arquivos que o LumeBSD acrescenta ao "
                    "FreeBSD 15.1, nos mesmos caminhos da árvore do FreeBSD.\n"
                    "A fonte de verdade são os `patches/`; esta pasta é gerada "
                    "por `ferramentas/fonte.py gerar` e conferida no CI.\n\n"
                    "## Arquivos do FreeBSD que os patches alteram\n\n"
                    "Estes só aparecem nos patches (são mudanças pequenas "
                    "em arquivos do FreeBSD):\n\n")
            f.writelines(f"- `{rel}`\n" for rel in alterados)
    for e in erros:
        print(e)
    if erros:
        sys.exit(1)
    print(f"fonte/: {len(novos)} arquivos {'gerados' if modo == 'gerar' else 'conferidos'}")


if __name__ == "__main__":
    main()

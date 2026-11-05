"""Confere o catálogo da loja (lume loja) contra os pacotes do FreeBSD 15.

uso: conferir-catalogo.py CATALOGO [packagesite.yaml]

Sem o packagesite.yaml, baixa o catálogo quarterly de pkg.freebsd.org.
Confere: 9 campos por linha, ids únicos, categorias e kits que existem,
URLs dos apps da web e, principalmente, que cada pacote existe no
repositório. Mostra o tamanho de cada kit (instalado, com as dependências
que ainda não vêm com o desktop).
"""
import json
import os
import subprocess
import sys
import tempfile

SITE = "https://pkg.freebsd.org/FreeBSD:15:amd64/quarterly/packagesite.pkg"


def ler_indice(caminho):
    idx = {}
    for linha in open(caminho):
        d = json.loads(linha)
        idx[d["name"]] = d
    return idx


def baixar(tmp):
    subprocess.run(["curl", "-sSf", "-o", f"{tmp}/p.pkg", SITE], check=True)
    subprocess.run(["bsdtar", "-xf", "p.pkg", "packagesite.yaml"], cwd=tmp,
                   check=True)
    return os.path.join(tmp, "packagesite.yaml")


def fecho(idx, nomes):
    """Os pacotes e todas as dependências."""
    vistos, fila = set(), list(nomes)
    while fila:
        n = fila.pop()
        if n in vistos or n not in idx:
            continue
        vistos.add(n)
        fila.extend(idx[n].get("deps", {}).keys())
    return vistos


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    cat = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        idx = ler_indice(sys.argv[2] if len(sys.argv) == 3 else baixar(tmp))

    erros, cats, kits, ids, itens = [], set(), {}, set(), []
    for n, linha in enumerate(open(cat, encoding="utf-8"), 1):
        linha = linha.rstrip("\n")
        if not linha.strip() or linha.startswith("#"):
            continue
        if linha.startswith("@categoria "):
            cats.add(linha.split()[1].split("|")[0])
            continue
        if linha.startswith("@kit "):
            c = linha[5:].split("|")
            kits[c[0]] = c[3].split()
            continue
        c = linha.split("|")
        if len(c) != 9:
            erros.append(f"linha {n}: {len(c)} campos (esperava 9)")
            continue
        iid, categoria, nome, pacotes, ks = c[:5]
        if iid in ids:
            erros.append(f"linha {n}: id repetido: {iid}")
        ids.add(iid)
        itens.append((iid, categoria, pacotes.split(), ks.split()))
        if not nome or not c[6]:
            erros.append(f"linha {n}: {iid} sem nome ou descrição")

    # O desktop já instalado (lume desktop): o que vier com ele não conta.
    desktop = os.path.join(os.path.dirname(cat), "..", "desktop.txt")
    base = set()
    if os.path.exists(desktop):
        nomes = [l.split("#")[0].strip() for l in open(desktop)]
        base = fecho(idx, [x for x in nomes if x])

    for iid, categoria, pacotes, ks in itens:
        if categoria not in cats:
            erros.append(f"{iid}: categoria desconhecida: {categoria}")
        for k in ks:
            if k not in kits:
                erros.append(f"{iid}: kit desconhecido: {k}")
        web = [p for p in pacotes if p.startswith("web:")]
        if web and (len(pacotes) != 1 or not web[0].startswith("web:https://")):
            erros.append(f"{iid}: app da web precisa ser só web:https://...")
        for p in pacotes:
            if p.startswith("web:") or p == "+linux":
                continue
            if p not in idx:
                erros.append(f"{iid}: pacote não existe no FreeBSD 15: {p}")
    for k, inc in kits.items():
        for i in inc:
            if i not in kits:
                erros.append(f"kit {k}: inclui kit desconhecido: {i}")

    for k in kits:
        alvo = {k} | set(kits[k])
        pk = [p for _, _, ps, ks in itens if alvo & set(ks) for p in ps
              if not p.startswith("web:") and p != "+linux"]
        novos = fecho(idx, pk) - base
        mb = sum(idx[p].get("flatsize", 0) for p in novos) / 1e6
        down = sum(idx[p].get("pkgsize", 0) for p in novos) / 1e6
        print(f"kit {k}: {len(pk)} pacotes, {len(novos)} com dependências; "
              f"baixa {down:.0f} MB, ocupa {mb:.0f} MB")
    print(f"{len(itens)} programas em {len(cats)} categorias")
    if erros:
        print("\n".join(erros), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

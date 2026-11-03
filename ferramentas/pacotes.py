"""Baixa pacotes oficiais do FreeBSD 15 (com dependências) e confere cada um.

uso: pacotes.py DESTINO pacote [pacote ...]
Um nome com * é um padrão: 'gpu-firmware-intel-kmod-*' pega todos.

Lê os catálogos assinados de pkg.freebsd.org (quarterly e kmods do 15.1),
resolve as dependências e confere o checksum de cada arquivo contra o
catálogo: BLAKE2b-512 em z-base32, o formato "2$" do pkg.
Precisa de curl e bsdtar (o catálogo vem comprimido com zstd).
"""
import fnmatch
import hashlib
import json
import os
import subprocess
import sys
import tempfile

SITE = "https://pkg.freebsd.org/FreeBSD:15:amd64"
REPOS = ["kmods_quarterly_1", "quarterly"]     # drivers do kernel primeiro
ALFA = "ybndrfg8ejkmcpqxot1uwisza345h769"


def soma_pkg(caminho):
    h = hashlib.blake2b(open(caminho, "rb").read()).digest()
    bits = "".join(f"{x:08b}"[::-1] for x in h)
    return "2$" + "".join(ALFA[int(bits[i:i + 5].ljust(5, "0")[::-1], 2)]
                          for i in range(0, len(bits), 5))


def catalogo(tmp):
    idx = {}
    for repo in REPOS:
        pasta = os.path.join(tmp, repo)
        os.makedirs(pasta)
        subprocess.run(["curl", "-sSf", "-o", f"{pasta}/packagesite.pkg",
                        f"{SITE}/{repo}/packagesite.pkg"], check=True)
        subprocess.run(["bsdtar", "-xf", "packagesite.pkg"], cwd=pasta,
                       check=True)
        for linha in open(f"{pasta}/packagesite.yaml"):
            d = json.loads(linha)
            d["repo"] = repo
            idx.setdefault(d["name"], d)
    return idx


def main():
    destino, pedidos = sys.argv[1], sys.argv[2:]
    os.makedirs(destino, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        idx = catalogo(tmp)
    fila = []
    for p in pedidos:
        achados = fnmatch.filter(idx, p) if "*" in p else [p]
        if not achados:
            sys.exit(f"nenhum pacote para o padrão: {p}")
        fila.extend(achados)
    escolhidos = {}
    while fila:
        nome = fila.pop()
        if nome in escolhidos:
            continue
        if nome not in idx:
            sys.exit(f"pacote não encontrado: {nome}")
        escolhidos[nome] = idx[nome]
        fila.extend((idx[nome].get("deps") or {}).keys())
    for d in escolhidos.values():
        arq = os.path.join(destino, f"{d['name']}-{d['version']}.pkg")
        if os.path.exists(arq) and soma_pkg(arq) == d["sum"]:
            continue
        url = f"{SITE}/{d['repo']}/{d.get('path') or d.get('repopath')}"
        subprocess.run(["curl", "-sSf", "-o", arq, url], check=True)
        if soma_pkg(arq) != d["sum"]:
            os.remove(arq)
            sys.exit(f"checksum não confere: {d['name']}")
        print(f"  {d['name']}-{d['version']}")
    print(f"{len(escolhidos)} pacotes conferidos em {destino}")


main()

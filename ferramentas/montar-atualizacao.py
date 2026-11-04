"""Gera o pacote de atualização do LumeBSD (para "lume atualizar").

uso: montar-atualizacao.py STAGE VERSAO SAIDA
     [--substituir CAMINHO ARQUIVO]...   (só para testes: troca um arquivo)

STAGE   saída de "installworld distribution installkernel -DNO_ROOT"
        (usa o METALOG original do build, sem o que é só do pendrive)
VERSAO  versão completa, por exemplo 1.1.1
SAIDA   diretório onde ficam:
          LumeBSD-VERSAO-sistema.tar.zst  o sistema base inteiro, com donos,
                                          permissões e flags do METALOG
          atualizacao.txt                 o índice: versão, arquivo, SHA256

O pacote traz /usr/share/lume/versao e /usr/share/lume/sistema.mtree (a
lista do sistema, que o "lume atualizar" usa para saber o que mudou, o que
saiu e o que é configuração do usuário).
"""
import hashlib
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sistema import com_originais, extras_lume, linhas_do_build  # noqa: E402


def main():
    args = sys.argv[1:]
    trocas = []
    while "--substituir" in args:
        i = args.index("--substituir")
        trocas.append((args[i + 1].lstrip("/"), os.path.abspath(args[i + 2])))
        del args[i:i + 3]
    if len(args) != 3 or not re.fullmatch(r"\d+\.\d+\.\d+", args[1]):
        sys.exit(__doc__)
    stage, versao, saida = os.path.abspath(args[0]), args[1], os.path.abspath(args[2])
    os.makedirs(saida, exist_ok=True)

    cabecalho, linhas = linhas_do_build(stage)
    linhas = com_originais(stage, linhas) + extras_lume(stage, cabecalho, linhas, versao)

    # Arquivos trocados (testes): o pacote leva o arquivo dado no lugar.
    spec_linhas = []
    for l in linhas:
        caminho = l.split()[0]
        for rel, arq in trocas:
            if caminho == "./" + rel:
                l = re.sub(r" (size|sha256digest|md5digest)=\S+", "", l)
                l += f" contents={arq}"
        spec_linhas.append(l)

    spec = os.path.join(saida, "pacote.mtree")
    with open(spec, "w") as f:
        f.write(cabecalho + "\n" + "\n".join(spec_linhas) + "\n")
    nome = f"LumeBSD-{versao}-sistema.tar.zst"
    pacote = os.path.join(saida, nome)
    if os.path.exists(pacote):
        os.remove(pacote)
    subprocess.run(["bsdtar", "-c", "--zstd", "--options", "zstd:compression-level=12",
                    "-f", pacote, "@" + spec], cwd=stage, check=True)
    os.remove(spec)

    h = hashlib.sha256()
    with open(pacote, "rb") as f:
        for bloco in iter(lambda: f.read(1 << 20), b""):
            h.update(bloco)
    with open(os.path.join(saida, "atualizacao.txt"), "w") as f:
        f.write(f"versao={versao}\narquivo={nome}\nsha256={h.hexdigest()}\n"
                f"tamanho={os.path.getsize(pacote)}\n")
    print(f"{nome}: {os.path.getsize(pacote) // 1024 ** 2} MiB, "
          f"{len(linhas)} entradas, sha256 {h.hexdigest()}")


if __name__ == "__main__":
    main()

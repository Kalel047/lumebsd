"""Partes comuns ao pendrive e ao pacote de atualização: a lista do sistema.

O METALOG do "installworld -DNO_ROOT" diz cada arquivo do sistema base, com
dono, permissões, flags e a tag "config" nos arquivos de configuração. O
LumeBSD guarda essa lista (sem tamanhos e hashes) em
/usr/share/lume/sistema.mtree e a versão completa em /usr/share/lume/versao.
"""
import os
import re
import shutil

RAIZ = "uname=root gname=wheel"
LISTA = "usr/share/lume/sistema.mtree"
VERSAO = "usr/share/lume/versao"


def linhas_do_build(stage):
    """Cabeçalho e linhas do METALOG original do build.

    O montar-pendrive acrescenta ao METALOG o que é só do pendrive; a cópia
    METALOG.build guarda o original na primeira vez.
    """
    metalog = os.path.join(stage, "METALOG")
    original = metalog + ".build"
    if not os.path.exists(original):
        shutil.copy(metalog, original)
    linhas = open(original).read().splitlines()
    return linhas[0], [l for l in linhas[1:] if l.strip()]


def extras_lume(stage, cabecalho, linhas, versao):
    """Grava versao e sistema.mtree no stage e devolve as linhas deles.

    A lista inclui as duas entradas, para o sistema instalado (e o
    "lume atualizar") saberem que esses arquivos são do sistema.
    """
    os.makedirs(os.path.join(stage, "usr/share/lume"), exist_ok=True)
    with open(os.path.join(stage, VERSAO), "w") as f:
        f.write(versao + "\n")
    novas = [f"./{VERSAO} type=file {RAIZ} mode=0444 tags=package=lume",
             f"./{LISTA} type=file {RAIZ} mode=0444 tags=package=lume"]
    caminhos = {l.split()[0] for l in novas}
    base = [l for l in linhas if l.split()[0] not in caminhos]
    sistema = [cabecalho] + [re.sub(r" (size|sha256digest|md5digest|time)=\S+", "", l)
                             for l in base + novas]
    with open(os.path.join(stage, LISTA), "w") as f:
        f.write("\n".join(sistema) + "\n")
    return novas


def com_originais(stage, linhas):
    """Linhas do METALOG apontando, onde preciso, para o arquivo original.

    O pendrive troca a mensagem de boas-vindas no stage; a original fica em
    etc/motd.template.build e é ela que vai no pacote de atualização.
    """
    motd = os.path.join(stage, "etc/motd.template.build")
    saida = []
    for l in linhas:
        if l.split()[0] == "./etc/motd.template" and os.path.exists(motd):
            l = re.sub(r" (size|sha256digest|md5digest)=\S+", "", l)
            l += f" contents={motd}"
        saida.append(l)
    return saida

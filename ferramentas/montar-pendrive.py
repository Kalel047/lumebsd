"""Monta a imagem do pendrive do LumeBSD (ao vivo + instalador).

uso: montar-pendrive.py STAGE PACOTES FERRAMENTAS IMAGEM

STAGE        saída de "installworld distribution installkernel -DNO_ROOT"
             (com o METALOG)
PACOTES      diretório com os pacotes essenciais (.pkg): pkg, driver de
             vídeo, firmwares e microcódigo
FERRAMENTAS  diretório com makefs, mkimg e kldxref do build
             (obj/.../tmp/legacy/bin)
IMAGEM       arquivo de saída

O pendrive é um LumeBSD completo que dá boot sem tocar no disco do
computador. Ele leva:
  - /usr/share/lume/sistema.mtree: a lista dos arquivos do sistema (do
    METALOG), que o "lume instalar" usa para copiar o sistema limpo;
  - /usr/share/lume/pacotes: os pacotes essenciais, instalados no disco;
  - os mesmos drivers e firmwares já extraídos, para testar o hardware ao
    vivo (lume hardware, lume video testar, lume wifi).
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sistema import RAIZ, extras_lume, linhas_do_build  # noqa: E402

stage, pacotes, ferramentas, imagem = (os.path.abspath(a) for a in sys.argv[1:5])
if len(sys.argv) > 5:
    VERSAO = sys.argv[5]
else:
    VERSAO = open(os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "VERSAO")).read().strip()
METALOG = os.path.join(stage, "METALOG")
# Sempre parte do METALOG do build, mesmo se o script rodar de novo.
linhas_do_build(stage)
shutil.copy(METALOG + ".build", METALOG)


def ler_metalog():
    linhas = open(METALOG).read().splitlines()
    return linhas[0], [l for l in linhas[1:] if l.strip()]


def caminho(linha):
    return linha.split()[0]


def escrever(rel, conteudo, modo="0644"):
    """Escreve um arquivo no stage e devolve a linha do METALOG."""
    dst = os.path.join(stage, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w") as f:
        f.write(conteudo)
    return f"./{rel} type=file {RAIZ} mode={modo}"


cabecalho, linhas = ler_metalog()

# 1. Lista do sistema para o instalador e o "lume atualizar" (o METALOG sem
#    tamanhos e hashes, incluindo a própria lista) e a versão completa:
#    /usr/share/lume/sistema.mtree e /usr/share/lume/versao.
novas = extras_lume(stage, cabecalho, linhas, VERSAO)
# A mensagem de boas-vindas do sistema instalado (a do stage é trocada pela
# do pendrive no passo 4; guarda a original na primeira vez).
MOTD = os.path.join(stage, "etc/motd.template")
if not os.path.exists(MOTD + ".build"):
    shutil.copy(MOTD, MOTD + ".build")
shutil.copy(MOTD + ".build", os.path.join(stage, "usr/share/lume/motd.template"))
novas.append(f"./usr/share/lume/motd.template type=file {RAIZ} mode=0444")

# 2. Pacotes essenciais: copiados para o instalador e extraídos para uso ao
#    vivo (menos o próprio pkg, que só faz falta no disco instalado).
destino = os.path.join(stage, "usr/share/lume/pacotes")
os.makedirs(destino, exist_ok=True)
for nome in sorted(os.listdir(pacotes)):
    if not nome.endswith(".pkg"):
        continue
    shutil.copy(os.path.join(pacotes, nome), destino)
    novas.append(f"./usr/share/lume/pacotes/{nome} type=file {RAIZ} mode=0444")
    if nome.startswith("pkg-"):
        continue
    arq = os.path.join(pacotes, nome)
    subprocess.run(["bsdtar", "-xf", arq, "--exclude", "+*", "-C", stage],
                   check=True)
    listagem = subprocess.run(["bsdtar", "-tvf", arq], check=True,
                              capture_output=True, text=True).stdout
    for l in listagem.splitlines():
        partes = l.split(None, 8)
        if len(partes) < 9 or partes[8].lstrip("/").startswith("+"):
            continue
        rel = partes[8].lstrip("/")
        if partes[0].startswith("d"):
            novas.append(f"./{rel.rstrip('/')} type=dir {RAIZ} mode=0755")
        elif partes[0].startswith("-"):
            modo = "0555" if "x" in partes[0] else "0444"
            novas.append(f"./{rel} type=file {RAIZ} mode={modo}")
        elif partes[0].startswith("l") and " -> " in rel:
            nome_link, alvo = rel.split(" -> ", 1)
            novas.append(f"./{nome_link} type=link {RAIZ} mode=0755 "
                         f"link={alvo}")

# 3. Índice dos módulos de terceiros (o kldload acha as dependências).
subprocess.run([os.path.join(ferramentas, "kldxref"),
                os.path.join(stage, "boot/modules")], check=True)
novas.append(f"./boot/modules/linker.hints type=file {RAIZ} mode=0444")

# 4. Configuração do modo ao vivo.
novas += [
    escrever("etc/lume-ao-vivo", "LumeBSD rodando a partir do pendrive.\n"),
    escrever("etc/fstab", "/dev/gpt/lumeroot\t/\tufs\trw,noatime\t1\t1\n"),
    escrever("etc/rc.conf", """hostname="lume-ao-vivo"
ifconfig_DEFAULT="DHCP"
dumpdev="NO"
kld_list="acpi_ibm coretemp"
devmatch_blocklist="if_iwx if_iwm"
lume_configurar_enable="NO"
lume_farol_enable="YES"
lume_notebook_enable="YES"
lume_video_enable="NO"
vigia_enable="YES"
"""),
    escrever("boot/loader.conf", """# LumeBSD 1.1 - pendrive ao vivo
boot_multicons="YES"
console="efi,comconsole"
autoboot_delay="5"
kern.geom.label.disk_ident.enable="0"
kern.geom.label.gptid.enable="0"
cpu_microcode_load="YES"
cpu_microcode_name="/boot/firmware/intel-ucode.bin"
compat.linuxkpi.iwlwifi_11n_disable="0"
compat.linuxkpi.iwlwifi_disable_11ac="0"
"""),
    escrever("etc/motd.template", """
   LumeBSD 1.1 "Farol"  -  pendrive ao vivo

   Nada no disco deste computador foi alterado.

     lume hardware        o que funciona aqui (comece por aqui)
     lume video testar    testa o driver de vídeo (NVIDIA ou Intel)
     lume wifi            conecta no Wi-Fi
     lume instalar        instala o LumeBSD no disco inteiro (APAGA TUDO)

   Você está como root, sem senha. Desligar: shutdown -p now

"""),
]

# 5. Junta tudo: as linhas novas substituem as antigas do mesmo caminho, e
#    diretórios que faltam no METALOG são criados.
novos_caminhos = {caminho(l) for l in novas}
final = [l for l in linhas if caminho(l) not in novos_caminhos] + novas
vistos = {caminho(l) for l in final}
dirs = []
for l in final:
    partes = caminho(l).split("/")
    for i in range(2, len(partes)):
        d = "/".join(partes[:i])
        if d not in vistos:
            vistos.add(d)
            dirs.append(f"{d} type=dir {RAIZ} mode=0755")
# Pais antes dos filhos: ordena (de forma estável) pela profundidade.
final = sorted(final + dirs,
               key=lambda l: 0 if caminho(l) == "." else caminho(l).count("/"))
with open(METALOG, "w") as f:
    f.write(cabecalho + "\n" + "\n".join(final) + "\n")

# 6. Sistema de arquivos UFS e imagem GPT (UEFI e BIOS).
usado = int(subprocess.run(["du", "-sk", stage], capture_output=True,
                           text=True, check=True).stdout.split()[0])
tamanho_mb = usado // 1024 * 5 // 4 + 768
saida = os.path.dirname(os.path.abspath(imagem))
ufs = os.path.join(saida, "pendrive-raiz.ufs")
fat = os.path.join(saida, "pendrive-efi.fat")
efi = os.path.join(saida, "pendrive-efi")
for f in (ufs, fat, imagem):
    if os.path.exists(f):
        os.remove(f)
subprocess.run([os.path.join(ferramentas, "makefs"), "-t", "ffs", "-B",
                "little", "-o", "version=2,label=lumeroot,softupdates=1",
                "-s", f"{tamanho_mb}m", "-F", "METALOG", "-N", "etc", ufs,
                "."], cwd=stage, check=True)
shutil.rmtree(efi, ignore_errors=True)
os.makedirs(os.path.join(efi, "EFI/BOOT"))
shutil.copy(os.path.join(stage, "boot/loader.efi"),
            os.path.join(efi, "EFI/BOOT/BOOTX64.EFI"))
subprocess.run([os.path.join(ferramentas, "makefs"), "-t", "msdos", "-o",
                "fat_type=32,sectors_per_cluster=1,volume_label=EFISYS",
                "-s", "64m", fat, efi], check=True)
subprocess.run([os.path.join(ferramentas, "mkimg"), "-s", "gpt", "-f", "raw",
                "-b", os.path.join(stage, "boot/pmbr"),
                "-p", f"efi/efiboot:={fat}",
                "-p", "freebsd-boot/gptboot:=" +
                os.path.join(stage, "boot/gptboot"),
                "-p", f"freebsd-ufs/lumeroot:={ufs}",
                "-o", imagem], check=True)
os.remove(ufs)
os.remove(fat)
shutil.rmtree(efi)
print(f"imagem: {imagem} ({os.path.getsize(imagem) // 1024 ** 2} MiB, "
      f"sistema {usado // 1024} MiB)")

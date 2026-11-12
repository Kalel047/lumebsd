"""Monta a imagem do pendrive do LumeBSD (ao vivo + instalador).

uso: montar-pendrive.py [--iso ISO] STAGE PACOTES FERRAMENTAS IMAGEM [VERSAO]

STAGE        saída de "installworld distribution installkernel -DNO_ROOT"
             (com o METALOG)
PACOTES      diretório com os pacotes essenciais (.pkg): pkg, driver de
             vídeo, firmwares e microcódigo
FERRAMENTAS  diretório com makefs, mkimg e kldxref do build
             (obj/.../tmp/legacy/bin)
IMAGEM       arquivo de saída
--iso ISO    também gera a mesma coisa em .iso híbrida (DVD ou pendrive,
             UEFI ou BIOS): a raiz é o próprio cd9660, só de leitura, e
             /etc, /var, /root, /mnt e /media vão para a memória no boot (o
             rc.initdiskless do FreeBSD)

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

args = sys.argv[1:]
ISO = None
if args[:1] == ["--iso"]:
    ISO = os.path.abspath(args[1])
    args = args[2:]
stage, pacotes, ferramentas, imagem = (os.path.abspath(a) for a in args[:4])
if len(args) > 4:
    VERSAO = args[4]
else:
    VERSAO = open(os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "VERSAO")).read().strip()
SERIE = VERSAO.rsplit(".", 1)[0]     # 1.2.0 -> 1.2
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
    escrever("boot/loader.conf", f"""# LumeBSD {SERIE} - pendrive ao vivo
boot_multicons="YES"
# Só "efi": o comconsole falha num PC sem porta serial ("console comconsole
# failed to initialize", repetido na tela). Numa máquina com porta serial, o
# firmware já a põe no ConOut, e o kernel a usa de segunda (hw.uart.console).
console="efi"
# A tela é o console principal: sem isto, numa máquina virtual com porta
# serial o loader UEFI segue o firmware, que põe a serial na frente (veja
# também /boot/lua/local.lua, para o BIOS).
boot_serial="NO"
# Numa máquina virtual em BIOS, o console usaria o modo texto do VGA, cuja
# fonte não tem ã nem õ ("n?o"); o modo gráfico tem a fonte inteira.
hw.vga.textmode="0"
autoboot_delay="5"
kern.geom.label.disk_ident.enable="0"
kern.geom.label.gptid.enable="0"
cpu_microcode_load="YES"
cpu_microcode_name="/boot/firmware/intel-ucode.bin"
compat.linuxkpi.iwlwifi_11n_disable="0"
compat.linuxkpi.iwlwifi_disable_11ac="0"
"""),
    # Em BIOS (SeaBIOS, o padrão do virt-manager e do GNOME Boxes), o loader
    # faz da serial o console principal sempre que "comconsole" está na
    # lista: as mensagens do boot e o rc iriam para ela, e a tela ficaria só
    # com o cursor. Lá, só "vidconsole"; a serial continua de segunda
    # (boot_multicons). O loader.lua roda este arquivo depois do loader.conf.
    escrever("boot/lua/local.lua", """-- LumeBSD: a tela é o console principal também em BIOS.
local core = require("core")
if not core.isUEFIBoot() then
	loader.setenv("console", "vidconsole")
end
""", "0444"),
    escrever("etc/motd.template", f"""
   LumeBSD {SERIE} "Farol"  -  pendrive ao vivo

   Nada no disco deste computador foi alterado.

     lume hardware        o que funciona aqui (comece por aqui)
     lume video testar    testa o driver de vídeo (Intel, AMD ou NVIDIA)
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


# 7. A .iso híbrida: o mesmo sistema ao vivo, com a raiz no cd9660.
def gerar_iso(iso):
    rotulo = "LUMEBSD"
    fstab = os.path.join(stage, "etc/fstab")
    rcconf = os.path.join(stage, "etc/rc.conf")
    orig = {f: open(f).read() for f in (fstab, rcconf)}
    extras = [
        escrever("etc/fstab", f"/dev/iso9660/{rotulo}\t/\tcd9660\tro\t0\t0\n"),
        escrever("etc/rc.conf", orig[rcconf] +
                 '# Na .iso a raiz é só de leitura; /etc, /var e /root ficam na\n'
                 '# memória (/etc/diskless e /conf/base) e o /tmp também.\n'
                 'root_rw_mount="NO"\ntmpmfs="YES"\ntmpsize="512m"\n'),
        escrever("etc/diskless", ""),
    ]
    # /conf/base/DIR: "remount" põe o DIR original (só de leitura) por baixo
    # do modelo; o rc.initdiskless cria um disco na memória do tamanho de
    # md_size (setores de 512 bytes) e copia tudo para ele.
    # /mnt e /media: onde o instalador e o usuário montam discos.
    for d, setores in (("etc", 131072), ("var", 1048576), ("root", 16384),
                       ("mnt", 2048), ("media", 2048)):
        extras.append(escrever(f"conf/base/{d}/remount", f"mount_nullfs /{d}\n"))
        extras.append(escrever(f"conf/base/{d}/md_size", f"{setores}\n"))
    for d in ("conf", "conf/base", "conf/base/etc", "conf/base/var",
              "conf/base/root", "conf/base/mnt", "conf/base/media"):
        extras.append(f"./{d} type=dir {RAIZ} mode=0755")
    trocados = {caminho(l) for l in extras}
    linhas_iso = [l for l in final if caminho(l) not in trocados] + extras
    linhas_iso.sort(key=lambda l: 0 if caminho(l) == "." else caminho(l).count("/"))
    meta_iso = os.path.join(stage, "METALOG.iso")
    with open(meta_iso, "w") as f:
        f.write(cabecalho + "\n" + "\n".join(linhas_iso) + "\n")

    # O ESP do boot UEFI vai dentro da .iso (El Torito).
    esp_dir = os.path.join(saida, "iso-efi")
    esp = os.path.join(saida, "iso-efi.fat")
    shutil.rmtree(esp_dir, ignore_errors=True)
    os.makedirs(os.path.join(esp_dir, "EFI/BOOT"))
    shutil.copy(os.path.join(stage, "boot/loader.efi"),
                os.path.join(esp_dir, "EFI/BOOT/BOOTX64.EFI"))
    if os.path.exists(esp):
        os.remove(esp)
    subprocess.run([os.path.join(ferramentas, "makefs"), "-t", "msdos", "-o",
                    "fat_type=12,sectors_per_cluster=1,volume_label=EFISYS",
                    "-s", "2048k", esp, esp_dir], check=True)
    # O mesmo ESP da .iso do FreeBSD: FAT12 de 2 MB, com o sistema de
    # arquivos do tamanho exato do arquivo (outros tamanhos o makefs faz
    # menores que o arquivo, ou vazios, e o UEFI não acha o boot).
    with open(esp, "rb") as f:
        setor = f.read(512)
    if setor[510:512] != b"\x55\xaa" or int.from_bytes(setor[19:21], "little") * 512 != os.path.getsize(esp):
        sys.exit("montar-pendrive: o ESP da .iso saiu errado")
    if os.path.exists(iso):
        os.remove(iso)
    try:
        subprocess.run([os.path.join(ferramentas, "makefs"), "-t", "cd9660",
                        "-o", "rockridge", "-o", f"label={rotulo}",
                        "-o", "publisher=LumeBSD https://github.com/Kalel047/lumebsd",
                        "-o", "bootimage=i386;boot/cdboot", "-o", "no-emul-boot",
                        "-o", f"bootimage=i386;{esp}", "-o", "no-emul-boot",
                        "-o", "platformid=efi",
                        "-F", "METALOG.iso", "-N", "etc", iso, "."],
                       cwd=stage, check=True)
    finally:
        for f, c in orig.items():
            open(f, "w").write(c)
        os.remove(os.path.join(stage, "etc/diskless"))
        shutil.rmtree(os.path.join(stage, "conf"))
        os.remove(meta_iso)
    # Híbrida: GPT com o boot BIOS (isoboot) e o ESP na área de sistema da
    # .iso, para gravar num pendrive com dd (como a do FreeBSD).
    et = subprocess.run([os.path.join(ferramentas, "etdump"), "-f",
                         "shell", iso], capture_output=True, text=True,
                        check=True).stdout
    espparam = []
    for linha in et.split():
        campos = dict(c.split("=", 1) for c in linha.split(";") if "=" in c)
        if campos.get("et_platform") == "efi":
            inicio = int(campos["et_lba"]) * 2048
            tamanho = int(campos["et_sectors"]) * 512
            espparam = ["-p", f"efi::{tamanho}:{inicio}"]
            break
    hibrida = os.path.join(saida, "iso-hibrida.img")
    subprocess.run([os.path.join(ferramentas, "mkimg"), "-s", "gpt",
                    "--capacity", str(os.path.getsize(iso)),
                    "-b", os.path.join(stage, "boot/pmbr"),
                    "-p", "freebsd-boot:=" + os.path.join(stage, "boot/isoboot")]
                   + espparam + ["-o", hibrida], check=True)
    with open(hibrida, "rb") as h, open(iso, "r+b") as f:
        f.write(h.read(32 * 1024))
    os.remove(hibrida)
    os.remove(esp)
    shutil.rmtree(esp_dir)
    print(f"iso: {iso} ({os.path.getsize(iso) // 1024 ** 2} MiB)")


if ISO:
    gerar_iso(ISO)

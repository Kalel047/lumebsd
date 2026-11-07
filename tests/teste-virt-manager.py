"""Testa o LumeBSD numa máquina virtual como a que o virt-manager cria.

uso: teste-virt-manager.py bios|uefi LOG

O virt-manager (e o GNOME Boxes), no Fedora e em outros, criam por padrão:
máquina q35, firmware SeaBIOS (BIOS), disco SATA, CD-ROM SATA com a .iso e
uma porta serial. Foi assim que três problemas apareceram na 1.5 e foram
corrigidos na 1.5.1:

  - em BIOS, o sistema ao vivo mandava o rc para a serial (a tela ficava só
    com o cursor);
  - em BIOS, o disco instalado não dava boot (só havia o carregador UEFI);
  - em UEFI com porta serial, o firmware põe a serial na frente, e o
    assistente do primeiro boot aparecia nela, não na tela.

O teste instala a partir da .iso (pela serial, como root, sem o desktop) e dá
boot pelo disco, como uma pessoa faria: a senha do disco e as respostas do
assistente vão pelo teclado da VM, lendo a tela por OCR (tesseract). As fotos
da tela ficam em LUME_TESTE_DIR.

Variáveis: LUME_ISO (a .iso), LUME_TESTE_DIR, LUME_KVM=1, OVMF_CODE,
OVMF_VARS.
"""
import os
import select
import shutil
import socket
import subprocess
import sys
import time

TOPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.environ.get("LUME_TESTE_DIR", os.path.join(TOPO, "teste-virt-manager"))
_VERSAO = ".".join(open(os.path.join(TOPO, "VERSAO")).read().split(".")[:2])
ISO = os.environ.get("LUME_ISO") or os.path.join(TOPO, f"LumeBSD-{_VERSAO}-Farol.iso")
OVMF_CODE = os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd")
OVMF_VARS = os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd")
SENHA_DISCO = "farol-cripto-2026"
SENHA = "farol123"

modo, logpath = sys.argv[1], sys.argv[2]
assert modo in ("bios", "uefi"), __doc__
os.makedirs(DIR, exist_ok=True)
DISCO = f"{DIR}/disco-{modo}.img"
VARS = f"{DIR}/vars-{modo}.fd"
MON = f"{DIR}/monitor-{modo}.sock"
log = open(logpath, "wb")
t0 = time.time()
p = None
buf = b""


def marca(txt):
    print(f"[{round(time.time() - t0):5d} s] {txt}", flush=True)


def ligar(com_iso):
    """Liga a VM: q35, SATA, serial; com a .iso no CD-ROM na instalação."""
    global p, buf
    args = ["qemu-system-x86_64", "-machine", "q35", "-m", "4096", "-smp", "2",
            "-drive", f"if=none,id=disco,format=raw,file={DISCO}",
            "-device", "ide-hd,bus=ide.0,drive=disco,bootindex=1",
            "-nic", "user,model=e1000e", "-vga", "std", "-display", "none",
            "-serial", "stdio", "-monitor", f"unix:{MON},server,nowait", "-no-reboot"]
    if modo == "uefi":
        args += ["-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
                 "-drive", f"if=pflash,format=raw,file={VARS}"]
    if com_iso:
        args += ["-drive", f"if=none,id=cd,media=cdrom,format=raw,file={ISO}",
                 "-device", "ide-cd,bus=ide.1,drive=cd,bootindex=0"]
    if os.environ.get("LUME_KVM"):
        args += ["-enable-kvm", "-cpu", "host"]
    if os.path.exists(MON):
        os.remove(MON)
    buf = b""
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    time.sleep(2)


def ler(timeout):
    global buf
    r, _, _ = select.select([p.stdout], [], [], timeout)
    if r:
        d = os.read(p.stdout.fileno(), 65536)
        if d:
            buf += d
            log.write(d)
            log.flush()
            return True
    return False


def esperar(pat, timeout=300):
    global buf
    fim = time.time() + timeout
    while time.time() < fim:
        if pat.encode() in buf:
            buf = buf[buf.index(pat.encode()) + len(pat):]
            return
        ler(1)
    falhou(f"esperando {pat!r} na serial")


def falhou(txt):
    marca(f"FALHOU {txt}")
    try:
        foto("falhou")
    except Exception:
        pass
    p.kill()
    sys.exit(1)


def enviar(s):
    time.sleep(0.3)
    dados = (s + "\r").encode()
    for i in range(0, len(dados), 32):
        p.stdin.write(dados[i:i + 32])
        p.stdin.flush()
        time.sleep(0.05)


def comando(c, prompt, timeout=180):
    enviar(c)
    esperar(prompt, timeout)


def monitor(cmd):
    s = socket.socket(socket.AF_UNIX)
    s.connect(MON)
    s.recv(4096)
    s.sendall((cmd + "\n").encode())
    time.sleep(0.4)
    s.close()


def foto(nome):
    ppm = f"{DIR}/{modo}-{nome}.ppm"
    monitor(f"screendump {ppm}")
    time.sleep(1.5)
    png = f"{DIR}/{modo}-{nome}.png"
    subprocess.run(["python3", "-c", f"from PIL import Image; Image.open('{ppm}').save('{png}')"],
                   check=False)
    os.remove(ppm)
    return png


def tela():
    """O texto da tela da VM (OCR)."""
    png = foto("ocr")
    r = subprocess.run(["tesseract", png, "-", "--psm", "6"], capture_output=True, text=True)
    return r.stdout


def esperar_tela(pats, timeout):
    """Espera um dos textos aparecer na tela (não na serial)."""
    fim = time.time() + timeout
    while time.time() < fim:
        while ler(0.2):
            pass
        t = tela()
        for x in pats:
            if x.lower() in t.lower():
                return x, t
        time.sleep(4)
    falhou(f"esperando {pats!r} na tela")


TECLAS = {" ": "spc", "-": "minus", "/": "slash", ".": "dot", "_": "shift-minus",
          ",": "comma", "=": "equal", "\n": "ret", ";": "semicolon",
          "|": "shift-backslash", ":": "shift-semicolon"}


def digitar(texto):
    """Digita pelo teclado da VM (o loader e o console usam o layout US)."""
    for ch in texto:
        if ch in TECLAS:
            k = TECLAS[ch]
        elif ch.isupper():
            k = "shift-" + ch.lower()
        else:
            k = ch
        monitor(f"sendkey {k}")
        time.sleep(0.08)


# --- 1. a .iso: o sistema ao vivo e a instalação -----------------------------
for f in (DISCO, VARS):
    if os.path.exists(f):
        os.remove(f)
subprocess.run(["truncate", "-s", "32G", DISCO], check=True)
if modo == "uefi":
    shutil.copy(OVMF_VARS, VARS)

marca(f"{modo}: ligando pela .iso ({os.path.basename(ISO)})")
ligar(True)
# O rc agora vai para a tela (console principal); a serial é a segunda e
# ganha um login.
esperar("login: ", 1800)
marca("ao vivo: a serial chegou ao login")
x, texto = esperar_tela(["(ttyv0)", "lume configurar"], 300)
f = foto("1-aovivo")
marca(f"ao vivo: a tela mostra o sistema ({x!r}) -> {f}")
P = "root@lume-ao-vivo:~ # "
enviar("root")
esperar(P, 120)
for c in ["sysctl machdep.bootmethod", "sysctl kern.console",
          "dmesg | grep 'Dual Console'", "sysctl -n kern.disks"]:
    comando(c, P)
saida = open(logpath, "rb").read().decode(errors="replace")
if "Serial Primary" in saida:
    falhou("o sistema ao vivo ainda está com a serial como console principal")
marca("ao vivo: a tela é o console principal")

marca("instalando no ada0 (sem o desktop)")
enviar(f"env LUME_DISCO=ada0 LUME_CONFIRMA=APAGAR LUME_SENHA_DISCO={SENHA_DISCO}"
       " LUME_DESKTOP=n LUME_KIT= lume instalar")
esperar("LumeBSD instalado em", 7200)
marca("instalação concluída")
esperar(P, 120)
for c in ["gpart show -l ada0", "grep -iE 'erro|error|fail' /tmp/lume-instalar.log | head",
          "zpool import -N -R /mnt -f zroot 2>/dev/null; true",
          "efibootmgr 2>&1 | head -5"]:
    comando(c, P, 300)
enviar("shutdown -p now")
esperar("Uptime", 300)
p.wait(120)

# --- 2. o disco instalado, sem a .iso ----------------------------------------
marca(f"{modo}: ligando pelo disco")
inicio = log.tell()
ligar(False)
# A senha do disco: em BIOS, o gptzfsboot só pergunta na tela; em UEFI, o
# loader.efi pergunta na tela (e na serial, pelo firmware).
x, _ = esperar_tela(["Passphrase"], 900)
foto("2-senha-do-disco")
marca("o carregador pediu a senha do disco (na tela)")
time.sleep(1)
digitar(SENHA_DISCO + "\n")

# O assistente do primeiro boot: na tela, não na serial. Sem o desktop, ele
# pede um Enter antes de começar (e pula sozinho em 120 s).
x, texto = esperar_tela(["Pressione Enter", "primeiro boot"], 1800)
f = foto("3-assistente")
marca(f"o assistente apareceu na tela -> {f}")
if "n?o" in texto or "n7o" in texto:
    falhou("a tela está sem o ã (modo texto do VGA)")
log.flush()
if "Nova senha do root" in open(logpath, "rb").read()[inicio:].decode(errors="replace"):
    falhou("o assistente foi para a serial")
digitar("\n")
esperar_tela(["Nova senha do root"], 120)
# Teclado: o US (3), para o teclado da VM digitar o que o teste manda.
for resposta in [SENHA, SENHA, "kalel", "", SENHA, SENHA, "farol", "", "3", "1", "s"]:
    digitar(resposta + "\n")
    time.sleep(3)
x, texto = esperar_tela(["login:"], 900)
f = foto("4-login")
marca(f"configurado: a tela chegou ao login -> {f}")
# Pela tela, como uma pessoa: entra como root e confere o console e o usuário.
digitar("root\n")
time.sleep(4)
digitar(SENHA + "\n")
time.sleep(6)
digitar("clear; sysctl machdep.bootmethod kern.console; dmesg | grep -i console; id kalel\n")
time.sleep(6)
x, texto = esperar_tela(["uid="], 120)
f = foto("5-conferencia")
marca(f"conferência na tela -> {f}")
print(texto, flush=True)
if "machdep.bootmethod: " + modo.upper() not in texto.replace("machdep,bootmethod", "machdep.bootmethod"):
    marca(f"aviso: o OCR não leu machdep.bootmethod: {modo.upper()} (veja a foto)")
if "Serial Primary" in texto:
    falhou("o sistema instalado está com a serial como console principal")
digitar("shutdown -p now\n")
p.wait(300)
marca(f"{modo}: tudo certo")

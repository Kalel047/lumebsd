"""Testa o pendrive do LumeBSD no QEMU como se fosse um PC.

UEFI (OVMF), pendrive ligado em SATA, disco de destino NVMe (nda0), vídeo
VGA com framebuffer UEFI, console serial para conversar.

uso: python3 tests/teste-pendrive.py FASE LOG
  fumaca     boota o pendrive sem gravar nada nele e confere os comandos
  aovivo     boota o pendrive, roda lume hardware/video, instala no NVMe
  instalado  boota o NVMe: senha do disco, assistente, verificações
  desktop    (depois de instalado) login gráfico, fotos da tela
  forja      (depois de instalado) binário, compilação com opções, atualizar
  ajustar    entra como root no instalado e roda os comandos de $AJUSTES

Variáveis: LUME_IMG (a imagem), LUME_TESTE_DIR (onde ficam disco NVMe,
fotos e o socket do monitor), OVMF_CODE/OVMF_VARS, LUME_KVM=1 para usar
aceleração, LUME_REPO_URL (espelho de pacotes para "aovivo").
O pendrive vai em SATA porque o USB emulado do QEMU dá erros de leitura
sob carga; para o sistema é o mesmo disco.
"""
import os
import select
import shutil
import socket
import subprocess
import sys
import time

TOPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.environ.get("LUME_TESTE_DIR", os.path.join(TOPO, "teste-pendrive"))
PEN = os.environ.get("LUME_IMG", os.path.join(TOPO, "LumeBSD-1.1-Farol-pendrive.img"))
OVMF_CODE = os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd")
OVMF_VARS = os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd")
NVME = f"{DIR}/nvme.img"
VARS = f"{DIR}/OVMF_VARS.fd"
MON = f"{DIR}/monitor.sock"
SENHA_DISCO = "farol-cripto-2026"
REPO = os.environ.get("LUME_REPO_URL", "http://10.0.2.2:8000")

fase, logpath = sys.argv[1], sys.argv[2]
os.makedirs(DIR, exist_ok=True)
if fase == "aovivo" or not os.path.exists(NVME):
    if os.path.exists(NVME):
        os.remove(NVME)
    subprocess.run(["truncate", "-s", "40G", NVME], check=True)
    shutil.copy(OVMF_VARS, VARS)

args = ["qemu-system-x86_64", "-machine", "q35", "-m", "4096", "-smp", "2",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
        "-drive", f"if=pflash,format=raw,file={VARS}",
        "-drive", f"if=none,id=nv,format=raw,file={NVME}",
        "-device", "nvme,serial=LUMENVME,drive=nv",
        "-device", "qemu-xhci,id=xhci",
        "-nic", "user,model=e1000e",
        "-vga", "std", "-display", "none",
        "-serial", "stdio", "-monitor", f"unix:{MON},server,nowait",
        "-no-reboot"]
if os.environ.get("LUME_REINICIAR"):
    # Deixa o QEMU reiniciar (pânico -> reboot), para testar a volta automática.
    args.remove("-no-reboot")
if os.environ.get("LUME_KVM"):
    args += ["-enable-kvm", "-cpu", "host"]
if fase in ("aovivo", "fumaca"):
    # fumaca: boota a imagem sem gravar nada nela (snapshot do QEMU).
    extra = ",snapshot=on" if fase == "fumaca" else ""
    args += ["-drive", f"if=none,id=pen,format=raw,file={PEN}{extra}",
             # USB emulado (umass no qemu-xhci) dá erros de leitura sob carga
             # no QEMU; o mesmo disco via SATA testa a mesma imagem.
             "-device", "ide-hd,bus=ide.0,drive=pen,bootindex=0"]

log = open(logpath, "wb")
p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT)
buf = b""
t0 = time.time()


def esperar(pat, timeout=300):
    global buf
    pat = pat.encode()
    fim = time.time() + timeout
    while time.time() < fim:
        if pat in buf:
            buf = buf[buf.index(pat) + len(pat):]
            return True
        r, _, _ = select.select([p.stdout], [], [], 1)
        if r:
            d = os.read(p.stdout.fileno(), 65536)
            if not d:
                break
            buf += d
            log.write(d)
            log.flush()
    print(f"FALHOU esperando {pat!r} ({round(time.time() - t0)} s)", flush=True)
    p.kill()
    sys.exit(1)


def enviar(s):
    time.sleep(0.3)
    p.stdin.write((s + "\r").encode())
    p.stdin.flush()


def esperar_um(pats, timeout=300):
    """Espera o primeiro de vários padrões; devolve qual apareceu."""
    global buf
    fim = time.time() + timeout
    while time.time() < fim:
        achados = [(buf.index(x.encode()), x) for x in pats if x.encode() in buf]
        if achados:
            i, x = min(achados)
            buf = buf[i + len(x.encode()):]
            return x
        r, _, _ = select.select([p.stdout], [], [], 1)
        if r:
            d = os.read(p.stdout.fileno(), 65536)
            if not d:
                break
            buf += d
            log.write(d)
            log.flush()
    print(f"FALHOU esperando {pats!r} ({round(time.time() - t0)} s)", flush=True)
    p.kill()
    sys.exit(1)


def senha_do_disco(depois):
    """Digita a senha do GELI; discos instalados antes da correção pedem 2x."""
    esperar("GELI Passphrase", 600)
    marca("loader pediu a senha do disco")
    time.sleep(1)
    enviar(SENHA_DISCO)
    if esperar_um(["PPaasssspphhrraassee", depois], 900) != depois:
        marca("loader pediu a senha DE NOVO (geom_eli_passphrase_prompt)")
        time.sleep(1)
        enviar(SENHA_DISCO)
        esperar(depois, 900)


def comando(c, prompt, timeout=120):
    enviar(c)
    esperar(prompt, timeout)


def monitor(cmd):
    s = socket.socket(socket.AF_UNIX)
    s.connect(MON)
    s.recv(4096)
    s.sendall((cmd + "\n").encode())
    time.sleep(1)
    s.close()


def foto(nome):
    ppm = f"{DIR}/{nome}.ppm"
    monitor(f"screendump {ppm}")
    time.sleep(1)
    subprocess.run(["python3", "-c", f"""
from PIL import Image
Image.open('{ppm}').save('{DIR}/{nome}.png')
"""], check=False)
    print(f"foto: {DIR}/{nome}.png", flush=True)


def marca(txt):
    print(f"[{round(time.time() - t0):4d} s] {txt}", flush=True)


if fase == "aovivo":
    P = "root@lume-ao-vivo:~ # "
    esperar("login: ", 900)
    marca("pendrive bootou até o login")
    enviar("root")
    esperar(P, 120)
    foto("aovivo-console")
    for c in ["uname -a", "lume hardware", "lume video testar", "lume video",
              "ls /usr/share/lume/pacotes", "kldstat | tail -5"]:
        comando(c, P, 180)
    marca("instalando no NVMe")
    enviar(f"env LUME_DISCO=nda0 LUME_CONFIRMA=APAGAR LUME_SENHA_DISCO={SENHA_DISCO}"
           f" LUME_DESKTOP={os.environ.get('LUME_TESTE_DESKTOP', 's')} LUME_CONSOLE_SERIAL=1 LUME_REPO_URL={REPO}"
           " lume instalar")
    esperar("LumeBSD instalado em", 7200)
    marca("instalação concluída")
    esperar(P, 60)
    for c in ["cat /tmp/lume-instalar.log | grep -iE 'erro|error|fail' | head -20",
              "gpart show -l nda0", "efibootmgr"]:
        comando(c, P)
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "fumaca":
    P = "root@lume-ao-vivo:~ # "
    # Com um LumeBSD já instalado no NVMe, o loader do pendrive pergunta a
    # senha daquele disco; Enter pula.
    vezes = 0
    while esperar_um(["GELI Passphrase", "login: "], 900) != "login: ":
        vezes += 1
        marca(f"loader perguntou a senha do disco instalado ({vezes}); Enter")
        time.sleep(1)
        enviar("")
    marca("pendrive bootou até o login")
    enviar("root")
    esperar(P, 120)
    for c in ["uname -a", "lume versao", "lume hardware",
              "kldstat | grep -c ko", "for f in /usr/sbin/lume-* /usr/sbin/forja; do sh -n $f || echo ERRO $f; done; echo scripts ok",
              "grep -c sddm_xkb_layout /usr/sbin/lume-configurar", "vigia", "forja"]:
        comando(c, P, 180)
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "instalado":
    senha_do_disco("Pressione Enter para começar")
    marca("sistema bootou; assistente apareceu")
    enviar("")
    for pat, resp in [("Nova senha do root: ", "farol123"), ("Repita a senha: ", "farol123"),
                      ("Login: ", "fabiano"), ("Nome completo [fabiano]: ", "Fabiano"),
                      ("Senha de fabiano: ", "lume2026"), ("Repita a senha: ", "lume2026"),
                      ("Nome [farol]: ", "thinkpad"), ("Fuso [America/Sao_Paulo]: ", ""),
                      ("Opção [1]: ", ""), ("Opção [1]: ", ""),
                      ("Aplicar estas configurações? (s/n) [s]: ", "")]:
        esperar(pat)
        enviar(resp)
    esperar("login: ", 900)
    marca("login do sistema instalado")
    time.sleep(20)
    foto("instalado-tela")
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    for c in ["grep -c geom_eli_passphrase_prompt /boot/loader.conf; "
              "sed -i '' '/geom_eli_passphrase_prompt/d' /boot/loader.conf",
              "uname -a", "zpool status zroot | head -12", "zfs list -o name,used,mountpoint",
              "bectl list", "geli status", "swapinfo -h", "lume", "vigia",
              "lume video", "pkg info | wc -l", "pkg info drm-612-kmod plasma6-plasma-workspace | grep -E 'Name|Version'",
              "service sddm status; pgrep -lf Xorg", "id fabiano", "lume hardware"]:
        comando(c, P, 180)
    foto("instalado-sddm")
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "ajustar":
    # Entra como root pelo serial e roda os comandos de AJUSTES (um por linha).
    # Responde a senha do disco quantas vezes ela for pedida (um pânico com
    # LUME_REINICIAR=1 faz o computador reiniciar e pedir de novo).
    boots = 0
    while esperar_um(["GELI Passphrase", "login: "], 1800) != "login: ":
        boots += 1
        marca(f"boot {boots}: senha do disco")
        time.sleep(1)
        enviar(SENHA_DISCO)
    marca(f"login depois de {boots} boot(s)")
    # O prompt inteiro: saídas como a do getfacl também têm "# ".
    P = os.environ.get("PROMPT", "root@thinkpad:~ # ")
    enviar("root")
    r = esperar_um(["Password:", P], 60)
    if r == "Password:":
        enviar(os.environ.get("SENHA_ROOT", "farol123"))
        esperar(P, 60)
    for c in os.environ["AJUSTES"].splitlines():
        if c.strip():
            comando(c, P, int(os.environ.get("AJUSTES_TEMPO", "300")))
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "desktop":
    senha_do_disco("login: ")
    marca("sistema no login; esperando o SDDM desenhar")
    time.sleep(90)
    foto("1-login")
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    marca("senha digitada na tela de login")
    time.sleep(240)
    foto("2-plasma")
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    comando("pgrep -lf 'Xorg|plasmashell|kwin' | cut -c1-80", P)
    # Abre programas na sessão do fabiano (mesmo DISPLAY, Xauthority e D-Bus).
    sessao = ("pid=$(pgrep -u fabiano plasmashell); "
              "eval $(procstat -e $pid | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)=' | sed 's/^/export /'); ")
    comando(sessao + "setxkbmap -query", P)
    comando(sessao + "su -m fabiano -c 'env HOME=/home/fabiano daemon -f konsole -e sh -c \"lume; lume farol; exec sh\"'", P)
    time.sleep(90)
    foto("3-konsole")
    comando(sessao + "su -m fabiano -c 'env HOME=/home/fabiano daemon -f dolphin /home/fabiano'", P)
    time.sleep(90)
    foto("4-dolphin")
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "forja":
    senha_do_disco("login: ")
    marca("sistema instalado no login")
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    passos = [
        ("grep -q MASTER_SITE_OVERRIDE /etc/make.conf || echo 'MASTER_SITE_OVERRIDE?= http://10.0.2.2:8000/distfiles/' >> /etc/make.conf", 60),
        ("time env FORJA_ARVORE_URL=http://10.0.2.2:8000/ports-2026Q4.tar.gz forja arvore", 1800),
        ("forja instalar htop", 300),
        ("forja opcoes nano", 120),
        ("forja opcoes nano -NLS -DOCS", 120),
        ("cat /etc/forja/opcoes | grep -v '^#'", 30),
        ("pkg delete -y -f pkgconf > /dev/null; pkg info -e pkgconf || echo 'pkgconf ausente'", 120),
        ("time forja instalar nano", 3600),
        ("pkg query '%n %v %At' pkgconf nano", 60),
        ("nano --version | head -3", 30),
        ("forja mundo", 120),
        ("forja info nano", 60),
        ("forja opcoes nano +NLS", 120),
        ("time forja atualizar", 3600),
        ("nano --version | grep -o 'disable-nls' || echo 'NLS ligado'", 30),
        ("forja mundo", 120),
        ("bectl list", 30),
        ("pkg lock -l", 30),
    ]
    if os.environ.get("PULAR_ARVORE"):
        passos = [x for x in passos if "forja arvore" not in x[0]]
    for c, t in passos:
        comando(c, P, t)
    enviar("shutdown -p now")
    esperar("Uptime", 300)

print(f"OK em {round(time.time() - t0)} s", flush=True)
p.kill()

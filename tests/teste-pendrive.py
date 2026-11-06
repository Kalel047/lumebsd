"""Testa o pendrive do LumeBSD no QEMU como se fosse um PC.

UEFI (OVMF), pendrive ligado em SATA, disco de destino NVMe (nda0), vídeo
VGA com framebuffer UEFI, console serial para conversar.

uso: python3 tests/teste-pendrive.py FASE LOG
  fumaca     boota o pendrive sem gravar nada nele e confere os comandos
  aovivo     boota o pendrive, roda lume hardware/video, instala no NVMe
  instalado  boota o NVMe: senha do disco, assistente, verificações
  desktop    (depois de instalado) login gráfico, fotos da tela
  forja      (depois de instalado) binário, compilação com opções, atualizar
  tempo      (depois do desktop) lume tempo: fotos, versões, recuperar,
             mudou, voltar e a janela "Versões anteriores" do Dolphin
  app        (depois do desktop, com lume_atualizar_url apontando para uma
             versão nova e o xdotool instalado) o app Atualizações do
             LumeBSD: aviso na bandeja, novidades, senha, instalação,
             reiniciar
  loja       (depois do instalado, com o kit geral) os Programas do LumeBSD:
             kits, busca pelo programa conhecido, instalar com senha, e um
             .exe com dois cliques (oferece o Wine, instala, abre); a
             camada Linux e a Ajuda
  exe        (depois do loja) .exe de 64 bits abre no Wine; o de 32 bits
             ganha a explicação
  eink       (depois do instalado) o filtro e-ink (papel e tinta; a tela
             cheia com cor; Ctrl+Meta+T), as animações carregadas, a loja
             nova, um .exe de 32 bits (instala o Wine e a parte de 32 bits
             do espelho em REPO32) e um .dmg do Mac (a loja procurando o
             substituto)
  sessao     entra na sessão gráfica e roda, como root, os comandos de
             $AJUSTES (um por linha; para diagnóstico)
  ajustar    entra como root no instalado e roda os comandos de $AJUSTES

Variáveis: LUME_IMG (a imagem), LUME_TESTE_DIR (onde ficam disco NVMe,
fotos e o socket do monitor), OVMF_CODE/OVMF_VARS, LUME_KVM=1 para usar
aceleração, LUME_REPO_URL (espelho de pacotes para "aovivo"), PREPARO
(comandos antes do teste "tempo"), LUME_TESTE_KIT (o kit que o "aovivo"
instala junto com o desktop; sem ela, o instalador pergunta... e o teste
responde o padrão).
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
# Sem LUME_IMG, a imagem da versão atual (a que o build-lumebsd.sh acaba de gerar).
_VERSAO = ".".join(open(os.path.join(TOPO, "VERSAO")).read().split(".")[:2])
PEN = os.environ.get("LUME_IMG") or os.path.join(TOPO, f"LumeBSD-{_VERSAO}-Farol-pendrive.img")
OVMF_CODE = os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd")
OVMF_VARS = os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd")
NVME = f"{DIR}/nvme.img"
VARS = f"{DIR}/OVMF_VARS.fd"
MON = f"{DIR}/monitor.sock"
SENHA_DISCO = "farol-cripto-2026"
REPO = os.environ.get("LUME_REPO_URL", "http://10.0.2.2:8000")

fase, logpath = sys.argv[1], sys.argv[2]
os.makedirs(DIR, exist_ok=True)
if fase in ("aovivo", "iso") or not os.path.exists(NVME):
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
if fase in ("iso", "isousb"):
    # A .iso: num DVD (iso) ou gravada num pendrive com dd (isousb, a mesma
    # imagem como disco: o GPT híbrido).
    ISO = os.environ["LUME_ISO"]
    if fase == "iso":
        args += ["-drive", f"if=none,id=dvd,media=cdrom,format=raw,file={ISO}",
                 "-device", "ide-cd,bus=ide.0,drive=dvd,bootindex=0"]
    else:
        args += ["-drive", f"if=none,id=pen,format=raw,file={ISO},snapshot=on",
                 "-device", "ide-hd,bus=ide.0,drive=pen,bootindex=0"]
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
    # Em pedaços: linhas longas mandadas de uma vez perdem letras no
    # console serial do FreeBSD emulado.
    time.sleep(0.3)
    dados = (s + "\r").encode()
    for i in range(0, len(dados), 32):
        p.stdin.write(dados[i:i + 32])
        p.stdin.flush()
        time.sleep(0.05)


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


def achou(pat, timeout):
    """Como esperar, mas sem desistir do teste: verdadeiro se apareceu."""
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
    return False


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
           f" LUME_KIT={os.environ.get('LUME_TESTE_KIT', '')}"
           " lume instalar")
    esperar("LumeBSD instalado em", 7200)
    marca("instalação concluída")
    esperar(P, 60)
    for c in ["cat /tmp/lume-instalar.log | grep -iE 'erro|error|fail' | head -20",
              "gpart show -l nda0", "efibootmgr"]:
        comando(c, P)
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase in ("iso", "isousb"):
    P = "root@lume-ao-vivo:~ # "
    esperar("login: ", 900)
    marca(f"{fase}: bootou até o login")
    enviar("root")
    esperar(P, 120)
    foto(f"{fase}-console")
    for c in ["uname -a", "mount", "df -h / /etc /var /tmp /root",
              "touch /etc/teste-escrita /var/teste /root/teste && echo escreve-ok",
              "glabel status | grep -iE 'iso9660|lumeroot'; sysctl kern.disks",
              "lume hardware", "lume video", "ls /usr/share/lume/pacotes | wc -l"]:
        comando(c, P, 180)
    if fase == "iso":
        marca("instalando no NVMe a partir do DVD")
        enviar(f"env LUME_DISCO=nda0 LUME_CONFIRMA=APAGAR LUME_SENHA_DISCO={SENHA_DISCO}"
               f" LUME_DESKTOP={os.environ.get('LUME_TESTE_DESKTOP', 'n')} LUME_CONSOLE_SERIAL=1 LUME_REPO_URL={REPO}"
               f" LUME_KIT={os.environ.get('LUME_TESTE_KIT', '')}"
               " lume instalar")
        esperar("LumeBSD instalado em", 7200)
        marca("instalação concluída")
        esperar(P, 60)
        for c in ["grep -iE 'erro|error|fail' /tmp/lume-instalar.log | head -20",
                  "gpart show -l nda0"]:
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
                      ("Login: ", "kalel"), ("Nome completo [kalel]: ", "Kalel"),
                      ("Senha de kalel: ", "lume2026"), ("Repita a senha: ", "lume2026"),
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
              "service sddm status; pgrep -lf Xorg", "id kalel", "lume hardware"]:
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
    # Abre programas na sessão do kalel (mesmo DISPLAY, Xauthority e D-Bus).
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    comando(sessao + "setxkbmap -query", P)
    comando(sessao + "su -m kalel -c 'env HOME=/home/kalel daemon -f konsole -e sh -c \"lume; lume farol; exec sh\"'", P)
    time.sleep(90)
    foto("3-konsole")
    comando(sessao + "su -m kalel -c 'env HOME=/home/kalel daemon -f dolphin /home/kalel'", P)
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

elif fase == "tempo":
    senha_do_disco("login: ")
    marca("sistema no login; esperando o SDDM")
    time.sleep(90)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    marca("senha digitada na tela de login")
    time.sleep(240)
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    # PREPARO: comandos antes do teste (um por linha), como trocar um script.
    for c in os.environ.get("PREPARO", "").splitlines():
        if c.strip():
            comando(c, P, 300)
    F = "su - kalel -c"
    D = "Documentos/contrato.txt"
    passos = [
        "grep lume-tempo /var/log/cron | tail -4",
        "lume tempo",
        "lume-tempo automatico; zfs get -r -s local -o name,value lume:tempo",
        f"{F} 'mkdir -p Documentos; echo \"versão 1: rascunho\" > {D}'",
        "lume tempo foto 'teste: versão 1'",
        f"{F} 'echo \"versão 2: revisado\" > {D}; echo foto > Documentos/foto.jpg'",
        "lume tempo antes 'teste: versão 2'",
        f"{F} 'echo \"versão 3: final\" > {D}; rm Documentos/foto.jpg'",
        f"{F} 'lume tempo versoes {D}; lume tempo versoes Documentos/foto.jpg'",
        f"{F} 'lume tempo recuperar Documentos/foto.jpg; cat Documentos/foto.jpg'",
        f"{F} 'lume tempo recuperar {D} 2; ls Documentos'",
        f"{F} 'lume tempo recuperar {D} --substituir; cat {D}; ls Documentos'",
        "lume tempo linha",
        "lume tempo mudou 2",
        "lume | tail -4",
    ]
    for c in passos:
        comando(c, P, 300)
    # A janela "Versões anteriores", na sessão do kalel.
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    comando("ls -l /usr/local/share/kio/servicemenus/", P)
    comando(sessao + "su -m kalel -c 'env HOME=/home/kalel daemon -f dolphin /home/kalel/Documentos'", P)
    time.sleep(60)
    # O botão direito no primeiro arquivo (contrato.txt), com o xdotool se
    # houver (PREPARO pode instalar); senão, a tecla Menu.
    clique = ("w=$(xdotool search --onlyvisible --class dolphin | tail -1); "
              "xdotool windowactivate --sync $w mousemove --window $w 228 104 click 3")
    comando(sessao + f"if command -v xdotool >/dev/null; then su -m kalel -c '{clique}'; "
            "else echo sem-xdotool; fi", P)
    time.sleep(10)
    foto("5-dolphin-menu")
    monitor("sendkey esc")
    comando(sessao + f"su -m kalel -c 'env HOME=/home/kalel daemon -f lume-tempo gui /home/kalel/{D}'", P)
    time.sleep(40)
    foto("6-versoes-anteriores")
    monitor("sendkey down")  # a versão mais velha
    monitor("sendkey ret")
    time.sleep(15)
    foto("7-o-que-fazer")
    monitor("sendkey down")  # "Recuperar ao lado da atual"
    monitor("sendkey ret")
    time.sleep(15)
    foto("8-recuperado")
    monitor("sendkey ret")
    time.sleep(20)
    foto("9-dolphin-depois")
    comando(f"{F} 'ls -l Documentos; head Documentos/*'", P)
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "app":
    senha_do_disco("login: ")
    marca("sistema no login; esperando o SDDM")
    time.sleep(90)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    marca("senha digitada na tela de login")
    # O aviso da bandeja sai uns 90 s depois da entrada e fica 15 s.
    time.sleep(110)
    for i in range(8):
        foto(f"app-1-bandeja-{i}")
        time.sleep(9)
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    X = sessao + "su -m kalel -c "
    comando("pgrep -lf lume-atualizacoes; ls /usr/local/share/applications/lume-atualizacoes.desktop /usr/local/share/polkit-1/actions/org.lumebsd.atualizar.policy", P)
    comando(X + "'env HOME=/home/kalel daemon -f lume-atualizacoes'", P)
    time.sleep(60)
    foto("app-2-nova")
    comando("ANTES=$(grep -c 'pronto: .* ativado' /var/log/lume-atualizar.log); echo $ANTES", P)
    # O botão principal (Instalar, depois Reiniciar agora) fica no canto de
    # baixo à direita da janela.
    janela = ("w=$(xdotool search --onlyvisible --classname lume-atualizacoes | tail -1); "
              "xdotool windowactivate --sync $w; eval $(xdotool getwindowgeometry --shell $w); "
              "xdotool mousemove --window $w $((WIDTH - 71)) $((HEIGHT - 36)) click 1")
    comando(X + f"'{janela}'", P)   # Instalar
    time.sleep(25)
    foto("app-3-senha")
    comando(X + "'xdotool type --delay 120 lume2026; xdotool key Return'", P)
    time.sleep(40)
    foto("app-4-instalando")
    # Espera o lume-atualizar terminar (a linha "pronto" no registro).
    comando("i=0; until grep -q 'pronto: .* ativado' /var/log/lume-atualizar.log 2>/dev/null && "
            "[ $(grep -c 'pronto: .* ativado' /var/log/lume-atualizar.log) -gt ${ANTES:-0} ]; do "
            "i=$((i+1)); [ $i -gt 300 ] && break; sleep 5; done; tail -3 /var/log/lume-atualizar.log", P, 1700)
    time.sleep(15)
    foto("app-5-pronto")
    comando("bectl list", P)
    # "Reiniciar agora", a confirmação (o botão padrão) e, se o polkit pedir
    # de novo, a senha.
    comando(X + f"'{janela}'", P)
    time.sleep(8)
    foto("app-6-reiniciar")
    # (Daqui em diante o computador pode desligar a qualquer momento: sem
    # esperar o prompt.)
    enviar(X + "'xdotool key Return'")
    if not achou("Uptime", 25):
        enviar(X + "'xdotool search --onlyvisible --name Autentica >/dev/null && "
               "{ xdotool type --delay 120 lume2026; xdotool key Return; }'")
    if not achou("Uptime", 300):
        marca("o app não reiniciou o computador; desligando pelo console")
        enviar("shutdown -p now")
        esperar("Uptime", 300)
    else:
        marca("o app reiniciou o computador")

elif fase == "loja":
    senha_do_disco("login: ")
    marca("sistema no login; esperando o SDDM")
    time.sleep(90)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    marca("senha digitada na tela de login")
    time.sleep(200)
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    # PREPARO: comandos antes do teste (um por linha), como trocar um script.
    for c in os.environ.get("PREPARO", "").splitlines():
        if c.strip():
            comando(c, P, 300)
    X = sessao + "su -m kalel -c "
    H = "env HOME=/home/kalel "
    for c in ["lume loja", "lume loja procurar matlab", "lume loja procurar excel",
              "cat /var/db/lume/kits; ls /usr/local/share/applications | grep lume",
              "pkg info -e libreoffice pt_BR-libreoffice webfonts vlc xournalpp && echo kit-geral-ok",
              "grep -i ms-dos /usr/local/etc/xdg/mimeapps.list",
              "lume compat", "lume loja info qalculate",
              "ASSUME_ALWAYS_YES=yes pkg install -q xdotool >/dev/null; pkg info -e xdotool && echo xdotool-ok",
              # A VM lenta repete as teclas do xdotool ("keynoooote").
              X + "'xset r off; xset q | grep -i repeat'"]:
        comando(c, P, 600)

    def janela(cmd):
        comando(X + "'w=$(xdotool search --onlyvisible --classname lume-programas | tail -1); "
                f"xdotool windowactivate --sync $w; {cmd}'", P)

    def senha():
        comando(X + "'xdotool search --onlyvisible --name Autentica >/dev/null && "
                "{ xdotool type --delay 120 lume2026; xdotool key Return; }; true'", P)

    # A loja: o início com os kits e a busca pelo programa conhecido.
    comando(X + f"'{H}daemon -f lume-programas'", P)
    time.sleep(60)
    foto("loja-1-inicio")
    janela("xdotool type --delay 150 matlab")
    time.sleep(10)
    foto("loja-2-matlab")
    janela("xdotool key --delay 80 BackSpace BackSpace BackSpace BackSpace BackSpace BackSpace; xdotool type --delay 150 autocad")
    time.sleep(10)
    foto("loja-3-autocad")
    comando("pkill -f lume-programas; true", P)

    # Instalar com a senha: o Qalculate!, pelo app.
    comando(X + f"'{H}daemon -f lume-programas --instalar qalculate'", P)
    time.sleep(60)
    foto("loja-4-confirmar")
    janela("xdotool key Return")
    time.sleep(25)
    foto("loja-5-senha")
    senha()
    time.sleep(30)
    foto("loja-6-instalando")
    comando("i=0; until pkg info -e qalculate-qt && ! pgrep -f lume-programas >/dev/null; do "
            "i=$((i+1)); [ $i -gt 240 ] && break; sleep 5; done; lume loja info qalculate", P, 1500)
    comando(X + f"'{H}daemon -f lume-programas'", P)
    time.sleep(50)
    janela("xdotool mousemove --window $w 125 124 click 1")
    time.sleep(10)
    foto("loja-7-instalados")
    comando("pkill -f lume-programas; true", P)

    # Um programa do Windows com dois cliques: o atalho do sistema oferece o
    # Wine, a loja instala (com a senha) e o programa abre.
    comando(f"mkdir -p /home/kalel/Downloads && fetch -q -o /home/kalel/Downloads/calculadora.exe {REPO}/teste/calculadora.exe && "
            "chown -R kalel /home/kalel/Downloads && ls -l /home/kalel/Downloads", P)
    comando(X + f"'{H}daemon -f kioclient exec /home/kalel/Downloads/calculadora.exe'", P)
    time.sleep(40)
    foto("loja-8-wine-pergunta")
    comando(X + "'xdotool search --onlyvisible --name \"Programa do Windows\" windowactivate --sync key Return'", P)
    time.sleep(60)
    foto("loja-9-wine-confirmar")
    janela("xdotool key Return")
    time.sleep(25)
    senha()
    time.sleep(60)
    foto("loja-10-wine-instalando")
    comando("i=0; until pkg info -e wine; do i=$((i+1)); [ $i -gt 480 ] && break; sleep 5; done; "
            "pkg info -e wine && echo wine-ok", P, 2600)
    marca("Wine instalado; esperando o programa abrir")
    comando(X + "'i=0; until xdotool search --onlyvisible --name \"Calculadora de teste\" >/dev/null; do "
            "i=$((i+1)); [ $i -gt 240 ] && break; sleep 5; done; true'", P, 1300)
    time.sleep(10)
    foto("loja-11-exe")

    # A camada Linux e a Ajuda.
    comando("lume compat linux ligar; lume compat; kldstat | grep -i linux", P, 300)
    comando(X + f"'{H}daemon -f lume-ajuda'", P)
    time.sleep(120)
    foto("loja-12-ajuda")
    comando(X + f"'{H}daemon -f lume-ajuda trocando'", P)
    time.sleep(40)
    foto("loja-13-trocando")
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "exe":
    senha_do_disco("login: ")
    time.sleep(90)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    time.sleep(200)
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    for c in os.environ.get("PREPARO", "").splitlines():
        if c.strip():
            comando(c, P, 300)
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    X = sessao + "su -m kalel -c "
    H = "env HOME=/home/kalel "
    comando(f"mkdir -p /home/kalel/Downloads; for e in calculadora calculadora32; do fetch -q -o /home/kalel/Downloads/$e.exe {REPO}/teste/$e.exe; done; "
            "chown kalel /home/kalel/Downloads/*; lume compat", P)
    # O caminho dos dois cliques (o mimeapps.list) já foi testado na fase
    # loja; aqui o lume-compat direto, com o registro do Wine.
    comando("grep -i ms-dos /usr/local/etc/xdg/mimeapps.list", P)
    comando(X + f"'{H}WINEDEBUG=err+all daemon -o /tmp/compat.log /usr/sbin/lume-compat abrir /home/kalel/Downloads/calculadora.exe'", P)
    comando(X + "'i=0; until xdotool search --onlyvisible --name \"Calculadora de teste\" >/dev/null; do "
            "i=$((i+1)); [ $i -gt 72 ] && break; sleep 5; done; xdotool search --onlyvisible --name \"Calculadora de teste\" && echo EXE-ABRIU'", P, 1300)
    time.sleep(10)
    foto("exe-1-64bits")
    comando("tail -40 /tmp/compat.log; ps -axo user,pid,etime,command | grep -i wine | grep -v grep | cut -c1-150; ls /home/kalel/.wine", P)
    comando(X + "'xdotool search --onlyvisible --name . | while read w; do xdotool getwindowname $w; done | sort -u | head -30'", P)
    comando(X + "'xdotool search --onlyvisible --name \"Calculadora de teste\" windowactivate --sync key Return; true'", P)
    comando(X + f"'{H}daemon -f kioclient exec /home/kalel/Downloads/calculadora32.exe'", P)
    time.sleep(40)
    foto("exe-2-32bits")
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "sessao":
    senha_do_disco("login: ")
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    # ANTES: comandos antes do login gráfico (variáveis da sessão, logs).
    for c in os.environ.get("ANTES", "").splitlines():
        if c.strip():
            comando(c, P, 300)
    time.sleep(30)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    time.sleep(220)
    for c in os.environ.get("AJUSTES", "").splitlines():
        if c.startswith("FOTO "):
            foto(c[5:].strip())
        elif c.strip():
            comando(c, P, int(os.environ.get("AJUSTES_TEMPO", "600")))
    foto("sessao")
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase == "eink":
    REPO32 = os.environ.get("REPO32", "http://10.0.2.2:8001")
    senha_do_disco("login: ")
    marca("sistema no login; esperando o SDDM")
    time.sleep(90)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    marca("senha digitada na tela de login")
    time.sleep(220)
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    for c in os.environ.get("PREPARO", "").splitlines():
        if c.strip():
            comando(c, P, 300)
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    X = sessao + "su -m kalel -c "
    H = "env HOME=/home/kalel "
    Q = "qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects"
    for c in ["ls -l /usr/local/lib/qt6/plugins/kwin-x11/effects/plugins/lume_tinta.so /usr/local/share/kwin-x11/effects/",
              "grep -A3 Compositing /usr/local/etc/xdg/kwinrc; ls /usr/lib32/libc.so.7",
              X + f"'{Q}.isEffectLoaded lume_tinta; {Q}.isEffectLoaded lume_refresh; {Q}.isEffectLoaded lume_esmaecer'",
              X + "'qdbus6 org.kde.KWin /KWin org.kde.KWin.supportInformation | grep -i -E \"compositing|OpenGL renderer|lume\" | head -12'",
              X + f"'{H}lume tema tinta; {H}lume tema animacao'",
              "lume compat",
              "ASSUME_ALWAYS_YES=yes pkg install -q xdotool >/dev/null; pkg info -e xdotool && echo xdotool-ok",
              # A VM lenta repete as teclas do xdotool ("keynoooote").
              X + "'xset r off; xset q | grep -i repeat'"]:
        comando(c, P, 600)

    def janela(nome, cmd):
        comando(X + f"'w=$(xdotool search --onlyvisible --name \"{nome}\" | tail -1); "
                f"xdotool windowactivate --sync $w; {cmd}'", P)

    # O filtro: a foto de cores no Firefox, em janela (papel e tinta) e em
    # tela cheia (cor de verdade).
    comando(f"mkdir -p /home/kalel/Imagens && fetch -q -o /home/kalel/Imagens/cores.png {REPO}/teste/cores.png && chown -R kalel /home/kalel/Imagens", P)
    comando(X + f"'{H}daemon -f firefox --new-window file:///home/kalel/Imagens/cores.png'", P)
    time.sleep(150)
    foto("eink-1-filtro")
    janela("cores.png", "xdotool key F11")
    time.sleep(25)
    foto("eink-2-tela-cheia")
    janela("cores.png", "xdotool key F11")
    time.sleep(20)
    comando(X + "'xdotool key ctrl+super+t'", P)
    time.sleep(15)
    foto("eink-3-desligado")
    comando(X + "'xdotool key ctrl+super+t'", P)
    time.sleep(15)
    comando("pkill -f firefox; true", P)
    time.sleep(10)

    # A loja nova, com o filtro.
    comando(X + f"'{H}daemon -f lume-programas'", P)
    time.sleep(60)
    foto("eink-4-loja")
    janela("Programas do LumeBSD", "xdotool type --delay 150 keynote")
    time.sleep(10)
    foto("eink-5-loja-busca")
    comando("pkill -f lume-programas; true", P)

    # Um .exe de 32 bits sem nada instalado: o Wine pela loja (senha), depois
    # a parte de 32 bits do espelho i386 (sem senha) e o programa.
    comando("mkdir -p /tmp/pkg32-repos /home/kalel/.i386-wine-pkg/tmp/pkg32-repos && "
            f"printf 'espelho32: {{ url: \"{REPO32}\", signature_type: \"fingerprints\", "
            "fingerprints: \"/usr/share/keys/pkg\", enabled: yes }}\\n' > /tmp/pkg32-repos/espelho32.conf && "
            "cp /tmp/pkg32-repos/espelho32.conf /home/kalel/.i386-wine-pkg/tmp/pkg32-repos/ && "
            "chown -R kalel /home/kalel/.i386-wine-pkg && cat /tmp/pkg32-repos/espelho32.conf", P)
    comando(f"mkdir -p /home/kalel/Downloads && fetch -q -o /home/kalel/Downloads/calculadora32.exe {REPO}/teste/calculadora32.exe && "
            "chown -R kalel /home/kalel/Downloads", P)
    comando(X + f"'{H}LUME_PKG32_REPOS=/tmp/pkg32-repos LUME_PKG32_REPO=espelho32 "
            "daemon -o /tmp/compat32.log /usr/sbin/lume-compat abrir /home/kalel/Downloads/calculadora32.exe'", P)
    time.sleep(40)
    foto("eink-6-exe-wine")
    comando(X + "'xdotool search --onlyvisible --name \"Programa do Windows\" windowactivate --sync key Return'", P)
    comando(X + "'sleep 30; xdotool search --onlyvisible --name . | while read w; do xdotool getwindowname $w; done | sort -u'", P)
    # A loja calcula o tamanho (o rquery demora) e só então pergunta.
    comando(X + "'i=0; until xdotool search --onlyvisible --name \"^Instalar\" >/dev/null; do "
            "i=$((i+1)); [ $i -gt 120 ] && break; sleep 5; done; "
            "xdotool search --onlyvisible --name \"^Instalar\" windowactivate --sync key Return && echo confirmou'", P, 700)
    comando(X + "'i=0; until xdotool search --onlyvisible --name Autentica >/dev/null; do "
            "i=$((i+1)); [ $i -gt 60 ] && break; sleep 2; done; true'", P, 200)
    comando(X + "'xdotool search --onlyvisible --name Autentica >/dev/null && "
            "{ xdotool type --delay 120 lume2026; xdotool key Return; }; true'", P)
    comando("i=0; until pkg info -e wine; do i=$((i+1)); [ $i -gt 480 ] && break; sleep 5; done; "
            "pkg info -e wine && echo wine-ok", P, 2600)
    comando(X + "'i=0; until xdotool search --onlyvisible --name \"Programa do Windows\" >/dev/null; do "
            "i=$((i+1)); [ $i -gt 60 ] && break; sleep 3; done; true'", P, 300)
    time.sleep(5)
    foto("eink-7-exe-32bits-pergunta")
    comando(X + "'xdotool search --onlyvisible --name \"Programa do Windows\" windowactivate --sync key Return'", P)
    time.sleep(30)
    foto("eink-8-exe-32bits-baixando")
    comando("i=0; until [ -x /home/kalel/.i386-wine-pkg/usr/local/bin/wine ]; do i=$((i+1)); "
            "[ $i -gt 480 ] && break; sleep 5; done; ls -l /home/kalel/.i386-wine-pkg/usr/local/bin/wine; "
            "tail -5 /home/kalel/.cache/lume-wine32.log", P, 2600)
    comando(X + "'i=0; until xdotool search --onlyvisible --name \"Calculadora de teste\" >/dev/null; do "
            "i=$((i+1)); [ $i -gt 180 ] && break; sleep 5; done; "
            "xdotool search --onlyvisible --name \"Calculadora de teste\" && echo EXE32-ABRIU'", P, 1000)
    time.sleep(10)
    foto("eink-9-exe-32bits")
    comando("tail -20 /tmp/compat32.log", P)
    comando(X + "'xdotool search --onlyvisible --name \"Calculadora de teste\" windowactivate --sync key Return; true'", P)

    # Um .dmg do Mac: a explicação e a loja procurando o substituto.
    comando("printf 'koly' > /home/kalel/Downloads/Keynote-14.2.dmg && chown kalel /home/kalel/Downloads/Keynote-14.2.dmg", P)
    comando(X + f"'{H}daemon -f kioclient exec /home/kalel/Downloads/Keynote-14.2.dmg'", P)
    time.sleep(40)
    foto("eink-10-dmg")
    comando(X + "'xdotool search --onlyvisible --name \"Programa do Mac\" windowactivate --sync key Return'", P)
    time.sleep(60)
    foto("eink-11-dmg-loja")
    comando("pkill -f lume-programas; true", P)
    comando(X + f"'{H}daemon -f lume-ajuda tinta'", P)
    time.sleep(100)
    foto("eink-12-ajuda")
    enviar("shutdown -p now")
    esperar("Uptime", 300)

elif fase in ("app-falhou", "app-voltar"):
    # app-falhou: depois de uma atualização que não passou na checagem (o
    # computador voltou), a tela que explica e "Apagar a tentativa".
    # app-voltar: "Voltar para a versão anterior" e "Reiniciar agora".
    senha_do_disco("login: ")
    time.sleep(90)
    for tecla in "lume2026":
        monitor(f"sendkey {tecla}")
    monitor("sendkey ret")
    time.sleep(200)
    P = "root@thinkpad:~ # "
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    sessao = ("for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); "
              "E=$(procstat -e $pid 2>/dev/null | tr ' ' '\\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)='); [ -n \"$E\" ] && break; done; "
              "eval $(echo \"$E\" | sed 's/^/export /'); ")
    X = sessao + "su -m kalel -c "

    def clicar(dx, dy):
        """Clica num ponto contado do canto de baixo à direita (dx > 0) ou
        de baixo à esquerda (dx < 0) da janela do app."""
        x = f"$((WIDTH - {dx}))" if dx > 0 else f"{-dx}"
        comando(X + "'w=$(xdotool search --onlyvisible --classname lume-atualizacoes | tail -1); "
                "xdotool windowactivate --sync $w; eval $(xdotool getwindowgeometry --shell $w); "
                f"xdotool mousemove --window $w {x} $((HEIGHT - {dy})) click 1'", P)

    def senha():
        comando(X + "'xdotool search --onlyvisible --name Autentica >/dev/null && "
                "{ xdotool type --delay 120 lume2026; xdotool key Return; }; true'", P)

    comando("lume atualizar --maquina estado", P)
    comando(X + "'env HOME=/home/kalel daemon -f lume-atualizacoes'", P)
    time.sleep(60)
    if fase == "app-falhou":
        foto("app-7-falhou")
        clicar(225, 36)                 # Apagar a tentativa
        time.sleep(15)
        senha()
        time.sleep(60)
        foto("app-8-depois")
        comando("bectl list; tail -3 /var/log/lume-atualizar.log", P)
        enviar("shutdown -p now")
        esperar("Uptime", 300)
    else:
        foto("app-9-em-dia")
        clicar(-150, 36)                # Voltar para a versão anterior (...)
        time.sleep(8)
        foto("app-10-voltar")
        comando(X + "'xdotool key Return'", P)
        time.sleep(15)
        senha()
        time.sleep(30)
        foto("app-11-volta-pronta")
        comando("bectl list", P)
        clicar(71, 36)                  # Reiniciar agora
        time.sleep(8)
        foto("app-12-reiniciar")
        enviar(X + "'xdotool key Return'")
        if not achou("Uptime", 25):
            enviar(X + "'xdotool search --onlyvisible --name Autentica >/dev/null && "
                   "{ xdotool type --delay 120 lume2026; xdotool key Return; }'")
        if not achou("Uptime", 300):
            marca("o app não reiniciou o computador")
            sys.exit(1)
        marca("o app reiniciou o computador")

print(f"OK em {round(time.time() - t0)} s", flush=True)
p.kill()

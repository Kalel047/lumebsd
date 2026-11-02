"""Boota o LumeBSD no QEMU e conversa com o console serial.

uso: python3 wizard-test.py IMAGEM LOG roteiro
roteiro "primeiro": responde o assistente de primeiro boot e verifica o sistema
roteiro "segundo": confere que o assistente não aparece de novo
"""
import os
import select
import subprocess
import sys
import time

img, logpath, roteiro = sys.argv[1], sys.argv[2], sys.argv[3]
log = open(logpath, "wb")
p = subprocess.Popen(
    ["qemu-system-x86_64", "-m", "1024", "-smp", "2", "-nographic", "-no-reboot",
     "-drive", f"file={img},format=raw,if=virtio", "-nic", "user,model=virtio-net-pci"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
buf = b""
t0 = time.time()


def esperar(pat, timeout=180):
    global buf
    pat = pat.encode()
    fim = time.time() + timeout
    while time.time() < fim:
        if pat in buf:
            buf = buf[buf.index(pat) + len(pat):]
            return True
        r, _, _ = select.select([p.stdout], [], [], 1)
        if r:
            d = os.read(p.stdout.fileno(), 4096)
            if not d:
                break
            buf += d
            log.write(d)
            log.flush()
    print(f"FALHOU esperando {pat!r} ({round(time.time() - t0)} s)")
    p.kill()
    sys.exit(1)


def enviar(s):
    time.sleep(0.3)
    p.stdin.write((s + "\r").encode())
    p.stdin.flush()


def comando(c, prompt):
    enviar(c)
    esperar(prompt, 60)


if roteiro == "primeiro":
    esperar("Pressione Enter para começar", 1500)
    print(f"assistente apareceu em {round(time.time() - t0)} s")
    enviar("")
    respostas = [
        ("Nova senha do root: ", "curta"),          # rejeitada: menos de 6
        ("pelo menos 6 caracteres", None),
        ("Nova senha do root: ", "farol123"),
        ("Repita a senha: ", "farol123"),
        ("Login: ", "Fabiano"),                      # rejeitado: maiúscula
        ("minúsculas", None),
        ("Login: ", "fabiano"),
        ("Nome completo [fabiano]: ", "Fabiano Silva"),
        ("Senha de fabiano: ", "lume2026"),
        ("Repita a senha: ", "lume2026"),
        ("Nome [farol]: ", "lumebox"),
        ("Fuso [America/Sao_Paulo]: ", ""),
        ("Opção [1]: ", ""),                          # teclado ABNT2
        ("Opção [1]: ", ""),                          # português
        ("Aplicar estas configurações? (s/n) [s]: ", ""),
        ("Pronto! Entre como 'fabiano'", None),
    ]
    for pat, resp in respostas:
        esperar(pat)
        if resp is not None:
            enviar(resp)
    print("assistente concluído")
    esperar("login: ", 600)
    print(f"login em {round(time.time() - t0)} s")
    enviar("fabiano")
    esperar("Password:")
    enviar("lume2026")
    esperar("$ ", 60)
    for c in ["id", "hostname", "locale | head -2", "date", "lume"]:
        comando(c, "$ ")
    enviar("su -")
    esperar("Password:")
    enviar("farol123")
    esperar("# ", 60)
    for c in ["id -un", "df -h /", "swapinfo -h", "sysrc keymap hostname",
              "ls -l /etc/localtime /var/db/lume/configurado", "ls /firstboot"]:
        comando(c, "# ")
    enviar("shutdown -p now")
elif roteiro == "ausente":
    esperar("Pressione Enter para começar", 1500)
    esperar("Configuração adiada", 200)
    print(f"assistente adiado sozinho em {round(time.time() - t0)} s")
    esperar("login: ", 600)
    enviar("root")
    esperar("# ", 60)
    comando("lume", "# ")
    enviar("shutdown -p now")
elif roteiro in ("servicos", "persistencia"):
    P = "lumebox:~ # "
    esperar("login: ", 1500)
    print(f"login em {round(time.time() - t0)} s")
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    if roteiro == "servicos":
        passos = [
            ("vigia", 0),
            ("cp /usr/share/examples/vigia/*.conf /etc/vigia/servicos/ && vigia recarregar", 4),
            ("vigia", 0),
            ("fetch -qo - http://127.0.0.1:8080/", 0),
            ("vigia info web", 0),
            ("vigia log relogio -n 5", 0),
            ("pkill -9 -f 'HTTP/1.0 200' && echo web derrubado", 3),
            ("vigia", 0),
            ("fetch -qo - http://127.0.0.1:8080/", 0),
            ("vigia ativar instavel", 35),
            ("vigia", 0),
            ("vigia log instavel -n 8", 0),
            ("vigia parar relogio", 0),
            ("vigia desativar web", 0),
            ("su -m fabiano -c 'vigia parar ntpd'", 0),
            ("su -m fabiano -c 'vigia log ntpd -n 2'", 0),
            ("echo 'comando = x' > /etc/vigia/servicos/Quebrado.conf; printf 'reiniciar = talvez\\n' > /etc/vigia/servicos/quebrado.conf; vigia recarregar", 0),
            ("vigia", 0),
            ("rm /etc/vigia/servicos/quebrado.conf /etc/vigia/servicos/Quebrado.conf; vigia recarregar", 0),
            ("lume", 0),
        ]
    else:
        passos = [("vigia", 0), ("vigia desativar instavel", 0), ("vigia", 0)]
    for c, pausa in passos:
        comando(c, P)
        if pausa:
            time.sleep(pausa)
    enviar("shutdown -p now")
elif roteiro == "segundo":
    esperar("login: ", 1500)
    print(f"login em {round(time.time() - t0)} s")
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar("# ", 60)
    comando("lume", "# ")
    enviar("shutdown -p now")

esperar("Uptime", 180)
time.sleep(3)
p.kill()
print("OK")

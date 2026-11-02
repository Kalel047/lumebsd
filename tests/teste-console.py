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
elif roteiro == "recursos":
    P = "lumebox:~ # "
    esperar("login: ", 1500)
    print(f"login em {round(time.time() - t0)} s")
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    garfos = ("printf 'descricao = Tenta criar 6 processos com limite de 3\\n"
              "comando = sh -c \"for i in 1 2 3 4 5 6; do sleep 300 & done; wait\"\\n"
              "limite_processos = 3\\n' > /etc/vigia/servicos/garfos.conf")
    eco = ("printf 'descricao = Eco por socket unix\\nescutar = unix:/var/run/eco.sock\\n"
           "comando = sh -c \"read -r x; echo eco: \\\\$x\"\\nusuario = nobody\\n' "
           "> /etc/vigia/servicos/eco.conf")
    finger = ("printf 'descricao = Porta 79 atendida por nobody\\nescutar = tcp:79\\n"
              "comando = sh -c \"echo Ola, \\\\$VIGIA_CLIENTE; id -un\"\\nusuario = nobody\\n' "
              "> /etc/vigia/servicos/finger.conf")
    passos = [
        ("sysctl kern.racct.enable", 0),
        ("cp /usr/share/examples/vigia/*.conf /etc/vigia/servicos/ && vigia recarregar", 4),
        ("vigia", 0),
        ("ps -ax -o pid,class,user,command | grep -E 'CLASS|v-' | grep -v grep", 0),
        ("for i in 1 2 3; do fetch -qo - http://127.0.0.1:8080/; done", 0),
        ("vigia info web", 0),
        ("for i in 1 2 3 4 5; do (sleep 20 | nc 127.0.0.1 8080 > /dev/null &); done; sleep 2; vigia", 0),
        ("rctl | grep v-", 0),
        ("pkill -f 'nc 127.0.0.1 8080'; sleep 2; vigia info web | grep -E 'conex|estado'", 0),
        ("vigia parar relogio", 0),
        ("fetch -qo - http://127.0.0.1:8080/", 0),
        ("vigia iniciar web", 2),
        ("vigia", 0),
        ("vigia reiniciar relogio", 3),
        ("vigia", 0),
        ("vigia ativar pesado", 15),
        ("vigia info pesado | grep -E 'estado|cpu|memória'", 0),
        ("top -b -d 2 -o cpu 4 | tail -5", 0),
        ("vigia desativar pesado", 0),
        (garfos + " && vigia recarregar", 4),
        ("vigia log garfos -n 4", 0),
        ("vigia info garfos | grep -E 'estado|processos'", 0),
        ("vigia parar garfos; sleep 1; pgrep -f 'sleep 300' | wc -l", 0),
        (eco + "; " + finger + "; vigia recarregar", 3),
        ("echo oi | nc -U -N /var/run/eco.sock", 0),
        ("nc -N 127.0.0.1 79 < /dev/null", 0),
        ("vigia", 0),
        ("rm /etc/vigia/servicos/garfos.conf /etc/vigia/servicos/eco.conf /etc/vigia/servicos/finger.conf; vigia recarregar", 2),
        ("lume", 0),
    ]
    for c, pausa in passos:
        comando(c, P)
        if pausa:
            time.sleep(pausa)
    enviar("shutdown -p now")
elif roteiro == "cpu2":
    P = "lumebox:~ # "
    esperar("login: ", 1500)
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    livre = ("printf 'descricao = Igual ao pesado, sem limite\\n"
             "comando = sh -c \"while :; do :; done\"\\nusuario = nobody\\n' "
             "> /etc/vigia/servicos/livre.conf")
    medir = ("ps -ax -o class,time,%cpu,command | grep -E 'v-(pesado|livre)' | grep -v grep")
    alvo = os.environ.get("ALVO_CPU", "20")
    for c, pausa in [("sed -i '' 's/^limite_cpu.*/limite_cpu = " + alvo + "%/' /etc/vigia/servicos/pesado.conf", 0),
                     (livre + " && vigia recarregar && vigia iniciar pesado", 20),
                     (medir, 60),
                     (medir, 0),
                     ("vigia parar pesado; vigia parar livre; rm /etc/vigia/servicos/livre.conf; vigia recarregar", 0)]:
        comando(c, P)
        if pausa:
            time.sleep(pausa)
    enviar("shutdown -p now")
elif roteiro == "cpu":
    P = "lumebox:~ # "
    esperar("login: ", 1500)
    enviar("root")
    esperar("Password:")
    enviar("farol123")
    esperar(P, 60)
    for c, pausa in [("vigia ativar pesado", 40),
                     ("vigia info pesado | grep -E 'cpu|processos'", 0),
                     ("top -b -d 2 -o cpu 3 | tail -4", 0),
                     ("vigia info pesado | grep -E 'cpu|processos'", 0),
                     ("vigia desativar pesado", 0)]:
        comando(c, P)
        if pausa:
            time.sleep(pausa)
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
            ("pkill -9 -f 'while true; do date' && echo relogio derrubado", 3),
            ("vigia", 0),
            ("vigia log relogio -n 6", 0),
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

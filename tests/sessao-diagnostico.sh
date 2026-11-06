#!/bin/sh
# Diagnóstico e testes da sessão gráfica no QEMU (fase "sessao" do
# teste-pendrive.py): roda no LumeBSD como root, baixado do espelho.
# Uso: sh sessao-diagnostico.sh atalho|exe|senha|loja|kdialog|cores|tecla|dbus|
#      teclas|cadeia1..cadeia4|w32|dmg|dmg2|mac|motor
# Diagnóstico da sessão gráfica: atalho do filtro e o fluxo do .exe.
for n in plasmashell kwin_x11 ksmserver kded6; do
	pid=$(pgrep -n -u kalel $n)
	E=$(procstat -e $pid 2>/dev/null | tr ' ' '\n' | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|QT_[A-Z_]*|LANG)=')
	[ -n "$E" ] && break
done
eval "$(echo "$E" | sed 's/^/export /')"
echo "DISPLAY=$DISPLAY via $n"
U="su -m kalel -c"
janelas() { $U 'xdotool search --onlyvisible --name . 2>/dev/null | while read w; do xdotool getwindowname $w; done | sort -u'; }
case "$1" in
atalho)
	echo "--- componentes kglobalaccel"; $U 'qdbus6 org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel.allComponents' 2>&1 | head
	echo "--- ações do kwin com Lume"; $U 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.shortcutNames' 2>&1 | grep -i lume
	$U 'qdbus6 org.kde.kglobalaccel /kglobalaccel org.kde.KGlobalAccel.shortcutKeys kwin LumeTinta' 2>&1 | head -3
	echo "--- invocar LumeTinta pelo D-Bus"; $U 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeTinta' 2>&1
	;;
exe)
	ls -la /home/kalel/Downloads
	$U 'env HOME=/home/kalel daemon -o /tmp/compat32.log sh -x /usr/sbin/lume-compat abrir /home/kalel/Downloads/calculadora32.exe'
	sleep 40; echo "--- janelas 1"; janelas
	$U 'xdotool search --onlyvisible --name "Programa do Windows" windowactivate --sync key Return' 2>&1
	sleep 60; echo "--- janelas 2"; janelas
	ps -axo user,pid,etime,command | grep -E "lume|pkg|kdialog|pkexec|doas|sudo" | grep -v grep | cut -c1-160
	cat /tmp/compat32.log
	;;
esac
case "$1" in
cores)
	$U 'env HOME=/home/kalel daemon -f gwenview /home/kalel/Imagens/cores.png' 2>/dev/null
	$U 'env HOME=/home/kalel daemon -f okular /home/kalel/Imagens/cores.png' 2>/dev/null
	sleep 50; janelas
	;;
tecla)
	$U 'xdotool key --clearmodifiers ctrl+super+t'; sleep 5
	;;
dbus)
	$U 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeTinta'; sleep 5
	;;
teclas)
	grep -i lume /home/kalel/.config/kglobalshortcutsrc
	$U 'xmodmap -pm' 2>&1 | head -12
	;;
esac
case "$1" in
senha)
	$U 'xdotool search --onlyvisible --name "^Instalar" windowactivate --sync key Return' 2>&1; echo "enter: $?"
	sleep 25; echo "--- janelas 3"; janelas
	ps -axo user,pid,etime,command | grep -iE "polkit|pkexec|lume-loja|pkg |agent" | grep -v grep | cut -c1-160
	;;
esac
case "$1" in
loja)
	$U 'env HOME=/home/kalel daemon -o /tmp/loja.log /usr/bin/lume-programas --instalar windows'
	sleep 60; echo "--- janelas loja"; janelas
	ps -axo user,pid,etime,command | grep -E "lume-programas|lume-loja|rquery" | grep -v grep | cut -c1-160
	cat /tmp/loja.log | grep -v -E "canberra|qt.svg" | tail -20
	;;
kdialog)
	$U 'kdialog --title "Programa do Windows" --yes-label Instalar --no-label Cancelar --yesno teste; echo "kdialog=$?" > /tmp/kd.txt' &
	sleep 15; janelas
	$U 'xdotool search --onlyvisible --name "Programa do Windows" windowactivate --sync key Return'; echo "xdo=$?"
	sleep 5; cat /tmp/kd.txt
	;;
esac
espera() { # espera "nome" segundos
	i=0; until $U "xdotool search --onlyvisible --name \"$1\" >/dev/null 2>&1"; do
		i=$((i+5)); [ $i -gt $2 ] && { echo "SEM JANELA: $1"; return 1; }; sleep 5; done
	sleep 3; echo "janela: $1"
}
tecla() { # tenta até a janela sumir (o KWin pode recusar o foco pedido pelo xdotool)
	for t in 1 2 3 4 5 6; do
		$U "w=\$(xdotool search --onlyvisible --name \"$1\" | tail -1); [ -n \"\$w\" ] || exit 0; xdotool windowactivate --sync \$w; xdotool windowfocus --sync \$w; xdotool key --window \$w $2; xdotool key $2" 2>/dev/null
		sleep 4
		$U "xdotool search --onlyvisible --name \"$1\" >/dev/null 2>&1" || { echo "tecla ok ($t)"; return 0; }
	done; echo "tecla falhou: $1"; }
case "$1" in
cadeia1)
	$U 'xset r off'
	$U 'env HOME=/home/kalel LUME_PKG32_REPOS=/tmp/pkg32-repos LUME_PKG32_REPO=espelho32 daemon -o /tmp/compat32.log /usr/sbin/lume-compat abrir /home/kalel/Downloads/calculadora32.exe'
	espera "Programa do Windows" 120 && tecla "Programa do Windows" Return
	pkg info -e wine || { espera "^Instalar" 600 && tecla "^Instalar" Return; }
	;;
cadeia2)
	espera "Autentica" 120 && { $U 'xdotool type --delay 150 lume2026'; $U 'xdotool key Return'; }
	i=0; until pkg info -e wine; do i=$((i+1)); [ $i -gt 400 ] && break; sleep 5; done; pkg info -e wine && echo wine-ok
	;;
cadeia3)
	espera "Programa do Windows" 300 && tecla "Programa do Windows" Return
	;;
cadeia4)
	i=0; until [ -x /home/kalel/.i386-wine-pkg/usr/local/bin/wine ]; do i=$((i+1)); [ $i -gt 400 ] && break; sleep 5; done
	ls -l /home/kalel/.i386-wine-pkg/usr/local/bin/wine; tail -3 /home/kalel/.cache/lume-wine32.log
	espera "Calculadora de teste" 900 && echo EXE32-ABRIU
	tail -15 /tmp/compat32.log | grep -v -E "canberra|qt.svg"
	;;
esac
case "$1" in
w32)
	ls -d /home/kalel/.wine 2>&1; ps -axo user,pid,etime,command | grep -i wine | grep -v grep | cut -c1-150
	rm -f /tmp/w32.log
	$U 'cd /home/kalel/Downloads && env HOME=/home/kalel WINEDEBUG=err+all daemon -o /tmp/w32.log sh -x /usr/local/bin/wine calculadora32.exe'
	espera "Calculadora de teste" 600 && echo EXE32-ABRIU
	ps -axo user,pid,etime,command | grep -i wine | grep -v grep | cut -c1-150
	grep -v -E "canberra|qt.svg" /tmp/w32.log | tail -25
	;;
esac
case "$1" in
dmg)
	printf 'koly' > /home/kalel/Downloads/Keynote-14.2.dmg && chown kalel /home/kalel/Downloads/Keynote-14.2.dmg
	$U 'env HOME=/home/kalel daemon -f kioclient exec /home/kalel/Downloads/Keynote-14.2.dmg'
	espera "Programa do Mac" 180
	;;
dmg2)
	tecla "Programa do Mac" Return
	espera "Programas do LumeBSD" 180
	;;
esac
case "$1" in
mac)
	fetch -qo /tmp/lume-macho http://10.0.2.2:8000/teste/lume-macho && chmod +x /tmp/lume-macho
	fetch -qo /tmp/ola-mac http://10.0.2.2:8000/teste/ola-mac
	file /tmp/ola-mac
	su -m kalel -c '/tmp/lume-macho -v /tmp/ola-mac'; echo "saida=$?"
	;;
motor)
	ls -l /usr/bin/lume-motor
	su -m kalel -c 'cd /tmp && env HOME=/home/kalel lume-motor --png /tmp/motor --tamanho 640x360 --segundos 6.2' 2>&1 | tail -6
	ls /tmp/motor | head
	;;
esac

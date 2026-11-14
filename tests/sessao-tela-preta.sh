# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py: a tela preta
# depois de "lume tema claro" (2.0) e os botões da barra com as abas. Fotos
# em vários momentos depois de cada troca; o log do KWin vai para
# /tmp/kwin.log.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 90
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume_abas.so lume_tinta.so org.lumebsd.papel.so; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume_abas.so /tmp/lume_tinta.so /usr/local/lib/qt6/plugins/kwin-x11/effects/plugins/; install -m 555 /tmp/org.lumebsd.papel.so /usr/local/lib/qt6/plugins/org.kde.kdecoration3/org.lumebsd.papel.so
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f -o /tmp/kwin.log kwin_x11 --replace'; sleep 90; pgrep -lu kalel kwin_x11
eval "$S"; pkill -u kalel dolphin konsole; sleep 3; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f konsole; sleep 8; env HOME=/home/kalel daemon -f dolphin --new-window /home/kalel'; sleep 60
eval "$S"; su -m kalel -c 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeAbasEmpilhar'; sleep 20
FOTO preta-1-empilhadas
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema escuro'; sleep 5
FOTO preta-2-escuro-5s
sleep 55
FOTO preta-3-escuro-60s
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema claro'; sleep 5
FOTO preta-4-claro-5s
sleep 25
FOTO preta-5-claro-30s
sleep 90
FOTO preta-6-claro-2min
eval "$S"; su -m kalel -c 'qdbus6 org.kde.KWin /KWin supportInformation' | grep -iE "compositing|OpenGL|backend|lume|Active|Loaded" | head -40
grep -v "^$" /tmp/kwin.log | tail -60
ps -axo pcpu,rss,comm | sort -rn | head -8; pgrep -lf baloo

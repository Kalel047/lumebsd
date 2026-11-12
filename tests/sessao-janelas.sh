# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a decoração
# Papel, as abas de janelas (lume_abas) e o ícone de rede (lume-rede, com o
# wpa_supplicant falso: tests/wpa-falso.c). Os binários vêm do espelho de
# teste ($REPO/teste/), compilados pelo montar-efeitos.py e o montar-app.py.
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume_abas.so org.lumebsd.papel.so lume-rede wpa-falso; do fetch -qo /tmp/$f $R/$f; done; ls -l /tmp/lume_abas.so /tmp/org.lumebsd.papel.so /tmp/lume-rede /tmp/wpa-falso
install -m 555 /tmp/lume_abas.so /usr/local/lib/qt6/plugins/kwin-x11/effects/plugins/lume_abas.so; install -m 555 /tmp/org.lumebsd.papel.so /usr/local/lib/qt6/plugins/org.kde.kdecoration3/org.lumebsd.papel.so; install -m 555 /tmp/lume-rede /usr/bin/lume-rede; install -m 555 /tmp/wpa-falso /usr/local/bin/wpa-falso
eval "$S"; su -m kalel -c 'kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library org.lumebsd.papel; kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme Papel; qdbus6 org.kde.KWin /KWin reconfigure; qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect lume_abas; qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded lume_abas'
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f konsole; sleep 8; env HOME=/home/kalel daemon -f dolphin /home/kalel'; sleep 60
FOTO janelas-1-decoracao
eval "$S"; su -m kalel -c 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.shortcutNames | grep -i abas; qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeAbasEmpilhar'; sleep 15
FOTO janelas-2-empilhadas
eval "$S"; su -m kalel -c 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeAbasProxima'; sleep 15
FOTO janelas-3-outra-aba
eval "$S"; su -m kalel -c 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeAbasSeparar'; sleep 15
FOTO janelas-4-separadas
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema escuro'; sleep 25
FOTO janelas-5-escuro
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema claro'; sleep 10
daemon -f wpa-falso /tmp/wpa.sock /tmp/wpa-falso.log; sleep 2; ls -l /tmp/wpa.sock
su -m kalel -c 'env HOME=/home/kalel LUME_REDE_SOCKET=/tmp/wpa.sock lume-rede --estado'
su -m kalel -c 'env HOME=/home/kalel LUME_REDE_SOCKET=/tmp/wpa.sock LUME_REDE_SENHA=senha-errada lume-rede --conectar "Casa do Kalel"; echo "saida=$?"'
su -m kalel -c 'env HOME=/home/kalel LUME_REDE_SOCKET=/tmp/wpa.sock LUME_REDE_SENHA=farol-2026 lume-rede --conectar "Casa do Kalel"; echo "saida=$?"'
su -m kalel -c 'env HOME=/home/kalel LUME_REDE_SOCKET=/tmp/wpa.sock lume-rede --estado'; cat /tmp/wpa-falso.log | grep -v "STATUS\|SIGNAL_POLL\|PING\|SCAN" | tail -20
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_REDE_SOCKET=/tmp/wpa.sock daemon -f lume-rede'; sleep 20
FOTO janelas-6-rede

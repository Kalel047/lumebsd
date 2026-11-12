# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a identidade
# visual: o menu, o Sobre o sistema e o splash. Uso (o SDDM é fotografado
# antes do login, com ANTES="sleep 60" e "FOTO marca-1-sddm"):
#   ANTES="$(printf 'sleep 60\nFOTO marca-1-sddm')" \
#   AJUSTES="$(grep -v '^#' tests/sessao-marca.sh)" \
#       python3 tests/teste-pendrive.py sessao LOG
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
cat /var/run/os-release; ls /usr/local/share/icons/hicolor/scalable/apps/ | grep -i lume; grep -c . /usr/local/share/plasma/look-and-feel/org.lumebsd.papel/contents/splash/Splash.qml
FOTO marca-2-desktop
eval "$S"; su -m kalel -c 'qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.activateLauncherMenu'; sleep 15
FOTO marca-3-menu
eval "$S"; su -m kalel -c 'qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.activateLauncherMenu'; sleep 5
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f kinfocenter kcm_about-distro'; sleep 60
FOTO marca-4-sobre
eval "$S"; su -m kalel -c 'pkill -u kalel -x kinfocenter; cd /home/kalel; env HOME=/home/kalel daemon -f ksplashqml org.lumebsd.papel --test --window'; sleep 25
FOTO marca-5-splash
eval "$S"; su -m kalel -c 'pkill -u kalel -x ksplashqml; cd /home/kalel; env HOME=/home/kalel daemon -f ksplashqml org.lumebsd.papel-noite --test --window'; sleep 25
FOTO marca-6-splash-noite
eval "$S"; su -m kalel -c 'pkill -u kalel -x ksplashqml'

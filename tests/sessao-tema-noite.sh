# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para o tema Papel
# noite: rodam como root na serial, com a sessão do kalel aberta; as linhas
# "FOTO nome" tiram uma foto da tela. Uso:
#   AJUSTES="$(cat tests/sessao-tema-noite.sh | grep -v '^#')" \
#       python3 tests/teste-pendrive.py sessao LOG
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema; env HOME=/home/kalel lume tema tinta'
ls /usr/local/share/plasma/look-and-feel/ /usr/local/share/color-schemes/ /usr/local/share/icons/ | grep -iE "lume|papel"
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f konsole; env HOME=/home/kalel daemon -f dolphin /home/kalel; env HOME=/home/kalel daemon -f lume-programas'; sleep 90
FOTO tema-1-claro
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema escuro'; sleep 20
eval "$S"; su -m kalel -c 'pkill -u kalel -x lume-programas; pkill -u kalel -x konsole; pkill -u kalel -x dolphin; sleep 3; cd /home/kalel; env HOME=/home/kalel daemon -f konsole; env HOME=/home/kalel daemon -f dolphin /home/kalel; env HOME=/home/kalel daemon -f lume-programas'; sleep 90
FOTO tema-2-escuro
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema; kreadconfig6 --file kdeglobals --group General --key ColorScheme; kreadconfig6 --file kdeglobals --group Icons --key Theme; kreadconfig6 --file konsolerc --group "Desktop Entry" --key DefaultProfile; grep prefer-dark /home/kalel/.config/gtk-3.0/settings.ini'
eval "$S"; su -m kalel -c 'qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded lume_tinta'
eval "$S"; su -m kalel -c 'pkill -u kalel -x lume-programas; pkill -u kalel -x dolphin; sleep 2'; sleep 10
FOTO tema-3-escuro-desktop
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema claro'; sleep 30
FOTO tema-4-claro-de-novo
eval "$S"; su -m kalel -c 'kreadconfig6 --file kdeglobals --group General --key ColorScheme'

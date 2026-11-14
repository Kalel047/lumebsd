# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.1: as
# pilhas de janelas que sobrevivem à troca de tema (o lume_abas novo), o
# cp -p e o mv levando os atributos, a procura ao vivo vendo a cópia e as
# colunas do Dolphin. Depois do tests/sessao-vivo.sh.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 90
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume_abas.so cp mv lume-atributo; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume_abas.so /usr/local/lib/qt6/plugins/kwin-x11/effects/plugins/lume_abas.so; install -m 555 /tmp/cp /bin/cp; install -m 555 /tmp/mv /bin/mv; install -m 555 /tmp/lume-atributo /usr/bin/lume-atributo
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f kwin_x11 --replace'; sleep 90; pgrep -lu kalel kwin_x11; su -m kalel -c 'qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | grep lume'
eval "$S"; pkill -u kalel dolphin konsole; sleep 3; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f konsole; sleep 8; env HOME=/home/kalel daemon -f dolphin --new-window /home/kalel'; sleep 60
eval "$S"; su -m kalel -c 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeAbasEmpilhar'; sleep 20
FOTO abas21-1-empilhadas
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema escuro'; sleep 60
FOTO abas21-2-escuro
eval "$S"; su -m kalel -c 'qdbus6 org.kde.kglobalaccel /component/kwin org.kde.kglobalaccel.Component.invokeShortcut LumeAbasProxima'; sleep 20
FOTO abas21-3-outra-aba
eval "$S"; su -m kalel -c 'env HOME=/home/kalel lume tema claro'; sleep 60
FOTO abas21-4-claro
eval "$S"; su -m kalel -c 'qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect lume_abas'; sleep 15; pgrep -lu kalel kwin_x11
FOTO abas21-5-efeito-desligado
eval "$S"; su -m kalel -c 'qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect lume_abas; qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | grep lume'
df -T /tmp /home/kalel | tail -2; su -m kalel -c 'cd /home/kalel; daemon -f -o /tmp/vivo2.log lume-atributo procurar -v -d /home/kalel etiquetas=trabalho'; sleep 5
su -m kalel -c 'cd /home/kalel; mkdir -p /tmp/proj; echo y > /tmp/proj/plano.md; lume-atributo /tmp/proj/plano.md etiquetas=trabalho; lume-atributo /tmp/proj/plano.md; mkdir -p Projetos; cp -Rp /tmp/proj Projetos/; lume-atributo Projetos/proj/plano.md; cp -p Documentos/contrato.pdf Documentos/copia.pdf; lume-atributo Documentos/copia.pdf; cp Documentos/contrato.pdf Documentos/sem-p.pdf; lume-atributo Documentos/sem-p.pdf; echo sem-p=$?; mv Documentos/copia.pdf /tmp/copia.pdf; lume-atributo /tmp/copia.pdf; mv /tmp/copia.pdf Documentos/copia.pdf; sleep 2; mv Projetos/proj Projetos/proj2; sleep 1; rm -r Projetos Documentos/copia.pdf Documentos/sem-p.pdf'; sleep 3; cat /tmp/vivo2.log; pkill -f "procurar -v"
su -m kalel -c 'cd /home/kalel; rm -f .local/share/dolphin/dolphinstaterc .local/state/dolphinstaterc; kwriteconfig6 --file dolphinrc --group General --key GlobalViewProps true; kwriteconfig6 --file dolphinrc --group General --key RememberOpenedTabs false; mkdir -p .local/share/dolphin/view_properties/global; printf "[Dolphin]\nVersion=4\nViewMode=1\nVisibleRoles=Details_text,Details_tags,Details_rating,Details_comment\n" > .local/share/dolphin/view_properties/global/.directory; rm -f Documentos/.directory'
eval "$S"; pkill -u kalel dolphin; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f dolphin --new-window /home/kalel/Documentos'; sleep 45
FOTO abas21-6-dolphin-colunas

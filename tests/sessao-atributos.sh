# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para os atributos
# dos arquivos: o lume-atributo (C), o lume-atributos (a janela, a procura e
# a consulta salva) e o que o Dolphin mostra nas colunas Etiquetas, Nota e
# Comentário. Os binários vêm do espelho de teste ($REPO/teste/).
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume-tema.sh lume-atributo lume-atributos lume-atributos.desktop lume-atributos-menu.desktop lume-atributos-pasta.desktop lume-consulta.xml; do fetch -qo /tmp/$f $R/$f; done; ls -l /tmp/lume-atributo*
install -m 555 /tmp/lume-tema.sh /usr/sbin/lume-tema; install -m 555 /tmp/lume-atributo /usr/bin/lume-atributo; install -m 555 /tmp/lume-atributos /usr/bin/lume-atributos; mkdir -p /usr/share/lume/app; for f in lume-atributos.desktop lume-atributos-menu.desktop lume-atributos-pasta.desktop lume-consulta.xml; do install -m 444 /tmp/$f /usr/share/lume/app/$f; done; lume-tema integrar >/dev/null 2>&1; ls /usr/local/share/kio/servicemenus/ /usr/local/share/mime/packages/ | grep -i lume
zfs get -H -o name,value xattr $(df /home/kalel | tail -1 | cut -d" " -f1)
su -m kalel -c 'cd /home/kalel; mkdir -p Documentos Imagens; echo contrato > Documentos/contrato.pdf; echo notas > Documentos/notas.txt; echo foto > Imagens/praia.png; echo outro > Documentos/orcamento.ods'
su -m kalel -c 'cd /home/kalel; lume-atributo Documentos/contrato.pdf etiquetas+=trabalho etiquetas+=urgente nota=4 cliente=Acme "comentario=Assinar até sexta"; lume-atributo Documentos/orcamento.ods etiquetas+=trabalho nota=2,5 cliente=Acme; lume-atributo Imagens/praia.png etiquetas=viagem,família nota=5 "comentario=Praia do Forte"; lume-atributo Documentos/notas.txt etiquetas+=pessoal; lume-atributo Documentos/contrato.pdf; echo ---; lume-atributo Documentos/*.* nota; echo ---; lume-atributo Documentos/contrato.pdf etiquetas-=urgente; lume-atributo Documentos/contrato.pdf etiquetas'
su -m kalel -c 'cd /home/kalel; echo "== trabalho"; lume-atributo procurar etiquetas=trabalho; echo "== nota>=4"; lume-atributo procurar "nota>=4"; echo "== cliente~acm e nota>3"; lume-atributo procurar "cliente~acm" "nota>3"; echo "== nada"; lume-atributo procurar etiquetas=nada; echo "saida=$?"; echo "== erro"; lume-atributo procurar "nota>=x"; echo "saida=$?"'
getextattr -x user xdg.tags /home/kalel/Documentos/contrato.pdf; getextattr user baloo.rating /home/kalel/Documentos/contrato.pdf; lsextattr user /home/kalel/Documentos/contrato.pdf
su -m kalel -c 'cd /home/kalel; cp -p Documentos/contrato.pdf /tmp/copia.pdf; lume-atributo /tmp/copia.pdf; mv Documentos/notas.txt Documentos/notas2.txt; lume-atributo Documentos/notas2.txt etiquetas'
su -m kalel -c 'cd /home/kalel; mkdir -p .local/share/dolphin/view_properties/global; printf "[Dolphin]\nVersion=4\nViewMode=1\nVisibleRoles=Details_text,Details_tags,Details_rating,Details_comment,Details_modificationtime\n" > .local/share/dolphin/view_properties/global/.directory; kwriteconfig6 --file dolphinrc --group General --key GlobalViewProps true; mkdir -p Consultas; printf "pasta=~\ncondicoes=etiquetas=trabalho nota>=2\n" > Consultas/Trabalho.consulta'
su -m kalel -c 'cd /home/kalel; xdg-mime query filetype Consultas/Trabalho.consulta; xdg-mime query default application/x-lume-consulta'
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f dolphin /home/kalel/Documentos'; sleep 40
FOTO atributos-1-dolphin
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-atributos --editar /home/kalel/Documentos/contrato.pdf'; sleep 20
FOTO atributos-2-janela
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-atributos /home/kalel/Consultas/Trabalho.consulta'; sleep 20
FOTO atributos-3-consulta

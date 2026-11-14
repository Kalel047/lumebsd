# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.1: a
# procura ao vivo (lume-atributo procurar -v e a janela do lume-atributos) e
# as colunas Etiquetas e Comentário do Dolphin sem o índice do Baloo (o
# baloofilerc com a indexação "ligada" e nenhuma pasta). Depois do
# tests/sessao-atributos.sh (os mesmos arquivos de exemplo).
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume-atributo lume-atributos lume-tema.sh; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume-atributo /usr/bin/lume-atributo; install -m 555 /tmp/lume-atributos /usr/bin/lume-atributos; install -m 555 /tmp/lume-tema.sh /usr/sbin/lume-tema; cat /usr/local/etc/xdg/baloofilerc; lume-tema integrar >/dev/null 2>&1; echo ---; cat /usr/local/etc/xdg/baloofilerc
su -m kalel -c 'cd /home/kalel; ls Documentos Imagens; lume-atributo procurar etiquetas=trabalho; sysctl vfs.inotify.watches'
su -m kalel -c 'cd /home/kalel; daemon -f -o /tmp/vivo.log lume-atributo procurar -v -d /home/kalel etiquetas=trabalho'; sleep 5; cat /tmp/vivo.log; sysctl vfs.inotify.watches
su -m kalel -c 'cd /home/kalel; lume-atributo Documentos/notas2.txt etiquetas+=trabalho; sleep 1; lume-atributo Documentos/orcamento.ods etiquetas-=trabalho; sleep 1; echo x > Documentos/novo.txt; lume-atributo Documentos/novo.txt etiquetas=trabalho; sleep 1; lume-atributo Documentos/contrato.pdf nota=5; sleep 1; mkdir -p /tmp/proj; echo y > /tmp/proj/plano.md; lume-atributo /tmp/proj/plano.md etiquetas=trabalho; mkdir -p Projetos; cp -Rp /tmp/proj Projetos/; sleep 1; mv Documentos/novo.txt Documentos/novo2.txt; sleep 1; mv Projetos/proj Projetos/proj2; sleep 1; rm -r Projetos; sleep 1; echo z > .oculto; lume-atributo .oculto etiquetas=trabalho'; sleep 3; echo ===; cat /tmp/vivo.log; pkill -f "procurar -v"; sleep 1; sysctl vfs.inotify.watches
su -m kalel -c 'cd /home/kalel; lume-atributo Documentos/orcamento.ods etiquetas+=trabalho; lume-atributo Documentos/notas2.txt etiquetas-=trabalho; rm -f Documentos/novo2.txt .oculto; env HOME=/home/kalel balooctl6 config list includeFolders; env HOME=/home/kalel balooctl6 status'
eval "$S"; pkill -u kalel dolphin; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f dolphin /home/kalel/Documentos'; sleep 40
FOTO vivo-1-dolphin-colunas
eval "$S"; pkill -u kalel dolphin; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-atributos /home/kalel/Consultas/Trabalho.consulta'; sleep 25
FOTO vivo-2-consulta
su -m kalel -c 'cd /home/kalel; lume-atributo Imagens/praia.png etiquetas+=trabalho; lume-atributo Documentos/contrato.pdf "comentario=Assinado"'; sleep 8
FOTO vivo-3-consulta-mudou
su -m kalel -c 'cd /home/kalel; lume-atributo Imagens/praia.png etiquetas-=trabalho; lume-atributo Documentos/contrato.pdf "comentario=Assinar até sexta"'
su -m kalel -c 'env HOME=/home/kalel daemon -f /usr/local/lib/libexec/kf6/baloo_file'; sleep 60; ps -o pid,rss,time,command -U kalel | grep -i baloo | grep -v grep; ls -la /home/kalel/.local/share/baloo/ 2>&1; pkill -u kalel baloo_file

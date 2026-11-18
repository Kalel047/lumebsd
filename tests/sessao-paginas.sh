# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.2: o
# Páginas compilado para o FreeBSD (Qt do pacote) dá a mesma estrutura que
# no Linux (os resumos de referência vêm do tests/paginas.sh), e as fotos
# da galeria, de um modelo e de um .docx do Word.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 60
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume-paginas lume-paginas.desktop lume-paginas.svg word.docx abnt-esperado.txt auto-esperado.txt word-esperado.txt; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume-paginas /usr/bin/lume-paginas; install -m 444 /tmp/lume-paginas.svg /tmp/lume-paginas.desktop /usr/share/lume/app/; install -m 644 /tmp/lume-paginas.desktop /usr/local/share/applications/; cp /tmp/word.docx /home/kalel/Documentos/; chown kalel /home/kalel/Documentos/word.docx; ldd /usr/bin/lume-paginas | grep -c "not found"
(cd /tmp; lume-paginas --modelo abnt /tmp/abnt.odt; lume-paginas --modelo abnt /tmp/abnt.docx; lume-paginas --resumo /tmp/abnt.docx | diff - /tmp/abnt-esperado.txt && echo ABNT-IGUAL; lume-paginas --converter /tmp/abnt.odt /tmp/abnt.pdf; head -c 5 /tmp/abnt.pdf; echo; lume-paginas --resumo /tmp/word.docx | diff - /tmp/word-esperado.txt && echo WORD-IGUAL)
(cd /tmp; lume-paginas --autoteste /tmp/auto.odt | diff - /tmp/auto-esperado.txt && echo AUTO-IGUAL)
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-paginas'; sleep 25
FOTO paginas-1-galeria
eval "$S"; pkill -u kalel lume-paginas; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-paginas --modelo abnt /home/kalel/Documentos/trabalho.odt; env HOME=/home/kalel daemon -f lume-paginas /home/kalel/Documentos/trabalho.odt'; sleep 25
FOTO paginas-2-abnt
eval "$S"; pkill -u kalel lume-paginas; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-paginas /home/kalel/Documentos/word.docx'; sleep 25
FOTO paginas-3-word
eval "$S"; pkill -u kalel lume-paginas; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-paginas --modelo aula /home/kalel/Documentos/aula.docx; env HOME=/home/kalel daemon -f lume-paginas /home/kalel/Documentos/aula.docx'; sleep 25
FOTO paginas-4-aula
eval "$S"; pkill -u kalel lume-paginas; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-paginas --modelo relatorio /home/kalel/Documentos/relatorio.odt; env HOME=/home/kalel LUME_TEMA=escuro daemon -f lume-paginas /home/kalel/Documentos/relatorio.odt'; sleep 25
FOTO paginas-5-escuro

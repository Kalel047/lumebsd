# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.4: o
# Palco compilado para o FreeBSD (Qt do pacote) dá o mesmo resumo que no
# Linux (as referências vêm do tests/palco.sh e de um .pptx gravado por outro
# programa, o python-pptx), um vídeo de um .pptx "do PowerPoint" (que
# começa sozinho, repete e não tem som) toca pelo Qt Multimedia do sistema,
# e as fotos da galeria, de dois modelos, da tela do apresentador, de um
# .pptx de fora, do painel Animar, de uma tabela e do vídeo tocando.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 60
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume-palco lume-palco.desktop lume-palco.svg pp.pptx video.pptx conjuntura-esperado.txt palco-auto-esperado.txt pp-esperado.txt; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume-palco /usr/bin/lume-palco; install -m 444 /tmp/lume-palco.svg /tmp/lume-palco.desktop /usr/share/lume/app/; install -m 644 /tmp/lume-palco.desktop /usr/local/share/applications/; cp /tmp/pp.pptx /home/kalel/Documentos/relatorio.pptx; cp /tmp/video.pptx /home/kalel/Documentos/video.pptx; chown kalel /home/kalel/Documentos/relatorio.pptx /home/kalel/Documentos/video.pptx; ldd /usr/bin/lume-palco | grep -c "not found"; ldd /usr/bin/lume-palco | grep -c Qt6Multimedia; ls /usr/local/lib/qt6/plugins/multimedia
(cd /tmp; lume-palco --modelo conjuntura /tmp/conj.pptx; lume-palco --resumo /tmp/conj.pptx | diff - /tmp/conjuntura-esperado.txt && echo CONJ-IGUAL; lume-palco --converter /tmp/conj.pptx /tmp/conj.odp; lume-palco --resumo /tmp/conj.odp | diff - /tmp/conjuntura-esperado.txt && echo ODP-IGUAL; lume-palco --converter /tmp/conj.pptx /tmp/conj.pdf; head -c 5 /tmp/conj.pdf; echo; lume-palco --resumo /tmp/pp.pptx | diff - /tmp/pp-esperado.txt && echo PP-IGUAL)
(cd /tmp; lume-palco --autoteste /tmp/auto.pptx | diff - /tmp/palco-auto-esperado.txt && echo AUTO-IGUAL; lume-palco --resumo /tmp/video.pptx | grep -q "mp4 (28 KB) capa 640x360 sozinho repetir mudo" && echo VIDEO-LIDO)
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-palco'; sleep 25
FOTO palco-1-galeria
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-palco --modelo conjuntura /home/kalel/Documentos/conjuntura.pptx; env HOME=/home/kalel LUME_FOTO_SLIDE=3 daemon -f lume-palco /home/kalel/Documentos/conjuntura.pptx'; sleep 25
FOTO palco-2-conjuntura
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-palco --modelo aula /home/kalel/Documentos/aula.odp; env HOME=/home/kalel LUME_FOTO_SLIDE=4 LUME_FOTO_OBJETO=2 daemon -f lume-palco /home/kalel/Documentos/aula.odp'; sleep 25
FOTO palco-3-aula
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FOTO_SLIDE=3 LUME_FOTO_MODO=apresentador daemon -f lume-palco /home/kalel/Documentos/aula.odp'; sleep 25
FOTO palco-4-apresentador
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FOTO_SLIDE=2 daemon -f lume-palco /home/kalel/Documentos/relatorio.pptx'; sleep 25
FOTO palco-5-pptx
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FOTO_SLIDE=4 LUME_FOTO_PAINEL=3 LUME_FOTO_OBJETO=4 daemon -f lume-palco /home/kalel/Documentos/aula.odp'; sleep 25
FOTO palco-6-animar
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FOTO_SLIDE=4 LUME_FOTO_OBJETO=2 daemon -f lume-palco /home/kalel/Documentos/conjuntura.pptx'; sleep 25
FOTO palco-7-tabela
eval "$S"; pkill -u kalel lume-palco; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FOTO_MODO=apresentacao daemon -f lume-palco /home/kalel/Documentos/video.pptx'; sleep 25
FOTO palco-8-video

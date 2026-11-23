# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.5: os
# Números, o Páginas e o Palco compilados para o FreeBSD dão o mesmo
# resultado que no Linux; o gráfico copiado nos Números (--copiar-grafico)
# sai com os números da planilha; os autotestes do Páginas e do Palco colam
# esse gráfico pela área de transferência de verdade (rodam na sessão do
# KDE, pelo X); um .docx com o gráfico abre igual; e as fotos.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 60
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for a in paginas numeros palco; do for f in lume-$a lume-$a.desktop lume-$a.svg; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume-$a /usr/bin/lume-$a; install -m 444 /tmp/lume-$a.svg /tmp/lume-$a.desktop /usr/share/lume/app/; install -m 644 /tmp/lume-$a.desktop /usr/local/share/applications/; ldd /usr/bin/lume-$a | grep -c "not found"; done
for f in conjuntura-grafico.docx conjuntura-grafico-esperado.txt grafico-esperado.txt paginas-auto-esperado.txt palco-auto-esperado.txt; do fetch -qo /tmp/$f $R/$f; done; cp /tmp/conjuntura-grafico.docx /home/kalel/Documentos/; chown kalel /home/kalel/Documentos/conjuntura-grafico.docx
(cd /tmp; lume-numeros --modelo indicadores /tmp/ind.xlsx; lume-numeros --copiar-grafico /tmp/ind.xlsx 1 | diff - /tmp/grafico-esperado.txt && echo COPIAR-IGUAL; lume-paginas --resumo /tmp/conjuntura-grafico.docx | diff - /tmp/conjuntura-grafico-esperado.txt && echo DOCX-IGUAL; lume-paginas --converter /tmp/conjuntura-grafico.docx /tmp/cg.odt; lume-paginas --resumo /tmp/cg.odt | diff - /tmp/conjuntura-grafico-esperado.txt && echo ODT-IGUAL)
eval "$S"; su -m kalel -c 'cd /tmp; env HOME=/home/kalel lume-paginas --autoteste /tmp/pa.odt > /tmp/pa.txt 2>/dev/null; env HOME=/home/kalel lume-palco --autoteste /tmp/pl.pptx > /tmp/pl.txt 2>/dev/null'; diff /tmp/pa.txt /tmp/paginas-auto-esperado.txt && echo PAGINAS-COLOU; diff /tmp/pl.txt /tmp/palco-auto-esperado.txt && echo PALCO-COLOU; cp /tmp/pl.pptx /home/kalel/Documentos/colado.pptx; chown kalel /home/kalel/Documentos/colado.pptx
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-paginas /home/kalel/Documentos/conjuntura-grafico.docx'; sleep 25
FOTO paginas-6-grafico
eval "$S"; pkill -u kalel lume-paginas; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FOTO_SLIDE=5 LUME_FOTO_OBJETO=0 daemon -f lume-palco /home/kalel/Documentos/colado.pptx'; sleep 25
FOTO palco-9-grafico

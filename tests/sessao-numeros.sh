# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.3: o
# Números compilado para o FreeBSD (Qt do pacote) dá o mesmo resumo que no
# Linux (as referências vêm do tests/numeros.sh e de um .xlsx gravado por
# outro programa, o openpyxl), e as fotos da galeria, de modelos, de um
# .xlsx com painel congelado e do tema escuro.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 60
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for f in lume-numeros lume-numeros.desktop lume-numeros.svg vendas.xlsx juros-esperado.txt numeros-auto-esperado.txt vendas-esperado.txt; do fetch -qo /tmp/$f $R/$f; done; install -m 555 /tmp/lume-numeros /usr/bin/lume-numeros; install -m 444 /tmp/lume-numeros.svg /tmp/lume-numeros.desktop /usr/share/lume/app/; install -m 644 /tmp/lume-numeros.desktop /usr/local/share/applications/; cp /tmp/vendas.xlsx /home/kalel/Documentos/; chown kalel /home/kalel/Documentos/vendas.xlsx; ldd /usr/bin/lume-numeros | grep -c "not found"
(cd /tmp; lume-numeros --modelo juros /tmp/juros.xlsx; lume-numeros --resumo /tmp/juros.xlsx | diff - /tmp/juros-esperado.txt && echo JUROS-IGUAL; lume-numeros --converter /tmp/juros.xlsx /tmp/juros.ods; lume-numeros --resumo /tmp/juros.ods | diff - /tmp/juros-esperado.txt && echo ODS-IGUAL; lume-numeros --converter /tmp/juros.xlsx /tmp/juros.pdf; head -c 5 /tmp/juros.pdf; echo; lume-numeros --resumo /tmp/vendas.xlsx | diff - /tmp/vendas-esperado.txt && echo VENDAS-IGUAL)
(cd /tmp; lume-numeros --autoteste /tmp/auto.xlsx | diff - /tmp/numeros-auto-esperado.txt && echo AUTO-IGUAL)
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-numeros'; sleep 25
FOTO numeros-1-galeria
eval "$S"; pkill -u kalel lume-numeros; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-numeros --modelo orcamento /home/kalel/Documentos/orcamento.xlsx; env HOME=/home/kalel daemon -f lume-numeros /home/kalel/Documentos/orcamento.xlsx'; sleep 25
FOTO numeros-2-orcamento
eval "$S"; pkill -u kalel lume-numeros; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-numeros --modelo fluxo /home/kalel/Documentos/fluxo.ods; env HOME=/home/kalel daemon -f lume-numeros /home/kalel/Documentos/fluxo.ods'; sleep 25
FOTO numeros-3-fluxo
eval "$S"; pkill -u kalel lume-numeros; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-numeros /home/kalel/Documentos/vendas.xlsx'; sleep 25
FOTO numeros-4-excel
eval "$S"; pkill -u kalel lume-numeros; sleep 2; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel lume-numeros --modelo indicadores /home/kalel/Documentos/indicadores.xlsx; env HOME=/home/kalel LUME_TEMA=escuro daemon -f lume-numeros /home/kalel/Documentos/indicadores.xlsx'; sleep 25
FOTO numeros-5-escuro

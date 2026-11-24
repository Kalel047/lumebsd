# Comandos (AJUSTES) da fase "sessao" do teste-pendrive.py para a 2.6: os
# Replicants no KDE de verdade. A mesa (lume-mesa) roda o gráfico dos
# Números, os indicadores das Finanças e as cotações do Mercados (com dados
# de teste, sem rede); o KWin põe cada um abaixo das janelas, em todas as
# áreas de trabalho e fora da barra de tarefas (o xprop confere); um que
# cai volta; a planilha gravada de novo redesenha o replicante e o gráfico
# vinculado de um .docx aberto no Páginas; e as fotos.
n=0; until pgrep -u kalel plasmashell >/dev/null || [ $n -ge 120 ]; do sleep 10; n=$((n+1)); done; echo espera=$n; sleep 60
S='for n in plasmashell kwin_x11 ksmserver kded6; do pid=$(pgrep -n -u kalel $n); E=$(procstat -e $pid 2>/dev/null | tr " " "\n" | grep -E "^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_[A-Z_]*|KDE_[A-Z_]*|LANG)="); [ -n "$E" ] && break; done; eval $(echo "$E" | sed "s/^/export /")'
R=http://10.0.2.2:8000/teste; for a in mesa numeros financas mercados paginas palco; do fetch -qo /tmp/lume-$a $R/lume-$a; install -m 555 /tmp/lume-$a /usr/bin/lume-$a; ldd /usr/bin/lume-$a | grep -c "not found"; done
env ASSUME_ALWAYS_YES=yes pkg add -q http://10.0.2.2:8000/All/xprop-1.2.8.pkg http://10.0.2.2:8000/All/xdotool-4.20260303.1,1.pkg >/dev/null 2>&1; which xprop xdotool
mkdir -p /tmp/dados/sgs; for f in 1 13522 432 4389; do fetch -qo /tmp/dados/sgs/$f.json $R/sgs/$f.json; done; fetch -qo /tmp/dados/68b951a5c144d1394389dbc3084a32d7 $R/68b951a5c144d1394389dbc3084a32d7; chmod -R a+rX /tmp/dados
for f in ind.xlsx ind-novo.xlsx relatorio-vinculado.docx; do fetch -qo /tmp/$f $R/$f; done; cp /tmp/ind.xlsx /tmp/relatorio-vinculado.docx /home/kalel/Documentos/; chown kalel /home/kalel/Documentos/ind.xlsx /home/kalel/Documentos/relatorio-vinculado.docx
rm -f /home/kalel/.config/lume/mesa; su -m kalel -c 'env HOME=/home/kalel sh -c "lume-mesa --por lume-numeros grafico /home/kalel/Documentos/ind.xlsx 1; lume-mesa --por lume-financas indicadores; lume-mesa --por lume-mercados cotacao AAA BBB; lume-mesa --listar"'
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel LUME_FINANCAS_SGS=/tmp/dados/sgs LUME_MERCADOS_DADOS=/tmp/dados daemon -f -o /tmp/mesa.log lume-mesa'; sleep 20; echo "replicantes: $(pgrep -f -- '--replicante' | wc -l | tr -d ' ')"
eval "$S"; for c in lume-numeros lume-financas lume-mercados; do w=$(xdotool search --classname $c | head -1); echo "$c $(xprop -id $w _NET_WM_STATE _NET_WM_DESKTOP | tr '\n' ' ')"; done
sleep 3; for c in lume-numeros lume-financas lume-mercados; do w=$(xdotool search --classname $c | head -1); echo "$c depois $(xprop -id $w _NET_WM_STATE | tr "\n" " ")"; done
FOTO mesa-1
pkill -9 -f 'lume-financas --replicante'; sleep 6; echo "voltou: $(pgrep -f 'lume-financas --replicante' | wc -l | tr -d ' ')"
eval "$S"; su -m kalel -c 'cd /home/kalel; env HOME=/home/kalel daemon -f lume-paginas /home/kalel/Documentos/relatorio-vinculado.docx'; sleep 20
FOTO mesa-2-paginas-antes
su -m kalel -c 'cp /tmp/ind-novo.xlsx /home/kalel/Documentos/.ind.novo && mv /home/kalel/Documentos/.ind.novo /home/kalel/Documentos/ind.xlsx'; sleep 10
FOTO mesa-3-paginas-depois
eval "$S"; pkill -u kalel lume-paginas; sleep 3
FOTO mesa-4-depois
su -m kalel -c 'env HOME=/home/kalel lume-mesa --tirar r3'; sleep 3; echo "tirado: $(pgrep -f -- '--replicante' | wc -l | tr -d ' ')"; cat /tmp/mesa.log; su -m kalel -c 'env HOME=/home/kalel lume-mesa --listar'

#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# A mesa dos replicantes (lume-mesa) e o replicante dos Números: pôr, listar
# e tirar; o replicante desenha o gráfico da planilha (e avisa quando ela
# ou o gráfico somem); e, num X de verdade (Xvfb), a mesa roda um processo
# por replicante, o traz de volta quando cai, acompanha o que entra e o que
# sai, e o replicante se redesenha quando a planilha é gravada.
#
#   sh tests/mesa.sh LUME-MESA LUME-NUMEROS PASTA
set -u
M=$(realpath "$1"); N=$(realpath "$2"); T=$3
rm -rf "$T"; mkdir -p "$T/bin"; T=$(realpath "$T")
ln -s "$M" "$T/bin/lume-mesa"; ln -s "$N" "$T/bin/lume-numeros"
export PATH="$T/bin:$PATH" LUME_MESA="$T/mesa" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
falhou=0
erro() { echo "FALHOU: $*"; falhou=1; }

QT_QPA_PLATFORM=offscreen lume-numeros --modelo indicadores "$T/ind.xlsx"
[ "$(lume-mesa --por lume-numeros grafico "$T/ind.xlsx" 1)" = r1 ] || erro "--por"
[ "$(lume-mesa --por lume-numeros grafico "$T/ind.xlsx" 7)" = r2 ] || erro "--por (o segundo)"
lume-mesa --por sh -c true >/dev/null 2>&1 && erro "--por de um programa que não é do LumeBSD"
[ "$(lume-mesa --listar | wc -l)" -eq 2 ] || erro "--listar"
G='gráfico linhas "PIB, inflação e juros" 3 séries x 10, soma 161.39'
[ "$(QT_QPA_PLATFORM=offscreen lume-numeros --replicante r1 --foto "$T/r1.png")" = "$G" ] || erro "replicante do gráfico"
[ -s "$T/r1.png" ] || erro "foto do replicante"
QT_QPA_PLATFORM=offscreen lume-numeros --replicante r2 --foto "$T/r2.png" | grep -q "não tem mais esse gráfico" || erro "replicante de um gráfico que não existe"
lume-mesa --tirar r2 || erro "--tirar"
lume-mesa --tirar r2 2>/dev/null && erro "--tirar de novo"
[ "$(lume-mesa --listar | cut -d' ' -f1)" = r1 ] || erro "--listar depois de tirar"

# Uma planilha gravada de novo (como os apps gravam: um arquivo novo no lugar).
cat > "$T/mudar.py" <<'PY'
import os, sys, zipfile
a = sys.argv[1]; t = a + '.novo'
with zipfile.ZipFile(a) as z, zipfile.ZipFile(t, 'w', zipfile.ZIP_DEFLATED) as o:
    for i in z.infolist():
        d = z.read(i.filename)
        if i.filename.startswith('xl/worksheets/sheet'):
            d = d.replace(b'<v>14.25</v>', b'<v>20</v>', 1)
        o.writestr(i, d)
os.replace(t, a)
PY

if command -v xvfb-run >/dev/null 2>&1; then
	cat > "$T/vivo.sh" <<EOF
unset QT_QPA_PLATFORM
export LUME_REPLICANTE_DIZER=1
conta() { pgrep -fc "lume-numeros --replicante \$1" || true; }
lume-mesa > "$T/vivo.log" 2>&1 &
mesa=\$!
sleep 3; echo "um \$(conta r)"
python3 "$T/mudar.py" "$T/ind.xlsx"; sleep 2
pkill -9 -f 'lume-numeros --replicante r1'; sleep 3; echo "voltou \$(conta r1)"
lume-mesa --por lume-numeros grafico "$T/ind.xlsx" 1 >/dev/null; sleep 2; echo "dois \$(conta r)"
lume-mesa --tirar r2; sleep 2; echo "tirado \$(conta r)"
lume-mesa; echo "segunda \$?"
kill \$mesa; sleep 2; echo "fim \$(conta r)"
EOF
	timeout 90 xvfb-run -a sh "$T/vivo.sh" > "$T/vivo.txt" 2>&1
	cat > "$T/vivo-esperado.txt" <<'E'
um 1
voltou 1
dois 2
tirado 1
segunda 0
fim 0
E
	diff -u "$T/vivo-esperado.txt" "$T/vivo.txt" || erro "a mesa viva"
	# O que o replicante mostrou: a planilha, a planilha mudada, e de novo
	# depois de cair (e o r2, igual).
	grep -c "soma 161.39" "$T/vivo.log" | grep -qx 1 || erro "o replicante desenhou a planilha"
	grep -c "soma 167.14" "$T/vivo.log" | grep -qx 3 || { cat "$T/vivo.log"; erro "o replicante se redesenhou com a planilha nova"; }
	grep -q "QProcess" "$T/vivo.log" && erro "a mesa deixou um processo para trás"
else
	echo "(sem o xvfb-run: a mesa viva não foi testada)"
fi

[ $falhou -eq 0 ] && echo "mesa: tudo certo"
exit $falhou

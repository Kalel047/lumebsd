#!/bin/sh
# Testes do Páginas (lume-paginas) sem janela: cada modelo gravado em .odt
# e em .docx tem que voltar com a mesma estrutura (estilos, listas,
# tabelas, quebras, a folha); o .odt convertido para .docx e de volta
# também; a edição pelo teclado (autoteste) tem que sair como esperado; e
# os arquivos têm que ser zips com XML válido (e o mimetype do ODF primeiro,
# sem compressão).
#
#   sh tests/paginas.sh BINARIO [PASTA]
set -eu
B=$1
T=${2:-$(mktemp -d)}
mkdir -p "$T"
export QT_QPA_PLATFORM=offscreen
falhou=0
for m in branco abnt aula relatorio carta curriculo; do
	"$B" --modelo $m "$T/$m.odt"
	"$B" --modelo $m "$T/$m.docx"
	"$B" --resumo "$T/$m.odt" > "$T/$m.odt.txt"
	"$B" --resumo "$T/$m.docx" > "$T/$m.docx.txt"
	"$B" --converter "$T/$m.odt" "$T/$m-2.docx"
	"$B" --converter "$T/$m-2.docx" "$T/$m-3.odt"
	"$B" --resumo "$T/$m-3.odt" > "$T/$m-3.txt"
	"$B" --converter "$T/$m.odt" "$T/$m.pdf"
	if ! diff -u "$T/$m.odt.txt" "$T/$m.docx.txt" || ! diff -u "$T/$m.odt.txt" "$T/$m-3.txt"; then
		echo "FALHOU: $m não volta igual"
		falhou=1
	fi
	head -c 5 "$T/$m.pdf" | grep -q '%PDF-' || { echo "FALHOU: $m.pdf"; falhou=1; }
done
# O trabalho da ABNT: 3 páginas, as margens e os números.
grep -q '^folha 210x297 margens 30/20/20/30 numeros esquema abnt' "$T/abnt.odt.txt" || { echo "FALHOU: folha da ABNT"; falhou=1; }
grep -q '^paginas 3$' "$T/abnt.odt.txt" || { echo "FALHOU: ABNT não tem 3 páginas"; falhou=1; }
# A edição pelo teclado.
"$B" --autoteste "$T/auto.odt" > "$T/auto.txt"
"$B" --autoteste "$T/auto.docx" > "$T/auto-docx.txt"
cat > "$T/auto-esperado.txt" <<'X'
[titulo] N:"Meu trabalho"
[corpo] "Um texto com " N:"negrito" " e o resto."
[cab1] N:"Lista"
[corpo] lista-mar1 "primeiro"
[corpo] lista-mar2 "dentro"
[corpo] lista-mar1 "segundo"
[corpo] centro "centro"
[corpo] quebra "depois da quebra"
[corpo]
tabela 3x2
X
sed -n '3,12p' "$T/auto.txt" | diff -u "$T/auto-esperado.txt" - || { echo "FALHOU: autoteste"; falhou=1; }
diff -u "$T/auto.txt" "$T/auto-docx.txt" || { echo "FALHOU: autoteste .docx"; falhou=1; }
# Zips e XML.
python3 - "$T" <<'P' || falhou=1
import sys, glob, zipfile
from xml.dom import minidom
for f in glob.glob(sys.argv[1] + '/*.odt') + glob.glob(sys.argv[1] + '/*.docx'):
    z = zipfile.ZipFile(f)
    assert z.testzip() is None, f
    for n in z.namelist():
        if n.endswith('.xml') or n.endswith('.rels'):
            minidom.parseString(z.read(n))
    if f.endswith('.odt'):
        i = z.infolist()[0]
        assert i.filename == 'mimetype' and i.compress_type == 0, f
P
[ $falhou -eq 0 ] && echo "Páginas: tudo certo"
exit $falhou

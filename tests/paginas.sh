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
# O gráfico colado dos Números (o fim do autoteste) vira gráfico de verdade:
# volta com os dados pelo .odt e pelo .docx, e por .odt -> .docx -> .odt.
G='[corpo] {grafico linhas "PIB, inflação e juros" 3x10 soma 161.39 600x360}'
grep -qxF "$G" "$T/auto.txt" || { echo "FALHOU: gráfico colado"; falhou=1; }
"$B" --converter "$T/auto.odt" "$T/auto-g.docx"
"$B" --converter "$T/auto-g.docx" "$T/auto-g.odt"
"$B" --resumo "$T/auto.odt" > "$T/auto-r.txt"
"$B" --resumo "$T/auto-g.odt" | diff -u "$T/auto-r.txt" - || { echo "FALHOU: gráfico .odt -> .docx -> .odt"; falhou=1; }
unzip -l "$T/auto.docx" | grep -q 'word/charts/chart1.xml' || { echo "FALHOU: .docx sem c:chart"; falhou=1; }
unzip -l "$T/auto.docx" | grep -q 'word/embeddings/.*\.xlsx' || { echo "FALHOU: .docx sem a planilha do gráfico"; falhou=1; }
unzip -l "$T/auto.odt" | grep -q 'Object 1/content.xml' || { echo "FALHOU: .odt sem o objeto de gráfico"; falhou=1; }
# Um .docx "de fora" (como os do Word e do Google Docs): listas numeradas
# que continuam umas nas outras (startOverride) e uma tabela com a linha do
# cabeçalho que repete em cada página (tblHeader); vão e voltam pelo .odt.
python3 - "$T" <<'P'
import sys, zipfile
T = sys.argv[1]
W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'
def par(t, num=None):
    pp = '<w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="%s"/></w:numPr></w:pPr>' % num if num else ''
    return '<w:p>%s<w:r><w:t>%s</w:t></w:r></w:p>' % (pp, t)
def cel(t):
    return '<w:tc><w:tcPr><w:tcW w:w="4000" w:type="dxa"/></w:tcPr>%s</w:tc>' % par(t)
corpo = par('Bloco 1') + par('um', 1) + par('dois', 1) + par('Bloco 2') + par('tres', 2) + par('quatro', 2)
corpo += ('<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/></w:tblPr><w:tblGrid><w:gridCol w:w="4000"/><w:gridCol w:w="4000"/></w:tblGrid>'
          '<w:tr><w:trPr><w:tblHeader/></w:trPr>' + cel('Item') + cel('Gasto') + '</w:tr>')
for i in range(40):
    corpo += '<w:tr>' + cel('linha %d' % i) + cel('R$ %d' % i) + '</w:tr>'
corpo += '</w:tbl>' + par('fim')
num = ('<w:numbering %s><w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="decimal"/>'
       '<w:lvlText w:val="%%1."/></w:lvl></w:abstractNum><w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>'
       '<w:num w:numId="2"><w:abstractNumId w:val="0"/><w:lvlOverride w:ilvl="0"><w:startOverride w:val="3"/></w:lvlOverride></w:num></w:numbering>') % W
z = zipfile.ZipFile(T + '/fora.docx', 'w')
z.writestr('[Content_Types].xml', '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
           '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/>'
           '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
           '<Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/></Types>')
z.writestr('_rels/.rels', '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
           '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>')
z.writestr('word/_rels/document.xml.rels', '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
           '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/></Relationships>')
z.writestr('word/document.xml', '<?xml version="1.0"?><w:document %s><w:body>%s</w:body></w:document>' % (W, corpo))
z.writestr('word/numbering.xml', '<?xml version="1.0"?>' + num)
z.close()
P
"$B" --resumo "$T/fora.docx" > "$T/fora.txt"
"$B" --converter "$T/fora.docx" "$T/fora.odt"
"$B" --resumo "$T/fora.odt" > "$T/fora-odt.txt"
"$B" --converter "$T/fora.odt" "$T/fora-2.docx"
"$B" --resumo "$T/fora-2.docx" > "$T/fora-2.txt"
grep -qF '[corpo] lista-num1 desde3 "tres"' "$T/fora.txt" || { echo "FALHOU: .docx de fora (a lista que continua)"; falhou=1; }
grep -qF 'tabela 41x2 cabecalho1' "$T/fora.txt" || { echo "FALHOU: .docx de fora (o cabeçalho da tabela)"; falhou=1; }
diff -u "$T/fora.txt" "$T/fora-odt.txt" || { echo "FALHOU: .docx de fora pelo .odt"; falhou=1; }
diff -u "$T/fora.txt" "$T/fora-2.txt" || { echo "FALHOU: .docx de fora de volta"; falhou=1; }
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

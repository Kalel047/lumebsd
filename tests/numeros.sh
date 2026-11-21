#!/bin/sh
# Testes do Números (lume-numeros) sem janela: cada modelo gravado em .xlsx
# e em .ods tem que voltar igual (fórmulas, valores, formatos, larguras e
# gráficos); o .xlsx convertido para .ods e de volta também; as contas dos
# modelos têm que dar os valores conhecidos (conferidos à parte); a edição
# pelos caminhos da janela (autoteste) tem que sair como esperado; o CSV vai
# e volta; e os arquivos têm que ser zips com XML válido (e o mimetype do
# ODF primeiro, sem compressão).
#
#   sh tests/numeros.sh BINARIO [PASTA]
set -eu
B=$1
T=${2:-$(mktemp -d)}
mkdir -p "$T"
export QT_QPA_PLATFORM=offscreen
falhou=0
for m in branco orcamento notas juros fluxo indicadores; do
	"$B" --modelo $m "$T/$m.xlsx"
	"$B" --modelo $m "$T/$m.ods"
	"$B" --resumo "$T/$m.xlsx" > "$T/$m.xlsx.txt"
	"$B" --resumo "$T/$m.ods" > "$T/$m.ods.txt"
	"$B" --converter "$T/$m.xlsx" "$T/$m-2.ods"
	"$B" --converter "$T/$m-2.ods" "$T/$m-3.xlsx"
	"$B" --resumo "$T/$m-3.xlsx" > "$T/$m-3.txt"
	"$B" --converter "$T/$m.xlsx" "$T/$m.pdf"
	if ! diff -u "$T/$m.xlsx.txt" "$T/$m.ods.txt" || ! diff -u "$T/$m.xlsx.txt" "$T/$m-3.txt"; then
		echo "FALHOU: $m não volta igual"
		falhou=1
	fi
	head -c 5 "$T/$m.pdf" | grep -q '%PDF-' || { echo "FALHOU: $m.pdf"; falhou=1; }
done
# As contas (VPL e TIR a 12%, o saldo dos juros conferido com o VF, as
# médias da turma, o SOMASE do orçamento).
tem() { grep -qF -- "$2" "$T/$1.xlsx.txt" || { echo "FALHOU: $1 sem: $2"; falhou=1; }; }
tem fluxo 'G6 =B6+VPL(C3;B7:B11) -> "R$ 6.848,54"'
tem fluxo 'G7 =TIR(B6:B11) -> "16,95%"'
tem juros 'E6 =-VF(B5;B6;B4;B3) -> "R$ 13.928,96"'
tem juros 'E44 =B44+C44+D44 -> "R$ 13.928,96"'
tem notas 'F16 =MÉDIA(F5:F14) -> "6,85"'
tem notas 'F19 =CONT.SE(G5:G14;"Aprovado") -> "7"'
# O gráfico copiado (o que vai para o Páginas e o Palco), com os números de
# verdade da planilha; e o que não existe dá erro.
G='lume-grafico:linhas;PIB, inflação e juros|2015;2016;2017;2018;2019;2020;2021;2022;2023;2024|PIB (%);-3.5;-3.3;1.3;1.8;1.2;-3.3;4.8;3;3.2;3.4|IPCA (%);10.67;6.29;2.95;3.75;4.31;4.52;10.06;5.79;4.62;4.83|Selic (%);14.25;13.75;7;6.5;4.5;2;9.25;13.75;11.75;12.25'
[ "$("$B" --copiar-grafico "$T/indicadores.xlsx" 1)" = "$G" ] || { echo "FALHOU: --copiar-grafico"; falhou=1; }
[ "$("$B" --copiar-grafico "$T/indicadores.ods" 1)" = "$G" ] || { echo "FALHOU: --copiar-grafico .ods"; falhou=1; }
if "$B" --copiar-grafico "$T/indicadores.xlsx" 9 >/dev/null 2>&1; then echo "FALHOU: --copiar-grafico de gráfico que não existe"; falhou=1; fi
tem orcamento 'H9 =SOMASE($B$5:$B$16;"Receita";$D$5:$D$16) -> "R$ 5.550,00"'
tem orcamento 'grafico pizza "Para onde foi o dinheiro" G4:H7'
tem fluxo 'grafico colunas "Fluxo e acumulado" A5:C11'
# A edição pelos caminhos da janela.
"$B" --autoteste "$T/auto.xlsx" > "$T/auto.txt"
"$B" --autoteste "$T/auto.ods" > "$T/auto-ods.txt"
cat > "$T/auto-esperado.txt" <<'E'
  A2 "Café"
  B2 "R$ 18,90" [moeda 2]
  C2 "3"
  D2 =B2*C2 -> "R$ 56,70" [moeda 2]
  A3 "Leite"
  B3 "R$ 5,49" [moeda 2]
  C3 "6"
  D3 =B3*C3 -> "R$ 32,94" [moeda 2]
  A4 "Pão"
  B4 "R$ 0,75" [moeda 2]
  C4 "12"
  D4 =B4*C4 -> "R$ 9,00" [moeda 2]
  A5 "Açúcar"
  B5 "R$ 4,20" [moeda 2]
  C5 "2"
  D5 =B5*C5 -> "R$ 8,40" [moeda 2]
  A6 "Soma"
  B6 "" [moeda 2]
  D6 =SOMA(D2:D5) -> "R$ 107,04" [moeda 2]
  A8 "Café"
  B8 "R$ 18,90" [moeda 2]
  C8 "3"
  D8 =B8*C8 -> "R$ 56,70" [moeda 2]
  grafico colunas "" A1:D5 em 410,0 480x300
folha "Planilha 2" congelar 0,0
  A1 ='Planilha 1'!D6*2 -> "214,08"
E
sed -n '7,32p' "$T/auto.txt" | diff -u "$T/auto-esperado.txt" - || { echo "FALHOU: autoteste"; falhou=1; }
grep -q '^  A1 "Produto" \[geral N\]$' "$T/auto.txt" || { echo "FALHOU: autoteste (negrito)"; falhou=1; }
diff -u "$T/auto.txt" "$T/auto-ods.txt" || { echo "FALHOU: autoteste .ods"; falhou=1; }
# CSV: os valores vão e voltam (moeda e porcentagem reconhecidas).
"$B" --converter "$T/orcamento.xlsx" "$T/orcamento.csv"
"$B" --converter "$T/orcamento.csv" "$T/orcamento-csv.xlsx"
"$B" --resumo "$T/orcamento-csv.xlsx" > "$T/orcamento-csv.txt"
grep -qF 'C5 "R$ 4.800,00" [moeda 2]' "$T/orcamento-csv.txt" || { echo "FALHOU: CSV (moeda)"; falhou=1; }
grep -qF 'I5 "52,8%" [porcento 1]' "$T/orcamento-csv.txt" || { echo "FALHOU: CSV (porcentagem)"; falhou=1; }
# Um .xlsx "de fora" (como os do Google Planilhas) com uma data num formato
# próprio ("d.m": dia e mês): aparece como no Excel e vai e volta pelo .ods.
python3 - "$T" <<'P'
import sys, zipfile
T = sys.argv[1]
S = 'xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"'
z = zipfile.ZipFile(T + '/fora.xlsx', 'w')
z.writestr('[Content_Types].xml', '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
           '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/>'
           '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
           '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
           '<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>')
z.writestr('_rels/.rels', '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
           '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>')
z.writestr('xl/_rels/workbook.xml.rels', '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
           '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>'
           '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>')
z.writestr('xl/workbook.xml', '<?xml version="1.0"?><workbook %s xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
           '<sheets><sheet name="Datas" sheetId="1" r:id="rId1"/></sheets></workbook>' % S)
z.writestr('xl/styles.xml', '<?xml version="1.0"?><styleSheet %s><numFmts count="2"><numFmt numFmtId="164" formatCode="d.m"/>'
           '<numFmt numFmtId="165" formatCode="mmm/yy"/></numFmts><fonts count="1"><font><sz val="10"/><name val="Arial"/></font></fonts>'
           '<fills count="1"><fill><patternFill patternType="none"/></fill></fills><borders count="1"><border/></borders>'
           '<cellXfs count="3"><xf numFmtId="0"/><xf numFmtId="164" applyNumberFormat="1"/><xf numFmtId="165" applyNumberFormat="1"/></cellXfs></styleSheet>' % S)
z.writestr('xl/worksheets/sheet1.xml', '<?xml version="1.0"?><worksheet %s><sheetData><row r="1"><c r="A1" s="1"><v>45109</v></c>'
           '<c r="B1" s="2"><v>45109</v></c></row></sheetData></worksheet>' % S)
z.close()
P
"$B" --resumo "$T/fora.xlsx" > "$T/fora.txt"
"$B" --converter "$T/fora.xlsx" "$T/fora.ods"
"$B" --resumo "$T/fora.ods" > "$T/fora-ods.txt"
grep -qF 'A1 "2.7" [data d.m]' "$T/fora.txt" || { echo "FALHOU: data d.m"; falhou=1; }
grep -qF 'B1 "jul/23" [data mmm/yy]' "$T/fora.txt" || { echo "FALHOU: data mmm/yy"; falhou=1; }
diff -u "$T/fora.txt" "$T/fora-ods.txt" || { echo "FALHOU: datas pelo .ods"; falhou=1; }
# Zips e XML.
python3 - "$T" <<'P' || falhou=1
import sys, glob, zipfile
from xml.dom import minidom
for f in glob.glob(sys.argv[1] + '/*.xlsx') + glob.glob(sys.argv[1] + '/*.ods'):
    z = zipfile.ZipFile(f)
    assert z.testzip() is None, f
    for n in z.namelist():
        if n.endswith('.xml') or n.endswith('.rels'):
            minidom.parseString(z.read(n))
    if f.endswith('.ods'):
        i = z.infolist()[0]
        assert i.filename == 'mimetype' and i.compress_type == 0, f
P
[ $falhou -eq 0 ] && echo "Números: tudo certo"
exit $falhou

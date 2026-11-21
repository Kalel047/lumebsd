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

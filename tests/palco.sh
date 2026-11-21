#!/bin/sh
# Testes do Palco (lume-palco) sem janela: cada modelo gravado em .pptx e em
# .odp tem que voltar igual (slides, leiautes, posições, texto com a letra,
# tópicos e níveis, formas, gráficos com os dados, transições, notas); o
# .pptx convertido para .odp e de volta também; o PDF sai com uma página por
# slide; a edição pelos caminhos da janela (autoteste) tem que sair como
# esperado; os arquivos têm que ser zips com XML válido (e o mimetype do ODF
# primeiro, sem compressão). Com o python-pptx instalado, um .pptx feito por
# ele (com tudo herdado do mestre, como os do PowerPoint) também é lido.
#
#   sh tests/palco.sh BINARIO [PASTA]
set -eu
B=$1
T=${2:-$(mktemp -d)}
mkdir -p "$T"
export QT_QPA_PLATFORM=offscreen
falhou=0
for m in branco aula seminario conjuntura reuniao; do
	"$B" --modelo $m "$T/$m.pptx"
	"$B" --modelo $m "$T/$m.odp"
	"$B" --resumo "$T/$m.pptx" > "$T/$m.pptx.txt"
	"$B" --resumo "$T/$m.odp" > "$T/$m.odp.txt"
	"$B" --converter "$T/$m.pptx" "$T/$m-2.odp"
	"$B" --converter "$T/$m-2.odp" "$T/$m-3.pptx"
	"$B" --resumo "$T/$m-3.pptx" > "$T/$m-3.txt"
	"$B" --converter "$T/$m.pptx" "$T/$m.pdf"
	if ! diff -u "$T/$m.pptx.txt" "$T/$m.odp.txt" || ! diff -u "$T/$m.pptx.txt" "$T/$m-3.txt"; then
		echo "FALHOU: $m não volta igual"
		falhou=1
	fi
	head -c 5 "$T/$m.pdf" | grep -q '%PDF-' || { echo "FALHOU: $m.pdf"; falhou=1; }
done
# O que os modelos têm que ter.
tem() { grep -qF -- "$2" "$T/$1.pptx.txt" || { echo "FALHOU: $1 sem: $2"; falhou=1; }; }
tem aula 'palco 960x540, tema Lousa, 6 slides, 0 imagens, 1 gráficos'
tem aula 'pizza "3/8 da pizza" 1 séries x 2 categorias, soma 8.00'
tem aula 'slide 6 [Seção] transição pelo preto'
tem aula '    1. Qual é maior: 2/3 ou 3/5?'
tem aula '      • embaixo do traço'
tem aula '  notas: Pedir para a turma contar os pedaços em voz alta.'
tem conjuntura 'colunas "PIB: variação real no ano (%)" 1 séries x 10 categorias, soma 8.60'
tem conjuntura 'linhas "IPCA no ano e Selic no fim do ano (%)" 2 séries x 10 categorias, soma 152.79'
tem conjuntura 'slide 4 [Um número grande] transição ampliar'
tem conjuntura '    [centro] 7,1%'
tem reuniao 'pizza "Despesas do ano (exemplo)" 1 séries x 5 categorias, soma 131000.00'
tem seminario 'palco 960x540, tema Mar, 6 slides, 0 imagens, 0 gráficos'
tem seminario '  texto (tópicos 2) 494,151 394x335 22pt'
# Uma página de PDF por slide.
python3 - "$T/aula.pdf" <<'P' || { echo "FALHOU: páginas do PDF"; falhou=1; }
import re, sys
d = open(sys.argv[1], 'rb').read()
n = len(re.findall(rb'/Type\s*/Page[^s]', d))
assert n == 6, n
P
# A edição pelos caminhos da janela.
"$B" --autoteste "$T/auto.pptx" > "$T/auto.txt"
"$B" --autoteste "$T/auto.odp" > "$T/auto-odp.txt"
cat > "$T/auto-esperado.txt" <<'E'
palco 960x540, tema Lousa, 4 slides, 0 imagens, 1 gráficos
slide 1 [Título]
  texto (título) 77,140 806x151 54pt
    [centro] Minha aula
  texto (subtítulo) 115,313 730x86 24pt
    [centro] Ciências, {n,i:7º ano}
slide 2 [Título e tópicos] transição dissolver
  texto (título) 72,38 816x92 38pt
    O ciclo da água
  texto (tópicos) 72,151 816x335 24pt
    • Evaporação
      • o sol aquece a água
    • Condensação
  notas: Mostrar o vídeo da chuva.
slide 3 [Só o título] transição empurrar
  texto (título) 72,38 816x92 38pt
    Chuva por mês
  elipse 60,160 200x200 #2f4b6e 20pt
    [centro] Nuvem
  gráfico 320,140 580x360 colunas "Chuva (mm)" 2 séries x 4 categorias, soma 131.00
slide 4 [Seção] transição empurrar (pulado)
  texto (título) 77,194 806x119
  texto (subtítulo) 77,324 806x65
E
diff -u "$T/auto-esperado.txt" "$T/auto.txt" || { echo "FALHOU: autoteste"; falhou=1; }
diff -u "$T/auto.txt" "$T/auto-odp.txt" || { echo "FALHOU: autoteste .odp"; falhou=1; }
# Um .pptx do python-pptx: os lugares do título e do texto sem posição
# (vem do leiaute), as letras do mestre, um gráfico do PowerPoint, uma forma
# com o estilo do tema, uma imagem e as notas.
if python3 -c 'import pptx' 2>/dev/null; then
	python3 - "$T" <<'P'
import sys, zlib, struct
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.chart.data import CategoryChartData
from pptx.enum.chart import XL_CHART_TYPE
from pptx.enum.shapes import MSO_SHAPE
T = sys.argv[1]
def png(w, h):
    linha = b'\x00' + bytes([30, 120, 200]) * w
    def bloco(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    return (b'\x89PNG\r\n\x1a\n' + bloco(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + bloco(b'IDAT', zlib.compress(linha * h)) + bloco(b'IEND', b''))
open(T + '/azul.png', 'wb').write(png(200, 100))
p = Presentation()
s = p.slides.add_slide(p.slide_layouts[0])
s.shapes.title.text = 'Relatório trimestral'
s.placeholders[1].text = 'Equipe de vendas'
s = p.slides.add_slide(p.slide_layouts[1])
s.shapes.title.text = 'Resultados'
tf = s.placeholders[1].text_frame
tf.text = 'Receita subiu 12%'
q = tf.add_paragraph(); q.text = 'Puxada pelo Sul'; q.level = 1
s.notes_slide.notes_text_frame.text = 'Falar devagar'
s = p.slides.add_slide(p.slide_layouts[5])
s.shapes.title.text = 'Vendas por região'
cd = CategoryChartData(); cd.categories = ['Norte', 'Sul', 'Leste']
cd.add_series('2025', (10.5, 20, 15)); cd.add_series('2026', (12, 25, 14))
s.shapes.add_chart(XL_CHART_TYPE.COLUMN_CLUSTERED, Inches(1), Inches(1.6), Inches(8), Inches(5), cd)
s = p.slides.add_slide(p.slide_layouts[6])
sh = s.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(1), Inches(1), Inches(3), Inches(1.5)); sh.text = 'Forma com estilo'
sh2 = s.shapes.add_shape(MSO_SHAPE.OVAL, Inches(5), Inches(1), Inches(2), Inches(2)); sh2.rotation = 30
tb = s.shapes.add_textbox(Inches(1), Inches(4), Inches(4), Inches(1))
r = tb.text_frame.paragraphs[0].add_run(); r.text = 'Negrito'; r.font.bold = True; r.font.size = Pt(28)
s.shapes.add_picture(T + '/azul.png', Inches(6), Inches(4), Inches(2), Inches(1))
p.save(T + '/pp.pptx')
P
	"$B" --resumo "$T/pp.pptx" > "$T/pp.txt"
	pp() { grep -qF -- "$1" "$T/pp.txt" || { echo "FALHOU: python-pptx sem: $1"; falhou=1; }; }
	pp 'palco 720x540, tema Office Theme, 4 slides, 1 imagens, 1 gráficos'
	pp '  texto (título) 54,168 612x116 44pt'
	pp '  texto (tópicos) 36,126 648x356 32pt'
	pp '      • {28pt:Puxada pelo Sul}'
	pp '  notas: Falar devagar'
	pp 'colunas "" 2 séries x 3 categorias, soma 96.50'
	pp '  arredondado 72,72 216x108 #4f81bd borda #4f81bd 1.0pt 18pt'
	pp '  elipse 360,72 144x144 girado 30° #4f81bd borda #4f81bd 1.0pt'
	pp '  texto 72,288 288x72 28pt negrito'
	pp '  imagem 432,288 144x72 200x100'
	# E volta igual pelo .odp.
	"$B" --converter "$T/pp.pptx" "$T/pp.odp"
	"$B" --resumo "$T/pp.odp" > "$T/pp-odp.txt"
	diff -u "$T/pp.txt" "$T/pp-odp.txt" || { echo "FALHOU: python-pptx pelo .odp"; falhou=1; }
else
	echo "(sem o python-pptx: o teste do .pptx de fora fica para depois)"
fi
# Zips e XML.
python3 - "$T" <<'P' || falhou=1
import sys, glob, zipfile
from xml.dom import minidom
for f in glob.glob(sys.argv[1] + '/*.pptx') + glob.glob(sys.argv[1] + '/*.odp'):
    z = zipfile.ZipFile(f)
    assert z.testzip() is None, f
    for n in z.namelist():
        if n.endswith('.xml') or n.endswith('.rels'):
            minidom.parseString(z.read(n))
    if f.endswith('.odp'):
        i = z.infolist()[0]
        assert i.filename == 'mimetype' and i.compress_type == 0, f
P
[ $falhou -eq 0 ] && echo "Palco: tudo certo"
exit $falhou

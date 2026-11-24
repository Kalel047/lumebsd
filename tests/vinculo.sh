#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# O gráfico vinculado (um Replicant dentro de outro app): um gráfico dos
# Números no Páginas e no Palco guarda de onde veio no nome
# (lume-vinculo:ARQUIVO#N) e se atualiza com a planilha. O vínculo vai e
# volta pelo .docx, .odt, .pptx e .odp; --atualizar traz os dados novos
# (o que a janela faz ao abrir e quando a planilha é gravada); e um
# vínculo para uma planilha que sumiu deixa o gráfico como estava.
#
#   sh tests/vinculo.sh LUME-NUMEROS LUME-PAGINAS LUME-PALCO PASTA
set -u
N=$(realpath "$1"); P=$(realpath "$2"); L=$(realpath "$3"); T=$4
rm -rf "$T"; mkdir -p "$T"; T=$(realpath "$T")
export QT_QPA_PLATFORM=offscreen LUME_NUMEROS="$N" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
falhou=0
erro() { echo "FALHOU: $*"; falhou=1; }

"$N" --modelo indicadores "$T/ind.xlsx"
"$P" --autoteste "$T/pa.odt" >/dev/null
"$L" --autoteste "$T/pl.pptx" >/dev/null 2>&1

# O nome do gráfico colado (o fim dos autotestes) aponta para a planilha,
# como os Números põem na área de transferência e o Office guarda.
cat > "$T/vincular.py" <<'PY'
import re, sys, zipfile
a, b, parte, padrao, nome = sys.argv[1:6]
with zipfile.ZipFile(a) as z, zipfile.ZipFile(b, 'w', zipfile.ZIP_DEFLATED) as o:
    for i in z.infolist():
        d = z.read(i.filename)
        if i.filename == parte:
            d, n = re.subn(padrao.encode(), (r'\g<1>' + nome + '"').encode(), d)
            assert n == 1, (parte, n)
        o.writestr(i, d)
PY
V="lume-vinculo:$T/ind.xlsx#1"
python3 "$T/vincular.py" "$T/pa.odt" "$T/doc.odt" content.xml '(<draw:frame draw:name=")Gráfico 1"' "$V"
python3 "$T/vincular.py" "$T/pl.pptx" "$T/apr.pptx" ppt/slides/slide5.xml '(<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id="\d+" name=")[^"]*"' "$V"

GP='{grafico linhas "PIB, inflação e juros" 3x10 soma %s 600x360 vinculado ind.xlsx#1}'
GL='gráfico 192,108 576x324 linhas "PIB, inflação e juros" 3 séries x 10 categorias, soma %s vinculado ind.xlsx#1'
confere() {	# confere ARQUIVO SOMA
	case $1 in
	*.odt|*.docx) e=$(printf "$GP" "$2"); "$P" --resumo "$1" | grep -qF "$e" || erro "$1: esperava $e" ;;
	*) e=$(printf "$GL" "$2"); "$L" --resumo "$1" 2>/dev/null | grep -qF "$e" || erro "$1: esperava $e" ;;
	esac
}
confere "$T/doc.odt" 161.39
confere "$T/apr.pptx" 161.39
[ "$("$P" --atualizar "$T/doc.odt")" = "0 gráfico(s) atualizado(s)" ] || erro "Páginas: atualizou sem a planilha mudar"
[ "$("$L" --atualizar "$T/apr.pptx" 2>/dev/null)" = "0 gráfico(s) atualizado(s)" ] || erro "Palco: atualizou sem a planilha mudar"

# A planilha gravada de novo, com outro número.
python3 - "$T/ind.xlsx" <<'PY'
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
[ "$("$P" --atualizar "$T/doc.odt" "$T/doc.docx")" = "1 gráfico(s) atualizado(s)" ] || erro "Páginas: não atualizou"
[ "$("$L" --atualizar "$T/apr.pptx" "$T/apr.odp" 2>/dev/null)" = "1 gráfico(s) atualizado(s)" ] || erro "Palco: não atualizou"
confere "$T/doc.docx" 167.14
confere "$T/apr.odp" 167.14
# E vai e volta pelos outros formatos com o vínculo.
"$P" --converter "$T/doc.docx" "$T/doc2.odt"; confere "$T/doc2.odt" 167.14
"$L" --converter "$T/apr.odp" "$T/apr2.pptx" 2>/dev/null; confere "$T/apr2.pptx" 167.14
unzip -p "$T/doc.docx" word/document.xml | grep -qF "name=\"$V\"" || erro ".docx sem o vínculo no docPr"

# A planilha sumiu: o gráfico fica como estava (e o vínculo também).
mv "$T/ind.xlsx" "$T/ind-outro.xlsx"
[ "$("$P" --atualizar "$T/doc2.odt")" = "0 gráfico(s) atualizado(s)" ] || erro "Páginas: mexeu sem a planilha"
confere "$T/doc2.odt" 167.14

[ $falhou -eq 0 ] && echo "vínculos: tudo certo"
exit $falhou

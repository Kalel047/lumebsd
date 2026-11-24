// SPDX-License-Identifier: BSD-2-Clause
//
// LumeBSD, o escritório: o gráfico nos arquivos. O gráfico de verdade do
// Office (c:chartSpace, com a planilha dos dados embutida, que o "Editar
// dados" do PowerPoint e do Word abre) e o do ODF (o objeto de gráfico do
// Impress e do Writer, com a tabela de dados dentro); a leitura dos dois.
// Usado pelo Palco e pelo Páginas. Precisa, antes, do No, do ZipEscritor, do
// esc e do lerXml (a base de cada app, que são iguais).

#pragma once

#include "grafico.h"

#include <QSizeF>

#define GR_CAB "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"

inline QString grHex(const QColor &c) { return c.name().mid(1).toUpper(); }
inline qint64 grEmu(double pt) { return qint64(std::llround(pt * 12700)); }
inline QString grCm(double pt) { return QString::number(pt / 72.0 * 2.54, 'f', 3) + "cm"; }
inline QString grFamiliaCss(const QString &f) { return f.contains(' ') ? "'" + f + "'" : f; }
inline QString grPreench(const QColor &c)
{
	if (!c.isValid() || c.alpha() == 0)
		return "<a:noFill/>";
	QString s = "<a:solidFill><a:srgbClr val=\"" + grHex(c) + "\"";
	if (c.alpha() < 255)
		s += QString("><a:alpha val=\"%1\"/></a:srgbClr>").arg(c.alpha() * 100000 / 255);
	else
		s += "/>";
	return s + "</a:solidFill>";
}

// A letra da coluna (0: A, 1: B...).
inline QString colunaExcel(int c)
{
	QString s;
	for (c++; c > 0; c = (c - 1) / 26)
		s.prepend(QChar('A' + (c - 1) % 26));
	return s;
}

// A planilha embutida com os dados (o "Editar dados" do PowerPoint):
// as categorias na coluna A, uma série por coluna.
inline QByteArray planilhaDoGrafico(const DadosGrafico &g)
{
	ZipEscritor z;
	z.add("[Content_Types].xml", GR_CAB "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
	      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
	      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
	      "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
	      "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
	      "</Types>");
	z.add("_rels/.rels", GR_CAB "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
	      "</Relationships>");
	z.add("xl/workbook.xml", GR_CAB "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
	      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Planilha1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
	z.add("xl/_rels/workbook.xml.rels", GR_CAB "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
	      "</Relationships>");
	auto txt = [](const QString &ref, const QString &t) { return "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t>" + esc(t) + "</t></is></c>"; };
	const int nS = g.tipo == TipoGrafico::Pizza ? std::min<int>(1, int(g.series.size())) : int(g.series.size());
	QString s = GR_CAB "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData><row r=\"1\">";
	for (int k = 0; k < nS; k++)
		s += txt(colunaExcel(k + 1) + "1", g.series[k]);
	s += "</row>";
	for (int c = 0; c < g.categorias.size(); c++) {
		s += QString("<row r=\"%1\">").arg(c + 2) + txt(QString("A%1").arg(c + 2), g.categorias[c]);
		for (int k = 0; k < nS; k++) {
			const double v = k < int(g.valores.size()) && c < int(g.valores[size_t(k)].size()) ? g.valores[size_t(k)][size_t(c)] : 0;
			s += QString("<c r=\"%1%2\"><v>%3</v></c>").arg(colunaExcel(k + 1)).arg(c + 2).arg(QString::number(v, 'g', 15));
		}
		s += "</row>";
	}
	s += "</sheetData></worksheet>";
	z.add("xl/worksheets/sheet1.xml", s.toUtf8());
	return z.fechar();
}

inline QString grTxPr(const EstiloGrafico &e, double tam, const QColor &cor, bool negrito = false)
{
	return QString("<c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"%1\" b=\"%2\">").arg(qint64(std::llround(tam * 100))).arg(negrito ? 1 : 0) +
	       grPreench(cor) + "<a:latin typeface=\"" + esc(e.fonte) + "\"/></a:defRPr></a:pPr><a:endParaRPr lang=\"pt-BR\"/></a:p></c:txPr>";
}


inline QString graficoOoxml(const DadosGrafico &g, const EstiloGrafico &e, QSizeF tam, const QColor &fundoObj = QColor())
{
	const double letra = std::clamp(std::min(tam.width(), tam.height()) / 22, 9.0, 20.0);
	const bool pizza = g.tipo == TipoGrafico::Pizza;
	const int nS = pizza ? std::min<int>(1, int(g.series.size())) : int(g.series.size());
	const int nC = int(g.categorias.size());
	auto cor = [&](int i) { return e.serie.empty() ? e.texto : e.serie[size_t(i) % e.serie.size()]; };
	const QColor grade(e.apagado.red(), e.apagado.green(), e.apagado.blue());
	QString s = GR_CAB "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
	                "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
	                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
	                "<c:date1904 val=\"0\"/><c:lang val=\"pt-BR\"/><c:roundedCorners val=\"0\"/><c:chart>";
	if (!g.titulo.isEmpty())
		s += "<c:title><c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr/></a:pPr><a:r>" +
		     QString("<a:rPr lang=\"pt-BR\" sz=\"%1\" b=\"0\">").arg(qint64(std::llround(letra * 1.25 * 100))) + grPreench(e.texto) +
		     "<a:latin typeface=\"" + esc(e.fonte) + "\"/></a:rPr><a:t>" + esc(g.titulo) +
		     "</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/>";
	else
		s += "<c:autoTitleDeleted val=\"1\"/>";
	s += "<c:plotArea><c:layout/>";
	if (g.tipo == TipoGrafico::Linhas)
		s += "<c:lineChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>";
	else if (pizza)
		s += "<c:pieChart><c:varyColors val=\"1\"/>";
	else
		s += QString("<c:barChart><c:barDir val=\"%1\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>").arg(g.tipo == TipoGrafico::Barras ? "bar" : "col");
	const QString fimCat = "$A$" + QString::number(nC + 1);
	for (int k = 0; k < nS; k++) {
		const QString col = "$" + colunaExcel(k + 1) + "$";
		s += QString("<c:ser><c:idx val=\"%1\"/><c:order val=\"%1\"/>").arg(k);
		s += "<c:tx><c:strRef><c:f>Planilha1!" + col + "1</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>" + esc(g.series[k]) +
		     "</c:v></c:pt></c:strCache></c:strRef></c:tx>";
		if (g.tipo == TipoGrafico::Linhas)
			s += QString("<c:spPr><a:ln w=\"%1\" cap=\"rnd\">").arg(grEmu(std::max(2.0, letra * 0.22))) + grPreench(cor(k)) + "<a:round/></a:ln></c:spPr>" +
			     "<c:marker><c:symbol val=\"circle\"/><c:size val=\"5\"/><c:spPr>" + grPreench(cor(k)) + "<a:ln><a:noFill/></a:ln></c:spPr></c:marker>";
		else if (pizza)
			s += "<c:spPr><a:ln w=\"19050\">" + grPreench(e.fundo) + "</a:ln></c:spPr>";
		else
			s += "<c:spPr>" + grPreench(cor(k)) + "<a:ln><a:noFill/></a:ln></c:spPr><c:invertIfNegative val=\"0\"/>";
		if (pizza)
			for (int c = 0; c < nC; c++)
				s += QString("<c:dPt><c:idx val=\"%1\"/><c:bubble3D val=\"0\"/><c:spPr>").arg(c) + grPreench(cor(c)) + "<a:ln w=\"19050\">" +
				     grPreench(e.fundo) + "</a:ln></c:spPr></c:dPt>";
		if (pizza) {
			// O rótulo de cada fatia: claro no escuro, escuro no claro; sem
			// casa decimal quando a porcentagem é inteira (como no Palco).
			double total = 0;
			for (int c = 0; c < nC; c++)
				total += std::max(0.0, k < int(g.valores.size()) && c < int(g.valores[size_t(k)].size()) ? g.valores[size_t(k)][size_t(c)] : 0);
			s += "<c:dLbls>";
			for (int c = 0; c < nC; c++) {
				const double v = k < int(g.valores.size()) && c < int(g.valores[size_t(k)].size()) ? g.valores[size_t(k)][size_t(c)] : 0;
				const double pc = total > 0 ? std::max(0.0, v) / total * 100 : 0;
				const bool inteiro = std::fabs(pc - std::round(pc)) < 0.05;
				s += QString("<c:dLbl><c:idx val=\"%1\"/><c:numFmt formatCode=\"%2\" sourceLinked=\"0\"/><c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr>")
				         .arg(c)
				         .arg(inteiro ? "0%" : "0.0%") +
				     grTxPr(e, letra * 0.95, cor(c).lightness() < 150 ? QColor(0xff, 0xff, 0xff) : QColor(0x1c, 0x1b, 0x19)) +
				     QString("<c:dLblPos val=\"ctr\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"0\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/>"
				             "<c:showPercent val=\"%1\"/><c:showBubbleSize val=\"0\"/></c:dLbl>")
				         .arg(pc > 4 ? 1 : 0);
			}
			s += "<c:numFmt formatCode=\"0%\" sourceLinked=\"0\"/><c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr>" +
			     grTxPr(e, letra * 0.95, QColor(0xff, 0xff, 0xff)) +
			     "<c:dLblPos val=\"ctr\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"0\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/>"
			     "<c:showPercent val=\"1\"/><c:showBubbleSize val=\"0\"/><c:showLeaderLines val=\"0\"/></c:dLbls>";
		}
		s += "<c:cat><c:strRef><c:f>Planilha1!$A$2:" + fimCat + "</c:f><c:strCache>" + QString("<c:ptCount val=\"%1\"/>").arg(nC);
		for (int c = 0; c < nC; c++)
			s += QString("<c:pt idx=\"%1\"><c:v>").arg(c) + esc(g.categorias[c]) + "</c:v></c:pt>";
		s += "</c:strCache></c:strRef></c:cat>";
		s += "<c:val><c:numRef><c:f>Planilha1!" + col + "2:" + col + QString::number(nC + 1) + "</c:f><c:numCache><c:formatCode>General</c:formatCode>" +
		     QString("<c:ptCount val=\"%1\"/>").arg(nC);
		for (int c = 0; c < nC; c++) {
			const double v = k < int(g.valores.size()) && c < int(g.valores[size_t(k)].size()) ? g.valores[size_t(k)][size_t(c)] : 0;
			s += QString("<c:pt idx=\"%1\"><c:v>%2</c:v></c:pt>").arg(c).arg(QString::number(v, 'g', 15));
		}
		s += "</c:numCache></c:numRef></c:val>";
		if (g.tipo == TipoGrafico::Linhas)
			s += "<c:smooth val=\"0\"/>";
		s += "</c:ser>";
	}
	if (pizza)
		s += "<c:firstSliceAng val=\"0\"/></c:pieChart>";
	else {
		if (g.tipo == TipoGrafico::Linhas)
			s += "<c:marker val=\"1\"/>";
		else
			s += "<c:gapWidth val=\"60\"/><c:overlap val=\"-10\"/>";
		s += "<c:axId val=\"111111111\"/><c:axId val=\"222222222\"/>";
		s += g.tipo == TipoGrafico::Linhas ? "</c:lineChart>" : "</c:barChart>";
		const bool deitado = g.tipo == TipoGrafico::Barras;
		const QString linhaEixo = "<c:spPr><a:ln w=\"15240\">" + grPreench(e.texto) + "</a:ln></c:spPr>";
		s += QString("<c:catAx><c:axId val=\"111111111\"/><c:scaling><c:orientation val=\"%1\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"%2\"/>")
		         .arg(deitado ? "maxMin" : "minMax", deitado ? "l" : "b") +
		     "<c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"low\"/>" +
		     linhaEixo + grTxPr(e, letra * 0.9, e.apagado) +
		     "<c:crossAx val=\"222222222\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>";
		s += QString("<c:valAx><c:axId val=\"222222222\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"%1\"/>")
		         .arg(deitado ? "b" : "l") +
		     "<c:majorGridlines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"" + grHex(grade) +
		     "\"><a:alpha val=\"28000\"/></a:srgbClr></a:solidFill></a:ln></c:spPr></c:majorGridlines>"
		     "<c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>"
		     "<c:spPr><a:ln><a:noFill/></a:ln></c:spPr>" +
		     grTxPr(e, letra * 0.9, e.apagado) +
		     QString("<c:crossAx val=\"111111111\"/><c:crosses val=\"%1\"/><c:crossBetween val=\"between\"/></c:valAx>").arg("autoZero");
	}
	s += "<c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr></c:plotArea>";
	if (g.legenda)
		s += "<c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/>" + grTxPr(e, letra * 0.95, e.texto) + "</c:legend>";
	s += "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>";
	s += "<c:spPr>" + (fundoObj.isValid() ? grPreench(fundoObj) : QString("<a:noFill/>")) + "<a:ln><a:noFill/></a:ln></c:spPr>";
	s += grTxPr(e, letra, e.texto);
	s += "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData></c:chartSpace>";
	return s;
}

inline bool lerGraficoDoOffice(const No &x, DadosGrafico &g)
{
	const No *area = x.achar("c:plotArea");
	if (!area)
		return false;
	const No *tipo = nullptr;
	for (const No &k : area->filhos)
		if (k.nome.endsWith("Chart")) {
			tipo = &k;
			break;
		}
	if (!tipo)
		return false;
	g = DadosGrafico();
	if (tipo->nome == "c:barChart" || tipo->nome == "c:bar3DChart") {
		const No *dir = tipo->filho("c:barDir");
		g.tipo = dir && dir->a("val") == "bar" ? TipoGrafico::Barras : TipoGrafico::Colunas;
	} else if (tipo->nome == "c:pieChart" || tipo->nome == "c:pie3DChart" || tipo->nome == "c:doughnutChart")
		g.tipo = TipoGrafico::Pizza;
	else
		g.tipo = TipoGrafico::Linhas;
	if (const No *t = x.achar("c:title")) {
		QString s;
		for (const No *tx = t->achar("c:rich"); tx; tx = nullptr)
			s = tx->textoTodo();
		g.titulo = s.simplified();
	}
	g.legenda = x.achar("c:legend") != nullptr;
	auto pontos = [](const No *n) {
		QMap<int, QString> r;
		if (!n)
			return r;
		for (const char *cache : {"c:strCache", "c:numCache", "c:strLit", "c:numLit"})
			if (const No *c = n->achar(cache)) {
				for (const No *pt : c->todos("c:pt"))
					r.insert(pt->a("idx").toInt(), pt->filho("c:v") ? pt->filho("c:v")->textoTodo() : QString());
				return r;
			}
		if (const No *ml = n->achar("c:multiLvlStrCache"))
			if (const No *lv = ml->filho("c:lvl"))
				for (const No *pt : lv->todos("c:pt"))
					r.insert(pt->a("idx").toInt(), pt->filho("c:v") ? pt->filho("c:v")->textoTodo() : QString());
		return r;
	};
	int nCat = 0;
	for (const No *s : tipo->todos("c:ser")) {
		const QMap<int, QString> cat = pontos(s->filho("c:cat"));
		const QMap<int, QString> val = pontos(s->filho("c:val") ? s->filho("c:val") : s->filho("c:yVal"));
		int n = 0;
		for (auto it = val.cbegin(); it != val.cend(); ++it)
			n = std::max(n, it.key() + 1);
		for (auto it = cat.cbegin(); it != cat.cend(); ++it)
			n = std::max(n, it.key() + 1);
		if (g.categorias.isEmpty() && !cat.isEmpty())
			for (int k = 0; k < n; k++)
				g.categorias << cat.value(k);
		nCat = std::max(nCat, n);
		QString nome;
		if (const No *tx = s->filho("c:tx"))
			nome = (tx->achar("c:v") ? tx->achar("c:v")->textoTodo() : tx->achar("c:f") ? QString() : tx->textoTodo()).simplified();
		g.series << (nome.isEmpty() ? QString("Série %1").arg(g.series.size() + 1) : nome);
		std::vector<double> v;
		for (int k = 0; k < n; k++)
			v.push_back(val.value(k).toDouble());
		g.valores.push_back(v);
		if (g.tipo == TipoGrafico::Pizza)
			break;
	}
	while (g.categorias.size() < nCat)
		g.categorias << QString::number(g.categorias.size() + 1);
	for (auto &v : g.valores)
		v.resize(size_t(nCat), 0);
	return !g.valores.empty();
}

inline QString graficoOdfDoGrafico(const DadosGrafico &g, const EstiloGrafico &e, QSizeF tam, const QColor &fundoObj = QColor())
{
	const double letra = std::clamp(std::min(tam.width(), tam.height()) / 22, 9.0, 20.0);
	const bool pizza = g.tipo == TipoGrafico::Pizza;
	const int nS = pizza ? std::min<int>(1, int(g.series.size())) : int(g.series.size());
	const int nC = int(g.categorias.size());
	auto cor = [&](int i) { return e.serie.empty() ? e.texto : e.serie[size_t(i) % e.serie.size()]; };
	auto txt = [&](double k, const QColor &c) {
		return QString("<style:text-properties fo:font-size=\"%1pt\" fo:font-family=\"%2\" fo:color=\"%3\"/>")
		    .arg(QString::number(letra * k, 'f', 1), esc(grFamiliaCss(e.fonte)), c.name());
	};
	QString est, corpo;
	int n = 0;
	auto st = [&](const QString &dentro, const QString &atr = QString()) {
		const QString nome = QString("ch%1").arg(++n);
		est += "<style:style style:name=\"" + nome + "\" style:family=\"chart\"" + atr + ">" + dentro + "</style:style>";
		return nome;
	};
	// A porcentagem da pizza: sem casa decimal quando é inteira.
	est += "<number:percentage-style style:name=\"P0\"><number:number number:decimal-places=\"0\" number:min-decimal-places=\"0\" "
	       "number:min-integer-digits=\"1\"/><number:text>%</number:text></number:percentage-style>"
	       "<number:percentage-style style:name=\"P1\"><number:number number:decimal-places=\"1\" number:min-decimal-places=\"1\" "
	       "number:min-integer-digits=\"1\"/><number:text>%</number:text></number:percentage-style>";
	double total = 0;
	if (pizza && !g.valores.empty())
		for (double v : g.valores[0])
			total += std::max(0.0, v);
	const QString cls = g.tipo == TipoGrafico::Linhas ? "chart:line" : pizza ? "chart:circle" : "chart:bar";
	const QString fundo = fundoObj.isValid() ? "draw:fill=\"solid\" draw:fill-color=\"" + fundoObj.name() + "\"" : QString("draw:fill=\"none\"");
	corpo += "<chart:chart svg:width=\"" + grCm(tam.width()) + "\" svg:height=\"" + grCm(tam.height()) + "\" chart:class=\"" + cls +
	         "\" chart:style-name=\"" + st("<style:graphic-properties draw:stroke=\"none\" " + fundo + "/>") + "\">";
	if (!g.titulo.isEmpty())
		corpo += "<chart:title chart:style-name=\"" + st(txt(1.25, e.texto)) + "\"><text:p>" + esc(g.titulo) + "</text:p></chart:title>";
	if (g.legenda)
		corpo += "<chart:legend chart:legend-position=\"bottom\" style:legend-expansion=\"wide\" chart:style-name=\"" +
		         st("<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>" + txt(0.95, e.texto)) + "\"/>";
	corpo += QString("<chart:plot-area chart:data-source-has-labels=\"both\" chart:style-name=\"%1\">")
	             .arg(st(QString("<style:chart-properties chart:vertical=\"%1\"/>").arg(g.tipo == TipoGrafico::Barras ? "true" : "false")));
	const QString fimCat = QString::number(nC + 1);
	if (!pizza) {
		corpo += "<chart:axis chart:dimension=\"x\" chart:name=\"primary-x\" chart:style-name=\"" +
		         st(QString("<style:chart-properties chart:display-label=\"true\" chart:tick-marks-major-inner=\"false\" chart:tick-marks-major-outer=\"false\" "
		                    "chart:reverse-direction=\"%1\" chart:axis-label-position=\"outside-start\"/><style:graphic-properties svg:stroke-color=\"%2\"/>")
		                .arg(g.tipo == TipoGrafico::Barras ? "true" : "false", e.texto.name()) +
		            txt(0.9, e.apagado)) +
		         "\"><chart:categories table:cell-range-address=\"local-table.$A$2:.$A$" + fimCat + "\"/></chart:axis>";
		corpo += "<chart:axis chart:dimension=\"y\" chart:name=\"primary-y\" chart:style-name=\"" +
		         st("<style:chart-properties chart:display-label=\"true\" chart:tick-marks-major-inner=\"false\" chart:tick-marks-major-outer=\"false\"" +
		            QString(g.tipo == TipoGrafico::Linhas ? "" : " chart:gap-width=\"60\" chart:overlap=\"-10\"") +
		            "/><style:graphic-properties draw:stroke=\"none\"/>" + txt(0.9, e.apagado)) +
		         "\"><chart:grid chart:class=\"major\" chart:style-name=\"" +
		         st("<style:graphic-properties svg:stroke-color=\"" + e.apagado.name() + "\" svg:stroke-opacity=\"28%\"/>") + "\"/></chart:axis>";
	} else
		corpo += "<chart:axis chart:dimension=\"x\" chart:name=\"primary-x\"><chart:categories table:cell-range-address=\"local-table.$A$2:.$A$" + fimCat +
		         "\"/></chart:axis><chart:axis chart:dimension=\"y\" chart:name=\"primary-y\" chart:style-name=\"" +
		         st("<style:chart-properties chart:reverse-direction=\"true\"/>") + "\"/>";
	for (int k = 0; k < nS; k++) {
		const QString col = colunaExcel(k + 1);
		QString ser;
		if (g.tipo == TipoGrafico::Linhas)
			ser = QString("<style:chart-properties chart:symbol-type=\"named-symbol\" chart:symbol-name=\"circle\" chart:symbol-width=\"0.15cm\" chart:symbol-height=\"0.15cm\"/>"
			              "<style:graphic-properties svg:stroke-width=\"%1\" svg:stroke-color=\"%2\" draw:fill-color=\"%2\"/>")
			          .arg(grCm(std::max(2.0, letra * 0.22)), cor(k).name());
		else if (pizza)
			ser = "<style:chart-properties chart:link-data-style-to-source=\"false\" chart:data-label-number=\"percentage\" chart:label-position=\"center\"/>"
			      "<style:graphic-properties draw:stroke=\"solid\" svg:stroke-color=\"" + e.fundo.name() + "\" svg:stroke-width=\"0.053cm\"/>" + txt(0.95, QColor(0xff, 0xff, 0xff));
		else
			ser = "<style:graphic-properties draw:stroke=\"none\" draw:fill-color=\"" + cor(k).name() + "\"/>";
		corpo += "<chart:series chart:style-name=\"" + st(ser) + "\" chart:values-cell-range-address=\"local-table.$" + col + "$2:.$" + col + "$" + fimCat +
		         "\" chart:label-cell-address=\"local-table.$" + col + "$1\" chart:class=\"" + cls + "\">";
		if (pizza)
			for (int c = 0; c < nC; c++)
				corpo += "<chart:data-point chart:style-name=\"" +
				         st("<style:chart-properties chart:link-data-style-to-source=\"false\" chart:data-label-number=\"percentage\" chart:label-position=\"center\"/>"
				            "<style:graphic-properties draw:fill=\"solid\" draw:fill-color=\"" + cor(c).name() + "\" draw:stroke=\"solid\" svg:stroke-color=\"" +
				            e.fundo.name() + "\" svg:stroke-width=\"0.053cm\"/>" +
				            txt(0.95, cor(c).lightness() < 150 ? QColor(0xff, 0xff, 0xff) : QColor(0x1c, 0x1b, 0x19)),
				            [&] {
					            const double v = g.valores.empty() || c >= int(g.valores[0].size()) ? 0 : g.valores[0][size_t(c)];
					            const double pc = total > 0 ? std::max(0.0, v) / total * 100 : 0;
					            return QString(" style:percentage-data-style-name=\"%1\"").arg(std::fabs(pc - std::round(pc)) < 0.05 ? "P0" : "P1");
				            }()) +
				         "\"/>";
		else
			corpo += QString("<chart:data-point chart:repeated=\"%1\"/>").arg(std::max(1, nC));
		corpo += "</chart:series>";
	}
	corpo += "<chart:wall chart:style-name=\"" + st("<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>") + "\"/></chart:plot-area>";
	// A tabela de dados.
	auto cel = [](const QString &t) { return "<table:table-cell office:value-type=\"string\"><text:p>" + esc(t) + "</text:p></table:table-cell>"; };
	corpo += "<table:table table:name=\"local-table\"><table:table-header-columns><table:table-column/></table:table-header-columns>"
	         "<table:table-columns><table:table-column table:number-columns-repeated=\"" + QString::number(std::max(1, nS)) + "\"/></table:table-columns>"
	         "<table:table-header-rows><table:table-row><table:table-cell><text:p/></table:table-cell>";
	for (int k = 0; k < nS; k++)
		corpo += cel(g.series[k]);
	corpo += "</table:table-row></table:table-header-rows><table:table-rows>";
	for (int c = 0; c < nC; c++) {
		corpo += "<table:table-row>" + cel(g.categorias[c]);
		for (int k = 0; k < nS; k++) {
			const double v = k < int(g.valores.size()) && c < int(g.valores[size_t(k)].size()) ? g.valores[size_t(k)][size_t(c)] : 0;
			const QString x = QString::number(v, 'g', 15);
			corpo += "<table:table-cell office:value-type=\"float\" office:value=\"" + x + "\"><text:p>" + x + "</text:p></table:table-cell>";
		}
		corpo += "</table:table-row>";
	}
	corpo += "</table:table-rows></table:table></chart:chart>";
	return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
	           "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
	           "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
	           "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
	           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\" "
	           "xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" office:version=\"1.3\"><office:automatic-styles>" +
	       est + "</office:automatic-styles><office:body><office:chart>" + corpo + "</office:chart></office:body></office:document-content>";
}

inline bool lerGraficoDoOdf(const No &x, DadosGrafico &g)
{
	const No *ch = x.achar("chart:chart");
	const No *tab = x.achar("table:table");
	if (!ch || !tab)
		return false;
	g = DadosGrafico();
	const QString classe = ch->a("chart:class");
	const No *pa = x.achar("chart:plot-area");
	bool vertical = false;
	if (pa) {
		QString st = pa->a("chart:style-name");
		for (const No *a = x.achar("office:automatic-styles"); a; a = nullptr)
			for (const No &k : a->filhos)
				if (k.a("style:name") == st)
					if (const No *cp = k.filho("style:chart-properties"))
						vertical = cp->a("chart:vertical") == "true";
	}
	g.tipo = classe == "chart:bar" ? (vertical ? TipoGrafico::Barras : TipoGrafico::Colunas)
	         : (classe == "chart:circle" || classe == "chart:ring") ? TipoGrafico::Pizza : TipoGrafico::Linhas;
	if (const No *t = ch->filho("chart:title"))
		g.titulo = t->textoTodo().simplified();
	g.legenda = ch->filho("chart:legend") != nullptr;
	auto celulas = [](const No &linha) {
		QStringList r;
		std::vector<double> v;
		for (const No *c : linha.todos("table:table-cell")) {
			r << c->textoTodo().trimmed();
			v.push_back(c->a("office:value").isEmpty() ? QLocale::c().toDouble(c->textoTodo().trimmed()) : c->a("office:value").toDouble());
		}
		return std::make_pair(r, v);
	};
	if (const No *hr = tab->achar("table:table-header-rows"))
		if (const No *l = hr->filho("table:table-row"))
			g.series = celulas(*l).first.mid(1);
	std::vector<std::vector<double>> linhas;
	if (const No *rows = tab->achar("table:table-rows"))
		for (const No *l : rows->todos("table:table-row")) {
			auto [txt, v] = celulas(*l);
			if (txt.isEmpty())
				continue;
			g.categorias << txt[0];
			v.erase(v.begin());
			linhas.push_back(v);
		}
	const int ns = std::max<int>(int(g.series.size()), linhas.empty() ? 0 : int(linhas[0].size()));
	while (g.series.size() < ns)
		g.series << QString("Série %1").arg(g.series.size() + 1);
	g.valores.assign(size_t(ns), std::vector<double>(size_t(g.categorias.size()), 0));
	for (size_t c = 0; c < linhas.size(); c++)
		for (size_t s = 0; s < linhas[c].size() && s < size_t(ns); s++)
			g.valores[s][c] = linhas[c][s];
	if (g.tipo == TipoGrafico::Pizza && g.valores.size() > 1) {
		g.valores.resize(1);
		g.series = g.series.mid(0, 1);
	}
	return !g.valores.empty() && !g.categorias.isEmpty();
}

#undef GR_CAB

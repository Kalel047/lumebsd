// SPDX-License-Identifier: BSD-2-Clause
//
// lume-numeros: os arquivos. O .xlsx (Excel) e o .ods (LibreOffice),
// gravar e ler, com as fórmulas, os formatos, as larguras, os painéis
// congelados e os gráficos; o CSV; e o PDF.

#pragma once

#include "base.h"
#include "desenho.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPdfWriter>
#include <QTextStream>

#include <set>

// --- formatos de número: Formato <-> código do Excel -----------------------

inline QString codigoExcel(const Formato &f)
{
	const QString casas = f.casas > 0 ? "." + QString(f.casas, '0') : QString();
	switch (f.tipo) {
	case Formato::Numero:
		return (f.milhar ? "#,##0" : "0") + casas;
	case Formato::Moeda:
		// O código de moeda de verdade (o Excel e o LibreOffice reconhecem
		// como moeda; "R$" entre aspas vira só um texto na frente).
		return "[$R$-416] #,##0" + casas + ";-[$R$-416] #,##0" + casas;
	case Formato::Porcento:
		return "0" + casas + "%";
	case Formato::Data:
		return f.codigoData.isEmpty() ? QString("dd/mm/yyyy") : f.codigoData;
	case Formato::Texto:
		return "@";
	default:
		return "General";
	}
}

inline void formatoDeCodigo(QString cod, Formato &f)
{
	// Só a primeira seção (a dos positivos), sem o que está entre aspas ou colchetes.
	QString limpo;
	bool aspas = false;
	int colchete = 0;
	const QString primeira = cod.section(';', 0, 0);
	const bool moedaNosColchetes = cod.contains("[$R$") || cod.contains("[$$") || cod.contains("[$€") || cod.contains("[$US$");
	for (QChar c : primeira) {
		if (c == '"') { aspas = !aspas; continue; }
		if (c == '[') { colchete++; continue; }
		if (c == ']') { colchete--; continue; }
		if (aspas || colchete > 0 || c == '\\' || c == '_' || c == '*') continue;
		limpo += c;
	}
	const QString l = limpo.toLower();
	const bool moeda = cod.contains("R$") || cod.contains('$') || cod.contains("€") || moedaNosColchetes;
	if (cod.compare("General", Qt::CaseInsensitive) == 0 || cod.compare("Standard", Qt::CaseInsensitive) == 0 || cod.isEmpty()) {
		f.tipo = Formato::Geral;
		return;
	}
	if (cod == "@") {
		f.tipo = Formato::Texto;
		return;
	}
	if (!l.contains('0') && !l.contains('#') && (l.contains('d') || l.contains('y') || l.contains('m') || l.contains('a'))) {
		f.tipo = Formato::Data;
		// O código como veio (sem a parte da língua entre colchetes), se não
		// for o dd/mm/aaaa de sempre.
		QString c = primeira;
		c.remove(QRegularExpression("\\[[^\\]]*\\]"));
		const QString cl = c.toLower();
		if (cl != "dd/mm/yyyy" && cl != "dd/mm/aaaa" && cl != "m/d/yyyy" && cl != "mm-dd-yy" && cl != "d/m/yyyy" && cl != "dd/mm/yy")
			f.codigoData = c;
		return;
	}
	auto casasDe = [&] {
		const int p = int(limpo.indexOf('.'));
		int n = 0;
		if (p >= 0)
			for (int i = p + 1; i < limpo.size() && (limpo[i] == '0' || limpo[i] == '#'); i++) n++;
		return n;
	};
	if (limpo.contains('%')) {
		f.tipo = Formato::Porcento;
		f.casas = casasDe();
		return;
	}
	if (moeda) {
		f.tipo = Formato::Moeda;
		f.casas = casasDe();
		return;
	}
	if (limpo.contains('0') || limpo.contains('#')) {
		f.tipo = Formato::Numero;
		f.casas = casasDe();
		f.milhar = limpo.contains(',');
		return;
	}
	f.tipo = Formato::Geral;
}

inline QString codigoEmbutido(int id)
{
	static const QHash<int, QString> m = {
		{0, "General"}, {1, "0"}, {2, "0.00"}, {3, "#,##0"}, {4, "#,##0.00"},
		{5, "\"R$\" #,##0"}, {6, "\"R$\" #,##0"}, {7, "\"R$\" #,##0.00"}, {8, "\"R$\" #,##0.00"},
		{9, "0%"}, {10, "0.00%"}, {11, "0.00E+00"}, {12, "# ?/?"}, {14, "dd/mm/yyyy"}, {15, "d-mmm-yy"},
		{16, "d-mmm"}, {17, "mmm-yy"}, {22, "dd/mm/yyyy hh:mm"}, {37, "#,##0"}, {38, "#,##0"},
		{39, "#,##0.00"}, {40, "#,##0.00"}, {44, "\"R$\" #,##0.00"}, {49, "@"}};
	return m.value(id, "General");
}

inline QString hexCor(const QColor &c) { return "FF" + c.name(QColor::HexRgb).mid(1).toUpper(); }
inline QColor corArgb(const QString &v)
{
	if (v.size() == 8)
		return QColor("#" + v.mid(2));
	if (v.size() == 6)
		return QColor("#" + v);
	return QColor();
}

inline QString numeroXml(double n)
{
	return QString::number(n, 'g', 17);
}

// --- XLSX: gravar ----------------------------------------------------------

class EscritorXlsx
{
public:
	explicit EscritorXlsx(Pasta &p) : p_(p) {}

	QByteArray gravar()
	{
		ZipEscritor z;
		QStringList abas;
		QString wbRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
		QString tipos = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
		                "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
		                "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
		                "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>";
		int nGrafico = 0, nDesenho = 0;
		for (int f = 0; f < p_.quantas(); f++) {
			const QString arq = QString("sheet%1.xml").arg(f + 1);
			const Folha &fo = p_.folha(f);
			QString desenho;
			if (!fo.graficos.empty()) {
				nDesenho++;
				desenho = QString("drawing%1.xml").arg(nDesenho);
				QString dRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
				                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
				QString d = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
				            "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
				            "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
				            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
				            "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">";
				int k = 0;
				for (const Grafico &g : fo.graficos) {
					nGrafico++;
					k++;
					const QString ch = QString("chart%1.xml").arg(nGrafico);
					z.add("xl/charts/" + ch, grafico(f, g).toUtf8());
					tipos += "<Override PartName=\"/xl/charts/" + ch + "\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>";
					dRels += "<Relationship Id=\"rId" + QString::number(k) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" Target=\"../charts/" + ch + "\"/>";
					int r1, c1, r2, c2;
					double dx1, dy1, dx2, dy2;
					p_.celulaEm(f, g.x, g.y, r1, c1, dx1, dy1);
					p_.celulaEm(f, g.x + g.w, g.y + g.h, r2, c2, dx2, dy2);
					d += "<xdr:twoCellAnchor editAs=\"oneCell\"><xdr:from><xdr:col>" + QString::number(c1) + "</xdr:col><xdr:colOff>" +
					     QString::number(qint64(dx1 * 9525)) + "</xdr:colOff><xdr:row>" + QString::number(r1) + "</xdr:row><xdr:rowOff>" +
					     QString::number(qint64(dy1 * 9525)) + "</xdr:rowOff></xdr:from><xdr:to><xdr:col>" + QString::number(c2) +
					     "</xdr:col><xdr:colOff>" + QString::number(qint64(dx2 * 9525)) + "</xdr:colOff><xdr:row>" + QString::number(r2) +
					     "</xdr:row><xdr:rowOff>" + QString::number(qint64(dy2 * 9525)) + "</xdr:rowOff></xdr:to>"
					     "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"" + QString::number(k + 1) + "\" name=\"Gráfico " +
					     QString::number(k) + "\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>"
					     "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic>"
					     "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart r:id=\"rId" +
					     QString::number(k) + "\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor>";
				}
				d += "</xdr:wsDr>";
				dRels += "</Relationships>";
				z.add("xl/drawings/" + desenho, d.toUtf8());
				z.add("xl/drawings/_rels/" + desenho + ".rels", dRels.toUtf8());
				tipos += "<Override PartName=\"/xl/drawings/" + desenho + "\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>";
				z.add("xl/worksheets/_rels/" + arq + ".rels",
				      ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
				       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				       "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/" +
				       desenho + "\"/></Relationships>").toUtf8());
			}
			z.add("xl/worksheets/" + arq, folha(f, !desenho.isEmpty()).toUtf8());
			tipos += "<Override PartName=\"/xl/worksheets/" + arq + "\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
			wbRels += "<Relationship Id=\"rId" + QString::number(f + 1) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/" + arq + "\"/>";
			abas << "<sheet name=\"" + esc(fo.nome) + "\" sheetId=\"" + QString::number(f + 1) + "\" r:id=\"rId" + QString::number(f + 1) + "\"/>";
		}
		const int n = p_.quantas();
		wbRels += "<Relationship Id=\"rId" + QString::number(n + 1) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
		          "<Relationship Id=\"rId" + QString::number(n + 2) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
		          "</Relationships>";
		tipos += "</Types>";
		z.add("[Content_Types].xml", tipos.toUtf8());
		z.add("_rels/.rels",
		      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
		      "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
		      "</Relationships>");
		z.add("docProps/app.xml",
		      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		      "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>LumeBSD Números</Application></Properties>");
		z.add("xl/workbook.xml",
		      ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		       "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
		       "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
		       "<bookViews><workbookView activeTab=\"0\"/></bookViews><sheets>" + abas.join("") +
		       "</sheets><calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>").toUtf8());
		z.add("xl/_rels/workbook.xml.rels", wbRels.toUtf8());
		z.add("xl/styles.xml", estilos().toUtf8());
		QString ss = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		             "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"" + QString::number(textos_.size()) +
		             "\" uniqueCount=\"" + QString::number(textos_.size()) + "\">";
		for (const QString &t : textos_)
			ss += "<si><t xml:space=\"preserve\">" + esc(t) + "</t></si>";
		ss += "</sst>";
		z.add("xl/sharedStrings.xml", ss.toUtf8());
		return z.fechar();
	}

private:
	int texto(const QString &t)
	{
		auto it = idxTexto_.find(t);
		if (it != idxTexto_.end())
			return *it;
		textos_ << t;
		idxTexto_.insert(t, int(textos_.size()) - 1);
		return int(textos_.size()) - 1;
	}

	int estilo(const Formato &f)
	{
		if (formatos_.empty())
			formatos_.push_back(Formato()); // o 0 é sempre o padrão
		for (size_t i = 0; i < formatos_.size(); i++)
			if (formatos_[i] == f)
				return int(i);
		formatos_.push_back(f);
		return int(formatos_.size()) - 1;
	}

	QString folha(int f, bool comDesenho)
	{
		const Folha &fo = p_.folha(f);
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
		            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
		s += "<sheetViews><sheetView workbookViewId=\"0\"" + QString(f == 0 ? " tabSelected=\"1\"" : "") + ">";
		if (fo.congelarLinhas > 0 || fo.congelarColunas > 0) {
			s += "<pane";
			if (fo.congelarColunas > 0) s += " xSplit=\"" + QString::number(fo.congelarColunas) + "\"";
			if (fo.congelarLinhas > 0) s += " ySplit=\"" + QString::number(fo.congelarLinhas) + "\"";
			s += " topLeftCell=\"" + nomeCelula(fo.congelarLinhas, fo.congelarColunas) + "\" activePane=\"" +
			     (fo.congelarLinhas && fo.congelarColunas ? "bottomRight" : fo.congelarLinhas ? "bottomLeft" : "topRight") + "\" state=\"frozen\"/>";
		}
		s += "</sheetView></sheetViews><sheetFormatPr defaultColWidth=\"" + QString::number((fo.larguraPadrao - 5) / 7.0, 'f', 2) + "\" defaultRowHeight=\"18\"/>";
		if (!fo.largura.empty()) {
			s += "<cols>";
			for (auto [c, w] : fo.largura)
				s += "<col min=\"" + QString::number(c + 1) + "\" max=\"" + QString::number(c + 1) + "\" width=\"" +
				     QString::number(std::max(0.5, (w - 5) / 7.0), 'f', 2) + "\" customWidth=\"1\"/>";
			s += "</cols>";
		}
		s += "<sheetData>";
		std::map<int, std::map<int, const Celula *>> linhas;
		for (auto it = fo.cel.begin(); it != fo.cel.end(); ++it)
			linhas[linhaDe(it.key())][colunaDeChave(it.key())] = &*it;
		for (auto [r, h] : fo.altura)
			linhas[r];
		for (auto &[r, cels] : linhas) {
			s += "<row r=\"" + QString::number(r + 1) + "\"";
			auto ah = fo.altura.find(r);
			if (ah != fo.altura.end())
				s += " ht=\"" + QString::number(ah->second * 0.75, 'f', 2) + "\" customHeight=\"1\"";
			s += ">";
			for (auto [c, k] : cels) {
				s += "<c r=\"" + nomeCelula(r, c) + "\"";
				const int st = k->fmt.padrao() ? 0 : estilo(k->fmt);
				if (st)
					s += " s=\"" + QString::number(st) + "\"";
				const Valor &v = k->v;
				if (k->f) {
					const QString tipo = v.t == Valor::Texto ? "str" : v.t == Valor::Logico ? "b" : v.t == Valor::Erro ? "e" : QString();
					if (!tipo.isEmpty())
						s += " t=\"" + tipo + "\"";
					s += "><f>" + esc(textoDe(k->f, Dialeto::Canonico, true)) + "</f>";
					if (v.t == Valor::Numero || v.t == Valor::Logico)
						s += "<v>" + numeroXml(v.n) + "</v>";
					else if (v.t == Valor::Texto || v.t == Valor::Erro)
						s += "<v>" + esc(v.s) + "</v>";
					s += "</c>";
					continue;
				}
				switch (v.t) {
				case Valor::Numero: s += "><v>" + numeroXml(v.n) + "</v></c>"; break;
				case Valor::Texto: s += " t=\"s\"><v>" + QString::number(texto(v.s)) + "</v></c>"; break;
				case Valor::Logico: s += " t=\"b\"><v>" + QString::number(int(v.n)) + "</v></c>"; break;
				case Valor::Erro: s += " t=\"e\"><v>" + esc(v.s) + "</v></c>"; break;
				default: s += "/>"; break;
				}
			}
			s += "</row>";
		}
		s += "</sheetData><pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>";
		if (comDesenho)
			s += "<drawing r:id=\"rId1\"/>";
		return s + "</worksheet>";
	}

	QString estilos()
	{
		// As fontes, os preenchimentos, as bordas e os formatos de número de
		// cada estilo usado (o 0 é o padrão).
		QStringList fontes, fills, numFmts, xfs;
		QMap<QString, int> idxFonte, idxFill, idxNum;
		const QString base = "<sz val=\"" + QString::number(p_.tamanho) + "\"/><name val=\"" + esc(p_.fonte) + "\"/>";
		fontes << "<font>" + base + "</font>";
		idxFonte.insert("", 0);
		fills << "<fill><patternFill patternType=\"none\"/></fill>" << "<fill><patternFill patternType=\"gray125\"/></fill>";
		if (formatos_.empty() || !formatos_[0].padrao())
			formatos_.insert(formatos_.begin(), Formato());
		int proxNum = 164;
		for (const Formato &f : formatos_) {
			QString fk;
			if (f.negrito) fk += "<b/>";
			if (f.italico) fk += "<i/>";
			if (f.sublinhado) fk += "<u/>";
			if (f.cor.isValid()) fk += "<color rgb=\"" + hexCor(f.cor) + "\"/>";
			const QString sz = f.tamanho > 0 ? "<sz val=\"" + QString::number(f.tamanho) + "\"/><name val=\"" + esc(p_.fonte) + "\"/>" : base;
			const QString fonte = fk.isEmpty() && f.tamanho <= 0 ? QString() : "<font>" + fk + sz + "</font>";
			if (!idxFonte.contains(fonte)) { idxFonte.insert(fonte, int(fontes.size())); fontes << fonte; }
			const QString fill = f.fundo.isValid() ? "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"" + hexCor(f.fundo) + "\"/><bgColor indexed=\"64\"/></patternFill></fill>" : QString();
			if (!fill.isEmpty() && !idxFill.contains(fill)) { idxFill.insert(fill, int(fills.size())); fills << fill; }
			const QString cod = codigoExcel(f);
			int numId = 0;
			if (cod == "@") numId = 49;
			else if (cod != "General") {
				if (!idxNum.contains(cod)) { idxNum.insert(cod, proxNum); numFmts << "<numFmt numFmtId=\"" + QString::number(proxNum) + "\" formatCode=\"" + esc(cod) + "\"/>"; proxNum++; }
				numId = idxNum.value(cod);
			}
			QString x = "<xf numFmtId=\"" + QString::number(numId) + "\" fontId=\"" + QString::number(idxFonte.value(fonte)) +
			            "\" fillId=\"" + QString::number(fill.isEmpty() ? 0 : idxFill.value(fill)) + "\" borderId=\"" + QString(f.borda ? "1" : "0") +
			            "\" xfId=\"0\"";
			if (numId) x += " applyNumberFormat=\"1\"";
			if (!fonte.isEmpty()) x += " applyFont=\"1\"";
			if (!fill.isEmpty()) x += " applyFill=\"1\"";
			if (f.borda) x += " applyBorder=\"1\"";
			if (f.alin) x += " applyAlignment=\"1\"><alignment horizontal=\"" + QString(f.alin == 1 ? "left" : f.alin == 2 ? "center" : "right") + "\"/></xf>";
			else x += "/>";
			xfs << x;
		}
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
		if (!numFmts.isEmpty())
			s += "<numFmts count=\"" + QString::number(numFmts.size()) + "\">" + numFmts.join("") + "</numFmts>";
		s += "<fonts count=\"" + QString::number(fontes.size()) + "\">" + fontes.join("") + "</fonts>";
		s += "<fills count=\"" + QString::number(fills.size()) + "\">" + fills.join("") + "</fills>";
		s += "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
		     "<border><left style=\"thin\"><color rgb=\"FF1C1B19\"/></left><right style=\"thin\"><color rgb=\"FF1C1B19\"/></right>"
		     "<top style=\"thin\"><color rgb=\"FF1C1B19\"/></top><bottom style=\"thin\"><color rgb=\"FF1C1B19\"/></bottom><diagonal/></border></borders>";
		s += "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>";
		s += "<cellXfs count=\"" + QString::number(xfs.size()) + "\">" + xfs.join("") + "</cellXfs>";
		s += "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";
		return s;
	}

	static QString refAbs(const QString &folha, int r1, int c1, int r2, int c2)
	{
		const QString f = folhaTexto(folha, false) + "!";
		const QString a = "$" + nomeColuna(c1) + "$" + QString::number(r1 + 1);
		if (r1 == r2 && c1 == c2)
			return f + a;
		return f + a + ":$" + nomeColuna(c2) + "$" + QString::number(r2 + 1);
	}

	QString grafico(int f, const Grafico &g)
	{
		QStringList cats;
		std::vector<Serie> series;
		p_.dadosDoGrafico(f, g, cats, series);
		const QString nome = p_.folha(f).nome;
		// Onde estão os nomes, as categorias e os valores.
		const Arranjo arr = p_.arranjo(f, g);
		const int nSeries = int(arr.cols.size());
		const int r0 = arr.r0;
		const bool cab = arr.cab, temCats = arr.cats;
		const int c0 = arr.cols.empty() ? g.c1 : arr.cols[0];
		QString tipo, abre, fecha;
		const bool pizza = g.tipo == Grafico::Pizza, disp = g.tipo == Grafico::Dispersao;
		switch (g.tipo) {
		case Grafico::Barras: tipo = "c:barChart"; abre = "<c:barDir val=\"bar\"/><c:grouping val=\"clustered\"/>"; fecha = "<c:gapWidth val=\"80\"/>"; break;
		case Grafico::Linhas: tipo = "c:lineChart"; abre = "<c:grouping val=\"standard\"/>"; fecha = "<c:marker val=\"1\"/>"; break;
		case Grafico::Area: tipo = "c:areaChart"; abre = "<c:grouping val=\"standard\"/>"; break;
		case Grafico::Pizza: tipo = "c:pieChart"; abre = "<c:varyColors val=\"1\"/>"; fecha = "<c:firstSliceAng val=\"0\"/>"; break;
		case Grafico::Dispersao: tipo = "c:scatterChart"; abre = "<c:scatterStyle val=\"lineMarker\"/>"; break;
		default: tipo = "c:barChart"; abre = "<c:barDir val=\"col\"/><c:grouping val=\"clustered\"/>"; fecha = "<c:gapWidth val=\"80\"/>"; break;
		}
		if (!pizza && !abre.contains("varyColors"))
			abre += "<c:varyColors val=\"0\"/>";
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		            "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
		            "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
		            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><c:roundedCorners val=\"0\"/><c:chart>";
		if (!g.titulo.isEmpty())
			s += "<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>" + esc(g.titulo) + "</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/>";
		else
			s += "<c:autoTitleDeleted val=\"1\"/>";
		s += "<c:plotArea><c:layout/><" + tipo + ">" + abre;
		const int ini = disp ? 1 : 0;
		for (int k = ini; k < nSeries; k++) {
			const int c = arr.cols[size_t(k)];
			const int idx = k - ini;
			s += "<c:ser><c:idx val=\"" + QString::number(idx) + "\"/><c:order val=\"" + QString::number(idx) + "\"/>";
			if (cab)
				s += "<c:tx><c:strRef><c:f>" + esc(refAbs(nome, g.r1, c, g.r1, c)) + "</c:f></c:strRef></c:tx>";
			if (!pizza) {
				const QString cor = corDaSerie(idx).name().mid(1).toUpper();
				if (g.tipo == Grafico::Linhas || disp)
					s += "<c:spPr><a:ln w=\"28575\"><a:solidFill><a:srgbClr val=\"" + cor + "\"/></a:solidFill></a:ln></c:spPr>";
				else
					s += "<c:spPr><a:solidFill><a:srgbClr val=\"" + cor + "\"/></a:solidFill></c:spPr>";
			}
			if (disp) {
				s += "<c:xVal><c:numRef><c:f>" + esc(refAbs(nome, r0, c0, g.r2, c0)) + "</c:f></c:numRef></c:xVal>";
				s += "<c:yVal><c:numRef><c:f>" + esc(refAbs(nome, r0, c, g.r2, c)) + "</c:f></c:numRef></c:yVal><c:smooth val=\"0\"/>";
			} else {
				if (temCats)
					s += "<c:cat><c:strRef><c:f>" + esc(refAbs(nome, r0, g.c1, g.r2, g.c1)) + "</c:f></c:strRef></c:cat>";
				s += "<c:val><c:numRef><c:f>" + esc(refAbs(nome, r0, c, g.r2, c)) + "</c:f></c:numRef></c:val>";
				if (g.tipo == Grafico::Linhas)
					s += "<c:smooth val=\"0\"/>";
			}
			s += "</c:ser>";
			if (pizza)
				break;
		}
		s += fecha;
		if (!pizza)
			s += "<c:axId val=\"500000001\"/><c:axId val=\"500000002\"/>";
		s += "</" + tipo + ">";
		if (!pizza) {
			const QString eixoX = disp ? "c:valAx" : "c:catAx";
			const bool barras = g.tipo == Grafico::Barras;
			s += "<" + eixoX + "><c:axId val=\"500000001\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/>"
			     "<c:axPos val=\"" + QString(barras ? "l" : "b") + "\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:tickLblPos val=\"nextTo\"/>"
			     "<c:crossAx val=\"500000002\"/><c:crosses val=\"autoZero\"/></" + eixoX + ">";
			s += "<c:valAx><c:axId val=\"500000002\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/>"
			     "<c:axPos val=\"" + QString(barras ? "b" : "l") + "\"/><c:majorGridlines/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
			     "<c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"500000001\"/><c:crosses val=\"autoZero\"/></c:valAx>";
		}
		s += "</c:plotArea><c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/></c:legend><c:plotVisOnly val=\"1\"/></c:chart></c:chartSpace>";
		return s;
	}

	Pasta &p_;
	QStringList textos_;
	QHash<QString, int> idxTexto_;
	std::vector<Formato> formatos_;
};

// --- XLSX: ler -------------------------------------------------------------

// "Planilha1!$B$2:$B$9" -> folha e o retângulo.
inline bool lerRefGrafico(const QString &f, QString &folha, int &r1, int &c1, int &r2, int &c2)
{
	PExpr e = analisar(f, Dialeto::Canonico);
	if (!e)
		e = analisar(f, Dialeto::Odf);
	if (!e || (e->k != Expr::Ref && e->k != Expr::Area))
		return false;
	folha = e->folha;
	r1 = e->r1;
	c1 = e->c1;
	r2 = e->k == Expr::Area ? e->r2 : e->r1;
	c2 = e->k == Expr::Area ? e->c2 : e->c1;
	return true;
}

class LeitorXlsx
{
public:
	bool ler(const QByteArray &dados, Pasta &p, QString *erro)
	{
		if (!z_.abrir(dados) || !z_.tem("xl/workbook.xml")) {
			*erro = "não é uma planilha do Excel (.xlsx)";
			return false;
		}
		No wb, rels;
		lerXml(z_.ler("xl/workbook.xml"), wb);
		lerXml(z_.ler("xl/_rels/workbook.xml.rels"), rels);
		QHash<QString, QString> alvo;
		if (const No *r = rels.achar("rel:Relationships"))
			for (const No *x : r->todos("rel:Relationship"))
				alvo.insert(x->a("Id"), caminho("xl/", x->a("Target")));
		lerTextos();
		lerEstilos();
		p.folhas.clear();
		const No *sheets = wb.achar("x:sheets");
		if (!sheets) {
			*erro = "a planilha não tem folhas";
			return false;
		}
		for (const No *s : sheets->todos("x:sheet")) {
			const int f = p.novaFolha(s->a("name"));
			const QString arq = alvo.value(s->a("r:id"));
			lerFolha(p, f, arq);
		}
		if (p.folhas.empty())
			p.novaFolha();
		p.fonte = fonteBase_.isEmpty() ? QString("IBM Plex Sans") : fonteBase_;
		if (tamanhoBase_ > 0)
			p.tamanho = tamanhoBase_;
		return true;
	}

private:
	static QString caminho(const QString &base, QString alvo)
	{
		if (alvo.startsWith('/'))
			return alvo.mid(1);
		return QDir::cleanPath(base + alvo);
	}

	void lerTextos()
	{
		No s;
		if (!lerXml(z_.ler("xl/sharedStrings.xml"), s))
			return;
		if (const No *sst = s.achar("x:sst"))
			for (const No *si : sst->todos("x:si")) {
				QString t;
				for (const No &f : si->filhos) {
					if (f.nome == "x:t")
						t += f.textoTodo();
					else if (f.nome == "x:r")
						if (const No *tt = f.filho("x:t"))
							t += tt->textoTodo();
				}
				textos_ << t;
			}
	}

	void lerEstilos()
	{
		No s;
		if (!lerXml(z_.ler("xl/styles.xml"), s))
			return;
		const No *raiz = s.achar("x:styleSheet");
		if (!raiz)
			return;
		QHash<int, QString> codigos;
		if (const No *nf = raiz->filho("x:numFmts"))
			for (const No *n : nf->todos("x:numFmt"))
				codigos.insert(n->a("numFmtId").toInt(), n->a("formatCode"));
		struct Fonte_ { bool b = false, i = false, u = false; QColor cor; double tam = 0; };
		std::vector<Fonte_> fontes;
		if (const No *fs = raiz->filho("x:fonts"))
			for (const No *f : fs->todos("x:font")) {
				Fonte_ x;
				auto lig = [&](const char *n) { const No *k = f->filho(n); return k && k->a("val", "1") != "0" && k->a("val") != "false" && k->a("val") != "none"; };
				x.b = lig("x:b");
				x.i = lig("x:i");
				x.u = lig("x:u");
				if (const No *c = f->filho("x:color"))
					x.cor = corArgb(c->a("rgb"));
				if (const No *z = f->filho("x:sz"))
					x.tam = z->a("val").toDouble();
				if (fontes.empty()) {
					tamanhoBase_ = x.tam;
					if (const No *n = f->filho("x:name"))
						fonteBase_ = n->a("val");
				}
				fontes.push_back(x);
			}
		std::vector<QColor> fills;
		if (const No *fs = raiz->filho("x:fills"))
			for (const No *f : fs->todos("x:fill")) {
				QColor c;
				if (const No *pf = f->filho("x:patternFill"))
					if (pf->a("patternType") == "solid")
						if (const No *fg = pf->filho("x:fgColor"))
							c = corArgb(fg->a("rgb"));
				fills.push_back(c);
			}
		std::vector<bool> bordas;
		if (const No *bs = raiz->filho("x:borders"))
			for (const No *b : bs->todos("x:border")) {
				bool tem = false;
				for (const char *l : {"x:left", "x:right", "x:top", "x:bottom"})
					if (const No *k = b->filho(l))
						if (!k->a("style").isEmpty() && k->a("style") != "none")
							tem = true;
				bordas.push_back(tem);
			}
		if (const No *xs = raiz->filho("x:cellXfs"))
			for (const No *x : xs->todos("x:xf")) {
				Formato f;
				const int nid = x->a("numFmtId").toInt();
				formatoDeCodigo(codigos.contains(nid) ? codigos.value(nid) : codigoEmbutido(nid), f);
				const int fid = x->a("fontId").toInt();
				if (fid > 0 && fid < int(fontes.size())) {
					const Fonte_ &fo = fontes[size_t(fid)];
					f.negrito = fo.b;
					f.italico = fo.i;
					f.sublinhado = fo.u;
					if (fo.cor.isValid() && fo.cor != QColor(0, 0, 0))
						f.cor = fo.cor;
					if (fo.tam > 0 && fo.tam != tamanhoBase_)
						f.tamanho = fo.tam;
				}
				const int fl = x->a("fillId").toInt();
				if (fl >= 0 && fl < int(fills.size()) && fills[size_t(fl)].isValid() && fills[size_t(fl)] != Qt::white)
					f.fundo = fills[size_t(fl)];
				const int bd = x->a("borderId").toInt();
				f.borda = bd >= 0 && bd < int(bordas.size()) && bordas[size_t(bd)];
				if (const No *al = x->filho("x:alignment")) {
					const QString h = al->a("horizontal");
					f.alin = h == "left" ? 1 : h == "center" || h == "centerContinuous" ? 2 : h == "right" ? 3 : 0;
				}
				xfs_.push_back(f);
			}
	}

	void lerFolha(Pasta &p, int fi, const QString &arq)
	{
		No d;
		if (!lerXml(z_.ler(arq), d))
			return;
		const No *ws = d.achar("x:worksheet");
		if (!ws)
			return;
		Folha &fo = p.folha(fi);
		if (const No *sv = ws->achar("x:pane"))
			if (sv->a("state") == "frozen" || sv->a("state") == "frozenSplit") {
				fo.congelarColunas = int(sv->a("xSplit").toDouble());
				fo.congelarLinhas = int(sv->a("ySplit").toDouble());
			}
		// A largura padrão das colunas: sem dizer, a do Excel (8,43
		// caracteres, 64 px), que é menor que a daqui.
		{
			const No *fp = ws->filho("x:sheetFormatPr");
			const double w = fp && !fp->a("defaultColWidth").isEmpty() ? fp->a("defaultColWidth").toDouble()
			                 : fp && !fp->a("baseColWidth").isEmpty() ? fp->a("baseColWidth").toDouble() + 0.43 : 8.43;
			const int px = int(std::round(w * 7 + 5));
			if (w > 0 && std::abs(px - LARGURA_PADRAO) > 2)
				fo.larguraPadrao = px;
		}
		if (const No *cols = ws->filho("x:cols"))
			for (const No *c : cols->todos("x:col")) {
				if (c->a("customWidth") != "1" && c->a("width").isEmpty())
					continue;
				const int px = int(std::round(c->a("width").toDouble() * 7 + 5));
				const int a = c->a("min").toInt() - 1, b = std::min(c->a("max").toInt() - 1, a + 200);
				if (c->a("width").toDouble() > 0)
					for (int k = a; k <= b; k++) {
						if (std::abs(px - fo.larguraPadrao) > 2) fo.largura[k] = px;
						else fo.largura.erase(k);
					}
			}
		struct Compartilhada { PExpr f; int r, c; };
		QHash<QString, Compartilhada> compartilhadas;
		if (const No *sd = ws->filho("x:sheetData"))
			for (const No *row : sd->todos("x:row")) {
				const int r = row->a("r").toInt() - 1;
				if (row->a("customHeight") == "1" && !row->a("ht").isEmpty())
					fo.altura[r] = int(std::round(row->a("ht").toDouble() / 0.75));
				int proxCol = 0;
				for (const No *c : row->todos("x:c")) {
					int rr = r, cc = proxCol;
					const QString ref = c->a("r");
					if (!ref.isEmpty()) {
						Analisador a(ref, Dialeto::Canonico);
						PExpr e = analisar(ref, Dialeto::Canonico);
						if (e && e->k == Expr::Ref) { rr = e->r1; cc = e->c1; }
					}
					proxCol = cc + 1;
					Celula k;
					const int s = c->a("s").toInt();
					if (s > 0 && s < int(xfs_.size()))
						k.fmt = xfs_[size_t(s)];
					const QString t = c->a("t");
					const No *v = c->filho("x:v");
					const QString vt = v ? v->textoTodo() : QString();
					if (const No *f = c->filho("x:f")) {
						const QString texto = f->textoTodo();
						if (f->a("t") == "shared") {
							const QString si = f->a("si");
							if (!texto.isEmpty()) {
								PExpr e = analisar(texto, Dialeto::Canonico);
								compartilhadas.insert(si, {e, rr, cc});
								k.f = e;
							} else if (compartilhadas.contains(si)) {
								const Compartilhada &m = compartilhadas[si];
								k.f = deslocada(m.f, rr - m.r, cc - m.c);
							}
						} else if (!texto.isEmpty()) {
							k.f = analisar(texto, Dialeto::Canonico);
						}
					}
					if (t == "s")
						k.v = Valor::txt(textos_.value(vt.toInt()));
					else if (t == "inlineStr") {
						const No *is = c->filho("x:is");
						k.v = Valor::txt(is ? is->textoTodo() : QString());
					} else if (t == "str")
						k.v = Valor::txt(vt);
					else if (t == "b")
						k.v = Valor::log(vt == "1" || vt.compare("true", Qt::CaseInsensitive) == 0);
					else if (t == "e")
						k.v = Valor::erro(vt);
					else if (t == "d") {
						const QDate dd = QDate::fromString(vt.left(10), Qt::ISODate);
						k.v = dd.isValid() ? Valor::num(serialDe(dd)) : Valor::txt(vt);
						if (k.fmt.tipo == Formato::Geral) k.fmt.tipo = Formato::Data;
					} else if (!vt.isEmpty())
						k.v = Valor::num(vt.toDouble());
					if (!k.vazia() || !k.fmt.padrao())
						fo.cel.insert(chave(rr, cc), k);
				}
			}
		fo.recontar();
		// Os gráficos.
		if (const No *dr = ws->filho("x:drawing")) {
			No rs;
			const QString base = arq.left(arq.lastIndexOf('/') + 1);
			lerXml(z_.ler(base + "_rels/" + arq.mid(base.size()) + ".rels"), rs);
			QString arqDesenho;
			if (const No *r = rs.achar("rel:Relationships"))
				for (const No *x : r->todos("rel:Relationship"))
					if (x->a("Id") == dr->a("r:id"))
						arqDesenho = caminho(base, x->a("Target"));
			if (!arqDesenho.isEmpty())
				lerDesenho(p, fi, arqDesenho);
		}
	}

	void lerDesenho(Pasta &p, int fi, const QString &arq)
	{
		No d, rs;
		if (!lerXml(z_.ler(arq), d))
			return;
		const QString base = arq.left(arq.lastIndexOf('/') + 1);
		lerXml(z_.ler(base + "_rels/" + arq.mid(base.size()) + ".rels"), rs);
		QHash<QString, QString> alvo;
		if (const No *r = rs.achar("rel:Relationships"))
			for (const No *x : r->todos("rel:Relationship"))
				alvo.insert(x->a("Id"), caminho(base, x->a("Target")));
		const No *raiz = d.achar("xdr:wsDr");
		if (!raiz)
			return;
		for (const No &an : raiz->filhos) {
			if (an.nome != "xdr:twoCellAnchor" && an.nome != "xdr:oneCellAnchor" && an.nome != "xdr:absoluteAnchor")
				continue;
			const No *ch = an.achar("c:chart");
			if (!ch)
				continue;
			Grafico g;
			auto ponto = [&](const No *x, double &px, double &py) {
				if (!x) return false;
				const No *col = x->filho("xdr:col"), *co = x->filho("xdr:colOff"), *row = x->filho("xdr:row"), *ro = x->filho("xdr:rowOff");
				if (!col || !row) return false;
				px = p.xDe(fi, col->textoTodo().toInt()) + (co ? co->textoTodo().toDouble() / 9525 : 0);
				py = p.yDe(fi, row->textoTodo().toInt()) + (ro ? ro->textoTodo().toDouble() / 9525 : 0);
				return true;
			};
			double x2, y2;
			if (ponto(an.filho("xdr:from"), g.x, g.y)) {
				if (ponto(an.filho("xdr:to"), x2, y2)) {
					g.w = std::max(120.0, x2 - g.x);
					g.h = std::max(90.0, y2 - g.y);
				} else if (const No *e = an.filho("xdr:ext")) {
					g.w = e->a("cx").toDouble() / 9525;
					g.h = e->a("cy").toDouble() / 9525;
				}
			}
			if (lerGrafico(p, fi, alvo.value(ch->a("r:id")), g))
				p.folha(fi).graficos.push_back(g);
		}
	}

	bool lerGrafico(Pasta &p, int fi, const QString &arq, Grafico &g)
	{
		No d;
		if (!lerXml(z_.ler(arq), d))
			return false;
		const No *pa = d.achar("c:plotArea");
		if (!pa)
			return false;
		const No *tipo = nullptr;
		for (const No &x : pa->filhos)
			if (x.nome.startsWith("c:") && x.nome.endsWith("Chart")) {
				tipo = &x;
				break;
			}
		if (!tipo)
			return false;
		const QString t = tipo->nome;
		if (t == "c:barChart" || t == "c:bar3DChart") {
			const No *dir = tipo->filho("c:barDir");
			g.tipo = dir && dir->a("val") == "bar" ? Grafico::Barras : Grafico::Colunas;
		} else if (t == "c:lineChart" || t == "c:line3DChart" || t == "c:stockChart") g.tipo = Grafico::Linhas;
		else if (t == "c:areaChart" || t == "c:area3DChart") g.tipo = Grafico::Area;
		else if (t == "c:pieChart" || t == "c:pie3DChart" || t == "c:doughnutChart" || t == "c:ofPieChart") g.tipo = Grafico::Pizza;
		else if (t == "c:scatterChart" || t == "c:bubbleChart") g.tipo = Grafico::Dispersao;
		if (const No *ti = d.achar("c:title")) {
			QString s;
			std::function<void(const No &)> junta = [&](const No &n) {
				for (const No &f : n.filhos) {
					if (f.nome == "a:t") s += f.textoTodo();
					else junta(f);
				}
			};
			junta(*ti);
			g.titulo = s;
		}
		// O retângulo que cobre todas as referências da mesma folha, e a
		// coluna de cada série.
		bool algum = false;
		int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
		const QString nome = p.folha(fi).nome;
		std::vector<int> cols;
		int colCats = -1;
		auto junta = [&](const No *x, int *col) {
			if (!x)
				return;
			QString fo;
			int a, b, c, dd;
			if (lerRefGrafico(x->textoTodo(), fo, a, b, c, dd) && (fo.isEmpty() || fo == nome)) {
				if (!algum) { r1 = a; c1 = b; r2 = c; c2 = dd; algum = true; }
				r1 = std::min(r1, a); c1 = std::min(c1, b); r2 = std::max(r2, c); c2 = std::max(c2, dd);
				if (col)
					*col = b;
			}
		};
		for (const No *ser : tipo->todos("c:ser")) {
			int cv = -1, cx = -1;
			if (const No *tx = ser->filho("c:tx")) junta(tx->achar("c:f"), nullptr);
			for (const char *k : {"c:cat", "c:xVal"})
				if (const No *x = ser->filho(k)) junta(x->achar("c:f"), &cx);
			for (const char *k : {"c:val", "c:yVal"})
				if (const No *x = ser->filho(k)) junta(x->achar("c:f"), &cv);
			if (g.tipo == Grafico::Dispersao && cx >= 0 && cols.empty())
				cols.push_back(cx);
			else if (cx >= 0)
				colCats = cx;
			if (cv >= 0)
				cols.push_back(cv);
		}
		if (!algum)
			return false;
		g.r1 = r1; g.c1 = colCats >= 0 ? colCats : c1; g.r2 = r2; g.c2 = c2;
		if (colCats >= 0)
			c1 = colCats;
		g.c1 = c1;
		// Só guarda as colunas se não forem "todas as do lado".
		bool contiguas = !cols.empty();
		for (size_t i = 0; i < cols.size(); i++)
			if (cols[i] != (colCats >= 0 ? c1 + 1 : c1) + int(i))
				contiguas = false;
		if (contiguas && cols.back() == c2)
			cols.clear();
		g.series = cols;
		return true;
	}

	ZipLeitor z_;
	QStringList textos_;
	std::vector<Formato> xfs_;
	QString fonteBase_;
	double tamanhoBase_ = 0;
};

// --- ODS: gravar -----------------------------------------------------------

class EscritorOds
{
public:
	explicit EscritorOds(Pasta &p) : p_(p) {}

	QByteArray gravar()
	{
		QString corpo;
		ZipEscritor z;
		z.add("mimetype", "application/vnd.oasis.opendocument.spreadsheet", false);
		QString manifesto = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		                    "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
		                    "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.spreadsheet\"/>"
		                    "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
		                    "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
		                    "<manifest:file-entry manifest:full-path=\"settings.xml\" manifest:media-type=\"text/xml\"/>"
		                    "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>";
		int nObj = 0;
		for (int f = 0; f < p_.quantas(); f++) {
			const Folha &fo = p_.folha(f);
			corpo += "<table:table table:name=\"" + esc(fo.nome) + "\">";
			if (!fo.graficos.empty()) {
				corpo += "<table:shapes>";
				for (const Grafico &g : fo.graficos) {
					nObj++;
					const QString obj = "Object " + QString::number(nObj);
					z.add(obj + "/content.xml", grafico(f, g).toUtf8());
					z.add(obj + "/styles.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" office:version=\"1.3\"/>");
					manifesto += "<manifest:file-entry manifest:full-path=\"" + obj + "/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.chart\"/>"
					             "<manifest:file-entry manifest:full-path=\"" + obj + "/content.xml\" manifest:media-type=\"text/xml\"/>"
					             "<manifest:file-entry manifest:full-path=\"" + obj + "/styles.xml\" manifest:media-type=\"text/xml\"/>";
					corpo += "<draw:frame draw:z-index=\"" + QString::number(nObj) + "\" draw:name=\"Gráfico " + QString::number(nObj) +
					         "\" svg:width=\"" + cm(g.w) + "\" svg:height=\"" + cm(g.h) + "\" svg:x=\"" + cm(g.x) + "\" svg:y=\"" + cm(g.y) +
					         "\"><draw:object draw:notify-on-update-of-ranges=\"" + esc(faixaOdf(fo.nome, g.r1, g.c1, g.r2, g.c2)) +
					         "\" xlink:href=\"./" + obj + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>";
				}
				corpo += "</table:shapes>";
			}
			corpo += folha(f);
			corpo += "</table:table>";
		}
		manifesto += "</manifest:manifest>";
		const QString c = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		                  "<office:document-content " + NS + " office:version=\"1.3\"><office:automatic-styles>" + autos_ +
		                  "</office:automatic-styles><office:body><office:spreadsheet>" + corpo +
		                  "</office:spreadsheet></office:body></office:document-content>";
		z.add("content.xml", c.toUtf8());
		z.add("styles.xml",
		      ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles " + NS + " office:version=\"1.3\"><office:styles>"
		       "<style:default-style style:family=\"table-cell\"><style:text-properties fo:font-family=\"'" + esc(p_.fonte) +
		       "'\" style:font-name=\"" + esc(p_.fonte) + "\" fo:font-size=\"" + QString::number(p_.tamanho) + "pt\" fo:language=\"pt\" fo:country=\"BR\"/></style:default-style>"
		       "<style:style style:name=\"Default\" style:family=\"table-cell\"/></office:styles></office:document-styles>").toUtf8());
		z.add("meta.xml",
		      ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta " + NS + " office:version=\"1.3\"><office:meta>"
		       "<meta:generator>LumeBSD Números</meta:generator></office:meta></office:document-meta>").toUtf8());
		z.add("settings.xml", configuracoes().toUtf8());
		z.add("META-INF/manifest.xml", manifesto.toUtf8());
		return z.fechar();
	}

	static QString cm(double px) { return QString::number(px * 2.54 / 96, 'f', 3) + "cm"; }

private:
	inline static const QString NS =
		"xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
		"xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
		"xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
		"xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
		"xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
		"xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
		"xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
		"xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" "
		"xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" "
		"xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
		"xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\" "
		"xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" "
		"xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\"";

	static QString faixaOdf(const QString &folha, int r1, int c1, int r2, int c2)
	{
		const QString f = folhaTexto(folha, true).remove(0, folhaTexto(folha, true).startsWith('$') ? 1 : 0);
		return f + "." + nomeCelula(r1, c1) + ":" + f + "." + nomeCelula(r2, c2);
	}

	QString estiloDeDado(const Formato &f)
	{
		const QString chaveF = QString("%1/%2/%3/%4").arg(int(f.tipo)).arg(f.casas).arg(f.milhar).arg(f.codigoData);
		if (dados_.contains(chaveF))
			return dados_.value(chaveF);
		const QString nome = "N" + QString::number(dados_.size() + 1);
		dados_.insert(chaveF, nome);
		const QString casas = "number:decimal-places=\"" + QString::number(f.casas) + "\" number:min-decimal-places=\"" + QString::number(f.casas) + "\" number:min-integer-digits=\"1\"";
		switch (f.tipo) {
		case Formato::Numero:
			autos_ += "<number:number-style style:name=\"" + nome + "\"><number:number " + casas + (f.milhar ? " number:grouping=\"true\"" : "") + "/></number:number-style>";
			break;
		case Formato::Moeda:
			autos_ += "<number:currency-style style:name=\"" + nome + "\" number:language=\"pt\" number:country=\"BR\"><number:currency-symbol number:language=\"pt\" number:country=\"BR\">R$</number:currency-symbol><number:text> </number:text><number:number " + casas + " number:grouping=\"true\"/></number:currency-style>";
			break;
		case Formato::Porcento:
			autos_ += "<number:percentage-style style:name=\"" + nome + "\"><number:number " + casas + "/><number:text>%</number:text></number:percentage-style>";
			break;
		case Formato::Data: {
			// O formato da data pedaço por pedaço (o dd/mm/aaaa de sempre ou o código do Excel).
			QString x;
			for (const PedacoData &p : pedacosData(f.codigoData.isEmpty() ? QString("dd/mm/yyyy") : f.codigoData)) {
				const QString longo = p.n >= 2 ? " number:style=\"long\"" : "";
				switch (p.t) {
				case 'd': x += p.n >= 3 ? "<number:day-of-week" + QString(p.n >= 4 ? " number:style=\"long\"" : "") + "/>" : "<number:day" + longo + "/>"; break;
				case 'm': x += p.n >= 3 ? "<number:month number:textual=\"true\"" + QString(p.n >= 4 ? " number:style=\"long\"" : "") + "/>" : "<number:month" + longo + "/>"; break;
				case 'y': x += "<number:year" + QString(p.n >= 3 ? " number:style=\"long\"" : "") + "/>"; break;
				case 'h': x += "<number:hours" + longo + "/>"; break;
				case 'M': x += "<number:minutes" + longo + "/>"; break;
				case 's': x += "<number:seconds" + longo + "/>"; break;
				case 'a': x += "<number:am-pm/>"; break;
				default: x += "<number:text>" + esc(p.txt) + "</number:text>";
				}
			}
			autos_ += "<number:date-style style:name=\"" + nome + "\">" + x + "</number:date-style>";
			break;
		}
		case Formato::Texto:
			autos_ += "<number:text-style style:name=\"" + nome + "\"><number:text-content/></number:text-style>";
			break;
		default:
			return QString();
		}
		return nome;
	}

	QString estiloCelula(const Formato &f)
	{
		for (size_t i = 0; i < celulas_.size(); i++)
			if (celulas_[i] == f)
				return "ce" + QString::number(i + 1);
		celulas_.push_back(f);
		const QString nome = "ce" + QString::number(celulas_.size());
		const QString dado = estiloDeDado(f);
		QString s = "<style:style style:name=\"" + nome + "\" style:family=\"table-cell\" style:parent-style-name=\"Default\"" +
		            (dado.isEmpty() ? QString() : " style:data-style-name=\"" + dado + "\"") + ">";
		QString cp;
		if (f.fundo.isValid()) cp += " fo:background-color=\"" + f.fundo.name() + "\"";
		if (f.borda) cp += " fo:border=\"0.06pt solid #1c1b19\"";
		if (!cp.isEmpty()) s += "<style:table-cell-properties" + cp + "/>";
		if (f.alin) s += "<style:paragraph-properties fo:text-align=\"" + QString(f.alin == 1 ? "start" : f.alin == 2 ? "center" : "end") + "\"/>";
		QString tp;
		if (f.negrito) tp += " fo:font-weight=\"bold\"";
		if (f.italico) tp += " fo:font-style=\"italic\"";
		if (f.sublinhado) tp += " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"";
		if (f.cor.isValid()) tp += " fo:color=\"" + f.cor.name() + "\"";
		if (f.tamanho > 0) tp += " fo:font-size=\"" + QString::number(f.tamanho) + "pt\"";
		if (!tp.isEmpty()) s += "<style:text-properties" + tp + "/>";
		autos_ += s + "</style:style>";
		return nome;
	}

	QString estiloColuna(int px)
	{
		const QString k = "co" + QString::number(px);
		if (!feitos_.contains(k)) {
			feitos_.insert(k);
			autos_ += "<style:style style:name=\"" + k + "\" style:family=\"table-column\"><style:table-column-properties style:column-width=\"" + cm(px) + "\"/></style:style>";
		}
		return k;
	}
	QString estiloLinha(int px)
	{
		const QString k = "ro" + QString::number(px);
		if (!feitos_.contains(k)) {
			feitos_.insert(k);
			autos_ += "<style:style style:name=\"" + k + "\" style:family=\"table-row\"><style:table-row-properties style:row-height=\"" + cm(px) +
			          "\" style:use-optimal-row-height=\"false\"/></style:style>";
		}
		return k;
	}

	QString folha(int f)
	{
		const Folha &fo = p_.folha(f);
		QString s;
		const int ultC = std::max({fo.ultColuna, fo.largura.empty() ? -1 : fo.largura.rbegin()->first, 0});
		for (int c = 0; c <= ultC;) {
			const int w = fo.larguraDe(c);
			int n = 1;
			while (c + n <= ultC && fo.larguraDe(c + n) == w) n++;
			s += "<table:table-column table:style-name=\"" + estiloColuna(w) + "\"" + (n > 1 ? " table:number-columns-repeated=\"" + QString::number(n) + "\"" : QString()) + " table:default-cell-style-name=\"Default\"/>";
			c += n;
		}
		// O resto das colunas, com a largura padrão (como o LibreOffice faz).
		if (ultC < 1000)
			s += "<table:table-column table:style-name=\"" + estiloColuna(fo.larguraPadrao) + "\" table:number-columns-repeated=\"" +
		     QString::number(1024 - ultC - 1) + "\" table:default-cell-style-name=\"Default\"/>";
		const int ultR = std::max(fo.ultLinha, fo.altura.empty() ? -1 : fo.altura.rbegin()->first);
		int vazias = 0;
		auto soltaVazias = [&] {
			if (vazias > 0)
				s += "<table:table-row table:style-name=\"" + estiloLinha(ALTURA_PADRAO) + "\"" +
				     (vazias > 1 ? " table:number-rows-repeated=\"" + QString::number(vazias) + "\"" : QString()) + "><table:table-cell/></table:table-row>";
			vazias = 0;
		};
		for (int r = 0; r <= ultR; r++) {
			bool algo = fo.altura.count(r) > 0;
			for (int c = 0; c <= fo.ultColuna && !algo; c++)
				if (fo.em(r, c)) algo = true;
			if (!algo) { vazias++; continue; }
			soltaVazias();
			s += "<table:table-row table:style-name=\"" + estiloLinha(fo.alturaDe(r)) + "\">";
			int brancas = 0;
			for (int c = 0; c <= fo.ultColuna; c++) {
				const Celula *k = fo.em(r, c);
				if (!k) { brancas++; continue; }
				if (brancas) {
					s += "<table:table-cell" + (brancas > 1 ? " table:number-columns-repeated=\"" + QString::number(brancas) + "\"" : QString()) + "/>";
					brancas = 0;
				}
				s += celula(*k);
			}
			s += "</table:table-row>";
		}
		soltaVazias();
		return s;
	}

	QString celula(const Celula &k)
	{
		QString s = "<table:table-cell";
		if (!k.fmt.padrao())
			s += " table:style-name=\"" + estiloCelula(k.fmt) + "\"";
		if (k.f)
			s += " table:formula=\"" + esc("of:=" + textoDe(k.f, Dialeto::Odf)) + "\"";
		const Valor &v = k.v;
		const QString mostrado = mostrar(v, k.fmt);
		switch (v.t) {
		case Valor::Numero:
			if (k.fmt.tipo == Formato::Porcento)
				s += " office:value-type=\"percentage\" office:value=\"" + numeroXml(v.n) + "\"";
			else if (k.fmt.tipo == Formato::Moeda)
				s += " office:value-type=\"currency\" office:currency=\"BRL\" office:value=\"" + numeroXml(v.n) + "\"";
			else if (k.fmt.tipo == Formato::Data)
				s += " office:value-type=\"date\" office:date-value=\"" + dataDe(v.n).toString(Qt::ISODate) + "\"";
			else
				s += " office:value-type=\"float\" office:value=\"" + numeroXml(v.n) + "\"";
			break;
		case Valor::Logico:
			s += " office:value-type=\"boolean\" office:boolean-value=\"" + QString(v.n ? "true" : "false") + "\"";
			break;
		case Valor::Texto:
			s += " office:value-type=\"string\"";
			break;
		case Valor::Erro:
			s += " office:value-type=\"string\" office:string-value=\"\"";
			break;
		default:
			break;
		}
		if (v.t == Valor::Vazio)
			return s + "/>";
		QString linhas;
		for (const QString &l : mostrado.split('\n'))
			linhas += "<text:p>" + esc(l) + "</text:p>";
		return s + ">" + linhas + "</table:table-cell>";
	}

	QString grafico(int f, const Grafico &g)
	{
		const Folha &fo = p_.folha(f);
		static const char *classes[] = {"chart:bar", "chart:bar", "chart:line", "chart:circle", "chart:scatter", "chart:area"};
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content " + NS + " office:version=\"1.3\"><office:automatic-styles>"
		            "<style:style style:name=\"pa\" style:family=\"chart\"><style:chart-properties chart:vertical=\"" +
		            QString(g.tipo == Grafico::Barras ? "true" : "false") + "\"/></style:style>";
		QStringList cats;
		std::vector<Serie> series;
		p_.dadosDoGrafico(f, g, cats, series);
		for (size_t i = 0; i < series.size(); i++)
			s += "<style:style style:name=\"s" + QString::number(i) + "\" style:family=\"chart\"><style:graphic-properties draw:fill-color=\"" +
			     corDaSerie(int(i)).name() + "\" svg:stroke-color=\"" + corDaSerie(int(i)).name() + "\" svg:stroke-width=\"0.08cm\"/></style:style>";
		s += "</office:automatic-styles><office:body><office:chart><chart:chart chart:class=\"" + QString(classes[g.tipo]) +
		     "\" svg:width=\"" + cm(g.w) + "\" svg:height=\"" + cm(g.h) + "\">";
		if (!g.titulo.isEmpty())
			s += "<chart:title><text:p>" + esc(g.titulo) + "</text:p></chart:title>";
		s += "<chart:legend chart:legend-position=\"bottom\"/>";
		const Arranjo arr = p_.arranjo(f, g);
		const int nSeries = int(arr.cols.size());
		const int r0 = arr.r0;
		const bool cab = arr.cab, temCats = arr.cats;
		const int c0 = arr.cols.empty() ? g.c1 : arr.cols[0];
		const QString faixa = faixaOdf(fo.nome, g.r1, g.c1, g.r2, g.c2);
		s += "<chart:plot-area chart:style-name=\"pa\" table:cell-range-address=\"" + esc(faixa) + "\" chart:data-source-has-labels=\"" +
		     QString(cab && temCats ? "both" : cab ? "row" : temCats ? "column" : "none") + "\">";
		// A pizza também tem os eixos (sem desenhar): é onde ficam as categorias.
		s += "<chart:axis chart:dimension=\"x\" chart:name=\"primary-x\">";
		if (temCats && g.tipo != Grafico::Dispersao)
			s += "<chart:categories table:cell-range-address=\"" + esc(faixaOdf(fo.nome, r0, g.c1, g.r2, g.c1)) + "\"/>";
		s += "</chart:axis><chart:axis chart:dimension=\"y\" chart:name=\"primary-y\">";
		if (g.tipo != Grafico::Pizza)
			s += "<chart:grid chart:class=\"major\"/>";
		s += "</chart:axis>";
		const int ini = g.tipo == Grafico::Dispersao ? 1 : 0;
		for (int k = ini; k < nSeries; k++) {
			const int c = arr.cols[size_t(k)];
			s += "<chart:series chart:style-name=\"s" + QString::number(k - ini) + "\" chart:values-cell-range-address=\"" + esc(faixaOdf(fo.nome, r0, c, g.r2, c)) + "\"";
			if (cab)
				s += " chart:label-cell-address=\"" + esc(faixaOdf(fo.nome, g.r1, c, g.r1, c).section(':', 0, 0)) + "\"";
			s += " chart:class=\"" + QString(classes[g.tipo]) + "\">";
			if (g.tipo == Grafico::Dispersao)
				s += "<chart:domain table:cell-range-address=\"" + esc(faixaOdf(fo.nome, r0, c0, g.r2, c0)) + "\"/>";
			s += "</chart:series>";
			if (g.tipo == Grafico::Pizza)
				break;
		}
		s += "</chart:plot-area></chart:chart></office:chart></office:body></office:document-content>";
		return s;
	}

	QString configuracoes() const
	{
		QString t;
		for (const Folha &fo : p_.folhas) {
			t += "<config:config-item-map-entry config:name=\"" + esc(fo.nome) + "\">";
			auto item = [&](const char *n, const char *tipo, const QString &v) {
				t += QString("<config:config-item config:name=\"%1\" config:type=\"%2\">%3</config:config-item>").arg(n, tipo, v);
			};
			if (fo.congelarColunas > 0) {
				item("HorizontalSplitMode", "short", "2");
				item("HorizontalSplitPosition", "int", QString::number(fo.congelarColunas));
				item("PositionRight", "int", QString::number(fo.congelarColunas));
			}
			if (fo.congelarLinhas > 0) {
				item("VerticalSplitMode", "short", "2");
				item("VerticalSplitPosition", "int", QString::number(fo.congelarLinhas));
				item("PositionBottom", "int", QString::number(fo.congelarLinhas));
			}
			t += "</config:config-item-map-entry>";
		}
		return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-settings " + NS + " office:version=\"1.3\"><office:settings>"
		       "<config:config-item-set config:name=\"ooo:view-settings\"><config:config-item-map-indexed config:name=\"Views\">"
		       "<config:config-item-map-entry><config:config-item config:name=\"ViewId\" config:type=\"string\">view1</config:config-item>"
		       "<config:config-item-map-named config:name=\"Tables\">" + t + "</config:config-item-map-named>"
		       "</config:config-item-map-entry></config:config-item-map-indexed></config:config-item-set></office:settings></office:document-settings>";
	}

	Pasta &p_;
	QString autos_;
	QHash<QString, QString> dados_;
	std::vector<Formato> celulas_;
	QSet<QString> feitos_;
};

// --- ODS: ler --------------------------------------------------------------

inline double pxDe(const QString &s0)
{
	const QString s = s0.trimmed();
	int i = 0;
	while (i < s.size() && (s[i].isDigit() || s[i] == '.' || s[i] == '-'))
		i++;
	const double v = s.left(i).toDouble();
	const QString u = s.mid(i);
	if (u == "in") return v * 96;
	if (u == "mm") return v * 96 / 25.4;
	if (u == "pt") return v * 96 / 72;
	if (u == "px") return v;
	return v * 96 / 2.54; // cm
}

class LeitorOds
{
public:
	bool ler(const QByteArray &dados, Pasta &p, QString *erro)
	{
		if (!z_.abrir(dados) || !z_.tem("content.xml")) {
			*erro = "não é uma planilha OpenDocument (.ods)";
			return false;
		}
		No c, st, cfg;
		if (!lerXml(z_.ler("content.xml"), c)) {
			*erro = "o content.xml está danificado";
			return false;
		}
		lerXml(z_.ler("styles.xml"), st);
		for (const No *raiz : {&st, &c})
			for (const char *sec : {"office:styles", "office:automatic-styles"})
				if (const No *s = raiz->achar(sec))
					coletar(*s);
		if (const No *ds = st.achar("style:default-style"))
			if (const No *tp = ds->filho("style:text-properties")) {
				QString f = tp->a("fo:font-family", tp->a("style:font-name")).remove('\'');
				if (!f.isEmpty()) p.fonte = f;
				if (tp->a("fo:font-size").endsWith("pt")) p.tamanho = tp->a("fo:font-size").chopped(2).toDouble();
			}
		const No *planilha = c.achar("office:spreadsheet");
		if (!planilha) {
			*erro = "o documento não é uma planilha";
			return false;
		}
		p.folhas.clear();
		for (const No *t : planilha->todos("table:table"))
			lerTabela(p, *t);
		if (p.folhas.empty())
			p.novaFolha();
		// Os painéis congelados (settings.xml).
		if (lerXml(z_.ler("settings.xml"), cfg))
			if (const No *tabs = cfg.achar("config:config-item-map-named"))
				for (const No &e : tabs->filhos) {
					const int fi = p.folhaPorNome(e.a("config:name"));
					if (fi < 0) continue;
					int modoH = 0, modoV = 0, posH = 0, posV = 0;
					for (const No &i : e.filhos) {
						const QString n = i.a("config:name");
						const int v = i.textoTodo().toInt();
						if (n == "HorizontalSplitMode") modoH = v;
						else if (n == "VerticalSplitMode") modoV = v;
						else if (n == "HorizontalSplitPosition") posH = v;
						else if (n == "VerticalSplitPosition") posV = v;
					}
					if (modoH == 2) p.folha(fi).congelarColunas = posH;
					if (modoV == 2) p.folha(fi).congelarLinhas = posV;
				}
		return true;
	}

private:
	struct EstiloCel {
		QString pai, dado;
		Formato f;
		bool temF = false;
	};

	void coletar(const No &sec)
	{
		for (const No &s : sec.filhos) {
			const QString nome = s.a("style:name");
			if (s.nome == "number:number-style" || s.nome == "number:currency-style" || s.nome == "number:percentage-style" ||
			    s.nome == "number:date-style" || s.nome == "number:text-style" || s.nome == "number:time-style") {
				Formato f;
				if (s.nome == "number:currency-style") f.tipo = Formato::Moeda;
				else if (s.nome == "number:percentage-style") f.tipo = Formato::Porcento;
				else if (s.nome == "number:date-style") {
					f.tipo = Formato::Data;
					// O código do Excel a partir dos pedaços.
					QString c;
					for (const No &p : s.filhos) {
						const bool longo = p.a("number:style") == "long";
						if (p.nome == "number:day") c += longo ? "dd" : "d";
						else if (p.nome == "number:day-of-week") c += longo ? "dddd" : "ddd";
						else if (p.nome == "number:month") c += p.a("number:textual") == "true" ? (longo ? "mmmm" : "mmm") : (longo ? "mm" : "m");
						else if (p.nome == "number:year") c += longo ? "yyyy" : "yy";
						else if (p.nome == "number:hours") c += longo ? "hh" : "h";
						else if (p.nome == "number:minutes") c += longo ? "mm" : "m";
						else if (p.nome == "number:seconds") c += longo ? "ss" : "s";
						else if (p.nome == "number:am-pm") c += "AM/PM";
						else if (p.nome == "number:text") {
							const QString t = p.textoTodo();
							c += t.size() == 1 && QString("/.-: ,").contains(t) ? t : "\"" + t + "\"";
						}
					}
					if (c != "dd/mm/yyyy")
						f.codigoData = c;
				}
				else if (s.nome == "number:text-style") f.tipo = Formato::Texto;
				else f.tipo = Formato::Numero;
				if (const No *n = s.filho("number:number")) {
					f.casas = n->a("number:decimal-places", "0").toInt();
					f.milhar = n->a("number:grouping") == "true";
				} else if (f.tipo == Formato::Numero) {
					f.tipo = Formato::Geral;
				}
				dados_[nome] = f;
				continue;
			}
			if (s.nome != "style:style")
				continue;
			const QString fam = s.a("style:family");
			if (fam == "table-column") {
				if (const No *p = s.filho("style:table-column-properties"))
					colunas_[nome] = int(std::round(pxDe(p->a("style:column-width"))));
			} else if (fam == "table-row") {
				if (const No *p = s.filho("style:table-row-properties"))
					if (p->a("style:use-optimal-row-height") != "true")
						linhas_[nome] = int(std::round(pxDe(p->a("style:row-height"))));
			} else if (fam == "table-cell") {
				EstiloCel e;
				e.pai = s.a("style:parent-style-name");
				e.dado = s.a("style:data-style-name");
				if (const No *cp = s.filho("style:table-cell-properties")) {
					const QColor bg(cp->a("fo:background-color"));
					if (bg.isValid() && cp->a("fo:background-color") != "transparent" && bg != Qt::white) { e.f.fundo = bg; e.temF = true; }
					for (const char *b : {"fo:border", "fo:border-left", "fo:border-top"})
						if (!cp->a(b).isEmpty() && cp->a(b) != "none") { e.f.borda = true; e.temF = true; }
				}
				if (const No *pp = s.filho("style:paragraph-properties")) {
					const QString a = pp->a("fo:text-align");
					e.f.alin = a == "start" || a == "left" ? 1 : a == "center" ? 2 : a == "end" || a == "right" ? 3 : 0;
					if (e.f.alin) e.temF = true;
				}
				if (const No *tp = s.filho("style:text-properties")) {
					if (tp->a("fo:font-weight") == "bold" || tp->a("fo:font-weight").toInt() >= 600) e.f.negrito = true;
					if (tp->a("fo:font-style") == "italic") e.f.italico = true;
					if (!tp->a("style:text-underline-style").isEmpty() && tp->a("style:text-underline-style") != "none") e.f.sublinhado = true;
					const QColor cor(tp->a("fo:color"));
					if (cor.isValid() && cor != QColor(0, 0, 0)) e.f.cor = cor;
					if (tp->a("fo:font-size").endsWith("pt")) e.f.tamanho = tp->a("fo:font-size").chopped(2).toDouble();
					e.temF = true;
				}
				celulas_[nome] = e;
			}
		}
	}

	Formato formatoDe(const QString &nome, int prof = 0)
	{
		Formato f;
		auto it = celulas_.find(nome);
		if (it == celulas_.end() || prof > 10)
			return f;
		if (!it->second.pai.isEmpty() && it->second.pai != "Default")
			f = formatoDe(it->second.pai, prof + 1);
		const Formato &e = it->second.f;
		if (e.negrito) f.negrito = true;
		if (e.italico) f.italico = true;
		if (e.sublinhado) f.sublinhado = true;
		if (e.cor.isValid()) f.cor = e.cor;
		if (e.fundo.isValid()) f.fundo = e.fundo;
		if (e.borda) f.borda = true;
		if (e.alin) f.alin = e.alin;
		if (e.tamanho > 0) f.tamanho = e.tamanho;
		if (!it->second.dado.isEmpty() && dados_.count(it->second.dado)) {
			const Formato &d = dados_[it->second.dado];
			f.tipo = d.tipo;
			f.casas = d.casas;
			f.milhar = d.milhar;
			f.codigoData = d.codigoData;
		}
		return f;
	}

	void lerTabela(Pasta &p, const No &t)
	{
		const int fi = p.novaFolha(t.a("table:name"));
		Folha &fo = p.folha(fi);
		// As colunas (dentro de grupos e cabeçalhos também).
		int col = 0, larguraResto = 0;
		std::vector<std::pair<int, int>> larguras;
		std::vector<QString> estiloPadraoCol;
		std::function<void(const No &)> colunas = [&](const No &n) {
			for (const No &x : n.filhos) {
				if (x.nome == "table:table-column") {
					const int rep = std::clamp(x.a("table:number-columns-repeated", "1").toInt(), 1, 16384);
					const QString st = x.a("table:style-name");
					// Uma faixa longa (o resto da folha) diz a largura padrão.
					if (rep > 64 && colunas_.count(st))
						larguraResto = colunas_[st];
					for (int i = 0; i < rep && col < 1024; i++, col++) {
						if (colunas_.count(st) && rep <= 64)
							larguras.emplace_back(col, colunas_[st]);
						estiloPadraoCol.push_back(x.a("table:default-cell-style-name"));
					}
					if (rep > 1024) col += rep;
				} else if (x.nome == "table:table-columns" || x.nome == "table:table-header-columns" || x.nome == "table:table-column-group") {
					colunas(x);
				}
			}
		};
		colunas(t);
		if (larguraResto > 0 && std::abs(larguraResto - LARGURA_PADRAO) > 2)
			fo.larguraPadrao = larguraResto;
		for (auto [c, w] : larguras)
			if (std::abs(w - fo.larguraPadrao) > 2)
				fo.largura[c] = w;
		int r = 0;
		std::function<void(const No &)> linhas = [&](const No &n) {
			for (const No &x : n.filhos) {
				if (x.nome == "table:table-row") {
					const int rep = std::clamp(x.a("table:number-rows-repeated", "1").toInt(), 1, MAX_LINHAS);
					// Linhas repetidas com conteúdo: até 1000 (o resto costuma ser vazio).
					bool temConteudo = false;
					for (const No &c : x.filhos)
						if (!c.filhos.empty() || c.atr.contains("office:value-type") || c.atr.contains("table:formula"))
							temConteudo = true;
					const int vezes = temConteudo ? std::min(rep, 1000) : 1;
					for (int i = 0; i < vezes; i++)
						linha(p, fi, x, r + i, estiloPadraoCol);
					const QString st = x.a("table:style-name");
					if (linhas_.count(st) && std::abs(linhas_[st] - ALTURA_PADRAO) > 2 && rep <= 1000)
						for (int i = 0; i < rep; i++)
							fo.altura[r + i] = linhas_[st];
					r += rep;
				} else if (x.nome == "table:table-rows" || x.nome == "table:table-header-rows" || x.nome == "table:table-row-group") {
					linhas(x);
				}
			}
		};
		linhas(t);
		fo.recontar();
		// Os gráficos: soltos na folha (table:shapes) ou presos a uma célula
		// (o LibreOffice põe dentro da célula, com x e y a partir dela).
		auto grafico = [&](const No &fr, double x0, double y0) {
			const No *ob = fr.filho("draw:object");
			if (!ob)
				return;
			Grafico g;
			// Em px inteiros (o cm com 3 casas não volta exato).
			g.x = std::round(x0 + pxDe(fr.a("svg:x")));
			g.y = std::round(y0 + pxDe(fr.a("svg:y")));
			g.w = std::max(120.0, std::round(pxDe(fr.a("svg:width"))));
			g.h = std::max(90.0, std::round(pxDe(fr.a("svg:height"))));
			QString href = ob->a("xlink:href");
			if (href.startsWith("./")) href = href.mid(2);
			if (lerGrafico(p, fi, href + "/content.xml", g))
				fo.graficos.push_back(g);
		};
		if (const No *sh = t.filho("table:shapes"))
			for (const No &fr : sh->filhos)
				if (fr.nome == "draw:frame")
					grafico(fr, 0, 0);
		for (const auto &[r, c, fr] : ancorados_)
			grafico(*fr, p.xDe(fi, c), p.yDe(fi, r));
		ancorados_.clear();
	}

	void linha(Pasta &p, int fi, const No &x, int r, const std::vector<QString> &padraoCol)
	{
		Folha &fo = p.folha(fi);
		int c = 0;
		for (const No &cel : x.filhos) {
			if (cel.nome != "table:table-cell" && cel.nome != "table:covered-table-cell")
				continue;
			const int rep = std::clamp(cel.a("table:number-columns-repeated", "1").toInt(), 1, MAX_COLUNAS);
			for (const No &fr : cel.filhos)
				if (fr.nome == "draw:frame" && std::none_of(ancorados_.begin(), ancorados_.end(), [&](const auto &a) { return std::get<2>(a) == &fr; }))
					ancorados_.emplace_back(r, c, &fr);
			const bool tem = std::any_of(cel.filhos.begin(), cel.filhos.end(), [](const No &n) { return n.nome != "draw:frame"; }) || cel.atr.contains("office:value-type") || cel.atr.contains("table:formula");
			QString st = cel.a("table:style-name");
			if (st.isEmpty() && c < int(padraoCol.size()) && padraoCol[size_t(c)] != "Default")
				st = padraoCol[size_t(c)];
			const bool temEstilo = !st.isEmpty() && st != "Default";
			if (!tem && (!temEstilo || rep > 64)) {
				c += rep;
				continue;
			}
			Celula k;
			if (temEstilo)
				k.fmt = formatoDe(st);
			const QString tipo = cel.a("office:value-type");
			if (tipo == "float" || tipo == "percentage" || tipo == "currency") {
				k.v = Valor::num(cel.a("office:value").toDouble());
				if (tipo == "percentage" && k.fmt.tipo == Formato::Geral) { k.fmt.tipo = Formato::Porcento; k.fmt.casas = 0; }
				if (tipo == "currency" && k.fmt.tipo == Formato::Geral) k.fmt.tipo = Formato::Moeda;
			} else if (tipo == "date") {
				const QString dv = cel.a("office:date-value");
				const QDate d = QDate::fromString(dv.left(10), Qt::ISODate);
				double frac = 0;
				if (dv.size() > 10) {
					const QTime h = QTime::fromString(dv.mid(11, 8), "HH:mm:ss");
					if (h.isValid()) frac = h.msecsSinceStartOfDay() / 86400000.0;
				}
				k.v = d.isValid() ? Valor::num(serialDe(d) + frac) : Valor::txt(dv);
				if (k.fmt.tipo == Formato::Geral) k.fmt.tipo = Formato::Data;
			} else if (tipo == "time") {
				k.v = Valor::txt(cel.a("office:time-value"));
			} else if (tipo == "boolean") {
				k.v = Valor::log(cel.a("office:boolean-value") == "true");
			} else {
				QStringList ps;
				for (const No &pp : cel.filhos)
					if (pp.nome == "text:p" || pp.nome == "text:h") {
						QString s;
						std::function<void(const No &)> junta = [&](const No &n) {
							for (const No &f : n.filhos) {
								if (f.nome.isEmpty()) s += f.texto;
								else if (f.nome == "text:s") s += QString(std::max(1, f.a("text:c", "1").toInt()), ' ');
								else if (f.nome == "text:tab") s += '\t';
								else if (f.nome == "text:line-break") s += '\n';
								else if (f.nome != "office:annotation") junta(f);
							}
						};
						junta(pp);
						ps << s;
					}
				if (!ps.isEmpty())
					k.v = Valor::txt(ps.join('\n'));
				if (!cel.a("office:string-value").isNull() && ps.isEmpty())
					k.v = Valor::txt(cel.a("office:string-value"));
			}
			const QString form = cel.a("table:formula");
			if (!form.isEmpty()) {
				k.f = analisar(form, Dialeto::Odf);
				if (!k.f) // "msoxl:=" e outros dialetos: tenta o do Excel
					k.f = analisar(form.section(':', 1), Dialeto::Canonico);
			}
			for (int i = 0; i < std::min(rep, 1024); i++)
				if (!k.vazia() || !k.fmt.padrao())
					fo.cel.insert(chave(r, c + i), k);
			c += rep;
		}
	}

	bool lerGrafico(Pasta &p, int fi, const QString &arq, Grafico &g)
	{
		No d;
		if (!lerXml(z_.ler(arq), d))
			return false;
		const No *ch = d.achar("chart:chart");
		if (!ch)
			return false;
		const QString cl = ch->a("chart:class");
		bool vertical = false;
		if (const No *as = d.achar("office:automatic-styles"))
			for (const No &s : as->filhos)
				if (const No *cp = s.filho("style:chart-properties"))
					if (cp->a("chart:vertical") == "true")
						vertical = true;
		if (cl == "chart:bar") g.tipo = vertical ? Grafico::Barras : Grafico::Colunas;
		else if (cl == "chart:line") g.tipo = Grafico::Linhas;
		else if (cl == "chart:area") g.tipo = Grafico::Area;
		else if (cl == "chart:circle" || cl == "chart:ring") g.tipo = Grafico::Pizza;
		else if (cl == "chart:scatter" || cl == "chart:bubble") g.tipo = Grafico::Dispersao;
		if (const No *t = ch->filho("chart:title"))
			g.titulo = t->textoTodo().trimmed();
		const No *pa = ch->filho("chart:plot-area");
		if (!pa)
			return false;
		// "Plan.A1:Plan.C5" ou várias faixas separadas por espaço.
		QString faixa = pa->a("table:cell-range-address");
		bool algum = false;
		int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
		// Separa por espaço, menos dentro de aspas ('Planilha 1'.A1).
		auto partes = [](const QString &f) {
			QStringList l;
			QString atual;
			bool aspas = false;
			for (const QChar ch : f) {
				if (ch == '\'') aspas = !aspas;
				if (ch == ' ' && !aspas) {
					if (!atual.isEmpty()) l << atual;
					atual.clear();
				} else {
					atual += ch;
				}
			}
			if (!atual.isEmpty()) l << atual;
			return l;
		};
		auto junta = [&](const QString &f) {
			for (const QString &parte : partes(f)) {
				PExpr e = analisar("[" + QString(parte).replace(":", ":") + "]", Dialeto::Odf);
				if (!e || (e->k != Expr::Ref && e->k != Expr::Area))
					continue;
				if (!e->folha.isEmpty() && e->folha != p.folha(fi).nome)
					continue;
				const int a = e->r1, b = e->c1, c = e->k == Expr::Area ? e->r2 : e->r1, dd = e->k == Expr::Area ? e->c2 : e->c1;
				if (!algum) { r1 = a; c1 = b; r2 = c; c2 = dd; algum = true; }
				r1 = std::min(r1, a); c1 = std::min(c1, b); r2 = std::max(r2, c); c2 = std::max(c2, dd);
			}
		};
		junta(faixa);
		std::vector<int> cols;
		int colCats = -1;
		auto coluna = [&](const QString &f) {
			PExpr e = analisar("[" + partes(f).value(0) + "]", Dialeto::Odf);
			return e && (e->k == Expr::Ref || e->k == Expr::Area) ? e->c1 : -1;
		};
		for (const No &s : pa->filhos) {
			if (s.nome == "chart:series") {
				junta(s.a("chart:values-cell-range-address"));
				junta(s.a("chart:label-cell-address"));
				const int c = coluna(s.a("chart:values-cell-range-address"));
				if (g.tipo == Grafico::Dispersao && cols.empty())
					if (const No *d = s.filho("chart:domain")) {
						junta(d->a("table:cell-range-address"));
						cols.push_back(coluna(d->a("table:cell-range-address")));
					}
				if (c >= 0)
					cols.push_back(c);
			} else if (s.nome == "chart:axis") {
				if (const No *ct = s.filho("chart:categories")) {
					junta(ct->a("table:cell-range-address"));
					colCats = coluna(ct->a("table:cell-range-address"));
				}
			}
		}
		if (!algum)
			return false;
		g.r1 = r1; g.c1 = colCats >= 0 ? colCats : c1; g.r2 = r2; g.c2 = c2;
		bool contiguas = !cols.empty();
		for (size_t i = 0; i < cols.size(); i++)
			if (cols[i] != (colCats >= 0 ? g.c1 + 1 : g.c1) + int(i))
				contiguas = false;
		if (contiguas && cols.back() == c2)
			cols.clear();
		g.series = cols;
		return true;
	}

	ZipLeitor z_;
	std::map<QString, Formato> dados_;
	std::map<QString, EstiloCel> celulas_;
	std::map<QString, int> colunas_, linhas_;
	std::vector<std::tuple<int, int, const No *>> ancorados_;
};

// --- CSV -------------------------------------------------------------------

inline QStringList camposCsv(const QString &linha, QChar sep)
{
	QStringList r;
	QString atual;
	bool aspas = false;
	for (int i = 0; i < linha.size(); i++) {
		const QChar c = linha[i];
		if (aspas) {
			if (c == '"') {
				if (i + 1 < linha.size() && linha[i + 1] == '"') { atual += '"'; i++; }
				else aspas = false;
			} else atual += c;
		} else if (c == '"') aspas = true;
		else if (c == sep) { r << atual; atual.clear(); }
		else atual += c;
	}
	r << atual;
	return r;
}

inline bool lerCsv(const QByteArray &dados, Pasta &p, const QString &nome)
{
	QString t = QString::fromUtf8(dados);
	if (t.contains(QChar::ReplacementCharacter))
		t = QString::fromLatin1(dados); // planilhas antigas do Windows
	if (t.startsWith(QChar(0xFEFF)))
		t = t.mid(1);
	// Linhas (com quebra dentro de aspas).
	QStringList linhas;
	QString atual;
	bool aspas = false;
	for (QChar c : t) {
		if (c == '"') aspas = !aspas;
		if (c == '\n' && !aspas) {
			if (atual.endsWith('\r')) atual.chop(1);
			linhas << atual;
			atual.clear();
		} else atual += c;
	}
	if (!atual.isEmpty())
		linhas << atual;
	const QString primeira = linhas.value(0);
	QChar sep = ';';
	int melhor = -1;
	for (QChar s : {QChar(';'), QChar(','), QChar('\t'), QChar('|')}) {
		const int n = int(camposCsv(primeira, s).size());
		if (n > melhor) { melhor = n; sep = s; }
	}
	p.folhas.clear();
	const int f = p.novaFolha(nome);
	for (int r = 0; r < linhas.size(); r++) {
		const QStringList cs = camposCsv(linhas[r], sep);
		for (int c = 0; c < cs.size(); c++) {
			if (cs[c].isEmpty())
				continue;
			// Num CSV com vírgula separando, o ponto é decimal.
			QString v = cs[c];
			if (sep == ',' && QRegularExpression(R"(^-?\d+\.\d+$)").match(v).hasMatch())
				v.replace('.', ',');
			Celula k;
			const Interpretado in = interpretar(v.startsWith('=') ? "'" + v : v, Formato());
			k.v = in.v;
			if (in.tipo) {
				k.fmt.tipo = *in.tipo;
				if (in.casas >= 0) k.fmt.casas = in.casas;
			}
			p.por(f, r, c, k);
		}
	}
	p.folha(f).recontar();
	return true;
}

inline QByteArray gravarCsv(const Pasta &p, int f)
{
	const Folha &fo = p.folha(f);
	QString s;
	for (int r = 0; r <= fo.ultLinha; r++) {
		QStringList cs;
		for (int c = 0; c <= fo.ultColuna; c++) {
			const Celula *k = fo.em(r, c);
			QString v;
			if (k) {
				if (k->v.t == Valor::Numero && (k->fmt.tipo == Formato::Geral || k->fmt.tipo == Formato::Numero))
					v = QString::number(k->v.n, 'g', 15).replace('.', ',');
				else
					v = mostrar(k->v, k->fmt);
			}
			if (v.contains(';') || v.contains('"') || v.contains('\n'))
				v = "\"" + v.replace("\"", "\"\"") + "\"";
			cs << v;
		}
		s += cs.join(';') + "\r\n";
	}
	return s.toUtf8();
}

// --- PDF -------------------------------------------------------------------
//
// Cada folha, da A1 até a última célula (e o último gráfico), em páginas
// A4: em pé ou deitada, o que couber melhor, reduzindo até 60% para caber
// na largura; o que não couber vai para a página do lado.

inline bool exportarPdf(Pasta &p, const QString &arq, QString *erro)
{
	QPdfWriter pdf(arq);
	pdf.setCreator("LumeBSD Números");
	pdf.setResolution(96);
	const double margem = 40;
	QPainter pt;
	bool primeira = true;
	for (int f = 0; f < p.quantas(); f++) {
		const Folha &fo = p.folha(f);
		int ultC = std::max(fo.ultColuna, 0), ultR = std::max(fo.ultLinha, 0);
		double largT = p.xDe(f, ultC + 1), altT = p.yDe(f, ultR + 1);
		for (const Grafico &g : fo.graficos) {
			largT = std::max(largT, g.x + g.w);
			altT = std::max(altT, g.y + g.h);
		}
		while (p.xDe(f, ultC + 1) < largT) ultC++;
		while (p.yDe(f, ultR + 1) < altT) ultR++;
		largT = p.xDe(f, ultC + 1); // até a borda da última coluna (o corte é nas colunas)
		altT = p.yDe(f, ultR + 1);
		const bool deitada = largT > 794 - 2 * margem;
		const QSizeF pg = deitada ? QSizeF(1123, 794) : QSizeF(794, 1123);
		pdf.setPageOrientation(deitada ? QPageLayout::Landscape : QPageLayout::Portrait);
		pdf.setPageSize(QPageSize(QPageSize::A4));
		pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
		const double util = pg.width() - 2 * margem;
		const double escalaP = std::clamp(util / std::max(1.0, largT), 0.6, 1.0);
		const double altUtil = (pg.height() - 2 * margem - 24) / escalaP;
		const double largUtil = util / escalaP;
		// Os cortes de página nas bordas das linhas e das colunas.
		std::vector<int> cortesR{0}, cortesC{0};
		double acc = 0;
		for (int r = 0; r <= ultR; r++) {
			if (acc + fo.alturaDe(r) > altUtil && r > cortesR.back()) { cortesR.push_back(r); acc = 0; }
			acc += fo.alturaDe(r);
		}
		acc = 0;
		for (int c = 0; c <= ultC; c++) {
			if (acc + fo.larguraDe(c) > largUtil && c > cortesC.back()) { cortesC.push_back(c); acc = 0; }
			acc += fo.larguraDe(c);
		}
		cortesR.push_back(ultR + 1);
		cortesC.push_back(ultC + 1);
		for (size_t ic = 0; ic + 1 < cortesC.size(); ic++)
			for (size_t ir = 0; ir + 1 < cortesR.size(); ir++) {
				if (primeira) {
					if (!pt.begin(&pdf)) {
						*erro = "não deu para gravar o PDF";
						return false;
					}
					primeira = false;
				} else {
					pdf.newPage();
				}
				pt.save();
				QFont cab("IBM Plex Sans");
				cab.setPixelSize(10);
				pt.setFont(cab);
				pt.setPen(QColor(0x5d, 0x59, 0x50));
				pt.drawText(QRectF(margem, margem - 22, util, 16), Qt::AlignLeft | Qt::AlignVCenter, fo.nome);
				pt.translate(margem, margem);
				pt.scale(escalaP, escalaP);
				const int r1 = cortesR[ir], r2 = cortesR[ir + 1] - 1, c1 = cortesC[ic], c2 = cortesC[ic + 1] - 1;
				pt.setClipRect(QRectF(0, 0, p.xDe(f, c2 + 1) - p.xDe(f, c1), p.yDe(f, r2 + 1) - p.yDe(f, r1)));
				desenharCelulas(pt, p, f, r1, c1, r2, c2);
				pt.translate(-p.xDe(f, c1), -p.yDe(f, r1));
				for (const Grafico &g : fo.graficos)
					desenharGrafico(pt, QRectF(g.x, g.y, g.w, g.h), p, f, g);
				pt.restore();
			}
	}
	if (primeira) {
		*erro = "a planilha está vazia";
		return false;
	}
	pt.end();
	return true;
}

// --- abrir e gravar pela extensão ------------------------------------------

inline QString extensao(const QString &arq) { return QFileInfo(arq).suffix().toLower(); }

inline bool abrirArquivo(const QString &arq, Pasta &p, QString *erro)
{
	QFile f(arq);
	if (!f.open(QIODevice::ReadOnly)) {
		*erro = f.errorString();
		return false;
	}
	const QByteArray dados = f.readAll();
	const QString ext = extensao(arq);
	bool ok;
	if (ext == "ods" || ext == "ots" || (dados.startsWith("PK") && dados.contains("opendocument.spreadsheet"))) {
		LeitorOds l;
		ok = l.ler(dados, p, erro);
	} else if (ext == "xlsx" || ext == "xlsm" || ext == "xltx" || dados.startsWith("PK")) {
		LeitorXlsx l;
		ok = l.ler(dados, p, erro);
	} else if (ext == "xls" || dados.startsWith("\xD0\xCF\x11\xE0")) {
		*erro = "é um .xls do Excel antigo (anterior a 2007), que os Números não leem; abra no LibreOffice e salve como .xlsx ou .ods";
		return false;
	} else {
		ok = lerCsv(dados, p, QFileInfo(arq).completeBaseName());
	}
	if (ok)
		p.recalcular();
	return ok;
}

inline bool gravarArquivo(const QString &arq, Pasta &p, QString *erro, int folhaCsv = 0)
{
	const QString ext = extensao(arq);
	if (ext == "pdf")
		return exportarPdf(p, arq, erro);
	QByteArray dados;
	if (ext == "xlsx") {
		EscritorXlsx e(p);
		dados = e.gravar();
	} else if (ext == "ods") {
		EscritorOds e(p);
		dados = e.gravar();
	} else if (ext == "csv" || ext == "txt") {
		dados = gravarCsv(p, folhaCsv);
	} else {
		*erro = "formato desconhecido: ." + ext + " (use .xlsx, .ods, .csv ou .pdf)";
		return false;
	}
	const QString tmp = arq + ".gravando";
	QFile f(tmp);
	if (!f.open(QIODevice::WriteOnly) || f.write(dados) != dados.size() || !f.flush()) {
		*erro = f.errorString();
		f.remove();
		return false;
	}
	f.close();
	QFile::remove(arq);
	if (!QFile::rename(tmp, arq)) {
		*erro = "não deu para gravar " + arq;
		return false;
	}
	return true;
}

// A estrutura (os testes comparam): cada célula com a fórmula, o valor
// mostrado e o formato; as larguras; os gráficos.
inline QString resumo(Pasta &p)
{
	QString r;
	QTextStream o(&r);
	o << "fonte " << p.fonte << " " << p.tamanho << "\n";
	for (int f = 0; f < p.quantas(); f++) {
		const Folha &fo = p.folha(f);
		o << "folha \"" << fo.nome << "\" congelar " << fo.congelarLinhas << "," << fo.congelarColunas << "\n";
		if (fo.larguraPadrao != LARGURA_PADRAO)
			o << "  largura padrão " << fo.larguraPadrao << "\n";
		for (auto [c, w] : fo.largura)
			o << "  coluna " << nomeColuna(c) << " " << w << "\n";
		for (auto [rr, h] : fo.altura)
			o << "  linha " << rr + 1 << " " << h << "\n";
		QList<quint64> ks = fo.cel.keys();
		std::sort(ks.begin(), ks.end());
		for (quint64 k : ks) {
			const Celula &c = fo.cel[k];
			o << "  " << nomeCelula(linhaDe(k), colunaDeChave(k)) << " ";
			if (c.f)
				o << "=" << textoDe(c.f, Dialeto::Brasil) << " -> ";
			o << "\"" << mostrar(c.v, c.fmt) << "\"";
			const Formato &m = c.fmt;
			if (!m.padrao()) {
				static const char *tipos[] = {"geral", "numero", "moeda", "porcento", "data", "texto"};
				o << " [" << tipos[m.tipo];
				if (!m.codigoData.isEmpty()) o << " " << m.codigoData;
				if (m.tipo == Formato::Numero || m.tipo == Formato::Moeda || m.tipo == Formato::Porcento) o << " " << m.casas;
				if (m.negrito) o << " N";
				if (m.italico) o << " I";
				if (m.sublinhado) o << " S";
				if (m.cor.isValid()) o << " cor" << m.cor.name();
				if (m.fundo.isValid()) o << " fundo" << m.fundo.name();
				if (m.alin) o << " alin" << m.alin;
				if (m.borda) o << " borda";
				if (m.tamanho > 0) o << " " << m.tamanho << "pt";
				o << "]";
			}
			o << "\n";
		}
		for (const Grafico &g : fo.graficos) {
			static const char *tipos[] = {"colunas", "barras", "linhas", "pizza", "dispersao", "area"};
			o << "  grafico " << tipos[g.tipo] << " \"" << g.titulo << "\" " << nomeCelula(g.r1, g.c1) << ":" << nomeCelula(g.r2, g.c2)
			  << " em " << qRound(g.x / 10) * 10 << "," << qRound(g.y / 10) * 10 << " " << qRound(g.w / 10) * 10 << "x" << qRound(g.h / 10) * 10 << "\n";
		}
	}
	return r;
}

// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: o .pptx (PowerPoint). O gravador escreve um pacote completo
// (mestre, oito leiautes com os lugares do título e do texto, tema, notas),
// com a posição, a letra e a cor de cada caixa escritas por extenso, para o
// slide sair igual em qualquer programa. O gráfico vai como gráfico de
// verdade (c:chartSpace, com a planilha dos dados embutida), a tabela como
// a:tbl, o vídeo com a capa e as animações na árvore p:timing.
//
// O leitor segue a herança do PowerPoint: a caixa do slide, o lugar no
// leiaute, o lugar no mestre e os estilos do mestre, para a posição, o
// tamanho da letra, as cores (do esquema do tema, com lumMod e lumOff) e os
// marcadores. As formas e imagens soltas do mestre e do leiaute (faixas,
// logotipos) entram em cada slide. Lê também os gráficos do PowerPoint (os
// valores guardados no próprio gráfico), as tabelas, os vídeos e as
// animações.

#pragma once

#include "base.h"
#include "desenho.h"

#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QTransform>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>

inline QString hex6(const QColor &c) { return c.name().mid(1).toUpper(); }
inline qint64 emu(double pt) { return qint64(std::llround(pt * 12700)); }
inline QString E(double pt) { return QString::number(emu(pt)); }

// A imagem de um gráfico (para o .pptx e o .odp), no dobro do tamanho.
inline QByteArray pngDoGrafico(const Doc &d, const Objeto &o)
{
	const double k = 2.0;
	QImage img(std::max(1, int(o.r.width() * k)), std::max(1, int(o.r.height() * k)), QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	{
		QPainter p(&img);
		p.scale(k, k);
		p.translate(-o.r.topLeft());
		Objeto x = o;
		x.giro = 0;
		desenharObjeto(p, d, x);
	}
	QByteArray b;
	QBuffer buf(&b);
	buf.open(QIODevice::WriteOnly);
	img.save(&buf, "PNG");
	return b;
}

// A letra da coluna (0: A, 1: B...).
inline QString colunaExcel(int c)
{
	QString s;
	for (c++; c > 0; c = (c - 1) / 26)
		s.prepend(QChar('A' + (c - 1) % 26));
	return s;
}

inline QString extensaoDe(const QByteArray &b, const QString &chave)
{
	if (b.startsWith("\x89PNG")) return "png";
	if (b.startsWith("\xff\xd8")) return "jpeg";
	if (b.startsWith("GIF8")) return "gif";
	if (b.startsWith("BM")) return "bmp";
	if (b.mid(4, 4) == "ftyp") return QFileInfo(chave).suffix().toLower() == "mov" ? "mov" : "mp4";
	if (b.startsWith("\x1a\x45\xdf\xa3")) return QFileInfo(chave).suffix().toLower() == "mkv" ? "mkv" : "webm";
	const QString e = QFileInfo(chave).suffix().toLower();
	return e == "jpg" ? "jpeg" : e.isEmpty() ? "png" : e;
}

inline QString mimeDe(const QString &ext)
{
	if (ext == "jpeg" || ext == "jpg") return "image/jpeg";
	if (ext == "gif") return "image/gif";
	if (ext == "bmp") return "image/bmp";
	if (ext == "svg") return "image/svg+xml";
	if (ext == "emf") return "image/x-emf";
	if (ext == "wmf") return "image/x-wmf";
	if (ext == "mp4" || ext == "m4v") return "video/mp4";
	if (ext == "mov") return "video/quicktime";
	if (ext == "webm") return "video/webm";
	if (ext == "mkv") return "video/x-matroska";
	if (ext == "avi") return "video/x-msvideo";
	if (ext == "ogv") return "video/ogg";
	if (ext == "mpg" || ext == "mpeg") return "video/mpeg";
	if (ext == "wmv") return "video/x-ms-wmv";
	return "image/png";
}

// --- gravar ----------------------------------------------------------------

#define CAB "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
#define NS "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\""
#define GRUPO "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"

class EscritorPptx
{
public:
	explicit EscritorPptx(const Doc &d) : d_(d) {}

	QByteArray gravar()
	{
		ZipEscritor z;
		const int n = int(d_.slides.size());
		const auto &ls = leiautes();
		z.add("_rels/.rels", rels({{"rId1", "officeDocument", "ppt/presentation.xml"},
		                           {"rId2", "package/2006/relationships/metadata/core-properties", "docProps/core.xml"},
		                           {"rId3", "extended-properties", "docProps/app.xml"}}));
		z.add("docProps/app.xml", QString(CAB "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
		                                      "<Application>LumeBSD Palco</Application><Slides>%1</Slides><Notes>%2</Notes></Properties>")
		                              .arg(n).arg(quantasNotas()).toUtf8());
		const QString agora = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
		z.add("docProps/core.xml", QString(CAB "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
		                                       "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
		                                       "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
		                                       "<dc:title>%1</dc:title><dc:creator>%2</dc:creator>"
		                                       "<dcterms:created xsi:type=\"dcterms:W3CDTF\">%3</dcterms:created>"
		                                       "<dcterms:modified xsi:type=\"dcterms:W3CDTF\">%3</dcterms:modified></cp:coreProperties>")
		                               .arg(esc(d_.titulo), esc(d_.autor), agora).toUtf8());

		// A apresentação.
		QString pres = CAB "<p:presentation " NS " saveSubsetFonts=\"1\"><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
		                   "<p:notesMasterIdLst><p:notesMasterId r:id=\"rId2\"/></p:notesMasterIdLst><p:sldIdLst>";
		std::vector<std::array<QString, 3>> rp = {{"rId1", "slideMaster", "slideMasters/slideMaster1.xml"},
		                                          {"rId2", "notesMaster", "notesMasters/notesMaster1.xml"},
		                                          {"rId3", "theme", "theme/theme1.xml"},
		                                          {"rId4", "presProps", "presProps.xml"},
		                                          {"rId5", "viewProps", "viewProps.xml"},
		                                          {"rId6", "tableStyles", "tableStyles.xml"}};
		for (int i = 0; i < n; i++) {
			pres += QString("<p:sldId id=\"%1\" r:id=\"rId%2\"/>").arg(256 + i).arg(10 + i);
			rp.push_back({QString("rId%1").arg(10 + i), "slide", QString("slides/slide%1.xml").arg(i + 1)});
		}
		pres += QString("</p:sldIdLst><p:sldSz cx=\"%1\" cy=\"%2\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/>").arg(E(d_.larg), E(d_.alt));
		pres += "<p:defaultTextStyle><a:defPPr><a:defRPr lang=\"pt-BR\"/></a:defPPr>"
		        "<a:lvl1pPr><a:defRPr sz=\"1800\"><a:solidFill><a:schemeClr val=\"tx1\"/></a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl1pPr>"
		        "</p:defaultTextStyle></p:presentation>";
		z.add("ppt/presentation.xml", pres.toUtf8());
		z.add("ppt/_rels/presentation.xml.rels", rels(rp));
		z.add("ppt/presProps.xml", CAB "<p:presentationPr " NS "/>");
		z.add("ppt/viewProps.xml", CAB "<p:viewPr " NS "><p:normalViewPr><p:restoredLeft sz=\"15620\"/><p:restoredTop sz=\"94660\"/></p:normalViewPr>"
		                              "<p:gridSpacing cx=\"76200\" cy=\"76200\"/></p:viewPr>");
		z.add("ppt/tableStyles.xml", CAB "<a:tblStyleLst xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" def=\"{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}\"/>");
		z.add("ppt/theme/theme1.xml", temaXml(d_.tema.nome + " (LumeBSD)").toUtf8());
		z.add("ppt/theme/theme2.xml", temaXml("Anotações").toUtf8());

		// O mestre e os leiautes.
		QString mestre = CAB "<p:sldMaster " NS "><p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"" + hex6(d_.tema.fundo) +
		                 "\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree>" GRUPO;
		{
			const auto m = moldes(Layout::TituloConteudo, d_.larg, d_.alt);
			mestre += lugar(2, "Título", "type=\"title\"", m[0].r, 2);
			mestre += lugar(3, "Texto", "type=\"body\" idx=\"1\"", m[1].r, 0);
		}
		mestre += "</p:spTree></p:cSld><p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
		          "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/><p:sldLayoutIdLst>";
		std::vector<std::array<QString, 3>> rm;
		for (size_t i = 0; i < ls.size(); i++) {
			mestre += QString("<p:sldLayoutId id=\"%1\" r:id=\"rId%2\"/>").arg(2147483649u + i).arg(i + 1);
			rm.push_back({QString("rId%1").arg(i + 1), "slideLayout", QString("../slideLayouts/slideLayout%1.xml").arg(i + 1)});
		}
		rm.push_back({QString("rId%1").arg(ls.size() + 1), "theme", "../theme/theme1.xml"});
		mestre += "</p:sldLayoutIdLst><p:txStyles>" + estilosMestre() + "</p:txStyles></p:sldMaster>";
		z.add("ppt/slideMasters/slideMaster1.xml", mestre.toUtf8());
		z.add("ppt/slideMasters/_rels/slideMaster1.xml.rels", rels(rm));
		for (size_t i = 0; i < ls.size(); i++) {
			static const QHash<int, QString> tipo = {{int(Layout::Titulo), "title"}, {int(Layout::TituloConteudo), "obj"},
			                                         {int(Layout::Secao), "secHead"}, {int(Layout::DuasColunas), "twoObj"},
			                                         {int(Layout::SoTitulo), "titleOnly"}, {int(Layout::Branco), "blank"}};
			const QString t = tipo.value(int(ls[i].id));
			QString l = CAB "<p:sldLayout " NS + (t.isEmpty() ? QString() : " type=\"" + t + "\"") + " preserve=\"1\"><p:cSld name=\"" +
			            esc(ls[i].nome) + "\"><p:spTree>" GRUPO;
			int id = 2;
			for (const Objeto &o : moldes(ls[i].id, d_.larg, d_.alt))
				l += lugar(id++, nomePapel(o.papel), phDe(o.papel, ls[i].id), o.r, o.vAlin, &o);
			l += "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
			z.add(QString("ppt/slideLayouts/slideLayout%1.xml").arg(i + 1), l.toUtf8());
			z.add(QString("ppt/slideLayouts/_rels/slideLayout%1.xml.rels").arg(i + 1),
			      rels({{"rId1", "slideMaster", "../slideMasters/slideMaster1.xml"}}));
		}
		// O mestre das anotações (o PowerPoint quer um, se houver notas).
		z.add("ppt/notesMasters/notesMaster1.xml",
		      QString(CAB "<p:notesMaster " NS "><p:cSld><p:spTree>" GRUPO
		                  "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Imagem do slide\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr>"
		                  "<p:nvPr><p:ph type=\"sldImg\" idx=\"2\"/></p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x=\"685800\" y=\"1143000\"/><a:ext cx=\"5486400\" cy=\"3086100\"/></a:xfrm>"
		                  "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/><a:ln w=\"12700\"><a:solidFill><a:prstClr val=\"black\"/></a:solidFill></a:ln></p:spPr></p:sp>"
		                  "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Anotações\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"body\" sz=\"quarter\" idx=\"1\"/></p:nvPr></p:nvSpPr>"
		                  "<p:spPr><a:xfrm><a:off x=\"685800\" y=\"4400550\"/><a:ext cx=\"5486400\" cy=\"3600450\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr>"
		                  "<p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang=\"pt-BR\"/></a:p></p:txBody></p:sp>"
		                  "</p:spTree></p:cSld><p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" "
		                  "accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
		                  "<p:notesStyle><a:lvl1pPr><a:defRPr sz=\"1200\"><a:solidFill><a:schemeClr val=\"tx1\"/></a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl1pPr></p:notesStyle>"
		                  "</p:notesMaster>")
		          .toUtf8());
		z.add("ppt/notesMasters/_rels/notesMaster1.xml.rels", rels({{"rId1", "theme", "../theme/theme2.xml"}}));

		// Os slides.
		for (int i = 0; i < n; i++)
			slide(z, i);
		for (auto it = midias_.cbegin(); it != midias_.cend(); ++it)
			z.add("ppt/media/" + it.key(), it.value(), false);
		QString tipos = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		                "<Default Extension=\"png\" ContentType=\"image/png\"/>"
		                "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
		                "<Default Extension=\"gif\" ContentType=\"image/gif\"/>"
		                "<Default Extension=\"bmp\" ContentType=\"image/bmp\"/>"
		                "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
		                "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
		                "<Override PartName=\"/ppt/notesMasters/notesMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.notesMaster+xml\"/>"
		                "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
		                "<Override PartName=\"/ppt/theme/theme2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
		                "<Override PartName=\"/ppt/presProps.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presProps+xml\"/>"
		                "<Override PartName=\"/ppt/viewProps.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.viewProps+xml\"/>"
		                "<Override PartName=\"/ppt/tableStyles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml\"/>"
		                "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
		                "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>";
		for (size_t i = 0; i < leiautes().size(); i++)
			tipos += QString("<Override PartName=\"/ppt/slideLayouts/slideLayout%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>").arg(i + 1);
		for (int i = 0; i < n; i++) {
			tipos += QString("<Override PartName=\"/ppt/slides/slide%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>").arg(i + 1);
			if (!d_.slides[size_t(i)].notas.trimmed().isEmpty())
				tipos += QString("<Override PartName=\"/ppt/notesSlides/notesSlide%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml\"/>").arg(i + 1);
		}
		for (int k = 1; k <= graficos_; k++)
			tipos += QString("<Override PartName=\"/ppt/charts/chart%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>").arg(k);
		if (graficos_)
			tipos += "<Default Extension=\"xlsx\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\"/>";
		QStringList exts;
		for (auto it = midias_.cbegin(); it != midias_.cend(); ++it) {
			const QString e = QFileInfo(it.key()).suffix().toLower();
			if (!exts.contains(e) && !QStringList{"png", "jpeg", "gif", "bmp"}.contains(e)) {
				exts << e;
				tipos += "<Default Extension=\"" + e + "\" ContentType=\"" + mimeDe(e) + "\"/>";
			}
		}
		tipos += "</Types>";
		z.add("[Content_Types].xml", tipos.toUtf8());
		return z.fechar();
	}

private:
	int quantasNotas() const
	{
		int k = 0;
		for (const Slide &s : d_.slides)
			k += !s.notas.trimmed().isEmpty();
		return k;
	}

	static QByteArray rels(const std::vector<std::array<QString, 3>> &l)
	{
		QString s = CAB "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
		for (const auto &r : l) {
			const QString tipo = r[1] == "@media"             ? QString("http://schemas.microsoft.com/office/2007/relationships/media")
			                     : r[1].startsWith("package/") ? "http://schemas.openxmlformats.org/" + r[1]
			                                                   : "http://schemas.openxmlformats.org/officeDocument/2006/relationships/" + r[1];
			s += "<Relationship Id=\"" + r[0] + "\" Type=\"" + tipo + "\" Target=\"" + esc(r[2]) + "\"/>";
		}
		s += "</Relationships>";
		return s.toUtf8();
	}

	static QString nomePapel(Papel p)
	{
		switch (p) {
		case Papel::Titulo: return "Título";
		case Papel::Subtitulo: return "Subtítulo";
		case Papel::Corpo: return "Texto";
		case Papel::Corpo2: return "Texto 2";
		default: return "Caixa de texto";
		}
	}

	static QString phDe(Papel p, Layout l)
	{
		switch (p) {
		case Papel::Titulo: return l == Layout::Titulo ? "type=\"ctrTitle\"" : "type=\"title\"";
		case Papel::Subtitulo: return "type=\"subTitle\" idx=\"3\"";
		case Papel::Corpo: return "type=\"body\" idx=\"1\"";
		case Papel::Corpo2: return "type=\"body\" idx=\"2\"";
		default: return {};
		}
	}

	QString xfrm(const Objeto &o, const char *tag = "a:xfrm") const
	{
		QString s = QString("<") + tag;
		if (o.giro != 0)
			s += QString(" rot=\"%1\"").arg(qint64(std::llround(o.giro * 60000)));
		if (o.viraH)
			s += " flipH=\"1\"";
		if (o.viraV)
			s += " flipV=\"1\"";
		s += QString("><a:off x=\"%1\" y=\"%2\"/><a:ext cx=\"%3\" cy=\"%4\"/></").arg(E(o.r.x()), E(o.r.y()), E(o.r.width()), E(o.r.height()));
		return s + tag + ">";
	}

	// Um lugar (placeholder) do mestre ou de um leiaute.
	QString lugar(int id, const QString &nome, const QString &ph, const QRectF &r, int vAlin, const Objeto *o = nullptr) const
	{
		Objeto x;
		x.r = r;
		QString s = QString("<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"%2\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph %3/></p:nvPr></p:nvSpPr>")
		                .arg(id).arg(esc(nome), ph);
		s += "<p:spPr>" + xfrm(x) + "</p:spPr><p:txBody><a:bodyPr anchor=\"" + QString(vAlin == 1 ? "ctr" : vAlin == 2 ? "b" : "t") + "\">" +
		     (o && o->encolher ? "<a:normAutofit/>" : "") + "</a:bodyPr><a:lstStyle>";
		if (o) {
			const QString algn = o->pars.empty() || o->pars.front().alin == 0 ? "l" : o->pars.front().alin == 1 ? "ctr" : "r";
			s += QString("<a:lvl1pPr algn=\"%1\"><a:defRPr sz=\"%2\"%3/></a:lvl1pPr>")
			         .arg(algn).arg(qint64(std::llround(o->tamanho * 100))).arg(o->italico ? " i=\"1\"" : "");
		}
		s += "</a:lstStyle><a:p><a:r><a:rPr lang=\"pt-BR\"/><a:t>" + esc(o ? dica(*o) : nome) + "</a:t></a:r></a:p></p:txBody></p:sp>";
		return s;
	}

	QString estilosMestre() const
	{
		const QString tit = hex6(d_.tema.titulo), tx = hex6(d_.tema.texto);
		QString s = "<p:titleStyle><a:lvl1pPr algn=\"l\"><a:lnSpc><a:spcPct val=\"90000\"/></a:lnSpc><a:buNone/><a:defRPr sz=\"3800\"" +
		            QString(d_.tema.tituloNegrito ? " b=\"1\"" : "") + "><a:solidFill><a:srgbClr val=\"" + tit +
		            "\"/></a:solidFill><a:latin typeface=\"+mj-lt\"/></a:defRPr></a:lvl1pPr></p:titleStyle><p:bodyStyle>";
		for (int n = 0; n < 5; n++) {
			const int sz = 2400 - n * 200;
			s += QString("<a:lvl%1pPr marL=\"%2\" indent=\"-%3\" algn=\"l\"><a:spcBef><a:spcPts val=\"%4\"/></a:spcBef><a:buFont typeface=\"Arial\"/>"
			             "<a:buChar char=\"%5\"/><a:defRPr sz=\"%6\"><a:solidFill><a:srgbClr val=\"%7\"/></a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl%1pPr>")
			         .arg(n + 1)
			         .arg(emu(sz / 100.0 * 1.15 * (n + 1)))
			         .arg(emu(sz / 100.0 * 1.15))
			         .arg(sz * 35 / 100)
			         .arg(n == 0 ? "•" : n == 1 ? "–" : "•")
			         .arg(sz)
			         .arg(tx);
		}
		s += "</p:bodyStyle><p:otherStyle><a:defPPr><a:defRPr lang=\"pt-BR\"/></a:defPPr><a:lvl1pPr><a:defRPr sz=\"1800\"><a:solidFill><a:srgbClr val=\"" + tx +
		     "\"/></a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl1pPr></p:otherStyle>";
		return s;
	}

	QString temaXml(const QString &nome) const
	{
		const Tema &t = d_.tema;
		auto c = [&](const char *n, const QColor &k) { return QString("<a:%1><a:srgbClr val=\"%2\"/></a:%1>").arg(n, hex6(k)); };
		auto serie = [&](size_t i) { return t.serie.empty() ? t.destaque : t.serie[i % t.serie.size()]; };
		QString s = CAB "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"" + esc(nome) +
		            "\"><a:themeElements><a:clrScheme name=\"" + esc(t.nome) + "\">";
		s += c("dk1", t.texto) + c("lt1", t.fundo) + c("dk2", t.titulo) + c("lt2", t.apagado);
		for (size_t i = 0; i < 5; i++)
			s += c(QString("accent%1").arg(i + 1).toLatin1().constData(), serie(i));
		s += c("accent6", t.destaque) + c("hlink", serie(0)) + c("folHlink", t.apagado);
		s += "</a:clrScheme><a:fontScheme name=\"" + esc(t.nome) + "\"><a:majorFont><a:latin typeface=\"" + esc(t.fonteTitulo) +
		     "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin typeface=\"" + esc(t.fonteTexto) +
		     "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme><a:fmtScheme name=\"LumeBSD\"><a:fillStyleLst>";
		const QString cheio = "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>";
		s += cheio + cheio + cheio + "</a:fillStyleLst><a:lnStyleLst>";
		for (int w : {6350, 12700, 19050})
			s += QString("<a:ln w=\"%1\">").arg(w) + cheio + "</a:ln>";
		s += "</a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle>"
		     "<a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst>" +
		     cheio + cheio + cheio + "</a:bgFillStyleLst></a:fmtScheme></a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>";
		return s;
	}

	static QString preench(const QColor &c)
	{
		if (!c.isValid() || c.alpha() == 0)
			return "<a:noFill/>";
		QString s = "<a:solidFill><a:srgbClr val=\"" + hex6(c) + "\"";
		if (c.alpha() < 255)
			s += QString("><a:alpha val=\"%1\"/></a:srgbClr>").arg(c.alpha() * 100000 / 255);
		else
			s += "/>";
		return s + "</a:solidFill>";
	}

	QString corpo(const Objeto &o) const
	{
		return "<p:txBody><a:bodyPr wrap=\"square\" lIns=\"91440\" tIns=\"45720\" rIns=\"91440\" bIns=\"45720\" rtlCol=\"0\" anchor=\"" +
		       QString(o.vAlin == 1 ? "ctr" : o.vAlin == 2 ? "b" : "t") + "\">" + (o.encolher ? "<a:normAutofit/>" : "<a:noAutofit/>") +
		       "</a:bodyPr><a:lstStyle/>" + pars(o) + "</p:txBody>";
	}

	// Os parágrafos (a:p) de um texto.
	QString pars(const Objeto &o) const
	{
		QString s;
		const bool temaNegrito = o.papel == Papel::Titulo && d_.tema.tituloNegrito;
		bool primeiro = true;
		for (const Paragrafo &p : o.pars) {
			const double base = p.trechos.empty() || p.trechos.front().tamanho <= 0 ? o.tamanho : p.trechos.front().tamanho;
			const double recuo = base * 1.15;
			QString pp = QString("<a:pPr");
			if (p.marcador || p.numerado)
				pp += QString(" marL=\"%1\" indent=\"-%2\"").arg(emu(recuo * (p.nivel + 1))).arg(emu(recuo));
			else if (p.nivel > 0)
				pp += QString(" marL=\"%1\" indent=\"0\"").arg(emu(recuo * p.nivel));
			else
				pp += " marL=\"0\" indent=\"0\"";
			if (p.nivel > 0)
				pp += QString(" lvl=\"%1\"").arg(p.nivel);
			pp += QString(" algn=\"%1\">").arg(p.alin == 1 ? "ctr" : p.alin == 2 ? "r" : p.alin == 3 ? "just" : "l");
			if (o.espaco != 1.0)
				pp += QString("<a:lnSpc><a:spcPct val=\"%1\"/></a:lnSpc>").arg(qint64(std::llround(o.espaco * 100000)));
			const bool corpoTxt = o.papel == Papel::Corpo || o.papel == Papel::Corpo2 || p.marcador || p.numerado;
			pp += QString("<a:spcBef><a:spcPts val=\"%1\"/></a:spcBef>").arg(primeiro || !corpoTxt ? 0 : qint64(std::llround(base * 35)));
			if (p.numerado)
				pp += "<a:buFont typeface=\"+mj-lt\"/><a:buAutoNum type=\"arabicPeriod\"/>";
			else if (p.marcador)
				pp += QString("<a:buFont typeface=\"Arial\"/><a:buChar char=\"%1\"/>").arg(p.nivel == 1 ? "–" : "•");
			else
				pp += "<a:buNone/>";
			pp += "</a:pPr>";
			s += "<a:p>" + pp;
			for (const Trecho &t : p.trechos) {
				if (t.texto.isEmpty())
					continue;
				const double tam = t.tamanho > 0 ? t.tamanho : o.tamanho;
				QString r = QString("<a:r><a:rPr lang=\"pt-BR\" sz=\"%1\"").arg(qint64(std::llround(tam * 100)));
				if (t.negrito || o.negrito || temaNegrito)
					r += " b=\"1\"";
				if (t.italico || o.italico)
					r += " i=\"1\"";
				if (t.sublinhado)
					r += " u=\"sng\"";
				r += " dirty=\"0\">" + preench(t.cor.isValid() ? t.cor : corTexto(d_, o));
				r += "<a:latin typeface=\"" + esc(t.fonte.isEmpty() ? fonteDe(d_, o) : t.fonte) + "\"/></a:rPr>";
				// Uma quebra de linha dentro do parágrafo.
				const QStringList linhas = t.texto.split(QChar(0x2028));
				for (int k = 0; k < linhas.size(); k++) {
					if (k)
						s += QString("<a:br><a:rPr lang=\"pt-BR\" sz=\"%1\"/></a:br>").arg(qint64(std::llround(tam * 100)));
					s += r + "<a:t>" + esc(linhas[k]) + "</a:t></a:r>";
				}
			}
			s += QString("<a:endParaRPr lang=\"pt-BR\" sz=\"%1\" dirty=\"0\"/></a:p>").arg(qint64(std::llround(base * 100)));
			primeiro = false;
		}
		if (o.pars.empty())
			s += "<a:p><a:endParaRPr lang=\"pt-BR\"/></a:p>";
		return s;
	}

	QString novaMidia(const QByteArray &b, const QString &chave)
	{
		for (auto it = midias_.cbegin(); it != midias_.cend(); ++it)
			if (it.value() == b)
				return it.key();
		const QString e = extensaoDe(b, chave);
		const QString nome = QString(mimeDe(e).startsWith("video/") ? "media%1.%2" : "image%1.%2").arg(midias_.size() + 1).arg(e);
		midias_.insert(nome, b);
		return nome;
	}

	void slide(ZipEscritor &z, int i)
	{
		const Slide &sl = d_.slides[size_t(i)];
		std::vector<std::array<QString, 3>> rs;
		int nLeiaute = 1;
		for (size_t k = 0; k < leiautes().size(); k++)
			if (leiautes()[k].id == sl.layout)
				nLeiaute = int(k) + 1;
		rs.push_back({"rId1", "slideLayout", QString("../slideLayouts/slideLayout%1.xml").arg(nLeiaute)});
		auto rel = [&](const QString &tipo, const QString &alvo) {
			const QString id = QString("rId%1").arg(rs.size() + 1);
			rs.push_back({id, tipo, alvo});
			return id;
		};
		QString s = CAB "<p:sld " NS + QString(sl.pular ? " show=\"0\"" : "") + "><p:cSld>";
		if (!sl.fundoMidia.isEmpty() && d_.midias.contains(sl.fundoMidia)) {
			const QString id = rel("image", "../media/" + novaMidia(d_.midias.value(sl.fundoMidia), sl.fundoMidia));
			s += "<p:bg><p:bgPr><a:blipFill dpi=\"0\" rotWithShape=\"1\"><a:blip r:embed=\"" + id +
			     "\"/><a:srcRect/><a:stretch><a:fillRect/></a:stretch></a:blipFill><a:effectLst/></p:bgPr></p:bg>";
		} else if (sl.fundo.isValid())
			s += "<p:bg><p:bgPr>" + preench(sl.fundo) + "<a:effectLst/></p:bgPr></p:bg>";
		s += "<p:spTree>" GRUPO;
		int id = 2;
		std::vector<int> spid(sl.objs.size(), 0);
		std::vector<bool> ehSp(sl.objs.size(), false);
		for (size_t k = 0; k < sl.objs.size(); k++) {
			const Objeto &o = sl.objs[k];
			const QString nome = !o.nome.isEmpty() ? o.nome : QString("%1 %2").arg(nomePapel(o.papel)).arg(id - 1);
			if (o.tipo == Tipo::Grafico) {
				// O gráfico de verdade (o PowerPoint e o LibreOffice editam), com os
				// dados também numa planilha embutida.
				const int g = ++graficos_;
				const QString rid = rel("chart", QString("../charts/chart%1.xml").arg(g));
				z.add(QString("ppt/charts/chart%1.xml").arg(g), graficoXml(o).toUtf8());
				z.add(QString("ppt/charts/_rels/chart%1.xml.rels").arg(g),
				      rels({{"rId1", "package", QString("../embeddings/Microsoft_Excel_Worksheet%1.xlsx").arg(g)}}));
				z.add(QString("ppt/embeddings/Microsoft_Excel_Worksheet%1.xlsx").arg(g), planilha(o.graf), false);
				spid[k] = id;
				s += QString("<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"%1\" name=\"%2\"/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr>")
				         .arg(id++)
				         .arg(esc(o.nome.isEmpty() ? QString("Gráfico %1").arg(g) : o.nome));
				s += xfrm(o, "p:xfrm") + "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
				     "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" r:id=\"" + rid + "\"/></a:graphicData></a:graphic></p:graphicFrame>";
				continue;
			}
			if (o.tipo == Tipo::Tabela) {
				spid[k] = id;
				s += tabelaXml(o, id, o.nome.isEmpty() ? QString("Tabela %1").arg(id - 1) : o.nome);
				id++;
				continue;
			}
			if (o.tipo == Tipo::Video) {
				const QByteArray v = d_.midias.value(o.midia);
				if (v.isEmpty())
					continue;
				const QString alvo = "../media/" + novaMidia(v, o.midia);
				const QString rv = rel("video", alvo);
				const QString rm = rel("@media", alvo);
				QByteArray capa = d_.midias.value(o.capa);
				if (capa.isEmpty()) {
					// Uma capa escura (o PowerPoint quer uma imagem).
					QImage img(320, 180, QImage::Format_RGB32);
					img.fill(QColor(0x1c, 0x1b, 0x19));
					QBuffer buf(&capa);
					buf.open(QIODevice::WriteOnly);
					img.save(&buf, "PNG");
				}
				const QString ri = rel("image", "../media/" + novaMidia(capa, o.capa.isEmpty() ? "capa.png" : o.capa));
				spid[k] = id;
				s += QString("<p:pic><p:nvPicPr><p:cNvPr id=\"%1\" name=\"%2\"><a:hlinkClick r:id=\"\" action=\"ppaction://media\"/></p:cNvPr>"
				             "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr><a:videoFile r:link=\"%3\"/><p:extLst>"
				             "<p:ext uri=\"{DAA4B4D4-6D71-4841-9C94-3DE7FCFB9230}\"><p14:media xmlns:p14=\"http://schemas.microsoft.com/office/powerpoint/2010/main\" r:embed=\"%4\"/></p:ext>"
				             "</p:extLst></p:nvPr></p:nvPicPr>")
				         .arg(id++)
				         .arg(esc(o.nome.isEmpty() ? QFileInfo(o.midia).fileName() : o.nome), rv, rm);
				s += "<p:blipFill><a:blip r:embed=\"" + ri + "\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>" + xfrm(o) +
				     "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>";
				if (o.borda.isValid() && o.espBorda > 0)
					s += QString("<a:ln w=\"%1\">").arg(emu(o.espBorda)) + preench(o.borda) + "</a:ln>";
				s += "</p:spPr></p:pic>";
				continue;
			}
			if (o.tipo == Tipo::Imagem) {
				const QByteArray b = d_.midias.value(o.midia);
				if (b.isEmpty())
					continue;
				const QString rid = rel("image", "../media/" + novaMidia(b, o.midia));
				spid[k] = id;
				s += QString("<p:pic><p:nvPicPr><p:cNvPr id=\"%1\" name=\"%2\"/><p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>")
				         .arg(id++)
				         .arg(esc(nome));
				s += "<p:blipFill><a:blip r:embed=\"" + rid + "\"/>";
				if (o.recorte != QRectF(0, 0, 1, 1)) {
					const QRectF c = o.recorte;
					s += QString("<a:srcRect l=\"%1\" t=\"%2\" r=\"%3\" b=\"%4\"/>")
					         .arg(qint64(std::llround(c.left() * 100000)))
					         .arg(qint64(std::llround(c.top() * 100000)))
					         .arg(qint64(std::llround((1 - c.right()) * 100000)))
					         .arg(qint64(std::llround((1 - c.bottom()) * 100000)));
				}
				s += "<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>" + xfrm(o) + "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>";
				if (o.borda.isValid() && o.espBorda > 0)
					s += QString("<a:ln w=\"%1\">").arg(emu(o.espBorda)) + preench(o.borda) + "</a:ln>";
				s += "</p:spPr></p:pic>";
				continue;
			}
			static const QHash<int, QString> prst = {{int(Tipo::Texto), "rect"}, {int(Tipo::Retangulo), "rect"}, {int(Tipo::Arredondado), "roundRect"},
			                                         {int(Tipo::Elipse), "ellipse"}, {int(Tipo::Triangulo), "triangle"}, {int(Tipo::Seta), "rightArrow"},
			                                         {int(Tipo::Linha), "line"}};
			if (o.tipo == Tipo::Linha) {
				spid[k] = id;
				s += QString("<p:cxnSp><p:nvCxnSpPr><p:cNvPr id=\"%1\" name=\"%2\"/><p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr><p:spPr>").arg(id++).arg(esc(nome));
				s += xfrm(o) + "<a:prstGeom prst=\"line\"><a:avLst/></a:prstGeom>";
				s += QString("<a:ln w=\"%1\">").arg(emu(std::max(0.5, o.espBorda))) + preench(o.borda.isValid() ? o.borda : d_.tema.texto) + "</a:ln>";
				s += "</p:spPr></p:cxnSp>";
				continue;
			}
			const QString ph = phDe(o.papel, sl.layout);
			spid[k] = id;
			ehSp[k] = true;
			s += QString("<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"%2\"/>").arg(id++).arg(esc(nome));
			if (!ph.isEmpty())
				s += "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph " + ph + "/></p:nvPr>";
			else
				s += QString("<p:cNvSpPr") + (o.tipo == Tipo::Texto ? " txBox=\"1\"" : "") + "/><p:nvPr/>";
			s += "</p:nvSpPr><p:spPr>" + xfrm(o) + "<a:prstGeom prst=\"" + prst.value(int(o.tipo), "rect") + "\"><a:avLst/></a:prstGeom>";
			s += preench(o.preench);
			if (o.borda.isValid() && o.espBorda > 0)
				s += QString("<a:ln w=\"%1\">").arg(emu(o.espBorda)) + preench(o.borda) + "</a:ln>";
			else
				s += "<a:ln><a:noFill/></a:ln>";
			s += "</p:spPr>" + corpo(o) + "</p:sp>";
		}
		s += "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>";
		if (sl.transicao != Transicao::Nenhuma) {
			const QString spd = sl.duracao < 0.6 ? "fast" : sl.duracao < 1.2 ? "med" : "slow";
			QString t;
			switch (sl.transicao) {
			case Transicao::Dissolver: t = "<p:fade/>"; break;
			case Transicao::PeloPreto: t = "<p:fade thruBlk=\"1\"/>"; break;
			case Transicao::Empurrar: t = "<p:push dir=\"l\"/>"; break;
			case Transicao::Cobrir: t = "<p:cover dir=\"l\"/>"; break;
			case Transicao::Descobrir: t = "<p:pull dir=\"l\"/>"; break;
			case Transicao::Ampliar: t = "<p:zoom/>"; break;
			default: break;
			}
			s += "<p:transition spd=\"" + spd + "\">" + t + "</p:transition>";
		}
		s += timingXml(sl, spid, ehSp);
		s += "</p:sld>";
		if (!sl.notas.trimmed().isEmpty()) {
			const QString nid = rel("notesSlide", QString("../notesSlides/notesSlide%1.xml").arg(i + 1));
			Q_UNUSED(nid);
			QString nt = CAB "<p:notes " NS "><p:cSld><p:spTree>" GRUPO
			             "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Imagem do slide\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr>"
			             "<p:nvPr><p:ph type=\"sldImg\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>"
			             "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Anotações\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr>"
			             "<p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/>";
			for (const QString &l : sl.notas.split('\n'))
				nt += l.isEmpty() ? "<a:p><a:endParaRPr lang=\"pt-BR\"/></a:p>" : "<a:p><a:r><a:rPr lang=\"pt-BR\"/><a:t>" + esc(l) + "</a:t></a:r></a:p>";
			nt += "</p:txBody></p:sp></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:notes>";
			z.add(QString("ppt/notesSlides/notesSlide%1.xml").arg(i + 1), nt.toUtf8());
			z.add(QString("ppt/notesSlides/_rels/notesSlide%1.xml.rels").arg(i + 1),
			      rels({{"rId1", "notesMaster", "../notesMasters/notesMaster1.xml"}, {"rId2", "slide", QString("../slides/slide%1.xml").arg(i + 1)}}));
		}
		z.add(QString("ppt/slides/slide%1.xml").arg(i + 1), s.toUtf8());
		z.add(QString("ppt/slides/_rels/slide%1.xml.rels").arg(i + 1), rels(rs));
	}
	// --- o gráfico (DrawingML) ---------------------------------------------

	// A planilha embutida com os dados (o "Editar dados" do PowerPoint):
	// as categorias na coluna A, uma série por coluna.
	static QByteArray planilha(const DadosGrafico &g)
	{
		ZipEscritor z;
		z.add("[Content_Types].xml", CAB "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		      "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
		      "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
		      "</Types>");
		z.add("_rels/.rels", CAB "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
		      "</Relationships>");
		z.add("xl/workbook.xml", CAB "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
		      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Planilha1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
		z.add("xl/_rels/workbook.xml.rels", CAB "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
		      "</Relationships>");
		auto txt = [](const QString &ref, const QString &t) { return "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t>" + esc(t) + "</t></is></c>"; };
		const int nS = g.tipo == TipoGrafico::Pizza ? std::min<int>(1, int(g.series.size())) : int(g.series.size());
		QString s = CAB "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData><row r=\"1\">";
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

	QString txPr(double tam, const QColor &cor, bool negrito = false) const
	{
		return QString("<c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"%1\" b=\"%2\">").arg(qint64(std::llround(tam * 100))).arg(negrito ? 1 : 0) +
		       preench(cor) + "<a:latin typeface=\"" + esc(d_.tema.fonteTexto) + "\"/></a:defRPr></a:pPr><a:endParaRPr lang=\"pt-BR\"/></a:p></c:txPr>";
	}

	QString graficoXml(const Objeto &o) const
	{
		const DadosGrafico &g = o.graf;
		const double letra = std::clamp(std::min(o.r.width(), o.r.height()) / 22, 9.0, 20.0);
		const bool pizza = g.tipo == TipoGrafico::Pizza;
		const int nS = pizza ? std::min<int>(1, int(g.series.size())) : int(g.series.size());
		const int nC = int(g.categorias.size());
		auto cor = [&](int i) { return d_.tema.serie.empty() ? d_.tema.texto : d_.tema.serie[size_t(i) % d_.tema.serie.size()]; };
		const QColor grade(d_.tema.apagado.red(), d_.tema.apagado.green(), d_.tema.apagado.blue());
		QString s = CAB "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
		                "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
		                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
		                "<c:date1904 val=\"0\"/><c:lang val=\"pt-BR\"/><c:roundedCorners val=\"0\"/><c:chart>";
		if (!g.titulo.isEmpty())
			s += "<c:title><c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr/></a:pPr><a:r>" +
			     QString("<a:rPr lang=\"pt-BR\" sz=\"%1\" b=\"0\">").arg(qint64(std::llround(letra * 1.25 * 100))) + preench(d_.tema.texto) +
			     "<a:latin typeface=\"" + esc(d_.tema.fonteTexto) + "\"/></a:rPr><a:t>" + esc(g.titulo) +
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
				s += QString("<c:spPr><a:ln w=\"%1\" cap=\"rnd\">").arg(emu(std::max(2.0, letra * 0.22))) + preench(cor(k)) + "<a:round/></a:ln></c:spPr>" +
				     "<c:marker><c:symbol val=\"circle\"/><c:size val=\"5\"/><c:spPr>" + preench(cor(k)) + "<a:ln><a:noFill/></a:ln></c:spPr></c:marker>";
			else if (pizza)
				s += "<c:spPr><a:ln w=\"19050\">" + preench(fundoDe(d_, Slide())) + "</a:ln></c:spPr>";
			else
				s += "<c:spPr>" + preench(cor(k)) + "<a:ln><a:noFill/></a:ln></c:spPr><c:invertIfNegative val=\"0\"/>";
			if (pizza)
				for (int c = 0; c < nC; c++)
					s += QString("<c:dPt><c:idx val=\"%1\"/><c:bubble3D val=\"0\"/><c:spPr>").arg(c) + preench(cor(c)) + "<a:ln w=\"19050\">" +
					     preench(fundoDe(d_, Slide())) + "</a:ln></c:spPr></c:dPt>";
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
					     txPr(letra * 0.95, cor(c).lightness() < 150 ? QColor(0xff, 0xff, 0xff) : QColor(0x1c, 0x1b, 0x19)) +
					     QString("<c:dLblPos val=\"ctr\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"0\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/>"
					             "<c:showPercent val=\"%1\"/><c:showBubbleSize val=\"0\"/></c:dLbl>")
					         .arg(pc > 4 ? 1 : 0);
				}
				s += "<c:numFmt formatCode=\"0%\" sourceLinked=\"0\"/><c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr>" +
				     txPr(letra * 0.95, QColor(0xff, 0xff, 0xff)) +
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
			const QString linhaEixo = "<c:spPr><a:ln w=\"15240\">" + preench(d_.tema.texto) + "</a:ln></c:spPr>";
			s += QString("<c:catAx><c:axId val=\"111111111\"/><c:scaling><c:orientation val=\"%1\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"%2\"/>")
			         .arg(deitado ? "maxMin" : "minMax", deitado ? "l" : "b") +
			     "<c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"low\"/>" +
			     linhaEixo + txPr(letra * 0.9, d_.tema.apagado) +
			     "<c:crossAx val=\"222222222\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>";
			s += QString("<c:valAx><c:axId val=\"222222222\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"%1\"/>")
			         .arg(deitado ? "b" : "l") +
			     "<c:majorGridlines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"" + hex6(grade) +
			     "\"><a:alpha val=\"28000\"/></a:srgbClr></a:solidFill></a:ln></c:spPr></c:majorGridlines>"
			     "<c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>"
			     "<c:spPr><a:ln><a:noFill/></a:ln></c:spPr>" +
			     txPr(letra * 0.9, d_.tema.apagado) +
			     QString("<c:crossAx val=\"111111111\"/><c:crosses val=\"%1\"/><c:crossBetween val=\"between\"/></c:valAx>").arg("autoZero");
		}
		s += "<c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr></c:plotArea>";
		if (g.legenda)
			s += "<c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/>" + txPr(letra * 0.95, d_.tema.texto) + "</c:legend>";
		s += "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>";
		s += "<c:spPr>" + (o.preench.isValid() ? preench(o.preench) : QString("<a:noFill/>")) + "<a:ln><a:noFill/></a:ln></c:spPr>";
		s += txPr(letra, d_.tema.texto);
		s += "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData></c:chartSpace>";
		return s;
	}

	// --- a tabela ----------------------------------------------------------

	QString tabelaXml(const Objeto &t0, int id, const QString &nome) const
	{
		Objeto t = t0;
		arrumarTabela(d_, t);
		const QColor fio = t.borda.isValid() && t.espBorda > 0 ? t.borda : d_.tema.apagado;
		const double esp = t.borda.isValid() && t.espBorda > 0 ? t.espBorda : 0.75;
		auto ln = [&](const char *tag) {
			return QString("<a:%1 w=\"%2\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\">").arg(tag).arg(emu(esp)) + preench(fio) +
			       "<a:prstDash val=\"solid\"/></a:" + tag + ">";
		};
		QString s = QString("<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"%1\" name=\"%2\"/><p:cNvGraphicFramePr><a:graphicFrameLocks noGrp=\"1\"/>"
		                    "</p:cNvGraphicFramePr><p:nvPr/></p:nvGraphicFramePr>")
		                .arg(id)
		                .arg(esc(nome));
		s += xfrm(t, "p:xfrm") + "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\"><a:tbl>";
		s += QString("<a:tblPr firstRow=\"%1\" bandRow=\"%2\"/><a:tblGrid>").arg(t.cabecalho ? 1 : 0).arg(t.faixas ? 1 : 0);
		for (double w : t.largs)
			s += "<a:gridCol w=\"" + E(w) + "\"/>";
		s += "</a:tblGrid>";
		for (int l = 0; l < t.lins; l++) {
			s += "<a:tr h=\"" + E(t.celula(l, 0).r.height()) + "\">";
			for (int c = 0; c < t.cols; c++) {
				const Objeto &x = t.celula(l, c);
				s += "<a:tc><a:txBody><a:bodyPr/><a:lstStyle/>" + pars(x) + "</a:txBody>";
				s += QString("<a:tcPr marL=\"91440\" marR=\"91440\" marT=\"45720\" marB=\"45720\" anchor=\"%1\">").arg(x.vAlin == 0 ? "t" : x.vAlin == 2 ? "b" : "ctr");
				s += ln("lnL") + ln("lnR") + ln("lnT") + ln("lnB");
				s += x.fundoCel.isValid() ? preench(x.fundoCel) : QString("<a:noFill/>");
				s += "</a:tcPr></a:tc>";
			}
			s += "</a:tr>";
		}
		s += "</a:tbl></a:graphicData></a:graphic></p:graphicFrame>";
		return s;
	}

	// --- as animações (p:timing) -------------------------------------------
	//
	// A árvore do PowerPoint: a sequência principal tem um "par" por clique;
	// dentro, um "par" por instante de começo; dentro, um por efeito.

	QString timingXml(const Slide &sl, const std::vector<int> &spid, const std::vector<bool> &ehSp) const
	{
		const auto grupos = sequencia(sl);
		bool algum = false;
		for (const auto &g : grupos)
			algum = algum || !g.empty();
		bool videos = false;
		for (size_t k = 0; k < sl.objs.size(); k++)
			videos = videos || (sl.objs[k].tipo == Tipo::Video && spid[k] != 0);
		if (!algum && !videos)
			return {};
		int id = 3;
		QString cliques;
		// Os vídeos que começam sozinhos: "tocar" no começo do slide.
		QString tocar;
		for (size_t k = 0; k < sl.objs.size(); k++)
			if (sl.objs[k].tipo == Tipo::Video && sl.objs[k].sozinho && spid[k] != 0) {
				tocar += QString("<p:par><p:cTn id=\"%1\" presetID=\"1\" presetClass=\"mediacall\" presetSubtype=\"0\" fill=\"hold\" nodeType=\"%2\">"
				                 "<p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst><p:cmd type=\"call\" cmd=\"playFrom(0.0)\">")
				             .arg(id)
				             .arg(tocar.isEmpty() ? "afterEffect" : "withEffect");
				id++;
				tocar += QString("<p:cBhvr><p:cTn id=\"%1\" dur=\"1\" fill=\"hold\"/><p:tgtEl><p:spTgt spid=\"%2\"/></p:tgtEl></p:cBhvr></p:cmd></p:childTnLst></p:cTn></p:par>")
				             .arg(id++)
				             .arg(spid[k]);
			}
		if (!tocar.isEmpty() && grupos.front().empty())
			cliques += QString("<p:par><p:cTn id=\"%1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"indefinite\"/><p:cond evt=\"onBegin\" delay=\"0\"><p:tn val=\"2\"/></p:cond>"
			                   "</p:stCondLst><p:childTnLst><p:par><p:cTn id=\"%2\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>")
			               .arg(id)
			               .arg(id + 1) +
			           tocar + "</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par>";
		if (!tocar.isEmpty() && grupos.front().empty())
			id += 2;
		std::set<std::pair<int, bool>> construcoes; // (objeto, por parágrafo)
		for (size_t gi = 0; gi < grupos.size(); gi++) {
			const auto &g = grupos[gi];
			if (g.empty())
				continue;
			QString par = QString("<p:par><p:cTn id=\"%1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"indefinite\"/>").arg(id++);
			if (gi == 0)
				par += "<p:cond evt=\"onBegin\" delay=\"0\"><p:tn val=\"2\"/></p:cond>";
			par += "</p:stCondLst><p:childTnLst>";
			// Os instantes de começo, em ordem.
			std::vector<double> ts;
			for (const Evento &e : g)
				if (std::find(ts.begin(), ts.end(), e.t0) == ts.end())
					ts.push_back(e.t0);
			bool primeiro = true;
			for (double t0 : ts) {
				par += QString("<p:par><p:cTn id=\"%1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"%2\"/></p:stCondLst><p:childTnLst>")
				           .arg(id++)
				           .arg(qint64(std::llround(t0 * 1000)));
				if (gi == 0 && t0 == ts.front() && !tocar.isEmpty()) {
					par += tocar;
					primeiro = false;
				}
				for (const Evento &e : g) {
					if (e.t0 != t0 || e.obj < 0 || size_t(e.obj) >= spid.size() || spid[size_t(e.obj)] == 0)
						continue;
					const Objeto &o = sl.objs[size_t(e.obj)];
					const QString nodo = primeiro && gi > 0 ? "clickEffect" : e.a.inicio == 2 ? "afterEffect" : "withEffect";
					primeiro = false;
					const bool txEl = e.par >= 0 && ehSp[size_t(e.obj)];
					QString alvo = QString("<p:tgtEl><p:spTgt spid=\"%1\"").arg(spid[size_t(e.obj)]);
					if (txEl)
						alvo += QString("><p:txEl><p:pRg st=\"%1\" end=\"%1\"/></p:txEl></p:spTgt></p:tgtEl>").arg(e.par);
					else
						alvo += "/></p:tgtEl>";
					if (!e.saida)
						construcoes.insert({e.obj, txEl});
					par += efeitoXml(e, o, alvo, nodo, id);
				}
				par += "</p:childTnLst></p:cTn></p:par>";
			}
			par += "</p:childTnLst></p:cTn></p:par>";
			cliques += par;
		}
		// Como cada vídeo toca: o volume, sem som e repetir.
		QString midia;
		for (size_t k = 0; k < sl.objs.size(); k++) {
			const Objeto &o = sl.objs[k];
			if (o.tipo != Tipo::Video || spid[k] == 0)
				continue;
			midia += QString("<p:video><p:cMediaNode vol=\"80000\"%1><p:cTn id=\"%2\"%3 fill=\"hold\" display=\"0\"><p:stCondLst><p:cond delay=\"indefinite\"/>"
			                 "</p:stCondLst></p:cTn><p:tgtEl><p:spTgt spid=\"%4\"/></p:tgtEl></p:cMediaNode></p:video>")
			             .arg(o.mudo ? " mute=\"1\"" : "")
			             .arg(id++)
			             .arg(o.repetir ? " repeatCount=\"indefinite\"" : "")
			             .arg(spid[k]);
		}
		QString s = "<p:timing><p:tnLst><p:par><p:cTn id=\"1\" dur=\"indefinite\" restart=\"never\" nodeType=\"tmRoot\"><p:childTnLst>";
		if (!cliques.isEmpty())
			s += "<p:seq concurrent=\"1\" nextAc=\"seek\"><p:cTn id=\"2\" dur=\"indefinite\" nodeType=\"mainSeq\"><p:childTnLst>" + cliques +
			     "</p:childTnLst></p:cTn><p:prevCondLst><p:cond evt=\"onPrev\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>"
			     "<p:nextCondLst><p:cond evt=\"onNext\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq>";
		s += midia + "</p:childTnLst></p:cTn></p:par></p:tnLst>";
		if (!construcoes.empty()) {
			s += "<p:bldLst>";
			std::set<int> feitos;
			for (const auto &[obj, porPar] : construcoes) {
				if (!ehSp[size_t(obj)] || feitos.count(obj))
					continue;
				feitos.insert(obj);
				s += QString("<p:bldP spid=\"%1\" grpId=\"0\"%2/>").arg(spid[size_t(obj)]).arg(porPar ? " build=\"p\"" : " animBg=\"1\"");
			}
			s += "</p:bldLst>";
		}
		return s + "</p:timing>";
	}

	// Um efeito: o código do PowerPoint (presetID e subtipo) e os comportamentos.
	static QString efeitoXml(const Evento &e, const Objeto &o, const QString &alvo, const QString &nodo, int &id)
	{
		Q_UNUSED(o);
		const Animacao &a = e.a;
		const bool sai = e.saida;
		const int ms = std::max(1, int(std::lround(a.dur * 1000)));
		const int dd = a.dir < 0 ? 0 : a.dir > 3 ? 3 : a.dir;
		// O subtipo da direção: 4 de baixo, 8 da esquerda, 2 da direita, 1 de cima.
		static const int sub[] = {4, 8, 2, 1};
		int preset = 1, subtipo = 0;
		switch (a.ef) {
		case Efeito::Aparecer: preset = 1; break;
		case Efeito::Dissolver: preset = 10; break;
		case Efeito::Voar: preset = 2; subtipo = sub[dd]; break;
		case Efeito::Ampliar: preset = 53; subtipo = 16; break;
		case Efeito::Revelar: preset = 22; subtipo = sub[dd]; break;
		default: break;
		}
		QString s = QString("<p:par><p:cTn id=\"%1\" presetID=\"%2\" presetClass=\"%3\" presetSubtype=\"%4\" fill=\"hold\"%5 nodeType=\"%6\">"
		                    "<p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>")
		                .arg(id++)
		                .arg(preset)
		                .arg(sai ? "exit" : "entr")
		                .arg(subtipo)
		                .arg(e.par >= 0 || sai ? QString() : QString(" grpId=\"0\""))
		                .arg(nodo);
		auto cbhvr = [&](int dur, const QString &extra = QString(), int atraso = 0) {
			QString c = QString("<p:cBhvr%1><p:cTn id=\"%2\" dur=\"%3\" fill=\"hold\"").arg(extra).arg(id++).arg(dur);
			if (atraso)
				c += QString("><p:stCondLst><p:cond delay=\"%1\"/></p:stCondLst></p:cTn>").arg(atraso);
			else
				c += "/>";
			return c + alvo;
		};
		auto visivel = [&](bool v, int atraso) {
			return "<p:set>" + cbhvr(1, QString(), atraso) + "<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>" +
			       "<p:to><p:strVal val=\"" + (v ? "visible" : "hidden") + "\"/></p:to></p:set>";
		};
		auto anim = [&](const QString &attr, const QString &de, const QString &para) {
			return "<p:anim calcmode=\"lin\" valueType=\"num\">" + cbhvr(ms, " additive=\"base\"") + "<p:attrNameLst><p:attrName>" + attr +
			       "</p:attrName></p:attrNameLst></p:cBhvr><p:tavLst><p:tav tm=\"0\"><p:val><p:strVal val=\"" + de +
			       "\"/></p:val></p:tav><p:tav tm=\"100000\"><p:val><p:strVal val=\"" + para + "\"/></p:val></p:tav></p:tavLst></p:anim>";
		};
		auto filtro = [&](const QString &f) {
			return QString("<p:animEffect transition=\"%1\" filter=\"%2\">").arg(sai ? "out" : "in", f) + cbhvr(ms) + "</p:cBhvr></p:animEffect>";
		};
		if (!sai)
			s += visivel(true, 0);
		if (a.ef == Efeito::Dissolver)
			s += filtro("fade");
		else if (a.ef == Efeito::Voar) {
			const QString px = sai ? "ppt_x" : "#ppt_x", py = sai ? "ppt_y" : "#ppt_y";
			const QString fx = a.dir == 1 ? "0-#ppt_w/2" : a.dir == 2 ? "1+#ppt_w/2" : px;
			const QString fy = a.dir == 0 ? "1+#ppt_h/2" : a.dir == 3 ? "0-#ppt_h/2" : py;
			s += sai ? anim("ppt_x", px, QString(fx).remove('#')) + anim("ppt_y", py, QString(fy).remove('#'))
			         : anim("ppt_x", fx, px) + anim("ppt_y", fy, py);
		} else if (a.ef == Efeito::Ampliar) {
			s += sai ? anim("ppt_w", "ppt_w", "0") + anim("ppt_h", "ppt_h", "0") : anim("ppt_w", "0", "#ppt_w") + anim("ppt_h", "0", "#ppt_h");
			s += filtro("fade");
		} else if (a.ef == Efeito::Revelar) {
			static const char *ent[] = {"wipe(up)", "wipe(right)", "wipe(left)", "wipe(down)"};
			static const char *sa[] = {"wipe(down)", "wipe(left)", "wipe(right)", "wipe(up)"};
			s += filtro((sai ? sa : ent)[dd]);
		}
		if (sai)
			s += visivel(false, a.ef == Efeito::Aparecer ? 0 : std::max(0, ms - 1));
		return s + "</p:childTnLst></p:cTn></p:par>";
	}

#undef CAB
#undef NS
#undef GRUPO

	const Doc &d_;
	QMap<QString, QByteArray> midias_;
	int graficos_ = 0;
};

// --- ler -------------------------------------------------------------------

// Uma parte do caminho relativa a outra ("ppt/slides/slide1.xml" + "../media/a.png").
inline QString juntarCaminho(const QString &parte, const QString &alvo)
{
	if (alvo.startsWith('/'))
		return alvo.mid(1);
	QStringList p = parte.split('/');
	p.removeLast();
	for (const QString &x : alvo.split('/')) {
		if (x == "..") {
			if (!p.isEmpty())
				p.removeLast();
		} else if (x != "." && !x.isEmpty())
			p << x;
	}
	return p.join('/');
}

// As propriedades do texto num nível (o que não se sabe fica "sem valor").
struct Prop {
	double sz = -1;
	int b = -1, i = -1, u = -1;
	std::optional<QColor> cor;
	QString fonte;
	int algn = -1;   // 0 1 2 3
	int bu = -1;     // 0 sem, 1 marcador, 2 número
	double espaco = -1;

	void herdar(const Prop &o)
	{
		if (sz < 0) sz = o.sz;
		if (b < 0) b = o.b;
		if (i < 0) i = o.i;
		if (u < 0) u = o.u;
		if (!cor) cor = o.cor;
		if (fonte.isEmpty()) fonte = o.fonte;
		if (algn < 0) algn = o.algn;
		if (bu < 0) bu = o.bu;
		if (espaco < 0) espaco = o.espaco;
	}
};
using Niveis = std::array<Prop, 9>;

struct Lugar {
	QString tipo, idx;
	std::optional<QRectF> r;
	Niveis lst;
	int anchor = -1;
	int autoFit = -1;
};

class LeitorPptx
{
public:
	bool ler(const QByteArray &dados, Doc &d, QString *erro)
	{
		if (!z_.abrir(dados) || !z_.tem("ppt/presentation.xml")) {
			*erro = "não é um .pptx (falta ppt/presentation.xml)";
			return false;
		}
		No pres;
		if (!lerXml(z_.ler("ppt/presentation.xml"), pres)) {
			*erro = "o ppt/presentation.xml está estragado";
			return false;
		}
		const QHash<QString, QString> rp = relacoes("ppt/presentation.xml");
		d = Doc();
		d.slides.clear();
		if (const No *sz = pres.achar("p:sldSz")) {
			d.larg = std::max(1.0, sz->a("cx").toDouble() / 12700);
			d.alt = std::max(1.0, sz->a("cy").toDouble() / 12700);
		}
		if (const No *dt = pres.achar("p:defaultTextStyle"))
			lerNiveis(*dt, padrao_);
		// O tema e o mestre (o do primeiro slide; os outros mestres se leem quando um slide pede).
		No core;
		if (lerXml(z_.ler("docProps/core.xml"), core)) {
			if (const No *t = core.achar("dc:title"))
				d.titulo = t->textoTodo();
			if (const No *t = core.achar("dc:creator"))
				d.autor = t->textoTodo();
		}
		bool temaFeito = false;
		if (const No *lst = pres.achar("p:sldIdLst")) {
			for (const No *s : lst->todos("p:sldId")) {
				const QString parte = juntarCaminho("ppt/presentation.xml", rp.value(s->a("r:id")));
				if (!z_.tem(parte))
					continue;
				Slide sl;
				if (!lerSlide(parte, d, sl, temaFeito))
					continue;
				d.slides.push_back(std::move(sl));
			}
		}
		if (d.slides.empty())
			d.slides.push_back(novoSlide(d, Layout::Titulo));
		return true;
	}

private:
	struct Mestre {
		QHash<QString, QColor> esquema; // dk1 lt1 ... (já pelo clrMap: bg1 tx1 também)
		QString fonteTitulo, fonteTexto, nomeTema;
		Niveis titulo, corpo, outro;
		std::vector<Lugar> lugares;
		std::vector<Objeto> enfeites;
		std::optional<QColor> fundo;
		QString fundoMidia;
	};
	struct Leiaute {
		QString nome, tipo;
		QString mestre;
		std::vector<Lugar> lugares;
		std::vector<Objeto> enfeites;
		std::optional<QColor> fundo;
		QString fundoMidia;
		bool mostraMestre = true;
	};
	// O que vale enquanto se lê uma parte (slide, leiaute ou mestre).
	struct Contexto {
		QString parte;
		QHash<QString, QString> rels;
		const Mestre *mestre = nullptr;
		const Leiaute *leiaute = nullptr;
		Doc *doc = nullptr;
		bool ehSlide = false;
	};

	QHash<QString, QString> relacoes(const QString &parte)
	{
		QHash<QString, QString> r;
		QStringList p = parte.split('/');
		const QString nome = p.takeLast();
		No raiz;
		if (!lerXml(z_.ler(p.join('/') + "/_rels/" + nome + ".rels"), raiz))
			return r;
		if (const No *rs = raiz.filho("rel:Relationships"))
			for (const No *x : rs->todos("rel:Relationship")) {
				if (x->a("TargetMode") == "External")
					continue;
				r.insert(x->a("Id"), x->a("Target"));
				tipos_.insert(parte + "#" + x->a("Id"), x->a("Type").section('/', -1));
			}
		return r;
	}
	QString alvoDoTipo(const QString &parte, const QHash<QString, QString> &rels, const QString &tipo)
	{
		for (auto it = rels.cbegin(); it != rels.cend(); ++it)
			if (tipos_.value(parte + "#" + it.key()) == tipo)
				return juntarCaminho(parte, it.value());
		return {};
	}

	// --- cores ---
	static QColor hsl(const QColor &c, double lumMod, double lumOff)
	{
		float h, s, l, a;
		c.getHslF(&h, &s, &l, &a);
		l = float(std::clamp(l * lumMod + lumOff, 0.0, 1.0));
		QColor r = QColor::fromHslF(h < 0 ? 0 : h, s, l, a);
		return r;
	}
	std::optional<QColor> cor(const No *n, const Mestre *m, const QColor *phClr = nullptr)
	{
		if (!n)
			return std::nullopt;
		QColor c;
		const No *f = nullptr;
		if ((f = n->filho("a:srgbClr")))
			c = QColor("#" + f->a("val"));
		else if ((f = n->filho("a:schemeClr"))) {
			const QString v = f->a("val");
			if (v == "phClr" && phClr)
				c = *phClr;
			else if (m)
				c = m->esquema.value(v);
		} else if ((f = n->filho("a:sysClr")))
			c = QColor("#" + f->a("lastClr", f->a("val") == "window" ? "FFFFFF" : "000000"));
		else if ((f = n->filho("a:prstClr"))) {
			c = QColor(f->a("val"));
			if (!c.isValid())
				c = Qt::black;
		} else if ((f = n->filho("a:scrgbClr")))
			c = QColor::fromRgbF(float(f->a("r").toDouble() / 100000), float(f->a("g").toDouble() / 100000), float(f->a("b").toDouble() / 100000));
		else if ((f = n->filho("a:hslClr")))
			c = QColor::fromHslF(float(f->a("hue").toDouble() / 21600000), float(f->a("sat").toDouble() / 100000), float(f->a("lum").toDouble() / 100000));
		if (!f || !c.isValid())
			return std::nullopt;
		double lumMod = 1, lumOff = 0;
		for (const No &k : f->filhos) {
			const double v = k.a("val").toDouble() / 100000;
			if (k.nome == "a:lumMod") lumMod = v;
			else if (k.nome == "a:lumOff") lumOff = v;
			else if (k.nome == "a:alpha") c.setAlphaF(float(v));
			else if (k.nome == "a:shade") c = QColor::fromRgbF(float(c.redF() * v), float(c.greenF() * v), float(c.blueF() * v), c.alphaF());
			else if (k.nome == "a:tint")
				c = QColor::fromRgbF(float(1 - (1 - c.redF()) * v), float(1 - (1 - c.greenF()) * v), float(1 - (1 - c.blueF()) * v), c.alphaF());
		}
		if (lumMod != 1 || lumOff != 0)
			c = hsl(c, lumMod, lumOff);
		return c;
	}

	// O preenchimento de um spPr (ou bgPr): nullopt = não diz; inválida = sem.
	std::optional<QColor> preench(const No *pr, const Mestre *m, const Contexto &cx, QString *midia = nullptr)
	{
		if (!pr)
			return std::nullopt;
		if (pr->filho("a:noFill"))
			return QColor();
		if (const No *s = pr->filho("a:solidFill"))
			return cor(s, m);
		if (const No *g = pr->filho("a:gradFill"))
			if (const No *gs = g->achar("a:gs"))
				return cor(gs, m);
		if (const No *b = pr->filho("a:blipFill")) {
			if (midia)
				if (const No *bl = b->filho("a:blip"))
					*midia = guardar(cx, bl->a("r:embed"));
			return QColor();
		}
		if (pr->filho("a:pattFill"))
			if (const No *fg = pr->achar("a:fgClr"))
				return cor(fg, m);
		return std::nullopt;
	}

	QString guardar(const Contexto &cx, const QString &rid)
	{
		const QString alvo = juntarCaminho(cx.parte, cx.rels.value(rid));
		if (rid.isEmpty() || !z_.tem(alvo))
			return {};
		const QByteArray b = z_.ler(alvo);
		if (b.isEmpty())
			return {};
		return cx.doc->guardarMidia(b, QFileInfo(alvo).suffix());
	}

	// --- texto ---
	void lerRPr(const No *r, Prop &p, const Mestre *m)
	{
		if (!r)
			return;
		if (r->atr.contains("sz")) p.sz = r->a("sz").toDouble() / 100;
		if (r->atr.contains("b")) p.b = r->a("b") == "1" || r->a("b") == "true";
		if (r->atr.contains("i")) p.i = r->a("i") == "1" || r->a("i") == "true";
		if (r->atr.contains("u")) p.u = r->a("u") != "none";
		if (const No *f = r->filho("a:solidFill"))
			p.cor = cor(f, m);
		if (const No *f = r->filho("a:latin"))
			p.fonte = f->a("typeface");
	}
	void lerPPr(const No *pp, Prop &p, const Mestre *m)
	{
		if (!pp)
			return;
		const QString a = pp->a("algn");
		if (!a.isEmpty())
			p.algn = a == "ctr" ? 1 : a == "r" ? 2 : (a == "just" || a == "dist") ? 3 : 0;
		if (pp->filho("a:buNone")) p.bu = 0;
		else if (pp->filho("a:buAutoNum")) p.bu = 2;
		else if (pp->filho("a:buChar") || pp->filho("a:buBlip")) p.bu = 1;
		if (const No *ls = pp->filho("a:lnSpc"))
			if (const No *pc = ls->filho("a:spcPct"))
				p.espaco = pc->a("val").toDouble() / 100000;
		lerRPr(pp->filho("a:defRPr"), p, m);
	}
	void lerNiveis(const No &lst, Niveis &n, const Mestre *m = nullptr)
	{
		for (int k = 0; k < 9; k++)
			lerPPr(lst.filho(QString("a:lvl%1pPr").arg(k + 1)), n[size_t(k)], m);
		if (const No *def = lst.filho("a:defPPr"))
			for (Prop &p : n) {
				Prop x;
				lerPPr(def, x, m);
				p.herdar(x);
			}
	}

	static QString tipoLugar(const No *ph)
	{
		QString t = ph->a("type", "body");
		if (t == "obj" || t == "tx")
			t = "body";
		return t;
	}
	static bool tituloTipo(const QString &t) { return t == "title" || t == "ctrTitle"; }

	static const Lugar *acharLugar(const std::vector<Lugar> &l, const QString &tipo, const QString &idx)
	{
		if (!idx.isEmpty())
			for (const Lugar &x : l)
				if (x.idx == idx && (x.tipo == tipo || (tituloTipo(x.tipo) == tituloTipo(tipo))))
					return &x;
		for (const Lugar &x : l)
			if (x.tipo == tipo)
				return &x;
		for (const Lugar &x : l)
			if (tituloTipo(x.tipo) && tituloTipo(tipo))
				return &x;
		if (!tituloTipo(tipo) && tipo != "dt" && tipo != "ftr" && tipo != "sldNum")
			for (const Lugar &x : l)
				if (x.tipo == "body" || x.tipo == "subTitle")
					if (!idx.isEmpty() && x.idx == idx)
						return &x;
		return nullptr;
	}
	static const Lugar *acharMestre(const std::vector<Lugar> &l, const QString &tipo)
	{
		const QString t = tituloTipo(tipo) ? "title" : (tipo == "subTitle" || tipo == "body") ? "body" : tipo;
		for (const Lugar &x : l)
			if (x.tipo == t || (tituloTipo(x.tipo) && t == "title"))
				return &x;
		return nullptr;
	}

	static std::optional<QRectF> lerXfrm(const No *x)
	{
		if (!x)
			return std::nullopt;
		const No *off = x->filho("a:off"), *ext = x->filho("a:ext");
		if (!off || !ext)
			return std::nullopt;
		return QRectF(off->a("x").toDouble() / 12700, off->a("y").toDouble() / 12700, ext->a("cx").toDouble() / 12700, ext->a("cy").toDouble() / 12700);
	}

	std::vector<Lugar> lerLugares(const No &spTree, const Mestre *m)
	{
		std::vector<Lugar> r;
		std::function<void(const No &)> anda = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (f.nome == "p:grpSp") {
					anda(f);
					continue;
				}
				if (f.nome != "p:sp" && f.nome != "p:pic")
					continue;
				const No *ph = f.achar("p:ph");
				if (!ph)
					continue;
				Lugar l;
				l.tipo = tipoLugar(ph);
				l.idx = ph->a("idx");
				if (const No *sp = f.filho("p:spPr"))
					l.r = lerXfrm(sp->filho("a:xfrm"));
				if (const No *tb = f.filho("p:txBody")) {
					if (const No *ls = tb->filho("a:lstStyle"))
						lerNiveis(*ls, l.lst, m);
					if (const No *bp = tb->filho("a:bodyPr")) {
						const QString a = bp->a("anchor");
						if (!a.isEmpty())
							l.anchor = a == "ctr" ? 1 : a == "b" ? 2 : 0;
						if (bp->filho("a:normAutofit")) l.autoFit = 1;
						else if (bp->filho("a:noAutofit") || bp->filho("a:spAutoFit")) l.autoFit = 0;
					}
				}
				r.push_back(l);
			}
		};
		anda(spTree);
		return r;
	}

	const Mestre *mestre(const QString &parte, Doc &d)
	{
		auto it = mestres_.find(parte);
		if (it != mestres_.end())
			return &it->second;
		Mestre &m = mestres_[parte];
		const QHash<QString, QString> rels = relacoes(parte);
		const QString parteTema = alvoDoTipo(parte, rels, "theme");
		No t;
		QHash<QString, QColor> esq;
		if (lerXml(z_.ler(parteTema), t)) {
			if (const No *th = t.filho("a:theme"))
				m.nomeTema = th->a("name");
			if (const No *cs = t.achar("a:clrScheme"))
				for (const No &k : cs->filhos)
					if (!k.nome.isEmpty()) {
						Mestre vazio;
						if (auto c = cor(&k, &vazio))
							esq.insert(k.nome.mid(2), *c);
					}
			if (const No *fs = t.achar("a:fontScheme")) {
				if (const No *mj = fs->achar("a:majorFont"))
					if (const No *l = mj->filho("a:latin"))
						m.fonteTitulo = l->a("typeface");
				if (const No *mn = fs->achar("a:minorFont"))
					if (const No *l = mn->filho("a:latin"))
						m.fonteTexto = l->a("typeface");
			}
		}
		No x;
		lerXml(z_.ler(parte), x);
		m.esquema = esq;
		if (const No *cm = x.achar("p:clrMap"))
			for (auto a = cm->atr.cbegin(); a != cm->atr.cend(); ++a)
				m.esquema.insert(a.key(), esq.value(a.value()));
		for (const char *k : {"bg1", "tx1", "bg2", "tx2"})
			if (!m.esquema.contains(k))
				m.esquema.insert(k, esq.value(QString(k).replace("bg", "lt").replace("tx", "dk")));
		if (const No *ts = x.achar("p:txStyles")) {
			if (const No *s = ts->filho("p:titleStyle")) lerNiveis(*s, m.titulo, &m);
			if (const No *s = ts->filho("p:bodyStyle")) lerNiveis(*s, m.corpo, &m);
			if (const No *s = ts->filho("p:otherStyle")) lerNiveis(*s, m.outro, &m);
		}
		Contexto cx{parte, rels, &m, nullptr, &d, false};
		if (const No *bg = x.achar("p:bg"))
			m.fundo = fundo(*bg, &m, cx, &m.fundoMidia);
		if (const No *st = x.achar("p:spTree")) {
			m.lugares = lerLugares(*st, &m);
			lerArvore(*st, QTransform(), cx, m.enfeites, true);
		}
		return &m;
	}

	const Leiaute *leiaute(const QString &parte, Doc &d)
	{
		auto it = leiautes_.find(parte);
		if (it != leiautes_.end())
			return &it->second;
		Leiaute &l = leiautes_[parte];
		const QHash<QString, QString> rels = relacoes(parte);
		l.mestre = alvoDoTipo(parte, rels, "slideMaster");
		const Mestre *m = mestre(l.mestre, d);
		No x;
		lerXml(z_.ler(parte), x);
		if (const No *sl = x.filho("p:sldLayout")) {
			l.tipo = sl->a("type");
			l.mostraMestre = sl->a("showMasterSp", "1") != "0";
		}
		if (const No *c = x.achar("p:cSld"))
			l.nome = c->a("name");
		Contexto cx{parte, rels, m, nullptr, &d, false};
		if (const No *bg = x.achar("p:bg"))
			l.fundo = fundo(*bg, m, cx, &l.fundoMidia);
		if (const No *st = x.achar("p:spTree")) {
			l.lugares = lerLugares(*st, m);
			lerArvore(*st, QTransform(), cx, l.enfeites, true);
		}
		return &l;
	}

	std::optional<QColor> fundo(const No &bg, const Mestre *m, const Contexto &cx, QString *midia)
	{
		if (const No *pr = bg.filho("p:bgPr"))
			return preench(pr, m, cx, midia);
		if (const No *ref = bg.filho("p:bgRef"))
			return cor(ref, m);
		return std::nullopt;
	}

	// --- objetos ---

	void lerArvore(const No &arvore, const QTransform &tr, const Contexto &cx, std::vector<Objeto> &saida, bool soEnfeites)
	{
		for (const No &f : arvore.filhos) {
			if (f.nome == "p:grpSp") {
				QTransform t = tr;
				if (const No *gp = f.filho("p:grpSpPr"))
					if (const No *xf = gp->filho("a:xfrm")) {
						const No *off = xf->filho("a:off"), *ext = xf->filho("a:ext"), *co = xf->filho("a:chOff"), *ce = xf->filho("a:chExt");
						if (off && ext && co && ce) {
							const double sx = ce->a("cx").toDouble() > 0 ? ext->a("cx").toDouble() / ce->a("cx").toDouble() : 1;
							const double sy = ce->a("cy").toDouble() > 0 ? ext->a("cy").toDouble() / ce->a("cy").toDouble() : 1;
							QTransform g;
							g.translate(off->a("x").toDouble() / 12700, off->a("y").toDouble() / 12700);
							g.scale(sx, sy);
							g.translate(-co->a("x").toDouble() / 12700, -co->a("y").toDouble() / 12700);
							t = g * tr;
						}
					}
				lerArvore(f, t, cx, saida, soEnfeites);
				continue;
			}
			if (f.nome == "mc:AlternateContent") {
				if (const No *fb = f.filho("mc:Fallback"))
					lerArvore(*fb, tr, cx, saida, soEnfeites);
				continue;
			}
			if (f.nome != "p:sp" && f.nome != "p:pic" && f.nome != "p:cxnSp" && f.nome != "p:graphicFrame")
				continue;
			const No *ph = f.achar("p:ph");
			if (soEnfeites && ph)
				continue;
			Objeto o;
			if (!lerObjeto(f, ph, cx, o))
				continue;
			if (!tr.isIdentity()) {
				const QRectF r = tr.mapRect(o.r);
				o.r = r;
			}
			saida.push_back(std::move(o));
		}
	}

	bool lerObjeto(const No &f, const No *ph, const Contexto &cx, Objeto &o)
	{
		const Mestre *m = cx.mestre;
		const QString tipoPh = ph ? tipoLugar(ph) : QString();
		const QString idx = ph ? ph->a("idx") : QString();
		const Lugar *lL = ph && cx.leiaute ? acharLugar(cx.leiaute->lugares, tipoPh, idx) : nullptr;
		const Lugar *lM = ph && m ? acharMestre(m->lugares, tipoPh) : nullptr;
		if (const No *c = f.achar("p:cNvPr")) {
			o.nome = c->a("name");
			o.idArq = c->a("id").toInt();
		}
		const No *spPr = f.filho("p:spPr");
		std::optional<QRectF> r;
		const No *xf = nullptr;
		if (f.nome == "p:graphicFrame")
			xf = f.filho("p:xfrm");
		else if (spPr)
			xf = spPr->filho("a:xfrm");
		r = lerXfrm(xf);
		if (!r && lL) r = lL->r;
		if (!r && lM) r = lM->r;
		if (!r)
			return false;
		o.r = *r;
		if (xf) {
			o.giro = xf->a("rot").toDouble() / 60000;
			o.viraH = xf->a("flipH") == "1";
			o.viraV = xf->a("flipV") == "1";
		}
		// O papel.
		if (ph) {
			if (tituloTipo(tipoPh))
				o.papel = Papel::Titulo;
			else if (tipoPh == "subTitle")
				o.papel = Papel::Subtitulo;
			else if (tipoPh == "body")
				o.papel = corpos_++ == 0 ? Papel::Corpo : Papel::Corpo2;
		}
		// O estilo (p:style) das formas do PowerPoint.
		std::optional<QColor> fillRef, lnRef, fontRef;
		if (const No *st = f.filho("p:style")) {
			if (const No *x = st->filho("a:fillRef"); x && x->a("idx").toInt() > 0) fillRef = cor(x, m);
			if (const No *x = st->filho("a:lnRef"); x && x->a("idx").toInt() > 0) lnRef = cor(x, m);
			if (const No *x = st->filho("a:fontRef")) fontRef = cor(x, m);
		}

		if (f.nome == "p:pic" && (f.achar("a:videoFile") || f.achar("p14:media"))) {
			o.tipo = Tipo::Video;
			QString v;
			if (const No *md = f.achar("p14:media"))
				v = guardar(cx, md->a("r:embed"));
			if (v.isEmpty())
				if (const No *vf = f.achar("a:videoFile"))
					v = guardar(cx, vf->a("r:link"));
			if (v.isEmpty())
				return false;
			o.midia = v;
			if (const No *bf = f.filho("p:blipFill"))
				if (const No *bl = bf->filho("a:blip"))
					o.capa = guardar(cx, bl->a("r:embed"));
			lerLinha(spPr, lnRef, o, m);
			return true;
		}
		if (f.nome == "p:pic") {
			o.tipo = Tipo::Imagem;
			if (const No *c = f.achar("p:cNvPr"))
				if (lerDados(c->a("descr"), o.graf)) {
					o.tipo = Tipo::Grafico;
					return true;
				}
			if (const No *bf = f.filho("p:blipFill")) {
				if (const No *bl = bf->filho("a:blip"))
					o.midia = guardar(cx, bl->a("r:embed"));
				if (const No *sr = bf->filho("a:srcRect")) {
					const double l = sr->a("l").toDouble() / 100000, t = sr->a("t").toDouble() / 100000;
					const double rr = sr->a("r").toDouble() / 100000, b = sr->a("b").toDouble() / 100000;
					if (l || t || rr || b)
						o.recorte = QRectF(l, t, std::max(0.01, 1 - l - rr), std::max(0.01, 1 - t - b));
				}
			}
			lerLinha(spPr, lnRef, o, m);
			return !o.midia.isEmpty();
		}
		if (f.nome == "p:graphicFrame")
			return lerQuadro(f, cx, o);

		QString prst = "rect";
		if (spPr)
			if (const No *g = spPr->filho("a:prstGeom"))
				prst = g->a("prst");
		if (f.nome == "p:cxnSp" || prst == "line" || prst.contains("Connector"))
			o.tipo = Tipo::Linha;
		else if (prst == "ellipse" || prst == "flowChartConnector")
			o.tipo = Tipo::Elipse;
		else if (prst == "roundRect" || prst == "flowChartAlternateProcess" || prst == "snipRoundRect")
			o.tipo = Tipo::Arredondado;
		else if (prst == "triangle")
			o.tipo = Tipo::Triangulo;
		else if (prst == "rightArrow")
			o.tipo = Tipo::Seta;
		const std::optional<QColor> fill = preench(spPr, m, cx);
		if (fill)
			o.preench = *fill;
		else if (fillRef)
			o.preench = *fillRef;
		lerLinha(spPr, lnRef, o, m);
		if (o.tipo == Tipo::Linha) {
			if (!o.borda.isValid() || o.espBorda <= 0) {
				o.borda = cx.doc->tema.texto;
				o.espBorda = 1;
			}
			return true;
		}
		// Um retângulo sem cor nem borda é só uma caixa de texto.
		const bool desenho = o.preench.isValid() || (o.borda.isValid() && o.espBorda > 0);
		if (o.tipo == Tipo::Texto && desenho)
			o.tipo = Tipo::Retangulo;
		lerTexto(f.filho("p:txBody"), ph, tipoPh, lL, lM, fontRef, cx, o);
		if (o.tipo == Tipo::Texto && !o.temTexto() && o.papel == Papel::Livre)
			return false;
		return true;
	}

	void lerLinha(const No *spPr, const std::optional<QColor> &lnRef, Objeto &o, const Mestre *m)
	{
		const No *ln = spPr ? spPr->filho("a:ln") : nullptr;
		if (ln && ln->filho("a:noFill")) {
			o.borda = QColor();
			return;
		}
		std::optional<QColor> c;
		if (ln)
			if (const No *s = ln->filho("a:solidFill"))
				c = cor(s, m);
		if (!c && lnRef)
			c = lnRef;
		if (c) {
			o.borda = *c;
			o.espBorda = ln && ln->atr.contains("w") ? ln->a("w").toDouble() / 12700 : 1;
		}
	}

	void lerTexto(const No *tb, const No *ph, const QString &tipoPh, const Lugar *lL, const Lugar *lM, const std::optional<QColor> &fontRef,
	              const Contexto &cx, Objeto &o)
	{
		const Mestre *m = cx.mestre;
		// O alinhamento vertical e o encolher.
		int anchor = -1, autoFit = -1;
		Niveis proprio;
		if (tb) {
			if (const No *bp = tb->filho("a:bodyPr")) {
				const QString a = bp->a("anchor");
				if (!a.isEmpty())
					anchor = a == "ctr" ? 1 : a == "b" ? 2 : 0;
				if (bp->filho("a:normAutofit")) autoFit = 1;
				else if (bp->filho("a:noAutofit") || bp->filho("a:spAutoFit")) autoFit = 0;
			}
			if (const No *ls = tb->filho("a:lstStyle"))
				lerNiveis(*ls, proprio, m);
		}
		for (const Lugar *l : {lL, lM})
			if (l) {
				if (anchor < 0) anchor = l->anchor;
				if (autoFit < 0) autoFit = l->autoFit;
			}
		o.vAlin = anchor < 0 ? (o.forma() ? 1 : (tituloTipo(tipoPh) ? 1 : 0)) : anchor;
		o.encolher = autoFit == 1;
		const Niveis *estilo = nullptr;
		if (m)
			estilo = !ph ? &padrao_ : tituloTipo(tipoPh) ? &m->titulo : (tipoPh == "body" || tipoPh == "subTitle") ? &m->corpo : &m->outro;
		auto nivel = [&](int n) {
			Prop p = proprio[size_t(n)];
			if (lL) p.herdar(lL->lst[size_t(n)]);
			if (lM) p.herdar(lM->lst[size_t(n)]);
			// A cor do estilo da forma (fontRef) vem antes dos estilos do mestre.
			if (fontRef && !p.cor) p.cor = fontRef;
			if (estilo) p.herdar((*estilo)[size_t(n)]);
			if (!ph && m) p.herdar(m->outro[size_t(n)]);
			if (tipoPh == "subTitle" && p.bu < 0) p.bu = 0;
			return p;
		};
		o.pars.clear();
		if (!tb)
			return;
		double primeiro = -1;
		for (const No *pn : tb->todos("a:p")) {
			Paragrafo par;
			const No *pPr = pn->filho("a:pPr");
			par.nivel = pPr ? std::clamp(pPr->a("lvl").toInt(), 0, 8) : 0;
			Prop np;
			lerPPr(pPr, np, m);
			np.herdar(nivel(par.nivel));
			par.alin = std::max(0, np.algn);
			par.marcador = np.bu == 1;
			par.numerado = np.bu == 2;
			if (np.espaco > 0)
				o.espaco = np.espaco;
			auto trecho = [&](const No *rPr, const QString &txt) {
				Prop rp;
				lerRPr(rPr, rp, m);
				rp.herdar(np);
				Trecho t;
				t.texto = txt;
				t.negrito = rp.b == 1;
				t.italico = rp.i == 1;
				t.sublinhado = rp.u == 1;
				t.tamanho = rp.sz > 0 ? rp.sz : 18;
				if (rp.cor)
					t.cor = *rp.cor;
				QString fn = rp.fonte;
				if (fn == "+mj-lt" || fn == "+mj-ea") fn = m ? m->fonteTitulo : QString();
				else if (fn == "+mn-lt" || fn == "+mn-ea" || fn.startsWith('+')) fn = m ? m->fonteTexto : QString();
				t.fonte = fn;
				if (primeiro < 0)
					primeiro = t.tamanho;
				par.trechos.push_back(t);
			};
			for (const No &k : pn->filhos) {
				if (k.nome == "a:r")
					trecho(k.filho("a:rPr"), k.filho("a:t") ? k.filho("a:t")->textoTodo() : QString());
				else if (k.nome == "a:fld")
					trecho(k.filho("a:rPr"), k.filho("a:t") ? k.filho("a:t")->textoTodo() : QString());
				else if (k.nome == "a:br")
					trecho(k.filho("a:rPr"), QString(QChar(0x2028)));
			}
			if (par.trechos.empty()) {
				// Um parágrafo vazio: o tamanho vem do endParaRPr.
				Prop rp;
				lerRPr(pn->filho("a:endParaRPr"), rp, m);
				rp.herdar(np);
				Trecho t;
				t.tamanho = rp.sz > 0 ? rp.sz : 18;
				if (primeiro < 0 && pn != tb->todos("a:p").back())
					primeiro = t.tamanho;
				par.trechos.push_back(t);
			}
			o.pars.push_back(par);
		}
		o.tamanho = primeiro > 0 ? primeiro : (nivel(0).sz > 0 ? nivel(0).sz : 18);
		// Um texto em branco não marca nada.
		bool algum = false;
		for (const Paragrafo &p : o.pars)
			algum = algum || !p.texto().trimmed().isEmpty();
		if (!algum) {
			Paragrafo p;
			if (!o.pars.empty()) {
				p.alin = o.pars.front().alin;
				p.marcador = o.pars.front().marcador;
			}
			o.pars = {p};
		}
	}

	// Os quadros: gráficos do PowerPoint e tabelas.
	bool lerQuadro(const No &f, const Contexto &cx, Objeto &o)
	{
		const No *gd = f.achar("a:graphicData");
		if (!gd)
			return false;
		const QString uri = gd->a("uri");
		if (uri.endsWith("/chart")) {
			const No *ch = gd->achar("c:chart");
			if (!ch)
				return false;
			const QString parte = juntarCaminho(cx.parte, cx.rels.value(ch->a("r:id")));
			No x;
			if (!lerXml(z_.ler(parte), x))
				return false;
			o.tipo = Tipo::Grafico;
			return lerGraficoOoxml(x, o.graf);
		}
		if (const No *tbl = gd->achar("a:tbl")) {
			const No *grid = tbl->filho("a:tblGrid");
			const auto trs = tbl->todos("a:tr");
			if (!grid || trs.empty())
				return false;
			std::vector<double> largs;
			for (const No *gc : grid->todos("a:gridCol"))
				largs.push_back(gc->a("w").toDouble() / 12700);
			if (largs.empty())
				return false;
			const QRectF r = o.r;
			const QString nome = o.nome;
			const int idArq = o.idArq;
			o = novaTabela(int(trs.size()), int(largs.size()), r);
			o.nome = nome;
			o.idArq = idArq;
			o.largs = largs;
			if (const No *pr = tbl->filho("a:tblPr")) {
				o.cabecalho = pr->a("firstRow") == "1" || pr->a("firstRow") == "true";
				o.faixas = pr->a("bandRow") == "1" || pr->a("bandRow") == "true";
			} else
				o.cabecalho = o.faixas = false;
			double tamanho = 0;
			for (int l = 0; l < o.lins; l++) {
				o.alts[size_t(l)] = trs[size_t(l)]->a("h").toDouble() / 12700;
				const auto tcs = trs[size_t(l)]->todos("a:tc");
				for (int c = 0; c < o.cols && c < int(tcs.size()); c++) {
					const No *tc = tcs[size_t(c)];
					Objeto &x = o.celula(l, c);
					lerTexto(tc->filho("a:txBody"), nullptr, QString(), nullptr, nullptr, std::nullopt, cx, x);
					if (x.pars.empty())
						x.pars = {Paragrafo()};
					// Sem cor escrita na célula, a cor vem do estilo da tabela
					// (no Palco, a do tema conforme o fundo), não do mestre.
					bool corEscrita = false;
					if (const No *tb = tc->filho("a:txBody"))
						for (const char *tag : {"a:rPr", "a:defRPr", "a:endParaRPr"})
							for (const No *r = tb->achar(tag); r && !corEscrita; r = nullptr)
								corEscrita = r->achar("a:solidFill") != nullptr;
					if (!corEscrita)
						for (Paragrafo &p : x.pars)
							for (Trecho &t : p.trechos)
								t.cor = QColor();
					x.vAlin = 0;
					if (const No *pr = tc->filho("a:tcPr")) {
						const QString an = pr->a("anchor", "t");
						x.vAlin = an == "ctr" ? 1 : an == "b" ? 2 : 0;
						if (const std::optional<QColor> fill = preench(pr, cx.mestre, cx))
							x.preench = *fill;
						if (!o.borda.isValid())
							if (const No *ln = pr->filho("a:lnT") ? pr->filho("a:lnT") : pr->filho("a:lnL"))
								if (const No *sf = ln->filho("a:solidFill"); sf && cor(sf, cx.mestre)) {
									o.borda = *cor(sf, cx.mestre);
									o.espBorda = ln->atr.contains("w") ? ln->a("w").toDouble() / 12700 : 1;
								}
					}
					if (tamanho <= 0 && x.temTexto())
						tamanho = x.tamanho;
				}
			}
			if (tamanho > 0)
				o.tamanho = tamanho;
			return true;
		}
		return false;
	}

public:
	static bool lerGraficoOoxml(const No &x, DadosGrafico &g)
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

private:
	// As animações (p:timing): a sequência principal, um "par" por clique.
	static void lerAnimacoes(const No &tm, Slide &sl, size_t n0)
	{
		const No *seq = nullptr;
		std::function<void(const No &)> acha = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (seq)
					return;
				if (f.nome == "p:seq")
					if (const No *c = f.filho("p:cTn"); c && c->a("nodeType") == "mainSeq") {
						seq = &f;
						return;
					}
				acha(f);
			}
		};
		acha(tm);
		auto objeto = [&](int spid) -> int {
			for (size_t k = n0; k < sl.objs.size(); k++)
				if (sl.objs[k].idArq == spid)
					return int(k);
			return -1;
		};
		// Como cada vídeo toca (sem som, repetir).
		std::function<void(const No &)> midias = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (f.nome == "p:cMediaNode") {
					const No *alvo = f.achar("p:spTgt");
					const int k = alvo ? objeto(alvo->a("spid").toInt()) : -1;
					if (k >= 0 && sl.objs[size_t(k)].tipo == Tipo::Video) {
						sl.objs[size_t(k)].mudo = f.a("mute") == "1" || f.a("mute") == "true";
						if (const No *c = f.filho("p:cTn"))
							sl.objs[size_t(k)].repetir = c->a("repeatCount") == "indefinite";
					}
				} else
					midias(f);
			}
		};
		midias(tm);
		if (!seq)
			return;
		const No *lista = seq->filho("p:cTn") ? seq->filho("p:cTn")->filho("p:childTnLst") : nullptr;
		if (!lista)
			return;
		int ordem = 0;
		std::set<std::pair<int, bool>> feitos;
		// Os efeitos, em ordem: os "par" com presetClass.
		std::function<void(const No &)> efeitos = [&](const No &n) {
			for (const No &f : n.filhos) {
				const No *c = f.nome == "p:par" ? f.filho("p:cTn") : nullptr;
				if (!c) {
					if (f.nome != "p:par")
						efeitos(f);
					continue;
				}
				const QString classe = c->a("presetClass");
				if (classe.isEmpty()) {
					efeitos(f);
					continue;
				}
				if (classe == "mediacall") {
					// "Tocar" sem clique: o vídeo começa sozinho.
					if (const No *alvo = f.achar("p:spTgt"); alvo && c->a("nodeType") != "clickEffect" && f.achar("p:cmd") &&
					                                          f.achar("p:cmd")->a("cmd").startsWith("play")) {
						const int k = objeto(alvo->a("spid").toInt());
						if (k >= 0 && sl.objs[size_t(k)].tipo == Tipo::Video)
							sl.objs[size_t(k)].sozinho = true;
					}
					continue;
				}
				if (classe != "entr" && classe != "exit")
					continue;
				const No *alvo = f.achar("p:spTgt");
				if (!alvo)
					continue;
				const int k = objeto(alvo->a("spid").toInt());
				if (k < 0)
					continue;
				const bool sai = classe == "exit";
				const bool porPar = alvo->achar("p:pRg") != nullptr;
				// Por parágrafo: o primeiro efeito vale para o objeto todo.
				if (feitos.count({k, sai}))
					continue;
				feitos.insert({k, sai});
				Animacao a;
				const int preset = c->a("presetID").toInt(), sub = c->a("presetSubtype").toInt();
				switch (preset) {
				case 1: a.ef = Efeito::Aparecer; break;
				case 2: case 3: case 12: case 47: a.ef = Efeito::Voar; break;
				case 22: case 18: case 3001: a.ef = Efeito::Revelar; break;
				case 23: case 53: case 31: case 21: case 55: a.ef = Efeito::Ampliar; break;
				default: a.ef = Efeito::Dissolver; break;
				}
				if (a.ef == Efeito::Voar || a.ef == Efeito::Revelar)
					a.dir = sub == 8 ? 1 : sub == 2 ? 2 : sub == 1 ? 3 : 0;
				const QString nodo = c->a("nodeType");
				a.inicio = nodo == "withEffect" ? 1 : nodo == "afterEffect" ? 2 : 0;
				// A duração: a do primeiro comportamento que não é um "set".
				a.dur = 0.5;
				for (const char *tag : {"p:animEffect", "p:anim", "p:animScale", "p:animMotion"})
					if (const No *b = f.achar(tag))
						if (const No *bc = b->achar("p:cTn")) {
							const double ms = bc->a("dur").toDouble();
							if (ms > 1) {
								a.dur = ms / 1000;
								break;
							}
						}
				a.porParagrafo = porPar && !sai;
				a.ordem = ordem++;
				Objeto &o = sl.objs[size_t(k)];
				(sai ? o.saida : o.entrada) = a;
			}
		};
		efeitos(*lista);
		// O primeiro clique do slide pode ser automático (onBegin): os efeitos
		// dele marcados "ao clicar" começam sozinhos.
		if (const No *p0 = lista->filho("p:par"))
			if (const No *c0 = p0->filho("p:cTn"))
				if (const No *st = c0->filho("p:stCondLst"))
					for (const No *cd : st->todos("p:cond"))
						if (cd->a("evt") == "onBegin") {
							std::set<int> nele;
							std::function<void(const No &)> junta = [&](const No &n) {
								for (const No &f : n.filhos) {
									if (f.nome == "p:cTn" && (f.a("presetClass") == "entr" || f.a("presetClass") == "exit"))
										if (const No *a = f.achar("p:spTgt"))
											nele.insert(objeto(a->a("spid").toInt()));
									junta(f);
								}
							};
							junta(*p0);
							for (int k : nele)
								if (k >= 0)
									for (Animacao *a : {&sl.objs[size_t(k)].entrada, &sl.objs[size_t(k)].saida})
										if (a->tem() && a->ordem == 0 && a->inicio == 0)
											a->inicio = 2;
						}
	}

	bool lerSlide(const QString &parte, Doc &d, Slide &sl, bool &temaFeito)
	{
		No x;
		if (!lerXml(z_.ler(parte), x))
			return false;
		const QHash<QString, QString> rels = relacoes(parte);
		const Leiaute *l = leiaute(alvoDoTipo(parte, rels, "slideLayout"), d);
		const Mestre *m = l ? mestre(l->mestre, d) : nullptr;
		if (!temaFeito && m) {
			temaFeito = true;
			temaDoMestre(*m, d);
		}
		const No *s = x.filho("p:sld");
		if (!s)
			return false;
		sl.pular = s->a("show", "1") == "0";
		// O leiaute pelo nome (os do Palco) ou pelo que o slide tem.
		sl.layout = Layout::TituloConteudo;
		inferirLeiaute_ = false;
		if (l) {
			bool achou = false;
			for (const auto &lt : leiautes())
				if (lt.nome == l->nome) {
					sl.layout = lt.id;
					achou = true;
				}
			if (!achou) {
				static const QHash<QString, Layout> pt = {{"title", Layout::Titulo}, {"secHead", Layout::Secao}, {"twoObj", Layout::DuasColunas},
				                                         {"twoTxTwoObj", Layout::DuasColunas}, {"titleOnly", Layout::SoTitulo}, {"blank", Layout::Branco}};
				sl.layout = pt.value(l->tipo, Layout::TituloConteudo);
				inferirLeiaute_ = true;
			}
		}
		Contexto cx{parte, rels, m, l, &d, true};
		// O fundo: o do slide, o do leiaute ou o do mestre.
		std::optional<QColor> f;
		QString fm;
		if (const No *bg = x.achar("p:bg"))
			f = fundo(*bg, m, cx, &fm);
		if (!f && l && l->fundo) {
			f = l->fundo;
			fm = l->fundoMidia;
		}
		if (!f && m && m->fundo) {
			f = m->fundo;
			fm = m->fundoMidia;
		}
		if (f && f->isValid() && *f != d.tema.fundo)
			sl.fundo = *f;
		sl.fundoMidia = fm;
		// Os enfeites do mestre e do leiaute vêm antes (ficam atrás).
		const bool mostra = s->a("showMasterSp", "1") != "0";
		if (mostra && l) {
			if (l->mostraMestre && m)
				for (const Objeto &o : m->enfeites)
					sl.objs.push_back(o);
			for (const Objeto &o : l->enfeites)
				sl.objs.push_back(o);
		}
		corpos_ = 0;
		const size_t n0 = sl.objs.size();
		for (Objeto &o : sl.objs)
			o.idArq = 0;
		if (const No *st = x.achar("p:spTree"))
			lerArvore(*st, QTransform(), cx, sl.objs, false);
		if (const No *tm = s->filho("p:timing"))
			lerAnimacoes(*tm, sl, n0);
		if (inferirLeiaute_)
			sl.layout = inferirLeiaute(sl);
		// A transição (direto ou dentro de mc:AlternateContent).
		const No *t = s->filho("p:transition");
		if (!t)
			if (const No *ac = s->filho("mc:AlternateContent"))
				t = ac->achar("p:transition");
		if (t) {
			const QString spd = t->a("spd", "fast");
			sl.duracao = spd == "slow" ? 1.5 : spd == "med" ? 1.0 : 0.5;
			if (t->atr.contains("p14:dur"))
				sl.duracao = t->a("p14:dur").toDouble() / 1000;
			for (const No &k : t->filhos) {
				const QString n = k.nome.section(':', 1);
				if (n.isEmpty() || n == "sndAc" || n == "extLst")
					continue;
				const bool preto = k.a("thruBlk") == "1" || k.a("thruBlk") == "true";
				if (n == "fade") sl.transicao = preto ? Transicao::PeloPreto : Transicao::Dissolver;
				else if (n == "push") sl.transicao = Transicao::Empurrar;
				else if (n == "cover") sl.transicao = Transicao::Cobrir;
				else if (n == "pull") sl.transicao = Transicao::Descobrir;
				else if (n == "zoom" || n == "newsflash") sl.transicao = Transicao::Ampliar;
				else sl.transicao = Transicao::Dissolver;
			}
		}
		// As notas.
		const QString pn = alvoDoTipo(parte, rels, "notesSlide");
		No nx;
		if (!pn.isEmpty() && lerXml(z_.ler(pn), nx))
			if (const No *st = nx.achar("p:spTree"))
				for (const No *sp : st->todos("p:sp")) {
					const No *ph = sp->achar("p:ph");
					if (!ph || ph->a("type", "body") != "body")
						continue;
					QStringList linhas;
					if (const No *tb = sp->filho("p:txBody"))
						for (const No *p : tb->todos("a:p"))
							linhas << p->textoTodo();
					while (!linhas.isEmpty() && linhas.last().trimmed().isEmpty())
						linhas.removeLast();
					sl.notas = linhas.join('\n');
				}
		return true;
	}

	void temaDoMestre(const Mestre &m, Doc &d)
	{
		// Um tema do Palco volta a ser ele mesmo (um tema de fora gravado pelo
		// Palco também leva o " (LumeBSD)" no nome).
		const bool doPalco = m.nomeTema.endsWith(" (LumeBSD)");
		const QString nomeTema = doPalco ? m.nomeTema.chopped(10) : m.nomeTema;
		bool achou = false;
		if (doPalco)
			for (const Tema &t : temas())
				if (t.nome == nomeTema) {
					d.tema = t;
					achou = true;
					break;
				}
		if (!achou) {
			Tema t;
			t.id = "arquivo";
			t.nome = nomeTema.isEmpty() ? "Do arquivo" : nomeTema;
			t.texto = m.esquema.value("tx1", Qt::black);
			t.fundo = m.fundo && m.fundo->isValid() ? *m.fundo : m.esquema.value("bg1", Qt::white);
			t.titulo = m.titulo[0].cor ? *m.titulo[0].cor : t.texto;
			t.destaque = m.esquema.value("accent1", t.texto);
			for (int k = 1; k <= 6; k++)
				t.serie.push_back(m.esquema.value(QString("accent%1").arg(k), t.destaque));
			t.apagado = QColor((t.texto.red() + t.fundo.red()) / 2, (t.texto.green() + t.fundo.green()) / 2, (t.texto.blue() + t.fundo.blue()) / 2);
			t.fonteTitulo = m.fonteTitulo.isEmpty() ? "IBM Plex Sans" : m.fonteTitulo;
			t.fonteTexto = m.fonteTexto.isEmpty() ? "IBM Plex Sans" : m.fonteTexto;
			t.tituloNegrito = m.titulo[0].b == 1;
			d.tema = t;
		}
		// As cores do tema no mestre também valem para o fundo de cada slide.
		if (m.fundo && m.fundo->isValid())
			d.tema.fundo = *m.fundo;
	}

	ZipLeitor z_;
	QHash<QString, QString> tipos_;
	std::map<QString, Mestre> mestres_;
	std::map<QString, Leiaute> leiautes_;
	Niveis padrao_;
	int corpos_ = 0;
	bool inferirLeiaute_ = false;
};

// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: o .odp (OpenDocument, o do Impress do LibreOffice). Como no
// .pptx, cada caixa leva a posição, a letra e a cor por extenso. Os lugares
// do título e dos tópicos vão com presentation:class, para o Impress
// reconhecer; o gráfico vai como objeto de gráfico (com a tabela de dados),
// a tabela como table:table, o vídeo como draw:plugin e as animações como
// as do Impress (anim:par, com os parágrafos marcados por xml:id).
//
// O leitor resolve os estilos (automáticos, comuns e os do mestre, com a
// herança de parent-style-name), as listas aninhadas, as formas, as imagens,
// os grupos, os enfeites do mestre, o fundo, as transições, as notas, os
// gráficos embutidos do LibreOffice (a tabela de dados do objeto), as
// tabelas, os vídeos e as animações.

#pragma once

#include "pptx.h"

#include <QRegularExpression>

inline QString cmDe(double pt) { return QString::number(pt / 72.0 * 2.54, 'f', 3) + "cm"; }

inline double ptDe(const QString &s)
{
	static const QRegularExpression re("^\\s*(-?[0-9.]+)\\s*([a-z%]*)");
	const auto m = re.match(s);
	if (!m.hasMatch())
		return 0;
	const double v = m.captured(1).toDouble();
	const QString u = m.captured(2);
	if (u == "cm") return v / 2.54 * 72;
	if (u == "mm") return v / 25.4 * 72;
	if (u == "in" || u == "inch") return v * 72;
	if (u == "pc") return v * 12;
	if (u == "px") return v * 0.75;
	return v; // pt
}

// --- gravar ----------------------------------------------------------------

#define CAB "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
#define NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" " \
	"xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" " \
	"xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" " \
	"xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" " \
	"xmlns:smil=\"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0\" xmlns:anim=\"urn:oasis:names:tc:opendocument:xmlns:animation:1.0\" " \
	"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" " \
	"xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\""

class EscritorOdp
{
public:
	explicit EscritorOdp(const Doc &d) : d_(d) {}

	QByteArray gravar()
	{
		QString paginas;
		for (size_t i = 0; i < d_.slides.size(); i++)
			paginas += pagina(int(i));
		ZipEscritor z;
		z.add("mimetype", "application/vnd.oasis.opendocument.presentation", false);
		QString content = CAB "<office:document-content " NS " office:version=\"1.3\"><office:automatic-styles>" + autos_ +
		                  "</office:automatic-styles><office:body><office:presentation>" + paginas +
		                  "</office:presentation></office:body></office:document-content>";
		z.add("content.xml", content.toUtf8());
		z.add("styles.xml", estilos().toUtf8());
		z.add("meta.xml", QString(CAB "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
		                              "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" office:version=\"1.3\">"
		                              "<office:meta><meta:generator>LumeBSD Palco</meta:generator><dc:title>%1</dc:title><dc:creator>%2</dc:creator>"
		                              "<meta:user-defined meta:name=\"lume-tema\">%3</meta:user-defined>"
		                              "<meta:user-defined meta:name=\"lume-leiautes\">%4</meta:user-defined>"
		                              "<meta:user-defined meta:name=\"lume-tema-nome\">%5</meta:user-defined>"
		                              "<meta:user-defined meta:name=\"lume-tema-cores\">%6</meta:user-defined></office:meta></office:document-meta>")
		                          .arg(esc(d_.titulo), esc(d_.autor), esc(d_.tema.id), leiautesTexto(), esc(d_.tema.nome), coresTema())
		                          .toUtf8());
		QString man = CAB "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
		                  "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.presentation\"/>"
		                  "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
		                  "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
		                  "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>";
		for (auto it = midias_.cbegin(); it != midias_.cend(); ++it) {
			man += "<manifest:file-entry manifest:full-path=\"" + esc(caminho(it.key())) + "\" manifest:media-type=\"" +
			       mimeDe(QFileInfo(it.key()).suffix()) + "\"/>";
			z.add(caminho(it.key()), it.value(), false);
		}
		for (const auto &[nome, dados, mime] : extras_) {
			man += "<manifest:file-entry manifest:full-path=\"" + esc(nome) + "\" manifest:media-type=\"" + esc(mime) + "\"/>";
			if (!nome.endsWith('/'))
				z.add(nome, dados);
		}
		man += "</manifest:manifest>";
		z.add("META-INF/manifest.xml", man.toUtf8());
		return z.fechar();
	}

private:
	// As cores do tema que o .odp não guarda (o destaque e as das séries).
	QString coresTema() const
	{
		QStringList l{d_.tema.destaque.name()};
		for (const QColor &c : d_.tema.serie)
			l << c.name();
		return l.join(',');
	}

	QString leiautesTexto() const
	{
		QStringList l;
		for (const Slide &s : d_.slides)
			l << QString::number(int(s.layout));
		return l.join(',');
	}

	static QString familiaCss(const QString &f) { return f.contains(' ') ? "'" + f + "'" : f; }

	// Um estilo automático (o mesmo conteúdo, o mesmo nome).
	QString estilo(const QString &prefixo, const QString &familia, const QString &corpo, const QString &pai = QString())
	{
		const QString chave = prefixo + "|" + familia + "|" + pai + "|" + corpo;
		auto it = nomes_.constFind(chave);
		if (it != nomes_.cend())
			return *it;
		const QString nome = prefixo + QString::number(++contador_[prefixo]);
		nomes_.insert(chave, nome);
		autos_ += "<style:style style:name=\"" + nome + "\" style:family=\"" + familia + "\"" +
		          (pai.isEmpty() ? QString() : " style:parent-style-name=\"" + pai + "\"") + ">" + corpo + "</style:style>";
		return nome;
	}

	QString nomeMidia(const QByteArray &b, const QString &chave)
	{
		for (auto it = midias_.cbegin(); it != midias_.cend(); ++it)
			if (it.value() == b)
				return it.key();
		QString e = extensaoDe(b, chave);
		if (e == "jpeg")
			e = "jpg";
		const QString nome = QString(mimeDe(e).startsWith("video/") ? "video%1.%2" : "imagem%1.%2").arg(midias_.size() + 1).arg(e);
		midias_.insert(nome, b);
		return nome;
	}

	static QString corOdf(const QColor &c) { return c.name(); }

	// Onde a mídia fica no pacote (o vídeo em Media/, como o LibreOffice).
	static QString caminho(const QString &nome)
	{
		return (mimeDe(QFileInfo(nome).suffix().toLower()).startsWith("video/") ? "Media/" : "Pictures/") + nome;
	}

	QString propsGraficas(const Objeto &o) const
	{
		QString g = "<style:graphic-properties";
		if (o.preench.isValid() && o.preench.alpha() > 0 && o.tipo != Tipo::Linha) {
			g += " draw:fill=\"solid\" draw:fill-color=\"" + corOdf(o.preench) + "\"";
			if (o.preench.alpha() < 255)
				g += QString(" draw:opacity=\"%1%\"").arg(o.preench.alpha() * 100 / 255);
		} else
			g += " draw:fill=\"none\"";
		if (o.borda.isValid() && o.espBorda > 0)
			g += " draw:stroke=\"solid\" svg:stroke-color=\"" + corOdf(o.borda) + "\" svg:stroke-width=\"" + cmDe(o.espBorda) + "\"";
		else if (o.tipo == Tipo::Linha)
			g += " draw:stroke=\"solid\" svg:stroke-color=\"" + corOdf(d_.tema.texto) + "\" svg:stroke-width=\"" + cmDe(1) + "\"";
		else
			g += " draw:stroke=\"none\"";
		if (o.aceitaTexto()) {
			g += QString(" draw:textarea-vertical-align=\"%1\" draw:textarea-horizontal-align=\"justify\" draw:auto-grow-height=\"false\" draw:auto-grow-width=\"false\" "
			             "fo:min-height=\"0cm\" fo:padding-top=\"%3\" fo:padding-bottom=\"%4\" fo:padding-left=\"%5\" fo:padding-right=\"%6\" "
			             "fo:wrap-option=\"wrap\" draw:fit-to-size=\"false\" style:shrink-to-fit=\"%2\"")
			         .arg(o.vAlin == 1 ? "middle" : o.vAlin == 2 ? "bottom" : "top")
			         .arg(o.encolher ? "true" : "false")
			         .arg(cmDe(o.margem.top()), cmDe(o.margem.bottom()), cmDe(o.margem.left()), cmDe(o.margem.right()));
		}
		return g + "/>";
	}

	QString propsTexto(const Doc &d, const Objeto &o, const Trecho &t) const
	{
		const double tam = t.tamanho > 0 ? t.tamanho : o.tamanho;
		const bool b = t.negrito || o.negrito || (o.papel == Papel::Titulo && d.tema.tituloNegrito);
		return QString("<style:text-properties fo:font-size=\"%1pt\" fo:font-family=\"%2\" fo:font-weight=\"%3\" fo:font-style=\"%4\" "
		               "style:text-underline-style=\"%5\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\" fo:color=\"%6\"/>")
		    .arg(QString::number(tam, 'g', 6), esc(familiaCss(t.fonte.isEmpty() ? fonteDe(d, o) : t.fonte)), b ? "bold" : "normal",
		         t.italico || o.italico ? "italic" : "normal", t.sublinhado ? "solid" : "none", corOdf(t.cor.isValid() ? t.cor : corTexto(d, o)));
	}

	QString paragrafo(const Objeto &o, const Paragrafo &p, bool primeiro)
	{
		const double base = p.trechos.empty() || p.trechos.front().tamanho <= 0 ? o.tamanho : p.trechos.front().tamanho;
		const bool corpoTxt = o.papel == Papel::Corpo || o.papel == Papel::Corpo2 || p.marcador || p.numerado;
		QString pp = QString("<style:paragraph-properties fo:text-align=\"%1\" fo:margin-top=\"%2\" fo:margin-bottom=\"0cm\" fo:line-height=\"%3%\"")
		                 .arg(p.alin == 1 ? "center" : p.alin == 2 ? "end" : p.alin == 3 ? "justify" : "start")
		                 .arg(cmDe(primeiro || !corpoTxt ? 0 : base * 0.35))
		                 .arg(qint64(std::llround(o.espaco * 100)));
		if (!p.marcador && !p.numerado && p.nivel > 0)
			pp += " fo:margin-left=\"" + cmDe(base * 1.15 * p.nivel) + "\"";
		pp += "/>";
		Trecho primeiroT = p.trechos.empty() ? Trecho() : p.trechos.front();
		primeiroT.texto.clear();
		const QString P = estilo("P", "paragraph", pp + propsTexto(d_, o, primeiroT));
		const QString pid = parPos_ < parIds_.size() ? parIds_[parPos_] : QString();
		parPos_++;
		QString s = "<text:p text:style-name=\"" + P + "\"" + (pid.isEmpty() ? QString() : " xml:id=\"" + pid + "\" text:id=\"" + pid + "\"") + ">";
		for (const Trecho &t : p.trechos) {
			if (t.texto.isEmpty())
				continue;
			const QString T = estilo("T", "text", propsTexto(d_, o, t));
			QString txt;
			int espacos = 0;
			auto despeja = [&] {
				if (espacos == 1)
					txt += ' ';
				else if (espacos > 1)
					txt += " <text:s text:c=\"" + QString::number(espacos - 1) + "\"/>";
				espacos = 0;
			};
			for (QChar c : t.texto) {
				if (c == ' ') {
					espacos++;
					continue;
				}
				despeja();
				if (c == '\t')
					txt += "<text:tab/>";
				else if (c == QChar(0x2028))
					txt += "<text:line-break/>";
				else
					txt += esc(QString(c));
			}
			despeja();
			s += "<text:span text:style-name=\"" + T + "\">" + txt + "</text:span>";
		}
		return s + "</text:p>";
	}

	// As listas: os parágrafos seguidos com marcador ou número, aninhados pelo nível.
	QString lista(const Objeto &o, size_t &i, int nivel, const QString &L, bool &primeiro)
	{
		QString s = "<text:list" + (nivel == 0 ? " text:style-name=\"" + L + "\"" : QString()) + ">";
		while (i < o.pars.size()) {
			const Paragrafo &p = o.pars[i];
			if (!(p.marcador || p.numerado) || p.nivel < nivel)
				break;
			s += "<text:list-item>";
			if (p.nivel == nivel) {
				s += paragrafo(o, p, primeiro);
				primeiro = false;
				i++;
			}
			if (i < o.pars.size() && (o.pars[i].marcador || o.pars[i].numerado) && o.pars[i].nivel > nivel)
				s += lista(o, i, nivel + 1, L, primeiro);
			s += "</text:list-item>";
		}
		return s + "</text:list>";
	}

	QString texto(const Objeto &o)
	{
		QString s;
		bool primeiro = true;
		for (size_t i = 0; i < o.pars.size();) {
			const Paragrafo &p = o.pars[i];
			if (p.marcador || p.numerado) {
				// O estilo da lista: o tipo de cada nível pelo primeiro parágrafo dele.
				size_t j = i;
				int tipo[5] = {0, 0, 0, 0, 0};
				bool visto[5] = {};
				while (j < o.pars.size() && (o.pars[j].marcador || o.pars[j].numerado)) {
					const int n = std::clamp(o.pars[j].nivel, 0, 4);
					if (!visto[n]) {
						visto[n] = true;
						tipo[n] = o.pars[j].numerado;
					}
					j++;
				}
				const double recuo = o.tamanho * 1.15;
				QString ls;
				for (int n = 0; n < 5; n++) {
					const QString props = "<style:list-level-properties text:space-before=\"" + cmDe(recuo * n) + "\" text:min-label-width=\"" + cmDe(recuo) +
				                      "\"/><style:text-properties fo:font-size=\"" + (tipo[n] ? "100%" : "80%") + "\" style:use-window-font-color=\"true\"/>";
					if (tipo[n])
						ls += QString("<text:list-level-style-number text:level=\"%1\" style:num-suffix=\".\" style:num-format=\"1\">").arg(n + 1) + props +
						      "</text:list-level-style-number>";
					else
						ls += QString("<text:list-level-style-bullet text:level=\"%1\" text:bullet-char=\"%2\">").arg(n + 1).arg(n == 1 ? "–" : "•") + props +
						      "</text:list-level-style-bullet>";
				}
				auto it = listas_.constFind(ls);
				QString L;
				if (it == listas_.cend()) {
					L = QString("L%1").arg(listas_.size() + 1);
					listas_.insert(ls, L);
					autos_ += "<text:list-style style:name=\"" + L + "\">" + ls + "</text:list-style>";
				} else
					L = *it;
				// Um nível que começa mais fundo que o zero ainda precisa dos níveis de cima.
				s += lista(o, i, 0, L, primeiro);
			} else {
				s += paragrafo(o, p, primeiro);
				primeiro = false;
				i++;
			}
		}
		return s;
	}

	QString posicao(const Objeto &o) const
	{
		const QString tam = " svg:width=\"" + cmDe(o.r.width()) + "\" svg:height=\"" + cmDe(o.r.height()) + "\"";
		if (o.giro == 0)
			return tam + " svg:x=\"" + cmDe(o.r.x()) + "\" svg:y=\"" + cmDe(o.r.y()) + "\"";
		// Girar em torno do centro: o ODF gira em torno da origem da forma
		// (o ângulo positivo é anti-horário) e depois desloca.
		const double a = -o.giro * M_PI / 180;
		const double w = o.r.width() / 2, h = o.r.height() / 2;
		const double cx = o.r.center().x() - (w * std::cos(a) + h * std::sin(a));
		const double cy = o.r.center().y() - (-w * std::sin(a) + h * std::cos(a));
		return tam + QString(" draw:transform=\"rotate (%1) translate (%2 %3)\"").arg(QString::number(a, 'g', 10), cmDe(cx), cmDe(cy));
	}

	QString objeto(const Objeto &o)
	{
		const QString camada = " draw:layer=\"layout\"";
		if (o.tipo == Tipo::Grafico) {
			// O gráfico do LibreOffice (um objeto embutido, com a tabela de
			// dados dentro) e a imagem dele para quem não abre o objeto.
			const QString obj = QString("Object %1").arg(++objetos_);
			extras_.push_back({obj + "/content.xml", graficoOdf(o).toUtf8(), "text/xml"});
			extras_.push_back({obj + "/styles.xml", QByteArray(CAB "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" office:version=\"1.3\"/>"), "text/xml"});
			extras_.push_back({obj + "/", QByteArray(), "application/vnd.oasis.opendocument.chart"});
			extras_.push_back({"ObjectReplacements/" + obj, pngDoGrafico(d_, o), "image/png"});
			Objeto semTexto = o;
			semTexto.borda = QColor();
			const QString gr = estilo("gr", "graphic", propsGraficas(semTexto), "standard");
			return "<draw:frame draw:style-name=\"" + gr + "\"" + camada + posicao(o) + (o.nome.isEmpty() ? QString() : " draw:name=\"" + esc(o.nome) + "\"") +
			       "><draw:object xlink:href=\"./" + obj + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>"
			       "<draw:image xlink:href=\"./ObjectReplacements/" + obj + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>";
		}
		if (o.tipo == Tipo::Tabela)
			return tabelaOdf(o, camada);
		if (o.tipo == Tipo::Video) {
			const QByteArray v = d_.midias.value(o.midia);
			if (v.isEmpty())
				return {};
			const QString nome = nomeMidia(v, o.midia);
			const QString gr = estilo("gr", "graphic", propsGraficas(o), "standard");
			QString s = "<draw:frame draw:style-name=\"" + gr + "\"" + camada + posicao(o) + (o.nome.isEmpty() ? QString() : " draw:name=\"" + esc(o.nome) + "\"") +
			            "><draw:plugin xlink:href=\"" + esc(caminho(nome)) + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\" draw:mime-type=\"" +
			            mimeDe(QFileInfo(nome).suffix()) + "\">";
			s += QString("<draw:param draw:name=\"Loop\" draw:value=\"%1\"/><draw:param draw:name=\"Mute\" draw:value=\"%2\"/>"
			             "<draw:param draw:name=\"VolumeDB\" draw:value=\"0\"/><draw:param draw:name=\"Zoom\" draw:value=\"fit\"/>")
			         .arg(o.repetir ? "true" : "false", o.mudo ? "true" : "false");
			s += "</draw:plugin>";
			const QByteArray capa = d_.midias.value(o.capa);
			if (!capa.isEmpty())
				s += "<draw:image xlink:href=\"Pictures/" + esc(nomeMidia(capa, o.capa)) + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>";
			return s + "</draw:frame>";
		}
		if (o.tipo == Tipo::Imagem) {
			QByteArray b;
			QString desc;
			{
				b = d_.midias.value(o.midia);
				// O recorte vai aplicado (o ODF mede o recorte em comprimento da imagem original).
				if (o.recorte != QRectF(0, 0, 1, 1)) {
					QImage img;
					if (img.loadFromData(b)) {
						const QRect c(int(o.recorte.x() * img.width()), int(o.recorte.y() * img.height()), std::max(1, int(o.recorte.width() * img.width())),
						              std::max(1, int(o.recorte.height() * img.height())));
						QByteArray nb;
						QBuffer buf(&nb);
						buf.open(QIODevice::WriteOnly);
						img.copy(c).save(&buf, "PNG");
						b = nb;
					}
				}
			}
			if (b.isEmpty())
				return {};
			Objeto semTexto = o;
			semTexto.preench = QColor();
			if (o.tipo == Tipo::Grafico)
				semTexto.borda = QColor();
			const QString gr = estilo("gr", "graphic", propsGraficas(semTexto) + "", "standard");
			const QString nome = nomeMidia(b, o.tipo == Tipo::Grafico ? "grafico.png" : o.midia);
			QString s = "<draw:frame draw:style-name=\"" + gr + "\"" + camada + posicao(o) + (o.nome.isEmpty() ? QString() : " draw:name=\"" + esc(o.nome) + "\"") + ">";
			s += "<draw:image xlink:href=\"Pictures/" + esc(nome) + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>";
			if (!desc.isEmpty())
				s += "<svg:title>Gráfico</svg:title><svg:desc>" + esc(desc) + "</svg:desc>";
			return s + "</draw:frame>";
		}
		if (o.tipo == Tipo::Linha) {
			const QString gr = estilo("gr", "graphic", propsGraficas(o), "standard");
			QPointF a = o.r.topLeft(), b = o.r.bottomRight();
			if (o.viraV)
				std::swap(a.ry(), b.ry());
			if (o.viraH)
				std::swap(a.rx(), b.rx());
			return "<draw:line draw:style-name=\"" + gr + "\"" + camada + " svg:x1=\"" + cmDe(a.x()) + "\" svg:y1=\"" + cmDe(a.y()) + "\" svg:x2=\"" +
			       cmDe(b.x()) + "\" svg:y2=\"" + cmDe(b.y()) + "\"><text:p/></draw:line>";
		}
		const QString classe = o.papel == Papel::Titulo ? "title" : o.papel == Papel::Subtitulo ? "subtitle" : (o.papel == Papel::Corpo || o.papel == Papel::Corpo2) ? "outline" : "";
		if (o.tipo == Tipo::Texto) {
			QString atrib;
			if (!classe.isEmpty()) {
				const QString pai = classe == "title" ? "Lume-title" : classe == "subtitle" ? "Lume-subtitle" : "Lume-outline1";
				atrib = " presentation:style-name=\"" + estilo("pr", "presentation", propsGraficas(o), pai) + "\" presentation:class=\"" + classe + "\"" +
				        (o.temTexto() ? QString() : " presentation:placeholder=\"true\"");
			} else
				atrib = " draw:style-name=\"" + estilo("gr", "graphic", propsGraficas(o), "standard") + "\"";
			return "<draw:frame" + atrib + camada + posicao(o) + "><draw:text-box>" + (o.temTexto() ? texto(o) : QString()) + "</draw:text-box></draw:frame>";
		}
		static const QHash<int, QString> tipo = {{int(Tipo::Retangulo), "rectangle"}, {int(Tipo::Arredondado), "round-rectangle"},
		                                         {int(Tipo::Elipse), "ellipse"}, {int(Tipo::Triangulo), "isosceles-triangle"},
		                                         {int(Tipo::Seta), "right-arrow"}};
		const QString gr = estilo("gr", "graphic", propsGraficas(o), "standard");
		QString s = "<draw:custom-shape draw:style-name=\"" + gr + "\"" + camada + posicao(o) + ">";
		s += o.temTexto() ? texto(o) : QString();
		QString espelho;
		if (o.viraH) espelho += " draw:mirror-horizontal=\"true\"";
		if (o.viraV) espelho += " draw:mirror-vertical=\"true\"";
		s += "<draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" draw:type=\"" + tipo.value(int(o.tipo), "rectangle") + "\"" + espelho + "/>";
		return s + "</draw:custom-shape>";
	}

	// --- o gráfico (um objeto do LibreOffice) ------------------------------

	QString graficoOdf(const Objeto &o) { return graficoOdfDoGrafico(o.graf, estiloDoTema(d_), o.r.size(), o.preench); }

	// --- a tabela ----------------------------------------------------------

	QString tabelaOdf(const Objeto &t0, const QString &camada)
	{
		Objeto t = t0;
		arrumarTabela(d_, t);
		const QColor fio = t.borda.isValid() && t.espBorda > 0 ? t.borda : d_.tema.apagado;
		const double esp = t.borda.isValid() && t.espBorda > 0 ? t.espBorda : 0.75;
		const QString gr = estilo("gr", "graphic", "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>", "standard");
		QString s = "<draw:frame draw:style-name=\"" + gr + "\"" + camada + posicao(t) + (t.nome.isEmpty() ? QString() : " draw:name=\"" + esc(t.nome) + "\"") + ">";
		s += QString("<table:table table:use-first-row-styles=\"%1\" table:use-banding-rows-styles=\"%2\">").arg(t.cabecalho ? "true" : "false", t.faixas ? "true" : "false");
		for (double w : t.largs)
			s += "<table:table-column table:style-name=\"" +
			     estilo("co", "table-column", "<style:table-column-properties style:column-width=\"" + cmDe(w) + "\" style:use-optimal-column-width=\"false\"/>") + "\"/>";
		for (int l = 0; l < t.lins; l++) {
			s += "<table:table-row table:style-name=\"" +
			     estilo("ro", "table-row", "<style:table-row-properties style:row-height=\"" + cmDe(t.celula(l, 0).r.height()) +
			                                   "\" style:use-optimal-row-height=\"false\"/>") +
			     "\">";
			for (int c = 0; c < t.cols; c++) {
				const Objeto &x = t.celula(l, c);
				const QString fill = x.fundoCel.isValid() ? "draw:fill=\"solid\" draw:fill-color=\"" + x.fundoCel.name() + "\"" : QString("draw:fill=\"none\"");
				const QString ce = estilo("ce", "table-cell",
				                          "<loext:graphic-properties " + fill + " draw:textarea-vertical-align=\"" +
				                              (x.vAlin == 0 ? "top" : x.vAlin == 2 ? "bottom" : "middle") +
				                              "\" fo:padding-top=\"0.127cm\" fo:padding-bottom=\"0.127cm\" fo:padding-left=\"0.254cm\" fo:padding-right=\"0.254cm\"/>"
				                              "<style:paragraph-properties fo:border=\"" + QString::number(esp, 'g', 4) + "pt solid " + fio.name() + "\"/>");
				s += "<table:table-cell table:style-name=\"" + ce + "\">" + texto(x) + "</table:table-cell>";
			}
			s += "</table:table-row>";
		}
		return s + "</table:table></draw:frame>";
	}

	// --- as animações (anim:, como o Impress) ------------------------------

	static QString segundos(double s) { return QString::number(std::max(0.0, s), 'g', 6) + "s"; }

	QString animacoesOdf(const Slide &sl, const std::vector<QString> &ids, const std::vector<std::vector<QString>> &pids) const
	{
		const auto grupos = sequencia(sl);
		QString seq;
		auto alvoDe = [&](const Evento &e) -> QString {
			if (e.par >= 0 && size_t(e.obj) < pids.size() && size_t(e.par) < pids[size_t(e.obj)].size() && !pids[size_t(e.obj)][size_t(e.par)].isEmpty())
				return pids[size_t(e.obj)][size_t(e.par)];
			return size_t(e.obj) < ids.size() ? ids[size_t(e.obj)] : QString();
		};
		// O vídeo que começa sozinho.
		QString play;
		for (size_t k = 0; k < sl.objs.size(); k++)
			if (sl.objs[k].tipo == Tipo::Video && sl.objs[k].sozinho && !ids[k].isEmpty())
				play += "<anim:par smil:begin=\"0s\" smil:fill=\"hold\" presentation:node-type=\"with-previous\" presentation:preset-class=\"media-call\" "
				        "presentation:preset-id=\"ooo-media-start\"><anim:command anim:command=\"play\" smil:targetElement=\"" + ids[k] + "\"/></anim:par>";
		for (size_t gi = 0; gi < grupos.size(); gi++) {
			const auto &g = grupos[gi];
			if (g.empty() && !(gi == 0 && !play.isEmpty()))
				continue;
			seq += QString("<anim:par smil:begin=\"%1\" smil:fill=\"hold\">").arg(gi == 0 ? "0s" : "next");
			if (gi == 0 && !play.isEmpty())
				seq += "<anim:par smil:begin=\"0s\" smil:fill=\"hold\">" + play + "</anim:par>";
			std::vector<double> ts;
			for (const Evento &e : g)
				if (std::find(ts.begin(), ts.end(), e.t0) == ts.end())
					ts.push_back(e.t0);
			bool primeiro = true;
			for (double t0 : ts) {
				seq += "<anim:par smil:begin=\"" + segundos(t0) + "\" smil:fill=\"hold\">";
				for (const Evento &e : g) {
					if (e.t0 != t0)
						continue;
					const QString alvo = alvoDe(e);
					if (alvo.isEmpty())
						continue;
					const QString nodo = primeiro && gi > 0 ? "on-click" : e.a.inicio == 2 ? "after-previous" : "with-previous";
					primeiro = false;
					seq += efeitoOdf(e, alvo, nodo);
				}
				seq += "</anim:par>";
			}
			seq += "</anim:par>";
		}
		if (seq.isEmpty())
			return {};
		return "<anim:par presentation:node-type=\"timing-root\"><anim:seq presentation:node-type=\"main-sequence\">" + seq + "</anim:seq></anim:par>";
	}

	static QString efeitoOdf(const Evento &e, const QString &alvo, const QString &nodo)
	{
		const Animacao &a = e.a;
		const bool sai = e.saida;
		const int dd = a.dir < 0 ? 0 : a.dir > 3 ? 3 : a.dir;
		static const char *subs[] = {"from-bottom", "from-left", "from-right", "from-top"};
		QString preset, sub;
		switch (a.ef) {
		case Efeito::Aparecer: preset = sai ? "ooo-exit-disappear" : "ooo-entrance-appear"; break;
		case Efeito::Dissolver: preset = sai ? "ooo-exit-fade-out" : "ooo-entrance-fade-in"; break;
		case Efeito::Voar: preset = sai ? "ooo-exit-fly-out" : "ooo-entrance-fly-in"; sub = subs[dd]; break;
		case Efeito::Ampliar: preset = sai ? "ooo-exit-fade-out-and-zoom" : "ooo-entrance-fade-in-and-zoom"; break;
		case Efeito::Revelar: preset = sai ? "ooo-exit-wipe" : "ooo-entrance-wipe"; sub = subs[dd]; break;
		default: break;
		}
		const QString dur = segundos(a.dur);
		const QString T = " smil:targetElement=\"" + alvo + "\"";
		QString s = "<anim:par smil:begin=\"0s\" smil:fill=\"hold\" presentation:node-type=\"" + nodo + "\" presentation:preset-class=\"" +
		            (sai ? "exit" : "entrance") + "\" presentation:preset-id=\"" + preset + "\"" +
		            (sub.isEmpty() ? QString() : " presentation:preset-sub-type=\"" + sub + "\"") + ">";
		if (!sai)
			s += "<anim:set smil:begin=\"0s\" smil:dur=\"0.001s\" smil:fill=\"hold\"" + T + " smil:attributeName=\"visibility\" smil:to=\"visible\"/>";
		const QString modo = sai ? " smil:mode=\"out\"" : QString();
		if (a.ef == Efeito::Dissolver)
			s += "<anim:transitionFilter smil:dur=\"" + dur + "\"" + T + " smil:type=\"fade\" smil:subtype=\"crossfade\"" + modo + "/>";
		else if (a.ef == Efeito::Voar) {
			const QString fx = dd == 1 ? "0-width/2" : dd == 2 ? "1+width/2" : "x";
			const QString fy = dd == 0 ? "1+height/2" : dd == 3 ? "0-height/2" : "y";
			s += "<anim:animate smil:dur=\"" + dur + "\" smil:fill=\"hold\"" + T + " smil:attributeName=\"x\" smil:values=\"" + (sai ? "x;" + fx : fx + ";x") +
			     "\" smil:keyTimes=\"0;1\"/>";
			s += "<anim:animate smil:dur=\"" + dur + "\" smil:fill=\"hold\"" + T + " smil:attributeName=\"y\" smil:values=\"" + (sai ? "y;" + fy : fy + ";y") +
			     "\" smil:keyTimes=\"0;1\"/>";
		} else if (a.ef == Efeito::Ampliar) {
			s += "<anim:transitionFilter smil:dur=\"" + dur + "\"" + T + " smil:type=\"fade\" smil:subtype=\"crossfade\"" + modo + "/>";
			s += "<anim:animate smil:dur=\"" + dur + "\" smil:fill=\"hold\"" + T + " smil:attributeName=\"width\" smil:values=\"" +
			     (sai ? "width;0" : "0;width") + "\" smil:keyTimes=\"0;1\"/>";
			s += "<anim:animate smil:dur=\"" + dur + "\" smil:fill=\"hold\"" + T + " smil:attributeName=\"height\" smil:values=\"" +
			     (sai ? "height;0" : "0;height") + "\" smil:keyTimes=\"0;1\"/>";
		} else if (a.ef == Efeito::Revelar) {
			const bool horiz = dd == 1 || dd == 2;
			const bool rev = dd == 0 || dd == 2;
			s += "<anim:transitionFilter smil:dur=\"" + dur + "\"" + T + " smil:type=\"barWipe\" smil:subtype=\"" + (horiz ? "leftToRight" : "topToBottom") + "\"" +
			     (rev ? " smil:direction=\"reverse\"" : "") + modo + "/>";
		}
		if (sai)
			s += "<anim:set smil:begin=\"" + (a.ef == Efeito::Aparecer ? QString("0s") : segundos(a.dur - 0.001)) + "\" smil:dur=\"0.001s\" smil:fill=\"hold\"" + T +
			     " smil:attributeName=\"visibility\" smil:to=\"hidden\"/>";
		return s + "</anim:par>";
	}

	QString pagina(int i)
	{
		const Slide &sl = d_.slides[size_t(i)];
		QString dp = "<style:drawing-page-properties presentation:background-visible=\"true\" presentation:background-objects-visible=\"true\"";
		if (!sl.fundoMidia.isEmpty() && d_.midias.contains(sl.fundoMidia)) {
			const QString nome = nomeMidia(d_.midias.value(sl.fundoMidia), sl.fundoMidia);
			QString img = "Fundo_" + QFileInfo(nome).completeBaseName();
			if (!fundos_.contains(img))
				fundos_.insert(img, nome);
			dp += " draw:fill=\"bitmap\" draw:fill-image-name=\"" + img + "\" style:repeat=\"stretch\"";
		} else if (sl.fundo.isValid())
			dp += " draw:fill=\"solid\" draw:fill-color=\"" + corOdf(sl.fundo) + "\"";
		if (sl.pular)
			dp += " presentation:visibility=\"hidden\"";
		if (sl.transicao != Transicao::Nenhuma) {
			dp += QString(" presentation:transition-speed=\"%1\"").arg(sl.duracao < 0.6 ? "fast" : sl.duracao < 1.2 ? "medium" : "slow");
			switch (sl.transicao) {
			case Transicao::Dissolver: dp += " smil:type=\"fade\" smil:subtype=\"crossfade\""; break;
			case Transicao::PeloPreto: dp += " smil:type=\"fade\" smil:subtype=\"fadeOverColor\" smil:fadeColor=\"#000000\""; break;
			case Transicao::Empurrar: dp += " smil:type=\"pushWipe\" smil:subtype=\"fromRight\""; break;
			case Transicao::Cobrir: dp += " smil:type=\"slideWipe\" smil:subtype=\"fromRight\""; break;
			case Transicao::Descobrir: dp += " smil:type=\"slideWipe\" smil:subtype=\"fromRight\" smil:direction=\"reverse\""; break;
			case Transicao::Ampliar: dp += " smil:type=\"irisWipe\" smil:subtype=\"rectangle\" smil:direction=\"reverse\""; break;
			default: break;
			}
		}
		dp += "/>";
		const QString estiloPagina = estilo("dp", "drawing-page", dp);
		QString s = QString("<draw:page draw:name=\"page%1\" draw:style-name=\"%2\" draw:master-page-name=\"Lume\">").arg(i + 1).arg(estiloPagina);
		// Os nomes (xml:id) de quem tem animação: o objeto ou cada parágrafo.
		std::vector<QString> ids(sl.objs.size());
		std::vector<std::vector<QString>> pids(sl.objs.size());
		for (size_t k = 0; k < sl.objs.size(); k++) {
			const Objeto &o = sl.objs[k];
			if (!o.entrada.tem() && !o.saida.tem() && !(o.tipo == Tipo::Video && o.sozinho))
				continue;
			ids[k] = QString("id%1").arg(++ids_);
			if (o.entrada.porParagrafo && o.aceitaTexto() && o.temTexto())
				for (const Paragrafo &p : o.pars)
					pids[k].push_back(p.texto().trimmed().isEmpty() ? QString() : QString("id%1").arg(++ids_));
		}
		for (size_t k = 0; k < sl.objs.size(); k++) {
			parIds_ = QStringList(pids[k].begin(), pids[k].end());
			parPos_ = 0;
			QString x = objeto(sl.objs[k]);
			parIds_.clear();
			if (!ids[k].isEmpty() && !x.isEmpty()) {
				const int sp = int(x.indexOf(' '));
				if (sp > 0)
					x.insert(sp, " draw:id=\"" + ids[k] + "\" xml:id=\"" + ids[k] + "\"");
			}
			s += x;
		}
		s += animacoesOdf(sl, ids, pids);
		if (!sl.notas.trimmed().isEmpty()) {
			const QString pr = estilo("pr", "presentation", "<style:graphic-properties draw:fill=\"none\" draw:stroke=\"none\"/>", "Lume-notes");
			s += QString("<presentation:notes><draw:page-thumbnail draw:layer=\"layout\" svg:width=\"14.8cm\" svg:height=\"%1\" svg:x=\"3.1cm\" svg:y=\"2.3cm\" "
			             "draw:page-number=\"%2\" presentation:class=\"page\"/>")
			         .arg(cmDe(14.8 / 2.54 * 72 * d_.alt / d_.larg))
			         .arg(i + 1);
			s += "<draw:frame presentation:style-name=\"" + pr + "\" draw:layer=\"layout\" svg:width=\"16.8cm\" svg:height=\"13.3cm\" svg:x=\"2.1cm\" svg:y=\"11.7cm\" "
			     "presentation:class=\"notes\" presentation:placeholder=\"false\"><draw:text-box>";
			for (const QString &l : sl.notas.split('\n'))
				s += "<text:p>" + esc(l) + "</text:p>";
			s += "</draw:text-box></draw:frame></presentation:notes>";
		}
		return s + "</draw:page>";
	}

	QString estilos() const
	{
		const Tema &t = d_.tema;
		auto txt = [&](double sz, const QColor &c, const QString &f, bool b) {
			return QString("<style:text-properties fo:font-size=\"%1pt\" fo:font-family=\"%2\" fo:color=\"%3\" fo:font-weight=\"%4\"/>")
			    .arg(sz)
			    .arg(esc(familiaCss(f)), c.name(), b ? "bold" : "normal");
		};
		QString s = CAB "<office:document-styles " NS " office:version=\"1.3\"><office:styles>";
		for (auto it = fundos_.cbegin(); it != fundos_.cend(); ++it)
			s += "<draw:fill-image draw:name=\"" + esc(it.key()) + "\" xlink:href=\"Pictures/" + esc(it.value()) +
			     "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>";
		s += "<style:default-style style:family=\"graphic\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>" +
		     txt(18, t.texto, t.fonteTexto, false) + "</style:default-style>";
		s += "<style:style style:name=\"standard\" style:family=\"graphic\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\" "
		     "fo:padding-top=\"0.127cm\" fo:padding-bottom=\"0.127cm\" fo:padding-left=\"0.254cm\" fo:padding-right=\"0.254cm\"/>" +
		     txt(18, t.texto, t.fonteTexto, false) + "</style:style>";
		s += "</office:styles><office:automatic-styles>";
		s += "<style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:margin-top=\"0cm\" fo:margin-bottom=\"0cm\" fo:margin-left=\"0cm\" "
		     "fo:margin-right=\"0cm\" fo:page-width=\"" + cmDe(d_.larg) + "\" fo:page-height=\"" + cmDe(d_.alt) +
		     "\" style:print-orientation=\"landscape\"/></style:page-layout>";
		s += "<style:style style:name=\"Mdp1\" style:family=\"drawing-page\"><style:drawing-page-properties draw:background-size=\"border\" draw:fill=\"solid\" "
		     "draw:fill-color=\"" + t.fundo.name() + "\"/></style:style>";
		s += "</office:automatic-styles><office:master-styles><draw:layer-set><draw:layer draw:name=\"layout\"/><draw:layer draw:name=\"background\"/>"
		     "<draw:layer draw:name=\"backgroundobjects\"/><draw:layer draw:name=\"controls\"/><draw:layer draw:name=\"measurelines\"/></draw:layer-set>";
		s += "<style:master-page style:name=\"Lume\" style:page-layout-name=\"PM1\" draw:style-name=\"Mdp1\"/>";
		s += "</office:master-styles></office:document-styles>";
		// Os estilos de apresentação do mestre (o título, o subtítulo, os
		// tópicos e as notas) vão em office:styles, antes do fecho.
		QString pres;
		pres += "<style:style style:name=\"Lume-title\" style:family=\"presentation\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\" "
		        "draw:textarea-vertical-align=\"bottom\"/><style:paragraph-properties fo:text-align=\"start\"/>" +
		        txt(38, t.titulo, t.fonteTitulo, t.tituloNegrito) + "</style:style>";
		pres += "<style:style style:name=\"Lume-subtitle\" style:family=\"presentation\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>"
		        "<style:paragraph-properties fo:text-align=\"center\"/>" + txt(24, t.texto, t.fonteTexto, false) + "</style:style>";
		pres += "<style:style style:name=\"Lume-outline1\" style:family=\"presentation\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>" +
		        txt(24, t.texto, t.fonteTexto, false) + "</style:style>";
		for (int n = 2; n <= 9; n++)
			pres += QString("<style:style style:name=\"Lume-outline%1\" style:family=\"presentation\" style:parent-style-name=\"Lume-outline%2\">"
			                "<style:text-properties fo:font-size=\"%3pt\"/></style:style>")
			            .arg(n)
			            .arg(n - 1)
			            .arg(std::max(16, 24 - (n - 1) * 2));
		pres += "<style:style style:name=\"Lume-notes\" style:family=\"presentation\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>" +
		        txt(12, QColor(0x1c, 0x1b, 0x19), t.fonteTexto, false) + "</style:style>";
		pres += "<style:style style:name=\"Lume-background\" style:family=\"presentation\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"solid\" "
		        "draw:fill-color=\"" + t.fundo.name() + "\"/></style:style>";
		pres += "<style:style style:name=\"Lume-backgroundobjects\" style:family=\"presentation\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/></style:style>";
		s.replace("</office:styles>", pres + "</office:styles>");
		return s;
	}
#undef CAB
#undef NS

	const Doc &d_;
	QString autos_;
	QHash<QString, QString> nomes_;
	QHash<QString, int> contador_;
	QHash<QString, QString> listas_;
	QMap<QString, QByteArray> midias_;
	QMap<QString, QString> fundos_;
	std::vector<std::tuple<QString, QByteArray, QString>> extras_;
	int objetos_ = 0, ids_ = 0;
	QStringList parIds_;
	int parPos_ = 0;
};

// --- ler -------------------------------------------------------------------

class LeitorOdp
{
public:
	bool ler(const QByteArray &dados, Doc &d, QString *erro)
	{
		if (!z_.abrir(dados) || !z_.tem("content.xml")) {
			*erro = "não é um .odp (falta content.xml)";
			return false;
		}
		const QString tipo = QString::fromUtf8(z_.ler("mimetype")).trimmed();
		if (!tipo.isEmpty() && !tipo.contains("presentation")) {
			*erro = "este arquivo OpenDocument não é uma apresentação (" + tipo + ")";
			return false;
		}
		No content, styles, meta;
		if (!lerXml(z_.ler("content.xml"), content)) {
			*erro = "o content.xml está estragado";
			return false;
		}
		lerXml(z_.ler("styles.xml"), styles);
		d = Doc();
		d.slides.clear();
		d_ = &d;
		QString idTema;
		QStringList leiautes;
		QString nomeTema;
		QStringList coresTema;
		if (lerXml(z_.ler("meta.xml"), meta)) {
			if (const No *t = meta.achar("dc:title"))
				d.titulo = t->textoTodo();
			if (const No *t = meta.achar("dc:creator"))
				d.autor = t->textoTodo();
			if (const No *m = meta.achar("office:meta"))
				for (const No *u : m->todos("meta:user-defined")) {
					if (u->a("meta:name") == "lume-tema")
						idTema = u->textoTodo().trimmed();
					else if (u->a("meta:name") == "lume-leiautes")
						leiautes = u->textoTodo().trimmed().split(',');
					else if (u->a("meta:name") == "lume-tema-nome")
						nomeTema = u->textoTodo().trimmed();
					else if (u->a("meta:name") == "lume-tema-cores")
						coresTema = u->textoTodo().trimmed().split(',');
				}
		}
		// Os estilos: os comuns e automáticos dos dois arquivos.
		for (const No *raiz : {&styles, &content})
			if (const No *fd = raiz->achar("office:font-face-decls"))
				for (const No *f : fd->todos("style:font-face"))
					fontes_.insert(f->a("style:name"), f->a("svg:font-family", f->a("style:name")));
		for (const No *raiz : {&styles, &content})
			for (const char *sec : {"office:styles", "office:automatic-styles"})
				if (const No *s = raiz->achar(sec))
					for (const No &k : s->filhos) {
						if (k.nome == "style:style")
							estilos_[k.a("style:family") + "|" + k.a("style:name")] = &k;
						else if (k.nome == "style:default-style")
							padroes_[k.a("style:family")] = &k;
						else if (k.nome == "text:list-style")
							listas_[k.a("style:name")] = &k;
						else if (k.nome == "style:page-layout")
							paginas_[k.a("style:name")] = &k;
						else if (k.nome == "draw:fill-image")
							imagens_[k.a("draw:name")] = k.a("xlink:href");
						else if (k.nome == "draw:gradient")
							gradientes_[k.a("draw:name")] = &k;
					}
		if (const No *ms = styles.achar("office:master-styles"))
			for (const No *m : ms->todos("style:master-page"))
				mestres_[m->a("style:name")] = m;
		// O tema do Palco, se veio daqui.
		bool nosso = false;
		for (const Tema &t : temas())
			if (t.id == idTema) {
				d.tema = t;
				nosso = true;
			}
		const No *pres = content.achar("office:presentation");
		if (!pres) {
			*erro = "o arquivo não tem slides";
			return false;
		}
		bool primeiro = true;
		for (const No *pg : pres->todos("draw:page")) {
			const No *mp = mestres_.value(pg->a("draw:master-page-name"), mestres_.isEmpty() ? nullptr : mestres_.first());
			if (primeiro && mp) {
				primeiro = false;
				if (const No *pl = paginas_.value(mp->a("style:page-layout-name")))
					if (const No *pp = pl->filho("style:page-layout-properties")) {
						const double w = ptDe(pp->a("fo:page-width")), h = ptDe(pp->a("fo:page-height"));
						if (w > 10 && h > 10) {
							d.larg = w;
							d.alt = h;
						}
					}
				if (!nosso) {
					temaDoMestre(*mp, d);
					if (!nomeTema.isEmpty())
						d.tema.nome = nomeTema;
					if (coresTema.size() >= 2 && QColor(coresTema[0]).isValid()) {
						d.tema.destaque = QColor(coresTema[0]);
						d.tema.serie.clear();
						for (int k = 1; k < coresTema.size(); k++)
							if (QColor(coresTema[k]).isValid())
								d.tema.serie.push_back(QColor(coresTema[k]));
					}
				}
			}
			Slide sl;
			lerPagina(*pg, mp, sl);
			d.slides.push_back(std::move(sl));
		}
		// Os leiautes do Palco (se o número de slides ainda bate).
		if (leiautes.size() == int(d.slides.size()))
			for (size_t i = 0; i < d.slides.size(); i++) {
				bool ok = false;
				const int v = leiautes[int(i)].toInt(&ok);
				if (ok && v >= 0 && v <= int(Layout::Branco))
					d.slides[i].layout = Layout(v);
			}
		if (d.slides.empty())
			d.slides.push_back(novoSlide(d, Layout::Titulo));
		return true;
	}

private:
	// Um atributo de um estilo, subindo pelos pais e pelo estilo padrão da família.
	QString prop(const QString &familia, const QString &nome, const QString &elemento, const QString &atributo) const
	{
		QString n = nome;
		for (int guarda = 0; guarda < 20 && !n.isEmpty(); guarda++) {
			const No *s = estilos_.value(familia + "|" + n);
			if (!s)
				break;
			if (const No *e = s->filho(elemento))
				if (e->atr.contains(atributo))
					return e->a(atributo);
			n = s->a("style:parent-style-name");
		}
		if (const No *p = padroes_.value(familia))
			if (const No *e = p->filho(elemento))
				return e->a(atributo);
		return {};
	}

	// O mesmo, numa lista de estilos (o primeiro que disser ganha).
	struct Fonte {
		QString familia, nome;
	};
	QString prop(const std::vector<Fonte> &fs, const QString &elemento, const QString &atributo) const
	{
		for (const Fonte &f : fs) {
			if (f.nome.isEmpty())
				continue;
			const QString v = prop(f.familia, f.nome, elemento, atributo);
			if (!v.isEmpty())
				return v;
		}
		return {};
	}

	// A família da letra: em cada estilo da cadeia vale fo:font-family ou
	// style:font-name (que aponta para uma declaração de fonte).
	QString familia(const std::vector<Fonte> &fs) const
	{
		auto olha = [&](const No *e) -> QString {
			if (!e)
				return {};
			if (e->atr.contains("fo:font-family"))
				return limparFamilia(e->a("fo:font-family"));
			if (e->atr.contains("style:font-name"))
				return limparFamilia(fontes_.value(e->a("style:font-name"), e->a("style:font-name")));
			return {};
		};
		for (const Fonte &f : fs) {
			QString n = f.nome;
			for (int guarda = 0; guarda < 20 && !n.isEmpty(); guarda++) {
				const No *s = estilos_.value(f.familia + "|" + n);
				if (!s)
					break;
				const QString v = olha(s->filho("style:text-properties"));
				if (!v.isEmpty())
					return v;
				n = s->a("style:parent-style-name");
			}
		}
		for (const Fonte &f : fs)
			if (const No *p = padroes_.value(f.familia)) {
				const QString v = olha(p->filho("style:text-properties"));
				if (!v.isEmpty())
					return v;
			}
		return {};
	}

	static QString limparFamilia(QString f)
	{
		f = f.trimmed();
		if ((f.startsWith('\'') && f.endsWith('\'')) || (f.startsWith('"') && f.endsWith('"')))
			f = f.mid(1, f.size() - 2);
		return f.section(',', 0, 0).trimmed().remove('\'');
	}

	// A cor de um preenchimento (com draw:fill): nullopt = não diz.
	std::optional<QColor> fill(const std::vector<Fonte> &fs, const QString &elemento, QString *midia = nullptr) const
	{
		const QString f = prop(fs, elemento, "draw:fill");
		if (f.isEmpty())
			return std::nullopt;
		if (f == "none")
			return QColor();
		if (f == "solid") {
			QColor c(prop(fs, elemento, "draw:fill-color"));
			if (!c.isValid())
				c = QColor(0x72, 0x9f, 0xcf); // o azul padrão do Impress
			const QString op = prop(fs, elemento, "draw:opacity");
			if (op.endsWith('%'))
				c.setAlphaF(float(op.chopped(1).toDouble() / 100));
			return c;
		}
		if (f == "gradient") {
			if (const No *g = gradientes_.value(prop(fs, elemento, "draw:fill-gradient-name")))
				return QColor(g->a("draw:start-color"));
			return std::nullopt;
		}
		if (f == "bitmap" && midia) {
			const QString href = imagens_.value(prop(fs, elemento, "draw:fill-image-name"));
			*midia = guardar(href);
			return QColor();
		}
		return std::nullopt;
	}

	QString guardar(const QString &href) const
	{
		QString h = href;
		if (h.startsWith("./"))
			h = h.mid(2);
		if (h.isEmpty() || !z_.tem(h))
			return {};
		const QByteArray b = z_.ler(h);
		return b.isEmpty() ? QString() : d_->guardarMidia(b, QFileInfo(h).suffix());
	}

	void temaDoMestre(const No &mp, Doc &d)
	{
		Tema t;
		t.id = "arquivo";
		t.nome = "Do arquivo";
		const std::vector<Fonte> pagina{{"drawing-page", mp.a("draw:style-name")}};
		const auto f = fill(pagina, "style:drawing-page-properties");
		t.fundo = f && f->isValid() ? *f : QColor(Qt::white);
		const QString m = mp.a("style:name");
		auto cor = [&](const QString &estilo, const QColor &padrao) {
			const QColor c(prop("presentation", m + "-" + estilo, "style:text-properties", "fo:color"));
			return c.isValid() ? c : padrao;
		};
		t.texto = cor("outline1", QColor(Qt::black));
		t.titulo = cor("title", t.texto);
		t.apagado = QColor((t.texto.red() + t.fundo.red()) / 2, (t.texto.green() + t.fundo.green()) / 2, (t.texto.blue() + t.fundo.blue()) / 2);
		t.destaque = QColor(0x72, 0x9f, 0xcf);
		t.serie = {QColor(0x00, 0x45, 0x86), QColor(0xff, 0x42, 0x0e), QColor(0xff, 0xd3, 0x20), QColor(0x57, 0x9d, 0x1c), QColor(0x7e, 0x00, 0x21)};
		t.fonteTitulo = familia({{"presentation", m + "-title"}});
		t.fonteTexto = familia({{"presentation", m + "-outline1"}});
		if (t.fonteTitulo.isEmpty()) t.fonteTitulo = "IBM Plex Sans";
		if (t.fonteTexto.isEmpty()) t.fonteTexto = "IBM Plex Sans";
		d.tema = t;
	}

	// --- texto ---

	struct Ctx {
		std::vector<Fonte> base;  // os estilos do objeto (graphic ou presentation)
		QString classe, mestre;
		Objeto *o = nullptr;
		double primeiro = -1;
		bool centroPadrao = false;
	};

	Trecho trecho(const QString &txt, const std::vector<Fonte> &fs) const
	{
		Trecho t;
		t.texto = txt;
		const QString sz = prop(fs, "style:text-properties", "fo:font-size");
		t.tamanho = sz.isEmpty() || sz.endsWith('%') ? 18 : ptDe(sz);
		t.negrito = prop(fs, "style:text-properties", "fo:font-weight") == "bold" || prop(fs, "style:text-properties", "fo:font-weight").toInt() >= 600;
		t.italico = prop(fs, "style:text-properties", "fo:font-style") == "italic";
		const QString u = prop(fs, "style:text-properties", "style:text-underline-style");
		t.sublinhado = !u.isEmpty() && u != "none";
		const QColor c(prop(fs, "style:text-properties", "fo:color"));
		if (c.isValid())
			t.cor = c;
		t.fonte = familia(fs);
		return t;
	}

	void spans(const No &n, const std::vector<Fonte> &fs, Paragrafo &p) const
	{
		for (const No &k : n.filhos) {
			if (k.nome.isEmpty()) {
				if (!k.texto.isEmpty())
					p.trechos.push_back(trecho(k.texto, fs));
			} else if (k.nome == "text:span" || k.nome == "text:a") {
				std::vector<Fonte> f2{{"text", k.a("text:style-name")}};
				f2.insert(f2.end(), fs.begin(), fs.end());
				spans(k, f2, p);
			} else if (k.nome == "text:s")
				p.trechos.push_back(trecho(QString(std::max(1, k.a("text:c", "1").toInt()), ' '), fs));
			else if (k.nome == "text:tab")
				p.trechos.push_back(trecho("\t", fs));
			else if (k.nome == "text:line-break")
				p.trechos.push_back(trecho(QString(QChar(0x2028)), fs));
			else if (!k.filhos.empty() && k.nome != "text:note")
				spans(k, fs, p);
		}
	}

	void paragrafo(const No &tp, Ctx &c, int nivel, int lista, const QString &estiloLista)
	{
		Paragrafo p;
		p.nivel = std::max(0, nivel);
		std::vector<Fonte> fs{{"paragraph", tp.a("text:style-name")}};
		// Os tópicos de nível n usam o estilo "outline(n+1)" do mestre.
		if (c.classe == "outline" && nivel > 0)
			fs.push_back({"presentation", c.mestre + QString("-outline%1").arg(nivel + 1)});
		fs.insert(fs.end(), c.base.begin(), c.base.end());
		const QString al = prop(fs, "style:paragraph-properties", "fo:text-align");
		p.alin = al == "center" ? 1 : (al == "end" || al == "right") ? 2 : al == "justify" ? 3 : 0;
		// Numa forma, o texto sem alinhamento dito fica no meio.
		if (al.isEmpty() && c.centroPadrao)
			p.alin = 1;
		if (lista >= 0) {
			p.marcador = lista == 1;
			p.numerado = lista == 2;
			Q_UNUSED(estiloLista);
		} else {
			const double ml = ptDe(prop(fs, "style:paragraph-properties", "fo:margin-left"));
			if (ml > 1)
				p.nivel = std::clamp(int(std::lround(ml / std::max(1.0, c.o->tamanho * 1.15))), 1, 4);
		}
		const QString lh = prop(fs, "style:paragraph-properties", "fo:line-height");
		if (lh.endsWith('%'))
			c.o->espaco = std::clamp(lh.chopped(1).toDouble() / 100, 0.5, 3.0);
		spans(tp, fs, p);
		if (p.trechos.empty()) {
			Trecho t = trecho(QString(), fs);
			p.trechos.push_back(t);
		}
		if (c.primeiro < 0)
			c.primeiro = p.trechos.front().tamanho;
		const QString pid = tp.a("xml:id", tp.a("text:id"));
		if (!pid.isEmpty())
			parPend_.push_back({pid, int(c.o->pars.size())});
		c.o->pars.push_back(p);
	}

	// O tipo de cada nível de uma lista: 1 marcador, 2 número, 0 nenhum.
	int tipoNivel(const QString &estilo, int nivel) const
	{
		const No *ls = listas_.value(estilo);
		if (!ls)
			return 1;
		for (const No &k : ls->filhos)
			if (k.a("text:level").toInt() == nivel + 1) {
				if (k.nome == "text:list-level-style-number")
					return k.a("style:num-format").isEmpty() ? 0 : 2;
				if (k.nome == "text:list-level-style-bullet")
					return k.a("text:bullet-char").trimmed().isEmpty() ? 0 : 1;
				return 0;
			}
		return 1;
	}

	void blocos(const No &n, Ctx &c, int nivel, const QString &estiloLista)
	{
		for (const No &k : n.filhos) {
			if (k.nome == "text:p" || k.nome == "text:h")
				paragrafo(k, c, nivel < 0 ? 0 : nivel, nivel < 0 ? -1 : tipoNivel(estiloLista, nivel), estiloLista);
			else if (k.nome == "text:list") {
				const QString el = k.a("text:style-name", estiloLista);
				for (const No &it : k.filhos)
					if (it.nome == "text:list-item" || it.nome == "text:list-header") {
						// Um item com outra lista dentro desce um nível.
						Ctx &cc = c;
						for (const No &x : it.filhos) {
							if (x.nome == "text:p" || x.nome == "text:h")
								paragrafo(x, cc, nivel + 1, it.nome == "text:list-header" ? 0 : tipoNivel(el, nivel + 1), el);
							else if (x.nome == "text:list") {
								No embrulho;
								embrulho.nome = "#";
								embrulho.filhos.push_back(x);
								blocos(embrulho, cc, nivel + 1, el);
							}
						}
					}
			}
		}
	}

	void lerTexto(const No &n, Objeto &o, const std::vector<Fonte> &base, const QString &classe, const QString &mestre, bool centro = false)
	{
		Ctx c{base, classe, mestre, &o, -1, centro};
		o.pars.clear();
		blocos(n, c, -1, QString());
		for (Paragrafo &p : o.pars)
			p.nivel = std::clamp(p.nivel, 0, 4);
		o.tamanho = c.primeiro > 0 ? c.primeiro : 18;
		bool algum = false;
		for (const Paragrafo &p : o.pars)
			algum = algum || !p.texto().trimmed().isEmpty();
		if (!algum) {
			Paragrafo p;
			if (!o.pars.empty())
				p = o.pars.front();
			p.trechos.clear();
			o.pars = {p};
		}
	}

	// --- objetos ---

	static void lerTransform(const QString &t, double &giro, QPointF &desloc, bool &tem)
	{
		static const QRegularExpression rr("rotate\\s*\\(\\s*([-0-9.eE]+)\\s*\\)");
		static const QRegularExpression rt("translate\\s*\\(\\s*([-0-9.a-z]+)[\\s,]+([-0-9.a-z]+)\\s*\\)");
		tem = false;
		const auto m1 = rr.match(t);
		if (m1.hasMatch()) {
			giro = m1.captured(1).toDouble();
			tem = true;
		}
		const auto m2 = rt.match(t);
		if (m2.hasMatch())
			desloc = QPointF(ptDe(m2.captured(1)), ptDe(m2.captured(2)));
	}

	bool posicao(const No &n, Objeto &o) const
	{
		const double w = ptDe(n.a("svg:width")), h = ptDe(n.a("svg:height"));
		o.r = QRectF(ptDe(n.a("svg:x")), ptDe(n.a("svg:y")), w, h);
		const QString tr = n.a("draw:transform");
		if (!tr.isEmpty()) {
			double a = 0;
			QPointF t;
			bool tem = false;
			lerTransform(tr, a, t, tem);
			if (tem) {
				// O centro depois de girar em torno da origem e deslocar.
				const double cx = t.x() + (w / 2 * std::cos(a) + h / 2 * std::sin(a));
				const double cy = t.y() + (-w / 2 * std::sin(a) + h / 2 * std::cos(a));
				o.r = QRectF(cx - w / 2, cy - h / 2, w, h);
				o.giro = -a * 180 / M_PI;
				while (o.giro < 0) o.giro += 360;
				while (o.giro >= 360) o.giro -= 360;
				if (std::fabs(o.giro) < 1e-6 || std::fabs(o.giro - 360) < 1e-6)
					o.giro = 0;
			} else
				o.r.moveTopLeft(t);
		}
		return w > 0 || h > 0;
	}

	std::vector<Fonte> estilosDe(const No &n) const
	{
		std::vector<Fonte> fs;
		if (n.atr.contains("presentation:style-name"))
			fs.push_back({"presentation", n.a("presentation:style-name")});
		if (n.atr.contains("draw:style-name"))
			fs.push_back({"graphic", n.a("draw:style-name")});
		if (n.atr.contains("draw:text-style-name"))
			fs.push_back({"paragraph", n.a("draw:text-style-name")});
		return fs;
	}

	void graficas(const std::vector<Fonte> &fs, Objeto &o) const
	{
		if (auto f = fill(fs, "style:graphic-properties"))
			o.preench = *f;
		const QString st = prop(fs, "style:graphic-properties", "draw:stroke");
		if (st == "solid" || st == "dash") {
			QColor c(prop(fs, "style:graphic-properties", "svg:stroke-color"));
			o.borda = c.isValid() ? c : QColor(0x34, 0x65, 0xa4);
			o.espBorda = std::max(0.75, ptDe(prop(fs, "style:graphic-properties", "svg:stroke-width")));
		}
		const QString va = prop(fs, "style:graphic-properties", "draw:textarea-vertical-align");
		o.vAlin = va == "middle" ? 1 : va == "bottom" ? 2 : va == "top" ? 0 : o.vAlin;
		o.encolher = prop(fs, "style:graphic-properties", "style:shrink-to-fit") == "true" ||
		             prop(fs, "style:graphic-properties", "draw:fit-to-size") == "shrink-to-fit";
		// A margem do texto.
		for (const auto &[lado, attr] : std::vector<std::pair<int, const char *>>{{0, "fo:padding-left"}, {1, "fo:padding-top"}, {2, "fo:padding-right"}, {3, "fo:padding-bottom"}}) {
			const QString v = prop(fs, "style:graphic-properties", attr);
			if (v.isEmpty())
				continue;
			const double pt = ptDe(v);
			if (lado == 0) o.margem.setLeft(pt);
			else if (lado == 1) o.margem.setTop(pt);
			else if (lado == 2) o.margem.setRight(pt);
			else o.margem.setBottom(pt);
		}
	}

	void objetos(const No &pai, std::vector<Objeto> &saida, const QString &mestre, bool soEnfeites, int &corpos)
	{
		for (const No &n : pai.filhos) {
			if (n.nome == "draw:g") {
				objetos(n, saida, mestre, soEnfeites, corpos);
				continue;
			}
			const QString classe = n.a("presentation:class");
			if (soEnfeites && !classe.isEmpty())
				continue;
			if (classe == "page" || classe == "notes" || classe == "header" || classe == "footer" || classe == "date-time" || classe == "page-number")
				continue;
			Objeto o;
			const std::vector<Fonte> fs = estilosDe(n);
			o.nome = n.a("draw:name");
			parPend_.clear();
			const QString idArq = n.a("xml:id", n.a("draw:id"));
			auto anota = [&] {
				if (!idArq.isEmpty())
					idObj_[idArq] = int(saida.size());
				for (const auto &[pid, k] : parPend_)
					idPar_[pid] = {int(saida.size()), k};
			};
			if (n.nome == "draw:frame") {
				if (!posicao(n, o))
					continue;
				if (const No *tb = n.filho("table:table")) {
					if (!tabela(*tb, o, mestre))
						continue;
					parPend_.clear();
					anota();
					saida.push_back(std::move(o));
					continue;
				}
				if (const No *pl = n.filho("draw:plugin")) {
					o.tipo = Tipo::Video;
					o.midia = guardar(pl->a("xlink:href"));
					if (o.midia.isEmpty())
						continue;
					for (const No *pa : pl->todos("draw:param")) {
						if (pa->a("draw:name") == "Loop") o.repetir = pa->a("draw:value") == "true";
						if (pa->a("draw:name") == "Mute") o.mudo = pa->a("draw:value") == "true";
					}
					if (const No *im = n.filho("draw:image"))
						o.capa = guardar(im->a("xlink:href"));
					graficas(fs, o);
					o.preench = QColor();
					// A borda só se o próprio estilo do quadro diz (a padrão do
					// Impress não aparece num vídeo).
					{
						const No *st = estilos_.value("graphic|" + n.a("draw:style-name"));
						const No *gp = st ? st->filho("style:graphic-properties") : nullptr;
						if (!gp || gp->a("draw:stroke") != "solid")
							o.borda = QColor();
					}
					anota();
					saida.push_back(std::move(o));
					continue;
				}
				if (classe == "title") o.papel = Papel::Titulo;
				else if (classe == "subtitle") o.papel = Papel::Subtitulo;
				else if (classe == "outline") o.papel = corpos++ == 0 ? Papel::Corpo : Papel::Corpo2;
				if (const No *tb = n.filho("draw:text-box")) {
					o.tipo = Tipo::Texto;
					graficas(fs, o);
					if (o.preench.isValid() || (o.borda.isValid() && o.espBorda > 0))
						o.tipo = Tipo::Retangulo;
					if (o.papel == Papel::Titulo && prop(fs, "style:graphic-properties", "draw:textarea-vertical-align").isEmpty())
						o.vAlin = 1;
					lerTexto(*tb, o, fs, classe, mestre);
					if (!o.temTexto() && o.papel == Papel::Livre)
						continue;
				} else if (const No *ob = n.filho("draw:object")) {
					if (!graficoEmbutido(ob->a("xlink:href"), o.graf))
						continue;
					o.tipo = Tipo::Grafico;
				} else if (const No *im = n.filho("draw:image")) {
					DadosGrafico g;
					const No *ds = n.filho("svg:desc");
					if (ds && lerDados(ds->textoTodo().trimmed(), g)) {
						o.tipo = Tipo::Grafico;
						o.graf = g;
					} else {
						o.tipo = Tipo::Imagem;
						o.midia = guardar(im->a("xlink:href"));
						if (o.midia.isEmpty())
							continue;
						graficas(fs, o);
						o.preench = QColor();
					}
				} else
					continue;
			} else if (n.nome == "draw:custom-shape" || n.nome == "draw:rect" || n.nome == "draw:ellipse" || n.nome == "draw:circle" ||
			           n.nome == "draw:polygon" || n.nome == "draw:path" || n.nome == "draw:regular-polygon") {
				if (!posicao(n, o))
					continue;
				o.tipo = n.nome == "draw:ellipse" || n.nome == "draw:circle" ? Tipo::Elipse : Tipo::Retangulo;
				bool retangulo = n.nome == "draw:rect";
				if (const No *eg = n.filho("draw:enhanced-geometry")) {
					const QString t = eg->a("draw:type");
					if (t == "ellipse" || t == "circle" || t == "ooxml-ellipse" || t == "flowchart-connector") o.tipo = Tipo::Elipse;
					else if (t == "round-rectangle" || t == "round-square" || t == "ooxml-roundRect" || t == "flowchart-alternate-process") o.tipo = Tipo::Arredondado;
					else if (t == "isosceles-triangle" || t == "ooxml-triangle") o.tipo = Tipo::Triangulo;
					else if (t == "right-arrow" || t == "ooxml-rightArrow") o.tipo = Tipo::Seta;
					else if (t == "ooxml-line" || t == "ooxml-straightConnector1") o.tipo = Tipo::Linha;
					retangulo = t == "rectangle" || t == "ooxml-rect" || t.isEmpty() || t == "non-primitive" || t == "mso-spt202";
					o.viraH = eg->a("draw:mirror-horizontal") == "true";
					o.viraV = eg->a("draw:mirror-vertical") == "true";
				}
				if (n.atr.contains("draw:corner-radius") && ptDe(n.a("draw:corner-radius")) > 0)
					o.tipo = Tipo::Arredondado;
				o.vAlin = 1;
				graficas(fs, o);
				if (o.tipo == Tipo::Linha) {
					if (!o.borda.isValid() || o.espBorda <= 0) {
						o.borda = d_->tema.texto;
						o.espBorda = 1;
					}
					saida.push_back(std::move(o));
					continue;
				}
				// Um retângulo sem cor nem borda é só uma caixa de texto.
				if (retangulo && o.tipo == Tipo::Retangulo && !o.preench.isValid() && !(o.borda.isValid() && o.espBorda > 0))
					o.tipo = Tipo::Texto;
				lerTexto(n, o, fs, QString(), mestre, o.tipo != Tipo::Texto);
				if (o.tipo == Tipo::Texto && !o.temTexto())
					continue;
			} else if (n.nome == "draw:line" || n.nome == "draw:connector") {
				const QPointF a(ptDe(n.a("svg:x1")), ptDe(n.a("svg:y1"))), b(ptDe(n.a("svg:x2")), ptDe(n.a("svg:y2")));
				o.tipo = Tipo::Linha;
				o.r = QRectF(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())), QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
				o.viraV = (b.y() < a.y()) != (b.x() < a.x());
				graficas(fs, o);
				if (!o.borda.isValid() || o.espBorda <= 0) {
					o.borda = d_->tema.texto;
					o.espBorda = 1;
				}
			} else
				continue;
			anota();
			saida.push_back(std::move(o));
		}
	}

	// Uma tabela do Impress: as colunas, as linhas, o fundo e o texto de cada célula.
	bool tabela(const No &tb, Objeto &o, const QString &mestre)
	{
		std::vector<double> largs;
		std::vector<QString> padraoCol;
		for (const No *c : tb.todos("table:table-column")) {
			const int rep = std::max(1, c->a("table:number-columns-repeated", "1").toInt());
			const double w = ptDe(prop("table-column", c->a("table:style-name"), "style:table-column-properties", "style:column-width"));
			for (int k = 0; k < rep && largs.size() < 200; k++) {
				largs.push_back(w);
				padraoCol.push_back(c->a("table:default-cell-style-name"));
			}
		}
		std::vector<const No *> linhas;
		std::function<void(const No &)> junta = [&](const No &x) {
			for (const No &k : x.filhos) {
				if (k.nome == "table:table-row")
					linhas.push_back(&k);
				else if (k.nome == "table:table-header-rows" || k.nome == "table:table-rows" || k.nome == "table:table-row-group")
					junta(k);
			}
		};
		junta(tb);
		if (linhas.empty() || largs.empty())
			return false;
		const QRectF r = o.r;
		const QString nome = o.nome;
		o = novaTabela(int(linhas.size()), int(largs.size()), r);
		o.nome = nome;
		double soma = 0;
		for (double w : largs) soma += w;
		for (size_t c = 0; c < largs.size(); c++)
			o.largs[c] = soma > 0 && largs[c] > 0 ? largs[c] : r.width() / largs.size();
		o.cabecalho = tb.a("table:use-first-row-styles") == "true";
		o.faixas = tb.a("table:use-banding-rows-styles") == "true";
		double tamanho = 0;
		for (int l = 0; l < o.lins; l++) {
			const No *lin = linhas[size_t(l)];
			const QString ro = lin->a("table:style-name");
			const double h = ptDe(prop("table-row", ro, "style:table-row-properties", "style:row-height"));
			const double hm = ptDe(prop("table-row", ro, "style:table-row-properties", "style:min-row-height"));
			o.alts[size_t(l)] = h > 0 ? h : hm > 0 ? hm : r.height() / o.lins;
			int c = 0;
			for (const No &cel : lin->filhos) {
				if (cel.nome != "table:table-cell" && cel.nome != "table:covered-table-cell")
					continue;
				const int rep = std::max(1, cel.a("table:number-columns-repeated", "1").toInt());
				for (int k = 0; k < rep && c < o.cols; k++, c++) {
					if (cel.nome == "table:covered-table-cell")
						continue;
					Objeto &x = o.celula(l, c);
					QString ce = cel.a("table:style-name");
					if (ce.isEmpty()) ce = lin->a("table:default-cell-style-name");
					if (ce.isEmpty() && size_t(c) < padraoCol.size()) ce = padraoCol[size_t(c)];
					const std::vector<Fonte> fs{{"table-cell", ce}};
					for (const char *el : {"loext:graphic-properties", "style:graphic-properties", "style:table-cell-properties"})
						if (auto f = fill(fs, el)) {
							x.preench = *f;
							break;
						}
					QString va;
					for (const char *el : {"loext:graphic-properties", "style:graphic-properties"})
						if (va.isEmpty())
							va = prop(fs, el, "draw:textarea-vertical-align");
					if (va.isEmpty())
						va = prop(fs, "style:table-cell-properties", "style:vertical-align");
					x.vAlin = va == "top" ? 0 : va == "bottom" ? 2 : 1;
					if (!o.borda.isValid()) {
						QString b;
						for (const char *el : {"style:paragraph-properties", "style:table-cell-properties"})
							for (const char *at : {"fo:border", "fo:border-top", "fo:border-left"})
								if (b.isEmpty() || b == "none")
									b = prop(fs, el, at);
						static const QRegularExpression rb("([0-9.]+[a-z]+)\\s+\\S+\\s+(#[0-9a-fA-F]{6})");
						const auto m = rb.match(b);
						if (m.hasMatch()) {
							o.borda = QColor(m.captured(2));
							o.espBorda = ptDe(m.captured(1));
						}
					}
					lerTexto(cel, x, fs, QString(), mestre);
					if (tamanho <= 0 && x.temTexto())
						tamanho = x.tamanho;
				}
			}
		}
		if (tamanho > 0)
			o.tamanho = tamanho;
		return true;
	}

	// As animações da página (a sequência principal do Impress).
	void animacoes(const No &pg, Slide &sl)
	{
		const No *seq = nullptr;
		std::function<void(const No &)> acha = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (seq)
					return;
				if (f.nome == "anim:seq" && f.a("presentation:node-type") == "main-sequence") {
					seq = &f;
					return;
				}
				if (f.nome.startsWith("anim:"))
					acha(f);
			}
		};
		acha(pg);
		if (!seq)
			return;
		auto segs = [](QString v) {
			v = v.trimmed();
			if (v.endsWith("ms")) return v.chopped(2).toDouble() / 1000;
			if (v.endsWith('s')) v.chop(1);
			return v.toDouble();
		};
		int ordem = 0;
		std::set<std::pair<int, bool>> feitos;
		std::function<void(const No &)> efeitos = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (f.nome != "anim:par" && f.nome != "anim:seq")
					continue;
				const QString classe = f.a("presentation:preset-class");
				if (classe.isEmpty()) {
					efeitos(f);
					continue;
				}
				// O alvo: o primeiro com smil:targetElement.
				QString alvo;
				std::function<void(const No &)> procura = [&](const No &x) {
					for (const No &k : x.filhos) {
						if (!alvo.isEmpty())
							return;
						if (k.atr.contains("smil:targetElement"))
							alvo = k.a("smil:targetElement");
						else
							procura(k);
					}
				};
				procura(f);
				int obj = -1;
				bool doPar = false;
				if (idObj_.contains(alvo))
					obj = idObj_.value(alvo);
				else if (idPar_.contains(alvo)) {
					obj = idPar_.value(alvo).first;
					doPar = true;
				}
				if (obj < 0 || obj >= int(sl.objs.size()))
					continue;
				const QString nodo = f.a("presentation:node-type");
				Objeto &o = sl.objs[size_t(obj)];
				if (classe == "media-call") {
					if (o.tipo == Tipo::Video && nodo != "on-click")
						o.sozinho = true;
					continue;
				}
				if (classe != "entrance" && classe != "exit")
					continue;
				const bool sai = classe == "exit";
				if (feitos.count({obj, sai}))
					continue;
				feitos.insert({obj, sai});
				Animacao a;
				const QString id = f.a("presentation:preset-id");
				if (id.contains("appear") || id.contains("disappear"))
					a.ef = Efeito::Aparecer;
				else if (id.contains("zoom") || id.contains("grow") || id.contains("expand"))
					a.ef = Efeito::Ampliar;
				else if (id.contains("fly") || id.contains("ascend") || id.contains("descend") || id.contains("rise"))
					a.ef = Efeito::Voar;
				else if (id.contains("wipe") || id.contains("wedge") || id.contains("split"))
					a.ef = Efeito::Revelar;
				else
					a.ef = Efeito::Dissolver;
				const QString sub = f.a("presentation:preset-sub-type");
				a.dir = sub == "from-left" ? 1 : sub == "from-right" ? 2 : sub == "from-top" ? 3 : 0;
				a.inicio = nodo == "with-previous" ? 1 : nodo == "after-previous" ? 2 : 0;
				a.dur = 0.5;
				std::function<bool(const No &)> dura = [&](const No &x) {
					for (const No &k : x.filhos) {
						if (k.nome != "anim:set" && k.atr.contains("smil:dur") && segs(k.a("smil:dur")) > 0.002) {
							a.dur = segs(k.a("smil:dur"));
							return true;
						}
						if (dura(k))
							return true;
					}
					return false;
				};
				dura(f);
				a.porParagrafo = doPar && !sai;
				a.ordem = ordem++;
				(sai ? o.saida : o.entrada) = a;
			}
		};
		efeitos(*seq);
	}

	// Um gráfico do LibreOffice: a tabela de dados dentro do objeto.
	bool graficoEmbutido(const QString &href, DadosGrafico &g) const
	{
		QString h = href;
		if (h.startsWith("./"))
			h = h.mid(2);
		No x;
		return lerXml(z_.ler(h + "/content.xml"), x) && lerGraficoDoOdf(x, g);
	}

	void lerPagina(const No &pg, const No *mp, Slide &sl)
	{
		const std::vector<Fonte> fp{{"drawing-page", pg.a("draw:style-name")}};
		const QString dp = "style:drawing-page-properties";
		QString fm;
		auto f = fill(fp, dp, &fm);
		if (!f && mp) {
			const std::vector<Fonte> fmst{{"drawing-page", mp->a("draw:style-name")}};
			f = fill(fmst, dp, &fm);
		}
		if (f && f->isValid() && *f != d_->tema.fundo)
			sl.fundo = *f;
		sl.fundoMidia = fm;
		sl.pular = prop(fp, dp, "presentation:visibility") == "hidden";
		const QString tipo = prop(fp, dp, "smil:type");
		if (!tipo.isEmpty() && tipo != "none") {
			const QString sub = prop(fp, dp, "smil:subtype"), dir = prop(fp, dp, "smil:direction");
			if (tipo == "fade") sl.transicao = sub == "fadeOverColor" || sub == "fadeToColor" ? Transicao::PeloPreto : Transicao::Dissolver;
			else if (tipo == "pushWipe") sl.transicao = Transicao::Empurrar;
			else if (tipo == "slideWipe") sl.transicao = dir == "reverse" ? Transicao::Descobrir : Transicao::Cobrir;
			else if (tipo == "irisWipe" || tipo == "zoom") sl.transicao = Transicao::Ampliar;
			else sl.transicao = Transicao::Dissolver;
			const QString v = prop(fp, dp, "presentation:transition-speed");
			sl.duracao = v == "slow" ? 1.5 : v == "fast" ? 0.5 : 1.0;
		}
		const QString mestre = mp ? mp->a("style:name") : QString();
		int corpos = 0;
		if (mp && pg.a("presentation:background-objects-visible") != "false" && prop(fp, dp, "presentation:background-objects-visible") != "false")
			objetos(*mp, sl.objs, mestre, true, corpos);
		corpos = 0;
		idObj_.clear();
		idPar_.clear();
		objetos(pg, sl.objs, mestre, false, corpos);
		animacoes(pg, sl);
		sl.layout = inferirLeiaute(sl);
		if (const No *nt = pg.filho("presentation:notes"))
			for (const No *fr : nt->todos("draw:frame"))
				if (fr->a("presentation:class") == "notes") {
					QStringList l;
					if (const No *tb = fr->filho("draw:text-box"))
						for (const No &p : tb->filhos)
							if (p.nome == "text:p" || p.nome == "text:list")
								l << p.textoTodo();
					while (!l.isEmpty() && l.last().trimmed().isEmpty())
						l.removeLast();
					sl.notas = l.join('\n');
				}
	}

	ZipLeitor z_;
	Doc *d_ = nullptr;
	QHash<QString, const No *> estilos_, padroes_, listas_, paginas_, gradientes_;
	QMap<QString, const No *> mestres_;
	QHash<QString, QString> imagens_, fontes_;
	// Os nomes (xml:id) dos objetos e dos parágrafos da página, para as animações.
	QHash<QString, int> idObj_;
	QHash<QString, std::pair<int, int>> idPar_;
	std::vector<std::pair<QString, int>> parPend_;
};

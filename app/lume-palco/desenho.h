// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: o desenho. Um slide se desenha do mesmo jeito no editor, na
// apresentação, nas miniaturas e no PDF: o texto pelo QTextDocument, as
// formas por caminhos, os gráficos à mão.
//
// O texto é montado dez vezes maior (em pixels inteiros) e desenhado com a
// escala de 1/10: assim o tamanho da letra não depende da resolução de quem
// desenha (tela, imagem ou PDF) e meio ponto ainda conta.

#pragma once

#include "palco.h"

#include <QAbstractTextDocumentLayout>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

constexpr double K = 10; // a escala do texto

// As folgas da caixa de texto, as mesmas do PowerPoint (0,1" e 0,05").
inline QRectF interior(const Objeto &o) { return o.r.adjusted(7.2, 3.6, -7.2, -3.6); }

inline QFont fonteK(const QString &familia, double pontos)
{
	QFont f(familia);
	f.setPixelSize(std::max(1, int(std::lround(pontos * K))));
	f.setHintingPreference(QFont::PreferNoHinting);
	f.setStyleStrategy(QFont::PreferAntialias);
	return f;
}

// O texto de um objeto como QTextDocument (na escala K). Com escala < 1, a
// letra diminui (para caber).
inline void montarDocumento(QTextDocument &td, const Doc &d, const Objeto &o, double escala = 1.0)
{
	td.clear();
	td.setDocumentMargin(0);
	td.setUseDesignMetrics(true);
	const double base = o.tamanho * escala;
	const QString familia = fonteDe(d, o);
	td.setDefaultFont(fonteK(familia, base));
	td.setIndentWidth(base * K * 1.15);
	td.setTextWidth(std::max(1.0, interior(o).width() * K));
	QTextCursor c(&td);
	QTextList *listas[5] = {};
	bool primeiro = true;
	for (const Paragrafo &p : o.pars) {
		QTextBlockFormat bf;
		bf.setAlignment(p.alin == 1 ? Qt::AlignHCenter : p.alin == 2 ? Qt::AlignRight : p.alin == 3 ? Qt::AlignJustify : Qt::AlignLeft);
		bf.setLineHeight(o.espaco * 100, QTextBlockFormat::ProportionalHeight);
		const bool corpo = o.papel == Papel::Corpo || o.papel == Papel::Corpo2 || p.marcador || p.numerado;
		bf.setTopMargin(primeiro ? 0 : (corpo ? base * K * 0.35 : 0));
		const double tamPar = p.trechos.empty() || p.trechos.front().tamanho <= 0 ? base : p.trechos.front().tamanho * escala;
		QTextCharFormat cf0;
		cf0.setFont(fonteK(familia, tamPar));
		cf0.setForeground(corTexto(d, o));
		cf0.setFontWeight(o.negrito || (o.papel == Papel::Titulo && d.tema.tituloNegrito) ? QFont::Bold : QFont::Normal);
		cf0.setFontItalic(o.italico);
		if (primeiro) {
			c.setBlockFormat(bf);
			c.setBlockCharFormat(cf0);
		} else
			c.insertBlock(bf, cf0);
		primeiro = false;
		const int nv = std::clamp(p.nivel, 0, 4);
		if (p.marcador || p.numerado) {
			QTextList *&l = listas[nv];
			const QTextListFormat::Style estilo = p.numerado ? QTextListFormat::ListDecimal
			                                      : nv == 0 ? QTextListFormat::ListDisc
			                                      : nv == 1 ? QTextListFormat::ListCircle : QTextListFormat::ListSquare;
			if (!l || l->format().style() != estilo) {
				QTextListFormat lf;
				lf.setStyle(estilo);
				lf.setIndent(nv + 1);
				if (p.numerado)
					lf.setNumberSuffix(".");
				l = c.createList(lf);
			} else
				l->add(c.block());
			for (int k = nv + 1; k < 5; k++)
				listas[k] = nullptr;
		} else {
			for (QTextList *&l : listas)
				l = nullptr;
			if (nv > 0) {
				QTextBlockFormat b2 = c.blockFormat();
				b2.setIndent(nv);
				c.setBlockFormat(b2);
			}
		}
		for (const Trecho &t : p.trechos) {
			QTextCharFormat cf = cf0;
			const double tam = t.tamanho > 0 ? t.tamanho * escala : base;
			cf.setFont(fonteK(t.fonte.isEmpty() ? familia : t.fonte, tam));
			cf.setFontWeight(t.negrito || o.negrito || (o.papel == Papel::Titulo && d.tema.tituloNegrito) ? QFont::Bold : QFont::Normal);
			cf.setFontItalic(t.italico || o.italico);
			cf.setFontUnderline(t.sublinhado);
			cf.setForeground(t.cor.isValid() ? t.cor : corTexto(d, o));
			c.insertText(t.texto, cf);
		}
	}
}

// A escala da letra para o texto caber na caixa (1 se já cabe).
inline double escalaQueCabe(const Doc &d, const Objeto &o)
{
	if (!o.encolher || !o.temTexto())
		return 1.0;
	const double alt = interior(o).height() * K;
	QTextDocument td;
	montarDocumento(td, d, o, 1.0);
	if (td.size().height() <= alt)
		return 1.0;
	double lo = 0.3, hi = 1.0;
	for (int i = 0; i < 7; i++) {
		const double m = (lo + hi) / 2;
		montarDocumento(td, d, o, m);
		(td.size().height() <= alt ? lo : hi) = m;
	}
	return lo;
}

inline void desenharTexto(QPainter &p, const Doc &d, const Objeto &o)
{
	if (!o.temTexto())
		return;
	QTextDocument td;
	montarDocumento(td, d, o, escalaQueCabe(d, o));
	const QRectF in = interior(o);
	const double h = td.size().height() / K;
	double y = in.top();
	if (o.vAlin == 1)
		y = in.top() + (in.height() - h) / 2;
	else if (o.vAlin == 2)
		y = in.bottom() - h;
	p.save();
	p.translate(in.left(), y);
	p.scale(1 / K, 1 / K);
	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.palette.setColor(QPalette::Text, corTexto(d, o));
	td.documentLayout()->draw(&p, ctx);
	p.restore();
}

// --- formas ----------------------------------------------------------------

inline QPainterPath caminho(Tipo t, const QRectF &r)
{
	QPainterPath c;
	switch (t) {
	case Tipo::Elipse:
		c.addEllipse(r);
		break;
	case Tipo::Arredondado: {
		const double raio = std::min(r.width(), r.height()) * 0.16;
		c.addRoundedRect(r, raio, raio);
		break;
	}
	case Tipo::Triangulo:
		c.moveTo(r.center().x(), r.top());
		c.lineTo(r.bottomRight());
		c.lineTo(r.bottomLeft());
		c.closeSubpath();
		break;
	case Tipo::Seta: {
		const double ponta = std::min(r.width() * 0.4, r.height());
		const double haste = r.height() * 0.25;
		c.moveTo(r.left(), r.top() + haste);
		c.lineTo(r.right() - ponta, r.top() + haste);
		c.lineTo(r.right() - ponta, r.top());
		c.lineTo(r.right(), r.center().y());
		c.lineTo(r.right() - ponta, r.bottom());
		c.lineTo(r.right() - ponta, r.bottom() - haste);
		c.lineTo(r.left(), r.bottom() - haste);
		c.closeSubpath();
		break;
	}
	case Tipo::Linha:
		c.moveTo(r.topLeft());
		c.lineTo(r.bottomRight());
		break;
	default:
		c.addRect(r);
	}
	return c;
}

inline QImage imagemDe(const Doc &d, const QString &chave)
{
	static QHash<size_t, QImage> cache;
	auto it = d.midias.constFind(chave);
	if (it == d.midias.cend())
		return {};
	const size_t h = qHash(*it);
	auto c = cache.constFind(h);
	if (c != cache.cend())
		return *c;
	QImage img;
	img.loadFromData(*it);
	if (cache.size() > 64)
		cache.clear();
	cache.insert(h, img);
	return img;
}

// --- gráficos --------------------------------------------------------------

inline QString numeroCurto(double v)
{
	const QLocale br(QLocale::Portuguese, QLocale::Brazil);
	const double a = std::fabs(v);
	if (a >= 1e9) return br.toString(v / 1e9, 'f', 1) + " bi";
	if (a >= 1e6) return br.toString(v / 1e6, 'f', 1) + " mi";
	if (a >= 1e4) return br.toString(v / 1e3, 'f', 0) + " mil";
	if (a == std::floor(a)) return br.toString(v, 'f', 0);
	return br.toString(v, 'f', a < 10 ? 1 : 0);
}

inline double passoBonito(double faixa, int marcas)
{
	const double bruto = faixa / std::max(1, marcas);
	const double e = std::pow(10.0, std::floor(std::log10(std::max(bruto, 1e-12))));
	for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
		if (m * e >= bruto)
			return m * e;
	return 10 * e;
}

inline void desenharGrafico(QPainter &p, const Doc &d, const Objeto &o)
{
	const DadosGrafico &g = o.graf;
	const QRectF r = o.r;
	const QColor tinta = d.tema.texto, apagado = d.tema.apagado;
	const QString fonte = d.tema.fonteTexto;
	const double letra = std::clamp(std::min(r.width(), r.height()) / 22, 9.0, 20.0);
	auto F = [&](double k) {
		// Em pixels do palco (pontos): o mesmo tamanho na tela e no PDF.
		QFont f(fonte);
		f.setPixelSize(std::max(1, int(std::lround(letra * k))));
		f.setHintingPreference(QFont::PreferNoHinting);
		return f;
	};
	auto cor = [&](int i) { return d.tema.serie.empty() ? tinta : d.tema.serie[size_t(i) % d.tema.serie.size()]; };
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	QRectF area = r.adjusted(4, 4, -4, -4);
	if (!g.titulo.isEmpty()) {
		p.setFont(F(1.25));
		p.setPen(tinta);
		const double h = letra * 2.0;
		p.drawText(QRectF(area.left(), area.top(), area.width(), h), Qt::AlignHCenter | Qt::AlignVCenter, g.titulo);
		area.setTop(area.top() + h + 4);
	}
	const bool pizza = g.tipo == TipoGrafico::Pizza;
	const int nSeries = pizza ? std::min<int>(1, int(g.valores.size())) : int(g.valores.size());
	const int nCat = int(g.categorias.size());
	// A legenda embaixo.
	if (g.legenda && (pizza ? nCat > 0 : nSeries > 0)) {
		p.setFont(F(0.95));
		const QFontMetricsF fm(p.font());
		const QStringList nomes = pizza ? g.categorias : g.series.mid(0, nSeries);
		double larg = 0;
		for (const QString &n : nomes)
			larg += fm.horizontalAdvance(n) + letra * 2.4;
		const double h = letra * 1.9;
		double x = area.center().x() - std::min(larg, area.width()) / 2;
		const double y = area.bottom() - h;
		for (int i = 0; i < nomes.size(); i++) {
			const double w = fm.horizontalAdvance(nomes[i]);
			if (x + w + letra * 1.2 > area.right() + 1)
				break;
			p.fillRect(QRectF(x, y + h / 2 - letra * 0.4, letra * 0.8, letra * 0.8), cor(i));
			p.setPen(tinta);
			p.drawText(QRectF(x + letra * 1.1, y, w + 4, h), Qt::AlignVCenter | Qt::AlignLeft, nomes[i]);
			x += w + letra * 2.4;
		}
		area.setBottom(y - 4);
	}
	if (nCat == 0 || nSeries == 0) {
		p.restore();
		return;
	}
	auto valor = [&](int s, int c) { return s < int(g.valores.size()) && c < int(g.valores[s].size()) ? g.valores[s][c] : 0.0; };

	if (pizza) {
		double total = 0;
		for (int c = 0; c < nCat; c++)
			total += std::max(0.0, valor(0, c));
		const double lado = std::min(area.width(), area.height());
		const QRectF roda(area.center().x() - lado / 2, area.center().y() - lado / 2, lado, lado);
		double ang = 90 * 16;
		p.setFont(F(0.95));
		for (int c = 0; c < nCat && total > 0; c++) {
			const double fr = std::max(0.0, valor(0, c)) / total;
			const double span = -fr * 360 * 16;
			p.setPen(QPen(fundoDe(d, Slide()), 1.5));
			p.setBrush(cor(c));
			p.drawPie(roda, int(ang), int(span));
			if (fr > 0.04) {
				const double meio = (ang + span / 2) / 16 * M_PI / 180;
				const QPointF pt(roda.center().x() + std::cos(meio) * lado * 0.32, roda.center().y() - std::sin(meio) * lado * 0.32);
				const QColor fc = cor(c);
				p.setPen(fc.lightness() < 150 ? Qt::white : QColor(0x1c, 0x1b, 0x19));
				const double pc = fr * 100;
				const bool inteiro = std::fabs(pc - std::round(pc)) < 0.05;
				p.drawText(QRectF(pt.x() - 40, pt.y() - 12, 80, 24), Qt::AlignCenter,
				           QLocale(QLocale::Portuguese, QLocale::Brazil).toString(pc, 'f', inteiro ? 0 : 1) + "%");
			}
			ang += span;
		}
		p.restore();
		return;
	}

	double mn = 0, mx = 0;
	for (int s = 0; s < nSeries; s++)
		for (int c = 0; c < nCat; c++) {
			mn = std::min(mn, valor(s, c));
			mx = std::max(mx, valor(s, c));
		}
	if (mx == mn)
		mx = mn + 1;
	const double passo = passoBonito(mx - mn, 5);
	mn = std::floor(mn / passo) * passo;
	mx = std::ceil(mx / passo) * passo;
	const bool deitado = g.tipo == TipoGrafico::Barras;
	p.setFont(F(0.9));
	const QFontMetricsF fm(p.font());
	double largEixo = 0;
	for (double v = mn; v <= mx + passo / 2; v += passo)
		largEixo = std::max(largEixo, fm.horizontalAdvance(numeroCurto(v)));
	double largCat = 0;
	for (const QString &c : g.categorias)
		largCat = std::max(largCat, fm.horizontalAdvance(c));
	QRectF plot = area;
	if (deitado) {
		plot.setLeft(area.left() + std::min(largCat, area.width() * 0.35) + 8);
		plot.setBottom(area.bottom() - letra * 1.8);
	} else {
		plot.setLeft(area.left() + largEixo + 8);
		plot.setBottom(area.bottom() - letra * 1.8);
	}
	plot.setRight(plot.right() - 6);
	auto posV = [&](double v) {
		const double f = (v - mn) / (mx - mn);
		return deitado ? plot.left() + f * plot.width() : plot.bottom() - f * plot.height();
	};
	// A grade e o eixo dos valores.
	for (double v = mn; v <= mx + passo / 2; v += passo) {
		const double q = posV(v);
		p.setPen(QPen(QColor(apagado.red(), apagado.green(), apagado.blue(), 70), 0.8));
		if (deitado)
			p.drawLine(QPointF(q, plot.top()), QPointF(q, plot.bottom()));
		else
			p.drawLine(QPointF(plot.left(), q), QPointF(plot.right(), q));
		p.setPen(apagado);
		if (deitado)
			p.drawText(QRectF(q - 40, plot.bottom() + 2, 80, letra * 1.7), Qt::AlignHCenter | Qt::AlignTop, numeroCurto(v));
		else
			p.drawText(QRectF(area.left(), q - letra, largEixo + 2, letra * 2), Qt::AlignRight | Qt::AlignVCenter, numeroCurto(v));
	}
	p.setPen(QPen(tinta, 1.2));
	const double zero = posV(0);
	if (deitado)
		p.drawLine(QPointF(zero, plot.top()), QPointF(zero, plot.bottom()));
	else
		p.drawLine(QPointF(plot.left(), zero), QPointF(plot.right(), zero));
	// As categorias.
	const double faixa = (deitado ? plot.height() : plot.width()) / nCat;
	p.setPen(apagado);
	const int pula = deitado ? 1 : std::max(1, int(std::ceil((largCat + 8) / std::max(1.0, faixa))));
	for (int c = 0; c < nCat; c++) {
		if (c % pula)
			continue;
		if (deitado)
			p.drawText(QRectF(area.left(), plot.top() + c * faixa, plot.left() - area.left() - 6, faixa), Qt::AlignRight | Qt::AlignVCenter,
			           fm.elidedText(g.categorias[c], Qt::ElideRight, plot.left() - area.left() - 6));
		else
			p.drawText(QRectF(plot.left() + c * faixa - faixa * pula / 2.0 + faixa / 2, plot.bottom() + 2, faixa * pula, letra * 1.7),
			           Qt::AlignHCenter | Qt::AlignTop, g.categorias[c]);
	}
	if (g.tipo == TipoGrafico::Linhas) {
		for (int s = 0; s < nSeries; s++) {
			QPainterPath c;
			for (int k = 0; k < nCat; k++) {
				const QPointF pt(plot.left() + (k + 0.5) * faixa, posV(valor(s, k)));
				if (k == 0)
					c.moveTo(pt);
				else
					c.lineTo(pt);
			}
			p.setPen(QPen(cor(s), std::max(2.0, letra * 0.22), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
			p.drawPath(c);
			p.setBrush(cor(s));
			if (nCat <= 24)
				for (int k = 0; k < nCat; k++)
					p.drawEllipse(QPointF(plot.left() + (k + 0.5) * faixa, posV(valor(s, k))), letra * 0.28, letra * 0.28);
		}
	} else {
		const double grupo = faixa * 0.72;
		const double barra = grupo / nSeries;
		p.setPen(Qt::NoPen);
		for (int s = 0; s < nSeries; s++)
			for (int c = 0; c < nCat; c++) {
				const double ini = c * faixa + (faixa - grupo) / 2 + s * barra;
				const double a = posV(valor(s, c));
				p.setBrush(cor(s));
				if (deitado)
					p.drawRect(QRectF(std::min(a, zero), plot.top() + ini, std::fabs(a - zero), barra * 0.92));
				else
					p.drawRect(QRectF(plot.left() + ini, std::min(a, zero), barra * 0.92, std::fabs(a - zero)));
			}
	}
	p.restore();
}

// --- objetos e slides ------------------------------------------------------

inline QString dica(const Objeto &o)
{
	switch (o.papel) {
	case Papel::Titulo: return "Clique duas vezes para escrever o título";
	case Papel::Subtitulo: return "Clique duas vezes para escrever o subtítulo";
	case Papel::Corpo:
	case Papel::Corpo2: return "Clique duas vezes para escrever os tópicos";
	default: return {};
	}
}

inline void desenharObjeto(QPainter &p, const Doc &d, const Objeto &o, bool comTexto = true, bool dicas = false)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const QPointF c = o.r.center();
	if (o.giro != 0 || o.viraH || o.viraV) {
		p.translate(c);
		p.rotate(o.giro);
		p.scale(o.viraH ? -1 : 1, o.viraV ? -1 : 1);
		p.translate(-c);
	}
	if (o.tipo == Tipo::Imagem) {
		const QImage img = imagemDe(d, o.midia);
		if (!img.isNull()) {
			const QRectF fonte(o.recorte.x() * img.width(), o.recorte.y() * img.height(), o.recorte.width() * img.width(),
			                   o.recorte.height() * img.height());
			p.drawImage(o.r, img, fonte);
		} else {
			p.fillRect(o.r, QColor(0xd9, 0xd3, 0xc5));
			p.setPen(QPen(QColor(0x6d, 0x68, 0x5e), 1, Qt::DashLine));
			p.drawRect(o.r);
		}
		if (o.borda.isValid() && o.espBorda > 0) {
			p.setPen(QPen(o.borda, o.espBorda));
			p.setBrush(Qt::NoBrush);
			p.drawRect(o.r);
		}
	} else if (o.tipo == Tipo::Grafico) {
		if (o.preench.isValid())
			p.fillRect(o.r, o.preench);
		desenharGrafico(p, d, o);
	} else {
		const QPainterPath cm = caminho(o.tipo, o.r);
		if (o.tipo != Tipo::Linha && o.preench.isValid())
			p.fillPath(cm, o.preench);
		if (o.borda.isValid() && o.espBorda > 0) {
			p.setPen(QPen(o.borda, o.espBorda, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
			p.drawPath(cm);
		}
	}
	p.restore();
	// O texto não espelha (só gira).
	if (comTexto && o.aceitaTexto()) {
		p.save();
		if (o.giro != 0) {
			p.translate(c);
			p.rotate(o.giro);
			p.translate(-c);
		}
		if (o.temTexto())
			desenharTexto(p, d, o);
		else if (dicas && o.papel != Papel::Livre) {
			Objeto x = o;
			x.pars = paragrafos(dica(o), false, o.pars.empty() ? 0 : o.pars.front().alin);
			x.cor = d.tema.apagado;
			x.cor.setAlpha(170);
			x.encolher = true;
			desenharTexto(p, d, x);
		}
		p.restore();
	}
}

inline void desenharFundo(QPainter &p, const Doc &d, const Slide &s)
{
	const QRectF palco(0, 0, d.larg, d.alt);
	p.fillRect(palco, fundoDe(d, s));
	if (!s.fundoMidia.isEmpty()) {
		const QImage img = imagemDe(d, s.fundoMidia);
		if (!img.isNull()) {
			// Cobre o palco todo, sem deformar.
			const double k = std::max(d.larg / img.width(), d.alt / img.height());
			const QSizeF t(d.larg / k, d.alt / k);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(palco, img, QRectF((img.width() - t.width()) / 2, (img.height() - t.height()) / 2, t.width(), t.height()));
		}
	}
}

// Desenha o slide inteiro dentro de "alvo" (mantendo a proporção do palco).
inline void desenharSlide(QPainter &p, const Doc &d, const Slide &s, const QRectF &alvo, bool dicas = false)
{
	p.save();
	const double k = std::min(alvo.width() / d.larg, alvo.height() / d.alt);
	p.translate(alvo.center().x() - d.larg * k / 2, alvo.center().y() - d.alt * k / 2);
	p.scale(k, k);
	p.setClipRect(QRectF(0, 0, d.larg, d.alt));
	desenharFundo(p, d, s);
	for (const Objeto &o : s.objs)
		desenharObjeto(p, d, o, true, dicas);
	p.restore();
}

inline QImage imagemSlide(const Doc &d, const Slide &s, int largura, bool dicas = false)
{
	const int altura = std::max(1, int(std::lround(largura * d.alt / d.larg)));
	QImage img(largura, altura, QImage::Format_ARGB32_Premultiplied);
	img.fill(fundoDe(d, s));
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	desenharSlide(p, d, s, QRectF(0, 0, largura, altura), dicas);
	return img;
}

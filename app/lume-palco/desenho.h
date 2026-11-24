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
inline QRectF interior(const Objeto &o) { return o.r.marginsRemoved(o.margem); }

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

// O quanto um objeto (ou um parágrafo) está presente num efeito: aplica no
// pintor a opacidade, o deslocamento, a escala ou o recorte. Falso: não aparece.
inline bool aplicarQuadro(QPainter &p, const Quadro &q, const QRectF &area, double W, double H)
{
	if (q.ef == Efeito::Nenhum)
		return true;
	const double p0 = std::clamp(q.p, 0.0, 1.0);
	const double e = 1 - std::pow(1 - p0, 3); // ease out
	const double v = q.saida ? 1 - e : e;     // 1: todo à vista
	switch (q.ef) {
	case Efeito::Aparecer:
		return q.saida ? p0 < 1 : p0 > 0;
	case Efeito::Dissolver:
		p.setOpacity(p.opacity() * v);
		break;
	case Efeito::Voar: {
		const double f = 1 - v;
		const QPointF d = q.dir == 1 ? QPointF(-f * area.right(), 0)
		                  : q.dir == 2 ? QPointF(f * (W - area.left()), 0)
		                  : q.dir == 3 ? QPointF(0, -f * area.bottom())
		                               : QPointF(0, f * (H - area.top()));
		p.translate(d);
		break;
	}
	case Efeito::Ampliar: {
		const double k = 0.2 + 0.8 * v;
		p.translate(area.center());
		p.scale(k, k);
		p.translate(-area.center());
		p.setOpacity(p.opacity() * v);
		break;
	}
	case Efeito::Revelar: {
		QRectF c = area.adjusted(-2, -2, 2, 2);
		if (q.dir == 1) c.setWidth(c.width() * v);
		else if (q.dir == 2) c.setLeft(c.right() - c.width() * v);
		else if (q.dir == 3) c.setHeight(c.height() * v);
		else c.setTop(c.bottom() - c.height() * v);
		p.setClipRect(c, Qt::IntersectClip);
		break;
	}
	default:
		break;
	}
	return v > 0.001;
}

inline void desenharTexto(QPainter &p, const Doc &d, const Objeto &o, const std::vector<Quadro> *pars = nullptr, double W = 0, double H = 0)
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
	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.palette.setColor(QPalette::Text, corTexto(d, o));
	if (!pars || pars->empty()) {
		p.save();
		p.translate(in.left(), y);
		p.scale(1 / K, 1 / K);
		td.documentLayout()->draw(&p, ctx);
		p.restore();
		return;
	}
	// Um parágrafo de cada vez, cada um com o seu efeito.
	int i = 0;
	for (QTextBlock b = td.begin(); b.isValid(); b = b.next(), i++) {
		const QRectF br = td.documentLayout()->blockBoundingRect(b);
		const QRectF faixa(0, br.top(), td.textWidth(), br.height());
		const QRectF area(in.left(), y + br.top() / K, in.width(), br.height() / K);
		p.save();
		if (i < int(pars->size()) && !aplicarQuadro(p, (*pars)[size_t(i)], area, W, H)) {
			p.restore();
			continue;
		}
		p.translate(in.left(), y);
		p.scale(1 / K, 1 / K);
		p.setClipRect(faixa.adjusted(-td.textWidth(), -1, td.textWidth(), 1), Qt::IntersectClip);
		td.documentLayout()->draw(&p, ctx);
		p.restore();
	}
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

// As cores e a letra do tema para o desenho dos gráficos (../comum/grafico.h).
inline EstiloGrafico estiloDoTema(const Doc &d)
{
	EstiloGrafico e;
	e.fonte = d.tema.fonteTexto;
	e.texto = d.tema.texto;
	e.apagado = d.tema.apagado;
	e.fundo = fundoDe(d, Slide());
	e.serie = d.tema.serie;
	return e;
}

inline void desenharGrafico(QPainter &p, const Doc &d, const Objeto &o) { desenharGrafico(p, o.r, o.graf, estiloDoTema(d)); }

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

// --- tabelas ---------------------------------------------------------------
//
// As linhas crescem para o texto caber (como no PowerPoint); as colunas
// dividem a largura da tabela. As cores vêm do tema: a primeira linha
// (o cabeçalho) na primeira cor das séries, as outras em faixas.

inline QColor misturar(const QColor &a, const QColor &b, double f)
{
	return QColor::fromRgbF(float(a.redF() * (1 - f) + b.redF() * f), float(a.greenF() * (1 - f) + b.greenF() * f),
	                        float(a.blueF() * (1 - f) + b.blueF() * f));
}

inline QColor fundoDaCelula(const Doc &d, const Objeto &t, int l)
{
	if (l == 0 && t.cabecalho)
		return d.tema.serie.empty() ? d.tema.destaque : d.tema.serie.front();
	if (t.faixas && (l - (t.cabecalho ? 1 : 0)) % 2 == 1)
		return misturar(d.tema.fundo, d.tema.texto, 0.07);
	return QColor();
}

inline void arrumarTabela(const Doc &d, Objeto &t)
{
	if (t.tipo != Tipo::Tabela || t.lins <= 0 || t.cols <= 0)
		return;
	t.cel.resize(size_t(t.lins * t.cols));
	t.largs.resize(size_t(t.cols), t.r.width() / t.cols);
	t.alts.resize(size_t(t.lins), 24);
	double soma = 0;
	for (double &w : t.largs) {
		w = std::max(4.0, w);
		soma += w;
	}
	for (double &w : t.largs)
		w *= t.r.width() / soma;
	t.giro = 0;
	double y = t.r.top();
	for (int l = 0; l < t.lins; l++) {
		double h = t.alts[size_t(l)];
		double x = t.r.left();
		for (int c = 0; c < t.cols; c++) {
			Objeto &o = t.celula(l, c);
			o.tipo = Tipo::Texto;
			o.papel = Papel::Livre;
			o.giro = 0;
			o.r = QRectF(x, y, t.largs[size_t(c)], h);
			o.fundoCel = o.preench.isValid() ? o.preench : fundoDaCelula(d, t, l);
			if (o.temTexto()) {
				QTextDocument td;
				montarDocumento(td, d, o);
				h = std::max(h, td.size().height() / K + 7.2);
			}
			x += t.largs[size_t(c)];
		}
		for (int c = 0; c < t.cols; c++)
			t.celula(l, c).r.setHeight(h);
		y += h;
	}
	t.r.setHeight(y - t.r.top());
}

inline void desenharTabela(QPainter &p, const Doc &d, const Objeto &t0, int semTexto = -1, bool comTexto = true)
{
	Objeto t = t0;
	arrumarTabela(d, t);
	const QColor fio = t.borda.isValid() && t.espBorda > 0 ? t.borda : d.tema.apagado;
	const double esp = t.borda.isValid() && t.espBorda > 0 ? t.espBorda : 0.75;
	for (const Objeto &c : t.cel)
		if (c.fundoCel.isValid())
			p.fillRect(c.r, c.fundoCel);
	p.setPen(QPen(fio, esp));
	p.setBrush(Qt::NoBrush);
	for (int l = 1; l < t.lins; l++) {
		const double y = t.celula(l, 0).r.top();
		p.drawLine(QPointF(t.r.left(), y), QPointF(t.r.right(), y));
	}
	for (int c = 1; c < t.cols; c++) {
		const double x = t.celula(0, c).r.left();
		p.drawLine(QPointF(x, t.r.top()), QPointF(x, t.r.bottom()));
	}
	p.drawRect(t.r);
	for (int k = 0; k < int(t.cel.size()) && comTexto; k++)
		if (k != semTexto)
			desenharTexto(p, d, t.cel[size_t(k)]);
}

// --- vídeo -----------------------------------------------------------------

inline void desenharVideo(QPainter &p, const Doc &d, const Objeto &o, bool dicas, const QImage *quadro = nullptr)
{
	const QImage capa = quadro && !quadro->isNull() ? *quadro : imagemDe(d, o.capa);
	p.fillRect(o.r, QColor(0x1c, 0x1b, 0x19));
	if (!capa.isNull()) {
		// A imagem inteira, sem deformar, no meio.
		const double k = std::min(o.r.width() / capa.width(), o.r.height() / capa.height());
		QRectF r(0, 0, capa.width() * k, capa.height() * k);
		r.moveCenter(o.r.center());
		p.drawImage(r, capa);
	}
	if (dicas && !quadro) {
		const double raio = std::clamp(std::min(o.r.width(), o.r.height()) * 0.12, 10.0, 40.0);
		const QPointF c = o.r.center();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 140));
		p.drawEllipse(c, raio, raio);
		QPainterPath tri;
		tri.moveTo(c + QPointF(-raio * 0.35, -raio * 0.5));
		tri.lineTo(c + QPointF(raio * 0.55, 0));
		tri.lineTo(c + QPointF(-raio * 0.35, raio * 0.5));
		tri.closeSubpath();
		p.fillPath(tri, Qt::white);
	}
	if (o.borda.isValid() && o.espBorda > 0) {
		p.setPen(QPen(o.borda, o.espBorda));
		p.setBrush(Qt::NoBrush);
		p.drawRect(o.r);
	}
}

// Como desenhar um objeto: com o texto, com as dicas dos lugares vazios, o
// estado das animações, a célula em edição (sem o texto) e o quadro do vídeo.
struct ComoDesenhar {
	bool comTexto = true, dicas = false;
	const EstadoObj *estado = nullptr;
	int celulaSemTexto = -1;
	const QImage *quadroVideo = nullptr;
};

inline void desenharObjeto(QPainter &p, const Doc &d, const Objeto &o, const ComoDesenhar &como)
{
	const bool comTexto = como.comTexto, dicas = como.dicas;
	const EstadoObj *est = como.estado && como.estado->tem ? como.estado : nullptr;
	p.save();
	// A animação do objeto todo (ou, por parágrafo, a do primeiro para a forma).
	Quadro qForma;
	if (est) {
		qForma = est->todo;
		if (est->pars.size() == o.pars.size() && !est->pars.empty())
			for (size_t i = 0; i < o.pars.size(); i++)
				if (!o.pars[i].texto().trimmed().isEmpty()) {
					qForma = est->pars[i];
					break;
				}
	}
	const bool porPar = est && !est->pars.empty() && est->pars.size() == o.pars.size();
	if (!aplicarQuadro(p, porPar ? est->todo : qForma, o.r, d.larg, d.alt)) {
		p.restore();
		return;
	}
	p.save();
	if (porPar && !aplicarQuadro(p, qForma, o.r, d.larg, d.alt))
		p.setOpacity(0);
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
	} else if (o.tipo == Tipo::Tabela) {
		desenharTabela(p, d, o, como.celulaSemTexto, comTexto);
	} else if (o.tipo == Tipo::Video) {
		desenharVideo(p, d, o, dicas, como.quadroVideo);
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
			desenharTexto(p, d, o, porPar ? &est->pars : nullptr, d.larg, d.alt);
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
	p.restore();
}

inline void desenharObjeto(QPainter &p, const Doc &d, const Objeto &o, bool comTexto = true, bool dicas = false)
{
	ComoDesenhar c;
	c.comTexto = comTexto;
	c.dicas = dicas;
	desenharObjeto(p, d, o, c);
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

// Desenha o slide inteiro dentro de "alvo" (mantendo a proporção do palco),
// num estado das animações (sem estado: tudo à vista).
inline void desenharSlide(QPainter &p, const Doc &d, const Slide &s, const QRectF &alvo, bool dicas = false,
                          const std::vector<EstadoObj> *estado = nullptr, const QHash<int, QImage> *videos = nullptr)
{
	p.save();
	const double k = std::min(alvo.width() / d.larg, alvo.height() / d.alt);
	p.translate(alvo.center().x() - d.larg * k / 2, alvo.center().y() - d.alt * k / 2);
	p.scale(k, k);
	p.setClipRect(QRectF(0, 0, d.larg, d.alt));
	desenharFundo(p, d, s);
	for (size_t i = 0; i < s.objs.size(); i++) {
		ComoDesenhar c;
		c.dicas = dicas;
		if (estado && i < estado->size())
			c.estado = &(*estado)[i];
		if (videos) {
			auto it = videos->constFind(int(i));
			if (it != videos->cend())
				c.quadroVideo = &*it;
		}
		desenharObjeto(p, d, s.objs[i], c);
	}
	p.restore();
}

inline QImage imagemSlide(const Doc &d, const Slide &s, int largura, bool dicas = false, const std::vector<EstadoObj> *estado = nullptr)
{
	const int altura = std::max(1, int(std::lround(largura * d.alt / d.larg)));
	QImage img(largura, altura, QImage::Format_ARGB32_Premultiplied);
	img.fill(fundoDe(d, s));
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	desenharSlide(p, d, s, QRectF(0, 0, largura, altura), dicas, estado);
	return img;
}

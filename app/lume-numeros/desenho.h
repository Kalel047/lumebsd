// SPDX-License-Identifier: BSD-2-Clause
//
// lume-numeros: o desenho. As células (para o PDF e as miniaturas) e os
// gráficos (na tela, no PDF e nas miniaturas), com as cores do Papel.

#pragma once

#include "planilha.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

struct Tinta {
	QColor papel = QColor(0xff, 0xfd, 0xf8);
	QColor tinta = QColor(0x1c, 0x1b, 0x19);
	QColor fio = QColor(0xd9, 0xd3, 0xc5);
	QColor apagado = QColor(0x5d, 0x59, 0x50);
};

// As cores das séries: tinta e tons de terra, como as Finanças.
inline QColor corDaSerie(int i)
{
	static const QColor c[] = {QColor(0x2f, 0x4b, 0x6e), QColor(0xb5, 0x6a, 0x2e), QColor(0x5f, 0x7f, 0x4a),
	                           QColor(0x8a, 0x3b, 0x3b), QColor(0x6b, 0x5b, 0x8a), QColor(0x3f, 0x7f, 0x7f),
	                           QColor(0xa8, 0x8a, 0x2c), QColor(0x55, 0x55, 0x55)};
	return c[i % 8];
}

inline QString numeroCurto(double v)
{
	const double a = std::fabs(v);
	if (a >= 1e9) return br().toString(v / 1e9, 'f', a >= 1e10 ? 0 : 1) + " bi";
	if (a >= 1e6) return br().toString(v / 1e6, 'f', a >= 1e7 ? 0 : 1) + " mi";
	if (a >= 1e4) return br().toString(v / 1e3, 'f', 0) + " mil";
	if (a == std::floor(a)) return br().toString(v, 'f', 0);
	return br().toString(v, 'f', a < 1 ? 3 : 1);
}

// Uma escala "bonita" de 0 (ou do mínimo) até o máximo.
inline void escala(double mn, double mx, double &a, double &b, double &passo)
{
	if (mn > 0) mn = 0;
	if (mx < 0) mx = 0;
	if (mx == mn) mx = mn + 1;
	const double bruto = (mx - mn) / 5;
	const double p10 = std::pow(10, std::floor(std::log10(bruto)));
	const double m = bruto / p10;
	passo = (m <= 1 ? 1 : m <= 2 ? 2 : m <= 2.5 ? 2.5 : m <= 5 ? 5 : 10) * p10;
	a = std::floor(mn / passo) * passo;
	b = std::ceil(mx / passo) * passo;
}

inline void desenharGrafico(QPainter &p, const QRectF &r, const Pasta &pasta, int f, const Grafico &g, const Tinta &t = Tinta(),
                            const QString &familia = "IBM Plex Sans")
{
	QStringList cats;
	std::vector<Serie> series;
	pasta.dadosDoGrafico(f, g, cats, series);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(r, t.papel);
	p.setPen(QPen(t.tinta, 1));
	p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
	QFont fn(familia);
	fn.setPixelSize(11);
	QFont ft(familia == "IBM Plex Sans" ? QString("IBM Plex Serif") : familia);
	ft.setPixelSize(15);
	QRectF area = r.adjusted(14, 10, -14, -10);
	if (!g.titulo.isEmpty()) {
		p.setFont(ft);
		p.setPen(t.tinta);
		p.drawText(QRectF(area.left(), area.top(), area.width(), 22), Qt::AlignLeft | Qt::AlignVCenter, g.titulo);
		area.setTop(area.top() + 28);
	}
	p.setFont(fn);
	const QFontMetricsF fm(fn);
	// A legenda, embaixo.
	const bool pizza = g.tipo == Grafico::Pizza;
	QStringList nomes;
	if (pizza)
		nomes = cats;
	else
		for (const Serie &s : series)
			nomes << s.nome;
	if (nomes.size() > 1 || pizza) {
		double x = area.left(), y = area.bottom() - 14;
		double linhas = 1;
		for (const QString &n : nomes) {
			const double w = 16 + fm.horizontalAdvance(n) + 14;
			if (x + w > area.right() && x > area.left()) { x = area.left(); linhas++; }
			x += w;
		}
		area.setBottom(area.bottom() - 18 * linhas - 4);
		x = area.left();
		y = area.bottom() + 8;
		for (int i = 0; i < nomes.size(); i++) {
			const double w = 16 + fm.horizontalAdvance(nomes[i]) + 14;
			if (x + w > r.right() - 14 && x > area.left()) { x = area.left(); y += 18; }
			p.fillRect(QRectF(x, y + 3, 10, 10), corDaSerie(i));
			p.setPen(t.tinta);
			p.drawText(QRectF(x + 14, y, w, 16), Qt::AlignLeft | Qt::AlignVCenter, nomes[i]);
			x += w;
		}
	}
	if (series.empty() || cats.isEmpty() || area.height() < 30 || area.width() < 40) {
		p.setPen(t.apagado);
		p.drawText(area, Qt::AlignCenter, "Sem dados");
		p.restore();
		return;
	}
	if (pizza) {
		const Serie &s = series[0];
		double tot = 0;
		for (double v : s.v) tot += std::max(0.0, v);
		const double d = std::min(area.width(), area.height()) - 10;
		const QRectF c(area.center().x() - d / 2, area.center().y() - d / 2, d, d);
		double ang = 90 * 16;
		for (size_t i = 0; i < s.v.size(); i++) {
			const double fr = tot > 0 ? std::max(0.0, s.v[i]) / tot : 0;
			const int span = -int(std::round(fr * 360 * 16));
			p.setPen(QPen(t.papel, 1.5));
			p.setBrush(corDaSerie(int(i)));
			p.drawPie(c, int(ang), span);
			if (fr >= 0.05) {
				const double meio = (ang + span / 2.0) / 16.0 * M_PI / 180.0;
				const QPointF q(c.center().x() + std::cos(meio) * d * 0.33, c.center().y() - std::sin(meio) * d * 0.33);
				p.setPen(Qt::white);
				p.drawText(QRectF(q.x() - 30, q.y() - 9, 60, 18), Qt::AlignCenter, br().toString(fr * 100, 'f', 0) + "%");
			}
			ang += span;
		}
		p.restore();
		return;
	}
	// Os eixos.
	const bool dispersao = g.tipo == Grafico::Dispersao;
	const bool barras = g.tipo == Grafico::Barras;
	double mn = 0, mx = 0;
	bool primeiro = true;
	for (const Serie &s : series)
		for (size_t i = 0; i < s.v.size(); i++)
			if (s.tem[i]) {
				if (primeiro) { mn = mx = s.v[i]; primeiro = false; }
				mn = std::min(mn, s.v[i]);
				mx = std::max(mx, s.v[i]);
			}
	double a, b, passo;
	escala(mn, mx, a, b, passo);
	// Dispersão: x é a 1ª série, y as outras.
	double xa = 0, xb = 1, xpasso = 1;
	if (dispersao && series.size() >= 2) {
		double xmn = series[0].v[0], xmx = xmn;
		for (double v : series[0].v) { xmn = std::min(xmn, v); xmx = std::max(xmx, v); }
		escala(xmn, xmx, xa, xb, xpasso);
		mn = mx = series[1].v[0];
		for (size_t k = 1; k < series.size(); k++)
			for (double v : series[k].v) { mn = std::min(mn, v); mx = std::max(mx, v); }
		escala(mn, mx, a, b, passo);
	}
	double maiorRot = 0;
	for (double v = a; v <= b + passo / 2; v += passo)
		maiorRot = std::max(maiorRot, fm.horizontalAdvance(numeroCurto(v)));
	QRectF pl = area;
	if (barras) {
		double maiorCat = 0;
		for (const QString &c : cats) maiorCat = std::max(maiorCat, fm.horizontalAdvance(c));
		pl.setLeft(pl.left() + std::min(maiorCat, area.width() * 0.35) + 8);
		pl.setBottom(pl.bottom() - 18);
	} else {
		pl.setLeft(pl.left() + maiorRot + 8);
		pl.setBottom(pl.bottom() - 18);
	}
	pl.setTop(pl.top() + 4);
	pl.setRight(pl.right() - 6);
	auto posV = [&](double v) {
		return barras ? pl.left() + (v - a) / (b - a) * pl.width() : pl.bottom() - (v - a) / (b - a) * pl.height();
	};
	// A grade.
	p.setPen(QPen(t.fio, 1));
	for (double v = a; v <= b + passo / 2; v += passo) {
		const double q = posV(v);
		if (barras) {
			p.drawLine(QPointF(q, pl.top()), QPointF(q, pl.bottom()));
			p.setPen(t.apagado);
			p.drawText(QRectF(q - 40, pl.bottom() + 2, 80, 16), Qt::AlignHCenter | Qt::AlignTop, numeroCurto(v));
		} else {
			p.drawLine(QPointF(pl.left(), q), QPointF(pl.right(), q));
			p.setPen(t.apagado);
			p.drawText(QRectF(area.left(), q - 8, maiorRot, 16), Qt::AlignRight | Qt::AlignVCenter, numeroCurto(v));
		}
		p.setPen(QPen(t.fio, 1));
	}
	p.setPen(QPen(t.tinta, 1));
	if (barras)
		p.drawLine(QPointF(posV(0), pl.top()), QPointF(posV(0), pl.bottom()));
	else
		p.drawLine(QPointF(pl.left(), posV(0)), QPointF(pl.right(), posV(0)));
	const int n = int(cats.size());
	if (dispersao && series.size() >= 2) {
		auto px = [&](double v) { return pl.left() + (v - xa) / (xb - xa) * pl.width(); };
		p.setPen(t.apagado);
		for (double v = xa; v <= xb + xpasso / 2; v += xpasso)
			p.drawText(QRectF(px(v) - 40, pl.bottom() + 2, 80, 16), Qt::AlignHCenter | Qt::AlignTop, numeroCurto(v));
		for (size_t k = 1; k < series.size(); k++) {
			p.setPen(Qt::NoPen);
			p.setBrush(corDaSerie(int(k - 1)));
			for (int i = 0; i < n; i++)
				if (series[k].tem[i] && series[0].tem[i])
					p.drawEllipse(QPointF(px(series[0].v[i]), posV(series[k].v[i])), 3.5, 3.5);
		}
		p.restore();
		return;
	}
	// As categorias no eixo.
	const double faixa = (barras ? pl.height() : pl.width()) / std::max(1, n);
	p.setPen(t.apagado);
	const int pular = std::max(1, int(std::ceil(fm.horizontalAdvance("0000000") * n / std::max(1.0, barras ? 1e9 : pl.width()))));
	for (int i = 0; i < n; i++) {
		if (barras) {
			p.drawText(QRectF(area.left(), pl.top() + i * faixa, pl.left() - area.left() - 6, faixa), Qt::AlignRight | Qt::AlignVCenter,
			           fm.elidedText(cats[i], Qt::ElideRight, pl.left() - area.left() - 6));
		} else if (i % pular == 0) {
			p.drawText(QRectF(pl.left() + i * faixa - 20, pl.bottom() + 2, faixa + 40, 16), Qt::AlignHCenter | Qt::AlignTop,
			           fm.elidedText(cats[i], Qt::ElideRight, faixa * pular + 30));
		}
	}
	const int ns = int(series.size());
	if (g.tipo == Grafico::Colunas || barras) {
		const double grupo = faixa * 0.72;
		const double larg = grupo / ns;
		for (int k = 0; k < ns; k++)
			for (int i = 0; i < n; i++) {
				if (!series[size_t(k)].tem[size_t(i)]) continue;
				const double v = series[size_t(k)].v[size_t(i)];
				const double ini = i * faixa + (faixa - grupo) / 2 + k * larg;
				QRectF bar;
				if (barras)
					bar = QRectF(QPointF(posV(std::min(0.0, v)), pl.top() + ini), QPointF(posV(std::max(0.0, v)), pl.top() + ini + larg - 1));
				else
					bar = QRectF(QPointF(pl.left() + ini, posV(std::max(0.0, v))), QPointF(pl.left() + ini + larg - 1, posV(std::min(0.0, v))));
				p.fillRect(bar, corDaSerie(k));
			}
	} else {
		for (int k = 0; k < ns; k++) {
			QPainterPath caminho;
			bool comecou = false;
			std::vector<QPointF> pts;
			for (int i = 0; i < n; i++) {
				if (!series[size_t(k)].tem[size_t(i)]) continue;
				const QPointF q(pl.left() + (i + 0.5) * faixa, posV(series[size_t(k)].v[size_t(i)]));
				pts.push_back(q);
				if (!comecou) { caminho.moveTo(q); comecou = true; }
				else caminho.lineTo(q);
			}
			if (pts.empty()) continue;
			if (g.tipo == Grafico::Area) {
				QPainterPath ar = caminho;
				ar.lineTo(pts.back().x(), posV(0));
				ar.lineTo(pts.front().x(), posV(0));
				ar.closeSubpath();
				QColor c = corDaSerie(k);
				c.setAlpha(110);
				p.fillPath(ar, c);
			}
			p.setPen(QPen(corDaSerie(k), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
			p.drawPath(caminho);
			if (n <= 24 && g.tipo == Grafico::Linhas) {
				p.setBrush(corDaSerie(k));
				p.setPen(Qt::NoPen);
				for (const QPointF &q : pts) p.drawEllipse(q, 2.8, 2.8);
			}
		}
	}
	p.restore();
}

// As células de um pedaço da folha, com a origem em (0, 0) no canto de
// (r1, c1). Para o PDF e as miniaturas (a tela usa o QTableView).
inline void desenharCelulas(QPainter &p, const Pasta &pasta, int f, int r1, int c1, int r2, int c2, bool grade = true)
{
	const Folha &fo = pasta.folha(f);
	const Tinta t;
	QFont base(pasta.fonte);
	base.setPointSizeF(pasta.tamanho);
	for (int fase = 0; fase < 2; fase++) {
	double y = 0;
	for (int r = r1; r <= r2; r++) {
		const double h = fo.alturaDe(r);
		double x = 0;
		for (int c = c1; c <= c2; c++) {
			const double w = fo.larguraDe(c);
			const QRectF cel(x, y, w, h);
			const Celula *k = fo.em(r, c);
			if (fase == 1)
				goto texto;
			if (k && k->fmt.fundo.isValid())
				p.fillRect(cel, k->fmt.fundo);
			if (grade) {
				p.setPen(QPen(t.fio, 0.6));
				p.drawLine(cel.topRight(), cel.bottomRight());
				p.drawLine(cel.bottomLeft(), cel.bottomRight());
			}
			if (k && k->fmt.borda) {
				p.setPen(QPen(t.tinta, 0.8));
				p.drawRect(cel);
			}
			x += w;
			continue;
		texto:
			if (k && k->v.t != Valor::Vazio) {
				QFont fn = base;
				fn.setBold(k->fmt.negrito);
				fn.setItalic(k->fmt.italico);
				fn.setUnderline(k->fmt.sublinhado);
				if (k->fmt.tamanho > 0)
					fn.setPointSizeF(k->fmt.tamanho);
				p.setFont(fn);
				p.setPen(k->v.t == Valor::Erro ? QColor(0x9a, 0x2a, 0x2a) : (k->fmt.cor.isValid() ? k->fmt.cor : t.tinta));
				int al = k->fmt.alin;
				if (al == 0)
					al = k->v.t == Valor::Numero ? 3 : (k->v.t == Valor::Logico || k->v.t == Valor::Erro) ? 2 : 1;
				const Qt::Alignment a = (al == 2 ? Qt::AlignHCenter : al == 3 ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
				QString s = mostrar(k->v, k->fmt);
				const QRectF dentro = cel.adjusted(4, 0, -4, 0);
				// Texto que passa da célula invade as vazias do lado (como no Excel).
				QRectF caixa = dentro;
				if (k->v.t == Valor::Texto && al == 1) {
					double extra = 0;
					for (int cc = c + 1; cc <= c2 && !fo.em(r, cc); cc++)
						extra += fo.larguraDe(cc);
					caixa.setWidth(dentro.width() + extra);
				} else if (k->v.t == Valor::Numero && QFontMetricsF(fn).horizontalAdvance(s) > dentro.width()) {
					s = QString(int(dentro.width() / QFontMetricsF(fn).horizontalAdvance('#')), '#');
				}
				p.save();
				p.setClipRect(caixa.adjusted(-4, 0, 4, 0));
				p.drawText(caixa, a, s);
				p.restore();
			}
			x += w;
		}
		y += h;
	}
	}
}

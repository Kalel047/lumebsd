// SPDX-License-Identifier: BSD-2-Clause
//
// LumeBSD, o escritório: os dados de um gráfico (colunas, barras, linhas ou
// pizza), o texto que os leva de um app para o outro (a área de
// transferência e o texto alternativo das imagens) e o desenho. Os Números
// copiam o gráfico assim; o Palco e o Páginas colam como gráfico, com os
// dados (e gravam como gráfico de verdade: grafico-arquivos.h).

#pragma once

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

// O tipo na área de transferência (o texto lume-grafico:...).
inline const char *MIME_GRAFICO = "application/x-lume-grafico";

// --- os dados --------------------------------------------------------------

enum class TipoGrafico { Colunas, Barras, Linhas, Pizza };

struct DadosGrafico {
	TipoGrafico tipo = TipoGrafico::Colunas;
	QString titulo;
	QStringList categorias;
	QStringList series;
	std::vector<std::vector<double>> valores; // [série][categoria]
	bool legenda = true;
};

// Os dados em texto (vão no "texto alternativo" da imagem que representa o
// gráfico no .pptx e no .odp, e voltam a ser um gráfico quando o Palco lê
// o arquivo de novo):  tipo;título|cat1;cat2|série1;v;v|série2;v;v
inline QString gravarDados(const DadosGrafico &g)
{
	static const char *nomes[] = {"colunas", "barras", "linhas", "pizza"};
	auto limpa = [](QString s) { return s.replace('|', '/').replace(';', ','); };
	QStringList partes;
	partes << QString(nomes[int(g.tipo)]) + ";" + limpa(g.titulo) + (g.legenda ? "" : ";semlegenda");
	QStringList cats;
	for (const QString &c : g.categorias)
		cats << limpa(c);
	partes << cats.join(';');
	for (int s = 0; s < g.series.size(); s++) {
		QStringList l{limpa(g.series[s])};
		for (int c = 0; c < g.categorias.size(); c++) {
			const double v = s < int(g.valores.size()) && c < int(g.valores[s].size()) ? g.valores[s][c] : 0;
			l << QString::number(v, 'g', 12);
		}
		partes << l.join(';');
	}
	return "lume-grafico:" + partes.join('|');
}

inline bool lerDados(const QString &txt, DadosGrafico &g)
{
	if (!txt.startsWith("lume-grafico:"))
		return false;
	const QStringList partes = txt.mid(13).split('|');
	if (partes.size() < 2)
		return false;
	const QStringList cab = partes[0].split(';');
	const QString t = cab.value(0);
	g = DadosGrafico();
	g.tipo = t == "barras" ? TipoGrafico::Barras : t == "linhas" ? TipoGrafico::Linhas : t == "pizza" ? TipoGrafico::Pizza : TipoGrafico::Colunas;
	g.titulo = cab.value(1);
	g.legenda = cab.value(2) != "semlegenda";
	g.categorias = partes[1].split(';');
	for (int i = 2; i < partes.size(); i++) {
		const QStringList l = partes[i].split(';');
		g.series << l.value(0);
		std::vector<double> v;
		for (int c = 1; c < l.size(); c++)
			v.push_back(l[c].toDouble());
		v.resize(size_t(g.categorias.size()), 0);
		g.valores.push_back(v);
	}
	return true;
}

// --- o desenho -------------------------------------------------------------

// As cores e a letra (o Palco tira do tema; o Páginas, do papel).
struct EstiloGrafico {
	QString fonte = "IBM Plex Sans";
	QColor texto = QColor(0x2b, 0x2a, 0x27), apagado = QColor(0x6d, 0x68, 0x5e), fundo = QColor(0xff, 0xff, 0xff);
	std::vector<QColor> serie = {QColor(0x2f, 0x4b, 0x6e), QColor(0xb5, 0x65, 0x2a), QColor(0x5b, 0x7a, 0x48), QColor(0x8a, 0x5a, 0x8c), QColor(0xc9, 0xa2, 0x27)};
};

inline QString grNumeroCurto(double v)
{
	const QLocale br(QLocale::Portuguese, QLocale::Brazil);
	const double a = std::fabs(v);
	if (a >= 1e9) return br.toString(v / 1e9, 'f', 1) + " bi";
	if (a >= 1e6) return br.toString(v / 1e6, 'f', 1) + " mi";
	if (a >= 1e4) return br.toString(v / 1e3, 'f', 0) + " mil";
	if (a == std::floor(a)) return br.toString(v, 'f', 0);
	return br.toString(v, 'f', a < 10 ? 1 : 0);
}

inline double grPassoBonito(double faixa, int marcas)
{
	const double bruto = faixa / std::max(1, marcas);
	const double e = std::pow(10.0, std::floor(std::log10(std::max(bruto, 1e-12))));
	for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
		if (m * e >= bruto)
			return m * e;
	return 10 * e;
}

inline void desenharGrafico(QPainter &p, const QRectF &r, const DadosGrafico &g, const EstiloGrafico &e)
{
	const QColor tinta = e.texto, apagado = e.apagado;
	const QString fonte = e.fonte;
	const double letra = std::clamp(std::min(r.width(), r.height()) / 22, 9.0, 20.0);
	auto F = [&](double k) {
		// Em pixels do palco (pontos): o mesmo tamanho na tela e no PDF.
		QFont f(fonte);
		f.setPixelSize(std::max(1, int(std::lround(letra * k))));
		f.setHintingPreference(QFont::PreferNoHinting);
		return f;
	};
	auto cor = [&](int i) { return e.serie.empty() ? tinta : e.serie[size_t(i) % e.serie.size()]; };
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
			p.setPen(QPen(e.fundo, 1.5));
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
	const double passo = grPassoBonito(mx - mn, 5);
	mn = std::floor(mn / passo) * passo;
	mx = std::ceil(mx / passo) * passo;
	const bool deitado = g.tipo == TipoGrafico::Barras;
	p.setFont(F(0.9));
	const QFontMetricsF fm(p.font());
	double largEixo = 0;
	for (double v = mn; v <= mx + passo / 2; v += passo)
		largEixo = std::max(largEixo, fm.horizontalAdvance(grNumeroCurto(v)));
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
			p.drawText(QRectF(q - 40, plot.bottom() + 2, 80, letra * 1.7), Qt::AlignHCenter | Qt::AlignTop, grNumeroCurto(v));
		else
			p.drawText(QRectF(area.left(), q - letra, largEixo + 2, letra * 2), Qt::AlignRight | Qt::AlignVCenter, grNumeroCurto(v));
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

// SPDX-License-Identifier: BSD-2-Clause
//
// dados.h - os ativos e as cotações (os símbolos, o histórico, as contas de
// retorno, correlação e beta) dos Mercados do LumeBSD.
//
// As cotações vêm do serviço público de gráficos do Yahoo Finance: de graça
// e com atraso (em geral 15 minutos nas bolsas), sem contrato. Se ele mudar,
// só este arquivo muda.

#pragma once

#include "base.h"

// --- Os símbolos -------------------------------------------------------------

// O que a pessoa digita vira o símbolo da fonte: PETR4 -> PETR4.SA,
// USDBRL -> USDBRL=X, IBOV -> ^BVSP, OURO -> GC=F, BTC -> BTC-USD.
static const struct {
	const char *apelido, *simbolo, *nome;
} APELIDOS[] = {
	{"IBOV", "^BVSP", "Ibovespa"},
	{"IBOVESPA", "^BVSP", "Ibovespa"},
	{"SPX", "^GSPC", "S&P 500"},
	{"SP500", "^GSPC", "S&P 500"},
	{"NASDAQ", "^IXIC", "Nasdaq"},
	{"DOW", "^DJI", "Dow Jones"},
	{"VIX", "^VIX", "VIX (volatilidade do S&P 500)"},
	{"DOLAR", "USDBRL=X", "Dólar"},
	{"EURO", "EURBRL=X", "Euro"},
	{"DXY", "DX-Y.NYB", "Índice do dólar"},
	{"OURO", "GC=F", "Ouro"},
	{"PRATA", "SI=F", "Prata"},
	{"COBRE", "HG=F", "Cobre"},
	{"BRENT", "BZ=F", "Petróleo Brent"},
	{"PETROLEO", "BZ=F", "Petróleo Brent"},
	{"WTI", "CL=F", "Petróleo WTI"},
	{"GAS", "NG=F", "Gás natural"},
	{"SOJA", "ZS=F", "Soja"},
	{"MILHO", "ZC=F", "Milho"},
	{"CAFE", "KC=F", "Café"},
	{"ACUCAR", "SB=F", "Açúcar"},
	{"BOI", "LE=F", "Boi gordo (Chicago)"},
	{"BTC", "BTC-USD", "Bitcoin"},
	{"BITCOIN", "BTC-USD", "Bitcoin"},
	{"ETH", "ETH-USD", "Ethereum"},
	{"US10", "^TNX", "Juros dos EUA, 10 anos"},
	{"US30", "^TYX", "Juros dos EUA, 30 anos"},
};

static const char *const MOEDAS[] = {"BRL", "USD", "EUR", "GBP", "JPY", "CHF", "CNY", "ARS",
				     "MXN", "CAD", "AUD", "CLP", "COP", "PYG", "UYU"};

static bool
ehMoeda(const QString &t)
{
	for (const char *m : MOEDAS)
		if (t == QLatin1String(m))
			return true;
	return false;
}

static QString
simboloDe(QString t)
{
	t = t.trimmed().toUpper();
	for (const auto &a : APELIDOS)
		if (t == QLatin1String(a.apelido))
			return QString::fromLatin1(a.simbolo);
	static const QRegularExpression b3(QStringLiteral("^[A-Z]{4}\\d{1,2}$"));
	if (b3.match(t).hasMatch())
		return t + ".SA";
	if (t.size() == 6 && ehMoeda(t.left(3)) && ehMoeda(t.mid(3)))
		return t + "=X";
	return t;
}

// O símbolo como a pessoa escreve: sem o .SA e o =X.
static QString
curto(QString s)
{
	for (const auto &a : APELIDOS)
		if (s == QLatin1String(a.simbolo))
			return QString::fromLatin1(a.apelido);
	if (s.endsWith(".SA"))
		s.chop(3);
	else if (s.endsWith("=X"))
		s.chop(2);
	return s;
}

static QString
nomeApelido(const QString &simbolo)
{
	for (const auto &a : APELIDOS)
		if (simbolo == QLatin1String(a.simbolo))
			return QString::fromUtf8(a.nome);
	return QString();
}

// --- As cotações ---------------------------------------------------------------

struct Cotacao {
	bool ok = false;
	QString simbolo, nome, moeda, bolsa, tipo;
	double preco = NAO, anterior = NAO, max52 = NAO, min52 = NAO, maxDia = NAO, minDia = NAO, volume = NAO;
	qint64 hora = 0;
	QVector<qint64> t;
	QVector<double> c, v;	// fechamentos (NaN onde faltou) e volumes
	QVector<QPair<qint64, double>> dividendos;

	double dia() const { return preco / anterior - 1; }
	// A variação de `pregoes` pregões atrás até agora.
	double desde(int pregoes) const
	{
		int k = c.size() - 1, n = 0;
		for (; k >= 0 && n < pregoes; k--)
			if (std::isfinite(c[k]))
				n++;
		for (; k >= 0; k--)
			if (std::isfinite(c[k]))
				return preco / c[k] - 1;
		return NAO;
	}
	QString titulo() const
	{
		const QString n = nomeApelido(simbolo);
		return n.isEmpty() ? (nome.isEmpty() ? curto(simbolo) : nome) : n;
	}
};

static Cotacao
lerSerie(const QJsonObject &o)
{
	Cotacao q;
	const QJsonObject m = o.value("meta").toObject();
	q.simbolo = m.value("symbol").toString();
	q.nome = m.value("longName").toString();
	if (q.nome.isEmpty())
		q.nome = m.value("shortName").toString().simplified();
	q.moeda = m.value("currency").toString();
	q.bolsa = m.value("fullExchangeName").toString();
	q.tipo = m.value("instrumentType").toString();
	auto n = [&](const char *k) { return m.contains(k) ? m.value(k).toDouble() : NAO; };
	q.preco = n("regularMarketPrice");
	q.max52 = n("fiftyTwoWeekHigh");
	q.min52 = n("fiftyTwoWeekLow");
	q.maxDia = n("regularMarketDayHigh");
	q.minDia = n("regularMarketDayLow");
	q.volume = n("regularMarketVolume");
	q.hora = qint64(m.value("regularMarketTime").toDouble());
	for (const QJsonValue &v : o.value("timestamp").toArray())
		q.t << qint64(v.toDouble());
	const QJsonObject ind = o.value("indicators").toObject().value("quote").toArray().at(0).toObject();
	for (const QJsonValue &v : ind.value("close").toArray())
		q.c << (v.isDouble() ? v.toDouble() : NAO);
	for (const QJsonValue &v : ind.value("volume").toArray())
		q.v << (v.isDouble() ? v.toDouble() : NAO);
	while (q.c.size() < q.t.size())
		q.c << NAO;
	while (q.v.size() < q.t.size())
		q.v << NAO;
	const QJsonObject div = o.value("events").toObject().value("dividends").toObject();
	for (auto it = div.begin(); it != div.end(); ++it)
		q.dividendos << qMakePair(qint64(it.value().toObject().value("date").toDouble()),
		    it.value().toObject().value("amount").toDouble());
	std::sort(q.dividendos.begin(), q.dividendos.end());
	// O fechamento anterior: o que a fonte diz, ou o penúltimo da série.
	if (m.contains("regularMarketChangePercent"))
		q.anterior = q.preco / (1 + m.value("regularMarketChangePercent").toDouble() / 100);
	else if (m.contains("previousClose"))
		q.anterior = m.value("previousClose").toDouble();
	else {
		int vistos = 0;
		for (int k = q.c.size() - 1; k >= 0; k--)
			if (std::isfinite(q.c[k]) && ++vistos == 2) {
				q.anterior = q.c[k];
				break;
			}
		if (!std::isfinite(q.anterior))
			q.anterior = n("chartPreviousClose");
	}
	q.ok = std::isfinite(q.preco) && !q.simbolo.isEmpty();
	return q;
}

static const char *FONTE_COTACOES = "Cotações: Yahoo Finance, com atraso (em geral 15 minutos nas bolsas).";

static QString
urlHistorico(const QString &simbolo, const QString &periodo, const QString &intervalo)
{
	return QStringLiteral("https://query1.finance.yahoo.com/v8/finance/chart/%1?range=%2&interval=%3&events=div,splits")
	    .arg(QString::fromLatin1(QUrl::toPercentEncoding(simbolo)), periodo, intervalo);
}

static QString
urlVarios(const QStringList &simbolos, const QString &periodo, const QString &intervalo)
{
	QStringList s;
	for (const QString &x : simbolos)
		s << QString::fromLatin1(QUrl::toPercentEncoding(x));
	return QStringLiteral("https://query1.finance.yahoo.com/v7/finance/spark?symbols=%1&range=%2&interval=%3")
	    .arg(s.join(","), periodo, intervalo);
}

static int
validadeDe(const QString &periodo)
{
	return periodo == "1d" || periodo == "5d" ? 60 : periodo == "1mo" || periodo == "3mo" ? 300 : 3600;
}

// Um ativo, com o histórico e os dividendos.
static void
buscarHistorico(const QString &simbolo, const QString &periodo, const QString &intervalo, QObject *dono,
    const std::function<void(const Cotacao &, const QString &)> &f)
{
	buscar(urlHistorico(simbolo, periodo, intervalo), validadeDe(periodo), dono,
	    [f, simbolo](const QByteArray &d, const QString &aviso) {
		const QJsonArray r = QJsonDocument::fromJson(d).object().value("chart").toObject().value("result").toArray();
		Cotacao q = r.isEmpty() ? Cotacao() : lerSerie(r.at(0).toObject());
		q.simbolo = simbolo;
		if (!q.ok && aviso.isEmpty()) {
			f(q, QStringLiteral("Não achei o ativo “%1”. Ações da B3 são como PETR4; dos EUA, como AAPL; "
			    "moedas, como USDBRL.").arg(curto(simbolo)));
			return;
		}
		f(q, aviso);
	});
}

// Vários ativos de uma vez (de dez em dez), cada um com o seu histórico.
static void
buscarVarios(const QStringList &simbolos, const QString &periodo, const QString &intervalo, QObject *dono,
    const std::function<void(const QMap<QString, Cotacao> &, const QString &)> &f)
{
	struct Estado {
		QMap<QString, Cotacao> r;
		QString aviso;
		int faltam = 0;
	};
	auto e = std::make_shared<Estado>();
	QVector<QStringList> lotes;
	for (int k = 0; k < simbolos.size(); k += 10)
		lotes << simbolos.mid(k, 10);
	e->faltam = lotes.size();
	if (lotes.isEmpty()) {
		QTimer::singleShot(0, dono, [f, e] { f(e->r, QString()); });
		return;
	}
	for (const QStringList &l : lotes)
		buscar(urlVarios(l, periodo, intervalo), validadeDe(periodo), dono, [f, e](const QByteArray &d, const QString &aviso) {
			for (const QJsonValue &v : QJsonDocument::fromJson(d).object().value("spark").toObject().value("result").toArray()) {
				Cotacao q = lerSerie(v.toObject().value("response").toArray().at(0).toObject());
				// A fonte troca o nome de alguns (USDBRL=X vira BRL=X): fica o pedido.
				q.simbolo = v.toObject().value("symbol").toString();
				if (q.ok)
					e->r[q.simbolo] = q;
			}
			if (!aviso.isEmpty())
				e->aviso = aviso;
			if (--e->faltam == 0)
				f(e->r, e->aviso);
		});
}

// --- As contas ---------------------------------------------------------------

namespace contas {

// Os retornos (logarítmicos) dos dias em que os dois ativos têm preço.
static void
alinhar(const Cotacao &a, const Cotacao &b, QVector<double> *ra, QVector<double> *rb)
{
	QMap<qint64, double> pb;
	for (int k = 0; k < b.t.size(); k++)
		if (std::isfinite(b.c[k]))
			pb[b.t[k] / 86400] = b.c[k];
	double ua = NAO, ub = NAO;
	for (int k = 0; k < a.t.size(); k++) {
		const auto it = pb.constFind(a.t[k] / 86400);
		if (!std::isfinite(a.c[k]) || it == pb.constEnd())
			continue;
		if (std::isfinite(ua) && ua > 0 && ub > 0 && a.c[k] > 0 && *it > 0) {
			*ra << std::log(a.c[k] / ua);
			*rb << std::log(*it / ub);
		}
		ua = a.c[k];
		ub = *it;
	}
}

struct Relacao {
	int n = 0;
	double correlacao = NAO, beta = NAO, alfa = NAO, r2 = NAO;
};

// y = alfa + beta·x, por mínimos quadrados.
static Relacao
relacionar(const QVector<double> &y, const QVector<double> &x)
{
	Relacao r;
	r.n = std::min(x.size(), y.size());
	if (r.n < 10)
		return r;
	double mx = 0, my = 0;
	for (int i = 0; i < r.n; i++) {
		mx += x[i];
		my += y[i];
	}
	mx /= r.n;
	my /= r.n;
	double sxy = 0, sxx = 0, syy = 0;
	for (int i = 0; i < r.n; i++) {
		sxy += (x[i] - mx) * (y[i] - my);
		sxx += (x[i] - mx) * (x[i] - mx);
		syy += (y[i] - my) * (y[i] - my);
	}
	if (sxx <= 0 || syy <= 0)
		return r;
	r.correlacao = sxy / std::sqrt(sxx * syy);
	r.beta = sxy / sxx;
	r.alfa = my - r.beta * mx;
	r.r2 = r.correlacao * r.correlacao;
	return r;
}

static Relacao
relacionar(const Cotacao &y, const Cotacao &x)
{
	QVector<double> ry, rx;
	alinhar(y, x, &ry, &rx);
	return relacionar(ry, rx);
}

// A volatilidade anualizada (252 pregões) e a maior queda do pico ao fundo.
static double
volatilidade(const Cotacao &q, double porAno = 252)
{
	QVector<double> r;
	double u = NAO;
	for (double c : q.c) {
		if (!std::isfinite(c) || c <= 0)
			continue;
		if (std::isfinite(u))
			r << std::log(c / u);
		u = c;
	}
	if (r.size() < 5)
		return NAO;
	double m = 0, s = 0;
	for (double x : r)
		m += x;
	m /= r.size();
	for (double x : r)
		s += (x - m) * (x - m);
	return std::sqrt(s / (r.size() - 1) * porAno);
}

static double
maiorQueda(const Cotacao &q)
{
	double pico = NAO, pior = 0;
	for (double c : q.c) {
		if (!std::isfinite(c))
			continue;
		if (!std::isfinite(pico) || c > pico)
			pico = c;
		pior = std::min(pior, c / pico - 1);
	}
	return pior;
}

static QVector<double>
mediaMovel(const QVector<double> &c, int n)
{
	QVector<double> r(c.size(), NAO);
	QVector<double> janela;
	double soma = 0;
	for (int k = 0; k < c.size(); k++) {
		if (!std::isfinite(c[k]))
			continue;
		janela << c[k];
		soma += c[k];
		if (janela.size() > n) {
			soma -= janela.first();
			janela.removeFirst();
		}
		if (janela.size() == n)
			r[k] = soma / n;
	}
	return r;
}

// --- Títulos de renda fixa ---

struct Titulo {
	double preco = NAO, duracao = NAO, modificada = NAO, convexidade = NAO, dv01 = NAO, corrente = NAO;
};

// Um título com cupom fixo: face, cupom ao ano, `vezes` pagamentos por ano,
// `anos` até o vencimento, a taxa (ao ano, capitalizada a cada pagamento,
// a convenção dos títulos americanos).
static Titulo
titulo(double face, double cupom, int vezes, double anos, double taxa)
{
	Titulo t;
	const int n = std::max(1, int(std::lround(anos * vezes)));
	const double y = taxa / vezes, c = face * cupom / vezes;
	double preco = 0, dur = 0, conv = 0;
	for (int k = 1; k <= n; k++) {
		const double fluxo = c + (k == n ? face : 0);
		const double vp = fluxo / std::pow(1 + y, k);
		preco += vp;
		dur += vp * k / vezes;
		conv += vp * k * (k + 1);
	}
	t.preco = preco;
	t.duracao = dur / preco;
	t.modificada = t.duracao / (1 + y);
	t.convexidade = conv / (preco * (1 + y) * (1 + y) * vezes * vezes);
	t.dv01 = t.modificada * preco * 0.0001;
	t.corrente = face * cupom / preco;
	return t;
}

// A taxa que dá o preço (bisseção: o preço cai quando a taxa sobe).
static double
taxaDoTitulo(double face, double cupom, int vezes, double anos, double preco)
{
	double a = -0.9 * vezes, b = 10;
	a = std::max(a, -0.99);
	for (int k = 0; k < 200; k++) {
		const double m = (a + b) / 2;
		if (titulo(face, cupom, vezes, anos, m).preco > preco)
			a = m;
		else
			b = m;
	}
	return (a + b) / 2;
}

} // namespace contas

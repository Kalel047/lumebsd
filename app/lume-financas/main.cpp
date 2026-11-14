// SPDX-License-Identifier: BSD-2-Clause
//
// lume-financas - "Finanças do LumeBSD": as contas de economia e finanças
// de quem estuda, ensina e trabalha com elas, e de quem só quer saber
// quanto vai pagar no financiamento. Um livro de cinco capítulos:
//
//   1 Juros e taxas          o valor do dinheiro no tempo (n, i, VP, PMT,
//                            VF, como na HP 12C), taxas equivalentes,
//                            nominal e efetiva, juros reais (Fisher)
//   2 Financiamento          Price e SAC lado a lado, a tabela mês a mês,
//                            o custo efetivo total (CET) com seguro e tarifas
//   3 Projetos               fluxo de caixa: VPL, TIR, TIRM, índice de
//                            lucratividade, paybacks, o perfil do VPL
//   4 Inflação e índices     corrigir um valor pelo IPCA, INPC, IGP-M,
//                            Selic ou CDI, e os indicadores do dia, com os
//                            dados do Banco Central (SGS)
//   5 Estatística            uma tabela (colada da planilha ou um CSV), a
//                            estatística descritiva, as correlações e a
//                            regressão por MQO, com erros-padrão, t, p,
//                            R², F e Durbin-Watson; em log, as elasticidades
//
// A linha de comando faz as mesmas contas sem janela (e é como os testes
// conferem os números):
//
//   lume-financas --price VALOR TAXA% MESES
//   lume-financas --sac VALOR TAXA% MESES
//   lume-financas --projeto TMA% F0 F1 F2 ...
//   lume-financas --taxa TAXA% DE PARA        (dia, util, mes, bimestre,
//                                             trimestre, semestre, ano)
//   lume-financas --corrigir INDICE VALOR MM/AAAA MM/AAAA
//   lume-financas --indicadores
//   lume-financas --regressao ARQUIVO.csv Y X1 [X2 ...] [--log]
//
// Os dados do Banco Central vêm da API do SGS pelo fetch(1) (ou o curl,
// fora do FreeBSD) e ficam guardados em ~/.cache/lume-financas, para o app
// funcionar sem internet com o que já buscou. LUME_FINANCAS_SGS=PASTA lê
// as séries de PASTA/CODIGO.json (os testes, sem rede).
//
// Sem moc: só sinais do próprio Qt ligados a lambdas, para compilar com o
// clang de fora do FreeBSD contra os cabeçalhos do pacote qt6-base.

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSessionManager>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>

static const double NAO = std::numeric_limits<double>::quiet_NaN();

// --- Números em português ---------------------------------------------------

static const QLocale &
br()
{
	static const QLocale l(QLocale::Portuguese, QLocale::Brazil);
	return l;
}

// Lê "1.234,56", "1234.56", "R$ 1.000", "12,5%" e "−3". Um ponto seguido
// de três algarismos, sem vírgula, é separador de milhar ("1.000" é mil),
// como se escreve no Brasil.
static double
lerNumero(QString t, bool *ok = nullptr)
{
	t = t.trimmed();
	t.remove(QStringLiteral("R$"));
	t.remove(QLatin1Char('%'));
	t.remove(QLatin1Char(' '));
	t.remove(QChar(0x00a0));
	t.remove(QChar(0x202f));
	t.replace(QChar(0x2212), QLatin1Char('-'));
	static const QRegularExpression milhar(QStringLiteral("^[-+]?\\d{1,3}(\\.\\d{3})+$"));
	if (t.contains(QLatin1Char(','))) {
		t.remove(QLatin1Char('.'));
		t.replace(QLatin1Char(','), QLatin1Char('.'));
	} else if (milhar.match(t).hasMatch())
		t.remove(QLatin1Char('.'));
	bool k = false;
	const double v = t.toDouble(&k);
	k = k && std::isfinite(v);
	if (ok)
		*ok = k;
	return k ? v : 0;
}

static QString
num(double v, int casas = 2)
{
	if (std::isnan(v))
		return QStringLiteral("—");
	if (std::isinf(v))
		return v > 0 ? QStringLiteral("∞") : QStringLiteral("−∞");
	if (std::fabs(v) < 0.5 * std::pow(10.0, -casas))
		v = 0;	// sem "-0,00"
	QString s = br().toString(v, 'f', casas);
	s.replace(QLatin1Char('-'), QChar(0x2212));
	return s;
}

static QString
moeda(double v)
{
	if (!std::isfinite(v))
		return num(v);
	return (v <= -0.005 ? QStringLiteral("−R$ ") : QStringLiteral("R$ ")) + num(std::fabs(v));
}

static QString
pct(double fracao, int casas = 2)
{
	if (!std::isfinite(fracao))
		return num(fracao);
	return num(fracao * 100, casas) + QStringLiteral("%");
}

// Para planilhas em português: vírgula decimal, sem milhar.
static QString
csvNum(double v, int casas = 2)
{
	QLocale l = br();
	l.setNumberOptions(QLocale::OmitGroupSeparator);
	return std::isfinite(v) ? l.toString(v, 'f', casas) : QString();
}

static const char *const MESES[] = {"jan", "fev", "mar", "abr", "mai", "jun",
				    "jul", "ago", "set", "out", "nov", "dez"};

static QString
mesAno(const QDate &d)
{
	return QStringLiteral("%1/%2").arg(QLatin1String(MESES[d.month() - 1])).arg(d.year());
}

static QDate
lerMesAno(const QString &t)
{
	QDate d = QDate::fromString(t.trimmed(), QStringLiteral("MM/yyyy"));
	if (!d.isValid())
		d = QDate::fromString(t.trimmed(), QStringLiteral("M/yyyy"));
	return d.isValid() ? QDate(d.year(), d.month(), 1) : QDate();
}

// --- As contas ---------------------------------------------------------------

namespace contas {

// O valor do dinheiro no tempo, na convenção da HP 12C: o que sai do bolso
// é negativo, o que entra é positivo, e a soma, levada ao fim, dá zero:
//   VP·(1+i)^n + PMT·(1+i·b)·((1+i)^n − 1)/i + VF = 0   (b = 1 no início)
struct Tvm {
	double n = NAO, i = NAO, vp = NAO, pmt = NAO, vf = NAO;
	bool inicio = false;
};
enum Incognita { N, I, VP, PMT, VF };

static double
tvmResto(double n, double i, double vp, double pmt, double vf, bool inicio)
{
	if (std::fabs(i) < 1e-12)
		return vp + pmt * n + vf;
	const double q = std::pow(1 + i, n);
	return vp * q + pmt * (1 + (inicio ? i : 0)) * (q - 1) / i + vf;
}

static const char *SINAIS =
    "Com esses valores a conta não fecha. Confira os sinais: o que sai do "
    "bolso é negativo e o que entra é positivo (um empréstimo: VP positivo, "
    "parcelas negativas).";

// Acha a raiz de f entre a e b (f(a) e f(b) com sinais trocados).
static double
bissecao(const std::function<double(double)> &f, double a, double b)
{
	double fa = f(a);
	for (int k = 0; k < 300; k++) {
		const double m = (a + b) / 2, fm = f(m);
		if (fm == 0 || (b - a) / 2 < 1e-15)
			return m;
		if ((fm < 0) == (fa < 0)) {
			a = m;
			fa = fm;
		} else
			b = m;
	}
	return (a + b) / 2;
}

// As taxas onde f troca de sinal, entre −99% e 1000% por período, em ordem.
static QVector<double>
raizes(const std::function<double(double)> &f)
{
	QVector<double> grade;
	for (int k = 0; k < 300; k++)	// −0,99 … −1e−7
		grade << -0.99 * std::pow(1e-7 / 0.99, k / 299.0);
	grade << 0.0;
	for (int k = 0; k < 600; k++)	// 1e−7 … 10
		grade << 1e-7 * std::pow(10 / 1e-7, k / 599.0);
	QVector<double> r;
	double xa = grade[0], fa = f(xa);
	for (int k = 1; k < grade.size(); k++) {
		const double xb = grade[k], fb = f(xb);
		if (std::isfinite(fa) && std::isfinite(fb)) {
			if (fa == 0)
				r << xa;
			else if ((fa < 0) != (fb < 0) && fb != 0)
				r << bissecao(f, xa, xb);
		}
		xa = xb;
		fa = fb;
	}
	if (std::isfinite(fa) && fa == 0)
		r << xa;
	return r;
}

static bool
resolverTvm(Tvm &t, Incognita x, QString *erro)
{
	const double k = 1 + (t.inicio && std::isfinite(t.i) ? t.i : 0);
	auto falta = [&](double v) { return !std::isfinite(v); };
	switch (x) {
	case VF:
	case VP:
	case PMT:
		if (falta(t.n) || falta(t.i) || (x != VP && falta(t.vp)) ||
		    (x != PMT && falta(t.pmt)) || (x != VF && falta(t.vf))) {
			*erro = "Preencha os outros quatro campos.";
			return false;
		}
		break;
	case N:
		if (falta(t.i) || falta(t.vp) || falta(t.pmt) || falta(t.vf)) {
			*erro = "Preencha i, VP, PMT e VF.";
			return false;
		}
		break;
	case I:
		if (falta(t.n) || falta(t.vp) || falta(t.pmt) || falta(t.vf)) {
			*erro = "Preencha n, VP, PMT e VF.";
			return false;
		}
		break;
	}
	if (x != I && t.i <= -1) {
		*erro = "A taxa precisa ser maior que −100%.";
		return false;
	}
	const bool zero = x != I && std::fabs(t.i) < 1e-12;
	switch (x) {
	case VF:
		if (zero)
			t.vf = -(t.vp + t.pmt * t.n);
		else {
			const double q = std::pow(1 + t.i, t.n);
			t.vf = -(t.vp * q + t.pmt * k * (q - 1) / t.i);
		}
		return true;
	case VP:
		if (zero)
			t.vp = -(t.vf + t.pmt * t.n);
		else {
			const double q = std::pow(1 + t.i, t.n);
			t.vp = -(t.vf + t.pmt * k * (q - 1) / t.i) / q;
		}
		return true;
	case PMT:
		if (t.n == 0) {
			*erro = "Com n = 0 não há parcelas.";
			return false;
		}
		if (zero)
			t.pmt = -(t.vp + t.vf) / t.n;
		else {
			const double q = std::pow(1 + t.i, t.n);
			t.pmt = -(t.vp * q + t.vf) / (k * (q - 1) / t.i);
		}
		return true;
	case N: {
		if (zero) {
			if (t.pmt == 0) {
				*erro = "Sem juros e sem parcelas, o dinheiro não muda: n não existe.";
				return false;
			}
			t.n = -(t.vp + t.vf) / t.pmt;
		} else {
			const double a = t.pmt * k / t.i;
			const double q = (a - t.vf) / (t.vp + a);
			if (!(q > 0) || !std::isfinite(q)) {
				*erro = SINAIS;
				return false;
			}
			t.n = std::log(q) / std::log(1 + t.i);
		}
		if (!(t.n >= 0) || !std::isfinite(t.n)) {
			*erro = SINAIS;
			return false;
		}
		return true;
	}
	case I: {
		if (t.n <= 0) {
			*erro = "n precisa ser maior que zero.";
			return false;
		}
		const Tvm c = t;
		QVector<double> r = raizes([&](double i) {
			return tvmResto(c.n, i, c.vp, c.pmt, c.vf, c.inicio);
		});
		if (r.isEmpty()) {
			*erro = SINAIS;
			return false;
		}
		// A primeira não negativa, se houver (a que a HP mostra).
		t.i = r.first();
		for (double v : r)
			if (v >= -1e-12) {
				t.i = v;
				break;
			}
		return true;
	}
	}
	return false;
}

// Os períodos das taxas, em anos (o ano comercial: 360 dias corridos ou
// 252 dias úteis, como no CDI e na Selic).
struct Periodo {
	const char *id, *nome;
	double anos;
};
static const Periodo PERIODOS[] = {
	{"dia", "ao dia (corrido)", 1.0 / 360},
	{"util", "ao dia útil", 1.0 / 252},
	{"mes", "ao mês", 1.0 / 12},
	{"bimestre", "ao bimestre", 1.0 / 6},
	{"trimestre", "ao trimestre", 1.0 / 4},
	{"semestre", "ao semestre", 1.0 / 2},
	{"ano", "ao ano", 1.0},
};

static double
equivalente(double i, double anosDe, double anosPara)
{
	return std::pow(1 + i, anosPara / anosDe) - 1;
}

// A equação de Fisher: (1 + nominal) = (1 + real)·(1 + inflação).
static double
real(double nominal, double inflacao)
{
	return (1 + nominal) / (1 + inflacao) - 1;
}

// --- Financiamento ---

struct Linha {
	int k;
	double prestacao, juros, amortizacao, saldo;
};

static QVector<Linha>
price(double vp, double i, int n)
{
	QVector<Linha> r;
	if (n <= 0)
		return r;
	const double pmt = std::fabs(i) < 1e-12 ? vp / n : vp * i / (1 - std::pow(1 + i, -n));
	double saldo = vp;
	for (int k = 1; k <= n; k++) {
		const double j = saldo * i;
		double a = pmt - j;
		if (k == n)
			a = saldo;	// o último centavo
		saldo -= a;
		r.push_back({k, a + j, j, a, std::fabs(saldo) < 1e-6 ? 0 : saldo});
	}
	return r;
}

static QVector<Linha>
sac(double vp, double i, int n)
{
	QVector<Linha> r;
	if (n <= 0)
		return r;
	const double a = vp / n;
	double saldo = vp;
	for (int k = 1; k <= n; k++) {
		const double j = saldo * i;
		saldo -= a;
		r.push_back({k, a + j, j, a, std::fabs(saldo) < 1e-6 ? 0 : saldo});
	}
	return r;
}

// --- Fluxo de caixa ---

static double
vpl(const QVector<double> &f, double r)
{
	double s = 0, d = 1;
	for (double x : f) {
		s += x / d;
		d *= 1 + r;
	}
	return s;
}

static QVector<double>
tirs(const QVector<double> &f)
{
	QVector<double> r = raizes([&](double t) { return vpl(f, t); });
	QVector<double> u;
	for (double v : r)
		if (u.isEmpty() || std::fabs(v - u.last()) > 1e-7)
			u << v;
	return u;
}

// A TIR modificada: as saídas trazidas a valor presente pela taxa de
// financiamento, as entradas levadas ao fim pela de reinvestimento.
static double
tirm(const QVector<double> &f, double financiamento, double reinvestimento)
{
	const int n = f.size() - 1;
	if (n < 1)
		return NAO;
	double vpSaidas = 0, vfEntradas = 0;
	for (int t = 0; t <= n; t++)
		if (f[t] < 0)
			vpSaidas += f[t] / std::pow(1 + financiamento, t);
		else
			vfEntradas += f[t] * std::pow(1 + reinvestimento, n - t);
	if (vpSaidas >= 0 || vfEntradas <= 0)
		return NAO;
	return std::pow(vfEntradas / -vpSaidas, 1.0 / n) - 1;
}

// Em quantos períodos o acumulado fica positivo de vez (com fração, por
// interpolação no período em que vira); NaN se termina negativo.
static double
payback(const QVector<double> &f, double r)
{
	double acum = 0, d = 1, quando = 0;
	for (int t = 0; t < f.size(); t++) {
		const double x = f[t] / d;
		if (acum < 0 && acum + x >= 0)
			quando = t - 1 + (-acum) / x;
		acum += x;
		d *= 1 + r;
	}
	return acum < 0 ? NAO : quando;
}

static double
indiceLucratividade(const QVector<double> &f, double r)
{
	double entradas = 0, saidas = 0, d = 1;
	for (double x : f) {
		(x >= 0 ? entradas : saidas) += x / d;
		d *= 1 + r;
	}
	return saidas < 0 ? entradas / -saidas : NAO;
}

// --- Estatística ---

static double
betacf(double a, double b, double x)
{
	const double MINIMO = 1e-300;
	double qab = a + b, qap = a + 1, qam = a - 1, c = 1, d = 1 - qab * x / qap;
	if (std::fabs(d) < MINIMO)
		d = MINIMO;
	d = 1 / d;
	double h = d;
	for (int m = 1; m <= 300; m++) {
		const int m2 = 2 * m;
		double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
		d = 1 + aa * d;
		if (std::fabs(d) < MINIMO)
			d = MINIMO;
		c = 1 + aa / c;
		if (std::fabs(c) < MINIMO)
			c = MINIMO;
		d = 1 / d;
		h *= d * c;
		aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
		d = 1 + aa * d;
		if (std::fabs(d) < MINIMO)
			d = MINIMO;
		c = 1 + aa / c;
		if (std::fabs(c) < MINIMO)
			c = MINIMO;
		d = 1 / d;
		const double del = d * c;
		h *= del;
		if (std::fabs(del - 1) < 1e-14)
			break;
	}
	return h;
}

// A beta incompleta regularizada I_x(a, b).
static double
betai(double a, double b, double x)
{
	if (x <= 0)
		return 0;
	if (x >= 1)
		return 1;
	const double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
	    a * std::log(x) + b * std::log(1 - x));
	if (x < (a + 1) / (a + b + 2))
		return bt * betacf(a, b, x) / a;
	return 1 - bt * betacf(b, a, 1 - x) / b;
}

// O p-valor bicaudal de t com gl graus de liberdade, e o de F(d1, d2).
static double
pT(double t, double gl)
{
	if (!std::isfinite(t))
		return std::isnan(t) ? NAO : 0;
	return betai(gl / 2, 0.5, gl / (gl + t * t));
}

static double
pF(double f, double d1, double d2)
{
	if (!std::isfinite(f))
		return std::isnan(f) ? NAO : 0;
	if (f <= 0)
		return 1;
	return betai(d2 / 2, d1 / 2, d2 / (d2 + d1 * f));
}

// Inverte a matriz (Gauss-Jordan com pivô); false se for singular.
static bool
inverter(QVector<QVector<double>> &a)
{
	const int n = a.size();
	QVector<QVector<double>> inv(n, QVector<double>(n, 0));
	for (int i = 0; i < n; i++)
		inv[i][i] = 1;
	double escala = 0;
	for (const auto &l : a)
		for (double v : l)
			escala = std::max(escala, std::fabs(v));
	for (int c = 0; c < n; c++) {
		int p = c;
		for (int l = c + 1; l < n; l++)
			if (std::fabs(a[l][c]) > std::fabs(a[p][c]))
				p = l;
		if (std::fabs(a[p][c]) <= 1e-12 * std::max(escala, 1.0))
			return false;
		std::swap(a[p], a[c]);
		std::swap(inv[p], inv[c]);
		const double d = a[c][c];
		for (int k = 0; k < n; k++) {
			a[c][k] /= d;
			inv[c][k] /= d;
		}
		for (int l = 0; l < n; l++)
			if (l != c && a[l][c] != 0) {
				const double f = a[l][c];
				for (int k = 0; k < n; k++) {
					a[l][k] -= f * a[c][k];
					inv[l][k] -= f * inv[c][k];
				}
			}
	}
	a = inv;
	return true;
}

struct Regressao {
	QString erro;
	int n = 0, k = 0;
	QStringList nomes;
	QVector<double> b, ep, t, p, ajustados, residuos;
	double r2 = NAO, r2a = NAO, f = NAO, pf = NAO, s = NAO, dw = NAO;
};

// Mínimos quadrados ordinários, com constante: y = b0 + b1·x1 + … + e.
static Regressao
mqo(const QVector<double> &y, const QVector<QVector<double>> &xs, const QStringList &nomesX)
{
	Regressao r;
	r.n = y.size();
	r.k = xs.size() + 1;
	r.nomes = QStringList{QStringLiteral("const")} + nomesX;
	if (r.n <= r.k) {
		r.erro = QStringLiteral("Poucas observações: com %1 coeficientes, são precisas pelo "
		    "menos %2 linhas completas (há %3).").arg(r.k).arg(r.k + 1).arg(r.n);
		return r;
	}
	auto x = [&](int linha, int j) { return j == 0 ? 1.0 : xs[j - 1][linha]; };
	QVector<QVector<double>> xtx(r.k, QVector<double>(r.k, 0));
	QVector<double> xty(r.k, 0);
	for (int l = 0; l < r.n; l++)
		for (int a = 0; a < r.k; a++) {
			xty[a] += x(l, a) * y[l];
			for (int b = 0; b < r.k; b++)
				xtx[a][b] += x(l, a) * x(l, b);
		}
	if (!inverter(xtx)) {
		r.erro = "As variáveis explicativas são colineares (uma é combinação das "
		    "outras, ou é constante): tire uma delas.";
		return r;
	}
	r.b.fill(0, r.k);
	for (int a = 0; a < r.k; a++)
		for (int b = 0; b < r.k; b++)
			r.b[a] += xtx[a][b] * xty[b];
	double media = 0;
	for (double v : y)
		media += v;
	media /= r.n;
	double sqr = 0, sqt = 0, dwn = 0;
	for (int l = 0; l < r.n; l++) {
		double yh = 0;
		for (int a = 0; a < r.k; a++)
			yh += r.b[a] * x(l, a);
		const double e = y[l] - yh;
		r.ajustados << yh;
		r.residuos << e;
		sqr += e * e;
		sqt += (y[l] - media) * (y[l] - media);
		if (l > 0)
			dwn += (e - r.residuos[l - 1]) * (e - r.residuos[l - 1]);
	}
	if (sqt <= 0) {
		r.erro = "A variável dependente é constante: não há o que explicar.";
		return r;
	}
	const double gl = r.n - r.k;
	const double s2 = sqr / gl;
	r.s = std::sqrt(s2);
	r.r2 = 1 - sqr / sqt;
	r.r2a = 1 - (1 - r.r2) * (r.n - 1) / gl;
	for (int a = 0; a < r.k; a++) {
		const double e = std::sqrt(std::max(0.0, s2 * xtx[a][a]));
		r.ep << e;
		const double t = e > 0 ? r.b[a] / e : (r.b[a] == 0 ? NAO : INFINITY);
		r.t << t;
		r.p << pT(std::fabs(t), gl);
	}
	if (r.k > 1) {
		r.f = sqr > 0 ? ((sqt - sqr) / (r.k - 1)) / s2 : INFINITY;
		r.pf = pF(r.f, r.k - 1, gl);
	}
	r.dw = sqr > 0 ? dwn / sqr : NAO;
	return r;
}

static const char *
estrelas(double p)
{
	if (!std::isfinite(p))
		return "";
	return p < 0.01 ? "***" : p < 0.05 ? "**" : p < 0.1 ? "*" : "";
}

// O relatório da regressão, no jeito dos programas de econometria (gretl,
// Stata): para ler, copiar para o trabalho e conferir com o do professor.
static QString
relatorio(const Regressao &r, const QString &nomeY, bool logs, int descartadas)
{
	QString o;
	QTextStream s(&o);
	s << "Mínimos quadrados ordinários (MQO)\n";
	s << "Variável dependente: " << (logs ? "ln " : "") << nomeY << "\n";
	s << "Observações: " << r.n;
	if (descartadas > 0)
		s << " (" << descartadas << (descartadas == 1 ? " linha incompleta ficou" :
		    " linhas incompletas ficaram") << " de fora)";
	s << "\n";
	if (logs)
		s << "Em logaritmos: cada coeficiente é uma elasticidade (1% a mais em X, "
		    "b% a mais em Y).\n";
	s << "\n";
	int largura = 12;
	for (const QString &n : r.nomes)
		largura = std::max(largura, int(n.size()) + (logs ? 5 : 2));
	s << QString().leftJustified(largura) << QStringLiteral("coeficiente").rightJustified(14)
	  << QStringLiteral("erro-padrão").rightJustified(14) << QStringLiteral("t").rightJustified(10)
	  << QStringLiteral("p-valor").rightJustified(10) << "\n";
	for (int a = 0; a < r.k; a++) {
		const QString nome = (logs && a > 0 ? QStringLiteral("ln ") : QString()) + r.nomes[a];
		s << nome.leftJustified(largura) << num(r.b[a], 4).rightJustified(14)
		  << num(r.ep[a], 4).rightJustified(14) << num(r.t[a], 3).rightJustified(10)
		  << num(r.p[a], 4).rightJustified(10) << " " << estrelas(r.p[a]) << "\n";
	}
	s << "\n";
	s << "R²            " << num(r.r2, 4).rightJustified(12) << "    R² ajustado     "
	  << num(r.r2a, 4).rightJustified(12) << "\n";
	if (r.k > 1)
		s << QStringLiteral("F(%1, %2)").arg(r.k - 1).arg(r.n - r.k).leftJustified(14)
		  << num(r.f, 3).rightJustified(12) << "    p-valor (F)     "
		  << num(r.pf, 4).rightJustified(12) << "\n";
	s << "Erro-padrão   " << num(r.s, 4).rightJustified(12) << "    Durbin-Watson   "
	  << num(r.dw, 4).rightJustified(12) << "\n";
	s << "\n*** p < 0,01   ** p < 0,05   * p < 0,10\n";
	return o;
}

struct Descritiva {
	int n = 0;
	double media = NAO, mediana = NAO, dp = NAO, min = NAO, max = NAO;
};

static Descritiva
descrever(QVector<double> v)
{
	Descritiva d;
	d.n = v.size();
	if (v.isEmpty())
		return d;
	std::sort(v.begin(), v.end());
	double s = 0;
	for (double x : v)
		s += x;
	d.media = s / d.n;
	d.mediana = d.n % 2 ? v[d.n / 2] : (v[d.n / 2 - 1] + v[d.n / 2]) / 2;
	d.min = v.first();
	d.max = v.last();
	if (d.n > 1) {
		double q = 0;
		for (double x : v)
			q += (x - d.media) * (x - d.media);
		d.dp = std::sqrt(q / (d.n - 1));
	}
	return d;
}

static double
correlacao(const QVector<double> &a, const QVector<double> &b)
{
	const int n = std::min(a.size(), b.size());
	if (n < 2)
		return NAO;
	double ma = 0, mb = 0;
	for (int i = 0; i < n; i++) {
		ma += a[i];
		mb += b[i];
	}
	ma /= n;
	mb /= n;
	double sab = 0, saa = 0, sbb = 0;
	for (int i = 0; i < n; i++) {
		sab += (a[i] - ma) * (b[i] - mb);
		saa += (a[i] - ma) * (a[i] - ma);
		sbb += (b[i] - mb) * (b[i] - mb);
	}
	return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : NAO;
}

} // namespace contas

// --- Tabelas de dados (CSV e o que se cola da planilha) ---------------------

struct Dados {
	QStringList nomes;
	QVector<QStringList> linhas;
};

// O separador é o que mais aparece na primeira linha: tabulação (colado da
// planilha), ponto e vírgula (CSV em português) ou vírgula.
static Dados
lerTabela(const QString &texto)
{
	Dados d;
	QStringList ls = texto.split(QRegularExpression(QStringLiteral("\r?\n")));
	while (!ls.isEmpty() && ls.last().trimmed().isEmpty())
		ls.removeLast();
	if (ls.isEmpty())
		return d;
	const QString &p = ls.first();
	QChar sep = QLatin1Char('\t');
	if (p.count(QLatin1Char('\t')) == 0)
		sep = p.count(QLatin1Char(';')) >= p.count(QLatin1Char(',')) && p.count(QLatin1Char(';')) > 0
		    ? QLatin1Char(';') : QLatin1Char(',');
	auto partes = [&](const QString &l) {
		QStringList r;
		for (QString c : l.split(sep)) {
			c = c.trimmed();
			if (c.size() >= 2 && c.startsWith(QLatin1Char('"')) && c.endsWith(QLatin1Char('"')))
				c = c.mid(1, c.size() - 2);
			r << c;
		}
		return r;
	};
	QStringList primeira = partes(p);
	bool cabecalho = false;
	for (const QString &c : primeira) {
		bool ok;
		lerNumero(c, &ok);
		if (!ok && !c.isEmpty())
			cabecalho = true;
	}
	int inicio = 0;
	if (cabecalho) {
		d.nomes = primeira;
		inicio = 1;
	}
	int colunas = d.nomes.size();
	for (int i = inicio; i < ls.size(); i++) {
		if (ls[i].trimmed().isEmpty())
			continue;
		d.linhas << partes(ls[i]);
		colunas = std::max(colunas, int(d.linhas.last().size()));
	}
	for (int c = d.nomes.size(); c < colunas; c++)
		d.nomes << QStringLiteral("v%1").arg(c + 1);
	for (int c = 0; c < d.nomes.size(); c++)
		if (d.nomes[c].isEmpty())
			d.nomes[c] = QStringLiteral("v%1").arg(c + 1);
	return d;
}

// As linhas em que Y e todos os X são números (e positivos, em log).
struct Amostra {
	QVector<double> y;
	QVector<QVector<double>> xs;
	int descartadas = 0;
	QString erro;
};

static Amostra
montarAmostra(const Dados &d, int colY, const QVector<int> &colsX, bool logs)
{
	Amostra a;
	a.xs.resize(colsX.size());
	QVector<int> todas = QVector<int>{colY} + colsX;
	for (const QStringList &l : d.linhas) {
		QVector<double> v;
		bool ok = true, vazia = true;
		for (int c : todas) {
			const QString t = c < l.size() ? l[c] : QString();
			if (!t.trimmed().isEmpty())
				vazia = false;
			bool k;
			double x = lerNumero(t, &k);
			if (!k) {
				ok = false;
				continue;
			}
			if (logs) {
				if (x <= 0) {
					a.erro = QStringLiteral("O logaritmo só existe para números positivos, "
					    "e a coluna “%1” tem zero ou negativo.").arg(d.nomes[c]);
					return a;
				}
				x = std::log(x);
			}
			v << x;
		}
		if (vazia)
			continue;
		if (!ok) {
			a.descartadas++;
			continue;
		}
		a.y << v[0];
		for (int j = 0; j < colsX.size(); j++)
			a.xs[j] << v[j + 1];
	}
	return a;
}

// --- Banco Central (SGS) ----------------------------------------------------

struct Ponto {
	QDate data;
	double valor;
};

struct Indice {
	const char *id, *nome;
	int serie;
	const char *descricao;
};
static const Indice INDICES[] = {
	{"ipca", "IPCA", 433, "a inflação oficial do Brasil (IBGE)"},
	{"inpc", "INPC", 188, "a inflação das famílias com renda de até 5 salários mínimos (IBGE)"},
	{"igpm", "IGP-M", 189, "o índice dos aluguéis e de muitos contratos (FGV)"},
	{"selic", "Selic", 4390, "a taxa Selic acumulada em cada mês"},
	{"cdi", "CDI", 4391, "o CDI acumulado em cada mês (a referência da renda fixa)"},
};

static const Indice *
acharIndice(const QString &id)
{
	for (const Indice &i : INDICES)
		if (id.compare(QLatin1String(i.id), Qt::CaseInsensitive) == 0 ||
		    id.compare(QString::fromUtf8(i.nome), Qt::CaseInsensitive) == 0)
			return &i;
	return nullptr;
}

// Os indicadores do dia: o último valor de cada série.
struct Indicador {
	const char *nome;
	int serie;
	bool porcento;
	const char *detalhe;
};
static const Indicador INDICADORES[] = {
	{"Selic (meta)", 432, true, "ao ano, definida pelo Copom"},
	{"IPCA em 12 meses", 13522, true, "a inflação acumulada"},
	{"CDI", 4389, true, "ao ano (252 dias úteis)"},
	{"Dólar (PTAX)", 1, false, "venda, do Banco Central"},
};

static QVector<Ponto>
lerSgs(const QByteArray &json, bool *ok)
{
	QJsonParseError e;
	const QJsonDocument d = QJsonDocument::fromJson(json, &e);
	*ok = e.error == QJsonParseError::NoError && d.isArray();
	QVector<Ponto> r;
	if (!*ok)
		return r;
	for (const QJsonValue &v : d.array()) {
		const QJsonObject o = v.toObject();
		const QDate dt = QDate::fromString(o.value(QStringLiteral("data")).toString(),
		    QStringLiteral("dd/MM/yyyy"));
		bool k;
		const double x = o.value(QStringLiteral("valor")).toString().toDouble(&k);
		if (dt.isValid() && k)
			r.push_back({dt, x});
	}
	*ok = !r.isEmpty();
	return r;
}

using Resposta = std::function<void(const QVector<Ponto> &, const QString &aviso)>;

// Busca uma série (inteira, ou só o último valor) e chama f no fim. Guarda o
// que veio; sem rede, usa o guardado (e avisa de quando é).
static void
buscarSgs(int serie, bool ultimo, QObject *dono, const Resposta &f)
{
	if (const char *dir = getenv("LUME_FINANCAS_SGS")) {
		QFile a(QString::fromLocal8Bit(dir) + QStringLiteral("/%1.json").arg(serie));
		bool ok = false;
		QVector<Ponto> p;
		if (a.open(QIODevice::ReadOnly))
			p = lerSgs(a.readAll(), &ok);
		if (ok && ultimo)
			p = {p.last()};
		QTimer::singleShot(0, dono, [f, p, ok] {
			f(p, ok ? QString() : QStringLiteral("Sem os dados de teste."));
		});
		return;
	}
	const QString pasta = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	QDir().mkpath(pasta);
	const QString cache = pasta + QStringLiteral("/sgs-%1%2.json").arg(serie)
	    .arg(ultimo ? QStringLiteral("-ultimo") : QString());
	const QFileInfo info(cache);
	auto guardado = [cache](bool *ok) {
		QFile a(cache);
		*ok = false;
		return a.open(QIODevice::ReadOnly) ? lerSgs(a.readAll(), ok) : QVector<Ponto>();
	};
	// Fresco (menos de 6 horas): nem pergunta ao Banco Central.
	if (info.exists() && info.lastModified().secsTo(QDateTime::currentDateTime()) < 6 * 3600) {
		bool ok;
		QVector<Ponto> p = guardado(&ok);
		if (ok) {
			QTimer::singleShot(0, dono, [f, p] { f(p, QString()); });
			return;
		}
	}
	const QString url = QStringLiteral("https://api.bcb.gov.br/dados/serie/bcdata.sgs.%1/%2")
	    .arg(serie).arg(ultimo ? QStringLiteral("dados/ultimos/1?formato=json")
				   : QStringLiteral("dados?formato=json"));
	auto *p = new QProcess(dono);
	auto falhou = [f, guardado, info]() {
		bool ok;
		QVector<Ponto> v = guardado(&ok);
		if (ok)
			f(v, QStringLiteral("Sem conexão com o Banco Central: usando os dados guardados em %1.")
			    .arg(info.lastModified().toString(QStringLiteral("dd/MM/yyyy 'às' HH:mm"))));
		else
			f({}, QStringLiteral("Não deu para buscar os dados no Banco Central. "
			    "O computador está na internet?"));
	};
	QObject::connect(p, &QProcess::finished, dono, [p, cache, f, falhou](int codigo, QProcess::ExitStatus st) {
		const QByteArray saida = p->readAllStandardOutput();
		p->deleteLater();
		bool ok = false;
		QVector<Ponto> v;
		if (st == QProcess::NormalExit && codigo == 0)
			v = lerSgs(saida, &ok);
		if (!ok) {
			falhou();
			return;
		}
		QFile a(cache);
		if (a.open(QIODevice::WriteOnly | QIODevice::Truncate))
			a.write(saida);
		f(v, QString());
	});
	QObject::connect(p, &QProcess::errorOccurred, dono, [p, falhou](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart)
			return;
		p->deleteLater();
		falhou();
	});
	if (QFile::exists(QStringLiteral("/usr/bin/fetch")))
		p->start(QStringLiteral("/usr/bin/fetch"), {"-q", "-T", "25", "-o", "-", url});
	else
		p->start(QStringLiteral("curl"), {"-fsSL", "--max-time", "25", url});
}

struct Correcao {
	QString erro;
	double fator = 1;
	QVector<Ponto> meses;	// valor = o fator acumulado até aquele mês
};

// Corrige de ini a fim, os dois meses inclusos (como a Calculadora do
// Cidadão do Banco Central).
static Correcao
corrigir(const QVector<Ponto> &serie, const Indice &ind, QDate ini, QDate fim)
{
	Correcao c;
	if (!ini.isValid() || !fim.isValid() || fim < ini) {
		c.erro = "O mês final precisa ser igual ou depois do inicial.";
		return c;
	}
	QMap<QDate, double> m;
	for (const Ponto &p : serie)
		m[QDate(p.data.year(), p.data.month(), 1)] = p.valor;
	if (m.isEmpty()) {
		c.erro = "A série veio vazia.";
		return c;
	}
	const QDate primeiro = m.firstKey(), ultimo = m.lastKey();
	for (QDate d = ini; d <= fim; d = d.addMonths(1)) {
		if (!m.contains(d)) {
			if (d > ultimo)
				c.erro = QStringLiteral("O %1 de %2 ainda não foi divulgado: o último é o de %3.")
				    .arg(QString::fromUtf8(ind.nome), mesAno(d), mesAno(ultimo));
			else if (d < primeiro)
				c.erro = QStringLiteral("O %1 começa em %2.").arg(QString::fromUtf8(ind.nome), mesAno(primeiro));
			else
				c.erro = QStringLiteral("Falta o %1 de %2.").arg(QString::fromUtf8(ind.nome), mesAno(d));
			return c;
		}
		c.fator *= 1 + m[d] / 100;
		c.meses.push_back({d, c.fator});
	}
	return c;
}

// --- A linha de comando -----------------------------------------------------

static void
dizer(const QString &s)
{
	fputs(s.toUtf8().constData(), stdout);
	fputc('\n', stdout);
}

static int
uso()
{
	fputs("uso: lume-financas [--price|--sac VALOR TAXA% MESES]\n"
	      "                   [--projeto TMA% F0 F1 ...]\n"
	      "                   [--taxa TAXA% DE PARA]\n"
	      "                   [--corrigir INDICE VALOR MM/AAAA MM/AAAA]\n"
	      "                   [--indicadores]\n"
	      "                   [--regressao ARQUIVO.csv Y X1 [X2 ...] [--log]]\n", stderr);
	return 2;
}

static void
resumoFinanciamento(const QString &nome, const QVector<contas::Linha> &t)
{
	double juros = 0, total = 0;
	for (const auto &l : t) {
		juros += l.juros;
		total += l.prestacao;
	}
	dizer(QStringLiteral("%1: %2 parcelas").arg(nome).arg(t.size()));
	dizer(QStringLiteral("  primeira parcela  %1").arg(moeda(t.first().prestacao)));
	dizer(QStringLiteral("  última parcela    %1").arg(moeda(t.last().prestacao)));
	dizer(QStringLiteral("  total de juros    %1").arg(moeda(juros)));
	dizer(QStringLiteral("  total pago        %1").arg(moeda(total)));
	dizer(QStringLiteral("  mês  parcela        juros          amortização    saldo"));
	for (const auto &l : t)
		if (l.k <= 3 || l.k > t.size() - 2)
			dizer(QStringLiteral("  %1  %2 %3 %4 %5").arg(l.k, 3).arg(num(l.prestacao).rightJustified(14))
			    .arg(num(l.juros).rightJustified(14)).arg(num(l.amortizacao).rightJustified(14))
			    .arg(num(l.saldo).rightJustified(14)));
}

static const contas::Periodo *
acharPeriodo(const QString &id)
{
	for (const auto &p : contas::PERIODOS)
		if (id == QLatin1String(p.id))
			return &p;
	return nullptr;
}

static int
linhaDeComando(QCoreApplication &app, const QStringList &a)
{
	const QString cmd = a.value(1);
	auto numero = [](const QString &t, double *v) {
		bool ok;
		*v = lerNumero(t, &ok);
		return ok;
	};
	if (cmd == "--price" || cmd == "--sac") {
		double vp, i, n;
		if (a.size() != 5 || !numero(a[2], &vp) || !numero(a[3], &i) || !numero(a[4], &n) || n < 1)
			return uso();
		auto t = cmd == "--price" ? contas::price(vp, i / 100, int(n)) : contas::sac(vp, i / 100, int(n));
		resumoFinanciamento(cmd == "--price" ? QStringLiteral("Price") : QStringLiteral("SAC"), t);
		return 0;
	}
	if (cmd == "--projeto") {
		double tma;
		if (a.size() < 5 || !numero(a[2], &tma))
			return uso();
		tma /= 100;
		QVector<double> f;
		for (int k = 3; k < a.size(); k++) {
			double v;
			if (!numero(a[k], &v))
				return uso();
			f << v;
		}
		const auto r = contas::tirs(f);
		QStringList ts;
		for (double v : r)
			ts << pct(v);
		dizer(QStringLiteral("VPL                %1").arg(moeda(contas::vpl(f, tma))));
		dizer(QStringLiteral("TIR                %1").arg(ts.isEmpty() ? QStringLiteral("não existe") : ts.join(", ")));
		dizer(QStringLiteral("TIRM               %1").arg(pct(contas::tirm(f, tma, tma))));
		dizer(QStringLiteral("IL                 %1").arg(num(contas::indiceLucratividade(f, tma), 4)));
		dizer(QStringLiteral("payback simples    %1").arg(num(contas::payback(f, 0), 2)));
		dizer(QStringLiteral("payback descontado %1").arg(num(contas::payback(f, tma), 2)));
		return 0;
	}
	if (cmd == "--taxa") {
		double i;
		const contas::Periodo *de = acharPeriodo(a.value(3)), *para = acharPeriodo(a.value(4));
		if (a.size() != 5 || !numero(a[2], &i) || !de || !para)
			return uso();
		dizer(QStringLiteral("%1 %2 = %3 %4").arg(pct(i / 100, 4), QString::fromUtf8(de->nome),
		    pct(contas::equivalente(i / 100, de->anos, para->anos), 4), QString::fromUtf8(para->nome)));
		return 0;
	}
	if (cmd == "--corrigir") {
		double valor;
		const Indice *ind = acharIndice(a.value(2));
		const QDate ini = lerMesAno(a.value(4)), fim = lerMesAno(a.value(5));
		if (a.size() != 6 || !ind || !numero(a[3], &valor) || !ini.isValid() || !fim.isValid())
			return uso();
		int saida = 1;
		buscarSgs(ind->serie, false, &app, [&](const QVector<Ponto> &p, const QString &aviso) {
			if (!aviso.isEmpty())
				fprintf(stderr, "%s\n", aviso.toUtf8().constData());
			if (!p.isEmpty()) {
				const Correcao c = corrigir(p, *ind, ini, fim);
				if (!c.erro.isEmpty())
					fprintf(stderr, "%s\n", c.erro.toUtf8().constData());
				else {
					dizer(QStringLiteral("%1 de %2 a %3 (%4 meses)").arg(QString::fromUtf8(ind->nome),
					    mesAno(ini), mesAno(fim)).arg(c.meses.size()));
					dizer(QStringLiteral("  fator              %1").arg(num(c.fator, 7)));
					dizer(QStringLiteral("  variação           %1").arg(pct(c.fator - 1)));
					dizer(QStringLiteral("  valor corrigido    %1").arg(moeda(valor * c.fator)));
					saida = 0;
				}
			}
			app.exit(saida);
		});
		return app.exec();
	}
	if (cmd == "--indicadores") {
		const int total = int(sizeof INDICADORES / sizeof INDICADORES[0]);
		auto *faltam = new int(total);
		auto *linhas = new QStringList[total];
		for (int k = 0; k < total; k++) {
			const Indicador &ind = INDICADORES[k];
			buscarSgs(ind.serie, true, &app, [&app, k, ind, faltam, linhas](const QVector<Ponto> &p, const QString &aviso) {
				if (p.isEmpty())
					linhas[k] << QStringLiteral("%1: %2").arg(QString::fromUtf8(ind.nome), aviso);
				else
					linhas[k] << QStringLiteral("%1  %2  (%3)").arg(QString::fromUtf8(ind.nome).leftJustified(18),
					    ind.porcento ? num(p.last().valor) + "%" : moeda(p.last().valor),
					    p.last().data.toString(QStringLiteral("dd/MM/yyyy")));
				if (--*faltam == 0) {
					for (int j = 0; j < total; j++)
						dizer(linhas[j].join("\n"));
					app.exit(0);
				}
			});
		}
		return app.exec();
	}
	if (cmd == "--regressao") {
		QStringList r = a.mid(2);
		const bool logs = r.removeAll(QStringLiteral("--log")) > 0;
		if (r.size() < 3)
			return uso();
		QFile f(r[0]);
		if (!f.open(QIODevice::ReadOnly)) {
			fprintf(stderr, "lume-financas: não abriu %s\n", r[0].toUtf8().constData());
			return 1;
		}
		const Dados d = lerTabela(QString::fromUtf8(f.readAll()));
		const int y = d.nomes.indexOf(r[1]);
		QVector<int> xs;
		for (int k = 2; k < r.size(); k++) {
			const int c = d.nomes.indexOf(r[k]);
			if (c < 0) {
				fprintf(stderr, "lume-financas: não há a coluna %s\n", r[k].toUtf8().constData());
				return 1;
			}
			xs << c;
		}
		if (y < 0) {
			fprintf(stderr, "lume-financas: não há a coluna %s\n", r[1].toUtf8().constData());
			return 1;
		}
		const Amostra am = montarAmostra(d, y, xs, logs);
		if (!am.erro.isEmpty()) {
			fprintf(stderr, "%s\n", am.erro.toUtf8().constData());
			return 1;
		}
		const contas::Regressao reg = contas::mqo(am.y, am.xs, r.mid(2));
		if (!reg.erro.isEmpty()) {
			fprintf(stderr, "%s\n", reg.erro.toUtf8().constData());
			return 1;
		}
		fputs(contas::relatorio(reg, r[1], logs, am.descartadas).toUtf8().constData(), stdout);
		return 0;
	}
	return -1;
}

// --- A cara: papel e tinta ----------------------------------------------------

// A mesma folha dos outros apps do LumeBSD (os Programas): serifa nos
// títulos, mono nos rótulos e nos números, fios finos em vez de sombras.
static const char *ESTILO = R"QSS(
* { font-family: "IBM Plex Sans", "Noto Sans", sans-serif; color: #1c1b19; }
QWidget#raiz, QWidget#pagina, QScrollArea, QScrollArea > QWidget > QWidget, QStackedWidget { background: #efeadf; }
QLabel { background: transparent; }
QLabel#marca { font-family: "IBM Plex Serif", serif; font-size: 25px; }
QLabel#sub, QLabel#italico, QLabel#detalhe, QLabel#vazio {
	font-family: "IBM Plex Serif", serif; font-style: italic; color: #5d5950; }
QLabel#sub, QLabel#italico { font-size: 15px; }
QLabel#detalhe { font-size: 12px; }
QLabel#capitulo { font-family: "IBM Plex Serif", serif; font-size: 30px; }
QLabel#parte { font-family: "IBM Plex Serif", serif; font-size: 21px; }
QLabel#numero { font-family: "IBM Plex Serif", serif; font-size: 44px; }
QLabel#valor { font-family: "IBM Plex Serif", serif; font-size: 23px; }
QLabel#texto, QLabel#aviso { font-family: "IBM Plex Serif", serif; font-size: 14px; }
QLabel#aviso { font-style: italic; }
QLabel#rotulo { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: #5d5950; }
QFrame#fio { background: #1c1b19; border: none; min-height: 1px; max-height: 1px; }
QFrame#fioV { background: #1c1b19; border: none; min-width: 1px; max-width: 1px; }
QFrame#cartao { background: #f6f2e9; border: 1px solid #1c1b19; }
QFrame#cartao QLabel { background: transparent; }
QLineEdit {
	background: transparent; border: none; border-bottom: 1px solid #1c1b19;
	padding: 4px 2px; font-family: "IBM Plex Mono", monospace; font-size: 13px;
	selection-background-color: #1c1b19; selection-color: #efeadf; }
QLineEdit:focus { border-bottom: 2px solid #1c1b19; }
QLineEdit#calculado { background: #e2dccd; }
QComboBox, QDateEdit {
	background: #f6f2e9; border: 1px solid #1c1b19; padding: 3px 8px;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; min-height: 20px; }
QComboBox::drop-down, QDateEdit::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
	background: #f6f2e9; border: 1px solid #1c1b19; outline: 0;
	selection-background-color: #1c1b19; selection-color: #efeadf; }
QCheckBox { font-family: "IBM Plex Serif", serif; font-size: 14px; spacing: 8px; }
QCheckBox::indicator { width: 12px; height: 12px; border: 1px solid #1c1b19; background: #f6f2e9; }
QCheckBox::indicator:checked { background: #1c1b19; }
QListWidget#indice {
	background: #efeadf; border: none; outline: 0;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; }
QListWidget#indice::item { padding: 8px 14px; border: none; color: #1c1b19; }
QListWidget#indice::item:selected { background: #1c1b19; color: #efeadf; }
QListWidget#indice::item:hover:!selected { background: #e2dccd; }
QListWidget#variaveis {
	background: #f6f2e9; border: 1px solid #1c1b19; outline: 0;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; }
QListWidget#variaveis::item { padding: 3px 6px; }
QListWidget#variaveis::item:selected { background: #e2dccd; color: #1c1b19; }
QTableWidget {
	background: #f6f2e9; border: 1px solid #1c1b19; gridline-color: #d6d0c1;
	font-family: "IBM Plex Mono", monospace; font-size: 12px;
	selection-background-color: #1c1b19; selection-color: #efeadf; }
QHeaderView { background: #efeadf; }
QHeaderView::section {
	background: #efeadf; border: none; border-bottom: 1px solid #1c1b19;
	border-right: 1px solid #d6d0c1; padding: 4px 6px;
	font-family: "IBM Plex Mono", monospace; font-size: 11px; color: #5d5950; }
QHeaderView::section:vertical { border-right: 1px solid #1c1b19; border-bottom: 1px solid #d6d0c1; }
QTableCornerButton::section { background: #efeadf; border: none; }
QPushButton {
	background: transparent; border: 1px solid #1c1b19; padding: 6px 16px;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; min-height: 18px; }
QPushButton:hover { background: #e2dccd; }
QPushButton:pressed { background: #1c1b19; color: #efeadf; }
QPushButton#primario { background: #1c1b19; color: #efeadf; }
QPushButton#primario:hover { background: #3b3934; }
QPushButton:disabled, QPushButton#primario:disabled {
	background: transparent; color: #9b968b; border: 1px solid #bdb7a9; }
QPushButton#link {
	border: none; padding: 2px 0; background: transparent; text-decoration: underline;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; }
QPushButton#link:hover { background: transparent; color: #5d5950; }
QPlainTextEdit { background: #f6f2e9; border: 1px solid #1c1b19;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 7px; margin: 2px; }
QScrollBar::handle:vertical { background: #8a857a; min-height: 36px; }
QScrollBar::handle:horizontal { background: #8a857a; min-width: 36px; }
QScrollBar::handle:hover { background: #1c1b19; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QMessageBox, QDialog { background: #efeadf; }
QMessageBox QLabel { font-family: "IBM Plex Serif", serif; font-size: 14px; }
QToolTip { background: #1c1b19; color: #efeadf; border: none; padding: 4px 6px; }
)QSS";

// À noite (o Papel noite), cada cor trocada pela sua par escura.
static QString
estiloNoite()
{
	static const char *const PARES[][2] = {
		{"#efeadf", "#1e1d1b"}, // papel -> fundo
		{"#1c1b19", "#dedad0"}, // tinta -> texto
		{"#f6f2e9", "#272623"}, // cartão
		{"#5d5950", "#a9a499"}, // texto secundário
		{"#e2dccd", "#34322e"}, // ao passar o mouse
		{"#3b3934", "#c4bfb3"}, // botão principal, ao passar o mouse
		{"#9b968b", "#6e6a62"}, // desabilitado
		{"#bdb7a9", "#4a4740"}, // contorno desabilitado
		{"#8a857a", "#7c776d"}, // barra de rolagem
		{"#d6d0c1", "#3a3833"}, // grade das tabelas
	};
	QString e = QString::fromUtf8(ESTILO);
	// Em duas passadas, para uma troca não desfazer a outra (papel <-> tinta).
	for (int i = 0; i < int(sizeof PARES / sizeof PARES[0]); i++)
		e.replace(QLatin1String(PARES[i][0]), QStringLiteral("@%1@").arg(i), Qt::CaseInsensitive);
	for (int i = 0; i < int(sizeof PARES / sizeof PARES[0]); i++)
		e.replace(QStringLiteral("@%1@").arg(i), QLatin1String(PARES[i][1]));
	return e;
}

static QFrame *
fio(bool vertical = false)
{
	auto *f = new QFrame;
	f->setObjectName(vertical ? "fioV" : "fio");
	return f;
}

static QLabel *
rotulo(const QString &texto, const char *nome = "rotulo")
{
	auto *l = new QLabel(texto);
	l->setObjectName(nome);
	l->setWordWrap(true);
	return l;
}

static QLineEdit *
campo(const QString &valor = {}, const QString &dica = {})
{
	auto *e = new QLineEdit(valor);
	e->setPlaceholderText(dica);
	e->setMinimumWidth(150);
	return e;
}

static QPushButton *
botao(const QString &texto, const char *nome = nullptr)
{
	auto *b = new QPushButton(texto);
	if (nome)
		b->setObjectName(nome);
	b->setCursor(Qt::PointingHandCursor);
	return b;
}

static double
valorDe(const QLineEdit *e, bool *ok = nullptr)
{
	return lerNumero(e->text(), ok);
}

// --- O gráfico ---------------------------------------------------------------

// Linhas, pontos e barras em tinta sobre o papel, sem cor: a segunda série
// tracejada ou em cinza, como num livro impresso. Passando o mouse, os
// valores do ponto mais perto.
struct Serie {
	QString nome;
	QVector<QPointF> pontos;
	enum Tipo { Linha, Pontos, Barras } tipo = Linha;
	bool tracejada = false, cinza = false;
};

struct Marca {
	double x;
	QString rotulo;
};

static QVector<double>
marcasEixo(double &lo, double &hi, int alvo, double *passoSaida, bool inteiro = false)
{
	if (!(hi - lo > 1e-12)) {
		const double m = std::max(std::fabs(lo), 1.0) * 0.1;
		lo -= m;
		hi += m;
	}
	const double bruto = (hi - lo) / alvo;
	const double mag = std::pow(10, std::floor(std::log10(bruto)));
	const double r = bruto / mag;
	double passo = (r < 1.5 ? 1 : r < 3 ? 2 : r < 7 ? 5 : 10) * mag;
	if (inteiro)
		passo = std::max(1.0, std::round(passo));
	lo = std::floor(lo / passo + 1e-9) * passo;
	hi = std::ceil(hi / passo - 1e-9) * passo;
	QVector<double> m;
	for (double v = lo; v <= hi + passo * 1e-6; v += passo)
		m << (std::fabs(v) < passo * 1e-9 ? 0 : v);
	*passoSaida = passo;
	return m;
}

static QString
rotuloEixo(double v, double passo)
{
	const double a = std::fabs(v);
	if (a >= 1e9)
		return num(v / 1e9, passo >= 1e9 ? 0 : 1) + " bi";
	if (a >= 1e6)
		return num(v / 1e6, passo >= 1e6 ? 0 : 1) + " mi";
	if (a >= 1e4)
		return num(v / 1e3, passo >= 1e3 ? 0 : 1) + " mil";
	int casas = passo >= 1 ? 0 : std::min(4, int(std::ceil(-std::log10(passo) - 1e-9)));
	return num(v, casas);
}

class Grafico : public QWidget {
public:
	explicit Grafico(const QString &vazio = QStringLiteral("O gráfico aparece aqui depois da conta."))
	    : vazio_(vazio)
	{
		setMinimumHeight(250);
		setMouseTracking(true);
	}

	std::function<QString(double)> formatoX, formatoY;

	void definir(const QString &eixoX, const QString &eixoY, const QVector<Serie> &s,
	    const QVector<Marca> &m = {}, bool zero = false, bool xInteiro = false)
	{
		eixoX_ = eixoX;
		eixoY_ = eixoY;
		series_ = s;
		marcas_ = m;
		zero_ = zero;
		xInteiro_ = xInteiro;
		update();
	}
	void limpar(const QString &vazio = {})
	{
		series_.clear();
		marcas_.clear();
		if (!vazio.isEmpty())
			vazio_ = vazio;
		update();
	}

protected:
	void mouseMoveEvent(QMouseEvent *e) override
	{
		mouse_ = e->position();
		update();
	}
	void leaveEvent(QEvent *) override
	{
		mouse_ = QPointF(-1, -1);
		update();
	}
	void paintEvent(QPaintEvent *) override;

private:
	QString vazio_, eixoX_, eixoY_;
	QVector<Serie> series_;
	QVector<Marca> marcas_;
	bool zero_ = false, xInteiro_ = false;
	QPointF mouse_{-1, -1};
};

void
Grafico::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QColor papel = palette().color(QPalette::Window);
	const bool noite = papel.lightness() < 128;
	const QColor tinta = palette().color(QPalette::WindowText);
	const QColor segunda = noite ? QColor(0xa9, 0xa4, 0x99) : QColor(0x5d, 0x59, 0x50);
	const QColor grade = noite ? QColor(0x3a, 0x38, 0x33) : QColor(0xd6, 0xd0, 0xc1);
	const QColor cinza = noite ? QColor(0x8a, 0x85, 0x7a) : QColor(0x9b, 0x96, 0x8b);
	QFont mono(QStringLiteral("IBM Plex Mono"));
	mono.setStyleHint(QFont::Monospace);
	mono.setPixelSize(10);
	QFont serifa(QStringLiteral("IBM Plex Serif"));
	serifa.setStyleHint(QFont::Serif);
	serifa.setItalic(true);
	serifa.setPixelSize(14);

	bool tem = false;
	double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
	bool barras = false;
	for (const Serie &s : series_)
		for (const QPointF &pt : s.pontos) {
			if (!std::isfinite(pt.x()) || !std::isfinite(pt.y()))
				continue;
			tem = true;
			x0 = std::min(x0, pt.x());
			x1 = std::max(x1, pt.x());
			y0 = std::min(y0, pt.y());
			y1 = std::max(y1, pt.y());
			barras = barras || s.tipo == Serie::Barras;
		}
	if (!tem) {
		p.setPen(segunda);
		p.setFont(serifa);
		p.drawText(rect(), Qt::AlignCenter, vazio_);
		return;
	}
	for (const Marca &m : marcas_)
		if (std::isfinite(m.x)) {
			x0 = std::min(x0, m.x);
			x1 = std::max(x1, m.x);
		}
	if (zero_ || barras) {
		y0 = std::min(y0, 0.0);
		y1 = std::max(y1, 0.0);
	}
	if (barras) {
		x0 -= 0.5;
		x1 += 0.5;
	}
	double passoY, passoX;
	const QVector<double> my = marcasEixo(y0, y1, 5, &passoY);
	double xa = x0, xb = x1;
	QVector<double> mx = marcasEixo(xa, xb, 6, &passoX, xInteiro_);
	if (!barras && !xInteiro_) {
		x0 = xa;
		x1 = xb;
	}

	QFontMetrics fm(mono);
	int esquerda = 0;
	for (double v : my)
		esquerda = std::max(esquerda, fm.horizontalAdvance(formatoY ? formatoY(v) : rotuloEixo(v, passoY)));
	const QRectF area(esquerda + 16, 34, width() - esquerda - 16 - 14, height() - 34 - 40);
	auto px = [&](double x) { return area.left() + (x - x0) / (x1 - x0) * area.width(); };
	auto py = [&](double y) { return area.bottom() - (y - y0) / (y1 - y0) * area.height(); };

	// A grade e os eixos.
	p.setFont(mono);
	for (double v : my) {
		const double y = py(v);
		p.setPen(QPen(std::fabs(v) < passoY * 1e-6 && y0 < 0 ? segunda : grade, 1));
		p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
		p.setPen(segunda);
		p.drawText(QRectF(0, y - 8, esquerda + 10, 16), Qt::AlignRight | Qt::AlignVCenter,
		    formatoY ? formatoY(v) : rotuloEixo(v, passoY));
	}
	p.setPen(QPen(tinta, 1));
	p.drawLine(area.bottomLeft(), area.bottomRight());
	p.drawLine(area.bottomLeft(), area.topLeft());
	p.setPen(segunda);
	for (double v : mx) {
		if (v < x0 - 1e-9 || v > x1 + 1e-9)
			continue;
		const double x = px(v);
		p.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4));
		const QString t = formatoX ? formatoX(v) : rotuloEixo(v, passoX);
		p.drawText(QRectF(x - 60, area.bottom() + 6, 120, 14), Qt::AlignHCenter | Qt::AlignTop, t);
	}
	p.drawText(QRectF(area.left(), area.bottom() + 22, area.width(), 16), Qt::AlignHCenter, eixoX_);
	p.drawText(QRectF(0, 2, width() / 2, 14), Qt::AlignLeft, eixoY_);

	// As marcas (a TMA, a TIR...): fios pontilhados com o nome em cima.
	for (const Marca &m : marcas_) {
		if (!std::isfinite(m.x) || m.x < x0 || m.x > x1)
			continue;
		const double x = px(m.x);
		p.setPen(QPen(segunda, 1, Qt::DotLine));
		p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
		p.setPen(tinta);
		p.drawText(QRectF(x - 80, area.top() - 14, 160, 12), Qt::AlignHCenter, m.rotulo);
	}

	// As séries.
	p.setClipRect(area.adjusted(-4, -4, 4, 4));
	int nbarras = 0, ibarra = 0;
	for (const Serie &s : series_)
		nbarras += s.tipo == Serie::Barras;
	for (const Serie &s : series_) {
		const QColor c = s.cinza ? cinza : tinta;
		if (s.tipo == Serie::Linha) {
			QPolygonF l;
			for (const QPointF &pt : s.pontos)
				if (std::isfinite(pt.y()))
					l << QPointF(px(pt.x()), py(pt.y()));
			p.setPen(QPen(c, 1.6, s.tracejada ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.drawPolyline(l);
		} else if (s.tipo == Serie::Pontos) {
			p.setPen(QPen(c, 1.2));
			p.setBrush(Qt::NoBrush);
			for (const QPointF &pt : s.pontos)
				p.drawEllipse(QPointF(px(pt.x()), py(pt.y())), 3, 3);
		} else {
			const double total = std::max(1.0, area.width() / std::max(1.0, x1 - x0)) * 0.72;
			const double larg = total / std::max(1, nbarras);
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			for (const QPointF &pt : s.pontos) {
				const double x = px(pt.x()) - total / 2 + ibarra * larg;
				const double ya = py(pt.y()), yz = py(0);
				p.drawRect(QRectF(x, std::min(ya, yz), std::max(1.0, larg - 1), std::fabs(yz - ya)));
			}
			ibarra++;
			p.setBrush(Qt::NoBrush);
		}
	}
	p.setClipping(false);

	// A legenda, em cima à direita.
	double lx = area.right();
	for (int k = series_.size() - 1; k >= 0; k--) {
		const Serie &s = series_[k];
		if (s.nome.isEmpty())
			continue;
		const double w = fm.horizontalAdvance(s.nome);
		lx -= w;
		p.setPen(segunda);
		p.drawText(QPointF(lx, 14), s.nome);
		lx -= 26;
		const QColor c = s.cinza ? cinza : tinta;
		if (s.tipo == Serie::Barras)
			p.fillRect(QRectF(lx, 6, 18, 9), c);
		else if (s.tipo == Serie::Pontos) {
			p.setPen(QPen(c, 1.2));
			p.drawEllipse(QPointF(lx + 9, 10), 3, 3);
		} else {
			p.setPen(QPen(c, 1.6, s.tracejada ? Qt::DashLine : Qt::SolidLine));
			p.drawLine(QPointF(lx, 10), QPointF(lx + 18, 10));
		}
		lx -= 18;
	}

	// O ponto mais perto do mouse.
	if (area.contains(mouse_) && !series_.isEmpty() && !series_[0].pontos.isEmpty()) {
		const auto &pts = series_[0].pontos;
		int melhor = 0;
		for (int k = 1; k < pts.size(); k++)
			if (std::fabs(px(pts[k].x()) - mouse_.x()) < std::fabs(px(pts[melhor].x()) - mouse_.x()))
				melhor = k;
		const double x = px(pts[melhor].x());
		p.setPen(QPen(segunda, 1, Qt::DotLine));
		p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
		QStringList t;
		t << (formatoX ? formatoX(pts[melhor].x()) : num(pts[melhor].x(), xInteiro_ ? 0 : 2));
		for (const Serie &s : series_) {
			if (s.pontos.size() != pts.size())
				continue;
			const double y = s.pontos[melhor].y();
			t << (s.nome.isEmpty() ? QString() : s.nome + ": ") +
			    (formatoY ? formatoY(y) : num(y, std::fabs(y) < 10 ? 4 : 2));
			if (s.tipo != Serie::Barras) {
				p.setPen(QPen(tinta, 1.2));
				p.setBrush(papel);
				p.drawEllipse(QPointF(x, py(y)), 3.5, 3.5);
				p.setBrush(Qt::NoBrush);
			}
		}
		int w = 0;
		for (const QString &l : t)
			w = std::max(w, fm.horizontalAdvance(l));
		QRectF caixa(x + 10, area.top() + 6, w + 14, t.size() * 14 + 8);
		if (caixa.right() > area.right())
			caixa.moveRight(x - 10);
		p.fillRect(caixa, tinta);
		p.setPen(papel);
		for (int k = 0; k < t.size(); k++)
			p.drawText(QPointF(caixa.left() + 7, caixa.top() + 16 + k * 14), t[k]);
	}
}

// --- As páginas ---------------------------------------------------------------

// Um capítulo: o número grande, o título, uma frase, o fio.
class Pagina : public QScrollArea {
public:
	Pagina(const QString &numero, const QString &titulo, const QString &sub)
	{
		setWidgetResizable(true);
		setFrameShape(QFrame::NoFrame);
		auto *w = new QWidget;
		w->setObjectName("pagina");
		v = new QVBoxLayout(w);
		v->setContentsMargins(40, 26, 40, 44);
		v->setSpacing(14);
		auto *cab = new QHBoxLayout;
		cab->setSpacing(20);
		auto *n = rotulo(numero, "numero");
		n->setWordWrap(false);
		cab->addWidget(n, 0, Qt::AlignTop);
		auto *tx = new QVBoxLayout;
		tx->setSpacing(2);
		tx->addWidget(rotulo(titulo, "capitulo"));
		tx->addWidget(rotulo(sub, "italico"));
		cab->addLayout(tx, 1);
		v->addLayout(cab);
		v->addWidget(fio());
		setWidget(w);
	}

	void parte(const QString &titulo, const QString &sub = {})
	{
		v->addSpacing(10);
		v->addWidget(rotulo(titulo, "parte"));
		if (!sub.isEmpty())
			v->addWidget(rotulo(sub, "italico"));
	}

	QVBoxLayout *v;
};

// O cartão dos resultados: rótulo em mono, o número em serifa, uma linha de
// explicação embaixo.
class Quadro : public QFrame {
public:
	explicit Quadro(int colunas = 3) : colunas_(colunas)
	{
		setObjectName("cartao");
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		g_ = new QGridLayout(this);
		g_->setContentsMargins(20, 16, 20, 16);
		g_->setHorizontalSpacing(28);
		g_->setVerticalSpacing(14);
	}
	void item(const QString &chave, const QString &nome)
	{
		auto *caixa = new QVBoxLayout;
		caixa->setSpacing(1);
		auto *r = rotulo(nome.toUpper());
		auto *v = rotulo(QStringLiteral("—"), "valor");
		v->setTextInteractionFlags(Qt::TextSelectableByMouse);
		auto *d = rotulo(QString(), "detalhe");
		caixa->addWidget(r);
		caixa->addWidget(v);
		caixa->addWidget(d);
		caixa->addStretch(1);
		const int k = itens_.size();
		g_->addLayout(caixa, k / colunas_, k % colunas_);
		g_->setColumnStretch(k % colunas_, 1);
		itens_[chave] = {v, d};
	}
	void valor(const QString &chave, const QString &v, const QString &detalhe = {})
	{
		if (!itens_.contains(chave))
			return;
		itens_[chave].first->setText(v);
		itens_[chave].second->setText(detalhe);
	}
	void limpar()
	{
		for (auto &i : itens_) {
			i.first->setText(QStringLiteral("—"));
			i.second->clear();
		}
	}

private:
	int colunas_;
	QGridLayout *g_;
	QMap<QString, QPair<QLabel *, QLabel *>> itens_;
};

static QTableWidgetItem *
celula(const QString &t, bool editavel = false)
{
	auto *i = new QTableWidgetItem(t);
	i->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	if (!editavel)
		i->setFlags(i->flags() & ~Qt::ItemIsEditable);
	return i;
}

static QTableWidget *
tabela(const QStringList &cabecalho, int altura = 300)
{
	auto *t = new QTableWidget(0, cabecalho.size());
	t->setHorizontalHeaderLabels(cabecalho);
	t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	t->verticalHeader()->setDefaultSectionSize(24);
	t->setMinimumHeight(altura);
	t->setMaximumHeight(altura + 200);
	return t;
}

static void
salvarCsv(QWidget *pai, const QString &nome, const QString &conteudo)
{
	const QString arq = QFileDialog::getSaveFileName(pai, QStringLiteral("Salvar"),
	    QDir::homePath() + "/" + nome, QStringLiteral("Planilha CSV (*.csv)"));
	if (arq.isEmpty())
		return;
	QFile f(arq);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QMessageBox::warning(pai, QStringLiteral("Salvar"), QStringLiteral("Não deu para gravar %1.").arg(arq));
		return;
	}
	f.write("\xEF\xBB\xBF");	// o BOM: o Excel lê os acentos
	f.write(conteudo.toUtf8());
}

// --- 1. Juros e taxas ---

class PaginaJuros : public Pagina {
public:
	PaginaJuros()
	    : Pagina("1", "Juros e taxas",
		  "O valor do dinheiro no tempo, as taxas equivalentes e os juros reais.")
	{
		parte("Valor do dinheiro no tempo",
		    "Preencha quatro campos e clique em “calcular” no que falta. Como na HP 12C: o que "
		    "sai do bolso é negativo, o que entra é positivo.");
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(8);
		const char *nomes[] = {"n  número de períodos", "i  taxa por período (%)",
				       "VP  valor presente", "PMT  parcela (por período)", "VF  valor futuro"};
		const char *iniciais[] = {"12", "1", "-1.000", "0", ""};
		for (int k = 0; k < 5; k++) {
			e_[k] = campo(QString::fromUtf8(iniciais[k]));
			auto *b = botao("calcular", "link");
			g->addWidget(rotulo(QString::fromUtf8(nomes[k])), k, 0);
			g->addWidget(e_[k], k, 1);
			g->addWidget(b, k, 2, Qt::AlignLeft);
			connect(b, &QPushButton::clicked, this, [this, k] { calcular(contas::Incognita(k)); });
			connect(e_[k], &QLineEdit::returnPressed, this, [this] { calcularFaltante(); });
		}
		g->setColumnMinimumWidth(0, 230);
		g->setColumnStretch(3, 1);
		v->addLayout(g);
		inicio_ = new QCheckBox("Parcelas no início de cada período (antecipadas, o BEGIN da HP)");
		v->addWidget(inicio_);
		auto *h = new QHBoxLayout;
		auto *limpa = botao("Limpar");
		h->addWidget(limpa);
		h->addStretch(1);
		v->addLayout(h);
		connect(limpa, &QPushButton::clicked, this, [this] {
			for (auto *e : e_) {
				e->clear();
				e->setObjectName(QString());
				e->setStyleSheet(QString());
			}
			resultado_->clear();
		});
		resultado_ = rotulo(QString(), "aviso");
		v->addWidget(resultado_);

		parte("Taxas equivalentes", "A mesma taxa, em outro período: 1% ao mês é 12,68% ao ano, não 12%.");
		auto *t = new QHBoxLayout;
		t->setSpacing(12);
		taxa_ = campo("1", "taxa (%)");
		taxa_->setMaximumWidth(160);
		de_ = new QComboBox;
		para_ = new QComboBox;
		for (const auto &p : contas::PERIODOS) {
			de_->addItem(QString::fromUtf8(p.nome));
			para_->addItem(QString::fromUtf8(p.nome));
		}
		de_->setCurrentIndex(2);
		para_->setCurrentIndex(6);
		t->addWidget(taxa_);
		t->addWidget(de_);
		t->addWidget(rotulo("é o mesmo que", "texto"));
		equivalente_ = rotulo(QString(), "valor");
		equivalente_->setWordWrap(false);
		t->addWidget(equivalente_);
		t->addWidget(para_);
		t->addStretch(1);
		v->addLayout(t);
		v->addWidget(rotulo("O dia útil segue o ano de 252 dias úteis do CDI e da Selic; os "
		    "outros, o ano comercial de 360 dias.", "detalhe"));

		parte("Taxa nominal e taxa efetiva",
		    "“12% ao ano, capitalizados mês a mês” são 1% ao mês, e 12,68% ao ano de verdade.");
		auto *n = new QHBoxLayout;
		n->setSpacing(12);
		nominal_ = campo("12", "taxa nominal ao ano (%)");
		nominal_->setMaximumWidth(160);
		capitalizacao_ = new QComboBox;
		capitalizacao_->addItems({"capitalizada por mês", "capitalizada por dia",
					  "capitalizada por trimestre", "capitalizada por semestre"});
		n->addWidget(nominal_);
		n->addWidget(rotulo("% ao ano,", "texto"));
		n->addWidget(capitalizacao_);
		n->addStretch(1);
		v->addLayout(n);
		efetiva_ = rotulo(QString(), "aviso");
		v->addWidget(efetiva_);

		parte("Juros reais (Fisher)",
		    "Quanto o dinheiro cresceu de verdade, descontada a inflação: (1 + nominal) = "
		    "(1 + real) × (1 + inflação).");
		auto *f = new QHBoxLayout;
		f->setSpacing(12);
		rendeu_ = campo("13,75", "rendimento (%)");
		inflacao_ = campo("4,63", "inflação (%)");
		rendeu_->setMaximumWidth(140);
		inflacao_->setMaximumWidth(140);
		f->addWidget(rotulo("rendeu", "texto"));
		f->addWidget(rendeu_);
		f->addWidget(rotulo("% com inflação de", "texto"));
		f->addWidget(inflacao_);
		f->addWidget(rotulo("%: o ganho real foi de", "texto"));
		real_ = rotulo(QString(), "valor");
		real_->setWordWrap(false);
		f->addWidget(real_);
		f->addStretch(1);
		v->addLayout(f);
		v->addStretch(1);

		for (QLineEdit *e : {taxa_, nominal_, rendeu_, inflacao_})
			connect(e, &QLineEdit::textChanged, this, [this] { converter(); });
		for (QComboBox *c : {de_, para_, capitalizacao_})
			connect(c, &QComboBox::currentIndexChanged, this, [this] { converter(); });
		converter();
	}

private:
	void calcularFaltante()
	{
		int vazios = 0, qual = -1;
		for (int k = 0; k < 5; k++)
			if (e_[k]->text().trimmed().isEmpty()) {
				vazios++;
				qual = k;
			}
		if (vazios == 1)
			calcular(contas::Incognita(qual));
	}

	void calcular(contas::Incognita x)
	{
		contas::Tvm t;
		double *campos[] = {&t.n, &t.i, &t.vp, &t.pmt, &t.vf};
		for (int k = 0; k < 5; k++) {
			bool ok;
			const double v = valorDe(e_[k], &ok);
			*campos[k] = ok && k != x ? v : NAO;
			e_[k]->setStyleSheet(QString());
		}
		if (std::isfinite(t.i))
			t.i /= 100;
		t.inicio = inicio_->isChecked();
		QString erro;
		if (!contas::resolverTvm(t, x, &erro)) {
			resultado_->setText(erro);
			return;
		}
		const double r = *campos[x];
		QString texto, explica;
		switch (x) {
		case contas::N:
			texto = num(r, 4);
			explica = QStringLiteral("São %1 períodos.").arg(num(r, 2));
			if (std::fabs(r - std::round(r)) > 1e-6)
				explica += QStringLiteral(" A HP 12C arredonda para cima: %1.").arg(std::ceil(r - 1e-9));
			break;
		case contas::I:
			texto = num(r * 100, 4);
			explica = QStringLiteral("A taxa é de %1 por período (%2 em 12 períodos).")
			    .arg(pct(r, 4), pct(std::pow(1 + r, 12) - 1));
			break;
		default:
			texto = num(r, 2);
			explica = QStringLiteral("%1 = %2.").arg(QStringList{"n", "i", "VP", "PMT", "VF"}[x], moeda(r));
			if (x == contas::PMT)
				explica += QStringLiteral(" No total, %1 em parcelas.").arg(moeda(r * t.n));
			break;
		}
		e_[x]->setText(texto);
		e_[x]->setStyleSheet(QStringLiteral("background: palette(midlight);"));
		resultado_->setText(explica);
	}

	void converter()
	{
		bool ok;
		const double i = valorDe(taxa_, &ok) / 100;
		if (ok && i > -1)
			equivalente_->setText(pct(contas::equivalente(i, contas::PERIODOS[de_->currentIndex()].anos,
			    contas::PERIODOS[para_->currentIndex()].anos), 4));
		else
			equivalente_->setText(QStringLiteral("—"));

		const double j = valorDe(nominal_, &ok) / 100;
		const int vezes[] = {12, 360, 4, 2};
		const char *por[] = {"ao mês", "ao dia", "ao trimestre", "ao semestre"};
		const int m = vezes[capitalizacao_->currentIndex()];
		if (ok)
			efetiva_->setText(QStringLiteral("São %1 %2, e a taxa efetiva é de %3 ao ano.")
			    .arg(pct(j / m, 4), QString::fromUtf8(por[capitalizacao_->currentIndex()]),
				pct(std::pow(1 + j / m, m) - 1, 4)));
		else
			efetiva_->clear();

		bool ok2;
		const double n = valorDe(rendeu_, &ok) / 100, pi = valorDe(inflacao_, &ok2) / 100;
		real_->setText(ok && ok2 && pi > -1 ? pct(contas::real(n, pi)) : QStringLiteral("—"));
	}

	QLineEdit *e_[5];
	QCheckBox *inicio_;
	QLabel *resultado_, *equivalente_, *efetiva_, *real_;
	QLineEdit *taxa_, *nominal_, *rendeu_, *inflacao_;
	QComboBox *de_, *para_, *capitalizacao_;
};

// --- 2. Financiamento ---

class PaginaFinanciamento : public Pagina {
public:
	PaginaFinanciamento()
	    : Pagina("2", "Financiamento",
		  "Price ou SAC: quanto você paga por mês, quanto de juros no total, e o custo de verdade.")
	{
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(8);
		valor_ = campo("250.000");
		taxa_ = campo("10,5");
		periodo_ = new QComboBox;
		periodo_->addItems({"ao ano", "ao mês"});
		prazo_ = campo("360");
		seguro_ = campo("", "0");
		tarifas_ = campo("", "0");
		g->addWidget(rotulo("valor financiado (R$)"), 0, 0);
		g->addWidget(valor_, 0, 1);
		g->addWidget(rotulo("juros (%)"), 1, 0);
		g->addWidget(taxa_, 1, 1);
		g->addWidget(periodo_, 1, 2, Qt::AlignLeft);
		g->addWidget(rotulo("prazo (meses)"), 2, 0);
		g->addWidget(prazo_, 2, 1);
		g->addWidget(rotulo("seguro e taxas por mês (R$)"), 0, 3);
		g->addWidget(seguro_, 0, 4);
		g->addWidget(rotulo("tarifas na contratação (R$)"), 1, 3);
		g->addWidget(tarifas_, 1, 4);
		g->setColumnMinimumWidth(0, 170);
		g->setColumnMinimumWidth(3, 210);
		g->setColumnStretch(5, 1);
		v->addLayout(g);
		auto *h = new QHBoxLayout;
		auto *calc = botao("Calcular", "primario");
		h->addWidget(calc);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);

		quadro_ = new Quadro(4);
		for (const char *s : {"price", "sac"}) {
			const QString n = QLatin1String(s) == "price" ? QStringLiteral("Price") : QStringLiteral("SAC");
			quadro_->item(QStringLiteral("%1-1").arg(s), n + " · primeira parcela");
			quadro_->item(QStringLiteral("%1-u").arg(s), n + " · última parcela");
			quadro_->item(QStringLiteral("%1-j").arg(s), n + " · total de juros");
			quadro_->item(QStringLiteral("%1-c").arg(s), n + " · CET");
		}
		v->addWidget(quadro_);

		parte("Parcela mês a mês");
		parcelas_ = new Grafico;
		parcelas_->formatoY = [](double y) { return rotuloEixo(y, 100); };
		v->addWidget(parcelas_);
		parte("Saldo devedor");
		saldos_ = new Grafico;
		v->addWidget(saldos_);

		parte("A tabela");
		auto *th = new QHBoxLayout;
		sistema_ = new QComboBox;
		sistema_->addItems({"Price (parcelas iguais)", "SAC (amortização constante)"});
		auto *bSalvar = botao("Salvar a tabela (CSV)");
		th->addWidget(sistema_);
		th->addWidget(bSalvar);
		th->addStretch(1);
		v->addLayout(th);
		tabela_ = tabela({"mês", "parcela", "juros", "amortização", "seguro e taxas", "saldo devedor"}, 360);
		v->addWidget(tabela_);
		v->addStretch(1);

		connect(calc, &QPushButton::clicked, this, [this] { calcular(); });
		for (QLineEdit *e : {valor_, taxa_, prazo_, seguro_, tarifas_})
			connect(e, &QLineEdit::returnPressed, this, [this] { calcular(); });
		connect(periodo_, &QComboBox::currentIndexChanged, this, [this] { calcular(); });
		connect(sistema_, &QComboBox::currentIndexChanged, this, [this] { preencher(); });
		connect(bSalvar, &QPushButton::clicked, this, [this] { salvar(); });
		calcular();
	}

private:
	// O CET: a taxa que iguala o que chegou no bolso (o valor menos as
	// tarifas) ao que saiu (as parcelas mais o seguro e as taxas).
	double cet(const QVector<contas::Linha> &t, double vp, double seguro, double tarifas)
	{
		QVector<double> f{vp - tarifas};
		for (const auto &l : t)
			f << -(l.prestacao + seguro);
		const auto r = contas::tirs(f);
		return r.isEmpty() ? NAO : r.first();
	}

	void calcular()
	{
		bool a, b, c, d = true, e = true;
		const double vp = valorDe(valor_, &a);
		double i = valorDe(taxa_, &b) / 100;
		const double n = valorDe(prazo_, &c);
		seguro_v_ = seguro_->text().trimmed().isEmpty() ? 0 : valorDe(seguro_, &d);
		tarifas_v_ = tarifas_->text().trimmed().isEmpty() ? 0 : valorDe(tarifas_, &e);
		if (!a || !b || !c || !d || !e || vp <= 0 || n < 1 || n > 1200 || i <= -1) {
			aviso_->setText("Preencha o valor, os juros e o prazo (de 1 a 1.200 meses).");
			return;
		}
		if (periodo_->currentIndex() == 0)
			i = contas::equivalente(i, 1, 1.0 / 12);
		i_ = i;
		price_ = contas::price(vp, i, int(n));
		sac_ = contas::sac(vp, i, int(n));
		auto total = [](const QVector<contas::Linha> &t) {
			double j = 0;
			for (const auto &l : t)
				j += l.juros;
			return j;
		};
		const double jp = total(price_), js = total(sac_);
		const struct {
			const char *s;
			const QVector<contas::Linha> *t;
			double j;
		} sist[] = {{"price", &price_, jp}, {"sac", &sac_, js}};
		for (const auto &s : sist) {
			const QString k = QLatin1String(s.s);
			const double c = cet(*s.t, vp, seguro_v_, tarifas_v_);
			quadro_->valor(k + "-1", moeda(s.t->first().prestacao + seguro_v_),
			    seguro_v_ > 0 ? QStringLiteral("com %1 de seguro e taxas").arg(moeda(seguro_v_)) : QString());
			quadro_->valor(k + "-u", moeda(s.t->last().prestacao + seguro_v_));
			quadro_->valor(k + "-j", moeda(s.j), QStringLiteral("total pago: %1")
			    .arg(moeda(vp + s.j + seguro_v_ * n + tarifas_v_)));
			quadro_->valor(k + "-c", pct(contas::equivalente(c, 1.0 / 12, 1)) + " a.a.",
			    QStringLiteral("%1 ao mês").arg(pct(c, 4)));
		}
		aviso_->setText(QStringLiteral("Juros de %1 ao mês (%2 ao ano). Na SAC você paga %3 a menos "
		    "de juros, mas a primeira parcela é %4 maior que na Price.")
		    .arg(pct(i, 4), pct(contas::equivalente(i, 1.0 / 12, 1)), moeda(jp - js),
			moeda(sac_.first().prestacao - price_.first().prestacao)));

		Serie p{"Price", {}, Serie::Linha, false, false}, s{"SAC", {}, Serie::Linha, true, false};
		Serie sp{"Price", {}, Serie::Linha, false, false}, ss{"SAC", {}, Serie::Linha, true, false};
		for (int k = 0; k < price_.size(); k++) {
			p.pontos << QPointF(k + 1, price_[k].prestacao + seguro_v_);
			s.pontos << QPointF(k + 1, sac_[k].prestacao + seguro_v_);
			sp.pontos << QPointF(k + 1, price_[k].saldo);
			ss.pontos << QPointF(k + 1, sac_[k].saldo);
		}
		parcelas_->definir("mês", "R$ por mês", {p, s}, {}, false, true);
		saldos_->definir("mês", "R$ devidos", {sp, ss}, {}, true, true);
		preencher();
	}

	void preencher()
	{
		const auto &t = sistema_->currentIndex() == 0 ? price_ : sac_;
		tabela_->setRowCount(t.size());
		for (int k = 0; k < t.size(); k++) {
			const auto &l = t[k];
			tabela_->setItem(k, 0, celula(QString::number(l.k)));
			tabela_->setItem(k, 1, celula(num(l.prestacao + seguro_v_)));
			tabela_->setItem(k, 2, celula(num(l.juros)));
			tabela_->setItem(k, 3, celula(num(l.amortizacao)));
			tabela_->setItem(k, 4, celula(num(seguro_v_)));
			tabela_->setItem(k, 5, celula(num(l.saldo)));
		}
		tabela_->verticalHeader()->setVisible(false);
	}

	void salvar()
	{
		const bool price = sistema_->currentIndex() == 0;
		const auto &t = price ? price_ : sac_;
		QString c = "mês;parcela;juros;amortização;seguro e taxas;saldo devedor\n";
		for (const auto &l : t)
			c += QStringLiteral("%1;%2;%3;%4;%5;%6\n").arg(l.k).arg(csvNum(l.prestacao + seguro_v_),
			    csvNum(l.juros), csvNum(l.amortizacao), csvNum(seguro_v_), csvNum(l.saldo));
		salvarCsv(this, price ? "financiamento-price.csv" : "financiamento-sac.csv", c);
	}

	QLineEdit *valor_, *taxa_, *prazo_, *seguro_, *tarifas_;
	QComboBox *periodo_, *sistema_;
	QLabel *aviso_;
	Quadro *quadro_;
	Grafico *parcelas_, *saldos_;
	QTableWidget *tabela_;
	QVector<contas::Linha> price_, sac_;
	double i_ = 0, seguro_v_ = 0, tarifas_v_ = 0;
};

// --- 3. Projetos ---

class PaginaProjetos : public Pagina {
public:
	PaginaProjetos()
	    : Pagina("3", "Projetos",
		  "Vale a pena investir? O fluxo de caixa, o VPL, a TIR e em quanto tempo o dinheiro volta.")
	{
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(8);
		tma_ = campo("10");
		reinv_ = campo("", "igual à TMA");
		g->addWidget(rotulo("TMA, a taxa mínima de atratividade (% por período)"), 0, 0);
		g->addWidget(tma_, 0, 1);
		g->addWidget(rotulo("taxa de reinvestimento, para a TIRM (%)"), 1, 0);
		g->setColumnMinimumWidth(0, 360);
		g->addWidget(reinv_, 1, 1);
		g->setColumnStretch(2, 1);
		v->addLayout(g);

		auto *corpo = new QHBoxLayout;
		corpo->setSpacing(24);
		auto *esq = new QVBoxLayout;
		fluxos_ = tabela({"fluxo de caixa (R$)"}, 300);
		fluxos_->setMaximumWidth(280);
		esq->addWidget(fluxos_);
		auto *b1 = new QHBoxLayout;
		auto *mais = botao("+ período"), *menos = botao("− período");
		b1->addWidget(mais);
		b1->addWidget(menos);
		esq->addLayout(b1);
		auto *b2 = new QHBoxLayout;
		auto *colar = botao("Colar"), *exemplo = botao("Exemplo");
		b2->addWidget(colar);
		b2->addWidget(exemplo);
		esq->addLayout(b2);
		esq->addWidget(rotulo("O período 0 é o investimento (negativo). Cole uma coluna da "
		    "planilha com “Colar”.", "detalhe"));
		esq->addStretch(1);
		corpo->addLayout(esq);

		auto *dir = new QVBoxLayout;
		quadro_ = new Quadro(2);
		quadro_->item("vpl", "VPL");
		quadro_->item("tir", "TIR");
		quadro_->item("tirm", "TIRM");
		quadro_->item("il", "Índice de lucratividade");
		quadro_->item("pb", "Payback simples");
		quadro_->item("pbd", "Payback descontado");
		quadro_->item("vaue", "VPL por período (VAUE)");
		dir->addWidget(quadro_);
		aviso_ = rotulo(QString(), "aviso");
		dir->addWidget(aviso_);
		dir->addStretch(1);
		corpo->addLayout(dir, 1);
		v->addLayout(corpo);

		parte("O perfil do VPL", "O VPL a cada taxa: onde a curva cruza o zero é a TIR.");
		perfil_ = new Grafico;
		perfil_->formatoX = [](double x) { return num(x, 0) + "%"; };
		v->addWidget(perfil_);
		parte("Os fluxos e o acumulado");
		barras_ = new Grafico;
		v->addWidget(barras_);
		v->addStretch(1);

		connect(mais, &QPushButton::clicked, this, [this] {
			fluxos_->insertRow(fluxos_->rowCount());
			numerar();
		});
		connect(menos, &QPushButton::clicked, this, [this] {
			if (fluxos_->rowCount() > 2)
				fluxos_->removeRow(fluxos_->rowCount() - 1);
			numerar();
			calcular();
		});
		connect(colar, &QPushButton::clicked, this, [this] {
			const Dados d = lerTabela(QGuiApplication::clipboard()->text());
			QVector<double> f;
			for (const QStringList &l : d.linhas) {
				bool ok;
				// A última coluna com número (a planilha pode ter "ano; fluxo").
				for (int c = l.size() - 1; c >= 0; c--) {
					const double x = lerNumero(l[c], &ok);
					if (ok) {
						f << x;
						break;
					}
				}
			}
			if (f.size() < 2) {
				aviso_->setText("Copie uma coluna de números na planilha e clique em “Colar”.");
				return;
			}
			carregar(f);
		});
		connect(exemplo, &QPushButton::clicked, this, [this] {
			tma_->setText("10");
			carregar({-10000, 3000, 3500, 4000, 4000, 2500});
		});
		connect(fluxos_, &QTableWidget::itemChanged, this, [this] { calcular(); });
		for (QLineEdit *e : {tma_, reinv_})
			connect(e, &QLineEdit::textChanged, this, [this] { calcular(); });
		carregar({-10000, 3000, 3500, 4000, 4000, 2500});
	}

private:
	void numerar()
	{
		QStringList n;
		for (int k = 0; k < fluxos_->rowCount(); k++)
			n << QString::number(k);
		fluxos_->setVerticalHeaderLabels(n);
	}

	void carregar(const QVector<double> &f)
	{
		{
			const QSignalBlocker b(fluxos_);
			fluxos_->setRowCount(f.size());
			for (int k = 0; k < f.size(); k++)
				fluxos_->setItem(k, 0, celula(num(f[k]), true));
			numerar();
		}
		calcular();
	}

	void calcular()
	{
		bool ok;
		const double tma = valorDe(tma_, &ok) / 100;
		if (!ok || tma <= -1) {
			aviso_->setText("Preencha a TMA.");
			return;
		}
		bool ok2 = true;
		const double reinv = reinv_->text().trimmed().isEmpty() ? tma : valorDe(reinv_, &ok2) / 100;
		QVector<double> f;
		for (int k = 0; k < fluxos_->rowCount(); k++) {
			const QTableWidgetItem *i = fluxos_->item(k, 0);
			f << (i ? lerNumero(i->text()) : 0);
		}
		// Os períodos vazios do fim não contam.
		while (f.size() > 2 && f.last() == 0 && (!fluxos_->item(f.size() - 1, 0) ||
		    fluxos_->item(f.size() - 1, 0)->text().trimmed().isEmpty()))
			f.removeLast();
		if (f.size() < 2) {
			aviso_->setText("São precisos pelo menos dois períodos.");
			return;
		}
		const double vpl = contas::vpl(f, tma);
		const QVector<double> tirs = contas::tirs(f);
		const int n = f.size() - 1;
		quadro_->valor("vpl", moeda(vpl), QStringLiteral("à TMA de %1").arg(pct(tma)));
		if (tirs.isEmpty())
			quadro_->valor("tir", QStringLiteral("não há"), "os fluxos não trocam de sinal");
		else {
			QStringList t;
			for (double x : tirs)
				t << pct(x);
			quadro_->valor("tir", t.join(" e "), tirs.size() > 1
			    ? QStringLiteral("mais de uma: o fluxo troca de sinal mais de uma vez; prefira o VPL")
			    : QStringLiteral("por período"));
		}
		quadro_->valor("tirm", ok2 ? pct(contas::tirm(f, tma, reinv)) : QStringLiteral("—"),
		    QStringLiteral("reinvestindo a %1").arg(pct(reinv)));
		const double il = contas::indiceLucratividade(f, tma);
		quadro_->valor("il", num(il, 3), std::isfinite(il)
		    ? QStringLiteral("cada R$ 1 investido volta como %1").arg(moeda(il)) : QString());
		auto periodos = [](double p) {
			return std::isnan(p) ? QStringLiteral("não se paga") : num(p, 2) + " períodos";
		};
		quadro_->valor("pb", periodos(contas::payback(f, 0)), "sem descontar os juros");
		quadro_->valor("pbd", periodos(contas::payback(f, tma)), "descontado à TMA");
		const double vaue = std::fabs(tma) < 1e-12 ? vpl / n : vpl * tma / (1 - std::pow(1 + tma, -n));
		quadro_->valor("vaue", moeda(vaue), QStringLiteral("o VPL em %1 parcelas iguais").arg(n));
		if (vpl > 0.005)
			aviso_->setText(QStringLiteral("O VPL é positivo: à TMA de %1, o projeto paga a taxa mínima e "
			    "ainda cria %2 de valor, em dinheiro de hoje. Vale a pena.").arg(pct(tma), moeda(vpl)));
		else if (vpl < -0.005)
			aviso_->setText(QStringLiteral("O VPL é negativo: o projeto não chega a render a TMA de %1. "
			    "Aplicar o dinheiro a essa taxa seria melhor.").arg(pct(tma)));
		else
			aviso_->setText("O VPL é zero: o projeto rende exatamente a TMA.");

		// O perfil do VPL, de 0% até um pouco depois da maior taxa que importa.
		double fim = std::max(0.3, 2 * tma);
		for (double t : tirs)
			if (t > 0 && t < 5)
				fim = std::max(fim, t * 1.6);
		Serie perfil{"VPL", {}, Serie::Linha, false, false};
		const double ini = std::min(0.0, tma);
		for (int k = 0; k <= 120; k++) {
			const double r = ini + (fim - ini) * k / 120;
			perfil.pontos << QPointF(r * 100, contas::vpl(f, r));
		}
		QVector<Marca> marcas{{tma * 100, "TMA"}};
		for (double t : tirs)
			if (t * 100 >= ini * 100 && t <= fim)
				marcas << Marca{t * 100, "TIR"};
		perfil_->definir("taxa de desconto", "VPL (R$)", {perfil}, marcas, true);

		Serie fl{"fluxo", {}, Serie::Barras, false, true}, ac{"acumulado descontado", {}, Serie::Linha, false, false};
		double soma = 0;
		for (int t = 0; t < f.size(); t++) {
			fl.pontos << QPointF(t, f[t]);
			soma += f[t] / std::pow(1 + tma, t);
			ac.pontos << QPointF(t, soma);
		}
		barras_->definir("período", "R$", {fl, ac}, {}, true, true);
	}

	QLineEdit *tma_, *reinv_;
	QTableWidget *fluxos_;
	Quadro *quadro_;
	QLabel *aviso_;
	Grafico *perfil_, *barras_;
};

// --- 4. Inflação e índices ---

class PaginaInflacao : public Pagina {
public:
	PaginaInflacao()
	    : Pagina("4", "Inflação e índices",
		  "Quanto vale hoje o dinheiro de antes, e os números do dia, direto do Banco Central.")
	{
		parte("Os indicadores");
		indicadores_ = new Quadro(4);
		for (const Indicador &i : INDICADORES)
			indicadores_->item(QString::number(i.serie), QString::fromUtf8(i.nome));
		v->addWidget(indicadores_);
		auto *h = new QHBoxLayout;
		auto *atualizar = botao("Atualizar");
		fonteInd_ = rotulo(QString(), "detalhe");
		h->addWidget(atualizar);
		h->addWidget(fonteInd_, 1);
		v->addLayout(h);

		parte("Corrigir um valor",
		    "Os dois meses entram na conta, como na Calculadora do Cidadão do Banco Central.");
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(8);
		valor_ = campo("1.000");
		indice_ = new QComboBox;
		for (const Indice &i : INDICES)
			indice_->addItem(QStringLiteral("%1 — %2").arg(QString::fromUtf8(i.nome), QString::fromUtf8(i.descricao)));
		const QDate hoje = QDate::currentDate();
		ini_ = new QDateEdit(QDate(hoje.year() - 5, hoje.month(), 1));
		fim_ = new QDateEdit(QDate(hoje.year(), hoje.month(), 1).addMonths(-2));
		for (QDateEdit *d : {ini_, fim_}) {
			d->setDisplayFormat("MM/yyyy");
			d->setButtonSymbols(QAbstractSpinBox::NoButtons);
			d->setMinimumWidth(110);
			d->setMinimumDate(QDate(1980, 1, 1));
			d->setMaximumDate(QDate(hoje.year() + 1, 12, 1));
		}
		g->addWidget(rotulo("valor (R$)"), 0, 0);
		g->addWidget(valor_, 0, 1);
		g->addWidget(rotulo("índice"), 1, 0);
		g->addWidget(indice_, 1, 1, 1, 3);
		g->addWidget(rotulo("de"), 2, 0);
		g->addWidget(ini_, 2, 1, Qt::AlignLeft);
		g->addWidget(rotulo("até"), 3, 0);
		g->addWidget(fim_, 3, 1, Qt::AlignLeft);
		g->setColumnStretch(4, 1);
		v->addLayout(g);
		auto *h2 = new QHBoxLayout;
		corrigir_ = botao("Corrigir", "primario");
		h2->addWidget(corrigir_);
		h2->addStretch(1);
		v->addLayout(h2);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		quadro_ = new Quadro(4);
		quadro_->item("valor", "Valor corrigido");
		quadro_->item("var", "Variação no período");
		quadro_->item("fator", "Fator");
		quadro_->item("media", "Média por mês");
		v->addWidget(quadro_);
		grafico_ = new Grafico;
		grafico_->formatoY = [](double y) { return rotuloEixo(y, 1); };
		v->addWidget(grafico_);
		v->addStretch(1);

		connect(atualizar, &QPushButton::clicked, this, [this] { buscarIndicadores(); });
		connect(corrigir_, &QPushButton::clicked, this, [this] { buscarCorrecao(); });
		connect(valor_, &QLineEdit::returnPressed, this, [this] { buscarCorrecao(); });
	}

	// Na primeira vez que a página aparece (para não ir à rede à toa).
	void mostrada()
	{
		if (primeira_) {
			primeira_ = false;
			buscarIndicadores();
		}
	}

private:
	void buscarIndicadores()
	{
		fonteInd_->setText("Buscando no Banco Central…");
		auto *faltam = new int(int(sizeof INDICADORES / sizeof INDICADORES[0]));
		auto *avisos = new QStringList;
		for (const Indicador &i : INDICADORES)
			buscarSgs(i.serie, true, this, [this, i, faltam, avisos](const QVector<Ponto> &p, const QString &aviso) {
				const QString k = QString::number(i.serie);
				if (p.isEmpty())
					indicadores_->valor(k, QStringLiteral("—"));
				else
					indicadores_->valor(k, i.porcento ? num(p.last().valor) + "%" : moeda(p.last().valor),
					    QStringLiteral("%1 · %2").arg(QString::fromUtf8(i.detalhe),
						p.last().data.toString("dd/MM/yyyy")));
				if (!aviso.isEmpty() && !avisos->contains(aviso))
					*avisos << aviso;
				if (--*faltam == 0) {
					fonteInd_->setText(avisos->isEmpty()
					    ? QStringLiteral("Fonte: Banco Central do Brasil, Sistema Gerenciador de Séries (SGS).")
					    : avisos->join(" "));
					delete faltam;
					delete avisos;
				}
			});
	}

	void buscarCorrecao()
	{
		bool ok;
		const double valor = valorDe(valor_, &ok);
		if (!ok) {
			aviso_->setText("Preencha o valor.");
			return;
		}
		const Indice ind = INDICES[indice_->currentIndex()];
		const QDate ini = ini_->date(), fim = fim_->date();
		const QDate a(ini.year(), ini.month(), 1), b(fim.year(), fim.month(), 1);
		aviso_->setText(QStringLiteral("Buscando o %1 no Banco Central…").arg(QString::fromUtf8(ind.nome)));
		corrigir_->setEnabled(false);
		QPointer<PaginaInflacao> eu(this);
		buscarSgs(ind.serie, false, this, [eu, ind, a, b, valor](const QVector<Ponto> &p, const QString &aviso) {
			if (!eu)
				return;
			eu->corrigir_->setEnabled(true);
			if (p.isEmpty()) {
				eu->aviso_->setText(aviso);
				return;
			}
			const Correcao c = corrigir(p, ind, a, b);
			if (!c.erro.isEmpty()) {
				eu->aviso_->setText(c.erro);
				eu->quadro_->limpar();
				eu->grafico_->limpar();
				return;
			}
			const int n = c.meses.size();
			eu->quadro_->valor("valor", moeda(valor * c.fator),
			    QStringLiteral("%1 em %2 valem isso em %3").arg(moeda(valor), mesAno(a), mesAno(b)));
			eu->quadro_->valor("var", pct(c.fator - 1), QStringLiteral("%1 meses de %2").arg(n).arg(QString::fromUtf8(ind.nome)));
			eu->quadro_->valor("fator", num(c.fator, 7));
			eu->quadro_->valor("media", pct(std::pow(c.fator, 1.0 / n) - 1, 4),
			    QStringLiteral("%1 ao ano").arg(pct(std::pow(c.fator, 12.0 / n) - 1)));
			eu->aviso_->setText((aviso.isEmpty() ? QString() : aviso + " ") +
			    QStringLiteral("Fonte: Banco Central do Brasil, série %1 do SGS; o último dado é de %2.")
			    .arg(ind.serie).arg(mesAno(p.last().data)));
			Serie s{QString(), {}, Serie::Linha, false, false};
			for (int k = 0; k < n; k++)
				s.pontos << QPointF(k, valor * c.meses[k].valor);
			eu->grafico_->formatoX = [a](double x) { return mesAno(a.addMonths(int(std::lround(x)))); };
			eu->grafico_->definir("mês", "valor corrigido (R$)", {s}, {}, false, true);
		});
	}

	bool primeira_ = true;
	Quadro *indicadores_, *quadro_;
	QLabel *fonteInd_, *aviso_;
	QLineEdit *valor_;
	QComboBox *indice_;
	QDateEdit *ini_, *fim_;
	QPushButton *corrigir_;
	Grafico *grafico_;
};

// --- 5. Estatística e regressão ---

class PaginaEstatistica : public Pagina {
public:
	PaginaEstatistica()
	    : Pagina("5", "Estatística e regressão",
		  "Cole os dados da planilha, veja a estatística descritiva e estime uma regressão por MQO.")
	{
		auto *b = new QHBoxLayout;
		auto *abrir = botao("Abrir CSV…"), *colar = botao("Colar da planilha"),
		     *exemplo = botao("Exemplo: consumo e renda"), *coluna = botao("+ coluna"),
		     *linhas = botao("+ 10 linhas"), *limpar = botao("Limpar");
		for (QPushButton *x : {abrir, colar, exemplo, coluna, linhas, limpar})
			b->addWidget(x);
		b->addStretch(1);
		v->addLayout(b);
		dados_ = new QTableWidget(20, 3);
		dados_->setHorizontalHeaderLabels({"y", "x1", "x2"});
		dados_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		dados_->verticalHeader()->setDefaultSectionSize(24);
		dados_->setMinimumHeight(280);
		v->addWidget(dados_);
		v->addWidget(rotulo("Dois cliques no nome de uma coluna para trocar o nome. Linhas com "
		    "algum dado faltando ficam de fora da regressão.", "detalhe"));

		parte("O modelo");
		auto *m = new QHBoxLayout;
		m->setSpacing(20);
		auto *mY = new QVBoxLayout;
		mY->addWidget(rotulo("VARIÁVEL DEPENDENTE (Y)"));
		y_ = new QComboBox;
		mY->addWidget(y_);
		logs_ = new QCheckBox("Em logaritmos (os coeficientes viram elasticidades)");
		mY->addWidget(logs_);
		auto *bEstimar = botao("Estimar", "primario");
		mY->addWidget(bEstimar, 0, Qt::AlignLeft);
		mY->addStretch(1);
		m->addLayout(mY);
		auto *mX = new QVBoxLayout;
		mX->addWidget(rotulo("EXPLICATIVAS (X)"));
		xs_ = new QListWidget;
		xs_->setObjectName("variaveis");
		xs_->setMaximumHeight(130);
		mX->addWidget(xs_);
		mX->addStretch(1);
		m->addLayout(mX, 1);
		v->addLayout(m);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);

		parte("O resultado");
		relatorio_ = new QPlainTextEdit;
		relatorio_->setReadOnly(true);
		relatorio_->setMinimumHeight(260);
		relatorio_->setPlaceholderText("O relatório da regressão aparece aqui.");
		v->addWidget(relatorio_);
		auto *copiar = botao("Copiar o relatório");
		v->addWidget(copiar, 0, Qt::AlignLeft);
		grafico_ = new Grafico(QStringLiteral("Com uma explicativa, o gráfico de dispersão e a reta; com "
		    "mais, os resíduos."));
		v->addWidget(grafico_);

		parte("Estatística descritiva");
		descritiva_ = tabela({"variável", "n", "média", "mediana", "desvio-padrão", "mínimo", "máximo"}, 160);
		v->addWidget(descritiva_);
		parte("Correlações");
		correlacoes_ = new QTableWidget;
		correlacoes_->setMinimumHeight(160);
		correlacoes_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		correlacoes_->setMaximumHeight(260);
		v->addWidget(correlacoes_);
		v->addStretch(1);

		connect(abrir, &QPushButton::clicked, this, [this] {
			const QString arq = QFileDialog::getOpenFileName(this, "Abrir dados", QDir::homePath(),
			    "Tabelas (*.csv *.tsv *.txt);;Todos (*)");
			if (arq.isEmpty())
				return;
			QFile f(arq);
			if (f.open(QIODevice::ReadOnly))
				carregar(lerTabela(QString::fromUtf8(f.readAll())));
		});
		connect(colar, &QPushButton::clicked, this, [this] {
			const Dados d = lerTabela(QGuiApplication::clipboard()->text());
			if (d.linhas.isEmpty())
				aviso_->setText("Copie as células na planilha (com os nomes na primeira linha) e clique em “Colar”.");
			else
				carregar(d);
		});
		connect(exemplo, &QPushButton::clicked, this, [this] { carregarExemplo(); });
		connect(coluna, &QPushButton::clicked, this, [this] {
			const int c = dados_->columnCount();
			dados_->insertColumn(c);
			dados_->setHorizontalHeaderItem(c, new QTableWidgetItem(QStringLiteral("v%1").arg(c + 1)));
			atualizarVariaveis();
		});
		connect(linhas, &QPushButton::clicked, this, [this] { dados_->setRowCount(dados_->rowCount() + 10); });
		connect(limpar, &QPushButton::clicked, this, [this] {
			dados_->clearContents();
			relatorio_->clear();
			grafico_->limpar();
			descritiva_->setRowCount(0);
			correlacoes_->clear();
			correlacoes_->setRowCount(0);
			correlacoes_->setColumnCount(0);
		});
		connect(dados_->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int c) {
			bool ok;
			const QString n = QInputDialog::getText(this, "Nome da coluna", "Nome:", QLineEdit::Normal,
			    nomeColuna(c), &ok).trimmed();
			if (ok && !n.isEmpty()) {
				dados_->setHorizontalHeaderItem(c, new QTableWidgetItem(n));
				atualizarVariaveis();
			}
		});
		connect(bEstimar, &QPushButton::clicked, this, [this] { estimar(); });
		connect(copiar, &QPushButton::clicked, this, [this] {
			QGuiApplication::clipboard()->setText(relatorio_->toPlainText());
		});
		atualizarVariaveis();
	}

private:
	QString nomeColuna(int c) const
	{
		const QTableWidgetItem *i = dados_->horizontalHeaderItem(c);
		return i && !i->text().isEmpty() ? i->text() : QStringLiteral("v%1").arg(c + 1);
	}

	Dados lerGrade() const
	{
		Dados d;
		for (int c = 0; c < dados_->columnCount(); c++)
			d.nomes << nomeColuna(c);
		for (int l = 0; l < dados_->rowCount(); l++) {
			QStringList r;
			bool algo = false;
			for (int c = 0; c < dados_->columnCount(); c++) {
				const QTableWidgetItem *i = dados_->item(l, c);
				r << (i ? i->text() : QString());
				algo = algo || !r.last().trimmed().isEmpty();
			}
			if (algo)
				d.linhas << r;
		}
		return d;
	}

	void carregar(const Dados &d)
	{
		dados_->clear();
		dados_->setColumnCount(d.nomes.size());
		dados_->setRowCount(std::max<int>(20, d.linhas.size() + 5));
		dados_->setHorizontalHeaderLabels(d.nomes);
		for (int l = 0; l < d.linhas.size(); l++)
			for (int c = 0; c < d.linhas[l].size() && c < d.nomes.size(); c++)
				dados_->setItem(l, c, new QTableWidgetItem(d.linhas[l][c]));
		atualizarVariaveis();
		if (d.nomes.size() >= 2) {
			y_->setCurrentIndex(0);
			for (int k = 0; k < xs_->count(); k++)
				xs_->item(k)->setCheckState(k == 0 ? Qt::Unchecked : Qt::Checked);
		}
		aviso_->setText(QStringLiteral("%1 linhas e %2 colunas.").arg(d.linhas.size()).arg(d.nomes.size()));
	}

	// A função consumo keynesiana: C = a + b·Y − c·juros, com um ruído fixo
	// (os mesmos números sempre, para a aula e para conferir).
	void carregarExemplo()
	{
		static const double ruido[] = {1.2, -0.8, 0.5, -1.5, 2.0, -0.3, 0.7, -1.1, 0.4, 1.6, -0.9, -0.2,
					       0.9, -1.3, 0.2, 1.1, -0.6, 0.3, -1.7, 0.8};
		static const double juros[] = {13.75, 13.75, 13.25, 12.75, 12.25, 11.75, 11.25, 10.75, 10.5, 10.5,
					       11.25, 12.25, 13.0, 13.75, 14.25, 14.25, 13.75, 12.5, 11.5, 10.75};
		Dados d;
		d.nomes = QStringList{"consumo", "renda", "juros"};
		for (int k = 0; k < 20; k++) {
			const double renda = 1000 + 45 * k + (k % 3) * 20;
			const double c = 120 + 0.72 * renda - 6.5 * juros[k] + 9 * ruido[k];
			d.linhas << QStringList{num(c, 2), num(renda, 2), num(juros[k], 2)};
		}
		carregar(d);
		logs_->setChecked(false);
		estimar();
	}

	void atualizarVariaveis()
	{
		const QString antes = y_->currentText();
		QSet<QString> marcadas;
		for (int k = 0; k < xs_->count(); k++)
			if (xs_->item(k)->checkState() == Qt::Checked)
				marcadas << xs_->item(k)->text();
		y_->clear();
		xs_->clear();
		for (int c = 0; c < dados_->columnCount(); c++) {
			const QString n = nomeColuna(c);
			y_->addItem(n);
			auto *i = new QListWidgetItem(n);
			i->setFlags(i->flags() | Qt::ItemIsUserCheckable);
			i->setCheckState(marcadas.contains(n) ? Qt::Checked : Qt::Unchecked);
			xs_->addItem(i);
		}
		const int k = y_->findText(antes);
		if (k >= 0)
			y_->setCurrentIndex(k);
	}

	void estimar()
	{
		const Dados d = lerGrade();
		const int cy = y_->currentIndex();
		QVector<int> cx;
		QStringList nomes;
		for (int k = 0; k < xs_->count(); k++)
			if (xs_->item(k)->checkState() == Qt::Checked && k != cy) {
				cx << k;
				nomes << d.nomes[k];
			}
		descrever(d);
		if (cy < 0 || cx.isEmpty()) {
			aviso_->setText("Escolha a variável dependente e marque pelo menos uma explicativa.");
			return;
		}
		const bool logs = logs_->isChecked();
		const Amostra a = montarAmostra(d, cy, cx, logs);
		if (!a.erro.isEmpty()) {
			aviso_->setText(a.erro);
			return;
		}
		const contas::Regressao r = contas::mqo(a.y, a.xs, nomes);
		if (!r.erro.isEmpty()) {
			aviso_->setText(r.erro);
			relatorio_->clear();
			grafico_->limpar();
			return;
		}
		relatorio_->setPlainText(contas::relatorio(r, d.nomes[cy], logs, a.descartadas));
		// Uma frase sobre o primeiro X, para quem está aprendendo.
		const QString x = nomes[0], y = d.nomes[cy];
		const QString signif = r.p[1] < 0.05 ? QStringLiteral("e o efeito é estatisticamente significativo a 5%")
						     : QStringLiteral("mas o efeito não é significativo a 5% (p = %1)").arg(num(r.p[1], 3));
		if (logs)
			aviso_->setText(QStringLiteral("Com 1% a mais de %1, %2 muda %3%%4, %5. O modelo explica %6 da variação.")
			    .arg(x, y, num(r.b[1], 3), cx.size() > 1 ? QStringLiteral(" (o resto constante)") : QString(),
				signif, pct(r.r2, 1)));
		else
			aviso_->setText(QStringLiteral("Cada unidade a mais de %1 muda %2 em %3%4, %5. O modelo explica %6 da variação.")
			    .arg(x, y, num(r.b[1], 4), cx.size() > 1 ? QStringLiteral(" (o resto constante)") : QString(),
				signif, pct(r.r2, 1)));
		const QString pre = logs ? QStringLiteral("ln ") : QString();
		if (cx.size() == 1) {
			Serie pts{"observado", {}, Serie::Pontos, false, false};
			Serie reta{"ajustado", {}, Serie::Linha, false, false};
			QVector<QPair<double, double>> ord;
			for (int k = 0; k < a.y.size(); k++) {
				pts.pontos << QPointF(a.xs[0][k], a.y[k]);
				ord << qMakePair(a.xs[0][k], r.ajustados[k]);
			}
			std::sort(ord.begin(), ord.end());
			for (const auto &o : ord)
				reta.pontos << QPointF(o.first, o.second);
			grafico_->formatoX = nullptr;
			grafico_->definir(pre + x, pre + y, {pts, reta});
		} else {
			Serie res{"resíduo", {}, Serie::Pontos, false, false};
			for (int k = 0; k < a.y.size(); k++)
				res.pontos << QPointF(r.ajustados[k], r.residuos[k]);
			grafico_->formatoX = nullptr;
			grafico_->definir(QStringLiteral("valor ajustado de %1%2").arg(pre, y), "resíduo", {res}, {}, true);
		}
	}

	void descrever(const Dados &d)
	{
		QVector<QVector<double>> col(d.nomes.size());
		QVector<int> numericas;
		for (int c = 0; c < d.nomes.size(); c++) {
			for (const QStringList &l : d.linhas) {
				bool ok;
				const double x = lerNumero(c < l.size() ? l[c] : QString(), &ok);
				if (ok)
					col[c] << x;
			}
			if (!col[c].isEmpty())
				numericas << c;
		}
		descritiva_->setRowCount(numericas.size());
		for (int k = 0; k < numericas.size(); k++) {
			const int c = numericas[k];
			const contas::Descritiva s = contas::descrever(col[c]);
			auto *n = celula(d.nomes[c]);
			n->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			descritiva_->setItem(k, 0, n);
			descritiva_->setItem(k, 1, celula(QString::number(s.n)));
			descritiva_->setItem(k, 2, celula(num(s.media, 4)));
			descritiva_->setItem(k, 3, celula(num(s.mediana, 4)));
			descritiva_->setItem(k, 4, celula(num(s.dp, 4)));
			descritiva_->setItem(k, 5, celula(num(s.min, 4)));
			descritiva_->setItem(k, 6, celula(num(s.max, 4)));
		}
		descritiva_->verticalHeader()->setVisible(false);

		// As correlações nas linhas em que as duas colunas têm número.
		QStringList nomes;
		for (int c : numericas)
			nomes << d.nomes[c];
		correlacoes_->clear();
		correlacoes_->setRowCount(numericas.size());
		correlacoes_->setColumnCount(numericas.size());
		correlacoes_->setHorizontalHeaderLabels(nomes);
		correlacoes_->setVerticalHeaderLabels(nomes);
		for (int a = 0; a < numericas.size(); a++)
			for (int b = 0; b < numericas.size(); b++) {
				QVector<double> xa, xb;
				for (const QStringList &l : d.linhas) {
					bool o1, o2;
					const int ca = numericas[a], cb = numericas[b];
					const double va = lerNumero(ca < l.size() ? l[ca] : QString(), &o1);
					const double vb = lerNumero(cb < l.size() ? l[cb] : QString(), &o2);
					if (o1 && o2) {
						xa << va;
						xb << vb;
					}
				}
				correlacoes_->setItem(a, b, celula(num(contas::correlacao(xa, xb), 3)));
			}
	}

	QTableWidget *dados_, *descritiva_, *correlacoes_;
	QComboBox *y_;
	QListWidget *xs_;
	QCheckBox *logs_;
	QLabel *aviso_;
	QPlainTextEdit *relatorio_;
	Grafico *grafico_;
};

// --- A janela ---------------------------------------------------------------

class Janela : public QWidget {
public:
	Janela()
	{
		setObjectName("raiz");
		setWindowTitle("Finanças do LumeBSD");
		setWindowIcon(QIcon("/usr/share/lume/app/lume-financas.svg"));
		resize(1200, 820);
		auto *v = new QVBoxLayout(this);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);

		auto *topo = new QVBoxLayout;
		topo->setContentsMargins(32, 22, 32, 18);
		topo->setSpacing(0);
		topo->addWidget(rotulo("Finanças", "marca"));
		topo->addWidget(rotulo("do LumeBSD, para estudar, ensinar e decidir", "sub"));
		v->addLayout(topo);
		v->addWidget(fio());

		auto *corpo = new QHBoxLayout;
		corpo->setContentsMargins(0, 0, 0, 0);
		corpo->setSpacing(0);
		auto *indice = new QListWidget;
		indice->setObjectName("indice");
		indice->setFixedWidth(250);
		indice->setFocusPolicy(Qt::NoFocus);
		auto *paginas = new QStackedWidget;
		auto *inflacao = new PaginaInflacao;
		const QList<QPair<QString, QWidget *>> capitulos = {
			{"1   Juros e taxas", new PaginaJuros},
			{"2   Financiamento", new PaginaFinanciamento},
			{"3   Projetos", new PaginaProjetos},
			{"4   Inflação e índices", inflacao},
			{"5   Estatística e regressão", new PaginaEstatistica},
		};
		for (const auto &c : capitulos) {
			indice->addItem(c.first);
			paginas->addWidget(c.second);
		}
		auto *esq = new QVBoxLayout;
		esq->setContentsMargins(0, 18, 0, 18);
		auto *cap = rotulo("CAPÍTULOS");
		cap->setContentsMargins(18, 0, 16, 0);
		esq->addWidget(cap);
		esq->addSpacing(6);
		esq->addWidget(indice, 1);
		auto *nota = rotulo("As contas ficam no seu computador; só os índices vêm do Banco Central.", "detalhe");
		nota->setContentsMargins(18, 0, 16, 0);
		esq->addWidget(nota);
		corpo->addLayout(esq);
		corpo->addWidget(fio(true));
		corpo->addWidget(paginas, 1);
		v->addLayout(corpo, 1);

		connect(indice, &QListWidget::currentRowChanged, this, [paginas, inflacao](int k) {
			paginas->setCurrentIndex(k);
			if (paginas->currentWidget() == inflacao)
				inflacao->mostrada();
		});
		indice->setCurrentRow(0);
		indice_ = indice;
	}

	void irPara(int k) { indice_->setCurrentRow(k); }

private:
	QListWidget *indice_;
};

int
main(int argc, char *argv[])
{
	// A linha de comando: sem janela, sem precisar de tela.
	if (argc > 1 && QByteArray(argv[1]).startsWith("--") && QByteArray(argv[1]) != "--capitulo") {
		QCoreApplication app(argc, argv);
		app.setApplicationName("lume-financas");
		const int r = linhaDeComando(app, app.arguments());
		return r < 0 ? uso() : r;
	}

	QApplication app(argc, argv);
	QTranslator qt;
	if (qt.load(QLocale(), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qt);
	app.setApplicationName("lume-financas");
	app.setApplicationDisplayName("Finanças do LumeBSD");
	app.setDesktopFileName("lume-financas");
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	    [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });
	QLocale::setDefault(br());

	// Papel e tinta, seja qual for o tema; com o Papel noite, a mesma cara
	// à noite.
	const bool noite = app.palette().color(QPalette::Window).lightness() < 128 ||
	    qEnvironmentVariable("LUME_TEMA") == "escuro";
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette pal;
	const QColor papel = noite ? QColor(0x1e, 0x1d, 0x1b) : QColor(0xef, 0xea, 0xdf);
	const QColor tinta = noite ? QColor(0xde, 0xda, 0xd0) : QColor(0x1c, 0x1b, 0x19);
	const QColor cartao = noite ? QColor(0x27, 0x26, 0x23) : QColor(0xf6, 0xf2, 0xe9);
	pal.setColor(QPalette::Window, papel);
	pal.setColor(QPalette::Base, cartao);
	pal.setColor(QPalette::AlternateBase, papel);
	pal.setColor(QPalette::Button, papel);
	pal.setColor(QPalette::Midlight, noite ? QColor(0x34, 0x32, 0x2e) : QColor(0xe2, 0xdc, 0xcd));
	for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipBase})
		pal.setColor(r, tinta);
	pal.setColor(QPalette::Highlight, tinta);
	pal.setColor(QPalette::HighlightedText, papel);
	pal.setColor(QPalette::PlaceholderText, noite ? QColor(0x7c, 0x77, 0x6d) : QColor(0x8a, 0x85, 0x7a));
	app.setPalette(pal);
	app.setStyleSheet(noite ? estiloNoite() : QString::fromUtf8(ESTILO));

	Janela j;
	const QStringList a = app.arguments();
	const int c = a.indexOf("--capitulo");
	if (c >= 0 && c + 1 < a.size())
		j.irPara(std::clamp(a[c + 1].toInt() - 1, 0, 4));
	j.show();
	return app.exec();
}

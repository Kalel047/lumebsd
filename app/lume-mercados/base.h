// SPDX-License-Identifier: BSD-2-Clause
//
// base.h - o que as telas dos Mercados do LumeBSD têm em comum: os números
// em português, a folha de papel e tinta, o gráfico, os quadros e a rede
// (com o que já veio guardado em ~/.cache/lume-mercados, para abrir sem
// internet com os últimos dados).

#pragma once

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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QSplitter>
#include <QTabWidget>
#include <QWheelEvent>
#include <QXmlStreamReader>
#include <QStringConverter>
#include <QTextBrowser>

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

static const char *const MESES[] = {"jan", "fev", "mar", "abr", "mai", "jun",
				    "jul", "ago", "set", "out", "nov", "dez"};

// --- A cara: papel e tinta ----------------------------------------------------

// A mesma folha dos outros apps do LumeBSD (os Programas): serifa nos
// títulos, mono nos rótulos e nos números, fios finos em vez de sombras.
static const char *FOLHA = R"QSS(
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
	QString e = QString::fromUtf8(FOLHA);
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
			// Um ponto sem valor quebra a linha em pedaços.
			p.setPen(QPen(c, 1.6, s.tracejada ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			QPolygonF l;
			for (const QPointF &pt : s.pontos) {
				if (std::isfinite(pt.y()))
					l << QPointF(px(pt.x()), py(pt.y()));
				else {
					p.drawPolyline(l);
					l.clear();
				}
			}
			p.drawPolyline(l);
		} else if (s.tipo == Serie::Pontos) {
			p.setPen(QPen(c, 1.2));
			p.setBrush(Qt::NoBrush);
			for (const QPointF &pt : s.pontos)
				if (std::isfinite(pt.y()))
					p.drawEllipse(QPointF(px(pt.x()), py(pt.y())), 3, 3);
		} else {
			const double total = std::max(1.0, area.width() / std::max(1.0, x1 - x0)) * 0.72;
			const double larg = total / std::max(1, nbarras);
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			for (const QPointF &pt : s.pontos) {
				if (!std::isfinite(pt.y()))
					continue;
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

// --- A rede ------------------------------------------------------------------

// Tudo o que vem da internet passa por aqui: guarda o que veio em
// ~/.cache/lume-mercados (o nome é o MD5 do endereço) e, sem rede, entrega
// o guardado com um aviso de quando é. LUME_MERCADOS_DADOS=PASTA lê dali em
// vez da internet (os testes).
static QString
chaveUrl(const QString &url)
{
	return QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
}

static QString
arquivoDe(const QString &url)
{
	if (const char *dir = getenv("LUME_MERCADOS_DADOS"))
		return QString::fromLocal8Bit(dir) + "/" + chaveUrl(url);
	const QString pasta = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	QDir().mkpath(pasta);
	return pasta + "/" + chaveUrl(url);
}

static QNetworkAccessManager *
rede()
{
	static QNetworkAccessManager *m = new QNetworkAccessManager(qApp);
	return m;
}

using Resposta = std::function<void(const QByteArray &, const QString &aviso)>;

static void
buscar(const QString &url, int validadeSeg, QObject *dono, const Resposta &f, bool segundaVez = false)
{
	const QString arq = arquivoDe(url);
	auto guardado = [arq](bool *ok) {
		QFile a(arq);
		*ok = a.open(QIODevice::ReadOnly);
		return *ok ? a.readAll() : QByteArray();
	};
	if (getenv("LUME_MERCADOS_DADOS")) {
		bool ok;
		const QByteArray d = guardado(&ok);
		QTimer::singleShot(0, dono, [f, d, ok] { f(d, ok ? QString() : QStringLiteral("Sem os dados de teste.")); });
		return;
	}
	const QFileInfo info(arq);
	if (info.exists() && info.lastModified().secsTo(QDateTime::currentDateTime()) < validadeSeg) {
		bool ok;
		const QByteArray d = guardado(&ok);
		if (ok) {
			QTimer::singleShot(0, dono, [f, d] { f(d, QString()); });
			return;
		}
	}
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, "lume-mercados/1.0 (LumeBSD; github.com/Kalel047/lumebsd)");
	req.setRawHeader("Digitraffic-User", "LumeBSD/lume-mercados");
	req.setTransferTimeout(45000);
	QNetworkReply *r = rede()->get(req);
	QObject::connect(r, &QNetworkReply::finished, dono, [=] {
		r->deleteLater();
		const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (r->error() == QNetworkReply::NoError && http >= 200 && http < 300) {
			const QByteArray d = r->readAll();
			QFile a(arq);
			if (a.open(QIODevice::WriteOnly | QIODevice::Truncate))
				a.write(d);
			f(d, QString());
			return;
		}
		// Um tropeço do servidor (502, 503): mais uma tentativa.
		if (http >= 500 && !segundaVez) {
			QTimer::singleShot(900, dono, [=] { buscar(url, 0, dono, f, true); });
			return;
		}
		bool ok;
		const QByteArray d = guardado(&ok);
		if (ok)
			f(d, QStringLiteral("Sem conexão com a fonte: dados guardados em %1.")
			    .arg(info.lastModified().toString(QStringLiteral("dd/MM/yyyy 'às' HH:mm"))));
		else
			f(QByteArray(), http == 429
			    ? QStringLiteral("A fonte pediu para esperar (muitas consultas). Tente daqui a pouco.")
			    : QStringLiteral("Não deu para buscar os dados (%1). O computador está na internet?")
				.arg(http ? QStringLiteral("HTTP %1").arg(http) : r->errorString()));
	});
}

// Tira um ou mais arquivos de dentro de um .zip (os dados abertos da CVM),
// um depois do outro, na saída. O tar do FreeBSD lê zip; fora dele, o bsdtar.
static void
extrairZip(const QString &zip, const QStringList &membros, QObject *dono,
    const std::function<void(const QByteArray &)> &f)
{
	auto *p = new QProcess(dono);
	QObject::connect(p, &QProcess::finished, dono, [p, f](int, QProcess::ExitStatus) {
		const QByteArray d = p->readAllStandardOutput();
		p->deleteLater();
		f(d);
	});
	QObject::connect(p, &QProcess::errorOccurred, dono, [p, f](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart)
			return;
		p->deleteLater();
		f(QByteArray());
	});
	const QString tar = QFile::exists("/usr/bin/bsdtar") ? "/usr/bin/bsdtar" : "tar";
	p->start(tar, QStringList{"-xOf", zip} + membros);
}

// Uma linha de CSV com ; e aspas.
static QStringList
partesCsv(const QString &l, QChar sep = QLatin1Char(';'))
{
	QStringList r;
	QString c;
	bool aspas = false;
	for (int i = 0; i < l.size(); i++) {
		const QChar ch = l[i];
		if (ch == QLatin1Char('"')) {
			if (aspas && i + 1 < l.size() && l[i + 1] == QLatin1Char('"')) {
				c += ch;
				i++;
			} else
				aspas = !aspas;
		} else if (ch == sep && !aspas) {
			r << c;
			c.clear();
		} else
			c += ch;
	}
	r << c;
	return r;
}

static QString
variacao(double fracao, int casas = 2)
{
	if (!std::isfinite(fracao))
		return QStringLiteral("—");
	return (fracao > 0 ? QStringLiteral("▲ ") : fracao < 0 ? QStringLiteral("▼ ") : QString()) + pct(std::fabs(fracao), casas);
}

// Uma tela: a página de uma função. `comando` leva a outra ("PETR4 G").
class Tela : public Pagina {
public:
	using Pagina::Pagina;
	virtual void abrir(const QStringList &) {}
	std::function<void(const QString &)> comando;
};

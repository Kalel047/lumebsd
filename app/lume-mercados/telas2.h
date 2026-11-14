// SPDX-License-Identifier: BSD-2-Clause
//
// telas2.h - a economia (Banco Central), o câmbio, as correlações, o ativo
// oposto e as relações entre as empresas.

#pragma once

#include "telas1.h"

// --- ECO: os indicadores da economia ----------------------------------------

struct SerieEco {
	int serie;
	const char *nome, *unidade;
};
static const SerieEco SERIES_ECO[] = {
	{432, "Selic, a meta", "% ao ano"},
	{13522, "IPCA em 12 meses", "%"},
	{433, "IPCA do mês", "%"},
	{189, "IGP-M do mês", "%"},
	{4389, "CDI", "% ao ano"},
	{1, "Dólar (PTAX, venda)", "R$"},
	{21619, "Euro (PTAX, venda)", "R$"},
	{24369, "Desemprego (PNAD Contínua)", "%"},
	{24363, "Atividade econômica (IBC-Br)", "índice"},
	{7326, "PIB, variação real no ano", "%"},
	{4380, "PIB do mês", "R$ milhões"},
	{4513, "Dívida líquida do setor público", "% do PIB"},
	{13762, "Dívida bruta do governo geral", "% do PIB"},
	{3546, "Reservas internacionais", "US$ milhões"},
	{22707, "Balança comercial, saldo do mês", "US$ milhões"},
	{20542, "Crédito, saldo total", "R$ milhões"},
};

static QVector<QPair<QDate, double>>
lerSgs(const QByteArray &json)
{
	QVector<QPair<QDate, double>> r;
	for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
		const QJsonObject o = v.toObject();
		const QDate d = QDate::fromString(o.value("data").toString(), "dd/MM/yyyy");
		bool ok;
		const double x = o.value("valor").toString().toDouble(&ok);
		if (d.isValid() && ok)
			r << qMakePair(d, x);
	}
	std::sort(r.begin(), r.end());
	return r;
}

class TelaEconomia : public Tela {
public:
	TelaEconomia() : Tela("ECO", "Economia", "Os indicadores do Brasil no Banco Central e o que o mercado espera (boletim Focus).")
	{
		parte("O que o mercado espera", "A mediana das projeções do boletim Focus para o fim de cada ano.");
		focus_ = tabela({"", "", ""}, 150);
		focus_->verticalHeader()->setVisible(false);
		focus_->setMaximumHeight(170);
		v->addWidget(focus_);
		focusAviso_ = rotulo(QString(), "detalhe");
		v->addWidget(focusAviso_);
		parte("Os indicadores");
		auto *h = new QHBoxLayout;
		h->setSpacing(18);
		lista_ = new QListWidget;
		lista_->setObjectName("variaveis");
		lista_->setFixedWidth(300);
		lista_->setMinimumHeight(420);
		for (const auto &s : SERIES_ECO)
			lista_->addItem(QString::fromUtf8(s.nome));
		h->addWidget(lista_);
		auto *d = new QVBoxLayout;
		quadro_ = new Quadro(3);
		quadro_->item("ultimo", "Último");
		quadro_->item("antes", "Um ano antes");
		quadro_->item("faixa", "Máxima e mínima em 10 anos");
		d->addWidget(quadro_);
		g_ = new Grafico("Escolha um indicador.");
		g_->setMinimumHeight(330);
		d->addWidget(g_, 1);
		h->addLayout(d, 1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "detalhe");
		v->addWidget(aviso_);
		v->addStretch(1);
		connect(lista_, &QListWidget::currentRowChanged, this, [this](int k) { serie(k); });
	}
	void abrir(const QStringList &) override
	{
		if (lista_->currentRow() < 0)
			lista_->setCurrentRow(0);
		focus();
	}

private:
	void serie(int k)
	{
		if (k < 0)
			return;
		const SerieEco s = SERIES_ECO[k];
		const int vez = ++vez_;
		aviso_->setText("Buscando no Banco Central…");
		// O Banco Central entrega no máximo dez anos de uma série diária.
		const QDate hoje = QDate::currentDate();
		const QString url = QStringLiteral("https://api.bcb.gov.br/dados/serie/bcdata.sgs.%1/dados?formato=json&dataInicial=%2&dataFinal=%3")
		    .arg(s.serie).arg(hoje.addYears(-10).addDays(7).toString("dd/MM/yyyy"), hoje.toString("dd/MM/yyyy"));
		buscar(url, 6 * 3600, this, [this, vez, s](const QByteArray &d, const QString &aviso) {
			if (vez != vez_)
				return;
			const auto p = lerSgs(d);
			if (p.isEmpty()) {
				aviso_->setText(aviso.isEmpty() ? QStringLiteral("A série veio vazia.") : aviso);
				g_->limpar();
				return;
			}
			const int casas = std::fabs(p.last().second) >= 1000 ? 0 : std::fabs(p.last().second) < 10 ? 4 : 2;
			const QString un = QString::fromUtf8(s.unidade);
			quadro_->valor("ultimo", num(p.last().second, casas == 4 ? 2 : casas) + " " + un, p.last().first.toString("dd/MM/yyyy"));
			const QDate antes = p.last().first.addYears(-1);
			for (int k = p.size() - 1; k >= 0; k--)
				if (p[k].first <= antes) {
					quadro_->valor("antes", num(p[k].second, casas == 4 ? 2 : casas) + " " + un, p[k].first.toString("dd/MM/yyyy"));
					break;
				}
			double lo = INFINITY, hi = -INFINITY;
			Serie l{QString(), {}, Serie::Linha, false, false};
			// Uma série diária de dez anos tem 2.500 pontos: um a cada tanto basta.
			const int passo = std::max<int>(1, int(p.size() / 600));
			QVector<QDate> datas;
			for (int k = 0; k < p.size(); k++) {
				lo = std::min(lo, p[k].second);
				hi = std::max(hi, p[k].second);
				if (k % passo == 0 || k == p.size() - 1) {
					l.pontos << QPointF(datas.size(), p[k].second);
					datas << p[k].first;
				}
			}
			quadro_->valor("faixa", num(hi, casas == 4 ? 2 : casas) + " · " + num(lo, casas == 4 ? 2 : casas));
			g_->formatoX = [datas](double x) {
				const int k = int(std::lround(x));
				return k >= 0 && k < datas.size() ? datas[k].toString("MM/yyyy") : QString();
			};
			g_->definir("", un, {l}, {}, false, true);
			aviso_->setText((aviso.isEmpty() ? QString() : aviso + " ") +
			    QStringLiteral("Fonte: Banco Central do Brasil, série %1 do SGS.").arg(s.serie));
		});
	}

	void focus()
	{
		static const char *const IND[] = {"IPCA", "PIB Total", "Câmbio", "Selic"};
		static const char *const NOME[] = {"IPCA (% no ano)", "PIB (% no ano)", "Dólar (R$, fim do ano)", "Selic (% ao ano, fim do ano)"};
		const int ano = QDate::currentDate().year();
		focus_->setRowCount(4);
		focus_->setHorizontalHeaderLabels({"", QString::number(ano), QString::number(ano + 1)});
		focus_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		for (int k = 0; k < 4; k++) {
			auto *n = celula(QString::fromUtf8(NOME[k]));
			n->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			focus_->setItem(k, 0, n);
			QUrl u("https://olinda.bcb.gov.br/olinda/servico/Expectativas/versao/v1/odata/ExpectativasMercadoAnuais");
			QUrlQuery q;
			q.addQueryItem("$top", "60");
			q.addQueryItem("$format", "json");
			q.addQueryItem("$orderby", "Data desc");
			q.addQueryItem("$filter", QStringLiteral("Indicador eq '%1' and baseCalculo eq 0").arg(QString::fromUtf8(IND[k])));
			u.setQuery(q);
			buscar(QString::fromLatin1(u.toEncoded()), 12 * 3600, this, [this, k, ano](const QByteArray &d, const QString &aviso) {
				QString data;
				for (const QJsonValue &v : QJsonDocument::fromJson(d).object().value("value").toArray()) {
					const QJsonObject o = v.toObject();
					if (data.isEmpty())
						data = o.value("Data").toString();
					if (o.value("Data").toString() != data)
						break;
					const int ref = o.value("DataReferencia").toString().toInt();
					if (ref == ano || ref == ano + 1)
						focus_->setItem(k, ref - ano + 1, celula(num(o.value("Mediana").toDouble(), 2)));
				}
				focusAviso_->setText(data.isEmpty() ? aviso
				    : QStringLiteral("Fonte: Banco Central do Brasil, Expectativas de Mercado (Focus), de %1.")
					.arg(QDate::fromString(data, "yyyy-MM-dd").toString("dd/MM/yyyy")));
			});
		}
	}

	QTableWidget *focus_;
	QListWidget *lista_;
	Quadro *quadro_;
	Grafico *g_;
	QLabel *aviso_, *focusAviso_;
	int vez_ = 0;
};

// --- CAMBIO: as moedas, umas nas outras ------------------------------------------

class TelaCambio : public Tela {
public:
	TelaCambio() : Tela("CAMBIO", "Câmbio", "Quanto vale uma moeda (a da linha) em cada uma das outras (a da coluna).")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		valor_ = campo("100");
		valor_->setMaximumWidth(140);
		de_ = new QComboBox;
		para_ = new QComboBox;
		for (int k = 0; k < N; k++) {
			de_->addItem(QLatin1String(MOEDAS[k]));
			para_->addItem(QLatin1String(MOEDAS[k]));
		}
		de_->setCurrentIndex(1);
		resultado_ = rotulo(QString(), "valor");
		resultado_->setWordWrap(false);
		h->addWidget(valor_);
		h->addWidget(de_);
		h->addWidget(rotulo("são", "texto"));
		h->addWidget(resultado_);
		h->addWidget(para_);
		h->addStretch(1);
		v->addLayout(h);
		QStringList cab;
		for (int k = 0; k < N; k++)
			cab << QLatin1String(MOEDAS[k]);
		t_ = tabela(cab, 330);
		t_->setVerticalHeaderLabels(cab);
		t_->setRowCount(N);
		t_->setVerticalHeaderLabels(cab);
		v->addWidget(t_);
		parte("Contra o real, hoje");
		dia_ = tabela({"moeda", "em reais", "no dia", "no mês", "tendência"}, 330);
		dia_->verticalHeader()->setVisible(false);
		v->addWidget(dia_);
		aviso_ = rotulo(QString(), "detalhe");
		v->addWidget(aviso_);
		v->addStretch(1);
		connect(valor_, &QLineEdit::textChanged, this, [this] { converter(); });
		connect(de_, &QComboBox::currentIndexChanged, this, [this] { converter(); });
		connect(para_, &QComboBox::currentIndexChanged, this, [this] { converter(); });
		connect(dia_, &QTableWidget::cellDoubleClicked, this, [this](int l, int) {
			if (comando)
				comando(dia_->item(l, 0)->text() + "BRL G");
		});
	}
	void abrir(const QStringList &) override
	{
		aviso_->setText("Buscando…");
		QStringList s;
		for (int k = 0; k < N; k++)
			if (k != 1)
				s << QLatin1String(MOEDAS[k]) + "=X";	// quantas dessa moeda por dólar
		buscarVarios(s, "3mo", "1d", this, [this](const QMap<QString, Cotacao> &q, const QString &aviso) {
			QVector<Cotacao> c(N);
			for (int k = 0; k < N; k++) {
				c[k] = q.value(QLatin1String(MOEDAS[k]) + "=X");
				porDolar_[k] = k == 1 ? 1 : c[k].ok ? c[k].preco : NAO;
			}
			for (int l = 0; l < N; l++)
				for (int k = 0; k < N; k++) {
					const double x = porDolar_[k] / porDolar_[l];
					t_->setItem(l, k, celula(l == k ? QStringLiteral("·") : num(x, x >= 100 ? 1 : x >= 1 ? 3 : 5)));
				}
			dia_->setRowCount(0);
			const Cotacao &real = c[0];
			for (int k = 1; k < N; k++) {
				if (!std::isfinite(porDolar_[k]) || !real.ok)
					continue;
				const int l = dia_->rowCount();
				dia_->insertRow(l);
				// A moeda em reais: reais por dólar ÷ moeda por dólar, dia a dia.
				Cotacao x;
				x.preco = real.preco / porDolar_[k];
				x.anterior = real.anterior / (k == 1 ? 1 : c[k].anterior);
				for (int i = 0; i < real.c.size(); i++) {
					const double outro = k == 1 ? 1 : (i < c[k].c.size() ? c[k].c[i] : NAO);
					x.c << real.c[i] / outro;
				}
				dia_->setItem(l, 0, celula(QLatin1String(MOEDAS[k])));
				dia_->setItem(l, 1, celula(num(x.preco, x.preco < 0.1 ? 5 : 4)));
				dia_->setItem(l, 2, celula(variacao(x.dia())));
				dia_->setItem(l, 3, celula(variacao(x.desde(21))));
				dia_->setItem(l, 4, celula(tendencia(x.c)));
			}
			aviso_->setText((aviso.isEmpty() ? QString() : aviso + " ") + QString::fromUtf8(FONTE_COTACOES));
			converter();
		});
	}

private:
	void converter()
	{
		bool ok;
		const double x = lerNumero(valor_->text(), &ok) * porDolar_[para_->currentIndex()] / porDolar_[de_->currentIndex()];
		resultado_->setText(ok && std::isfinite(x) ? num(x, 2) : QStringLiteral("—"));
	}

	static constexpr int N = int(sizeof MOEDAS / sizeof MOEDAS[0]);
	double porDolar_[N] = {};
	QLineEdit *valor_;
	QComboBox *de_, *para_;
	QLabel *resultado_, *aviso_;
	QTableWidget *t_, *dia_;
};

// --- CORR: as correlações ------------------------------------------------------

static const char *const PERIODOS_C[][2] = {{"6 meses", "6mo"}, {"1 ano", "1y"}, {"2 anos", "2y"}, {"5 anos", "5y"}};

class TelaCorrelacao : public Tela {
public:
	TelaCorrelacao() : Tela("CORR", "Correlações", "Quem anda junto com quem: +1 é igual, 0 é sem relação, −1 é o oposto. "
		"Com dois ativos, a regressão e o beta.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativos_ = campo("IBOV SPX DOLAR OURO BRENT BTC PETR4 VALE3 ITUB4", "ativos");
		ativos_->setMinimumWidth(460);
		periodo_ = new QComboBox;
		for (const auto &p : PERIODOS_C)
			periodo_->addItem(QString::fromUtf8(p[0]));
		periodo_->setCurrentIndex(1);
		auto *ir = botao("Calcular", "primario");
		h->addWidget(ativos_);
		h->addWidget(periodo_);
		h->addWidget(ir);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		t_ = new QTableWidget;
		t_->setMinimumHeight(330);
		t_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		v->addWidget(t_);
		parte("Dois a dois", "Clique numa casa da tabela: o ativo da linha explicado pelo da coluna.");
		quadro_ = new Quadro(4);
		quadro_->item("corr", "Correlação");
		quadro_->item("beta", "Beta");
		quadro_->item("r2", "R²");
		quadro_->item("n", "Dias em comum");
		v->addWidget(quadro_);
		g_ = new Grafico("Clique numa casa da tabela.");
		g_->setMinimumHeight(320);
		v->addWidget(g_);
		fonte_ = rotulo(QString(), "detalhe");
		v->addWidget(fonte_);
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { calcular(); });
		connect(ativos_, &QLineEdit::returnPressed, this, [this] { calcular(); });
		connect(periodo_, &QComboBox::currentIndexChanged, this, [this] { calcular(); });
		connect(t_, &QTableWidget::cellClicked, this, [this](int l, int c) { par(l, c); });
	}
	void abrir(const QStringList &a) override
	{
		if (a.size() >= 2)
			ativos_->setText(a.join(" "));
		else if (a.size() == 1) {
			const QString s = simboloDe(a.first());
			ativos_->setText(a.first() + (s.endsWith(".SA") ? " IBOV DOLAR" : " SPX") + " OURO BRENT");
		}
		calcular();
	}

private:
	void calcular()
	{
		const QStringList nomes = ativos_->text().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
		if (nomes.size() < 2) {
			aviso_->setText("Escreva pelo menos dois ativos.");
			return;
		}
		QStringList s;
		for (const QString &n : nomes)
			s << simboloDe(n);
		aviso_->setText("Buscando…");
		buscarVarios(s, PERIODOS_C[periodo_->currentIndex()][1], "1d", this, [this, s](const QMap<QString, Cotacao> &q, const QString &aviso) {
			q_.clear();
			QStringList cab, faltou;
			for (const QString &x : s)
				if (q.contains(x)) {
					q_ << q[x];
					cab << curto(x);
				} else
					faltou << curto(x);
			t_->clear();
			t_->setRowCount(cab.size());
			t_->setColumnCount(cab.size());
			t_->setHorizontalHeaderLabels(cab);
			t_->setVerticalHeaderLabels(cab);
			for (int a = 0; a < q_.size(); a++)
				for (int b = 0; b < q_.size(); b++) {
					const double c = a == b ? 1 : contas::relacionar(q_[a], q_[b]).correlacao;
					auto *i = celula(num(c, 2));
					// Mais tinta, mais relação (para um lado ou para o outro).
					if (a != b && std::isfinite(c)) {
						const bool noite = palette().color(QPalette::Window).lightness() < 128;
						QColor fundo = noite ? QColor(0xde, 0xda, 0xd0) : QColor(0x1c, 0x1b, 0x19);
						fundo.setAlphaF(std::min(1.0, std::fabs(c)) * 0.55);
						i->setBackground(fundo);
					}
					t_->setItem(a, b, i);
				}
			aviso_->setText((faltou.isEmpty() ? QString() : QStringLiteral("Não achei: %1. ").arg(faltou.join(", "))) + aviso +
			    QStringLiteral(" Correlação dos retornos diários, nos dias em que os dois negociaram."));
			fonte_->setText(QString::fromUtf8(FONTE_COTACOES));
			if (q_.size() >= 2)
				par(0, 1);
		});
	}

	void par(int l, int c)
	{
		if (l == c || l >= q_.size() || c >= q_.size())
			return;
		QVector<double> ry, rx;
		contas::alinhar(q_[l], q_[c], &ry, &rx);
		const contas::Relacao r = contas::relacionar(ry, rx);
		const QString y = curto(q_[l].simbolo), x = curto(q_[c].simbolo);
		quadro_->valor("corr", num(r.correlacao, 3), y + " e " + x);
		quadro_->valor("beta", num(r.beta, 3), QStringLiteral("quando %1 anda 1%, %2 anda %3%").arg(x, y, num(r.beta, 2)));
		quadro_->valor("r2", pct(r.r2, 1), QStringLiteral("do sobe e desce de %1 acompanha %2").arg(y, x));
		quadro_->valor("n", QString::number(r.n));
		Serie pts{"dias", {}, Serie::Pontos, false, false}, reta{"regressão", {}, Serie::Linha, false, false};
		double lo = INFINITY, hi = -INFINITY;
		for (int k = 0; k < r.n; k++) {
			pts.pontos << QPointF(rx[k] * 100, ry[k] * 100);
			lo = std::min(lo, rx[k]);
			hi = std::max(hi, rx[k]);
		}
		reta.pontos << QPointF(lo * 100, (r.alfa + r.beta * lo) * 100) << QPointF(hi * 100, (r.alfa + r.beta * hi) * 100);
		g_->formatoX = nullptr;
		g_->definir("retorno de " + x + " no dia (%)", "retorno de " + y + " (%)", {pts, reta});
	}

	QLineEdit *ativos_;
	QComboBox *periodo_;
	QLabel *aviso_, *fonte_;
	QTableWidget *t_;
	Quadro *quadro_;
	Grafico *g_;
	QVector<Cotacao> q_;
};

// --- OPOSTO: o ativo que anda ao contrário ---------------------------------------

// Onde procurar: índices, moedas, matérias-primas, juros, cripto, as ações
// mais negociadas e os fundos feitos para andar ao contrário de um índice.
static const struct {
	const char *simbolo, *nome, *inverso;
} UNIVERSO[] = {
	{"^BVSP", "Ibovespa", nullptr},
	{"^GSPC", "S&P 500", nullptr},
	{"^IXIC", "Nasdaq", nullptr},
	{"^VIX", "VIX (volatilidade do S&P 500)", nullptr},
	{"USDBRL=X", "Dólar em reais", nullptr},
	{"EURBRL=X", "Euro em reais", nullptr},
	{"DX-Y.NYB", "Índice do dólar", nullptr},
	{"GC=F", "Ouro", nullptr},
	{"SI=F", "Prata", nullptr},
	{"BZ=F", "Petróleo Brent", nullptr},
	{"HG=F", "Cobre", nullptr},
	{"ZS=F", "Soja", nullptr},
	{"KC=F", "Café", nullptr},
	{"^TNX", "Juros dos EUA, 10 anos", nullptr},
	{"TLT", "Títulos longos do Tesouro dos EUA (ETF)", nullptr},
	{"BTC-USD", "Bitcoin", nullptr},
	{"EWZ", "Bolsa brasileira em dólares (ETF)", nullptr},
	{"SH", "ProShares Short S&P500", "o S&P 500, −1× por dia"},
	{"SDS", "ProShares UltraShort S&P500", "o S&P 500, −2× por dia"},
	{"PSQ", "ProShares Short QQQ", "o Nasdaq-100, −1× por dia"},
	{"SQQQ", "ProShares UltraPro Short QQQ", "o Nasdaq-100, −3× por dia"},
	{"DOG", "ProShares Short Dow30", "o Dow Jones, −1× por dia"},
	{"RWM", "ProShares Short Russell2000", "o Russell 2000, −1× por dia"},
	{"TBF", "ProShares Short 20+ Year Treasury", "os títulos longos dos EUA, −1× por dia"},
	{"GLL", "ProShares UltraShort Gold", "o ouro, −2× por dia"},
	{"SCO", "ProShares UltraShort Bloomberg Crude Oil", "o petróleo, −2× por dia"},
	{"EUO", "ProShares UltraShort Euro", "o euro contra o dólar, −2× por dia"},
	{"BITI", "ProShares Short Bitcoin", "o bitcoin, −1× por dia"},
	{"PETR4.SA", "Petrobras", nullptr},
	{"VALE3.SA", "Vale", nullptr},
	{"ITUB4.SA", "Itaú Unibanco", nullptr},
	{"BBDC4.SA", "Bradesco", nullptr},
	{"BBAS3.SA", "Banco do Brasil", nullptr},
	{"ABEV3.SA", "Ambev", nullptr},
	{"WEGE3.SA", "WEG", nullptr},
	{"SUZB3.SA", "Suzano", nullptr},
	{"PRIO3.SA", "PRIO", nullptr},
	{"GGBR4.SA", "Gerdau", nullptr},
	{"B3SA3.SA", "B3", nullptr},
	{"RENT3.SA", "Localiza", nullptr},
	{"MGLU3.SA", "Magazine Luiza", nullptr},
	{"AAPL", "Apple", nullptr},
	{"MSFT", "Microsoft", nullptr},
	{"NVDA", "Nvidia", nullptr},
	{"AMZN", "Amazon", nullptr},
	{"GOOGL", "Alphabet (Google)", nullptr},
	{"META", "Meta", nullptr},
	{"TSLA", "Tesla", nullptr},
	{"JPM", "JPMorgan", nullptr},
	{"XOM", "ExxonMobil", nullptr},
};

class TelaOposto : public Tela {
public:
	TelaOposto() : Tela("OPOSTO", "O ativo oposto", "O que costuma subir quando o seu ativo cai: para proteger uma posição.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativo_ = campo("PETR4", "ativo");
		periodo_ = new QComboBox;
		for (const auto &p : PERIODOS_C)
			periodo_->addItem(QString::fromUtf8(p[0]));
		periodo_->setCurrentIndex(1);
		auto *ir = botao("Procurar", "primario");
		h->addWidget(ativo_);
		h->addWidget(periodo_);
		h->addWidget(ir);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		parte("Os mais opostos", "A proteção é quanto comprar do outro para cada 100 do seu ativo, pela regressão dos retornos.");
		t_ = tabela({"ativo", "", "correlação", "beta", "proteção por 100", "observação"}, 360);
		ajeitar(t_);
		v->addWidget(t_);
		parte("Os que mais andam junto");
		j_ = tabela({"ativo", "", "correlação", "beta", "", ""}, 200);
		ajeitar(j_);
		v->addWidget(j_);
		v->addWidget(rotulo("Correlação é passado, não promessa: em crises, ativos que andavam separados costumam cair juntos. "
		    "Os fundos inversos repõem a aposta todo dia, e por isso se afastam do índice em prazos longos. Na B3 não há "
		    "fundo inverso: o oposto se monta com venda a descoberto (aluguel), opções de venda ou contratos futuros. "
		    "Isto é uma conta, não uma recomendação.", "detalhe"));
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { procurar(); });
		connect(ativo_, &QLineEdit::returnPressed, this, [this] { procurar(); });
		connect(periodo_, &QComboBox::currentIndexChanged, this, [this] { procurar(); });
		for (QTableWidget *t : {t_, j_})
			connect(t, &QTableWidget::cellDoubleClicked, this, [this, t](int l, int) {
				if (comando)
					comando(ativo_->text() + " " + t->item(l, 0)->text() + " CORR");
			});
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativo_->setText(a.first());
		procurar();
	}

private:
	static void ajeitar(QTableWidget *t)
	{
		t->verticalHeader()->setVisible(false);
		t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		t->setSelectionBehavior(QAbstractItemView::SelectRows);
	}

	void procurar()
	{
		const QString alvo = simboloDe(ativo_->text());
		QStringList s{alvo};
		for (const auto &u : UNIVERSO)
			if (alvo != QLatin1String(u.simbolo))
				s << QString::fromLatin1(u.simbolo);
		aviso_->setText("Buscando e comparando com cinquenta ativos…");
		buscarVarios(s, PERIODOS_C[periodo_->currentIndex()][1], "1d", this, [this, alvo](const QMap<QString, Cotacao> &q, const QString &aviso) {
			if (!q.contains(alvo)) {
				aviso_->setText(aviso.isEmpty() ? QStringLiteral("Não achei o ativo “%1”.").arg(curto(alvo)) : aviso);
				return;
			}
			struct Linha {
				QString simbolo, nome, nota;
				contas::Relacao r;	// o alvo explicado pelo outro
			};
			QVector<Linha> ls;
			for (const auto &u : UNIVERSO) {
				const QString x = QString::fromLatin1(u.simbolo);
				if (x == alvo || !q.contains(x))
					continue;
				Linha l{x, QString::fromUtf8(u.nome), u.inverso ? QStringLiteral("fundo inverso: ") + QString::fromUtf8(u.inverso) : QString(),
				    contas::relacionar(q[alvo], q[x])};
				if (std::isfinite(l.r.correlacao))
					ls << l;
			}
			std::sort(ls.begin(), ls.end(), [](const Linha &a, const Linha &b) { return a.r.correlacao < b.r.correlacao; });
			auto encher = [&](QTableWidget *t, int de, int ate, bool protecao) {
				t->setRowCount(0);
				for (int k = de; k != ate; k += de < ate ? 1 : -1) {
					const Linha &l = ls[k];
					const int n = t->rowCount();
					t->insertRow(n);
					t->setItem(n, 0, celula(curto(l.simbolo)));
					auto *nome = celula(l.nome);
					nome->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
					t->setItem(n, 1, nome);
					t->setItem(n, 2, celula(num(l.r.correlacao, 2)));
					t->setItem(n, 3, celula(num(l.r.beta, 2)));
					// Variância mínima: comprar −beta do outro por unidade do alvo.
					t->setItem(n, 4, celula(protecao && l.r.beta < 0 ? num(-l.r.beta * 100, 0) : QString()));
					auto *nota = celula(l.nota);
					nota->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
					t->setItem(n, 5, nota);
				}
			};
			const int n = ls.size();
			encher(t_, 0, std::min(15, n), true);
			encher(j_, n - 1, std::max(-1, n - 9), false);
			if (ls.isEmpty()) {
				aviso_->setText("Não deu para comparar.");
				return;
			}
			const Linha &m = ls.first();
			aviso_->setText(QStringLiteral("O mais oposto a %1 foi %2 (%3), com correlação de %4%5. %6")
			    .arg(curto(alvo), curto(m.simbolo), m.nome, num(m.r.correlacao, 2),
				m.r.correlacao > -0.2 ? QStringLiteral(": fraca, não há um oposto claro nesta lista") : QString(), aviso));
		});
	}

	QLineEdit *ativo_;
	QComboBox *periodo_;
	QLabel *aviso_;
	QTableWidget *t_, *j_;
};

// --- REL: as relações entre as empresas -----------------------------------------

// De quem a empresa compra, para quem vende, com quem concorre e de que
// grupo é. As relações vêm de um arquivo de texto (uma por linha):
//
//   A <tab> fornece|concorre|controla <tab> B <tab> nota
//   = <tab> SÍMBOLO <tab> Nome
//
// O que vem com o sistema são relações públicas e conhecidas, sem valores;
// a pessoa acrescenta as suas em ~/.local/share/lume-mercados/relacoes.tsv.
struct Vinculo {
	QString a, tipo, b, nota;
};

struct Relacoes {
	QVector<Vinculo> v;
	QMap<QString, QString> nomes;

	void ler(const QString &arq)
	{
		QFile f(arq);
		if (!f.open(QIODevice::ReadOnly))
			return;
		for (const QString &l : QString::fromUtf8(f.readAll()).split('\n')) {
			if (l.startsWith('#') || l.trimmed().isEmpty())
				continue;
			const QStringList p = l.split('\t');
			if (p.value(0) == "=" && p.size() >= 3)
				nomes[p[1].trimmed()] = p[2].trimmed();
			else if (p.size() >= 3)
				v.push_back({p[0].trimmed(), p[1].trimmed(), p[2].trimmed(), p.value(3).trimmed()});
		}
	}
	QString nome(const QString &s) const { return nomes.value(s, curto(s)); }
};

static QString
arquivoDoSistema(const QString &nome)
{
	for (const QString &p : {qEnvironmentVariable("LUME_MERCADOS_PASTA"), QStringLiteral("/usr/share/lume/app"),
		 QCoreApplication::applicationDirPath()})
		if (!p.isEmpty() && QFile::exists(p + "/" + nome))
			return p + "/" + nome;
	return QString();
}

static QString
relacoesDaPessoa()
{
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/relacoes.tsv";
}

static Relacoes
carregarRelacoes()
{
	Relacoes r;
	r.ler(arquivoDoSistema("lume-mercados-relacoes.tsv"));
	r.ler(relacoesDaPessoa());
	return r;
}

struct Vizinho {
	QString grupo, simbolo, nota;
};

static const char *const GRUPOS_REL[] = {"Fornecedores", "Clientes", "Concorrentes", "Controlador", "Controladas"};

static QVector<Vizinho>
vizinhos(const Relacoes &r, const QString &s)
{
	QVector<Vizinho> v;
	QSet<QString> visto;
	auto por = [&](const char *g, const QString &outro, const QString &nota) {
		const QString k = QLatin1String(g) + outro;
		if (!visto.contains(k)) {
			visto << k;
			v.push_back({QString::fromUtf8(g), outro, nota});
		}
	};
	for (const Vinculo &x : r.v) {
		if (x.tipo == "fornece" && x.b == s)
			por(GRUPOS_REL[0], x.a, x.nota);
		else if (x.tipo == "fornece" && x.a == s)
			por(GRUPOS_REL[1], x.b, x.nota);
		else if (x.tipo == "concorre" && (x.a == s || x.b == s))
			por(GRUPOS_REL[2], x.a == s ? x.b : x.a, x.nota);
		else if (x.tipo == "controla" && x.b == s)
			por(GRUPOS_REL[3], x.a, x.nota);
		else if (x.tipo == "controla" && x.a == s)
			por(GRUPOS_REL[4], x.b, x.nota);
	}
	std::stable_sort(v.begin(), v.end(), [](const Vizinho &a, const Vizinho &b) {
		auto o = [](const QString &g) {
			for (int k = 0; k < 5; k++)
				if (g == QString::fromUtf8(GRUPOS_REL[k]))
					return k;
			return 9;
		};
		return o(a.grupo) < o(b.grupo);
	});
	return v;
}

// O desenho: fornecedores à esquerda, a empresa no meio, clientes à direita;
// concorrentes embaixo, o grupo em cima.
class Diagrama : public QWidget {
public:
	Diagrama() { setMinimumHeight(380); }
	void definir(const QString &centro, const QVector<Vizinho> &v, const Relacoes &r)
	{
		centro_ = centro;
		v_ = v;
		r_ = r;
		update();
	}
	std::function<void(const QString &)> clicou;

protected:
	void mousePressEvent(QMouseEvent *e) override
	{
		for (const auto &c : caixas_)
			if (c.first.contains(e->position()) && clicou)
				clicou(c.second);
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QColor tinta = palette().color(QPalette::WindowText), papel = palette().color(QPalette::Window);
		const QColor segunda = papel.lightness() < 128 ? QColor(0xa9, 0xa4, 0x99) : QColor(0x5d, 0x59, 0x50);
		QFont mono("IBM Plex Mono");
		mono.setPixelSize(11);
		QFont serifa("IBM Plex Serif");
		serifa.setPixelSize(17);
		caixas_.clear();
		if (centro_.isEmpty())
			return;
		const double w = width(), h = height(), cw = 170, ch = 26;
		const QRectF meio(w / 2 - 110, h / 2 - 26, 220, 52);
		auto lista = [&](const char *g) {
			QStringList s;
			for (const Vizinho &x : v_)
				if (x.grupo == QString::fromUtf8(g))
					s << x.simbolo;
			return s;
		};
		auto caixa = [&](const QRectF &r, const QString &s) {
			p.setPen(QPen(tinta, 1));
			p.setBrush(palette().color(QPalette::Base));
			p.drawRect(r);
			p.setFont(mono);
			p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
			    QFontMetrics(mono).elidedText(curto(s) + "  " + r_.nome(s), Qt::ElideRight, int(r.width()) - 12));
			caixas_ << qMakePair(r, s);
		};
		auto coluna = [&](const QStringList &s, double x, bool esquerda, const char *titulo) {
			const int n = std::min<int>(s.size(), int((h - 30) / (ch + 6)));
			const double y0 = h / 2 - n * (ch + 6) / 2.0;
			p.setPen(segunda);
			p.setFont(mono);
			if (n)
				p.drawText(QRectF(x, y0 - 20, cw, 16), Qt::AlignLeft, QString::fromUtf8(titulo).toUpper());
			for (int k = 0; k < n; k++) {
				const QRectF r(x, y0 + k * (ch + 6), cw, ch);
				p.setPen(QPen(segunda, 1));
				p.drawLine(QPointF(esquerda ? r.right() : r.left(), r.center().y()),
				    QPointF(esquerda ? meio.left() : meio.right(), meio.center().y()));
				caixa(r, s[k]);
			}
		};
		auto fileira = [&](const QStringList &s, double y, bool cima, const char *titulo) {
			const int n = std::min<int>(s.size(), int((w - 2 * cw - 80) / (cw + 8)));
			const double x0 = w / 2 - n * (cw + 8) / 2.0;
			p.setPen(segunda);
			p.setFont(mono);
			if (n)
				p.drawText(QRectF(x0, cima ? y - 18 : y + ch + 4, 300, 16), Qt::AlignLeft, QString::fromUtf8(titulo).toUpper());
			for (int k = 0; k < n; k++) {
				const QRectF r(x0 + k * (cw + 8), y, cw, ch);
				p.setPen(QPen(segunda, 1, Qt::DashLine));
				p.drawLine(QPointF(r.center().x(), cima ? r.bottom() : r.top()),
				    QPointF(meio.center().x(), cima ? meio.top() : meio.bottom()));
				caixa(r, s[k]);
			}
		};
		coluna(lista(GRUPOS_REL[0]), 14, true, "fornecedores");
		coluna(lista(GRUPOS_REL[1]), w - cw - 14, false, "clientes");
		fileira(lista(GRUPOS_REL[3]) + lista(GRUPOS_REL[4]), 24, true, "grupo (controlador e controladas)");
		fileira(lista(GRUPOS_REL[2]), h - ch - 26, false, "concorrentes");
		p.setPen(QPen(tinta, 2));
		p.setBrush(tinta);
		p.drawRect(meio);
		p.setPen(papel);
		p.setFont(serifa);
		p.drawText(meio, Qt::AlignCenter, QFontMetrics(serifa).elidedText(r_.nome(centro_), Qt::ElideRight, 200));
		p.setPen(segunda);
		p.setFont(mono);
		p.drawText(QRectF(meio.left() - 70, meio.center().y() - 22, 64, 14), Qt::AlignRight, "vendem →");
		p.drawText(QRectF(meio.right() + 6, meio.center().y() - 22, 64, 14), Qt::AlignLeft, "vende →");
	}

private:
	QString centro_;
	QVector<Vizinho> v_;
	Relacoes r_;
	QVector<QPair<QRectF, QString>> caixas_;
};

class TelaRelacoes : public Tela {
public:
	TelaRelacoes() : Tela("REL", "Relações", "De quem a empresa compra, para quem vende, com quem concorre, e o que um tombo dela faria com os outros.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativo_ = campo("PETR4", "empresa");
		choque_ = campo("-10", "queda ou alta (%)");
		choque_->setMaximumWidth(90);
		auto *ir = botao("Mostrar", "primario");
		auto *editar = botao("Minhas relações…");
		h->addWidget(ativo_);
		h->addWidget(ir);
		h->addWidget(rotulo("se ela mexer", "texto"));
		h->addWidget(choque_);
		h->addWidget(rotulo("%", "texto"));
		h->addWidget(editar);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		d_ = new Diagrama;
		v->addWidget(d_);
		t_ = tabela({"relação", "ativo", "empresa", "o quê", "correlação", "beta", "impacto estimado"}, 330);
		t_->verticalHeader()->setVisible(false);
		t_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		t_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		v->addWidget(rotulo("As relações são públicas e conhecidas (relatórios das empresas e a imprensa), sem os valores de cada "
		    "contrato, que são dados pagos. O impacto é estatístico: o beta de cada empresa contra esta, em um ano de retornos "
		    "diários, vezes o movimento. Diz como andaram juntas, não por quê. Em “Minhas relações” você acrescenta as suas.",
		    "detalhe"));
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { mostrar(); });
		connect(ativo_, &QLineEdit::returnPressed, this, [this] { mostrar(); });
		connect(choque_, &QLineEdit::textChanged, this, [this] { encher(); });
		d_->clicou = [this](const QString &s) {
			ativo_->setText(curto(s));
			mostrar();
		};
		connect(t_, &QTableWidget::cellDoubleClicked, this, [this](int l, int) {
			ativo_->setText(t_->item(l, 1)->text());
			mostrar();
		});
		connect(editar, &QPushButton::clicked, this, [] {
			const QString arq = relacoesDaPessoa();
			if (!QFile::exists(arq)) {
				QDir().mkpath(QFileInfo(arq).path());
				QFile f(arq);
				if (f.open(QIODevice::WriteOnly))
					f.write("# As suas relações, uma por linha, com TAB entre as partes:\n"
						"#   A<TAB>fornece<TAB>B<TAB>nota      (A vende para B)\n"
						"#   A<TAB>concorre<TAB>B<TAB>nota\n"
						"#   A<TAB>controla<TAB>B<TAB>nota\n"
						"#   =<TAB>SIMBOLO<TAB>Nome da empresa\n"
						"# Os símbolos são os das cotações: PETR4.SA, AAPL, 005930.KS.\n");
			}
			QDesktopServices::openUrl(QUrl::fromLocalFile(arq));
		});
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativo_->setText(a.first());
		mostrar();
	}

private:
	void mostrar()
	{
		r_ = carregarRelacoes();
		alvo_ = simboloDe(ativo_->text());
		viz_ = vizinhos(r_, alvo_);
		q_.clear();
		d_->definir(alvo_, viz_, r_);
		encher();
		if (viz_.isEmpty()) {
			aviso_->setText(QStringLiteral("Ainda não há relações de %1 no arquivo. Há %2 empresas com relações; "
			    "experimente PETR4, VALE3, AAPL, NVDA, EMBJ3, BA ou TSLA, ou acrescente em “Minhas relações”.")
			    .arg(curto(alvo_)).arg(r_.nomes.size()));
			return;
		}
		QStringList s{alvo_};
		for (const Vizinho &x : viz_)
			if (!s.contains(x.simbolo))
				s << x.simbolo;
		aviso_->setText("Buscando as cotações para estimar o impacto…");
		const QString alvo = alvo_;
		buscarVarios(s, "1y", "1d", this, [this, alvo](const QMap<QString, Cotacao> &q, const QString &aviso) {
			if (alvo != alvo_)
				return;
			q_ = q;
			aviso_->setText(QStringLiteral("%1: %2 relações. Clique numa empresa para seguir a cadeia. %3")
			    .arg(r_.nome(alvo_)).arg(viz_.size()).arg(aviso));
			encher();
		});
	}

	void encher()
	{
		bool ok;
		const double choque = lerNumero(choque_->text(), &ok) / 100;
		t_->setRowCount(0);
		for (const Vizinho &x : viz_) {
			const int l = t_->rowCount();
			t_->insertRow(l);
			auto esq = [](const QString &s) {
				auto *i = celula(s);
				i->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				return i;
			};
			t_->setItem(l, 0, esq(x.grupo));
			t_->setItem(l, 1, esq(curto(x.simbolo)));
			t_->setItem(l, 2, esq(r_.nome(x.simbolo)));
			t_->setItem(l, 3, esq(x.nota));
			if (q_.contains(x.simbolo) && q_.contains(alvo_)) {
				const contas::Relacao r = contas::relacionar(q_[x.simbolo], q_[alvo_]);
				t_->setItem(l, 4, celula(num(r.correlacao, 2)));
				t_->setItem(l, 5, celula(num(r.beta, 2)));
				// Com correlação fraca o beta é ruído: melhor não estimar.
				t_->setItem(l, 6, celula(!ok || !std::isfinite(r.beta) ? QStringLiteral("—")
				    : std::fabs(r.correlacao) < 0.2 ? QStringLiteral("sem relação clara") : variacao(r.beta * choque, 1)));
			} else
				for (int c = 4; c < 7; c++)
					t_->setItem(l, c, celula(q_.isEmpty() ? QString() : QStringLiteral("—")));
		}
	}

	QLineEdit *ativo_, *choque_;
	QLabel *aviso_;
	Diagrama *d_;
	QTableWidget *t_;
	Relacoes r_;
	QString alvo_;
	QVector<Vizinho> viz_;
	QMap<QString, Cotacao> q_;
};

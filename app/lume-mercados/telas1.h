// SPDX-License-Identifier: BSD-2-Clause
//
// telas1.h - o monitor do mundo, o gráfico, a ficha do ativo e os balanços
// (os dados abertos da CVM).

#pragma once

#include "dados.h"

// --- MUNDO: o monitor dos mercados -----------------------------------------------

static const struct {
	const char *grupo, *simbolo, *nome;
} MUNDO[] = {
	{"Bolsas", "^BVSP", "Ibovespa (Brasil)"},
	{"Bolsas", "^GSPC", "S&P 500 (EUA)"},
	{"Bolsas", "^IXIC", "Nasdaq (EUA)"},
	{"Bolsas", "^DJI", "Dow Jones (EUA)"},
	{"Bolsas", "^FTSE", "FTSE 100 (Reino Unido)"},
	{"Bolsas", "^GDAXI", "DAX (Alemanha)"},
	{"Bolsas", "^FCHI", "CAC 40 (França)"},
	{"Bolsas", "^STOXX50E", "Euro Stoxx 50"},
	{"Bolsas", "^N225", "Nikkei 225 (Japão)"},
	{"Bolsas", "^HSI", "Hang Seng (Hong Kong)"},
	{"Bolsas", "000001.SS", "Xangai (China)"},
	{"Bolsas", "^MXX", "IPC (México)"},
	{"Moedas", "USDBRL=X", "Dólar em reais"},
	{"Moedas", "EURBRL=X", "Euro em reais"},
	{"Moedas", "GBPBRL=X", "Libra em reais"},
	{"Moedas", "EURUSD=X", "Euro em dólares"},
	{"Moedas", "JPY=X", "Dólar em ienes"},
	{"Moedas", "CNY=X", "Dólar em yuans"},
	{"Moedas", "ARS=X", "Dólar em pesos argentinos"},
	{"Moedas", "DX-Y.NYB", "Índice do dólar (DXY)"},
	{"Matérias-primas", "BZ=F", "Petróleo Brent"},
	{"Matérias-primas", "CL=F", "Petróleo WTI"},
	{"Matérias-primas", "NG=F", "Gás natural"},
	{"Matérias-primas", "GC=F", "Ouro"},
	{"Matérias-primas", "SI=F", "Prata"},
	{"Matérias-primas", "HG=F", "Cobre"},
	{"Matérias-primas", "ZS=F", "Soja"},
	{"Matérias-primas", "ZC=F", "Milho"},
	{"Matérias-primas", "KC=F", "Café"},
	{"Matérias-primas", "SB=F", "Açúcar"},
	{"Matérias-primas", "LE=F", "Boi gordo"},
	{"Juros e risco", "^TNX", "EUA, 10 anos (% ao ano)"},
	{"Juros e risco", "^TYX", "EUA, 30 anos (% ao ano)"},
	{"Juros e risco", "^IRX", "EUA, 3 meses (% ao ano)"},
	{"Juros e risco", "^VIX", "VIX (medo no S&P 500)"},
	{"Cripto", "BTC-USD", "Bitcoin (US$)"},
	{"Cripto", "ETH-USD", "Ethereum (US$)"},
};

// A tendência em oito degraus, como um gráfico de uma linha só.
static QString
tendencia(const QVector<double> &c, int pontos = 24)
{
	QVector<double> v;
	for (double x : c)
		if (std::isfinite(x))
			v << x;
	if (v.size() < 2)
		return QString();
	if (v.size() > pontos)
		v = v.mid(v.size() - pontos);
	const double lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
	QString s;
	for (double x : v)
		s += QChar(0x2581 + (hi > lo ? int(std::lround((x - lo) / (hi - lo) * 7)) : 3));
	return s;
}

static int
casasDe(double preco)
{
	return std::fabs(preco) >= 1000 ? 0 : std::fabs(preco) >= 10 ? 2 : 4;
}

class TelaMundo : public Tela {
public:
	TelaMundo() : Tela("MUNDO", "Os mercados do mundo", "Bolsas, moedas, matérias-primas, juros e cripto. Dois cliques abrem o gráfico.")
	{
		auto *h = new QHBoxLayout;
		auto *b = botao("Atualizar");
		aviso_ = rotulo(QString(), "detalhe");
		h->addWidget(b);
		h->addWidget(aviso_, 1);
		v->addLayout(h);
		t_ = tabela({"", "último", "no dia", "no mês", "em 3 meses", "tendência"}, 620);
		t_->setMaximumHeight(100000);
		t_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		for (int c = 1; c < 6; c++)
			t_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_, 1);
		connect(b, &QPushButton::clicked, this, [this] { atualizar(); });
		connect(t_, &QTableWidget::cellDoubleClicked, this, [this](int l, int) {
			const QString s = t_->item(l, 0)->data(Qt::UserRole).toString();
			if (!s.isEmpty() && comando)
				comando(s + " G");
		});
		auto *relogio = new QTimer(this);
		connect(relogio, &QTimer::timeout, this, [this] {
			if (isVisible())
				atualizar();
		});
		relogio->start(90000);
	}
	void abrir(const QStringList &) override { atualizar(); }

private:
	void atualizar()
	{
		aviso_->setText("Buscando…");
		QStringList s;
		for (const auto &m : MUNDO)
			s << QString::fromLatin1(m.simbolo);
		buscarVarios(s, "3mo", "1d", this, [this](const QMap<QString, Cotacao> &q, const QString &aviso) {
			t_->setRowCount(0);
			QString grupo;
			for (const auto &m : MUNDO) {
				if (grupo != QString::fromUtf8(m.grupo)) {
					grupo = QString::fromUtf8(m.grupo);
					const int l = t_->rowCount();
					t_->insertRow(l);
					auto *i = new QTableWidgetItem(grupo.toUpper());
					i->setFlags(Qt::ItemIsEnabled);
					QFont f = i->font();
					f.setBold(true);
					i->setFont(f);
					t_->setItem(l, 0, i);
					t_->setSpan(l, 0, 1, 6);
				}
				const int l = t_->rowCount();
				t_->insertRow(l);
				auto *n = celula("  " + QString::fromUtf8(m.nome));
				n->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				n->setData(Qt::UserRole, QString::fromLatin1(m.simbolo));
				t_->setItem(l, 0, n);
				const Cotacao c = q.value(QString::fromLatin1(m.simbolo));
				if (!c.ok) {
					t_->setItem(l, 1, celula("—"));
					continue;
				}
				t_->setItem(l, 1, celula(num(c.preco, casasDe(c.preco))));
				t_->setItem(l, 2, celula(variacao(c.dia())));
				t_->setItem(l, 3, celula(variacao(c.desde(21))));
				t_->setItem(l, 4, celula(variacao(c.preco / primeiro(c) - 1)));
				t_->setItem(l, 5, celula(tendencia(c.c)));
			}
			aviso_->setText((aviso.isEmpty() ? QString() : aviso + " ") + QString::fromUtf8(FONTE_COTACOES) +
			    QDateTime::currentDateTime().toString(" 'Atualizado às' HH:mm."));
		});
	}
	static double primeiro(const Cotacao &c)
	{
		for (double x : c.c)
			if (std::isfinite(x))
				return x;
		return NAO;
	}

	QTableWidget *t_;
	QLabel *aviso_;
};

// --- G: o gráfico -----------------------------------------------------------------

static const struct {
	const char *nome, *periodo, *intervalo;
	double porAno;
} PERIODOS_G[] = {
	{"1 dia", "1d", "5m", 252 * 84}, {"5 dias", "5d", "30m", 252 * 14}, {"1 mês", "1mo", "1d", 252},
	{"6 meses", "6mo", "1d", 252},  {"1 ano", "1y", "1d", 252},	  {"5 anos", "5y", "1wk", 52},
	{"Tudo", "max", "1mo", 12},
};

class TelaGrafico : public Tela {
public:
	TelaGrafico() : Tela("G", "Gráfico", "O preço no tempo. Com dois ou mais ativos, todos partem de 100 para comparar.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativos_ = campo("PETR4", "ativos: PETR4 VALE3 IBOV");
		ativos_->setMinimumWidth(260);
		periodo_ = new QComboBox;
		for (const auto &p : PERIODOS_G)
			periodo_->addItem(QString::fromUtf8(p.nome));
		periodo_->setCurrentIndex(4);
		mm20_ = new QCheckBox("média 20");
		mm50_ = new QCheckBox("média 50");
		mm200_ = new QCheckBox("média 200");
		auto *ir = botao("Mostrar", "primario");
		h->addWidget(ativos_);
		h->addWidget(periodo_);
		h->addWidget(mm20_);
		h->addWidget(mm50_);
		h->addWidget(mm200_);
		h->addWidget(ir);
		h->addStretch(1);
		v->addLayout(h);
		titulo_ = rotulo(QString(), "parte");
		v->addWidget(titulo_);
		quadro_ = new Quadro(4);
		quadro_->item("ultimo", "Último");
		quadro_->item("periodo", "No período");
		quadro_->item("faixa", "Máxima e mínima do período");
		quadro_->item("52", "Faixa de 52 semanas");
		quadro_->item("vol", "Volatilidade (ao ano)");
		quadro_->item("queda", "Maior queda do pico");
		quadro_->item("volume", "Volume do dia");
		quadro_->item("hora", "Última negociação");
		v->addWidget(quadro_);
		g_ = new Grafico("Escolha um ativo e clique em “Mostrar”.");
		g_->setMinimumHeight(360);
		v->addWidget(g_);
		gv_ = new Grafico(" ");
		gv_->setMinimumHeight(190);
		v->addWidget(gv_);
		aviso_ = rotulo(QString(), "detalhe");
		v->addWidget(aviso_);
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { mostrar(); });
		connect(ativos_, &QLineEdit::returnPressed, this, [this] { mostrar(); });
		connect(periodo_, &QComboBox::currentIndexChanged, this, [this] { mostrar(); });
		for (QCheckBox *c : {mm20_, mm50_, mm200_})
			connect(c, &QCheckBox::toggled, this, [this] { desenhar(); });
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativos_->setText(a.join(" "));
		mostrar();
	}

private:
	void mostrar()
	{
		const QStringList nomes = ativos_->text().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
		if (nomes.isEmpty())
			return;
		const auto &p = PERIODOS_G[periodo_->currentIndex()];
		series_.clear();
		pedidos_ = nomes.size();
		const int vez = ++vez_;
		aviso_->setText("Buscando…");
		for (int k = 0; k < nomes.size(); k++)
			buscarHistorico(simboloDe(nomes[k]), p.periodo, p.intervalo, this, [this, k, vez](const Cotacao &q, const QString &aviso) {
				if (vez != vez_)
					return;
				if (q.ok)
					series_[k] = q;
				if (!aviso.isEmpty())
					aviso_->setText(aviso);
				else if (--pedidos_ == 0 || series_.size() == 1)
					aviso_->setText(QString::fromUtf8(FONTE_COTACOES));
				desenhar();
			});
	}

	QString data(qint64 t) const
	{
		const int p = periodo_->currentIndex();
		return QDateTime::fromSecsSinceEpoch(t).toString(p == 0 ? "HH:mm" : p == 1 ? "dd/MM HH:mm" : "dd/MM/yy");
	}

	void desenhar()
	{
		if (series_.isEmpty()) {
			g_->limpar();
			gv_->limpar();
			return;
		}
		const Cotacao &q = series_.first();
		const auto &p = PERIODOS_G[periodo_->currentIndex()];
		QStringList nomes;
		for (const Cotacao &s : series_)
			nomes << QStringLiteral("%1 — %2").arg(curto(s.simbolo), s.titulo());
		titulo_->setText(nomes.join("   ·   "));
		QVector<double> validos;
		for (double c : q.c)
			if (std::isfinite(c))
				validos << c;
		if (validos.isEmpty())
			return;
		const int casas = casasDe(q.preco);
		quadro_->valor("ultimo", num(q.preco, casas) + " " + q.moeda, "no dia: " + variacao(q.dia()));
		quadro_->valor("periodo", variacao(q.preco / validos.first() - 1), QString::fromUtf8(p.nome));
		quadro_->valor("faixa", num(*std::max_element(validos.begin(), validos.end()), casas) + " · " +
		    num(*std::min_element(validos.begin(), validos.end()), casas));
		quadro_->valor("52", num(q.max52, casas) + " · " + num(q.min52, casas));
		quadro_->valor("vol", pct(contas::volatilidade(q, p.porAno), 1), "o quanto o preço balança");
		quadro_->valor("queda", pct(contas::maiorQueda(q), 1), "no período");
		quadro_->valor("volume", std::isfinite(q.volume) && q.volume > 0 ? rotuloEixo(q.volume, 1) : QStringLiteral("—"));
		quadro_->valor("hora", QDateTime::fromSecsSinceEpoch(q.hora).toString("dd/MM HH:mm"), q.bolsa);

		const QVector<qint64> tempos = q.t;
		g_->formatoX = gv_->formatoX = [this, tempos](double x) {
			const int k = int(std::lround(x));
			return k >= 0 && k < tempos.size() ? data(tempos[k]) : QString();
		};
		QVector<Serie> s;
		const bool comparar = series_.size() > 1;
		int ordem = 0;
		for (const Cotacao &c : series_) {
			Serie l{curto(c.simbolo), {}, Serie::Linha, ordem == 1, ordem >= 2};
			// Os outros ativos entram nos dias do primeiro.
			QMap<qint64, double> porDia;
			for (int k = 0; k < c.t.size(); k++)
				porDia[c.t[k] / 86400] = c.c[k];
			double base = NAO;
			for (int k = 0; k < q.t.size(); k++) {
				double y = ordem == 0 ? q.c[k] : porDia.value(q.t[k] / 86400, NAO);
				if (comparar && std::isfinite(y)) {
					if (!std::isfinite(base))
						base = y;
					y = y / base * 100;
				}
				l.pontos << QPointF(k, y);
			}
			// Sem buracos: o gráfico liga o último preço ao seguinte.
			QVector<QPointF> cheios;
			for (const QPointF &pt : l.pontos)
				if (std::isfinite(pt.y()))
					cheios << pt;
			l.pontos = ordem == 0 && !comparar ? l.pontos : cheios;
			if (ordem == 0 && !comparar) {
				double u = NAO;
				for (QPointF &pt : l.pontos) {
					if (std::isfinite(pt.y()))
						u = pt.y();
					else
						pt.setY(u);
				}
			}
			s << l;
			ordem++;
		}
		if (!comparar) {
			const struct {
				QCheckBox *c;
				int n;
			} mm[] = {{mm20_, 20}, {mm50_, 50}, {mm200_, 200}};
			for (const auto &m : mm)
				if (m.c->isChecked()) {
					Serie l{QStringLiteral("média %1").arg(m.n), {}, Serie::Linha, true, m.n != 20};
					const QVector<double> r = contas::mediaMovel(q.c, m.n);
					for (int k = 0; k < r.size(); k++)
						if (std::isfinite(r[k]))
							l.pontos << QPointF(k, r[k]);
					if (!l.pontos.isEmpty())
						s << l;
				}
		}
		g_->formatoY = [casas, comparar](double y) { return num(y, comparar ? 0 : casas); };
		g_->definir("", comparar ? "base 100 no começo" : q.moeda, s, {}, false, true);
		Serie vol{"volume", {}, Serie::Barras, false, true};
		bool tem = false;
		for (int k = 0; k < q.v.size(); k++) {
			vol.pontos << QPointF(k, std::isfinite(q.v[k]) ? q.v[k] : 0);
			tem = tem || (std::isfinite(q.v[k]) && q.v[k] > 0);
		}
		if (tem && !comparar)
			gv_->definir("", "volume", {vol}, {}, true, true);
		else
			gv_->limpar(" ");
	}

	QLineEdit *ativos_;
	QComboBox *periodo_;
	QCheckBox *mm20_, *mm50_, *mm200_;
	QLabel *titulo_, *aviso_;
	Quadro *quadro_;
	Grafico *g_, *gv_;
	QMap<int, Cotacao> series_;
	int pedidos_ = 0, vez_ = 0;
};

// --- Os dados abertos da CVM ---------------------------------------------------

struct EmpresaCvm {
	QString cnpj, nome, setor, atividade, controle, site, situacao, registro;
};

static QString
urlFca(int ano)
{
	return QStringLiteral("https://dados.cvm.gov.br/dados/CIA_ABERTA/DOC/FCA/DADOS/fca_cia_aberta_%1.zip").arg(ano);
}

static QString
urlDfp(int ano)
{
	return QStringLiteral("https://dados.cvm.gov.br/dados/CIA_ABERTA/DOC/DFP/DADOS/dfp_cia_aberta_%1.zip").arg(ano);
}

// A empresa dona de um código da B3 (PETR4), pelo Formulário Cadastral.
static void
buscarEmpresaCvm(const QString &codigo, QObject *dono, const std::function<void(const EmpresaCvm &, const QString &)> &f,
    int ano = QDate::currentDate().year(), bool segunda = false)
{
	buscar(urlFca(ano), 7 * 86400, dono, [=](const QByteArray &d, const QString &aviso) {
		if (d.isEmpty()) {
			if (!segunda)
				buscarEmpresaCvm(codigo, dono, f, ano - 1, true);
			else
				f(EmpresaCvm(), aviso);
			return;
		}
		extrairZip(arquivoDe(urlFca(ano)), {QStringLiteral("fca_cia_aberta_valor_mobiliario_%1.csv").arg(ano),
		    QStringLiteral("fca_cia_aberta_geral_%1.csv").arg(ano)}, dono, [=](const QByteArray &csv) {
			// Os arquivos saem na ordem do zip: primeiro acha de quem é o código,
			// depois lê o cadastro dessa empresa.
			EmpresaCvm e;
			QStringList cab, cabGeral;
			QVector<QStringList> geral;
			for (const QString &l : QString::fromLatin1(csv).split('\n')) {
				const QStringList p = partesCsv(l.trimmed());
				if (l.startsWith("CNPJ_Companhia")) {
					cab = p;
					if (!cab.contains("Codigo_Negociacao"))
						cabGeral = p;
				} else if (!cab.contains("Codigo_Negociacao"))
					geral << p;
				else if (p.value(cab.indexOf("Codigo_Negociacao")).toUpper() == codigo &&
				    p.value(cab.indexOf("Data_Fim_Negociacao")).isEmpty())
					e.cnpj = p.value(0);
			}
			int versao = -1;
			for (const QStringList &p : geral) {
				auto col = [&](const char *n) { return p.value(cabGeral.indexOf(n)); };
				if (e.cnpj.isEmpty() || p.value(0) != e.cnpj || col("Versao").toInt() < versao)
					continue;
				versao = col("Versao").toInt();
				e.nome = col("Nome_Empresarial");
				e.setor = col("Setor_Atividade");
				e.atividade = col("Descricao_Atividade");
				e.controle = col("Especie_Controle_Acionario");
				e.site = col("Pagina_Web");
				e.situacao = col("Situacao_Emissor");
				e.registro = col("Data_Registro_CVM");
			}
			if (e.cnpj.isEmpty() && !segunda) {
				buscarEmpresaCvm(codigo, dono, f, ano - 1, true);
				return;
			}
			f(e, e.cnpj.isEmpty() ? QStringLiteral("A CVM não tem o código %1 no cadastro das companhias abertas.").arg(codigo) : aviso);
		});
	});
}

// --- DES: a ficha do ativo ---------------------------------------------------------

class TelaDescricao : public Tela {
public:
	TelaDescricao() : Tela("DES", "Ficha do ativo", "O que é, onde negocia, quanto vale e o que pagou de dividendos.")
	{
		auto *h = new QHBoxLayout;
		ativo_ = campo("PETR4", "ativo");
		auto *ir = botao("Mostrar", "primario");
		h->addWidget(ativo_);
		h->addWidget(ir);
		for (const char *f : {"G", "BAL", "REL", "OPOSTO", "NOT"}) {
			auto *b = botao(QString::fromLatin1(f), "link");
			h->addWidget(b);
			connect(b, &QPushButton::clicked, this, [this, f] {
				if (comando)
					comando(ativo_->text() + " " + QLatin1String(f));
			});
		}
		h->addStretch(1);
		v->addLayout(h);
		titulo_ = rotulo(QString(), "parte");
		v->addWidget(titulo_);
		quadro_ = new Quadro(4);
		quadro_->item("ultimo", "Último");
		quadro_->item("52", "Faixa de 52 semanas");
		quadro_->item("dy", "Dividendos em 12 meses");
		quadro_->item("bolsa", "Onde negocia");
		quadro_->item("ano", "Em 1 ano");
		quadro_->item("5anos", "Em 5 anos");
		quadro_->item("vol", "Volatilidade (ao ano)");
		quadro_->item("tipo", "Tipo");
		v->addWidget(quadro_);
		parte("A empresa");
		empresa_ = rotulo(QString(), "texto");
		empresa_->setTextInteractionFlags(Qt::TextSelectableByMouse);
		v->addWidget(empresa_);
		parte("Dividendos e juros sobre capital");
		div_ = tabela({"data", "por ação"}, 220);
		div_->setMaximumWidth(420);
		div_->verticalHeader()->setVisible(false);
		v->addWidget(div_);
		aviso_ = rotulo(QString(), "detalhe");
		v->addWidget(aviso_);
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { mostrar(); });
		connect(ativo_, &QLineEdit::returnPressed, this, [this] { mostrar(); });
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
		const QString s = simboloDe(ativo_->text());
		const int vez = ++vez_;
		aviso_->setText("Buscando…");
		empresa_->setText("—");
		buscarHistorico(s, "5y", "1wk", this, [this, vez, s](const Cotacao &q, const QString &aviso) {
			if (vez != vez_)
				return;
			if (!q.ok) {
				aviso_->setText(aviso);
				return;
			}
			const int casas = casasDe(q.preco);
			titulo_->setText(QStringLiteral("%1 — %2").arg(curto(q.simbolo), q.titulo()));
			quadro_->valor("ultimo", num(q.preco, casas) + " " + q.moeda, "no dia: " + variacao(q.dia()));
			quadro_->valor("52", num(q.max52, casas) + " · " + num(q.min52, casas), "máxima e mínima");
			quadro_->valor("bolsa", q.bolsa, q.moeda);
			quadro_->valor("ano", variacao(q.desde(52)));
			double primeiro = NAO;
			for (double c : q.c)
				if (std::isfinite(c)) {
					primeiro = c;
					break;
				}
			quadro_->valor("5anos", variacao(q.preco / primeiro - 1), "sem contar os dividendos");
			quadro_->valor("vol", pct(contas::volatilidade(q, 52), 1));
			const QMap<QString, QString> tipos{{"EQUITY", "Ação"}, {"ETF", "Fundo de índice (ETF)"}, {"INDEX", "Índice"},
				{"CURRENCY", "Moeda"}, {"FUTURE", "Contrato futuro"}, {"CRYPTOCURRENCY", "Criptomoeda"},
				{"MUTUALFUND", "Fundo"}};
			quadro_->valor("tipo", tipos.value(q.tipo, q.tipo));
			const qint64 umAno = QDateTime::currentSecsSinceEpoch() - 365 * 86400;
			double soma = 0;
			div_->setRowCount(0);
			for (int k = q.dividendos.size() - 1; k >= 0; k--) {
				const auto &d = q.dividendos[k];
				if (d.first >= umAno)
					soma += d.second;
				const int l = div_->rowCount();
				div_->insertRow(l);
				div_->setItem(l, 0, celula(QDateTime::fromSecsSinceEpoch(d.first).toString("dd/MM/yyyy")));
				div_->setItem(l, 1, celula(num(d.second, 4)));
			}
			quadro_->valor("dy", soma > 0 ? pct(soma / q.preco) : QStringLiteral("—"),
			    soma > 0 ? QStringLiteral("%1 %2 por ação").arg(num(soma, 2), q.moeda) : QStringLiteral("nada pago"));
			aviso_->setText((aviso.isEmpty() ? QString() : aviso + " ") + QString::fromUtf8(FONTE_COTACOES));
			if (!s.endsWith(".SA") || q.tipo != "EQUITY") {
				empresa_->setText(q.tipo == "EQUITY"
				    ? "O cadastro das empresas aparece para as companhias da B3 (dados abertos da CVM)."
				    : "—");
				return;
			}
			empresa_->setText("Buscando o cadastro na CVM…");
			buscarEmpresaCvm(curto(s), this, [this, vez](const EmpresaCvm &e, const QString &av) {
				if (vez != vez_)
					return;
				if (e.cnpj.isEmpty()) {
					empresa_->setText(av);
					return;
				}
				empresa_->setText(QStringLiteral("%1\nCNPJ %2 · %3 · controle %4 · na CVM desde %5\nSetor: %6\n%7\n%8")
				    .arg(e.nome, e.cnpj, e.situacao, e.controle.toLower(), e.registro, e.setor, e.atividade, e.site));
			});
		});
	}

	QLineEdit *ativo_;
	QLabel *titulo_, *empresa_, *aviso_;
	Quadro *quadro_;
	QTableWidget *div_;
	int vez_ = 0;
};

// --- BAL: os balanços (CVM) ---------------------------------------------------------

struct Balanco {
	double receita = NAO, bruto = NAO, ebit = NAO, lucro = NAO, ativo = NAO, pl = NAO, divida = NAO,
	       caixa = NAO, operacional = NAO;
	int versao = -1;
};

// Lê as contas de uma empresa nas demonstrações (DRE, balanço e fluxo de
// caixa, consolidados ou, na falta, individuais) de um ano da CVM.
// Com o CNPJ vazio, lê todas as companhias (a triagem).
static void lerDfpGrupo(const QByteArray &csv, const QString &cnpj, QMap<QString, QMap<int, Balanco>> *todos, bool consolidado);

// As demonstrações consolidadas; de quem só entrega as individuais (uma
// companhia sem controladas), as individuais.
static void
lerDfp(const QByteArray &csv, const QString &cnpj, QMap<QString, QMap<int, Balanco>> *todos)
{
	QMap<QString, QMap<int, Balanco>> ind;
	lerDfpGrupo(csv, cnpj, todos, true);
	lerDfpGrupo(csv, cnpj, &ind, false);
	// Há quem entregue o consolidado todo em zeros: conta como sem consolidado.
	auto vazio = [](const QMap<int, Balanco> &m) {
		for (const Balanco &b : m)
			if ((std::isfinite(b.receita) && b.receita != 0) || (std::isfinite(b.ativo) && b.ativo != 0))
				return false;
		return true;
	};
	for (auto it = ind.begin(); it != ind.end(); ++it)
		if (!todos->contains(it.key()) || vazio((*todos)[it.key()]))
			(*todos)[it.key()] = it.value();
}

static void
lerDfpGrupo(const QByteArray &csv, const QString &cnpj, QMap<QString, QMap<int, Balanco>> *todos, bool consolidado)
{
	const QByteArray chave = cnpj.toLatin1() + ";";
	QStringList cab;
	int inicio = 0;
	while (inicio < csv.size()) {
		int fim = csv.indexOf('\n', inicio);
		if (fim < 0)
			fim = csv.size();
		const QByteArray bruta = csv.mid(inicio, fim - inicio);
		inicio = fim + 1;
		if (bruta.startsWith("CNPJ_CIA")) {
			cab = partesCsv(QString::fromLatin1(bruta).trimmed());
			continue;
		}
		if ((!cnpj.isEmpty() && !bruta.startsWith(chave)) || !cab.contains("CD_CONTA"))
			continue;
		// Só as contas que interessam (o arquivo tem centenas por companhia).
		const QStringList p = partesCsv(QString::fromLatin1(bruta).trimmed());
		auto col = [&](const char *n) { return p.value(cab.indexOf(n)); };
		if (col("GRUPO_DFP").contains("Consolidado") != consolidado)
			continue;
		const int ano = col("DT_FIM_EXERC").left(4).toInt();
		const QString conta = col("CD_CONTA"), nome = col("DS_CONTA");
		double valor = col("VL_CONTA").toDouble();
		if (col("ESCALA_MOEDA").startsWith("MIL"))
			valor *= 1000;
		// Os fluxos (DRE, caixa) são do ano inteiro.
		if (cab.contains("DT_INI_EXERC") && !col("DT_INI_EXERC").endsWith("-01-01"))
			continue;
		if (conta.size() > 7)
			continue;
		Balanco &b = (*todos)[p.value(0)][ano];
		const int versao = col("VERSAO").toInt();
		if (versao < b.versao)
			continue;
		b.versao = versao;
		if (conta == "3.01")
			b.receita = valor;
		else if (conta == "3.03")
			b.bruto = valor;
		else if (conta == "3.05")
			b.ebit = valor;
		else if (conta.size() == 4 && conta.startsWith("3.") && nome.contains("Lucro") && nome.contains("Per"))
			b.lucro = valor;
		else if (conta == "1")
			b.ativo = valor;
		else if (conta == "1.01.01")
			b.caixa = valor;
		else if (conta.size() == 4 && conta.startsWith("2.") && nome.startsWith("Patrim"))
			b.pl = valor;
		else if (conta.size() == 7 && conta.startsWith("2.") && nome.startsWith("Empr") && nome.contains("Financiamentos"))
			b.divida = (std::isfinite(b.divida) && (conta == "2.02.01") ? b.divida : 0) + valor;
		else if (conta == "6.01")
			b.operacional = valor;
	}
}

#include "sec.h"

class TelaBalancos : public Tela {
public:
	TelaBalancos() : Tela("BAL", "Balanços", "Receita, lucro, dívida e margens, ano a ano: as companhias da B3 pela CVM, as dos EUA pela SEC.")
	{
		auto *h = new QHBoxLayout;
		ativo_ = campo("PETR4", "ação da B3 ou dos EUA");
		auto *ir = botao("Mostrar", "primario");
		h->addWidget(ativo_);
		h->addWidget(ir);
		h->addStretch(1);
		v->addLayout(h);
		titulo_ = rotulo(QString(), "parte");
		v->addWidget(titulo_);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		t_ = tabela({""}, 420);
		v->addWidget(t_);
		g_ = new Grafico("O gráfico da receita e do lucro aparece aqui.");
		v->addWidget(g_);
		fonte_ = rotulo(fonteCvm_, "detalhe");
		v->addWidget(fonte_);
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { mostrar(); });
		connect(ativo_, &QLineEdit::returnPressed, this, [this] { mostrar(); });
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
		const QString simbolo = simboloDe(ativo_->text());
		const QString codigo = curto(simbolo);
		const int vez = ++vez_;
		anos_.clear();
		moeda_ = "R$";
		if (!simbolo.endsWith(".SA")) {
			aviso_->setText("Buscando a companhia na SEC…");
			buscarCik(codigo, this, [this, vez, codigo](qint64 cik, const QString &nome, const QString &av) {
				if (vez != vez_)
					return;
				if (!cik) {
					aviso_->setText(av);
					t_->setRowCount(0);
					g_->limpar();
					return;
				}
				titulo_->setText(QStringLiteral("%1 — %2").arg(codigo, nome));
				aviso_->setText("Buscando os balanços na SEC…");
				buscarBalancoSec(cik, this, [this, vez](const QMap<int, Balanco> &a, const QString &moeda, const QString &av) {
					if (vez != vez_)
						return;
					anos_ = a;
					moeda_ = moeda == "USD" ? QStringLiteral("US$") : moeda;
					fonte_->setText("Fonte: SEC (EDGAR), dados XBRL dos relatórios anuais (10-K, 20-F). Na moeda da companhia.");
					faltam_ = 1;
					pronto();
					if (anos_.isEmpty() && !av.isEmpty())
						aviso_->setText(av);
				});
			});
			return;
		}
		fonte_->setText(fonteCvm_);
		aviso_->setText("Buscando a empresa na CVM…");
		buscarEmpresaCvm(codigo, this, [this, vez, codigo](const EmpresaCvm &e, const QString &av) {
			if (vez != vez_)
				return;
			if (e.cnpj.isEmpty()) {
				aviso_->setText(av.isEmpty() ? QStringLiteral("Os balanços são das companhias da B3.") : av);
				return;
			}
			titulo_->setText(QStringLiteral("%1 — %2").arg(codigo, e.nome));
			aviso_->setText("Buscando as demonstrações (na primeira vez, uns 25 MB)…");
			// Cada arquivo traz o ano e o anterior: dois arquivos, quatro anos.
			const int ano = QDate::currentDate().year() - 1;
			faltam_ = 2;
			for (int a : {ano, ano - 2})
				buscar(urlDfp(a), 7 * 86400, this, [this, vez, a, e](const QByteArray &d, const QString &) {
					if (vez != vez_)
						return;
					if (d.isEmpty()) {
						pronto();
						return;
					}
					QStringList m;
					for (const char *x : {"BPA", "BPP", "DRE", "DFC_MI", "DFC_MD"})
						for (const char *g : {"con", "ind"})
							m << QStringLiteral("dfp_cia_aberta_%1_%2_%3.csv").arg(QLatin1String(x), QLatin1String(g)).arg(a);
					extrairZip(arquivoDe(urlDfp(a)), m, this, [this, vez, e](const QByteArray &csv) {
						if (vez != vez_)
							return;
						QMap<QString, QMap<int, Balanco>> t;
						lerDfp(csv, e.cnpj, &t);
						for (auto it = t[e.cnpj].begin(); it != t[e.cnpj].end(); ++it)
							anos_[it.key()] = it.value();
						pronto();
					});
				});
		});
	}

	void pronto()
	{
		if (--faltam_ > 0)
			return;
		QList<int> anos = anos_.keys();
		for (int a : QList<int>(anos))
			if (!std::isfinite(anos_[a].receita) && !std::isfinite(anos_[a].ativo))
				anos.removeAll(a);
		if (anos.isEmpty()) {
			aviso_->setText("Não achei demonstrações consolidadas dessa empresa na CVM.");
			t_->setRowCount(0);
			g_->limpar();
			return;
		}
		QStringList cab{""};
		for (int a : anos)
			cab << QString::number(a);
		t_->setColumnCount(cab.size());
		t_->setHorizontalHeaderLabels(cab);
		t_->verticalHeader()->setVisible(false);
		auto mi = [](double x) { return std::isfinite(x) ? num(x / 1e6, 0) : QStringLiteral("—"); };
		auto grande = [this](double x) {
			return !std::isfinite(x) ? QStringLiteral("—")
			    : std::fabs(x) >= 1e9 ? QStringLiteral("%1 %2 bilhões").arg(moeda_, num(x / 1e9, 1))
						  : QStringLiteral("%1 %2 milhões").arg(moeda_, num(x / 1e6, 0));
		};
		auto pc = [](double a, double b) { return std::isfinite(a) && std::isfinite(b) && b != 0 ? pct(a / b, 1) : QStringLiteral("—"); };
		const struct {
			const char *nome;
			std::function<QString(const Balanco &)> f;
		} linhas[] = {
			{"Receita líquida (milhões)", [&](const Balanco &b) { return mi(b.receita); }},
			{"Lucro bruto", [&](const Balanco &b) { return mi(b.bruto); }},
			{"Resultado operacional (EBIT)", [&](const Balanco &b) { return mi(b.ebit); }},
			{"Lucro líquido", [&](const Balanco &b) { return mi(b.lucro); }},
			{"Caixa gerado nas operações", [&](const Balanco &b) { return mi(b.operacional); }},
			{"Ativo total", [&](const Balanco &b) { return mi(b.ativo); }},
			{"Patrimônio líquido", [&](const Balanco &b) { return mi(b.pl); }},
			{"Empréstimos e financiamentos", [&](const Balanco &b) { return mi(b.divida); }},
			{"Caixa", [&](const Balanco &b) { return mi(b.caixa); }},
			{"Margem bruta", [&](const Balanco &b) { return pc(b.bruto, b.receita); }},
			{"Margem operacional", [&](const Balanco &b) { return pc(b.ebit, b.receita); }},
			{"Margem líquida", [&](const Balanco &b) { return pc(b.lucro, b.receita); }},
			{"Retorno sobre o patrimônio (ROE)", [&](const Balanco &b) { return pc(b.lucro, b.pl); }},
			{"Dívida líquida ÷ patrimônio", [&](const Balanco &b) {
				return std::isfinite(b.divida) && std::isfinite(b.caixa) && b.pl > 0 ? num((b.divida - b.caixa) / b.pl, 2)
												   : QStringLiteral("—");
			}},
		};
		t_->setRowCount(int(sizeof linhas / sizeof linhas[0]));
		int l = 0;
		for (const auto &x : linhas) {
			auto *n = celula(QString::fromUtf8(x.nome));
			n->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			t_->setItem(l, 0, n);
			for (int k = 0; k < anos.size(); k++)
				t_->setItem(l, k + 1, celula(x.f(anos_[anos[k]])));
			l++;
		}
		t_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		Serie r{"receita", {}, Serie::Barras, false, true}, lu{"lucro líquido", {}, Serie::Barras, false, false};
		for (int k = 0; k < anos.size(); k++) {
			r.pontos << QPointF(k, anos_[anos[k]].receita / 1e6);
			lu.pontos << QPointF(k, anos_[anos[k]].lucro / 1e6);
		}
		g_->formatoX = [anos](double x) {
			const int k = int(std::lround(x));
			return k >= 0 && k < anos.size() ? QString::number(anos[k]) : QString();
		};
		g_->definir("", moeda_ + " milhões", {r, lu}, {}, true, true);
		const Balanco &u = anos_[anos.last()];
		aviso_->setText(QStringLiteral("Em %1: receita de %2, lucro de %3, margem líquida de %4.")
		    .arg(anos.last()).arg(grande(u.receita), grande(u.lucro), pc(u.lucro, u.receita)));
	}

	QLineEdit *ativo_;
	QLabel *titulo_, *aviso_, *fonte_;
	QTableWidget *t_;
	Grafico *g_;
	QMap<int, Balanco> anos_;
	QString moeda_ = "R$";
	const QString fonteCvm_ = QStringLiteral("Fonte: CVM, demonstrações financeiras padronizadas (DFP), dados abertos, em reais "
	    "(na primeira vez baixa uns 25 MB; depois fica guardado por uma semana). Companhias dos EUA: SEC (EDGAR), com o seu "
	    "e-mail na tela CONFIG.");
	int vez_ = 0, faltam_ = 0;
};

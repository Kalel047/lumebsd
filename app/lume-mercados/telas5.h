// SPDX-License-Identifier: BSD-2-Clause
//
// telas5.h - o pregão da B3 (o arquivo oficial do dia), a triagem das
// companhias e as opções.

#pragma once

#include "telas4.h"

// --- O pregão da B3 ----------------------------------------------------------------

// A B3 publica, no fim de cada pregão, todos os negócios do dia num arquivo
// de colunas fixas (COTAHIST): as ações, os fundos e as opções, com o preço
// de fechamento oficial.
struct PapelB3 {
	QString codigo, nome, isin;
	int mercado = 0;	// 10 à vista, 70 opção de compra, 80 opção de venda
	double ultimo = NAO, abertura = NAO, maximo = NAO, minimo = NAO, volume = 0, exercicio = NAO;
	int negocios = 0;
	QDate vencimento;
};

struct PregaoB3 {
	QDate data;
	QVector<PapelB3> papeis;
	QMap<QString, int> onde;
	const PapelB3 *achar(const QString &codigo) const
	{
		const auto it = onde.constFind(codigo);
		return it == onde.constEnd() ? nullptr : &papeis[*it];
	}
};

static void
buscarPregao(QObject *dono, const std::function<void(const PregaoB3 &, const QString &)> &f, int voltar = 0)
{
	static PregaoB3 guardado;
	if (!guardado.papeis.isEmpty() && guardado.data.daysTo(QDate::currentDate()) < 1) {
		QTimer::singleShot(0, dono, [f] { f(guardado, QString()); });
		return;
	}
	// Do dia de hoje para trás, até achar um pregão (fins de semana, feriados).
	QDate d = QDate::currentDate().addDays(-voltar);
	while (d.dayOfWeek() > 5) {
		d = d.addDays(-1);
		voltar++;
	}
	const QString url = QStringLiteral("https://bvmf.bmfbovespa.com.br/InstDados/SerHist/COTAHIST_D%1.ZIP").arg(d.toString("ddMMyyyy"));
	buscar(url, 6 * 3600, dono, [=](const QByteArray &zip, const QString &aviso) {
		if (!zip.startsWith("PK")) {
			if (voltar < 9)
				buscarPregao(dono, f, voltar + 1);
			else
				f(PregaoB3(), aviso.isEmpty() ? QStringLiteral("Não achei o arquivo do pregão da B3.") : aviso);
			return;
		}
		extrairZip(arquivoDe(url), {}, dono, [=](const QByteArray &txt) {
			PregaoB3 p;
			p.data = d;
			auto preco = [](const QByteArray &l, int de) { return l.mid(de, 13).toLongLong() / 100.0; };
			for (const QByteArray &l : txt.split('\n')) {
				if (l.size() < 245 || !l.startsWith("01"))
					continue;
				PapelB3 x;
				x.codigo = QString::fromLatin1(l.mid(12, 12)).trimmed();
				x.mercado = l.mid(24, 3).toInt();
				x.nome = QString::fromLatin1(l.mid(27, 12)).trimmed();
				x.abertura = preco(l, 56);
				x.maximo = preco(l, 69);
				x.minimo = preco(l, 82);
				x.ultimo = preco(l, 108);
				x.negocios = l.mid(147, 5).toInt();
				x.volume = l.mid(170, 18).toLongLong() / 100.0;
				x.exercicio = preco(l, 188);
				x.vencimento = QDate::fromString(QString::fromLatin1(l.mid(202, 8)), "yyyyMMdd");
				x.isin = QString::fromLatin1(l.mid(230, 12));
				p.onde[x.codigo] = p.papeis.size();
				p.papeis << x;
			}
			if (p.papeis.isEmpty()) {
				f(p, QStringLiteral("O arquivo do pregão veio vazio."));
				return;
			}
			guardado = p;
			f(p, aviso);
		});
	});
}

// --- TRI: a triagem das companhias da B3 ---------------------------------------------

struct LinhaTriagem {
	QString codigo, nome, setor;
	double preco = NAO, valor = NAO, pl = NAO, pvp = NAO, roe = NAO, margem = NAO, divida = NAO, receita = NAO,
	       lucro = NAO, crescimento = NAO;
};

class TelaTriagem : public Tela {
public:
	TelaTriagem() : Tela("TRI", "Triagem", "Todas as companhias da B3 de uma vez: filtre por preço, rentabilidade, dívida e tamanho.")
	{
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(14);
		g->setVerticalSpacing(8);
		const struct {
			const char *nome;
			QLineEdit **campo;
			const char *dica;
		} filtros[] = {
			{"P/L até", &plMax_, "15"},		{"P/VP até", &pvpMax_, "2"},
			{"ROE a partir de (%)", &roeMin_, "15"}, {"margem líquida a partir de (%)", &margemMin_, "10"},
			{"dívida líquida ÷ patrimônio até", &dividaMax_, "1"}, {"valor de mercado a partir de (R$ milhões)", &valorMin_, "1.000"},
			{"receita cresceu pelo menos (%)", &crescMin_, "0"}, {"nome ou setor contém", &texto_, "energia"},
		};
		int k = 0;
		for (const auto &x : filtros) {
			*x.campo = campo(QString(), QString::fromUtf8(x.dica));
			(*x.campo)->setMinimumWidth(90);
			(*x.campo)->setMaximumWidth(130);
			g->addWidget(rotulo(QString::fromUtf8(x.nome)), k / 4 * 2, k % 4);
			g->addWidget(*x.campo, k / 4 * 2 + 1, k % 4, Qt::AlignLeft);
			connect(*x.campo, &QLineEdit::textChanged, this, [this] { filtrar(); });
			k++;
		}
		v->addLayout(g);
		auto *h = new QHBoxLayout;
		auto *limpar = botao("Limpar os filtros");
		h->addWidget(limpar);
		const struct {
			const char *nome, *pl, *pvp, *roe, *margem, *divida, *valor;
		} prontos[] = {
			{"baratas e rentáveis", "10", "", "15", "", "", "500"},
			{"boas pagadoras de contas", "", "", "10", "10", "0,5", "500"},
			{"grandes", "", "", "", "", "", "20.000"},
		};
		for (const auto &p : prontos) {
			auto *b = botao(QString::fromUtf8(p.nome), "link");
			h->addWidget(b);
			const QStringList vals{QString::fromUtf8(p.pl), QString::fromUtf8(p.pvp), QString::fromUtf8(p.roe),
				QString::fromUtf8(p.margem), QString::fromUtf8(p.divida), QString::fromUtf8(p.valor)};
			connect(b, &QPushButton::clicked, this, [this, vals] {
				QLineEdit *c[] = {plMax_, pvpMax_, roeMin_, margemMin_, dividaMax_, valorMin_};
				for (int i = 0; i < 6; i++)
					c[i]->setText(vals[i]);
				crescMin_->clear();
				texto_->clear();
			});
		}
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		t_ = tabela({"código", "companhia", "setor", "preço", "valor de mercado", "P/L", "P/VP", "ROE", "margem líq.", "dív. líq./PL",
			     "receita", "cresc."}, 520);
		t_->setMaximumHeight(100000);
		t_->verticalHeader()->setVisible(false);
		t_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		t_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		t_->setSortingEnabled(true);
		v->addWidget(t_, 1);
		v->addWidget(rotulo("Balanços: CVM, demonstrações anuais consolidadas do último exercício. Preços: B3, fechamento do último "
		    "pregão. O valor de mercado soma as ações ordinárias e preferenciais pelo preço de cada uma; é aproximado quando "
		    "há units ou classes pouco negociadas. Clique no título de uma coluna para ordenar; dois cliques abrem a ficha.",
		    "detalhe"));
		connect(limpar, &QPushButton::clicked, this, [this] {
			for (QLineEdit *e : {plMax_, pvpMax_, roeMin_, margemMin_, dividaMax_, valorMin_, crescMin_, texto_})
				e->clear();
		});
		connect(t_, &QTableWidget::cellDoubleClicked, this, [this](int l, int) {
			if (comando)
				comando(t_->item(l, 0)->text() + " DES");
		});
	}
	void abrir(const QStringList &) override
	{
		if (!linhas_.isEmpty() || carregando_)
			return;
		carregando_ = true;
		aviso_->setText("Buscando os códigos, os balanços de todas as companhias (uns 12 MB na primeira vez) e o pregão…");
		buscarCodigos(this, [this](const Codigos &c, const QString &aviso) {
			if (c.porCnpj.isEmpty()) {
				falhou(aviso);
				return;
			}
			balancos(c, QDate::currentDate().year() - 1, false);
		});
	}

	// Para a linha de comando e os testes.
	// As ações de cada companhia pelo Formulário de Referência (em unidades),
	// sem as de tesouraria: CNPJ -> ordinárias, preferenciais.
	static QMap<QString, QPair<double, double>> acoesFre(const QByteArray &csv)
	{
		QMap<QString, QPair<double, double>> r;
		QMap<QString, int> versao;
		QStringList cab;
		for (const QStringList &p : lerCsv(QString::fromLatin1(csv))) {
			if (p.value(0) == "CNPJ_Companhia") {
				cab = p;
				continue;
			}
			if (p.value(cab.indexOf("Tipo_Capital")) != "Capital Integralizado")
				continue;
			const QString cnpj = soDigitos(p.value(0));
			const int v = p.value(2).toInt();
			if (versao.contains(cnpj) && versao[cnpj] > v)
				continue;
			versao[cnpj] = v;
			r[cnpj] = qMakePair(p.value(cab.indexOf("Quantidade_Acoes_Ordinarias")).toDouble(),
			    p.value(cab.indexOf("Quantidade_Acoes_Preferenciais")).toDouble());
		}
		return r;
	}

	static QVector<LinhaTriagem> montar(const Codigos &c, const QByteArray &csv, const PregaoB3 &p,
	    const QMap<QString, QPair<double, double>> &fre = {})
	{
		QMap<QString, QMap<int, Balanco>> todos;
		lerDfp(csv, QString(), &todos);
		// As ações de cada companhia (a composição do capital).
		QMap<QString, QPair<double, double>> acoes;
		bool capital = false;
		for (const QByteArray &bruta : csv.split('\n')) {
			if (bruta.startsWith("CNPJ_CIA")) {
				capital = bruta.contains("QT_ACAO_ORDIN_CAP_INTEGR");
				continue;
			}
			if (!capital)
				continue;
			const QList<QByteArray> x = bruta.trimmed().split(';');
			if (x.size() >= 10)
				acoes[soDigitos(QString::fromLatin1(x[0]))] = qMakePair(x[4].toDouble() - x[7].toDouble(), x[5].toDouble() - x[8].toDouble());
		}
		QVector<LinhaTriagem> r;
		for (auto it = todos.begin(); it != todos.end(); ++it) {
			const QString cnpj = soDigitos(it.key());
			if (!c.todos.contains(cnpj) || it.value().isEmpty())
				continue;
			const int ano = it.value().lastKey();
			const Balanco &b = it.value()[ano];
			LinhaTriagem l;
			l.nome = c.nome.value(cnpj);
			l.setor = c.setor.value(cnpj);
			// O código mais negociado; o valor soma as ordinárias e as preferenciais.
			// Quem só lista a unit (KLBN11) tem as ações no pregão do mesmo jeito.
			double maior = -1, on = NAO, pn = NAO;
			QStringList codigos = c.todos[cnpj];
			for (const QString &cod : c.todos[cnpj])
				for (const char *fim : {"3", "4", "5", "6"})
					if (!codigos.contains(cod.left(4) + fim) && p.achar(cod.left(4) + fim))
						codigos << cod.left(4) + fim;
			for (const QString &cod : codigos) {
				const PapelB3 *x = p.achar(cod);
				if (!x || x->mercado != 10)
					continue;
				// A unit vale pelo conjunto; o preço que a triagem mostra é o do mais negociado.
				if (x->volume > maior) {
					maior = x->volume;
					l.codigo = cod;
					l.preco = x->ultimo;
				}
				if (cod.endsWith('3'))
					on = x->ultimo;
				else if (cod.endsWith('4') || cod.endsWith('5') || cod.endsWith('6'))
					pn = std::isfinite(pn) ? pn : x->ultimo;
			}
			if (l.codigo.isEmpty())
				continue;
			// O Formulário de Referência dá as quantidades em unidades; a composição
			// do capital da DFP, às vezes em milhares (e não diz), fica de reserva.
			const auto q = fre.contains(cnpj) ? fre.value(cnpj) : acoes.value(cnpj);
			const double pOn = std::isfinite(on) ? on : std::isfinite(pn) ? pn : l.preco, pPn = std::isfinite(pn) ? pn : pOn;
			if (q.first + q.second > 0)
				l.valor = q.first * pOn + q.second * pPn;
			l.receita = b.receita;
			l.lucro = b.lucro;
			if (b.pl > 0) {
				l.roe = b.lucro / b.pl;
				l.pvp = l.valor / b.pl;
				if (std::isfinite(b.divida))
					l.divida = (b.divida - (std::isfinite(b.caixa) ? b.caixa : 0)) / b.pl;
			}
			if (b.lucro > 0)
				l.pl = l.valor / b.lucro;
			if (b.receita > 0)
				l.margem = b.lucro / b.receita;
			if (it.value().contains(ano - 1) && it.value()[ano - 1].receita > 0)
				l.crescimento = b.receita / it.value()[ano - 1].receita - 1;
			r << l;
		}
		return r;
	}

private:
	void falhou(const QString &aviso)
	{
		carregando_ = false;
		aviso_->setText(aviso.isEmpty() ? QStringLiteral("Não deu para montar a triagem.") : aviso);
	}

public:
	// As quantidades de ações (FRE); vazio, se não deu.
	static void capitalFre(QObject *dono, const std::function<void(const QMap<QString, QPair<double, double>> &)> &f,
	    int ano = QDate::currentDate().year(), bool segunda = false)
	{
		buscar(urlFre(ano), 7 * 86400, dono, [=](const QByteArray &d, const QString &) {
			if (d.isEmpty()) {
				if (!segunda)
					capitalFre(dono, f, ano - 1, true);
				else
					f({});
				return;
			}
			extrairZip(arquivoDe(urlFre(ano)), {QStringLiteral("fre_cia_aberta_capital_social_%1.csv").arg(ano)}, dono,
			    [f](const QByteArray &csv) { f(acoesFre(csv)); });
		});
	}

private:
	void capital(const std::function<void(const QMap<QString, QPair<double, double>> &)> &f) { capitalFre(this, f); }

	void balancos(const Codigos &c, int ano, bool segunda)
	{
		buscar(urlDfp(ano), 7 * 86400, this, [=](const QByteArray &d, const QString &aviso) {
			if (d.isEmpty()) {
				if (!segunda)
					balancos(c, ano - 1, true);
				else
					falhou(aviso);
				return;
			}
			QStringList m;
			for (const char *x : {"BPA_con", "BPP_con", "DRE_con", "BPA_ind", "BPP_ind", "DRE_ind", "composicao_capital"})
				m << QStringLiteral("dfp_cia_aberta_%1_%2.csv").arg(QLatin1String(x)).arg(ano);
			extrairZip(arquivoDe(urlDfp(ano)), m, this, [=](const QByteArray &csv) {
				buscarPregao(this, [=](const PregaoB3 &p, const QString &aviso) {
					if (p.papeis.isEmpty()) {
						falhou(aviso);
						return;
					}
					capital([=](const QMap<QString, QPair<double, double>> &fre) {
						linhas_ = montar(c, csv, p, fre);
						carregando_ = false;
						fonte_ = QStringLiteral("Balanços de %1; preços do pregão de %2; quantidade de ações do Formulário de "
						    "Referência.").arg(ano).arg(p.data.toString("dd/MM/yyyy"));
						filtrar();
					});
				});
			});
		});
	}

	void filtrar()
	{
		if (linhas_.isEmpty())
			return;
		auto limite = [](QLineEdit *e, double escala = 1) {
			bool ok;
			const double x = lerNumero(e->text(), &ok);
			return ok ? x * escala : NAO;
		};
		const double plMax = limite(plMax_), pvpMax = limite(pvpMax_), roeMin = limite(roeMin_, 0.01), margemMin = limite(margemMin_, 0.01),
			     dividaMax = limite(dividaMax_), valorMin = limite(valorMin_, 1e6), crescMin = limite(crescMin_, 0.01);
		const QString texto = texto_->text().trimmed();
		// "até": quem não tem o número fica de fora; "a partir de" também.
		auto ate = [](double x, double lim) { return std::isnan(lim) || (std::isfinite(x) && x <= lim); };
		auto desde = [](double x, double lim) { return std::isnan(lim) || (std::isfinite(x) && x >= lim); };
		t_->setSortingEnabled(false);
		t_->setRowCount(0);
		for (const LinhaTriagem &l : linhas_) {
			if (!ate(l.pl, plMax) || !ate(l.pvp, pvpMax) || !desde(l.roe, roeMin) || !desde(l.margem, margemMin) ||
			    !ate(l.divida, dividaMax) || !desde(l.valor, valorMin) || !desde(l.crescimento, crescMin))
				continue;
			if (!texto.isEmpty() && !l.nome.contains(texto, Qt::CaseInsensitive) && !l.setor.contains(texto, Qt::CaseInsensitive) &&
			    !l.codigo.contains(texto, Qt::CaseInsensitive))
				continue;
			const int n = t_->rowCount();
			t_->insertRow(n);
			auto esq = [](const QString &s) {
				auto *i = celula(s);
				i->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				return i;
			};
			t_->setItem(n, 0, esq(l.codigo));
			t_->setItem(n, 1, esq(l.nome));
			t_->setItem(n, 2, esq(l.setor.left(34)));
			t_->setItem(n, 3, new CasaNumero(num(l.preco), l.preco));
			t_->setItem(n, 4, new CasaNumero(dinheiroGrande(l.valor), l.valor));
			t_->setItem(n, 5, new CasaNumero(num(l.pl, 1), l.pl));
			t_->setItem(n, 6, new CasaNumero(num(l.pvp, 2), l.pvp));
			t_->setItem(n, 7, new CasaNumero(pct(l.roe, 1), l.roe));
			t_->setItem(n, 8, new CasaNumero(pct(l.margem, 1), l.margem));
			t_->setItem(n, 9, new CasaNumero(num(l.divida, 2), l.divida));
			t_->setItem(n, 10, new CasaNumero(dinheiroGrande(l.receita), l.receita));
			t_->setItem(n, 11, new CasaNumero(pct(l.crescimento, 1), l.crescimento));
		}
		t_->setSortingEnabled(true);
		t_->sortItems(4, Qt::DescendingOrder);
		aviso_->setText(QStringLiteral("%1 de %2 companhias passam nos filtros. %3").arg(t_->rowCount()).arg(linhas_.size()).arg(fonte_));
	}

	QLineEdit *plMax_, *pvpMax_, *roeMin_, *margemMin_, *dividaMax_, *valorMin_, *crescMin_, *texto_;
	QLabel *aviso_;
	QTableWidget *t_;
	QVector<LinhaTriagem> linhas_;
	QString fonte_;
	bool carregando_ = false;
};

// --- OPC: as opções ---------------------------------------------------------------------

struct PontaOpcao {
	QString codigo;
	double ultimo = NAO, compra = NAO, venda = NAO, vol = NAO, delta = NAO, aberto = NAO, negocios = NAO;
};

struct LinhaOpcao {
	double exercicio = 0;
	PontaOpcao call, put;
};

class TelaOpcoes : public Tela {
public:
	TelaOpcoes() : Tela("OPC", "Opções", "A grade de opções de um ativo por vencimento, a volatilidade implícita, e uma calculadora de Black-Scholes.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativo_ = campo("PETR4", "ativo: PETR4, AAPL");
		venc_ = new QComboBox;
		venc_->setMinimumWidth(170);
		perto_ = new QCheckBox("só perto do preço");
		perto_->setChecked(true);
		auto *ir = botao("Mostrar", "primario");
		h->addWidget(ativo_);
		h->addWidget(ir);
		h->addWidget(rotulo("VENCIMENTO"));
		h->addWidget(venc_);
		h->addWidget(perto_);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		t_ = tabela({"compra", "último", "negócios", "vol. impl.", "delta", "EXERCÍCIO", "delta", "vol. impl.", "negócios", "último",
			     "venda"}, 400);
		t_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		t_->horizontalHeader()->setSectionResizeMode(10, QHeaderView::ResizeToContents);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		g_ = new Grafico("O sorriso da volatilidade aparece aqui.");
		v->addWidget(g_);
		fonte_ = rotulo(QString(), "detalhe");
		v->addWidget(fonte_);

		parte("Calculadora", "O preço justo de uma opção europeia pelo modelo de Black-Scholes, e o que mexe nele.");
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(8);
		const char *nomes[] = {"preço do ativo", "preço de exercício", "dias até o vencimento", "volatilidade (% ao ano)", "juros (% ao ano)"};
		const char *ini[] = {"40", "42", "30", "35", "13,75"};
		for (int k = 0; k < 5; k++) {
			c_[k] = campo(QString::fromUtf8(ini[k]));
			g->addWidget(rotulo(QString::fromUtf8(nomes[k])), k, 0);
			g->addWidget(c_[k], k, 1);
			connect(c_[k], &QLineEdit::textChanged, this, [this] { calcular(); });
		}
		g->setColumnMinimumWidth(0, 220);
		g->setColumnStretch(2, 1);
		v->addLayout(g);
		quadro_ = new Quadro(5);
		for (const char *lado : {"compra", "venda"}) {
			const QString l = QString::fromUtf8(lado);
			quadro_->item(l + "p", "Opção de " + l);
			quadro_->item(l + "d", "Delta");
			quadro_->item(l + "g", "Gama");
			quadro_->item(l + "v", "Vega");
			quadro_->item(l + "t", "Teta (por dia)");
		}
		v->addWidget(quadro_);
		v->addStretch(1);
		connect(ir, &QPushButton::clicked, this, [this] { mostrar(); });
		connect(ativo_, &QLineEdit::returnPressed, this, [this] { mostrar(); });
		connect(venc_, &QComboBox::currentIndexChanged, this, [this] { encher(); });
		connect(perto_, &QCheckBox::toggled, this, [this] { encher(); });
		calcular();
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativo_->setText(a.first());
		mostrar();
	}

	// As opções de um papel no arquivo do pregão, por vencimento.
	static QMap<QDate, QVector<LinhaOpcao>> daB3(const PregaoB3 &p, const PapelB3 &ativo, double juros)
	{
		QMap<QDate, QMap<double, LinhaOpcao>> m;
		for (const PapelB3 &x : p.papeis) {
			if ((x.mercado != 70 && x.mercado != 80) || x.isin != ativo.isin || !x.vencimento.isValid())
				continue;
			LinhaOpcao &l = m[x.vencimento][x.exercicio];
			l.exercicio = x.exercicio;
			PontaOpcao &o = x.mercado == 70 ? l.call : l.put;
			o.codigo = x.codigo;
			o.ultimo = x.ultimo;
			o.negocios = x.negocios;
			const double T = std::max(1, int(p.data.daysTo(x.vencimento))) / 365.0;
			o.vol = contas::volImplicita(x.ultimo, ativo.ultimo, x.exercicio, T, juros, x.mercado == 70);
			if (std::isfinite(o.vol))
				o.delta = contas::blackScholes(ativo.ultimo, x.exercicio, T, juros, o.vol, x.mercado == 70).delta;
		}
		QMap<QDate, QVector<LinhaOpcao>> r;
		for (auto it = m.begin(); it != m.end(); ++it)
			r[it.key()] = it.value().values().toVector();
		return r;
	}

	// As da Cboe: o código traz o vencimento, o lado e o exercício
	// (AAPL261218C00250000).
	static QMap<QDate, QVector<LinhaOpcao>> daCboe(const QByteArray &json, double *preco)
	{
		const QJsonObject d = QJsonDocument::fromJson(json).object().value("data").toObject();
		*preco = d.value("current_price").toDouble(NAO);
		QMap<QDate, QMap<double, LinhaOpcao>> m;
		static const QRegularExpression re(QStringLiteral("^[A-Z.]+(\\d{6})([CP])(\\d{8})$"));
		for (const QJsonValue &v : d.value("options").toArray()) {
			const QJsonObject o = v.toObject();
			const auto c = re.match(o.value("option").toString());
			if (!c.hasMatch())
				continue;
			const QDate venc = QDate::fromString("20" + c.captured(1), "yyyyMMdd");
			const double k = c.captured(3).toDouble() / 1000;
			LinhaOpcao &l = m[venc][k];
			l.exercicio = k;
			PontaOpcao &p = c.captured(2) == "C" ? l.call : l.put;
			p.codigo = o.value("option").toString();
			p.ultimo = o.value("last_trade_price").toDouble(NAO);
			p.compra = o.value("bid").toDouble(NAO);
			p.venda = o.value("ask").toDouble(NAO);
			p.vol = o.value("iv").toDouble() > 0 ? o.value("iv").toDouble() : NAO;
			p.delta = o.value("delta").toDouble(NAO);
			p.aberto = o.value("open_interest").toDouble(NAO);
			p.negocios = o.value("volume").toDouble(NAO);
		}
		QMap<QDate, QVector<LinhaOpcao>> r;
		for (auto it = m.begin(); it != m.end(); ++it)
			r[it.key()] = it.value().values().toVector();
		return r;
	}

private:
	void mostrar()
	{
		const QString s = simboloDe(ativo_->text());
		const int vez = ++vez_;
		aviso_->setText("Buscando…");
		grade_.clear();
		if (s.endsWith(".SA")) {
			// Os juros de hoje (a Selic), para a volatilidade implícita.
			buscar("https://api.bcb.gov.br/dados/serie/bcdata.sgs.432/dados/ultimos/1?formato=json", 12 * 3600, this,
			    [this, vez, s](const QByteArray &d, const QString &) {
				const auto p = lerSgs(d);
				const double juros = p.isEmpty() ? 0.12 : std::log(1 + p.last().second / 100);
				buscarPregao(this, [this, vez, s, juros](const PregaoB3 &pr, const QString &aviso) {
					if (vez != vez_)
						return;
					const PapelB3 *a = pr.achar(curto(s));
					if (!a) {
						aviso_->setText(aviso.isEmpty() ? QStringLiteral("Não achei %1 no pregão da B3.").arg(curto(s)) : aviso);
						return;
					}
					preco_ = a->ultimo;
					grade_ = daB3(pr, *a, juros);
					fonte_->setText(QStringLiteral("Fonte: B3, arquivo do pregão de %1 (preços de fechamento; só as séries que "
					    "negociaram). A volatilidade implícita e o delta são calculados aqui por Black-Scholes, com a Selic. "
					    "Opções brasileiras de compra não são corrigidas por dividendos nesta conta.").arg(pr.data.toString("dd/MM/yyyy")));
					vencimentos(curto(s));
				});
			});
			return;
		}
		QString raiz = curto(s);
		if (s.startsWith('^'))
			raiz = "_" + s.mid(1);	// os índices: _SPX
		buscar(QStringLiteral("https://cdn.cboe.com/api/global/delayed_quotes/options/%1.json").arg(raiz), 300, this,
		    [this, vez, raiz](const QByteArray &d, const QString &aviso) {
			if (vez != vez_)
				return;
			grade_ = daCboe(d, &preco_);
			if (grade_.isEmpty()) {
				aviso_->setText(aviso.isEmpty() || aviso.contains("HTTP 4")
				    ? QStringLiteral("Não achei opções de %1. As da B3 vão pelo código da ação (PETR4); as dos EUA, pelo de lá (AAPL).").arg(raiz)
				    : aviso);
				encher();
				return;
			}
			fonte_->setText("Fonte: Cboe, cotações com 15 minutos de atraso; a volatilidade implícita e o delta são os da Cboe.");
			vencimentos(raiz);
		});
	}

	void vencimentos(const QString &nome)
	{
		const QSignalBlocker b(venc_);
		venc_->clear();
		const QDate hoje = QDate::currentDate();
		int escolha = 0;
		for (const QDate &d : grade_.keys()) {
			venc_->addItem(QStringLiteral("%1 (%2 dias)").arg(d.toString("dd/MM/yyyy")).arg(hoje.daysTo(d)), d);
			// De saída, o primeiro vencimento a mais de uma semana.
			if (escolha == 0 && hoje.daysTo(d) >= 7)
				escolha = venc_->count() - 1;
		}
		venc_->setCurrentIndex(escolha);
		nome_ = nome;
		encher();
	}

	void encher()
	{
		t_->setRowCount(0);
		const QDate d = venc_->currentData().toDate();
		if (!grade_.contains(d)) {
			g_->limpar();
			return;
		}
		Serie sc{"compra", {}, Serie::Linha, false, false}, sp{"venda", {}, Serie::Linha, true, false};
		int total = 0;
		for (const LinhaOpcao &l : grade_[d]) {
			total++;
			if (perto_->isChecked() && std::isfinite(preco_) && std::fabs(l.exercicio / preco_ - 1) > 0.25)
				continue;
			const int n = t_->rowCount();
			t_->insertRow(n);
			auto ponta = [&](const PontaOpcao &o, int cod, int ult, int neg, int vol, int del) {
				auto *c = celula(o.codigo);
				c->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				t_->setItem(n, cod, c);
				t_->setItem(n, ult, celula(o.codigo.isEmpty() ? QString() : num(o.ultimo)));
				t_->setItem(n, neg, celula(o.codigo.isEmpty() ? QString() : num(o.negocios, 0)));
				t_->setItem(n, vol, celula(std::isfinite(o.vol) ? pct(o.vol, 1) : QString()));
				t_->setItem(n, del, celula(std::isfinite(o.delta) ? num(o.delta, 2) : QString()));
			};
			ponta(l.call, 0, 1, 2, 3, 4);
			ponta(l.put, 10, 9, 8, 7, 6);
			auto *k = celula(num(l.exercicio));
			k->setTextAlignment(Qt::AlignCenter);
			QFont f = k->font();
			f.setBold(true);
			k->setFont(f);
			t_->setItem(n, 5, k);
			if (std::isfinite(l.call.vol))
				sc.pontos << QPointF(l.exercicio, l.call.vol * 100);
			if (std::isfinite(l.put.vol))
				sp.pontos << QPointF(l.exercicio, l.put.vol * 100);
		}
		aviso_->setText(QStringLiteral("%1 a %2: %3 exercícios neste vencimento, %4 na tela.").arg(nome_, num(preco_))
		    .arg(total).arg(t_->rowCount()));
		g_->formatoX = nullptr;
		g_->formatoY = [](double y) { return num(y, 0) + "%"; };
		if (sc.pontos.size() + sp.pontos.size() > 2)
			g_->definir("preço de exercício", "volatilidade implícita", {sc, sp}, {{preco_, "preço hoje"}});
		else
			g_->limpar();
	}

	void calcular()
	{
		double x[5];
		for (int k = 0; k < 5; k++) {
			bool ok;
			x[k] = lerNumero(c_[k]->text(), &ok);
			if (!ok) {
				quadro_->limpar();
				return;
			}
		}
		const double T = x[2] / 365, r = std::log(1 + x[4] / 100), vol = x[3] / 100;
		for (const char *lado : {"compra", "venda"}) {
			const QString l = QString::fromUtf8(lado);
			const contas::Opcao o = contas::blackScholes(x[0], x[1], T, r, vol, l == "compra");
			quadro_->valor(l + "p", num(o.preco, 4));
			quadro_->valor(l + "d", num(o.delta, 4), "por R$ 1 do ativo");
			quadro_->valor(l + "g", num(o.gama, 4));
			quadro_->valor(l + "v", num(o.vega, 4), "por ponto de volatilidade");
			quadro_->valor(l + "t", num(o.teta, 4));
		}
	}

	QLineEdit *ativo_, *c_[5];
	QComboBox *venc_;
	QCheckBox *perto_;
	QLabel *aviso_, *fonte_;
	QTableWidget *t_;
	Grafico *g_;
	Quadro *quadro_;
	QMap<QDate, QVector<LinhaOpcao>> grade_;
	QString nome_;
	double preco_ = NAO;
	int vez_ = 0;
};

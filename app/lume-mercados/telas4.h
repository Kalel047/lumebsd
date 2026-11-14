// SPDX-License-Identifier: BSD-2-Clause
//
// telas4.h - as notícias, a carteira, os títulos públicos e a calculadora
// de títulos.

#pragma once

#include "telas3.h"

// --- NOT: as notícias --------------------------------------------------------------

class TelaNoticias : public Tela {
public:
	TelaNoticias() : Tela("NOT", "Notícias", "As manchetes sobre um ativo ou um assunto. Dois cliques abrem a notícia no navegador.")
	{
		auto *h = new QHBoxLayout;
		assunto_ = campo("mercado financeiro", "assunto ou ativo");
		assunto_->setMinimumWidth(320);
		auto *ir = botao("Buscar", "primario");
		h->addWidget(assunto_);
		h->addWidget(ir);
		for (const char *a : {"economia", "Banco Central", "bolsa", "dólar", "petróleo", "agronegócio"}) {
			auto *b = botao(QString::fromUtf8(a), "link");
			h->addWidget(b);
			connect(b, &QPushButton::clicked, this, [this, a] {
				assunto_->setText(QString::fromUtf8(a));
				buscarNoticias();
			});
		}
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "detalhe");
		v->addWidget(aviso_);
		t_ = tabela({"quando", "onde saiu", "manchete"}, 620);
		t_->setMaximumHeight(100000);
		t_->verticalHeader()->setVisible(false);
		t_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		t_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		t_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_, 1);
		connect(ir, &QPushButton::clicked, this, [this] { buscarNoticias(); });
		connect(assunto_, &QLineEdit::returnPressed, this, [this] { buscarNoticias(); });
		connect(t_, &QTableWidget::cellDoubleClicked, this, [this](int l, int) {
			QDesktopServices::openUrl(QUrl(t_->item(l, 2)->data(Qt::UserRole).toString()));
		});
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			assunto_->setText(a.join(" "));
		buscarNoticias();
	}

private:
	void buscarNoticias()
	{
		QUrl u("https://news.google.com/rss/search");
		QUrlQuery q;
		q.addQueryItem("q", QString::fromLatin1(QUrl::toPercentEncoding(assunto_->text().trimmed())));
		q.addQueryItem("hl", "pt-BR");
		q.addQueryItem("gl", "BR");
		q.addQueryItem("ceid", "BR:pt-419");
		u.setQuery(q);
		aviso_->setText("Buscando…");
		const int vez = ++vez_;
		buscar(QString::fromLatin1(u.toEncoded()), 300, this, [this, vez](const QByteArray &d, const QString &aviso) {
			if (vez != vez_)
				return;
			struct Item {
				QDateTime quando;
				QString titulo, fonte, link;
			};
			QVector<Item> itens;
			QXmlStreamReader x(d);
			Item i;
			bool dentro = false;
			while (!x.atEnd()) {
				x.readNext();
				if (x.isStartElement() && x.name() == QLatin1String("item")) {
					dentro = true;
					i = Item();
				} else if (x.isEndElement() && x.name() == QLatin1String("item")) {
					dentro = false;
					itens << i;
				} else if (dentro && x.isStartElement()) {
					const QString n = x.name().toString();
					if (n == "title")
						i.titulo = x.readElementText();
					else if (n == "link")
						i.link = x.readElementText();
					else if (n == "source")
						i.fonte = x.readElementText();
					else if (n == "pubDate")
						i.quando = QDateTime::fromString(x.readElementText().replace(" GMT", " +0000"), Qt::RFC2822Date).toLocalTime();
				}
			}
			std::sort(itens.begin(), itens.end(), [](const Item &a, const Item &b) { return a.quando > b.quando; });
			t_->setRowCount(0);
			for (const Item &n : itens) {
				const int l = t_->rowCount();
				t_->insertRow(l);
				t_->setItem(l, 0, celula(n.quando.toString(n.quando.date() == QDate::currentDate() ? "HH:mm" : "dd/MM HH:mm")));
				auto *f = celula(n.fonte);
				f->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				t_->setItem(l, 1, f);
				// O título vem como "manchete - veículo".
				QString titulo = n.titulo;
				if (!n.fonte.isEmpty() && titulo.endsWith(" - " + n.fonte))
					titulo.chop(n.fonte.size() + 3);
				auto *m = celula(titulo);
				m->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				m->setData(Qt::UserRole, n.link);
				m->setToolTip(titulo);
				t_->setItem(l, 2, m);
			}
			aviso_->setText(itens.isEmpty() ? (aviso.isEmpty() ? QStringLiteral("Nada sobre isso.") : aviso)
			    : QStringLiteral("%1 manchetes, as mais novas primeiro. Fonte: Google Notícias; cada notícia é do veículo que a publicou. %2")
				.arg(itens.size()).arg(aviso));
		});
	}

	QLineEdit *assunto_;
	QLabel *aviso_;
	QTableWidget *t_;
	int vez_ = 0;
};

// --- CART: a carteira e a lista de acompanhamento ---------------------------------

class TelaCarteira : public Tela {
public:
	TelaCarteira() : Tela("CART", "Carteira", "Os seus ativos: quanto valem hoje e quanto renderam. Sem quantidade, o ativo é só acompanhado.")
	{
		quadro_ = new Quadro(4);
		quadro_->item("valor", "Valor da carteira");
		quadro_->item("dia", "No dia");
		quadro_->item("resultado", "Resultado");
		quadro_->item("custo", "Quanto custou");
		v->addWidget(quadro_);
		t_ = tabela({"ativo", "quantidade", "preço médio", "último", "no dia", "valor (R$)", "resultado (R$)", "resultado", "peso"}, 380);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		auto *h = new QHBoxLayout;
		auto *mais = botao("+ ativo"), *menos = botao("Tirar"), *at = botao("Atualizar"), *g = botao("Gráfico", "link"),
		     *c = botao("Correlações", "link");
		for (QPushButton *b : {mais, menos, at, g, c})
			h->addWidget(b);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "detalhe");
		v->addWidget(aviso_);
		v->addStretch(1);
		connect(mais, &QPushButton::clicked, this, [this] {
			linha("", NAO, NAO);
			t_->setCurrentCell(t_->rowCount() - 1, 0);
			t_->editItem(t_->item(t_->rowCount() - 1, 0));
		});
		connect(menos, &QPushButton::clicked, this, [this] {
			if (t_->currentRow() >= 0) {
				t_->removeRow(t_->currentRow());
				gravar();
				calcular();
			}
		});
		connect(at, &QPushButton::clicked, this, [this] { atualizar(); });
		connect(g, &QPushButton::clicked, this, [this] {
			if (comando && !ativos().isEmpty())
				comando(ativos().mid(0, 4).join(" ") + " G");
		});
		connect(c, &QPushButton::clicked, this, [this] {
			if (comando && ativos().size() > 1)
				comando(ativos().mid(0, 10).join(" ") + " CORR");
		});
		connect(t_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *i) {
			if (i->column() > 2)
				return;
			{
				const QSignalBlocker b(t_);
				if (i->column() == 0)
					i->setText(i->text().trimmed().toUpper());
				else {
					bool ok;
					const double x = lerNumero(i->text(), &ok);
					i->setText(ok ? num(x, i->column() == 1 && x == std::floor(x) ? 0 : 2) : QString());
				}
			}
			gravar();
			if (i->column() == 0)
				atualizar();
			else
				calcular();
		});
		ler();
	}
	void abrir(const QStringList &a) override
	{
		for (const QString &x : a)
			if (!ativos().contains(x.toUpper())) {
				linha(x.toUpper(), NAO, NAO);
				gravar();
			}
		atualizar();
	}

private:
	static QString arquivo() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/carteira.json"; }

	void linha(const QString &ativo, double qtd, double preco)
	{
		const QSignalBlocker b(t_);
		const int l = t_->rowCount();
		t_->insertRow(l);
		auto *a = new QTableWidgetItem(ativo);
		t_->setItem(l, 0, a);
		t_->setItem(l, 1, celula(std::isfinite(qtd) ? num(qtd, qtd == std::floor(qtd) ? 0 : 2) : QString(), true));
		t_->setItem(l, 2, celula(std::isfinite(preco) ? num(preco) : QString(), true));
		for (int c = 3; c < 9; c++)
			t_->setItem(l, c, celula(QString()));
	}
	QStringList ativos() const
	{
		QStringList r;
		for (int l = 0; l < t_->rowCount(); l++)
			if (!t_->item(l, 0)->text().isEmpty())
				r << t_->item(l, 0)->text();
		return r;
	}
	void ler()
	{
		QFile f(arquivo());
		if (!f.open(QIODevice::ReadOnly)) {
			for (const char *a : {"PETR4", "VALE3", "ITUB4", "AAPL", "DOLAR", "BTC"})
				linha(QLatin1String(a), NAO, NAO);
			return;
		}
		for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
			const QJsonObject o = v.toObject();
			linha(o.value("ativo").toString(), o.value("quantidade").toDouble(NAO), o.value("preco").toDouble(NAO));
		}
	}
	void gravar()
	{
		QJsonArray a;
		for (int l = 0; l < t_->rowCount(); l++) {
			QJsonObject o;
			o["ativo"] = t_->item(l, 0)->text();
			bool ok;
			double x = lerNumero(t_->item(l, 1)->text(), &ok);
			if (ok)
				o["quantidade"] = x;
			x = lerNumero(t_->item(l, 2)->text(), &ok);
			if (ok)
				o["preco"] = x;
			a.append(o);
		}
		QDir().mkpath(QFileInfo(arquivo()).path());
		QFile f(arquivo());
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
			f.write(QJsonDocument(a).toJson());
	}
	void atualizar()
	{
		QStringList s{"USDBRL=X", "EURBRL=X"};
		for (const QString &a : ativos())
			if (!s.contains(simboloDe(a)))
				s << simboloDe(a);
		aviso_->setText("Buscando…");
		buscarVarios(s, "5d", "1d", this, [this](const QMap<QString, Cotacao> &q, const QString &aviso) {
			q_ = q;
			calcular();
			aviso_->setText((aviso.isEmpty() ? QString() : aviso + " ") + QString::fromUtf8(FONTE_COTACOES) +
			    " Ativos em dólar e em euro são convertidos para reais. Guardado em " + QDir::toNativeSeparators(arquivo()) + ".");
		});
	}
	void calcular()
	{
		const QSignalBlocker b(t_);
		double total = 0, custo = 0, ontem = 0;
		QVector<double> valores(t_->rowCount(), NAO);
		for (int l = 0; l < t_->rowCount(); l++) {
			const Cotacao c = q_.value(simboloDe(t_->item(l, 0)->text()));
			for (int k = 3; k < 9; k++)
				t_->item(l, k)->setText(QString());
			if (!c.ok) {
				if (!q_.isEmpty() && !t_->item(l, 0)->text().isEmpty())
					t_->item(l, 3)->setText("não achei");
				continue;
			}
			t_->item(l, 3)->setText(num(c.preco, casasDe(c.preco)));
			t_->item(l, 4)->setText(variacao(c.dia()));
			bool temQ, temP;
			const double qtd = lerNumero(t_->item(l, 1)->text(), &temQ), pm = lerNumero(t_->item(l, 2)->text(), &temP);
			if (!temQ)
				continue;
			double cambio = 1, cambioOntem = 1;
			if (c.moeda == "USD" || c.moeda == "EUR") {
				const Cotacao fx = q_.value(c.moeda + "BRL=X");
				cambio = fx.ok ? fx.preco : NAO;
				cambioOntem = fx.ok ? fx.anterior : NAO;
			} else if (c.moeda != "BRL")
				cambio = cambioOntem = NAO;
			const double valor = qtd * c.preco * cambio;
			valores[l] = valor;
			t_->item(l, 5)->setText(std::isfinite(valor) ? num(valor) : "em " + c.moeda);
			if (!std::isfinite(valor))
				continue;
			total += valor;
			ontem += qtd * c.anterior * cambioOntem;
			if (temP) {
				// O preço médio está na moeda do ativo; o câmbio de hoje nos dois lados.
				const double pago = qtd * pm * cambio;
				custo += pago;
				t_->item(l, 6)->setText(num(valor - pago));
				t_->item(l, 7)->setText(variacao(valor / pago - 1));
			} else
				custo += valor;
		}
		for (int l = 0; l < t_->rowCount(); l++)
			if (std::isfinite(valores[l]) && total > 0)
				t_->item(l, 8)->setText(pct(valores[l] / total, 1));
		quadro_->valor("valor", total > 0 ? moeda(total) : QStringLiteral("—"), total > 0 ? QString() : "preencha as quantidades");
		quadro_->valor("dia", total > 0 ? variacao(total / ontem - 1) : QStringLiteral("—"), total > 0 ? moeda(total - ontem) : QString());
		quadro_->valor("resultado", total > 0 ? moeda(total - custo) : QStringLiteral("—"),
		    total > 0 && custo > 0 ? variacao(total / custo - 1) : QString());
		quadro_->valor("custo", total > 0 ? moeda(custo) : QStringLiteral("—"), "pelo preço médio");
	}

	Quadro *quadro_;
	QTableWidget *t_;
	QLabel *aviso_;
	QMap<QString, Cotacao> q_;
};

// --- TESOURO: os títulos públicos --------------------------------------------------

static const char *URL_TESOURO = "https://www.tesourotransparente.gov.br/ckan/dataset/df56aa42-484a-4a59-8184-7676580c81e3/"
				 "resource/796d2059-14e9-44e3-80c9-2d9e30b405c1/download/PrecoTaxaTesouroDireto.csv";

class TelaTesouro : public Tela {
public:
	TelaTesouro() : Tela("TESOURO", "Tesouro Direto", "As taxas e os preços dos títulos públicos, e a curva de juros que eles desenham.")
	{
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		g_ = new Grafico("A curva aparece aqui.");
		g_->setMinimumHeight(300);
		v->addWidget(g_);
		t_ = tabela({"título", "vencimento", "taxa de compra", "taxa de venda", "preço de compra", "preço de venda"}, 420);
		t_->verticalHeader()->setVisible(false);
		t_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		v->addWidget(t_);
		v->addWidget(rotulo("Fonte: Tesouro Nacional (Tesouro Transparente), dados abertos. Nos títulos IPCA+ a taxa é o juro "
		    "real, acima da inflação; no Tesouro Selic, o que se soma à Selic. O arquivo tem 14 MB e fica guardado por 12 horas.",
		    "detalhe"));
		v->addStretch(1);
	}
	void abrir(const QStringList &) override
	{
		if (carregado_)
			return;
		aviso_->setText("Buscando no Tesouro (14 MB na primeira vez)…");
		buscar(QString::fromLatin1(URL_TESOURO), 12 * 3600, this, [this](const QByteArray &d, const QString &aviso) {
			struct L {
				QString tipo;
				QDate venc;
				double tc, tv, pc, pv;
			};
			QVector<L> ls;
			int ultimo = 0;
			auto chave = [](const QByteArray &s) {	// dd/mm/aaaa -> aaaammdd
				return s.size() == 10 ? s.mid(6, 4).toInt() * 10000 + s.mid(3, 2).toInt() * 100 + s.mid(0, 2).toInt() : 0;
			};
			const QList<QByteArray> linhas = d.split('\n');
			for (const QByteArray &l : linhas) {
				const int a = l.indexOf(';'), b = l.indexOf(';', a + 1), c = l.indexOf(';', b + 1);
				if (c > 0)
					ultimo = std::max(ultimo, chave(l.mid(b + 1, c - b - 1)));
			}
			for (const QByteArray &l : linhas) {
				const QList<QByteArray> p = l.trimmed().split(';');
				if (p.size() < 7 || chave(p[2]) != ultimo)
					continue;
				ls.push_back({QString::fromLatin1(p[0]), QDate::fromString(QString::fromLatin1(p[1]), "dd/MM/yyyy"),
				    lerNumero(QString::fromLatin1(p[3])), lerNumero(QString::fromLatin1(p[4])),
				    lerNumero(QString::fromLatin1(p[5])), lerNumero(QString::fromLatin1(p[6]))});
			}
			if (ls.isEmpty()) {
				aviso_->setText(aviso.isEmpty() ? QStringLiteral("O arquivo do Tesouro veio vazio.") : aviso);
				return;
			}
			carregado_ = true;
			std::sort(ls.begin(), ls.end(), [](const L &a, const L &b) { return a.tipo == b.tipo ? a.venc < b.venc : a.tipo < b.tipo; });
			t_->setRowCount(0);
			Serie pre{"prefixado", {}, Serie::Linha, false, false}, ipca{"IPCA+ (juro real)", {}, Serie::Linha, true, false};
			const QDate hoje = QDate::currentDate();
			for (const L &x : ls) {
				const int l = t_->rowCount();
				t_->insertRow(l);
				auto *n = celula(x.tipo);
				n->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				t_->setItem(l, 0, n);
				t_->setItem(l, 1, celula(x.venc.toString("dd/MM/yyyy")));
				t_->setItem(l, 2, celula(num(x.tc) + "%"));
				t_->setItem(l, 3, celula(num(x.tv) + "%"));
				t_->setItem(l, 4, celula(x.pc > 0 ? num(x.pc) : QStringLiteral("—")));
				t_->setItem(l, 5, celula(num(x.pv)));
				const double anos = hoje.daysTo(x.venc) / 365.25;
				if (anos <= 0)
					continue;
				if (x.tipo == "Tesouro Prefixado")
					pre.pontos << QPointF(anos, x.tc);
				else if (x.tipo == "Tesouro IPCA+")
					ipca.pontos << QPointF(anos, x.tc);
			}
			g_->formatoX = [](double x) { return num(x, x == std::floor(x) ? 0 : 1); };
			g_->formatoY = [](double y) { return num(y, 1) + "%"; };
			g_->definir("anos até o vencimento", "% ao ano", {pre, ipca});
			const int d8 = ultimo;
			aviso_->setText(QStringLiteral("Taxas de %1/%2/%3. %4").arg(d8 % 100, 2, 10, QLatin1Char('0'))
			    .arg(d8 / 100 % 100, 2, 10, QLatin1Char('0')).arg(d8 / 10000).arg(aviso));
		});
	}

private:
	QLabel *aviso_;
	Grafico *g_;
	QTableWidget *t_;
	bool carregado_ = false;
};

// --- TIT: a calculadora de títulos --------------------------------------------------

class TelaTitulo : public Tela {
public:
	TelaTitulo() : Tela("TIT", "Calculadora de títulos", "O preço de um título de renda fixa a cada taxa, a duração e o quanto ele sente os juros.")
	{
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(8);
		face_ = campo("1.000");
		cupom_ = campo("10");
		vezes_ = new QComboBox;
		vezes_->addItems({"1 por ano", "2 por ano (semestral)", "4 por ano", "12 por ano"});
		vezes_->setCurrentIndex(1);
		anos_ = campo("10");
		taxa_ = campo("12");
		preco_ = campo("");
		auto *bp = botao("calcular", "link"), *bt = botao("calcular", "link");
		int l = 0;
		auto linha = [&](const char *n, QWidget *w, QWidget *extra = nullptr) {
			g->addWidget(rotulo(QString::fromUtf8(n)), l, 0);
			g->addWidget(w, l, 1);
			if (extra)
				g->addWidget(extra, l, 2, Qt::AlignLeft);
			l++;
		};
		linha("valor de face", face_);
		linha("cupom (% ao ano)", cupom_);
		linha("pagamentos", vezes_);
		linha("anos até o vencimento", anos_);
		linha("taxa de mercado (% ao ano)", taxa_, bt);
		linha("preço", preco_, bp);
		g->setColumnMinimumWidth(0, 220);
		g->setColumnStretch(3, 1);
		v->addLayout(g);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		quadro_ = new Quadro(3);
		quadro_->item("preco", "Preço");
		quadro_->item("corrente", "Rendimento corrente");
		quadro_->item("duracao", "Duração (Macaulay)");
		quadro_->item("mod", "Duração modificada");
		quadro_->item("conv", "Convexidade");
		quadro_->item("dv01", "DV01");
		v->addWidget(quadro_);
		gr_ = new Grafico;
		gr_->formatoX = [](double x) { return num(x, 0) + "%"; };
		v->addWidget(gr_);
		v->addWidget(rotulo("A taxa é capitalizada a cada pagamento (a convenção dos títulos americanos e europeus). "
		    "Cupom zero dá um título de desconto, como o Tesouro Prefixado.", "detalhe"));
		v->addStretch(1);
		connect(bp, &QPushButton::clicked, this, [this] { calcular(false); });
		connect(bt, &QPushButton::clicked, this, [this] { calcular(true); });
		for (QLineEdit *e : {face_, cupom_, anos_, taxa_})
			connect(e, &QLineEdit::returnPressed, this, [this] { calcular(false); });
		connect(preco_, &QLineEdit::returnPressed, this, [this] { calcular(true); });
		connect(vezes_, &QComboBox::currentIndexChanged, this, [this] { calcular(false); });
		calcular(false);
	}

private:
	void calcular(bool daTaxa)
	{
		bool a, b, c, d;
		const double face = lerNumero(face_->text(), &a), cupom = lerNumero(cupom_->text(), &b) / 100,
			     anos = lerNumero(anos_->text(), &c);
		const int vezes = QVector<int>{1, 2, 4, 12}[vezes_->currentIndex()];
		if (!a || !b || !c || face <= 0 || anos <= 0 || anos > 100) {
			aviso_->setText("Preencha o valor de face, o cupom e os anos.");
			return;
		}
		double taxa;
		if (daTaxa) {
			const double preco = lerNumero(preco_->text(), &d);
			if (!d || preco <= 0) {
				aviso_->setText("Preencha o preço.");
				return;
			}
			taxa = contas::taxaDoTitulo(face, cupom, vezes, anos, preco);
			taxa_->setText(num(taxa * 100, 4));
		} else {
			taxa = lerNumero(taxa_->text(), &d) / 100;
			if (!d || taxa <= -1) {
				aviso_->setText("Preencha a taxa.");
				return;
			}
		}
		const contas::Titulo t = contas::titulo(face, cupom, vezes, anos, taxa);
		if (!daTaxa)
			preco_->setText(num(t.preco, 2));
		quadro_->valor("preco", num(t.preco, 2), pct(t.preco / face, 2) + " do valor de face");
		quadro_->valor("corrente", pct(t.corrente), "o cupom do ano sobre o preço");
		quadro_->valor("duracao", num(t.duracao, 2) + " anos", "o prazo médio dos pagamentos");
		quadro_->valor("mod", num(t.modificada, 2), QStringLiteral("juros +1 ponto: preço %1").arg(variacao(-t.modificada * 0.01, 1)));
		quadro_->valor("conv", num(t.convexidade, 2));
		quadro_->valor("dv01", num(t.dv01, 4), "quanto o preço muda com 0,01 ponto");
		aviso_->setText(t.preco > face * 1.0005 ? "Negocia com ágio: o cupom paga mais que a taxa do mercado."
		    : t.preco < face * 0.9995 ? "Negocia com deságio: o cupom paga menos que a taxa do mercado."
					      : "Negocia ao par: o cupom é igual à taxa do mercado.");
		Serie s{"preço", {}, Serie::Linha, false, false};
		const double ate = std::max(0.2, taxa * 2);
		for (int k = 0; k <= 80; k++) {
			const double y = ate * k / 80;
			s.pontos << QPointF(y * 100, contas::titulo(face, cupom, vezes, anos, y).preco);
		}
		gr_->definir("taxa de mercado", "preço", {s}, {{taxa * 100, "hoje"}});
	}

	QLineEdit *face_, *cupom_, *anos_, *taxa_, *preco_;
	QComboBox *vezes_;
	QLabel *aviso_;
	Quadro *quadro_;
	Grafico *gr_;
};

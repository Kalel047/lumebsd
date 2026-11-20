// SPDX-License-Identifier: BSD-2-Clause
//
// telas6.h - os alertas de preço, o simulador de ordens, a conversa (IRC) e
// o que a pessoa configura.

#pragma once

#include "telas5.h"

// --- ALERTA: avisos de preço ---------------------------------------------------------

struct Alerta {
	QString ativo, disparou;
	bool acima = true;
	double valor = 0;
};

static QString
arquivoAlertas()
{
	return pastaDados() + "/alertas.json";
}

static QVector<Alerta>
lerAlertas()
{
	QVector<Alerta> r;
	QFile f(arquivoAlertas());
	if (f.open(QIODevice::ReadOnly))
		for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
			const QJsonObject o = v.toObject();
			r.push_back({o.value("ativo").toString(), o.value("disparou").toString(), o.value("acima").toBool(), o.value("valor").toDouble()});
		}
	return r;
}

static void
gravarAlertas(const QVector<Alerta> &a)
{
	QJsonArray j;
	for (const Alerta &x : a)
		j.append(QJsonObject{{"ativo", x.ativo}, {"disparou", x.disparou}, {"acima", x.acima}, {"valor", x.valor}});
	QFile f(arquivoAlertas());
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(j).toJson());
}

// Confere os alertas que ainda não dispararam; devolve os que dispararam agora.
static void
conferirAlertas(QObject *dono, const std::function<void(const QStringList &avisos, const QMap<QString, Cotacao> &)> &f)
{
	const QVector<Alerta> a = lerAlertas();
	QStringList s;
	for (const Alerta &x : a)
		if (!s.contains(simboloDe(x.ativo)))
			s << simboloDe(x.ativo);
	if (s.isEmpty()) {
		QTimer::singleShot(0, dono, [f] { f({}, {}); });
		return;
	}
	buscarVarios(s, "5d", "1d", dono, [f](const QMap<QString, Cotacao> &q, const QString &) {
		// Relê: a pessoa pode ter mexido na lista enquanto a cotação vinha.
		QVector<Alerta> a = lerAlertas();
		QStringList avisos;
		for (Alerta &x : a) {
			const Cotacao c = q.value(simboloDe(x.ativo));
			if (!c.ok || !x.disparou.isEmpty())
				continue;
			if (x.acima ? c.preco >= x.valor : c.preco <= x.valor) {
				x.disparou = QDateTime::currentDateTime().toString("dd/MM HH:mm");
				avisos << QStringLiteral("%1 %2 %3: está em %4").arg(x.ativo, x.acima ? "chegou a" : "caiu a", num(x.valor, casasDe(x.valor)),
				    num(c.preco, casasDe(c.preco)));
			}
		}
		if (!avisos.isEmpty())
			gravarAlertas(a);
		f(avisos, q);
	});
}

class TelaAlertas : public Tela {
public:
	TelaAlertas() : Tela("ALERTA", "Alertas", "Um aviso quando um ativo chega a um preço. Confere a cada minuto, enquanto o programa está aberto.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativo_ = campo("", "ativo");
		sentido_ = new QComboBox;
		sentido_->addItems({"subir até", "cair até"});
		valor_ = campo("", "preço");
		auto *mais = botao("Criar o alerta", "primario");
		h->addWidget(rotulo("avise quando", "texto"));
		h->addWidget(ativo_);
		h->addWidget(sentido_);
		h->addWidget(valor_);
		h->addWidget(mais);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		t_ = tabela({"ativo", "condição", "preço do alerta", "agora", "falta", "situação"}, 380);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		auto *b = new QHBoxLayout;
		auto *tirar = botao("Tirar"), *rearmar = botao("Armar de novo"), *conferir = botao("Conferir agora");
		for (QPushButton *x : {tirar, rearmar, conferir})
			b->addWidget(x);
		b->addStretch(1);
		v->addLayout(b);
		v->addWidget(rotulo("O aviso aparece na área de notificação e aqui. Com o programa fechado ninguém confere: para isso, "
		    "“lume-mercados --alertas” pode rodar de tempos em tempos (cron) e imprime os que dispararam. As cotações têm atraso.",
		    "detalhe"));
		v->addStretch(1);
		connect(mais, &QPushButton::clicked, this, [this] { criar(); });
		connect(valor_, &QLineEdit::returnPressed, this, [this] { criar(); });
		connect(tirar, &QPushButton::clicked, this, [this] {
			QVector<Alerta> a = lerAlertas();
			if (t_->currentRow() >= 0 && t_->currentRow() < a.size()) {
				a.removeAt(t_->currentRow());
				gravarAlertas(a);
				mostrar({});
				conferir_();
			}
		});
		connect(rearmar, &QPushButton::clicked, this, [this] {
			QVector<Alerta> a = lerAlertas();
			if (t_->currentRow() >= 0 && t_->currentRow() < a.size()) {
				a[t_->currentRow()].disparou.clear();
				gravarAlertas(a);
				conferir_();
			}
		});
		connect(conferir, &QPushButton::clicked, this, [this] { conferir_(); });
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativo_->setText(a.first().toUpper());
		conferir_();
	}
	// Chamado pelo relógio do programa, com as cotações que ele acabou de buscar.
	void mostrar(const QMap<QString, Cotacao> &q)
	{
		if (!q.isEmpty())
			q_ = q;
		const QVector<Alerta> a = lerAlertas();
		t_->setRowCount(0);
		for (const Alerta &x : a) {
			const int n = t_->rowCount();
			t_->insertRow(n);
			const Cotacao c = q_.value(simboloDe(x.ativo));
			t_->setItem(n, 0, celula(x.ativo));
			t_->setItem(n, 1, celula(x.acima ? "subir até" : "cair até"));
			t_->setItem(n, 2, celula(num(x.valor, casasDe(x.valor))));
			t_->setItem(n, 3, celula(c.ok ? num(c.preco, casasDe(c.preco)) : QStringLiteral("—")));
			t_->setItem(n, 4, celula(c.ok ? variacao(x.valor / c.preco - 1) : QString()));
			t_->setItem(n, 5, celula(x.disparou.isEmpty() ? QStringLiteral("armado") : "disparou em " + x.disparou));
		}
		aviso_->setText(a.isEmpty() ? QStringLiteral("Nenhum alerta ainda.") : QStringLiteral("%1 alertas. Conferido às %2.")
		    .arg(a.size()).arg(QTime::currentTime().toString("HH:mm")));
	}

private:
	void criar()
	{
		bool ok;
		const double x = lerNumero(valor_->text(), &ok);
		if (ativo_->text().trimmed().isEmpty() || !ok || x <= 0) {
			aviso_->setText("Preencha o ativo e o preço.");
			return;
		}
		QVector<Alerta> a = lerAlertas();
		a.push_back({ativo_->text().trimmed().toUpper(), QString(), sentido_->currentIndex() == 0, x});
		gravarAlertas(a);
		valor_->clear();
		conferir_();
	}
	void conferir_()
	{
		mostrar({});
		conferirAlertas(this, [this](const QStringList &, const QMap<QString, Cotacao> &q) { mostrar(q); });
	}

	QLineEdit *ativo_, *valor_;
	QComboBox *sentido_;
	QLabel *aviso_;
	QTableWidget *t_;
	QMap<QString, Cotacao> q_;
};

// --- ORDEM: o simulador de ordens ------------------------------------------------------

// Uma conta de mentira, com dinheiro de mentira, para treinar: as ordens são
// executadas aqui, contra as cotações (com atraso), e NENHUMA vai para uma
// bolsa ou corretora. Tudo em reais; ativos em dólar e em euro são convertidos.
struct OrdemSim {
	QString ativo, quando;
	bool compra = true;
	double qtd = 0, limite = NAO, preco = NAO;	// limite NaN = a mercado
};

struct Simulador {
	double caixa = 100000, inicial = 100000;
	QMap<QString, QPair<double, double>> posicoes;	// ativo -> quantidade, preço médio em reais
	QVector<OrdemSim> abertas, feitas;

	static QString arquivo() { return pastaDados() + "/simulador.json"; }

	static QJsonObject paraJson(const OrdemSim &o)
	{
		QJsonObject j{{"ativo", o.ativo}, {"quando", o.quando}, {"compra", o.compra}, {"qtd", o.qtd}};
		if (std::isfinite(o.limite))
			j["limite"] = o.limite;
		if (std::isfinite(o.preco))
			j["preco"] = o.preco;
		return j;
	}
	static OrdemSim deJson(const QJsonObject &j)
	{
		return {j.value("ativo").toString(), j.value("quando").toString(), j.value("compra").toBool(), j.value("qtd").toDouble(),
			j.value("limite").toDouble(NAO), j.value("preco").toDouble(NAO)};
	}
	void ler()
	{
		QFile f(arquivo());
		if (!f.open(QIODevice::ReadOnly))
			return;
		const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
		caixa = o.value("caixa").toDouble(100000);
		inicial = o.value("inicial").toDouble(100000);
		const QJsonObject p = o.value("posicoes").toObject();
		for (auto it = p.begin(); it != p.end(); ++it)
			posicoes[it.key()] = qMakePair(it.value().toObject().value("qtd").toDouble(), it.value().toObject().value("pm").toDouble());
		for (const QJsonValue &v : o.value("abertas").toArray())
			abertas << deJson(v.toObject());
		for (const QJsonValue &v : o.value("feitas").toArray())
			feitas << deJson(v.toObject());
	}
	void gravar() const
	{
		QJsonObject p;
		for (auto it = posicoes.begin(); it != posicoes.end(); ++it)
			p[it.key()] = QJsonObject{{"qtd", it.value().first}, {"pm", it.value().second}};
		QJsonArray a, fe;
		for (const OrdemSim &o : abertas)
			a.append(paraJson(o));
		for (const OrdemSim &o : feitas)
			fe.append(paraJson(o));
		QFile f(arquivo());
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
			f.write(QJsonDocument(QJsonObject{{"caixa", caixa}, {"inicial", inicial}, {"posicoes", p}, {"abertas", a}, {"feitas", fe}}).toJson());
	}

	// O preço do ativo em reais, pelas cotações; NaN se não dá para converter.
	static double emReais(const QString &ativo, const QMap<QString, Cotacao> &q)
	{
		const Cotacao c = q.value(simboloDe(ativo));
		if (!c.ok)
			return NAO;
		if (c.moeda == "BRL")
			return c.preco;
		const Cotacao fx = q.value(c.moeda + "BRL=X");
		return fx.ok ? c.preco * fx.preco : NAO;
	}

	// Executa a ordem ao preço dado (em reais); devolve o erro, ou vazio.
	QString executar(OrdemSim o, double preco)
	{
		const double total = o.qtd * preco;
		auto &p = posicoes[o.ativo];
		if (o.compra) {
			if (total > caixa + 0.005) {
				if (p.first == 0)
					posicoes.remove(o.ativo);
				return QStringLiteral("Falta dinheiro: a compra custa %1 e há %2 em caixa.").arg(moeda(total), moeda(caixa));
			}
			p.second = (p.first * p.second + total) / (p.first + o.qtd);
			p.first += o.qtd;
			caixa -= total;
		} else {
			if (o.qtd > p.first + 1e-9) {
				const double tem = p.first;
				if (tem == 0)
					posicoes.remove(o.ativo);
				return QStringLiteral("Você tem %1 de %2; o simulador não vende a descoberto.").arg(num(tem, 0), o.ativo);
			}
			p.first -= o.qtd;
			caixa += total;
			if (p.first < 1e-9)
				posicoes.remove(o.ativo);
		}
		o.preco = preco;
		o.quando = QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm");
		feitas.prepend(o);
		return QString();
	}

	// As ordens limitadas que o preço alcançou.
	int conferirAbertas(const QMap<QString, Cotacao> &q)
	{
		int n = 0;
		for (int k = abertas.size() - 1; k >= 0; k--) {
			const double p = emReais(abertas[k].ativo, q);
			if (!std::isfinite(p) || (abertas[k].compra ? p > abertas[k].limite : p < abertas[k].limite))
				continue;
			if (executar(abertas[k], p).isEmpty())
				n++;
			abertas.removeAt(k);
		}
		return n;
	}
	double patrimonio(const QMap<QString, Cotacao> &q) const
	{
		double t = caixa;
		for (auto it = posicoes.begin(); it != posicoes.end(); ++it) {
			const double p = emReais(it.key(), q);
			t += it.value().first * (std::isfinite(p) ? p : it.value().second);
		}
		return t;
	}
	QStringList simbolos(const QString &mais = {}) const
	{
		QStringList s{"USDBRL=X", "EURBRL=X"};
		QStringList a = posicoes.keys();
		for (const OrdemSim &o : abertas)
			a << o.ativo;
		if (!mais.isEmpty())
			a << mais;
		for (const QString &x : a)
			if (!s.contains(simboloDe(x)))
				s << simboloDe(x);
		return s;
	}
};


// --- A corretora (Alpaca) ------------------------------------------------------------

// A Alpaca, corretora americana, aceita ordens por programa com as chaves
// que a própria pessoa cria lá. Duas contas: a de papel (de treino, da
// própria Alpaca) e a real. LUME_MERCADOS_ALPACA troca o endereço (os testes).
static QString
urlAlpaca(bool real)
{
	const QString t = qEnvironmentVariable("LUME_MERCADOS_ALPACA");
	if (!t.isEmpty())
		return t;
	return real ? QStringLiteral("https://api.alpaca.markets") : QStringLiteral("https://paper-api.alpaca.markets");
}

using RespostaApi = std::function<void(int http, const QJsonDocument &, const QString &erro)>;

static void
pedirAlpaca(bool real, const QByteArray &metodo, const QString &caminho, const QJsonObject &corpo, QObject *dono, const RespostaApi &f)
{
	QNetworkRequest req{QUrl(urlAlpaca(real) + caminho)};
	req.setRawHeader("APCA-API-KEY-ID", config(real ? "alpaca_real_id" : "alpaca_id").toLatin1());
	req.setRawHeader("APCA-API-SECRET-KEY", config(real ? "alpaca_real_segredo" : "alpaca_segredo").toLatin1());
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setHeader(QNetworkRequest::UserAgentHeader, "lume-mercados/1.0 (LumeBSD)");
	req.setTransferTimeout(30000);
	QNetworkReply *r = rede()->sendCustomRequest(req, metodo, corpo.isEmpty() ? QByteArray() : QJsonDocument(corpo).toJson(QJsonDocument::Compact));
	QObject::connect(r, &QNetworkReply::finished, dono, [r, f] {
		r->deleteLater();
		const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray d = r->readAll();
		const QJsonDocument j = QJsonDocument::fromJson(d);
		QString erro;
		if (http < 200 || http >= 300)
			erro = j.object().value("message").toString(http ? QStringLiteral("HTTP %1").arg(http) : r->errorString());
		f(http, j, erro);
	});
}

class TelaOrdens : public Tela {
public:
	TelaOrdens() : Tela("ORDEM", "Ordens", "O simulador do LumeBSD, com dinheiro de mentira, ou a sua conta na corretora Alpaca (ações dos EUA).")
	{
		sim_.ler();
		auto *c = new QHBoxLayout;
		c->addWidget(rotulo("CONTA"));
		conta_ = new QComboBox;
		conta_->addItems({"simulador do LumeBSD (dinheiro de mentira, B3 e EUA)", "Alpaca, conta de papel (treino, ações dos EUA)",
				  "Alpaca, conta REAL (dinheiro de verdade, ações dos EUA)"});
		c->addWidget(conta_);
		c->addStretch(1);
		v->addLayout(c);
		quadro_ = new Quadro(4);
		quadro_->item("pat", "Patrimônio");
		quadro_->item("caixa", "Em caixa");
		quadro_->item("res", "Resultado");
		quadro_->item("n", "Ordens executadas");
		v->addWidget(quadro_);
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		lado_ = new QComboBox;
		lado_->addItems({"comprar", "vender"});
		qtd_ = campo("100", "quantidade");
		qtd_->setMaximumWidth(110);
		ativo_ = campo("PETR4", "ativo");
		tipo_ = new QComboBox;
		tipo_->addItems({"a mercado", "limitada a R$"});
		limite_ = campo("", "preço");
		limite_->setMaximumWidth(110);
		auto *enviar = botao("Enviar a ordem", "primario");
		for (QWidget *w : std::initializer_list<QWidget *>{lado_, qtd_, ativo_, tipo_, limite_, enviar})
			h->addWidget(w);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		parte("Posições");
		pos_ = tabela({"ativo", "quantidade", "preço médio", "agora", "valor", "resultado", ""}, 190);
		v->addWidget(pos_);
		parte("Ordens esperando o preço");
		ab_ = tabela({"ativo", "ordem", "quantidade", "limite", "agora / situação"}, 120);
		v->addWidget(ab_);
		auto *cancelar = botao("Cancelar a ordem");
		v->addWidget(cancelar, 0, Qt::AlignLeft);
		parte("Executadas");
		fe_ = tabela({"quando", "ativo", "ordem", "quantidade", "preço", "total"}, 200);
		v->addWidget(fe_);
		auto *zerar = botao("Recomeçar com R$ 100.000");
		v->addWidget(zerar, 0, Qt::AlignLeft);
		for (QTableWidget *t : {pos_, ab_, fe_}) {
			t->verticalHeader()->setVisible(false);
			t->setSelectionBehavior(QAbstractItemView::SelectRows);
		}
		v->addWidget(rotulo("No simulador do LumeBSD, nenhuma ordem sai deste computador: as a mercado saem ao último preço da "
		    "cotação (com atraso), sem corretagem nem imposto; as limitadas esperam o preço chegar, conferido a cada minuto. "
		    "Na Alpaca, as ordens vão para a corretora, com as chaves que você cria lá e põe na tela CONFIG; a conta real pede "
		    "confirmação a cada ordem. As corretoras brasileiras não abrem o envio de ordens a programas de pessoa física; "
		    "para a B3, só o simulador.", "detalhe"));
		v->addStretch(1);
		connect(enviar, &QPushButton::clicked, this, [this] { conta_->currentIndex() ? enviarAlpaca() : enviar_(); });
		connect(conta_, &QComboBox::currentIndexChanged, this, [this] { atualizar(); });
		connect(cancelar, &QPushButton::clicked, this, [this] {
			if (conta_->currentIndex() && ab_->currentRow() >= 0) {
				const QString id = ab_->item(ab_->currentRow(), 0)->data(Qt::UserRole).toString();
				pedirAlpaca(real(), "DELETE", "/v2/orders/" + id, {}, this, [this](int, const QJsonDocument &, const QString &erro) {
					aviso_->setText(erro.isEmpty() ? QStringLiteral("Pedido de cancelamento enviado à Alpaca.") : "A Alpaca respondeu: " + erro);
					atualizar();
				});
				return;
			}
			if (ab_->currentRow() >= 0 && ab_->currentRow() < sim_.abertas.size()) {
				sim_.abertas.removeAt(ab_->currentRow());
				sim_.gravar();
				mostrar();
			}
		});
		connect(zerar, &QPushButton::clicked, this, [this] {
			if (QMessageBox::question(this, "Simulador", "Apagar as posições e o histórico e recomeçar com R$ 100.000?") != QMessageBox::Yes)
				return;
			sim_ = Simulador();
			sim_.gravar();
			mostrar();
		});
		auto *relogio = new QTimer(this);
		connect(relogio, &QTimer::timeout, this, [this] {
			if (isVisible() || (!sim_.abertas.isEmpty() && !conta_->currentIndex()))
				atualizar();
		});
		relogio->start(60000);
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativo_->setText(a.first().toUpper());
		atualizar();
	}

private:
	bool real() const { return conta_->currentIndex() == 2; }

	void enviarAlpaca()
	{
		bool ok, okL = true;
		const QString ativo = ativo_->text().trimmed().toUpper();
		const double qtd = lerNumero(qtd_->text(), &ok);
		const bool limitada = tipo_->currentIndex() == 1;
		const double limite = limitada ? lerNumero(limite_->text(), &okL) : NAO;
		if (ativo.isEmpty() || !ok || qtd <= 0 || !okL || simboloDe(ativo).endsWith(".SA")) {
			aviso_->setText(simboloDe(ativo).endsWith(".SA") ? QStringLiteral("A Alpaca negocia ações dos EUA; para a B3, use o simulador.")
									  : QStringLiteral("Preencha o ativo, a quantidade e, na ordem limitada, o preço (em dólares)."));
			return;
		}
		const QString resumo = QStringLiteral("%1 %2 %3 %4").arg(lado_->currentIndex() == 0 ? "Comprar" : "Vender", num(qtd, qtd == std::floor(qtd) ? 0 : 4), ativo,
		    limitada ? QStringLiteral("a no máximo US$ %1").arg(num(limite)) : QStringLiteral("a mercado"));
		if (real() && QMessageBox::warning(this, "Ordem real", resumo + QStringLiteral(", na sua conta REAL da Alpaca, com dinheiro de verdade?"),
				  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
			return;
		QJsonObject o{{"symbol", ativo}, {"qty", QString::number(qtd, 'g', 10)}, {"side", lado_->currentIndex() == 0 ? "buy" : "sell"},
			      {"type", limitada ? "limit" : "market"}, {"time_in_force", "day"}};
		if (limitada)
			o["limit_price"] = QString::number(limite, 'f', 2);
		aviso_->setText("Enviando à Alpaca…");
		pedirAlpaca(real(), "POST", "/v2/orders", o, this, [this, resumo](int, const QJsonDocument &j, const QString &erro) {
			aviso_->setText(erro.isEmpty() ? QStringLiteral("A Alpaca recebeu a ordem (%1): %2.").arg(j.object().value("status").toString(), resumo)
						       : "A Alpaca recusou: " + erro);
			atualizar();
		});
	}

	// A conta, as posições e as ordens, pela Alpaca.
	void atualizarAlpaca()
	{
		const QString id = config(real() ? "alpaca_real_id" : "alpaca_id");
		for (QTableWidget *t : {pos_, ab_, fe_})
			t->setRowCount(0);
		quadro_->limpar();
		if (id.isEmpty()) {
			aviso_->setText(QStringLiteral("Ponha as chaves da conta %1 da Alpaca na tela CONFIG (crie em alpaca.markets).")
			    .arg(real() ? "real" : "de papel"));
			return;
		}
		const int vez = ++vez_;
		pedirAlpaca(real(), "GET", "/v2/account", {}, this, [this, vez](int, const QJsonDocument &j, const QString &erro) {
			if (vez != vez_)
				return;
			if (!erro.isEmpty()) {
				aviso_->setText("A Alpaca respondeu: " + erro);
				return;
			}
			const QJsonObject a = j.object();
			auto n = [&](const char *k) { return a.value(k).toString().toDouble(); };
			quadro_->valor("pat", "US$ " + num(n("equity")), a.value("status").toString());
			quadro_->valor("caixa", "US$ " + num(n("cash")), "poder de compra: US$ " + num(n("buying_power")));
			quadro_->valor("res", "US$ " + num(n("equity") - n("last_equity")), "hoje: " + variacao(n("equity") / n("last_equity") - 1));
			quadro_->valor("n", QString::number(fe_->rowCount()), QStringLiteral("conta %1 (%2)").arg(a.value("account_number").toString(), real() ? "real" : "de papel"));
		});
		pedirAlpaca(real(), "GET", "/v2/positions", {}, this, [this, vez](int, const QJsonDocument &j, const QString &) {
			if (vez != vez_)
				return;
			for (const QJsonValue &v : j.array()) {
				const QJsonObject p = v.toObject();
				auto n = [&](const char *k) { return p.value(k).toString().toDouble(); };
				const int l = pos_->rowCount();
				pos_->insertRow(l);
				pos_->setItem(l, 0, celula(p.value("symbol").toString()));
				pos_->setItem(l, 1, celula(num(n("qty"), 0)));
				pos_->setItem(l, 2, celula(num(n("avg_entry_price"))));
				pos_->setItem(l, 3, celula(num(n("current_price"))));
				pos_->setItem(l, 4, celula("US$ " + num(n("market_value"))));
				pos_->setItem(l, 5, celula("US$ " + num(n("unrealized_pl"))));
				pos_->setItem(l, 6, celula(variacao(n("unrealized_plpc"))));
			}
		});
		pedirAlpaca(real(), "GET", "/v2/orders?status=all&limit=50", {}, this, [this, vez](int, const QJsonDocument &j, const QString &) {
			if (vez != vez_)
				return;
			for (const QJsonValue &v : j.array()) {
				const QJsonObject o = v.toObject();
				const QString st = o.value("status").toString();
				const bool aberta = st == "new" || st == "accepted" || st == "partially_filled" || st == "pending_new";
				const QString lado = o.value("side").toString() == "buy" ? "compra" : "venda";
				if (aberta) {
					const int l = ab_->rowCount();
					ab_->insertRow(l);
					auto *i = celula(o.value("symbol").toString());
					i->setData(Qt::UserRole, o.value("id").toString());
					ab_->setItem(l, 0, i);
					ab_->setItem(l, 1, celula(lado + (o.value("type").toString() == "limit" ? " limitada" : "")));
					ab_->setItem(l, 2, celula(o.value("qty").toString()));
					ab_->setItem(l, 3, celula(o.value("limit_price").toString()));
					ab_->setItem(l, 4, celula(st));
				} else if (st == "filled") {
					const int l = fe_->rowCount();
					fe_->insertRow(l);
					const double q = o.value("filled_qty").toString().toDouble(), p = o.value("filled_avg_price").toString().toDouble();
					fe_->setItem(l, 0, celula(QDateTime::fromString(o.value("filled_at").toString(), Qt::ISODateWithMs).toLocalTime().toString("dd/MM/yyyy HH:mm")));
					fe_->setItem(l, 1, celula(o.value("symbol").toString()));
					fe_->setItem(l, 2, celula(lado));
					fe_->setItem(l, 3, celula(num(q, 0)));
					fe_->setItem(l, 4, celula(num(p)));
					fe_->setItem(l, 5, celula("US$ " + num(q * p)));
				}
			}
		});
	}

	void atualizar(const std::function<void()> &depois = nullptr)
	{
		if (conta_->currentIndex()) {
			atualizarAlpaca();
			return;
		}
		buscarVarios(sim_.simbolos(ativo_->text().trimmed()), "5d", "1d", this, [this, depois](const QMap<QString, Cotacao> &q, const QString &aviso) {
			q_ = q;
			if (sim_.conferirAbertas(q) > 0)
				sim_.gravar();
			if (!aviso.isEmpty())
				aviso_->setText(aviso);
			if (depois)
				depois();
			mostrar();
		});
	}
	void enviar_()
	{
		bool ok, okL = true;
		OrdemSim o;
		o.ativo = ativo_->text().trimmed().toUpper();
		o.compra = lado_->currentIndex() == 0;
		o.qtd = lerNumero(qtd_->text(), &ok);
		if (tipo_->currentIndex() == 1)
			o.limite = lerNumero(limite_->text(), &okL);
		if (o.ativo.isEmpty() || !ok || o.qtd <= 0 || !okL || (tipo_->currentIndex() == 1 && o.limite <= 0)) {
			aviso_->setText("Preencha o ativo, a quantidade e, na ordem limitada, o preço.");
			return;
		}
		aviso_->setText("Buscando o preço…");
		atualizar([this, o] {
			const double p = Simulador::emReais(o.ativo, q_);
			if (!std::isfinite(p)) {
				aviso_->setText(QStringLiteral("Não achei o preço de %1 em reais.").arg(o.ativo));
				return;
			}
			const bool ja = !std::isfinite(o.limite) || (o.compra ? p <= o.limite : p >= o.limite);
			if (!ja) {
				sim_.abertas << o;
				aviso_->setText(QStringLiteral("Ordem guardada: %1 %2 de %3 quando o preço %4 a %5 (agora, %6).")
				    .arg(o.compra ? "comprar" : "vender", num(o.qtd, 0), o.ativo, o.compra ? "cair" : "subir", moeda(o.limite), moeda(p)));
			} else {
				const QString erro = sim_.executar(o, p);
				aviso_->setText(erro.isEmpty() ? QStringLiteral("Executada no simulador: %1 de %2 %3 a %4, total de %5.")
				    .arg(o.compra ? "compra" : "venda", num(o.qtd, 0), o.ativo, moeda(p), moeda(o.qtd * p)) : erro);
			}
			sim_.gravar();
		});
	}
	void mostrar()
	{
		const double pat = sim_.patrimonio(q_);
		quadro_->valor("pat", moeda(pat));
		quadro_->valor("caixa", moeda(sim_.caixa));
		quadro_->valor("res", moeda(pat - sim_.inicial), variacao(pat / sim_.inicial - 1));
		quadro_->valor("n", QString::number(sim_.feitas.size()));
		pos_->setRowCount(0);
		for (auto it = sim_.posicoes.begin(); it != sim_.posicoes.end(); ++it) {
			const int n = pos_->rowCount();
			pos_->insertRow(n);
			const double p = Simulador::emReais(it.key(), q_), qtd = it.value().first, pm = it.value().second;
			pos_->setItem(n, 0, celula(it.key()));
			pos_->setItem(n, 1, celula(num(qtd, qtd == std::floor(qtd) ? 0 : 4)));
			pos_->setItem(n, 2, celula(num(pm)));
			pos_->setItem(n, 3, celula(num(p)));
			pos_->setItem(n, 4, celula(moeda(qtd * p)));
			pos_->setItem(n, 5, celula(moeda(qtd * (p - pm))));
			pos_->setItem(n, 6, celula(variacao(p / pm - 1)));
		}
		ab_->setRowCount(0);
		for (const OrdemSim &o : sim_.abertas) {
			const int n = ab_->rowCount();
			ab_->insertRow(n);
			ab_->setItem(n, 0, celula(o.ativo));
			ab_->setItem(n, 1, celula(o.compra ? "compra" : "venda"));
			ab_->setItem(n, 2, celula(num(o.qtd, 0)));
			ab_->setItem(n, 3, celula(num(o.limite)));
			ab_->setItem(n, 4, celula(num(Simulador::emReais(o.ativo, q_))));
		}
		fe_->setRowCount(0);
		for (const OrdemSim &o : sim_.feitas) {
			const int n = fe_->rowCount();
			fe_->insertRow(n);
			fe_->setItem(n, 0, celula(o.quando));
			fe_->setItem(n, 1, celula(o.ativo));
			fe_->setItem(n, 2, celula(o.compra ? "compra" : "venda"));
			fe_->setItem(n, 3, celula(num(o.qtd, 0)));
			fe_->setItem(n, 4, celula(num(o.preco)));
			fe_->setItem(n, 5, celula(moeda(o.qtd * o.preco)));
		}
	}

	Simulador sim_;
	Quadro *quadro_;
	int vez_ = 0;
	QComboBox *lado_, *tipo_, *conta_;
	QLineEdit *qtd_, *ativo_, *limite_;
	QLabel *aviso_;
	QTableWidget *pos_, *ab_, *fe_;
	QMap<QString, Cotacao> q_;
};

// --- CHAT: a conversa ------------------------------------------------------------------

// Um cliente de IRC, o protocolo aberto de conversa: quem usa os Mercados
// (e qualquer programa de IRC) entra no mesmo canal de um servidor público.
// Não é uma rede privada: o canal é aberto, e o servidor vê o endereço de
// quem entra. Só conecta quando a pessoa pede.
class TelaChat : public Tela {
public:
	TelaChat() : Tela("CHAT", "Conversa", "Um canal aberto de conversa (IRC) com quem mais usa os Mercados. Só conecta quando você pede.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		entrar_ = botao("Entrar", "primario");
		estado_ = rotulo(QString(), "aviso");
		h->addWidget(entrar_);
		h->addWidget(estado_, 1);
		v->addLayout(h);
		conversa_ = new QPlainTextEdit;
		conversa_->setReadOnly(true);
		conversa_->setMinimumHeight(460);
		conversa_->setMaximumBlockCount(2000);
		v->addWidget(conversa_, 1);
		auto *e = new QHBoxLayout;
		fala_ = campo("", "escreva e aperte Enter");
		e->addWidget(fala_, 1);
		v->addLayout(e);
		v->addWidget(rotulo("O canal é público: qualquer pessoa pode ler, não escreva nada reservado, e desconfie de dicas de "
		    "investimento de desconhecidos. O servidor, o canal e o apelido ficam na tela CONFIG.", "detalhe"));
		connect(entrar_, &QPushButton::clicked, this, [this] {
			if (s_)
				sair("Você saiu.");
			else
				conectar();
		});
		connect(fala_, &QLineEdit::returnPressed, this, [this] {
			const QString t = fala_->text().trimmed();
			if (t.isEmpty() || !dentro_)
				return;
			mandar(QStringLiteral("PRIVMSG %1 :%2").arg(canal_, t));
			dizer(QStringLiteral("<%1> %2").arg(apelido_, t));
			fala_->clear();
		});
		mostrarEstado();
	}

private:
	void mostrarEstado(const QString &extra = {})
	{
		entrar_->setText(s_ ? "Sair" : "Entrar");
		estado_->setText(!extra.isEmpty() ? extra : dentro_ ? QStringLiteral("Em %1, no servidor %2, como %3.").arg(canal_, servidor_, apelido_)
		    : QStringLiteral("Fora da conversa. Vai entrar em %1, no servidor %2.").arg(config("chat_canal", "#lumebsd"),
			config("chat_servidor", "irc.libera.chat")));
	}
	void dizer(const QString &t) { conversa_->appendPlainText(QTime::currentTime().toString("HH:mm ") + t); }
	void mandar(const QString &l)
	{
		if (s_)
			s_->write(l.toUtf8() + "\r\n");
	}
	void conectar()
	{
		servidor_ = config("chat_servidor", "irc.libera.chat");
		const int porta = config("chat_porta", "6697").toInt();
		canal_ = config("chat_canal", "#lumebsd");
		apelido_ = config("chat_apelido");
		if (apelido_.isEmpty())
			apelido_ = QStringLiteral("lume%1").arg(QRandomGenerator::global()->bounded(1000, 9999));
		s_ = new QSslSocket(this);
		buf_.clear();
		auto ola = [this] {
			mandar("NICK " + apelido_);
			mandar("USER lume 0 * :Mercados do LumeBSD");
		};
		// A porta 6667 é a sem criptografia; as outras, com.
		if (porta == 6667)
			connect(s_, &QSslSocket::connected, this, ola);
		else
			connect(s_, &QSslSocket::encrypted, this, ola);
		connect(s_, &QSslSocket::readyRead, this, [this] { ler(); });
		connect(s_, &QSslSocket::disconnected, this, [this] { sair("A conexão caiu."); });
		connect(s_, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
			sair(QStringLiteral("Não deu para conectar: %1").arg(s_ ? s_->errorString() : QString()));
		});
		if (porta == 6667)
			s_->connectToHost(servidor_, quint16(porta));
		else
			s_->connectToHostEncrypted(servidor_, quint16(porta));
		mostrarEstado(QStringLiteral("Conectando a %1…").arg(servidor_));
	}
	void sair(const QString &motivo)
	{
		if (!s_)
			return;
		QSslSocket *s = s_;
		s_ = nullptr;
		dentro_ = false;
		s->disconnect(this);
		s->write("QUIT :ate logo\r\n");
		s->disconnectFromHost();
		s->deleteLater();
		dizer("* " + motivo);
		mostrarEstado();
	}
	void ler()
	{
		if (!s_)
			return;
		buf_ += s_->readAll();
		int fim;
		while (s_ && (fim = buf_.indexOf('\n')) >= 0) {
			const QString l = QString::fromUtf8(buf_.left(fim)).trimmed();
			buf_.remove(0, fim + 1);
			linha(l);
		}
	}
	// :apelido!usuario@maquina COMANDO parâmetros :texto
	void linha(const QString &l)
	{
		if (l.startsWith("PING")) {
			mandar("PONG" + l.mid(4));
			return;
		}
		QString resto = l, de;
		if (resto.startsWith(':')) {
			de = resto.mid(1, resto.indexOf(' ') - 1);
			resto = resto.mid(resto.indexOf(' ') + 1);
		}
		const QString quem = de.section('!', 0, 0);
		const QString cmd = resto.section(' ', 0, 0);
		const int dois = resto.indexOf(" :");
		const QString texto = dois >= 0 ? resto.mid(dois + 2) : QString();
		if (cmd == "001") {
			mandar("JOIN " + canal_);
		} else if (cmd == "433") {	// o apelido já tem dono
			apelido_ += QString::number(QRandomGenerator::global()->bounded(10, 99));
			mandar("NICK " + apelido_);
		} else if (cmd == "JOIN") {
			if (quem == apelido_) {
				dentro_ = true;
				dizer(QStringLiteral("* Você entrou em %1.").arg(canal_));
				mostrarEstado();
			} else
				dizer(QStringLiteral("* %1 entrou.").arg(quem));
		} else if (cmd == "PART" || cmd == "QUIT") {
			dizer(QStringLiteral("* %1 saiu.").arg(quem));
		} else if (cmd == "PRIVMSG") {
			const QString para = resto.section(' ', 1, 1);
			if (texto.startsWith(QChar(1)))	// pedidos automáticos (CTCP): não responde
				return;
			dizer(para == canal_ ? QStringLiteral("<%1> %2").arg(quem, texto) : QStringLiteral("[só para você] <%1> %2").arg(quem, texto));
		} else if (cmd == "353") {
			dizer(QStringLiteral("* Aqui: %1").arg(texto));
		} else if (cmd == "332") {
			dizer(QStringLiteral("* Assunto: %1").arg(texto));
		} else if (cmd == "ERROR" || cmd == "465" || cmd == "471" || cmd == "473" || cmd == "474" || cmd == "475" || cmd == "477") {
			dizer(QStringLiteral("* O servidor recusou: %1").arg(texto));
		}
	}

	QPushButton *entrar_;
	QLabel *estado_;
	QPlainTextEdit *conversa_;
	QLineEdit *fala_;
	QSslSocket *s_ = nullptr;
	QByteArray buf_;
	QString servidor_, canal_, apelido_;
	bool dentro_ = false;
};

// --- CONFIG -----------------------------------------------------------------------------

class TelaConfig : public Tela {
public:
	TelaConfig() : Tela("CONFIG", "Configuração", "O que só você pode dar: o seu contato para a SEC, as suas chaves gratuitas, e onde conversar.")
	{
		const struct {
			const char *chave, *nome, *explica, *dica;
		} itens[] = {
			{"contato", "seu e-mail, para a SEC",
			 "A SEC (o regulador dos Estados Unidos) pede que cada programa que consulta os seus dados diga quem é o responsável. "
			 "O e-mail vai só para a SEC, no pedido dos balanços das empresas americanas (BAL).", "voce@exemplo.com"},
			{"chave_finnhub", "chave da Finnhub",
			 "Para as ações dos EUA em tempo real (VIVO). A conta e a chave são gratuitas: crie em finnhub.io e copie a chave para cá.", ""},
			{"alpaca_id", "Alpaca, conta de papel: chave",
			 "Para mandar ordens à corretora Alpaca (ORDEM), ações dos EUA. Crie a conta em alpaca.markets; a de papel é de treino.", ""},
			{"alpaca_segredo", "Alpaca, conta de papel: segredo", "", ""},
			{"alpaca_real_id", "Alpaca, conta REAL: chave", "Só se quiser mandar ordens com dinheiro de verdade.", ""},
			{"alpaca_real_segredo", "Alpaca, conta REAL: segredo", "", ""},
			{"chave_ais", "chave do aisstream.io",
			 "Para os navios do mundo todo (NAVIOS). A conta e a chave são gratuitas: crie em aisstream.io e copie a chave para cá.", ""},
			{"chat_servidor", "conversa: servidor de IRC", "", "irc.libera.chat"},
			{"chat_porta", "conversa: porta", "6697 é a porta com criptografia; 6667, a sem.", "6697"},
			{"chat_canal", "conversa: canal", "", "#lumebsd"},
			{"chat_apelido", "conversa: seu apelido", "Sem acentos nem espaços. Vazio, o programa inventa um.", ""},
		};
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(16);
		g->setVerticalSpacing(6);
		const QJsonObject c = lerConfig();
		int l = 0;
		for (const auto &i : itens) {
			auto *e = campo(c.value(QLatin1String(i.chave)).toString(), QString::fromUtf8(i.dica));
			e->setMinimumWidth(360);
			if (QString::fromLatin1(i.chave).startsWith("chave") || QString::fromLatin1(i.chave).contains("segredo"))
				e->setEchoMode(QLineEdit::PasswordEchoOnEdit);
			campos_[QLatin1String(i.chave)] = e;
			g->addWidget(rotulo(QString::fromUtf8(i.nome)), l, 0);
			g->addWidget(e, l, 1);
			l++;
			if (*i.explica) {
				g->addWidget(rotulo(QString::fromUtf8(i.explica), "detalhe"), l, 1);
				l++;
			}
			connect(e, &QLineEdit::editingFinished, this, [this] { gravar(); });
		}
		g->setColumnMinimumWidth(0, 230);
		g->setColumnStretch(2, 1);
		v->addLayout(g);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		v->addWidget(rotulo(QStringLiteral("Fica em %1, que só você lê. Nada disto sai do computador a não ser para o serviço de cada "
		    "linha.").arg(QDir::toNativeSeparators(arquivoConfig())), "detalhe"));
		v->addStretch(1);
	}

private:
	void gravar()
	{
		QJsonObject o = lerConfig();
		for (auto it = campos_.begin(); it != campos_.end(); ++it)
			o[it.key()] = it.value()->text().trimmed();
		gravarConfig(o);
		aviso_->setText(QTime::currentTime().toString("'Guardado às' HH:mm:ss."));
	}

	QMap<QString, QLineEdit *> campos_;
	QLabel *aviso_;
};

// --- VIVO: as cotações em tempo real ------------------------------------------------------

// Cada negócio, assim que acontece: as criptomoedas pela Coinbase (aberto,
// sem chave) e as ações e os fundos dos Estados Unidos pela Finnhub (com a
// chave gratuita da pessoa). A B3 não tem fonte gratuita em tempo real: as
// ações daqui seguem com o atraso de sempre, atualizadas a cada minuto.
class TelaAoVivo : public Tela {
public:
	TelaAoVivo() : Tela("VIVO", "Ao vivo", "Cada negócio assim que acontece: cripto pela Coinbase, ações dos EUA pela Finnhub (com a sua chave).")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		ativos_ = campo(config("vivo", "BTC ETH SOL AAPL NVDA MSFT PETR4 VALE3"), "ativos");
		ativos_->setMinimumWidth(460);
		auto *ir = botao("Acompanhar", "primario");
		h->addWidget(ativos_);
		h->addWidget(ir);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		t_ = tabela({"ativo", "último", "desde a abertura", "último negócio", "negócios vistos", "fonte", "agora"}, 360);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		g_ = new Grafico("Clique num ativo para ver os negócios chegando.");
		g_->setMinimumHeight(300);
		v->addWidget(g_);
		v->addWidget(rotulo("Coinbase: dados públicos, sem chave. Finnhub: crie a conta gratuita em finnhub.io e ponha a chave na tela "
		    "CONFIG; o plano gratuito tem negócios das bolsas americanas em tempo real. Ações da B3: com atraso (em geral 15 "
		    "minutos), porque as cotações da B3 em tempo real são vendidas por ela e pelos distribuidores autorizados.", "detalhe"));
		v->addStretch(1);
		coin_ = new FioWs(this);
		finn_ = new FioWs(this);
		coin_->abriu = [this] {
			QJsonArray p;
			for (const QString &s : lista_)
				if (rota(s) == 'c')
					p << produto(s);
			coin_->enviar(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "subscribe"}, {"product_ids", p},
			    {"channels", QJsonArray{"ticker"}}}).toJson(QJsonDocument::Compact)));
		};
		coin_->mensagem = [this](const QByteArray &m) {
			const QJsonObject o = QJsonDocument::fromJson(m).object();
			if (o.value("type").toString() != "ticker")
				return;
			const QString id = o.value("product_id").toString();
			for (const QString &s : lista_)
				if (rota(s) == 'c' && produto(s) == id)
					negocio(s, o.value("price").toString().toDouble(), QDateTime::fromString(o.value("time").toString(), Qt::ISODateWithMs),
					    o.value("open_24h").toString().toDouble());
		};
		finn_->abriu = [this] {
			for (const QString &s : lista_)
				if (rota(s) == 'f')
					finn_->enviar(QStringLiteral("{\"type\":\"subscribe\",\"symbol\":\"%1\"}").arg(s));
		};
		finn_->mensagem = [this](const QByteArray &m) {
			const QJsonObject o = QJsonDocument::fromJson(m).object();
			if (o.value("type").toString() != "trade")
				return;
			for (const QJsonValue &v : o.value("data").toArray()) {
				const QJsonObject x = v.toObject();
				negocio(x.value("s").toString(), x.value("p").toDouble(), QDateTime::fromMSecsSinceEpoch(qint64(x.value("t").toDouble())), NAO);
			}
		};
		auto caiu = [this](const QString &quem) {
			return [this, quem](const QString &motivo) {
				aviso_->setText(QStringLiteral("A conexão com a %1 caiu (%2); tentando de novo em 10 segundos.").arg(quem, motivo));
				QTimer::singleShot(10000, this, [this] {
					if (isVisible())
						conectar();
				});
			};
		};
		coin_->fechou = caiu("Coinbase");
		finn_->fechou = caiu("Finnhub");
		redesenho_.setInterval(500);
		connect(&redesenho_, &QTimer::timeout, this, [this] { pintar(); });
		atrasado_.setInterval(60000);
		connect(&atrasado_, &QTimer::timeout, this, [this] { buscarAtrasados(); });
		connect(ir, &QPushButton::clicked, this, [this] { acompanhar(); });
		connect(ativos_, &QLineEdit::returnPressed, this, [this] { acompanhar(); });
		connect(t_, &QTableWidget::itemSelectionChanged, this, [this] { pintar(); });
	}
	void abrir(const QStringList &a) override
	{
		if (!a.isEmpty())
			ativos_->setText(a.join(" "));
		acompanhar();
	}

protected:
	void hideEvent(QHideEvent *) override
	{
		// Fora da tela, nada de conexão aberta à toa.
		coin_->fechar();
		finn_->fechar();
		redesenho_.stop();
		atrasado_.stop();
	}

private:
	struct Vivo {
		double ultimo = NAO, referencia = NAO;
		QDateTime quando;
		int negocios = 0;
		QVector<QPointF> serie;	// segundos desde que começou, preço
		bool mudou = true;
	};

	// c: Coinbase, f: Finnhub, a: atrasado (Yahoo).
	char rota(const QString &s) const
	{
		static const QSet<QString> cripto{"BTC", "ETH", "SOL", "XRP", "ADA", "DOGE", "LTC", "DOT", "AVAX", "LINK", "BCH", "USDT", "USDC"};
		if (cripto.contains(s) || s.endsWith("-USD") || s.endsWith("-BRL"))
			return 'c';
		const QString y = simboloDe(s);
		if (y.endsWith(".SA") || y.contains('=') || y.startsWith('^') || y.contains('.') || config("chave_finnhub").isEmpty())
			return 'a';
		return 'f';
	}
	QString produto(const QString &s) const { return s.contains('-') ? s : s + "-USD"; }

	void acompanhar()
	{
		lista_.clear();
		for (const QString &x : ativos_->text().toUpper().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts))
			if (!lista_.contains(x))
				lista_ << x;
		QJsonObject c = lerConfig();
		c["vivo"] = lista_.join(" ");
		gravarConfig(c);
		vivos_.clear();
		inicio_ = QDateTime::currentDateTime();
		t_->setRowCount(lista_.size());
		for (int k = 0; k < lista_.size(); k++) {
			t_->setItem(k, 0, celula(lista_[k]));
			for (int c2 = 1; c2 < 7; c2++)
				t_->setItem(k, c2, celula(QString()));
			const char r = rota(lista_[k]);
			t_->item(k, 5)->setText(r == 'c' ? "Coinbase" : r == 'f' ? "Finnhub" : "com atraso");
		}
		conectar();
		if (!lista_.isEmpty())
			t_->selectRow(0);
		redesenho_.start();
		atrasado_.start();
		buscarAtrasados();
		const bool semChave = config("chave_finnhub").isEmpty();
		aviso_->setText(semChave ? QStringLiteral("Sem a chave da Finnhub na tela CONFIG, as ações dos EUA vão com atraso.")
					 : QStringLiteral("Conectando…"));
	}
	void conectar()
	{
		bool c = false, f = false;
		for (const QString &s : lista_) {
			c = c || rota(s) == 'c';
			f = f || rota(s) == 'f';
		}
		coin_->fechar();
		finn_->fechar();
		if (c)
			coin_->abrir(QUrl("wss://ws-feed.exchange.coinbase.com"));
		if (f)
			finn_->abrir(QUrl("wss://ws.finnhub.io?token=" + config("chave_finnhub")));
	}
	// O fechamento anterior de todos (a referência de "desde a abertura") e o
	// preço dos que não têm tempo real.
	void buscarAtrasados()
	{
		QStringList s;
		for (const QString &x : lista_)
			if (rota(x) != 'c')
				s << simboloDe(x);
		buscarVarios(s, "5d", "1d", this, [this](const QMap<QString, Cotacao> &q, const QString &) {
			for (const QString &x : lista_) {
				const Cotacao c = q.value(simboloDe(x));
				if (!c.ok)
					continue;
				Vivo &v = vivos_[x];
				v.referencia = c.anterior;
				if (rota(x) == 'a')
					negocio(x, c.preco, QDateTime::fromSecsSinceEpoch(c.hora), NAO);
			}
		});
	}
	void negocio(const QString &s, double preco, const QDateTime &quando, double abertura)
	{
		if (!(preco > 0))
			return;
		Vivo &v = vivos_[s];
		v.ultimo = preco;
		v.quando = quando.isValid() ? quando : QDateTime::currentDateTime();
		v.negocios++;
		if (std::isfinite(abertura))
			v.referencia = abertura;
		v.serie << QPointF(inicio_.msecsTo(QDateTime::currentDateTime()) / 1000.0, preco);
		if (v.serie.size() > 3000)
			v.serie.remove(0, 1000);
		v.mudou = true;
		if (!aviso_->text().startsWith("Ao vivo"))
			aviso_->setText("Ao vivo.");
	}
	void pintar()
	{
		for (int k = 0; k < lista_.size(); k++) {
			Vivo &v = vivos_[lista_[k]];
			const qint64 seg = v.quando.isValid() ? v.quando.secsTo(QDateTime::currentDateTime()) : -1;
			t_->item(k, 6)->setText(seg < 0 ? QString() : seg < 60 ? QStringLiteral("há %1 s").arg(seg)
			    : seg < 3600 ? QStringLiteral("há %1 min").arg(seg / 60) : QStringLiteral("há %1 h").arg(seg / 3600));
			if (!v.mudou)
				continue;
			v.mudou = false;
			t_->item(k, 1)->setText(num(v.ultimo, casasDe(v.ultimo)));
			t_->item(k, 2)->setText(variacao(v.ultimo / v.referencia - 1));
			t_->item(k, 3)->setText(v.quando.toLocalTime().toString("HH:mm:ss"));
			t_->item(k, 4)->setText(QString::number(v.negocios));
		}
		const int l = t_->currentRow();
		if (l < 0 || l >= lista_.size())
			return;
		const Vivo &v = vivos_[lista_[l]];
		Serie s{lista_[l], v.serie, Serie::Linha, false, false};
		g_->formatoX = [this](double x) { return inicio_.addMSecs(qint64(x * 1000)).toString("HH:mm:ss"); };
		if (v.serie.size() >= 2)
			g_->definir("", lista_[l], {s});
		else
			g_->limpar("Esperando os negócios de " + lista_[l] + "…");
	}

	QLineEdit *ativos_;
	QLabel *aviso_;
	QTableWidget *t_;
	Grafico *g_;
	FioWs *coin_, *finn_;
	QStringList lista_;
	QMap<QString, Vivo> vivos_;
	QDateTime inicio_;
	QTimer redesenho_, atrasado_;
};

// SPDX-License-Identifier: BSD-2-Clause
//
// lume-mercados - "Mercados do LumeBSD": um terminal de mercados para quem
// estuda, ensina e trabalha com economia e finanças. Uma linha de comando
// em cima (o ativo e a função: "PETR4 G", "VALE3 REL", "MUNDO") e uma tela
// por função:
//
//   MUNDO    bolsas, moedas, matérias-primas, juros e cripto do mundo
//   G        o gráfico de um ativo, ou vários comparados (base 100)
//   DES      a ficha do ativo, os dividendos e o cadastro na CVM
//   BAL      os balanços das companhias da B3 (CVM), ano a ano
//   ECO      os indicadores do Banco Central e as expectativas (Focus)
//   CAMBIO   as moedas, umas nas outras
//   CORR     correlações, e a regressão e o beta de um par
//   OPOSTO   o ativo que anda ao contrário (proteção)
//   REL      de quem a empresa compra, para quem vende, com quem concorre,
//            e o impacto estimado de um movimento dela nos outros
//   VOO      os aviões no ar, no mapa (ADS-B)
//   NAVIOS   os navios, no mapa (AIS)
//   NOT      as manchetes
//   CART     a carteira e a lista de acompanhamento
//   TESOURO  os títulos públicos e a curva de juros
//   TIT      a calculadora de títulos (preço, taxa, duração, convexidade)
//
// Escrito do zero, com dados públicos: cotações com atraso do Yahoo Finance,
// CVM, Banco Central, Tesouro Nacional, adsb.lol, OpenSky, digitraffic.fi e
// Google Notícias (cada tela diz a fonte). O que veio fica guardado em
// ~/.cache/lume-mercados, para abrir sem internet com os últimos dados.
//
// Na linha de comando, sem janela:
//
//   lume-mercados --cotacao ATIVO...
//   lume-mercados --correlacao A B [6mo|1y|2y|5y]
//   lume-mercados --relacoes ATIVO
//   lume-mercados --titulo FACE CUPOM% PAGAMENTOS-POR-ANO ANOS TAXA%
//   lume-mercados --chave URL       (o nome do arquivo guardado de uma URL)
//
// Sem moc: só sinais do próprio Qt ligados a lambdas. Os arquivos .h ao
// lado são partes deste programa, não uma biblioteca.

#include "telas6.h"

struct Funcao {
	const char *codigo, *outros, *nome, *exemplo;
	std::function<Tela *()> criar;
};

static const QVector<Funcao> &
funcoes()
{
	static const QVector<Funcao> f = {
		{"MUNDO", "WEI MERCADOS", "Os mercados do mundo", "MUNDO", [] { return new TelaMundo; }},
		{"G", "GP GRAFICO", "Gráfico", "PETR4 G · PETR4 VALE3 IBOV G", [] { return new TelaGrafico; }},
		{"DES", "FICHA", "Ficha do ativo", "ITUB4 DES", [] { return new TelaDescricao; }},
		{"BAL", "FA BALANCO BALANCOS", "Balanços", "WEGE3 BAL", [] { return new TelaBalancos; }},
		{"ECO", "ECST ECONOMIA FOCUS", "Economia", "ECO", [] { return new TelaEconomia; }},
		{"CAMBIO", "FXC MOEDAS", "Câmbio", "CAMBIO", [] { return new TelaCambio; }},
		{"CORR", "BETA REG CORRELACAO", "Correlações", "PETR4 BRENT CORR", [] { return new TelaCorrelacao; }},
		{"OPOSTO", "HEDGE PROTECAO", "O ativo oposto", "PETR4 OPOSTO", [] { return new TelaOposto; }},
		{"REL", "SPLC CADEIA RELACOES", "Relações entre empresas", "VALE3 REL", [] { return new TelaRelacoes; }},
		{"VOO", "VOOS AVIAO AVIOES", "Aviões", "VOO · TAM3456 VOO", [] { return new TelaVoos; }},
		{"NAVIOS", "NAVIO AIS", "Navios", "NAVIOS", [] { return new TelaNavios; }},
		{"NOT", "N CN NOTICIAS", "Notícias", "PETROBRAS NOT", [] { return new TelaNoticias; }},
		{"CART", "PORT LISTA CARTEIRA", "Carteira", "CART · WEGE3 CART", [] { return new TelaCarteira; }},
		{"TESOURO", "TD JUROS", "Tesouro Direto", "TESOURO", [] { return new TelaTesouro; }},
		{"TIT", "YAS TITULO", "Calculadora de títulos", "TIT", [] { return new TelaTitulo; }},
		{"TRI", "EQS TRIAGEM FILTRO", "Triagem de ações", "TRI", [] { return new TelaTriagem; }},
		{"OPC", "OMON OPCOES", "Opções", "PETR4 OPC · AAPL OPC", [] { return new TelaOpcoes; }},
		{"ALERTA", "ALRT ALERTAS", "Alertas de preço", "VALE3 ALERTA", [] { return new TelaAlertas; }},
		{"ORDEM", "EMSX ORDENS SIMULADOR", "Ordens", "PETR4 ORDEM", [] { return new TelaOrdens; }},
		{"VIVO", "TEMPO AOVIVO REALTIME", "Ao vivo (tempo real)", "BTC ETH AAPL VIVO", [] { return new TelaAoVivo; }},
		{"CHAT", "IB CONVERSA", "Conversa", "CHAT", [] { return new TelaChat; }},
		{"CONFIG", "CONFIGURACAO CHAVES", "Configuração", "CONFIG", [] { return new TelaConfig; }},
	};
	return f;
}

static int
acharFuncao(const QString &t)
{
	const QString u = t.toUpper();
	for (int k = 0; k < funcoes().size(); k++)
		if (u == QLatin1String(funcoes()[k].codigo) || QString::fromLatin1(funcoes()[k].outros).split(' ').contains(u))
			return k;
	return -1;
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
	fputs("uso: lume-mercados [ATIVO...] [FUNCAO]\n"
	      "     lume-mercados --cotacao ATIVO...\n"
	      "     lume-mercados --correlacao A B [6mo|1y|2y|5y]\n"
	      "     lume-mercados --relacoes ATIVO\n"
	      "     lume-mercados --relacoes-cvm ATIVO\n"
	      "     lume-mercados --triagem [NOME-OU-CODIGO]\n"
	      "     lume-mercados --opcao compra|venda ATIVO EXERCICIO DIAS VOL% JUROS%\n"
	      "     lume-mercados --alertas\n"
	      "     lume-mercados --balanco ATIVO-DOS-EUA\n"
	      "     lume-mercados --clientes ATIVO-DOS-EUA\n"
	      "     lume-mercados --titulo FACE CUPOM% PAGAMENTOS-POR-ANO ANOS TAXA%\n"
	      "     lume-mercados --chave URL\n", stderr);
	return 2;
}

static int
linhaDeComando(QCoreApplication &app, const QStringList &a)
{
	const QString cmd = a.value(1);
	if (cmd == "--chave" && a.size() == 3) {
		dizer(chaveUrl(a[2]));
		return 0;
	}
	if (cmd == "--titulo" && a.size() == 7) {
		bool ok[5];
		const double face = lerNumero(a[2], &ok[0]), cupom = lerNumero(a[3], &ok[1]) / 100, vezes = lerNumero(a[4], &ok[2]),
			     anos = lerNumero(a[5], &ok[3]), taxa = lerNumero(a[6], &ok[4]) / 100;
		if (!ok[0] || !ok[1] || !ok[2] || !ok[3] || !ok[4] || vezes < 1 || anos <= 0)
			return uso();
		const contas::Titulo t = contas::titulo(face, cupom, int(vezes), anos, taxa);
		dizer(QStringLiteral("preço               %1").arg(num(t.preco, 2)));
		dizer(QStringLiteral("duração (Macaulay)  %1").arg(num(t.duracao, 4)));
		dizer(QStringLiteral("duração modificada  %1").arg(num(t.modificada, 4)));
		dizer(QStringLiteral("convexidade         %1").arg(num(t.convexidade, 4)));
		dizer(QStringLiteral("taxa pelo preço     %1").arg(pct(contas::taxaDoTitulo(face, cupom, int(vezes), anos, t.preco), 4)));
		return 0;
	}
	if (cmd == "--relacoes" && a.size() == 3) {
		const Relacoes r = carregarRelacoes();
		const QString s = simboloDe(a[2]);
		const QVector<Vizinho> v = vizinhos(r, s);
		if (v.isEmpty()) {
			fprintf(stderr, "lume-mercados: sem relações de %s\n", a[2].toUtf8().constData());
			return 1;
		}
		dizer(r.nome(s));
		for (const Vizinho &x : v)
			dizer(QStringLiteral("  %1 %2 %3  %4").arg(x.grupo.leftJustified(13), curto(x.simbolo).leftJustified(10),
			    r.nome(x.simbolo).leftJustified(28), x.nota));
		return 0;
	}
	if (cmd == "--fio" && a.size() == 4) {	// diagnóstico: abre um WebSocket, manda uma mensagem, imprime 3 respostas
		auto *w = new FioWs(&app);
		auto *n = new int(0);
		const QString msg = a[3];
		w->abriu = [w, msg] { w->enviar(msg); };
		w->mensagem = [&app, n](const QByteArray &m) {
			dizer(QString::fromUtf8(m.left(300)));
			if (++*n == 3)
				app.exit(0);
		};
		w->fechou = [&app](const QString &m) {
			fprintf(stderr, "fechou: %s\n", m.toUtf8().constData());
			app.exit(1);
		};
		w->abrir(QUrl(a[2]));
		QTimer::singleShot(20000, &app, [&app] { fprintf(stderr, "sem resposta\n"); app.exit(1); });
		return app.exec();
	}
	if (cmd == "--clientes" && a.size() == 3) {
		buscarCik(curto(simboloDe(a[2])), &app, [&app](qint64 cik, const QString &nome, const QString &aviso) {
			if (!cik) {
				fprintf(stderr, "%s\n", aviso.toUtf8().constData());
				app.exit(1);
				return;
			}
			buscarClientesSec(cik, &app, [&app, nome](const RelatorioAnual &r, const QString &aviso) {
				if (r.url.isEmpty()) {
					fprintf(stderr, "%s\n", aviso.toUtf8().constData());
					app.exit(1);
					return;
				}
				dizer(QStringLiteral("%1: %2 de %3, %4 trechos sobre clientes").arg(nome, r.form, r.data).arg(r.clientes.size()));
				for (const TrechoCliente &t : r.clientes) {
					QStringList p;
					for (double x : t.percentuais)
						p << num(x, x == std::floor(x) ? 0 : 1) + "%";
					dizer(QStringLiteral("  [%1] %2").arg(p.join(" "), t.texto));
				}
				app.exit(0);
			});
		});
		return app.exec();
	}
	if (cmd == "--balanco" && a.size() == 3 && !simboloDe(a[2]).endsWith(".SA")) {
		buscarCik(curto(simboloDe(a[2])), &app, [&app](qint64 cik, const QString &nome, const QString &aviso) {
			if (!cik) {
				fprintf(stderr, "%s\n", aviso.toUtf8().constData());
				app.exit(1);
				return;
			}
			buscarBalancoSec(cik, &app, [&app, nome](const QMap<int, Balanco> &anos, const QString &moeda, const QString &aviso) {
				if (anos.isEmpty()) {
					fprintf(stderr, "%s\n", aviso.isEmpty() ? "lume-mercados: sem balanços" : aviso.toUtf8().constData());
					app.exit(1);
					return;
				}
				dizer(QStringLiteral("%1 (%2, milhões)").arg(nome, moeda));
				dizer("ano      receita       lucro       ativo  patrimônio");
				for (auto it = anos.begin(); it != anos.end(); ++it)
					dizer(QStringLiteral("%1 %2 %3 %4 %5").arg(it.key()).arg(num(it->receita / 1e6, 0).rightJustified(12),
					    num(it->lucro / 1e6, 0).rightJustified(11), num(it->ativo / 1e6, 0).rightJustified(11), num(it->pl / 1e6, 0).rightJustified(11)));
				app.exit(0);
			});
		});
		return app.exec();
	}
	if (cmd == "--alertas" && a.size() == 2) {
		conferirAlertas(&app, [&app](const QStringList &avisos, const QMap<QString, Cotacao> &) {
			for (const QString &x : avisos)
				dizer(x);
			app.exit(0);
		});
		return app.exec();
	}
	if (cmd == "--opcao" && a.size() == 8) {
		bool ok[6];
		const double S = lerNumero(a[3], &ok[0]), K = lerNumero(a[4], &ok[1]), dias = lerNumero(a[5], &ok[2]),
			     vol = lerNumero(a[6], &ok[3]) / 100, juros = lerNumero(a[7], &ok[4]) / 100;
		const bool compra = a[2] == "compra";
		if (!ok[0] || !ok[1] || !ok[2] || !ok[3] || !ok[4] || (!compra && a[2] != "venda"))
			return uso();
		const double r = std::log(1 + juros);
		const contas::Opcao o = contas::blackScholes(S, K, dias / 365, r, vol, compra);
		dizer(QStringLiteral("preço  %1").arg(num(o.preco, 4)));
		dizer(QStringLiteral("delta  %1").arg(num(o.delta, 4)));
		dizer(QStringLiteral("gama   %1").arg(num(o.gama, 4)));
		dizer(QStringLiteral("vega   %1").arg(num(o.vega, 4)));
		dizer(QStringLiteral("teta   %1").arg(num(o.teta, 4)));
		dizer(QStringLiteral("volatilidade implícita de volta  %1").arg(pct(contas::volImplicita(o.preco, S, K, dias / 365, r, compra), 2)));
		return 0;
	}
	if (cmd == "--triagem" && a.size() <= 3) {
		const QString filtro = a.value(2);
		buscarCodigos(&app, [&app, filtro](const Codigos &c, const QString &aviso) {
			if (c.porCnpj.isEmpty()) {
				fprintf(stderr, "%s\n", aviso.toUtf8().constData());
				app.exit(1);
				return;
			}
			const int ano = QDate::currentDate().year() - 1;
			buscar(urlDfp(ano), 7 * 86400, &app, [&app, filtro, c, ano](const QByteArray &d, const QString &aviso) {
				if (d.isEmpty()) {
					fprintf(stderr, "%s\n", aviso.toUtf8().constData());
					app.exit(1);
					return;
				}
				QStringList m;
				for (const char *x : {"BPA_con", "BPP_con", "DRE_con", "BPA_ind", "BPP_ind", "DRE_ind", "composicao_capital"})
					m << QStringLiteral("dfp_cia_aberta_%1_%2.csv").arg(QLatin1String(x)).arg(ano);
				extrairZip(arquivoDe(urlDfp(ano)), m, &app, [&app, filtro, c](const QByteArray &csv) {
					buscarPregao(&app, [&app, filtro, c, csv](const PregaoB3 &p, const QString &aviso) {
						if (p.papeis.isEmpty()) {
							fprintf(stderr, "%s\n", aviso.toUtf8().constData());
							app.exit(1);
							return;
						}
						TelaTriagem::capitalFre(&app, [&app, filtro, c, csv, p](const QMap<QString, QPair<double, double>> &fre) {
						QVector<LinhaTriagem> ls = TelaTriagem::montar(c, csv, p, fre);
						std::sort(ls.begin(), ls.end(), [](const LinhaTriagem &x, const LinhaTriagem &y) {
							return (std::isfinite(x.valor) ? x.valor : -1) > (std::isfinite(y.valor) ? y.valor : -1);
						});
						dizer(QStringLiteral("%1 companhias; pregão de %2").arg(ls.size()).arg(p.data.toString("dd/MM/yyyy")));
						dizer("código  preço       valor de mercado      P/L    P/VP   ROE      margem   companhia");
						for (const LinhaTriagem &l : ls)
							if (filtro.isEmpty() || l.codigo == filtro.toUpper() || l.nome.contains(filtro, Qt::CaseInsensitive))
								dizer(QStringLiteral("%1 %2 %3 %4 %5 %6 %7  %8").arg(l.codigo.leftJustified(7), num(l.preco).rightJustified(8),
								    dinheiroGrande(l.valor).rightJustified(18), num(l.pl, 1).rightJustified(8), num(l.pvp, 2).rightJustified(6),
								    pct(l.roe, 1).rightJustified(8), pct(l.margem, 1).rightJustified(8), l.nome.left(40)));
						app.exit(0);
						});
					});
				});
			});
		});
		return app.exec();
	}
	if (cmd == "--relacoes-cvm" && a.size() == 3) {
		buscarRelacoesCvm(curto(simboloDe(a[2])), &app, [&app](const RelacoesCvm &r, const QString &aviso) {
			if (!aviso.isEmpty())
				fprintf(stderr, "%s\n", aviso.toUtf8().constData());
			if (r.cnpj.isEmpty()) {
				app.exit(1);
				return;
			}
			dizer(QStringLiteral("%1 (formulário de %2)").arg(r.nome, r.data.left(4)));
			dizer("Contratos com partes relacionadas:");
			for (const LigacaoCvm &l : r.partes)
				dizer(QStringLiteral("  %1 %2 %3 contratos  %4  saldo %5").arg(l.nome.left(44).leftJustified(44), l.codigo.leftJustified(7))
				    .arg(l.contratos, 3).arg(dinheiroGrande(l.montante).rightJustified(14), dinheiroGrande(l.saldo)));
			dizer("Acionistas:");
			for (const LigacaoCvm &l : r.acionistas)
				dizer(QStringLiteral("  %1 %2 %3%%4").arg(l.nome.left(44).leftJustified(44), l.codigo.leftJustified(7), num(l.pct, 2).rightJustified(7),
				    l.controlador ? QStringLiteral("  controlador") : QString()));
			dizer("Sociedades em que participa:");
			for (const LigacaoCvm &l : r.sociedades)
				dizer(QStringLiteral("  %1 %2 %3%").arg(l.nome.left(44).leftJustified(44), l.codigo.leftJustified(7), num(l.pct, 2).rightJustified(7)));
			app.exit(0);
		});
		return app.exec();
	}
	if (cmd == "--cotacao" && a.size() >= 3) {
		QStringList s;
		for (const QString &x : a.mid(2))
			s << simboloDe(x);
		buscarVarios(s, "5d", "1d", &app, [&app, s](const QMap<QString, Cotacao> &q, const QString &aviso) {
			if (!aviso.isEmpty())
				fprintf(stderr, "%s\n", aviso.toUtf8().constData());
			int achados = 0;
			for (const QString &x : s) {
				const Cotacao c = q.value(x);
				if (!c.ok) {
					dizer(QStringLiteral("%1 não achei").arg(curto(x).leftJustified(10)));
					continue;
				}
				achados++;
				dizer(QStringLiteral("%1 %2 %3  %4  %5").arg(curto(x).leftJustified(10), num(c.preco, casasDe(c.preco)).rightJustified(12),
				    c.moeda, variacao(c.dia()).rightJustified(9), c.titulo()));
			}
			app.exit(achados ? 0 : 1);
		});
		return app.exec();
	}
	if (cmd == "--correlacao" && (a.size() == 4 || a.size() == 5)) {
		const QString x = simboloDe(a[2]), y = simboloDe(a[3]);
		buscarVarios({x, y}, a.value(4, "1y"), "1d", &app, [&app, x, y](const QMap<QString, Cotacao> &q, const QString &aviso) {
			if (!q.contains(x) || !q.contains(y)) {
				fprintf(stderr, "%s\n", (aviso.isEmpty() ? QStringLiteral("lume-mercados: não achei os dois ativos") : aviso).toUtf8().constData());
				app.exit(1);
				return;
			}
			const contas::Relacao r = contas::relacionar(q[x], q[y]);
			dizer(QStringLiteral("%1 explicado por %2, %3 dias").arg(curto(x), curto(y)).arg(r.n));
			dizer(QStringLiteral("  correlação  %1").arg(num(r.correlacao, 4)));
			dizer(QStringLiteral("  beta        %1").arg(num(r.beta, 4)));
			dizer(QStringLiteral("  R²          %1").arg(num(r.r2, 4)));
			app.exit(0);
		});
		return app.exec();
	}
	return uso();
}

// --- A janela ---------------------------------------------------------------

static const char *FOLHA_MERCADOS = R"QSS(
QLineEdit#comando {
	font-family: "IBM Plex Mono", monospace; font-size: 16px; padding: 9px 12px;
	border: 1px solid palette(window-text); background: palette(base); }
QLineEdit#comando:focus { border: 2px solid palette(window-text); padding: 8px 11px; }
QLabel#numero { font-size: 30px; font-family: "IBM Plex Mono", monospace; }
)QSS";

class Janela : public QWidget {
public:
	Janela()
	{
		setObjectName("raiz");
		setWindowTitle("Mercados do LumeBSD");
		setWindowIcon(QIcon("/usr/share/lume/app/lume-mercados.svg"));
		setAttribute(Qt::WA_DeleteOnClose);
		resize(1320, 880);
		auto *v = new QVBoxLayout(this);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);

		auto *topo = new QHBoxLayout;
		topo->setContentsMargins(28, 16, 28, 14);
		topo->setSpacing(22);
		auto *marca = new QVBoxLayout;
		marca->setSpacing(0);
		marca->addWidget(rotulo("Mercados", "marca"));
		marca->addWidget(rotulo("do LumeBSD", "sub"));
		topo->addLayout(marca);
		comando_ = new QLineEdit;
		comando_->setObjectName("comando");
		comando_->setPlaceholderText("o ativo e a função, e Enter:   PETR4 G   ·   VALE3 REL   ·   PETR4 OPOSTO   ·   MUNDO   ·   AJUDA");
		topo->addWidget(comando_, 1);
		auto *nova = botao("Nova janela");
		topo->addWidget(nova);
		v->addLayout(topo);
		v->addWidget(fio());

		auto *corpo = new QHBoxLayout;
		corpo->setContentsMargins(0, 0, 0, 0);
		corpo->setSpacing(0);
		indice_ = new QListWidget;
		indice_->setObjectName("indice");
		indice_->setFixedWidth(270);
		indice_->setFocusPolicy(Qt::NoFocus);
		paginas_ = new QStackedWidget;
		for (const Funcao &f : funcoes()) {
			indice_->addItem(QStringLiteral("%1 %2").arg(QString::fromLatin1(f.codigo).leftJustified(8), QString::fromUtf8(f.nome)));
			Tela *t = f.criar();
			t->comando = [this](const QString &c) { executar(c); };
			telas_ << t;
			paginas_->addWidget(t);
		}
		indice_->addItem("AJUDA    Como usar");
		paginas_->addWidget(ajuda());
		auto *esq = new QVBoxLayout;
		esq->setContentsMargins(0, 16, 0, 14);
		auto *cap = rotulo("FUNÇÕES");
		cap->setContentsMargins(18, 0, 16, 0);
		esq->addWidget(cap);
		esq->addSpacing(6);
		esq->addWidget(indice_, 1);
		auto *nota = rotulo("Dados públicos, com atraso. Para estudar e acompanhar, não para operar no segundo.", "detalhe");
		nota->setContentsMargins(18, 0, 16, 0);
		esq->addWidget(nota);
		corpo->addLayout(esq);
		corpo->addWidget(fio(true));
		corpo->addWidget(paginas_, 1);
		v->addLayout(corpo, 1);

		connect(indice_, &QListWidget::currentRowChanged, this, [this](int k) {
			paginas_->setCurrentIndex(k);
			if (k >= 0 && k < telas_.size() && !mudo_)
				telas_[k]->abrir({});
		});
		connect(comando_, &QLineEdit::returnPressed, this, [this] {
			executar(comando_->text());
			comando_->selectAll();
		});
		connect(nova, &QPushButton::clicked, this, [] { (new Janela)->show(); });
		// Os alertas: a cada minuto, enquanto o programa está aberto, com o
		// aviso na área de notificação (só a primeira janela confere).
		static bool conferindo = false;
		if (!conferindo) {
			conferindo = true;
			auto *bandeja = new QSystemTrayIcon(windowIcon(), qApp);
			bandeja->setToolTip("Mercados do LumeBSD");
			if (QSystemTrayIcon::isSystemTrayAvailable())
				bandeja->show();
			auto *relogio = new QTimer(qApp);
			QObject::connect(relogio, &QTimer::timeout, qApp, [bandeja] {
				conferirAlertas(qApp, [bandeja](const QStringList &avisos, const QMap<QString, Cotacao> &q) {
					for (QWidget *w : QApplication::topLevelWidgets())
						if (auto *j = dynamic_cast<Janela *>(w))
							j->alertas(q);
					if (avisos.isEmpty())
						return;
					if (bandeja->isVisible())
						bandeja->showMessage("Alerta de preço", avisos.join("\n"), QSystemTrayIcon::Information, 30000);
					else
						QApplication::alert(QApplication::activeWindow() ? QApplication::activeWindow() : QApplication::topLevelWidgets().value(0));
				});
			});
			relogio->start(60000);
		}
		comando_->setFocus();
	}

	void executar(const QString &texto)
	{
		QStringList ativos;
		int f = -1;
		bool ajuda = false;
		for (const QString &t : texto.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
			const int k = acharFuncao(t);
			if (k >= 0)
				f = k;
			else if (t.toUpper() == "AJUDA" || t == "?" || t.toUpper() == "HELP")
				ajuda = true;
			else
				ativos << t;
		}
		if (ajuda && f < 0) {
			indice_->setCurrentRow(telas_.size());
			return;
		}
		if (f < 0) {
			if (ativos.isEmpty())
				return;
			f = 1;	// só o ativo: o gráfico
		}
		mudo_ = true;
		indice_->setCurrentRow(f);
		mudo_ = false;
		telas_[f]->abrir(ativos);
		comando_->setText(texto.toUpper());
	}

	void alertas(const QMap<QString, Cotacao> &q)
	{
		for (Tela *t : telas_)
			if (auto *a = dynamic_cast<TelaAlertas *>(t))
				a->mostrar(q);
	}

private:
	QWidget *ajuda()
	{
		auto *p = new Pagina("AJUDA", "Como usar", "Escreva o ativo e a função na linha de cima e aperte Enter. Só o ativo abre o gráfico.");
		auto *t = tabela({"função", "também", "o que faz", "exemplo"}, 470);
		t->verticalHeader()->setVisible(false);
		t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		for (const Funcao &f : funcoes()) {
			const int l = t->rowCount();
			t->insertRow(l);
			const QString c[] = {QLatin1String(f.codigo), QString::fromLatin1(f.outros).split(' ').mid(0, 2).join(", "),
					     QString::fromUtf8(f.nome), QString::fromUtf8(f.exemplo)};
			for (int k = 0; k < 4; k++) {
				auto *i = celula(c[k]);
				i->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				t->setItem(l, k, i);
			}
		}
		p->v->addWidget(t);
		p->parte("Os ativos");
		p->v->addWidget(rotulo("Ações da B3 pelo código (PETR4, VALE3, BOVA11); dos Estados Unidos, pelo código de lá (AAPL, NVDA). "
		    "Moedas aos pares (USDBRL, EURUSD). Apelidos: IBOV, SPX, NASDAQ, DOLAR, EURO, OURO, BRENT, SOJA, CAFE, BTC, VIX, US10. "
		    "Outras bolsas, com o sufixo do Yahoo Finance: 7203.T (Tóquio), AIR.PA (Paris), 005930.KS (Seul).", "texto"));
		p->parte("O que este programa é");
		p->v->addWidget(rotulo("Um terminal de mercados feito do zero para o LumeBSD, com dados públicos e gratuitos: as cotações têm "
		    "atraso (em geral 15 minutos) e cada tela diz de onde vieram os números. Não substitui um serviço profissional pago "
		    "para quem opera em tempo real, e nada aqui é recomendação de investimento. “Nova janela” abre outra, para ver duas "
		    "funções lado a lado.", "texto"));
		p->v->addStretch(1);
		return p;
	}

	QLineEdit *comando_;
	QListWidget *indice_;
	QStackedWidget *paginas_;
	QVector<Tela *> telas_;
	bool mudo_ = false;
};

// O que ficou guardado há mais de oito dias não serve mais.
static void
limparGuardados()
{
	QDir d(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
	const QDateTime limite = QDateTime::currentDateTime().addDays(-8);
	for (const QFileInfo &f : d.entryInfoList(QDir::Files))
		if (f.lastModified() < limite && f.fileName().size() == 32)
			QFile::remove(f.filePath());
}

int
main(int argc, char *argv[])
{
	if (argc > 1 && QByteArray(argv[1]).startsWith("--")) {
		QCoreApplication app(argc, argv);
		app.setApplicationName("lume-mercados");
		return linhaDeComando(app, app.arguments());
	}

	QApplication app(argc, argv);
	QTranslator qt;
	if (qt.load(QLocale(), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qt);
	app.setApplicationName("lume-mercados");
	app.setApplicationDisplayName("Mercados do LumeBSD");
	app.setDesktopFileName("lume-mercados");
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	    [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });
	QLocale::setDefault(br());
	limparGuardados();

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
	app.setStyleSheet((noite ? estiloNoite() : QString::fromUtf8(FOLHA)) + QString::fromUtf8(FOLHA_MERCADOS));

	auto *j = new Janela;
	j->show();
	const QStringList a = app.arguments().mid(1);
	j->executar(a.isEmpty() ? QStringLiteral("MUNDO") : a.join(" "));
	return app.exec();
}

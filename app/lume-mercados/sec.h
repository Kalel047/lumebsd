// SPDX-License-Identifier: BSD-2-Clause
//
// sec.h - as companhias dos Estados Unidos pela SEC (EDGAR): os balanços
// anuais (as "company facts" em XBRL) e, do relatório anual (10-K, ou 20-F
// das estrangeiras), os trechos em que a própria companhia diz quanto da
// receita vem de cada cliente. A SEC pede que quem consulta se identifique:
// o contato vem da tela CONFIG (base.h põe no pedido).

#pragma once

// Incluído no meio de telas1.h, depois do Balanco e antes da tela BAL.

static bool
temContatoSec()
{
	return config("contato").contains('@');
}

static const char *SEM_CONTATO_SEC =
    "A SEC (o regulador dos Estados Unidos) só responde a quem diz quem é: ponha o seu e-mail na tela CONFIG e tente de novo.";

// O código (AAPL) -> o número da companhia na SEC (CIK) e o nome.
static void
buscarCik(const QString &codigo, QObject *dono, const std::function<void(qint64, const QString &, const QString &)> &f)
{
	if (!temContatoSec() && !getenv("LUME_MERCADOS_DADOS")) {
		QTimer::singleShot(0, dono, [f] { f(0, QString(), QString::fromUtf8(SEM_CONTATO_SEC)); });
		return;
	}
	buscar("https://www.sec.gov/files/company_tickers.json", 7 * 86400, dono, [=](const QByteArray &d, const QString &aviso) {
		const QString alvo = codigo.toUpper().replace('.', '-');
		const QJsonObject o = QJsonDocument::fromJson(d).object();
		for (auto it = o.begin(); it != o.end(); ++it) {
			const QJsonObject x = it.value().toObject();
			if (x.value("ticker").toString().toUpper() == alvo) {
				f(qint64(x.value("cik_str").toDouble()), x.value("title").toString(), aviso);
				return;
			}
		}
		f(0, QString(), aviso.isEmpty() ? QStringLiteral("A SEC não tem o código %1 (só as companhias registradas nos EUA).").arg(codigo) : aviso);
	});
}

static QString
cik10(qint64 cik)
{
	return QStringLiteral("%1").arg(cik, 10, 10, QLatin1Char('0'));
}

// Os balanços anuais, das "company facts": cada conta é uma lista de valores
// declarados, cada um com o período, o formulário e o ano fiscal. Ficam os
// do relatório anual que cobrem um ano inteiro (ou a data do fim do ano, nas
// contas de saldo); quando o mesmo período aparece em vários relatórios,
// vale o declarado por último.
static QMap<int, Balanco>
lerFatosSec(const QByteArray &json, QString *moeda)
{
	const QJsonObject fatos = QJsonDocument::fromJson(json).object().value("facts").toObject();
	const QJsonObject gaap = fatos.value("us-gaap").toObject(), ifrs = fatos.value("ifrs-full").toObject();
	QMap<int, Balanco> anos;
	struct Conta {
		double Balanco::*campo;
		QStringList nomes;
		bool saldo;
	};
	const Conta contas[] = {
		{&Balanco::receita, {"Revenues", "RevenueFromContractWithCustomerExcludingAssessedTax", "SalesRevenueNet",
				     "RevenueFromContractWithCustomerIncludingAssessedTax", "Revenue", "RevenueFromContractsWithCustomers"}, false},
		{&Balanco::bruto, {"GrossProfit"}, false},
		{&Balanco::ebit, {"OperatingIncomeLoss", "ProfitLossFromOperatingActivities"}, false},
		{&Balanco::lucro, {"NetIncomeLoss", "ProfitLossAttributableToOwnersOfParent", "ProfitLoss"}, false},
		{&Balanco::operacional, {"NetCashProvidedByUsedInOperatingActivities", "CashFlowsFromUsedInOperatingActivities"}, false},
		{&Balanco::ativo, {"Assets"}, true},
		{&Balanco::pl, {"StockholdersEquity", "EquityAttributableToOwnersOfParent", "Equity"}, true},
		{&Balanco::divida, {"LongTermDebt", "LongTermDebtNoncurrent", "Borrowings", "NoncurrentBorrowings"}, true},
		{&Balanco::caixa, {"CashAndCashEquivalentsAtCarryingValue", "CashAndCashEquivalents"}, true},
	};
	for (const Conta &c : contas) {
		QMap<int, QPair<QString, double>> melhor;	// ano -> (data de entrega, valor)
		for (const QString &nome : c.nomes) {
			const QJsonObject unidades = (gaap.contains(nome) ? gaap : ifrs).value(nome).toObject().value("units").toObject();
			if (unidades.isEmpty())
				continue;
			// Em dólares, se houver; senão, a moeda que a companhia usa (20-F).
			QString u = unidades.contains("USD") ? QStringLiteral("USD") : unidades.keys().value(0);
			if (moeda && moeda->isEmpty())
				*moeda = u;
			for (const QJsonValue &v : unidades.value(u).toArray()) {
				const QJsonObject x = v.toObject();
				const QString form = x.value("form").toString();
				if (!form.startsWith("10-K") && !form.startsWith("20-F") && !form.startsWith("40-F"))
					continue;
				const QDate fim = QDate::fromString(x.value("end").toString(), Qt::ISODate);
				if (!fim.isValid())
					continue;
				if (!c.saldo) {
					const QDate ini = QDate::fromString(x.value("start").toString(), Qt::ISODate);
					if (!ini.isValid() || ini.daysTo(fim) < 340 || ini.daysTo(fim) > 380)
						continue;
				}
				// O ano é o do fim do exercício, como as companhias chamam ("fiscal 2026").
				const int ano = fim.year();
				const QString entrega = x.value("filed").toString();
				if (!melhor.contains(ano) || entrega > melhor[ano].first)
					melhor[ano] = qMakePair(entrega, x.value("val").toDouble());
			}
			// A primeira conta que tem dados vale; as outras são para quem não usa essa.
			if (!melhor.isEmpty())
				break;
		}
		for (auto it = melhor.begin(); it != melhor.end(); ++it)
			anos[it.key()].*c.campo = it.value().second;
	}
	// Só os últimos cinco anos com receita ou ativo.
	for (const int a : anos.keys())
		if (!std::isfinite(anos[a].receita) && !std::isfinite(anos[a].ativo))
			anos.remove(a);
	while (anos.size() > 5)
		anos.remove(anos.firstKey());
	return anos;
}

static void
buscarBalancoSec(qint64 cik, QObject *dono, const std::function<void(const QMap<int, Balanco> &, const QString &moeda, const QString &aviso)> &f)
{
	buscar(QStringLiteral("https://data.sec.gov/api/xbrl/companyfacts/CIK%1.json").arg(cik10(cik)), 86400, dono,
	    [f](const QByteArray &d, const QString &aviso) {
		QString moeda;
		const QMap<int, Balanco> a = lerFatosSec(d, &moeda);
		f(a, moeda, aviso);
	});
}

// --- Os clientes, do relatório anual ---

struct TrechoCliente {
	QString texto;
	QVector<double> percentuais;
};

// O texto de um documento HTML, sem as marcas e com as entidades trocadas.
static QString
textoDeHtml(QString h)
{
	static const QRegularExpression semVer(QStringLiteral("<(script|style|head)[^>]*>.*?</\\1>"),
	    QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
	static const QRegularExpression bloco(QStringLiteral("</?(p|div|br|tr|li|h[1-6]|table)[^>]*>"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression marca(QStringLiteral("<[^>]*>"));
	static const QRegularExpression ent(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
	// As quebras de linha do código não são do texto; as dos blocos são.
	h.replace('\n', ' ').replace('\r', ' ');
	h.remove(semVer);
	h.replace(bloco, "\n");
	h.remove(marca);
	QString r;
	int ultimo = 0;
	for (auto it = ent.globalMatch(h); it.hasNext();) {
		const auto m = it.next();
		r += QStringView(h).mid(ultimo, m.capturedStart() - ultimo);
		bool ok;
		const uint c = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
		r += ok ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&c), 1) : QString();
		ultimo = m.capturedEnd();
	}
	r += QStringView(h).mid(ultimo);
	r.replace("&nbsp;", " ").replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
	    .replace("&rsquo;", "'").replace("&ldquo;", "\"").replace("&rdquo;", "\"").replace("&mdash;", "—").replace("&ndash;", "–");
	r.replace(QChar(0xa0), ' ');
	return r;
}

// As frases em que a companhia fala de clientes e de parcelas da receita
// ("Customer A accounted for 12% of net revenue"). É uma leitura do texto, não
// uma conta: mostra a frase inteira, para quem lê conferir.
static QVector<TrechoCliente>
clientesDoTexto(const QString &texto)
{
	QVector<TrechoCliente> r;
	static const QRegularExpression frase(QStringLiteral("[^.!?\\n]*(?:\\d+(?:\\.\\d+)?\\s?%|percent)[^.!?\\n]*[.!?]"),
	    QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression cliente(QStringLiteral("\\b(customer|customers|client|clients|distributor|distributors|"
	    "reseller|resellers|cliente|clientes)\\b"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression receita(QStringLiteral("\\b(revenue|revenues|net sales|sales|receita|receitas|vendas)\\b"),
	    QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression numero(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s?%"));
	QSet<QString> vistas;
	for (auto it = frase.globalMatch(texto); it.hasNext();) {
		const QString f = it.next().captured(0).simplified();
		if (f.size() < 40 || f.size() > 700 || !cliente.match(f).hasMatch() || !receita.match(f).hasMatch() || vistas.contains(f))
			continue;
		vistas << f;
		TrechoCliente t;
		t.texto = f;
		for (auto n = numero.globalMatch(f); n.hasNext();) {
			const double x = n.next().captured(1).toDouble();
			if (x > 0 && x <= 100)
				t.percentuais << x;
		}
		r << t;
	}
	return r;
}

struct RelatorioAnual {
	QString form, data, url;
	QVector<TrechoCliente> clientes;
};

// O relatório anual mais recente (10-K, 20-F ou 40-F) e os trechos dos clientes.
static void
buscarClientesSec(qint64 cik, QObject *dono, const std::function<void(const RelatorioAnual &, const QString &)> &f)
{
	buscar(QStringLiteral("https://data.sec.gov/submissions/CIK%1.json").arg(cik10(cik)), 86400, dono, [=](const QByteArray &d, const QString &aviso) {
		const QJsonObject rec = QJsonDocument::fromJson(d).object().value("filings").toObject().value("recent").toObject();
		const QJsonArray forms = rec.value("form").toArray(), acc = rec.value("accessionNumber").toArray(),
				 doc = rec.value("primaryDocument").toArray(), data = rec.value("filingDate").toArray();
		RelatorioAnual r;
		for (int k = 0; k < forms.size(); k++) {
			const QString form = forms.at(k).toString();
			if (form == "10-K" || form == "20-F" || form == "40-F") {
				r.form = form;
				r.data = data.at(k).toString();
				r.url = QStringLiteral("https://www.sec.gov/Archives/edgar/data/%1/%2/%3")
				    .arg(cik).arg(acc.at(k).toString().remove('-'), doc.at(k).toString());
				break;
			}
		}
		if (r.url.isEmpty()) {
			f(r, aviso.isEmpty() ? QStringLiteral("Não achei um relatório anual (10-K, 20-F) desta companhia na SEC.") : aviso);
			return;
		}
		// O relatório não muda depois de entregue: guardado por um mês.
		buscar(r.url, 30 * 86400, dono, [=](const QByteArray &html, const QString &aviso) mutable {
			r.clientes = clientesDoTexto(textoDeHtml(QString::fromUtf8(html)));
			f(r, aviso);
		});
	});
}

// SPDX-License-Identifier: BSD-2-Clause
//
// relcvm.h - as relações de uma companhia tiradas dos documentos que ela
// mesma entrega à CVM (o Formulário de Referência, dados abertos): quem são
// os acionistas e com quanto, de que sociedades ela participa e com quanto,
// e os contratos com partes relacionadas, com os valores. Nada disto é
// escrito à mão: o programa baixa, lê e soma.

#pragma once

#include "telas1.h"

static QString
urlFre(int ano)
{
	return QStringLiteral("https://dados.cvm.gov.br/dados/CIA_ABERTA/DOC/FRE/DADOS/fre_cia_aberta_%1.zip").arg(ano);
}

// O nome de uma empresa sem o que varia de um documento para outro.
static QString
nomeChave(QString n)
{
	n = n.normalized(QString::NormalizationForm_D).toUpper();
	QString r;
	for (const QChar c : n)
		if (c.isLetterOrNumber() || c == ' ')
			r += c.category() == QChar::Mark_NonSpacing ? QChar() : c;
	r.remove(QChar());
	r = " " + r.simplified() + " ";
	r.replace(" BCO ", " BANCO ");
	// Duas passadas: uma troca pode encostar em outra ("S A DO").
	for (int k = 0; k < 2; k++)
		for (const char *x : {" SA ", " S A ", " CIA ", " COMPANHIA ", " HOLDING ", " PARTICIPACOES ", " EM RECUPERACAO JUDICIAL ",
			 " DO ", " DA ", " DE ", " DOS ", " DAS ", " E "})
			r.replace(QLatin1String(x), " ");
	return r.simplified();
}

struct Codigos {
	QMap<QString, QString> porCnpj, porNome;	// -> o código mais comum (ON, PN, unit)
	QMap<QString, QStringList> todos;		// CNPJ -> todos os códigos
	QMap<QString, QString> nome, setor;		// CNPJ -> nome, setor
};

static QString
soDigitos(const QString &s)
{
	QString r;
	for (const QChar c : s)
		if (c.isDigit())
			r += c;
	return r;
}

// Os códigos de negociação de todas as companhias (Formulário Cadastral).
static void
buscarCodigos(QObject *dono, const std::function<void(const Codigos &, const QString &)> &f,
    int ano = QDate::currentDate().year(), bool segunda = false)
{
	static Codigos guardados;
	if (!guardados.porCnpj.isEmpty()) {
		QTimer::singleShot(0, dono, [f] { f(guardados, QString()); });
		return;
	}
	buscar(urlFca(ano), 7 * 86400, dono, [=](const QByteArray &d, const QString &aviso) {
		if (d.isEmpty()) {
			if (!segunda)
				buscarCodigos(dono, f, ano - 1, true);
			else
				f(Codigos(), aviso);
			return;
		}
		extrairZip(arquivoDe(urlFca(ano)), {QStringLiteral("fca_cia_aberta_valor_mobiliario_%1.csv").arg(ano),
		    QStringLiteral("fca_cia_aberta_geral_%1.csv").arg(ano)}, dono, [=](const QByteArray &csv) {
			Codigos c;
			QStringList cab;
			for (const QStringList &p : lerCsv(QString::fromLatin1(csv))) {
				if (p.value(0) == "CNPJ_Companhia") {
					cab = p;
					continue;
				}
				auto col = [&](const char *n) { return p.value(cab.indexOf(n)); };
				const QString cnpj = soDigitos(p.value(0));
				if (!cab.contains("Codigo_Negociacao")) {
					c.nome[cnpj] = col("Nome_Empresarial");
					c.setor[cnpj] = col("Setor_Atividade");
					continue;
				}
				const QString cod = col("Codigo_Negociacao").toUpper();
				static const QRegularExpression b3(QStringLiteral("^[A-Z]{4}\\d{1,2}$"));
				if (!b3.match(cod).hasMatch() || !col("Data_Fim_Negociacao").isEmpty() || c.todos[cnpj].contains(cod))
					continue;
				c.todos[cnpj] << cod;
				// O primeiro vale, e uma ação (3, 4) vale mais que o resto.
				const QString atual = c.porCnpj.value(cnpj);
				const bool acao = cod.endsWith('3') || cod.endsWith('4');
				if (atual.isEmpty() || (acao && !(atual.endsWith('3') || atual.endsWith('4'))))
					c.porCnpj[cnpj] = cod;
				if (c.nome.value(cnpj).isEmpty())
					c.nome[cnpj] = col("Nome_Empresarial");
			}
			for (auto it = c.porCnpj.begin(); it != c.porCnpj.end(); ++it)
				c.porNome[nomeChave(c.nome.value(it.key()))] = it.value();
			if (c.porCnpj.isEmpty() && !segunda) {
				buscarCodigos(dono, f, ano - 1, true);
				return;
			}
			guardados = c;
			f(c, aviso);
		});
	});
}

// O código de quem tem este CNPJ ou este nome, se negocia na B3.
static QString
codigoDe(const Codigos &c, const QString &cnpj, const QString &nome)
{
	const QString d = soDigitos(cnpj);
	if (d.size() == 14 && d != "00000000000000" && c.porCnpj.contains(d))
		return c.porCnpj[d];
	const QString k = nomeChave(nome);
	if (c.porNome.contains(k))
		return c.porNome[k];
	// "Braskem S.A. - Controlada em conjunto": o começo do nome.
	for (auto it = c.porNome.begin(); it != c.porNome.end(); ++it)
		if (it.key().size() >= 5 && (k.startsWith(it.key() + " ") || it.key().startsWith(k + " ")))
			return it.value();
	return QString();
}

struct LigacaoCvm {
	QString nome, codigo, nota;
	double pct = NAO, montante = 0, saldo = 0;
	int contratos = 0, credor = 0, devedor = 0;
	bool controlador = false;
};

struct RelacoesCvm {
	QString cnpj, nome, data;
	QVector<LigacaoCvm> acionistas, sociedades, partes;
};

static void
buscarRelacoesCvm(const QString &codigo, QObject *dono, const std::function<void(const RelacoesCvm &, const QString &)> &f,
    int ano = QDate::currentDate().year(), bool segunda = false)
{
	buscarCodigos(dono, [=](const Codigos &c, const QString &aviso) {
		QString cnpj;
		for (auto it = c.todos.begin(); it != c.todos.end(); ++it)
			if (it.value().contains(codigo))
				cnpj = it.key();
		if (cnpj.isEmpty()) {
			f(RelacoesCvm(), aviso.isEmpty() ? QStringLiteral("A CVM não tem o código %1 no cadastro das companhias abertas.").arg(codigo) : aviso);
			return;
		}
		buscar(urlFre(ano), 7 * 86400, dono, [=](const QByteArray &d, const QString &aviso) {
			if (d.isEmpty()) {
				if (!segunda)
					buscarRelacoesCvm(codigo, dono, f, ano - 1, true);
				else
					f(RelacoesCvm(), aviso);
				return;
			}
			QStringList m;
			for (const char *x : {"posicao_acionaria", "participacao_sociedade", "transacao_parte_relacionada"})
				m << QStringLiteral("fre_cia_aberta_%1_%2.csv").arg(QLatin1String(x)).arg(ano);
			extrairZip(arquivoDe(urlFre(ano)), m, dono, [=](const QByteArray &csv) {
				RelacoesCvm r;
				r.cnpj = cnpj;
				r.nome = c.nome.value(cnpj);
				// Só a última versão do formulário desta companhia.
				const QVector<QStringList> linhas = lerCsv(QString::fromLatin1(csv));
				QStringList cab;
				int versao = -1;
				for (const QStringList &p : linhas)
					if (soDigitos(p.value(0)) == cnpj)
						versao = std::max(versao, p.value(2).toInt());
				QMap<QString, int> onde;	// a parte relacionada -> a posição em r.partes
				for (const QStringList &p : linhas) {
					if (p.value(0) == "CNPJ_Companhia") {
						cab = p;
						continue;
					}
					if (soDigitos(p.value(0)) != cnpj || p.value(2).toInt() != versao)
						continue;
					auto col = [&](const char *n) { return p.value(cab.indexOf(n)); };
					r.data = p.value(1);
					if (cab.contains("Acionista")) {
						// As linhas com "acionista relacionado" são os donos dos donos.
						if (!col("Acionista_Relacionado").isEmpty() || col("Acionista").isEmpty())
							continue;
						LigacaoCvm l;
						l.nome = col("Acionista");
						l.pct = col("Percentual_Total_Acoes_Circulacao").toDouble();
						l.controlador = col("Acionista_Controlador") == "S";
						l.codigo = codigoDe(c, col("CPF_CNPJ_Acionista"), l.nome);
						if (l.pct > 0 || l.controlador)
							r.acionistas << l;
					} else if (cab.contains("Razao_Social")) {
						LigacaoCvm l;
						l.nome = col("Razao_Social");
						l.pct = col("Participacao_Emissor").toDouble();
						l.codigo = codigoDe(c, col("CNPJ"), l.nome);
						l.nota = col("Descricao_Atividades").left(140);
						r.sociedades << l;
					} else if (cab.contains("Parte_Relacionada")) {
						const QString nome = col("Parte_Relacionada").simplified();
						const QString k = nomeChave(nome);
						if (!onde.contains(k)) {
							onde[k] = r.partes.size();
							LigacaoCvm l;
							l.nome = nome;
							l.codigo = codigoDe(c, col("Documento_Parte_Relacionada"), nome);
							l.nota = col("Relacao_Emissor").simplified().left(160);
							r.partes << l;
						}
						LigacaoCvm &l = r.partes[onde[k]];
						l.contratos++;
						l.montante += col("Montante_Envolvido").toDouble();
						l.saldo += lerNumero(col("Saldo_Existente"));
						(col("Posicao_Contratual_Emissor").startsWith("Credor") ? l.credor : l.devedor)++;
					}
				}
				auto maior = [](const LigacaoCvm &a, const LigacaoCvm &b) { return a.pct > b.pct; };
				std::sort(r.acionistas.begin(), r.acionistas.end(), maior);
				std::sort(r.sociedades.begin(), r.sociedades.end(), maior);
				std::sort(r.partes.begin(), r.partes.end(), [](const LigacaoCvm &a, const LigacaoCvm &b) { return a.montante > b.montante; });
				if (versao < 0 && !segunda) {
					buscarRelacoesCvm(codigo, dono, f, ano - 1, true);
					return;
				}
				f(r, versao < 0 ? QStringLiteral("Esta companhia não entregou o Formulário de Referência.") : aviso);
			});
		});
	});
}

static QString
dinheiroGrande(double x)
{
	if (!std::isfinite(x) || x == 0)
		return QStringLiteral("—");
	const double a = std::fabs(x);
	return a >= 1e9 ? QStringLiteral("R$ %1 bi").arg(num(x / 1e9, 2)) : a >= 1e6 ? QStringLiteral("R$ %1 mi").arg(num(x / 1e6, 1))
										       : QStringLiteral("R$ %1 mil").arg(num(x / 1e3, 0));
}

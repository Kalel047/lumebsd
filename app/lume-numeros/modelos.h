// SPDX-License-Identifier: BSD-2-Clause
//
// lume-numeros: os modelos da galeria, feitos com fórmulas de verdade
// (quem abre vê como foi calculado).

#pragma once

#include "planilha.h"

struct Modelo {
	QString id, nome, descricao;
};

inline const QList<Modelo> &modelos()
{
	static const QList<Modelo> m = {
		{"branco", "Em branco", "Uma planilha vazia"},
		{"orcamento", "Orçamento pessoal", "Planejado × realizado e a regra 50/30/20"},
		{"notas", "Notas da turma", "Médias, situação de cada aluno e o gráfico da turma"},
		{"juros", "Juros compostos", "Capital, aporte mensal e o saldo mês a mês"},
		{"fluxo", "Fluxo de caixa", "VPL, TIR e payback de um projeto"},
		{"indicadores", "Indicadores econômicos", "PIB, IPCA e Selic de 2015 a 2024"},
	};
	return m;
}

class Montador
{
public:
	Montador(Pasta &p, int f) : p_(p), f_(f) {}

	static void cel(const QString &nome, int &r, int &c)
	{
		PExpr e = analisar(nome, Dialeto::Canonico);
		r = e ? e->r1 : 0;
		c = e ? e->c1 : 0;
	}
	void d(const QString &celula, const QString &texto)
	{
		int r, c;
		cel(celula, r, c);
		p_.digitar(f_, r, c, texto);
	}
	// Uma linha de valores a partir de uma célula.
	void linha(const QString &celula, const QStringList &vs)
	{
		int r, c;
		cel(celula, r, c);
		for (int i = 0; i < vs.size(); i++)
			if (!vs[i].isNull())
				p_.digitar(f_, r, c + i, vs[i]);
	}
	void fmt(const QString &faixa, const std::function<void(Formato &)> &f)
	{
		PExpr e = analisar(faixa, Dialeto::Canonico);
		if (!e)
			return;
		const int r2 = e->k == Expr::Area ? e->r2 : e->r1, c2 = e->k == Expr::Area ? e->c2 : e->c1;
		for (int r = e->r1; r <= r2; r++)
			for (int c = e->c1; c <= c2; c++) {
				const Celula *at = p_.folha(f_).em(r, c);
				Celula k = at ? *at : Celula();
				f(k.fmt);
				p_.por(f_, r, c, k);
			}
		p_.folha(f_).recontar();
	}
	void cabecalho(const QString &faixa)
	{
		fmt(faixa, [](Formato &f) {
			f.negrito = true;
			f.fundo = QColor(0xe9, 0xe3, 0xd6);
			f.borda = true;
		});
	}
	void largura(int c, int px) { p_.folha(f_).largura[c] = px; }
	void grafico(Grafico::Tipo t, const QString &titulo, const QString &faixa, double x, double y, double w = 460, double h = 280)
	{
		PExpr e = analisar(faixa, Dialeto::Canonico);
		Grafico g;
		g.tipo = t;
		g.titulo = titulo;
		g.r1 = e->r1; g.c1 = e->c1; g.r2 = e->r2; g.c2 = e->c2;
		g.x = x; g.y = y; g.w = w; g.h = h;
		p_.folha(f_).graficos.push_back(g);
	}

private:
	Pasta &p_;
	int f_;
};

inline void montarModelo(Pasta &p, const QString &id)
{
	p = Pasta();
	const QString nomes[] = {"Orçamento", "Notas", "Juros", "Fluxo", "Indicadores"};
	Montador m(p, 0);
	auto moeda = [](Formato &f) { f.tipo = Formato::Moeda; f.casas = 2; };
	auto pct = [](int casas) { return [casas](Formato &f) { f.tipo = Formato::Porcento; f.casas = casas; }; };
	auto num = [](int casas) { return [casas](Formato &f) { f.tipo = Formato::Numero; f.casas = casas; }; };
	auto negrito = [](Formato &f) { f.negrito = true; };
	auto titulo = [&](const QString &c, const QString &t) {
		m.d(c, t);
		m.fmt(c, [](Formato &f) { f.negrito = true; f.tamanho = 15; });
	};
	if (id == "orcamento") {
		p.renomear(0, nomes[0]);
		titulo("A1", "Orçamento do mês");
		m.d("A2", "Preencha o planejado e o realizado; o resto se calcula.");
		m.fmt("A2", [](Formato &f) { f.italico = true; f.cor = QColor(0x5d, 0x59, 0x50); });
		m.linha("A4", {"Item", "Grupo", "Planejado", "Realizado", "Diferença"});
		m.cabecalho("A4:E4");
		const QList<QStringList> itens = {
			{"Salário", "Receita", "4.800", "4.800"}, {"Trabalhos extras", "Receita", "600", "750"},
			{"Aluguel", "Essencial", "1.400", "1.400"}, {"Mercado", "Essencial", "800", "865,40"},
			{"Luz, água e internet", "Essencial", "320", "301,25"}, {"Transporte", "Essencial", "250", "268"},
			{"Saúde", "Essencial", "180", "95"}, {"Lazer", "Estilo de vida", "350", "420"},
			{"Restaurantes", "Estilo de vida", "300", "345,90"}, {"Assinaturas", "Estilo de vida", "90", "90"},
			{"Reserva de emergência", "Poupança", "600", "600"}, {"Investimentos", "Poupança", "400", "300"}};
		for (int i = 0; i < itens.size(); i++) {
			const QString r = QString::number(5 + i);
			m.linha("A" + r, itens[i]);
			m.d("E" + r, "=D" + r + "-C" + r);
		}
		m.fmt("C5:E16", moeda);
		m.linha("G4", {"Grupo", "Realizado", "% da receita", "Meta"});
		m.cabecalho("G4:J4");
		const QStringList grupos = {"Essencial", "Estilo de vida", "Poupança"};
		const QStringList metas = {"50%", "30%", "20%"};
		for (int i = 0; i < 3; i++) {
			const QString r = QString::number(5 + i);
			m.d("G" + r, grupos[i]);
			m.d("H" + r, "=SOMASE($B$5:$B$16;G" + r + ";$D$5:$D$16)");
			m.d("I" + r, "=H" + r + "/$H$9");
			m.d("J" + r, metas[i]);
		}
		m.d("G9", "Receita");
		m.d("H9", "=SOMASE($B$5:$B$16;\"Receita\";$D$5:$D$16)");
		m.d("G10", "Sobra do mês");
		m.d("H10", "=H9-H5-H6-H7");
		m.fmt("G9:H10", negrito);
		m.fmt("H5:H10", moeda);
		m.fmt("I5:J7", pct(1));
		m.largura(0, 170);
		m.largura(1, 110);
		m.largura(6, 110);
		m.grafico(Grafico::Pizza, "Para onde foi o dinheiro", "G4:H7", 600, 270, 400, 300);
	} else if (id == "notas") {
		p.renomear(0, nomes[1]);
		titulo("A1", "Notas da turma");
		m.d("A2", "Média para aprovação: 6. Entre 4 e 6, recuperação.");
		m.fmt("A2", [](Formato &f) { f.italico = true; f.cor = QColor(0x5d, 0x59, 0x50); });
		m.linha("A4", {"Aluno", "1º bim.", "2º bim.", "3º bim.", "4º bim.", "Média", "Situação"});
		m.cabecalho("A4:G4");
		const QList<QStringList> alunos = {
			{"Ana Beatriz", "8,5", "7", "9", "8"}, {"Bruno", "5", "6,5", "4", "6"}, {"Carla", "9,5", "9", "10", "9,5"},
			{"Diego", "3", "4,5", "5", "4"}, {"Eduarda", "7", "7,5", "6", "8"}, {"Felipe", "6", "5,5", "7", "6,5"},
			{"Gabriela", "10", "9,5", "9", "10"}, {"Henrique", "4", "3,5", "5", "3"}, {"Isabela", "8", "8,5", "7,5", "9"},
			{"João Pedro", "6,5", "7", "5,5", "6"}};
		for (int i = 0; i < alunos.size(); i++) {
			const QString r = QString::number(5 + i);
			m.linha("A" + r, alunos[i]);
			m.d("F" + r, "=MÉDIA(B" + r + ":E" + r + ")");
			m.d("G" + r, "=SE(F" + r + ">=6;\"Aprovado\";SE(F" + r + ">=4;\"Recuperação\";\"Reprovado\"))");
		}
		m.fmt("B5:F14", num(1));
		m.fmt("F5:F14", negrito);
		m.linha("A16", {"Média da turma", QString(), QString(), QString(), QString(), "=MÉDIA(F5:F14)"});
		m.linha("A17", {"Maior média", QString(), QString(), QString(), QString(), "=MÁXIMO(F5:F14)"});
		m.linha("A18", {"Menor média", QString(), QString(), QString(), QString(), "=MÍNIMO(F5:F14)"});
		m.linha("A19", {"Aprovados", QString(), QString(), QString(), QString(), "=CONT.SE(G5:G14;\"Aprovado\")"});
		m.linha("A20", {"Em recuperação", QString(), QString(), QString(), QString(), "=CONT.SE(G5:G14;\"Recuperação\")"});
		m.fmt("A16:A20", negrito);
		m.fmt("F16:F18", num(2));
		m.largura(0, 150);
		m.largura(6, 120);
		// O gráfico: os nomes (A) e só a média (F).
		m.grafico(Grafico::Barras, "Média de cada aluno", "A4:F14", 780, 70, 420, 330);
		p.folha(0).graficos.back().series = {5};
	} else if (id == "juros") {
		p.renomear(0, nomes[2]);
		titulo("A1", "Juros compostos");
		m.linha("A3", {"Capital inicial", "1.000"});
		m.linha("A4", {"Aporte por mês", "300"});
		m.linha("A5", {"Taxa ao mês", "0,85%"});
		m.linha("A6", {"Meses", "36"});
		m.fmt("B3:B4", moeda);
		m.fmt("B5", pct(2));
		m.fmt("A3:A6", negrito);
		m.linha("D3", {"Saldo no fim", "=E44"});
		m.linha("D4", {"Total aportado", "=B3+B4*B6"});
		m.linha("D5", {"Ganho com juros", "=E3-E4"});
		m.linha("D6", {"Conferência (VF)", "=-VF(B5;B6;B4;B3)"});
		m.fmt("E3:E6", moeda);
		m.fmt("D3:D6", negrito);
		m.linha("A8", {"Mês", "Saldo inicial", "Aporte", "Juros", "Saldo final"});
		m.cabecalho("A8:E8");
		for (int i = 1; i <= 36; i++) {
			const QString r = QString::number(8 + i), ant = QString::number(7 + i);
			m.d("A" + r, QString::number(i));
			m.d("B" + r, i == 1 ? "=$B$3" : "=E" + ant);
			m.d("C" + r, "=$B$4");
			m.d("D" + r, "=B" + r + "*$B$5");
			m.d("E" + r, "=B" + r + "+C" + r + "+D" + r);
		}
		m.fmt("B9:E44", moeda);
		m.largura(0, 140);
		m.largura(1, 120);
		m.largura(3, 140);
		m.largura(4, 120);
		// O gráfico: os meses (A) e só o saldo final (E).
		m.grafico(Grafico::Linhas, "O saldo mês a mês", "A8:E44", 640, 200, 480, 300);
		p.folha(0).graficos.back().series = {4};
	} else if (id == "fluxo") {
		p.renomear(0, nomes[3]);
		titulo("A1", "Fluxo de caixa do projeto");
		m.linha("A3", {"Taxa mínima de atratividade (ao ano)", QString(), "12%"});
		m.fmt("C3", pct(1));
		m.fmt("A3", negrito);
		m.linha("A5", {"Ano", "Fluxo", "Acumulado", "Descontado"});
		m.cabecalho("A5:D5");
		const QStringList fl = {"-50.000", "12.000", "15.000", "16.000", "18.000", "20.000"};
		for (int i = 0; i < fl.size(); i++) {
			const QString r = QString::number(6 + i);
			m.d("A" + r, QString::number(i));
			m.d("B" + r, fl[i]);
			m.d("C" + r, i == 0 ? "=B6" : "=C" + QString::number(5 + i) + "+B" + r);
			m.d("D" + r, "=B" + r + "/(1+$C$3)^A" + r);
		}
		m.fmt("B6:D11", moeda);
		m.linha("F5", {"Resultado", QString()});
		m.cabecalho("F5:G5");
		m.linha("F6", {"VPL", "=B6+VPL(C3;B7:B11)"});
		m.linha("F7", {"TIR", "=TIR(B6:B11)"});
		m.linha("F8", {"Payback (anos)", "=CONT.SE(C6:C11;\"<0\")"});
		m.linha("F9", {"Vale a pena?", "=SE(G6>0;\"Sim: o VPL é positivo\";\"Não: o VPL é negativo\")"});
		m.fmt("G6", moeda);
		m.fmt("G7", pct(2));
		m.fmt("F6:F9", negrito);
		m.largura(0, 70);
		m.largura(1, 120);
		m.largura(2, 120);
		m.largura(3, 120);
		m.largura(5, 130);
		m.largura(6, 190);
		m.grafico(Grafico::Colunas, "Fluxo e acumulado", "A5:C11", 40, 320, 520, 300);
	} else if (id == "indicadores") {
		p.renomear(0, nomes[4]);
		titulo("A1", "Indicadores econômicos do Brasil");
		m.d("A2", "PIB: variação real anual (IBGE). IPCA: inflação no ano (IBGE). Selic: meta no fim do ano (Banco Central).");
		m.fmt("A2", [](Formato &f) { f.italico = true; f.cor = QColor(0x5d, 0x59, 0x50); });
		m.linha("A4", {"Ano", "PIB (%)", "IPCA (%)", "Selic (%)", "Juro real (%)"});
		m.cabecalho("A4:E4");
		const QList<QStringList> dados = {
			{"2015", "-3,5", "10,67", "14,25"}, {"2016", "-3,3", "6,29", "13,75"}, {"2017", "1,3", "2,95", "7"},
			{"2018", "1,8", "3,75", "6,5"}, {"2019", "1,2", "4,31", "4,5"}, {"2020", "-3,3", "4,52", "2"},
			{"2021", "4,8", "10,06", "9,25"}, {"2022", "3", "5,79", "13,75"}, {"2023", "3,2", "4,62", "11,75"},
			{"2024", "3,4", "4,83", "12,25"}};
		for (int i = 0; i < dados.size(); i++) {
			const QString r = QString::number(5 + i);
			m.linha("A" + r, dados[i]);
			m.d("E" + r, "=((1+D" + r + "/100)/(1+C" + r + "/100)-1)*100");
		}
		m.fmt("B5:E14", num(2));
		m.linha("A16", {"Média", "=MÉDIA(B5:B14)", "=MÉDIA(C5:C14)", "=MÉDIA(D5:D14)", "=MÉDIA(E5:E14)"});
		m.linha("A17", {"Desvio-padrão", "=DESVPAD(B5:B14)", "=DESVPAD(C5:C14)", "=DESVPAD(D5:D14)", "=DESVPAD(E5:E14)"});
		m.linha("A18", {"Correlação PIB × Selic", "=CORREL(B5:B14;D5:D14)"});
		m.fmt("A16:A18", negrito);
		m.fmt("B16:E18", num(2));
		m.largura(0, 170);
		m.grafico(Grafico::Linhas, "PIB, inflação e juros", "A4:D14", 620, 60, 520, 320);
	}
	p.recalcular();
}

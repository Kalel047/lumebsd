// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: os modelos da galeria. Para quem ensina (uma aula), para quem
// estuda (um seminário), para quem lida com economia (a conjuntura, com os
// mesmos dados do IBGE e do Banco Central dos Números) e para o dia a dia
// (uma reunião).

#pragma once

#include "palco.h"

struct Modelo {
	QString id, nome, descricao;
};

inline const std::vector<Modelo> &modelos()
{
	static const std::vector<Modelo> m = {
		{"branco", "Em branco", "Um slide de título, no tema Papel"},
		{"aula", "Aula", "Objetivos, conteúdo, exemplo, exercícios e tarefa, na lousa"},
		{"seminario", "Seminário", "Roteiro, desenvolvimento, citação, conclusão e referências"},
		{"conjuntura", "Conjuntura econômica", "PIB, IPCA e Selic de 2015 a 2024, em gráficos"},
		{"reuniao", "Reunião", "Pauta, contas, votação e encerramento"},
	};
	return m;
}

class Roteiro
{
public:
	explicit Roteiro(Doc &d) : d_(d) {}

	Slide &slide(Layout l, const QString &titulo = QString(), const QString &sub = QString())
	{
		d_.slides.push_back(novoSlide(d_, l));
		Slide &s = d_.slides.back();
		for (Objeto &o : s.objs) {
			if (o.papel == Papel::Titulo && !titulo.isEmpty())
				texto(o, titulo);
			else if (o.papel == Papel::Subtitulo && !sub.isEmpty())
				texto(o, sub);
		}
		return s;
	}
	// Os tópicos (um por linha; um \t no começo desce um nível).
	void topicos(Slide &s, const QString &t, Papel p = Papel::Corpo, bool numerado = false)
	{
		for (Objeto &o : s.objs)
			if (o.papel == p) {
				texto(o, t);
				for (Paragrafo &par : o.pars) {
					par.marcador = !numerado;
					par.numerado = numerado;
				}
			}
	}
	static void texto(Objeto &o, const QString &t)
	{
		const int alin = o.pars.empty() ? 0 : o.pars.front().alin;
		const bool marc = !o.pars.empty() && o.pars.front().marcador;
		o.pars = paragrafos(t, marc, alin);
	}
	Objeto &forma(Slide &s, Tipo t, QRectF r, const QColor &cor, const QString &txt = QString())
	{
		Objeto o;
		o.tipo = t;
		o.r = r;
		o.preench = cor;
		o.vAlin = 1;
		o.tamanho = 20;
		if (!txt.isEmpty())
			o.pars = paragrafos(txt, false, 1);
		s.objs.push_back(o);
		return s.objs.back();
	}
	Objeto &grafico(Slide &s, QRectF r, const DadosGrafico &g)
	{
		Objeto o;
		o.tipo = Tipo::Grafico;
		o.r = r;
		o.graf = g;
		s.objs.push_back(o);
		return s.objs.back();
	}
	Objeto &caixa(Slide &s, QRectF r, const QString &txt, double tam, int alin = 0)
	{
		Objeto o;
		o.tipo = Tipo::Texto;
		o.r = r;
		o.tamanho = tam;
		o.pars = paragrafos(txt, false, alin);
		s.objs.push_back(o);
		return s.objs.back();
	}
	// Uma tabela (linhas separadas por \n, colunas por \t), com as larguras
	// relativas das colunas e o alinhamento de cada uma.
	Objeto &tabela(Slide &s, QRectF r, const QString &txt, const std::vector<double> &pesos = {}, const std::vector<int> &alins = {},
	               double tam = 18)
	{
		const QStringList ls = txt.split('\n');
		Objeto t = novaTabela(int(ls.size()), int(ls.value(0).split('\t').size()), r, tam);
		if (int(pesos.size()) == t.cols) {
			double soma = 0;
			for (double p : pesos) soma += p;
			for (int c = 0; c < t.cols; c++) t.largs[size_t(c)] = r.width() * pesos[size_t(c)] / soma;
		}
		for (int l = 0; l < t.lins; l++)
			for (int c = 0; c < t.cols; c++) {
				Objeto &x = t.celula(l, c);
				x.pars = paragrafos(ls[l].split('\t').value(c), false, c < int(alins.size()) ? alins[size_t(c)] : 0);
				if (l == 0 && t.cabecalho)
					x.negrito = true;
			}
		t.alts.assign(size_t(t.lins), r.height() / t.lins);
		s.objs.push_back(t);
		return s.objs.back();
	}
	// A entrada de um objeto (a ordem segue a de quem já tem animação).
	static void entrar(Slide &s, Objeto &o, Efeito ef, bool porParagrafo = false, int inicio = 0, int dir = 0, double dur = 0.5)
	{
		int ordem = 0;
		for (const Objeto &x : s.objs) {
			if (x.entrada.tem()) ordem = std::max(ordem, x.entrada.ordem + 1);
			if (x.saida.tem()) ordem = std::max(ordem, x.saida.ordem + 1);
		}
		o.entrada.ef = ef;
		o.entrada.porParagrafo = porParagrafo;
		o.entrada.inicio = inicio;
		o.entrada.dir = dir;
		o.entrada.dur = dur;
		o.entrada.ordem = ordem;
	}
	static Objeto &corpo(Slide &s, Papel p = Papel::Corpo)
	{
		for (Objeto &o : s.objs)
			if (o.papel == p)
				return o;
		return s.objs.back();
	}
	double W() const { return d_.larg; }
	double H() const { return d_.alt; }

private:
	Doc &d_;
};

inline void montarModelo(Doc &d, const QString &id)
{
	d = Doc();
	Roteiro r(d);
	const double W = d.larg, H = d.alt;
	const double m = W * 0.075;
	if (id == "aula") {
		d.tema = tema("giz");
		d.titulo = "Frações";
		Slide &s1 = r.slide(Layout::Titulo, "Frações", "Matemática · 6º ano");
		s1.notas = "Começar perguntando quem já dividiu uma pizza em partes iguais.";
		Slide &s2 = r.slide(Layout::TituloConteudo, "Objetivos da aula");
		r.topicos(s2, "Entender o que uma fração representa\nLer e escrever frações\nReconhecer frações equivalentes\nResolver problemas do dia a dia");
		Roteiro::entrar(s2, Roteiro::corpo(s2), Efeito::Dissolver, true);
		s2.transicao = Transicao::Dissolver;
		Slide &s3 = r.slide(Layout::DuasColunas, "O que é uma fração");
		r.topicos(s3, "Uma parte de um inteiro dividido em partes iguais\nO denominador diz em quantas partes\n\tembaixo do traço\nO numerador diz quantas partes pegamos\n\tem cima do traço");
		s3.objs.erase(std::remove_if(s3.objs.begin(), s3.objs.end(), [](const Objeto &o) { return o.papel == Papel::Corpo2; }), s3.objs.end());
		DadosGrafico pizza;
		pizza.tipo = TipoGrafico::Pizza;
		pizza.titulo = "3/8 da pizza";
		pizza.categorias = QStringList{"Pegamos 3", "Sobraram 5"};
		pizza.series = QStringList{"Pedaços"};
		pizza.valores = {{3, 5}};
		Objeto &gp = r.grafico(s3, QRectF(W * 0.55, H * 0.27, W * 0.38, H * 0.64), pizza);
		Roteiro::entrar(s3, gp, Efeito::Ampliar);
		s3.transicao = Transicao::Dissolver;
		s3.notas = "Pedir para a turma contar os pedaços em voz alta.";
		Slide &s4 = r.slide(Layout::SoTitulo, "Frações equivalentes");
		const double lado = W * 0.2, y = H * 0.36;
		const QStringList fr{"1/2", "2/4", "4/8"};
		for (int i = 0; i < 3; i++) {
			const double x = m + i * (lado + W * 0.1);
			Objeto &f = r.forma(s4, Tipo::Arredondado, QRectF(x, y, lado, H * 0.3), d.tema.serie[size_t(i)], fr[i]);
			f.tamanho = 54;
			f.cor = d.tema.fundo;
			f.negrito = true;
			if (i > 0)
				Roteiro::entrar(s4, f, Efeito::Voar, false, 0, 0);
			if (i < 2) {
				Objeto &igual = r.caixa(s4, QRectF(x + lado, y, W * 0.1, H * 0.3), "=", 54, 1);
				igual.vAlin = 1;
				Roteiro::entrar(s4, igual, Efeito::Dissolver);
			}
		}
		Objeto &regra = r.caixa(s4, QRectF(m, H * 0.74, W - 2 * m, H * 0.12), "Multiplicar em cima e embaixo pelo mesmo número não muda a fração.", 22, 1);
		Roteiro::entrar(s4, regra, Efeito::Revelar, false, 0, 1);
		s4.transicao = Transicao::Empurrar;
		Slide &s5 = r.slide(Layout::TituloConteudo, "Exercícios");
		r.topicos(s5, "Escreva a fração da parte pintada\nQual é maior: 2/3 ou 3/5?\nComplete: 3/4 = ?/12\nUma turma tem 30 alunos e 2/5 são meninas. Quantas meninas?", Papel::Corpo, true);
		Roteiro::entrar(s5, Roteiro::corpo(s5), Efeito::Aparecer, true);
		s5.transicao = Transicao::Dissolver;
		Slide &s6 = r.slide(Layout::Secao, "Para casa", "Páginas 42 e 43 do livro · entregar na quinta");
		s6.transicao = Transicao::PeloPreto;
	} else if (id == "seminario") {
		d.tema = tema("mar");
		d.titulo = "Energia solar no Brasil";
		Slide &s1 = r.slide(Layout::Titulo, "Energia solar no Brasil", "Seminário de Geografia · Grupo 3");
		s1.notas = "Apresentar o grupo: cada um fala de uma parte.";
		Slide &s2 = r.slide(Layout::TituloConteudo, "Roteiro");
		r.topicos(s2, "Como funciona um painel solar\nPor que o Brasil tem tanto sol\nNas casas e nas usinas\nO que ainda falta\nConclusão", Papel::Corpo, true);
		Roteiro::entrar(s2, Roteiro::corpo(s2), Efeito::Voar, true, 0, 1);
		Slide &s3 = r.slide(Layout::DuasColunas, "Vantagens e desafios");
		r.topicos(s3, "Vantagens\n\tenergia limpa e renovável\n\tsol o ano inteiro em quase todo o país\n\tconta de luz menor");
		r.topicos(s3, "Desafios\n\tcusto de instalação\n\tguardar a energia para a noite\n\treciclar os painéis velhos", Papel::Corpo2);
		Roteiro::entrar(s3, Roteiro::corpo(s3, Papel::Corpo2), Efeito::Dissolver);
		s3.transicao = Transicao::Empurrar;
		Slide &s4 = r.slide(Layout::Citacao);
		for (Objeto &o : s4.objs) {
			if (o.papel == Papel::Corpo)
				Roteiro::texto(o, "“O sol que bate no telhado também pode pagar a conta de luz.”");
			else if (o.papel == Papel::Subtitulo)
				Roteiro::texto(o, "— a ideia do nosso trabalho");
		}
		s4.transicao = Transicao::Dissolver;
		Slide &s5 = r.slide(Layout::TituloConteudo, "Conclusão");
		r.topicos(s5, "A energia solar já faz parte da matriz elétrica\nCresce nas casas, nas empresas e nas usinas\nO desafio é guardar a energia e baratear a instalação");
		Slide &s6 = r.slide(Layout::TituloConteudo, "Referências");
		r.topicos(s6, "SOBRENOME, Nome. Título do livro. Cidade: Editora, ano.\nINSTITUIÇÃO. Título da página. Disponível em: endereço. Acesso em: data.");
		for (Objeto &o : s6.objs)
			if (o.papel == Papel::Corpo)
				o.tamanho = 20;
	} else if (id == "conjuntura") {
		d.tema = tema("papel");
		d.titulo = "Conjuntura econômica";
		r.slide(Layout::Titulo, "Conjuntura econômica", "Brasil, 2015–2024 · atividade, inflação e juros");
		const QStringList anos{"2015", "2016", "2017", "2018", "2019", "2020", "2021", "2022", "2023", "2024"};
		const std::vector<double> pib{-3.5, -3.3, 1.3, 1.8, 1.2, -3.3, 4.8, 3.0, 3.2, 3.4};
		const std::vector<double> ipca{10.67, 6.29, 2.95, 3.75, 4.31, 4.52, 10.06, 5.79, 4.62, 4.83};
		const std::vector<double> selic{14.25, 13.75, 7.0, 6.5, 4.5, 2.0, 9.25, 13.75, 11.75, 12.25};
		Slide &s2 = r.slide(Layout::SoTitulo, "A atividade: o PIB");
		DadosGrafico g1;
		g1.tipo = TipoGrafico::Colunas;
		g1.titulo = "PIB: variação real no ano (%)";
		g1.categorias = anos;
		g1.series = QStringList{"PIB"};
		g1.valores = {pib};
		g1.legenda = false;
		r.grafico(s2, QRectF(m, H * 0.27, W * 0.58, H * 0.64), g1);
		Objeto &c2 = r.caixa(s2, QRectF(W * 0.69, H * 0.3, W * 0.25, H * 0.56),
		                     "Duas recessões: 2015–2016 e a pandemia em 2020.\n\nDe 2021 a 2024, crescimento acima de 3% ao ano.", 18);
		c2.cor = d.tema.apagado;
		Roteiro::entrar(s2, c2, Efeito::Dissolver, true);
		s2.transicao = Transicao::Dissolver;
		s2.notas = "Fonte: IBGE, Contas Nacionais.";
		Slide &s3 = r.slide(Layout::SoTitulo, "Inflação e juros");
		DadosGrafico g2;
		g2.tipo = TipoGrafico::Linhas;
		g2.titulo = "IPCA no ano e Selic no fim do ano (%)";
		g2.categorias = anos;
		g2.series = QStringList{"IPCA", "Selic"};
		g2.valores = {ipca, selic};
		Objeto &gl = r.grafico(s3, QRectF(m, H * 0.27, W - 2 * m, H * 0.66), g2);
		Roteiro::entrar(s3, gl, Efeito::Revelar, false, 1, 1, 1.0);
		s3.transicao = Transicao::Dissolver;
		s3.notas = "Fontes: IBGE (IPCA) e Banco Central (meta da Selic).";
		Slide &s3b = r.slide(Layout::SoTitulo, "Os números dos últimos cinco anos");
		QString tab = "Ano\tPIB (%)\tIPCA (%)\tSelic (%)";
		const QLocale br(QLocale::Portuguese, QLocale::Brazil);
		for (int a = 5; a < 10; a++)
			tab += "\n" + anos[a] + "\t" + br.toString(pib[size_t(a)], 'f', 1) + "\t" + br.toString(ipca[size_t(a)], 'f', 2) + "\t" +
			       br.toString(selic[size_t(a)], 'f', 2);
		r.tabela(s3b, QRectF(m, H * 0.28, W - 2 * m, H * 0.56), tab, {1.2, 1, 1, 1}, {0, 2, 2, 2}, 20);
		s3b.transicao = Transicao::Dissolver;
		s3b.notas = "PIB: IBGE. IPCA: IBGE. Selic: meta do Copom no fim do ano.";
		Slide &s4 = r.slide(Layout::Grande, "7,1%", "o juro real no fim de 2024: a Selic de 12,25% descontado o IPCA de 4,83%");
		s4.transicao = Transicao::Ampliar;
		s4.notas = "Juro real = (1 + Selic) / (1 + IPCA) − 1 = 1,1225 / 1,0483 − 1.";
		Slide &s5 = r.slide(Layout::DuasColunas, "O que olhar daqui para a frente");
		r.topicos(s5, "Riscos\n\tinflação de serviços\n\tcontas públicas\n\tjuros altos por mais tempo");
		r.topicos(s5, "Sinais bons\n\tmercado de trabalho firme\n\tsafra e exportações\n\tinflação perto da meta", Papel::Corpo2);
		Roteiro::entrar(s5, Roteiro::corpo(s5, Papel::Corpo2), Efeito::Voar, false, 0, 2);
		r.slide(Layout::Secao, "Perguntas?", "Os dados estão no modelo Indicadores econômicos dos Números");
	} else if (id == "reuniao") {
		d.tema = tema("terracota");
		d.titulo = "Reunião de condomínio";
		r.slide(Layout::Titulo, "Reunião de condomínio", "Assembleia ordinária");
		Slide &s2 = r.slide(Layout::TituloConteudo, "Pauta");
		r.topicos(s2, "Prestação de contas do ano\nOrçamento do ano que vem\nObras na garagem\nAssuntos gerais", Papel::Corpo, true);
		Slide &s3 = r.slide(Layout::SoTitulo, "Para onde foi o dinheiro");
		DadosGrafico g;
		g.tipo = TipoGrafico::Pizza;
		g.titulo = "Despesas do ano (exemplo)";
		g.categorias = QStringList{"Portaria", "Limpeza", "Água e luz", "Manutenção", "Administração"};
		g.series = QStringList{"R$"};
		g.valores = {{48000, 30000, 26000, 18000, 9000}};
		r.grafico(s3, QRectF(m, H * 0.25, W - 2 * m, H * 0.68), g);
		s3.transicao = Transicao::Dissolver;
		Slide &s3b = r.slide(Layout::SoTitulo, "Orçamento do ano que vem");
		Objeto &orc = r.tabela(s3b, QRectF(m, H * 0.27, W - 2 * m, H * 0.6),
		                       "Despesa\tEste ano\tProposta\nPortaria\tR$ 48.000\tR$ 50.400\nLimpeza\tR$ 30.000\tR$ 31.500\n"
		                       "Água e luz\tR$ 26.000\tR$ 27.000\nManutenção\tR$ 18.000\tR$ 24.000\nAdministração\tR$ 9.000\tR$ 9.000",
		                       {1.6, 1, 1}, {0, 2, 2});
		Roteiro::entrar(s3b, orc, Efeito::Dissolver);
		s3b.notas = "A manutenção sobe por causa das obras na garagem.";
		Slide &s4 = r.slide(Layout::TituloConteudo, "Para votar");
		r.topicos(s4, "Aprovar as contas do ano\nReajuste da taxa de condomínio\nOrçamento das obras na garagem\n\tlevantar três orçamentos antes");
		Slide &s5 = r.slide(Layout::Secao, "Obrigado!", "A ata vai por e-mail");
		s5.transicao = Transicao::PeloPreto;
	} else {
		d.tema = tema("papel");
		r.slide(Layout::Titulo);
	}
}

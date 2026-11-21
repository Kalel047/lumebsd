// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: o documento. Uma apresentação é uma lista de slides; cada
// slide tem objetos (caixas de texto, formas, imagens, gráficos, tabelas,
// vídeos), cada um com a sua animação de entrada e de saída, com
// posição em pontos (1/72 de polegada) sobre um palco de 960 x 540 (16:9)
// ou 720 x 540 (4:3), as mesmas medidas do PowerPoint e do Keynote.
//
// O tema dá o fundo, as cores do texto e as fontes. Uma cor inválida num
// objeto quer dizer "a do tema", e por isso trocar o tema repinta tudo.

#pragma once

#include <QMarginsF>

#include "../comum/grafico.h"

#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

// --- texto -----------------------------------------------------------------

struct Trecho {
	QString texto;
	bool negrito = false, italico = false, sublinhado = false;
	QColor cor;          // inválida: a do objeto
	double tamanho = 0;  // 0: o do objeto
	QString fonte;       // vazia: a do objeto
};

inline Trecho trecho(const QString &texto)
{
	Trecho t;
	t.texto = texto;
	return t;
}

struct Paragrafo {
	std::vector<Trecho> trechos;
	int nivel = 0;            // 0 a 4
	bool marcador = false;    // com marcador (•)
	bool numerado = false;    // 1. 2. 3.
	int alin = 0;             // 0 esquerda, 1 centro, 2 direita, 3 justificado

	QString texto() const
	{
		QString r;
		for (const Trecho &t : trechos)
			r += t.texto;
		return r;
	}
};

inline std::vector<Paragrafo> paragrafos(const QString &texto, bool marcador = false, int alin = 0)
{
	std::vector<Paragrafo> r;
	for (const QString &l : texto.split('\n')) {
		Paragrafo p;
		QString s = l;
		while (s.startsWith('\t')) {
			p.nivel++;
			s.remove(0, 1);
		}
		p.marcador = marcador;
		p.alin = alin;
		if (!s.isEmpty())
			p.trechos.push_back(trecho(s));
		r.push_back(p);
	}
	return r;
}

// Os dados dos gráficos (e o texto deles) estão em ../comum/grafico.h.

// --- animações -------------------------------------------------------------
//
// Cada objeto pode entrar e sair do slide com um efeito (como as "compilações"
// do Keynote). A ordem é um número por slide; o início é com o clique, junto
// com a anterior ou depois dela. Por parágrafo: cada tópico entra num passo.

enum class Efeito { Nenhum, Aparecer, Dissolver, Voar, Ampliar, Revelar };

inline const QStringList &nomesEfeito()
{
	static const QStringList n = {"Nenhum", "Aparecer", "Dissolver", "Voar", "Ampliar", "Revelar"};
	return n;
}

struct Animacao {
	Efeito ef = Efeito::Nenhum;
	int inicio = 0;           // 0 ao clicar, 1 com a anterior, 2 depois da anterior
	double dur = 0.5;         // segundos
	int dir = 0;              // de onde (Voar, Revelar): 0 de baixo, 1 da esquerda, 2 da direita, 3 de cima
	bool porParagrafo = false;
	int ordem = 0;
	bool tem() const { return ef != Efeito::Nenhum; }
};

// --- objetos ---------------------------------------------------------------

enum class Tipo { Texto, Retangulo, Arredondado, Elipse, Triangulo, Seta, Linha, Imagem, Grafico, Tabela, Video };
enum class Papel { Livre, Titulo, Subtitulo, Corpo, Corpo2 };

struct Objeto {
	Tipo tipo = Tipo::Texto;
	Papel papel = Papel::Livre;
	QRectF r;
	double giro = 0;          // graus, no sentido do relógio
	bool viraH = false, viraV = false; // espelhado (as linhas usam para a direção)
	QRectF recorte{0, 0, 1, 1}; // a parte da imagem que aparece (em frações)
	// O texto (também nas formas).
	std::vector<Paragrafo> pars;
	double tamanho = 20;      // pontos
	QString fonte;            // vazia: a do tema (títulos ou texto)
	QColor cor;               // inválida: a do tema
	bool negrito = false, italico = false;
	int vAlin = 0;            // 0 em cima, 1 no meio, 2 embaixo
	bool encolher = false;    // diminui a letra para caber
	double espaco = 1.0;      // entrelinha
	// O desenho.
	QColor preench;           // inválida: sem preenchimento
	QColor borda;             // inválida: sem borda
	double espBorda = 0;
	// Imagem, gráfico e vídeo.
	QString midia;            // a chave em Doc::midias (a imagem ou o vídeo)
	DadosGrafico graf;
	QString nome;             // o nome no arquivo (só para guardar)
	// O vídeo: a imagem de capa (também em Doc::midias) e como toca.
	QString capa;
	bool sozinho = false;     // começa quando o slide aparece (senão, com um clique nele)
	bool repetir = false, mudo = false;
	// A tabela: as células (caixas de texto, linha por linha), as larguras
	// das colunas e as alturas mínimas das linhas (em pontos).
	int lins = 0, cols = 0;
	std::vector<double> largs, alts;
	std::vector<Objeto> cel;
	bool cabecalho = true, faixas = true;
	QColor fundoCel;          // (só ao desenhar) o fundo da célula, para a cor do texto
	// As animações.
	Animacao entrada, saida;
	int idArq = 0;            // (só ao ler) o número do objeto no arquivo
	// A margem do texto dentro da caixa (pontos: esquerda, cima, direita, baixo).
	QMarginsF margem{7.2, 3.6, 7.2, 3.6};
	bool margemPadrao() const
	{
		return std::fabs(margem.left() - 7.2) < 0.2 && std::fabs(margem.top() - 3.6) < 0.2 && std::fabs(margem.right() - 7.2) < 0.2 &&
		       std::fabs(margem.bottom() - 3.6) < 0.2;
	}

	bool temTexto() const
	{
		for (const Paragrafo &p : pars)
			if (!p.texto().isEmpty())
				return true;
		return false;
	}
	QString texto() const
	{
		QStringList l;
		for (const Paragrafo &p : pars)
			l << QString(p.nivel, '\t') + p.texto();
		return l.join('\n');
	}
	bool forma() const
	{
		return tipo != Tipo::Texto && tipo != Tipo::Imagem && tipo != Tipo::Grafico && tipo != Tipo::Tabela && tipo != Tipo::Video;
	}
	bool aceitaTexto() const
	{
		return tipo != Tipo::Imagem && tipo != Tipo::Grafico && tipo != Tipo::Linha && tipo != Tipo::Tabela && tipo != Tipo::Video;
	}
	Objeto &celula(int l, int c) { return cel[size_t(l * cols + c)]; }
	const Objeto &celula(int l, int c) const { return cel[size_t(l * cols + c)]; }
};

// Uma tabela nova, com o texto das células (linhas separadas por \n, colunas por \t).
inline Objeto novaTabela(int lins, int cols, const QRectF &r, double tamanho = 18)
{
	Objeto t;
	t.tipo = Tipo::Tabela;
	t.lins = std::max(1, lins);
	t.cols = std::max(1, cols);
	t.r = r;
	t.tamanho = tamanho;
	t.largs.assign(size_t(t.cols), r.width() / t.cols);
	t.alts.assign(size_t(t.lins), std::max(12.0, r.height() / t.lins));
	for (int k = 0; k < t.lins * t.cols; k++) {
		Objeto c;
		c.tipo = Tipo::Texto;
		c.tamanho = tamanho;
		c.vAlin = 1;
		c.pars.push_back(Paragrafo());
		t.cel.push_back(c);
	}
	return t;
}

inline void textoNaTabela(Objeto &t, const QString &texto)
{
	const QStringList ls = texto.split('\n');
	for (int l = 0; l < t.lins && l < ls.size(); l++) {
		const QStringList cs = ls[l].split('\t');
		for (int c = 0; c < t.cols && c < cs.size(); c++) {
			Objeto &x = t.celula(l, c);
			const int alin = x.pars.empty() ? 0 : x.pars.front().alin;
			x.pars = paragrafos(cs[c], false, alin);
		}
	}
}

// Uma linha nova na posição "l" (0 a lins; o estilo vem da vizinha) ou,
// com "tirar", a linha "l" a menos. O mesmo para as colunas.
inline void tabelaLinha(Objeto &t, int l, bool tirar)
{
	if (tirar) {
		if (t.lins <= 1 || l < 0 || l >= t.lins) return;
		t.cel.erase(t.cel.begin() + l * t.cols, t.cel.begin() + (l + 1) * t.cols);
		t.alts.erase(t.alts.begin() + l);
		t.lins--;
		return;
	}
	l = std::clamp(l, 0, t.lins);
	const int viz = l == 0 ? 0 : l - 1; // a de cima (ou a primeira)
	std::vector<Objeto> nova;
	for (int c = 0; c < t.cols; c++) {
		Objeto x = t.celula(viz, c);
		const int alin = x.pars.empty() ? 0 : x.pars.front().alin;
		x.pars = {Paragrafo()};
		x.pars.front().alin = alin;
		nova.push_back(x);
	}
	t.cel.insert(t.cel.begin() + l * t.cols, nova.begin(), nova.end());
	t.alts.insert(t.alts.begin() + l, t.alts[size_t(viz)]);
	t.lins++;
}

inline void tabelaColuna(Objeto &t, int c0, bool tirar)
{
	if (tirar) {
		if (t.cols <= 1 || c0 < 0 || c0 >= t.cols) return;
		const double w = t.largs[size_t(c0)];
		for (int l = t.lins - 1; l >= 0; l--)
			t.cel.erase(t.cel.begin() + l * t.cols + c0);
		t.largs.erase(t.largs.begin() + c0);
		t.cols--;
		for (double &x : t.largs) x += w / t.cols;
		return;
	}
	c0 = std::clamp(c0, 0, t.cols);
	const int viz = std::min(c0, t.cols - 1);
	for (int l = t.lins - 1; l >= 0; l--) {
		Objeto x = t.cel[size_t(l * t.cols + viz)];
		const int alin = x.pars.empty() ? 0 : x.pars.front().alin;
		x.pars = {Paragrafo()};
		x.pars.front().alin = alin;
		t.cel.insert(t.cel.begin() + l * t.cols + c0, x);
	}
	// A coluna nova divide a largura com as outras (a tabela não cresce).
	const double w = t.r.width() / (t.cols + 1);
	t.largs.insert(t.largs.begin() + c0, w);
	t.cols++;
	double soma = 0;
	for (double x : t.largs) soma += x;
	for (double &x : t.largs) x *= t.r.width() / soma;
}

// --- slides ----------------------------------------------------------------

enum class Layout { Titulo, TituloConteudo, Secao, DuasColunas, SoTitulo, Citacao, Grande, Branco };

struct Leiaute {
	Layout id;
	QString nome;
};

inline const std::vector<Leiaute> &leiautes()
{
	static const std::vector<Leiaute> l = {
		{Layout::Titulo, "Título"},
		{Layout::TituloConteudo, "Título e tópicos"},
		{Layout::Secao, "Seção"},
		{Layout::DuasColunas, "Duas colunas"},
		{Layout::SoTitulo, "Só o título"},
		{Layout::Citacao, "Citação"},
		{Layout::Grande, "Um número grande"},
		{Layout::Branco, "Em branco"},
	};
	return l;
}

inline QString nomeLeiaute(Layout l)
{
	for (const Leiaute &x : leiautes())
		if (x.id == l)
			return x.nome;
	return {};
}

enum class Transicao { Nenhuma, Dissolver, Empurrar, Cobrir, Descobrir, Ampliar, PeloPreto };

inline const QStringList &nomesTransicao()
{
	static const QStringList n = {"Nenhuma", "Dissolver", "Empurrar", "Cobrir", "Descobrir", "Ampliar", "Pelo preto"};
	return n;
}

struct Slide {
	Layout layout = Layout::TituloConteudo;
	std::vector<Objeto> objs;
	QString notas;
	QColor fundo;             // inválida: a do tema
	QString fundoMidia;       // uma imagem de fundo
	Transicao transicao = Transicao::Nenhuma;
	double duracao = 0.7;     // segundos
	bool pular = false;       // não aparece na apresentação

	const Objeto *com(Papel p) const
	{
		for (const Objeto &o : objs)
			if (o.papel == p)
				return &o;
		return nullptr;
	}
	QString titulo() const
	{
		if (const Objeto *o = com(Papel::Titulo))
			return o->texto().simplified();
		for (const Objeto &o : objs)
			if (o.temTexto())
				return o.texto().section('\n', 0, 0).simplified();
		return {};
	}
};

// --- a sequência das animações --------------------------------------------
//
// Os eventos de um slide em ordem, em grupos: o grupo 0 roda sozinho quando
// o slide aparece (os que não começam com um clique); cada clique roda o
// grupo seguinte. Dentro do grupo, cada evento tem o seu começo (t0).

struct Evento {
	int obj = 0;
	bool saida = false;
	int par = -1;             // o parágrafo (por parágrafo) ou -1 (o objeto todo)
	Animacao a;
	double t0 = 0;
};

inline std::vector<std::vector<Evento>> sequencia(const Slide &s)
{
	struct Item {
		int ordem, obj;
		bool saida;
	};
	std::vector<Item> itens;
	for (int k = 0; k < int(s.objs.size()); k++) {
		const Objeto &o = s.objs[size_t(k)];
		if (o.entrada.tem()) itens.push_back({o.entrada.ordem, k, false});
		if (o.saida.tem()) itens.push_back({o.saida.ordem, k, true});
	}
	std::stable_sort(itens.begin(), itens.end(), [](const Item &a, const Item &b) {
		return a.ordem != b.ordem ? a.ordem < b.ordem : (a.obj != b.obj ? a.obj < b.obj : a.saida < b.saida);
	});
	std::vector<std::vector<Evento>> g(1);
	double fim = 0, ini = 0;
	auto poe = [&](Evento e, int inicio) {
		const double dur = e.a.ef == Efeito::Aparecer ? 0 : e.a.dur;
		if (inicio == 0) {
			g.emplace_back();
			ini = fim = 0;
		}
		if (inicio == 2)
			ini = fim;
		e.t0 = ini;
		fim = std::max(fim, ini + dur);
		g.back().push_back(e);
	};
	for (const Item &it : itens) {
		const Objeto &o = s.objs[size_t(it.obj)];
		const Animacao &a = it.saida ? o.saida : o.entrada;
		const bool pp = a.porParagrafo && o.aceitaTexto() && o.temTexto() && !it.saida;
		if (pp) {
			bool primeiro = true;
			for (int p = 0; p < int(o.pars.size()); p++) {
				if (o.pars[size_t(p)].texto().trimmed().isEmpty())
					continue;
				Evento e{it.obj, false, p, a, 0};
				poe(e, primeiro ? a.inicio : 0);
				primeiro = false;
			}
		} else
			poe(Evento{it.obj, it.saida, -1, a, 0}, a.inicio);
	}
	// O grupo 0 é o que roda sozinho (vazio se o primeiro evento é com clique).
	return g;
}

// Quanto falta de um grupo para acabar (segundos).
inline double duracaoGrupo(const std::vector<Evento> &g)
{
	double f = 0;
	for (const Evento &e : g)
		f = std::max(f, e.t0 + (e.a.ef == Efeito::Aparecer ? 0 : e.a.dur));
	return f;
}

// Como um objeto (ou um parágrafo) aparece num instante: o efeito e o quanto
// já andou (0 a 1). Sem efeito: visível.
struct Quadro {
	Efeito ef = Efeito::Nenhum;
	bool saida = false;
	double p = 1;
	int dir = 0;
	bool some() const { return ef != Efeito::Nenhum && ((!saida && p <= 0) || (saida && p >= 1)); }
};

struct EstadoObj {
	bool tem = false;
	Quadro todo;
	std::vector<Quadro> pars; // por parágrafo (vazio: o objeto todo)
};

// O estado do slide depois de "grupo" grupos completos e "t" segundos no
// grupo seguinte (t < 0: o grupo seguinte ainda não começou).
inline std::vector<EstadoObj> estadoDoSlide(const Slide &s, int grupo, double t)
{
	const auto g = sequencia(s);
	std::vector<EstadoObj> e(s.objs.size());
	// Quem tem entrada começa escondido.
	for (int k = 0; k < int(s.objs.size()); k++) {
		const Objeto &o = s.objs[size_t(k)];
		if (!o.entrada.tem())
			continue;
		e[size_t(k)].tem = true;
		Quadro q{o.entrada.ef, false, 0, o.entrada.dir};
		if (o.entrada.porParagrafo && o.aceitaTexto() && o.temTexto())
			e[size_t(k)].pars.assign(o.pars.size(), q);
		else
			e[size_t(k)].todo = q;
	}
	auto aplica = [&](const Evento &ev, double p) {
		EstadoObj &x = e[size_t(ev.obj)];
		x.tem = true;
		Quadro q{ev.a.ef, ev.saida, std::clamp(p, 0.0, 1.0), ev.a.dir};
		if (ev.par >= 0 && ev.par < int(x.pars.size()))
			x.pars[size_t(ev.par)] = q;
		else {
			x.todo = q;
			if (ev.saida)
				x.pars.clear();
		}
	};
	for (int k = 0; k < int(g.size()); k++) {
		if (k > grupo || (k == grupo && t < 0))
			break;
		for (const Evento &ev : g[size_t(k)]) {
			double p = 1;
			if (k == grupo) {
				const double dur = ev.a.ef == Efeito::Aparecer ? 0 : ev.a.dur;
				if (t < ev.t0) continue;
				p = dur <= 0 ? 1 : (t - ev.t0) / dur;
			}
			aplica(ev, p);
		}
	}
	return e;
}

// --- temas -----------------------------------------------------------------

struct Tema {
	QString id, nome;
	QColor fundo, titulo, texto, destaque, apagado;
	std::vector<QColor> serie;
	QString fonteTitulo, fonteTexto;
	bool tituloNegrito = false;
};

inline const std::vector<Tema> &temas()
{
	static const std::vector<Tema> t = {
		{"papel", "Papel", QColor(0xf6, 0xf2, 0xe9), QColor(0x1c, 0x1b, 0x19), QColor(0x2b, 0x2a, 0x27), QColor(0xb5, 0x65, 0x2a),
		 QColor(0x6d, 0x68, 0x5e),
		 {QColor(0x2f, 0x4b, 0x6e), QColor(0xb5, 0x65, 0x2a), QColor(0x5b, 0x7a, 0x48), QColor(0x8a, 0x5a, 0x8c), QColor(0xc9, 0xa2, 0x27)},
		 "IBM Plex Serif", "IBM Plex Sans", false},
		{"noite", "Noite", QColor(0x1e, 0x1d, 0x1b), QColor(0xf2, 0xef, 0xe8), QColor(0xde, 0xda, 0xd0), QColor(0xe0, 0xa4, 0x58),
		 QColor(0x9a, 0x94, 0x88),
		 {QColor(0x7f, 0xa7, 0xd6), QColor(0xe0, 0xa4, 0x58), QColor(0x9c, 0xc0, 0x84), QColor(0xc4, 0x9b, 0xc6), QColor(0xe8, 0xd0, 0x6c)},
		 "IBM Plex Serif", "IBM Plex Sans", false},
		{"giz", "Lousa", QColor(0x26, 0x3d, 0x33), QColor(0xf5, 0xf3, 0xe7), QColor(0xe6, 0xe4, 0xd6), QColor(0xf2, 0xc9, 0x4c),
		 QColor(0xa9, 0xb8, 0xac),
		 {QColor(0xf2, 0xc9, 0x4c), QColor(0xf5, 0xf3, 0xe7), QColor(0xef, 0x8f, 0x7a), QColor(0x8e, 0xc8, 0xe6), QColor(0xb7, 0xd9, 0x8b)},
		 "IBM Plex Serif", "IBM Plex Sans", false},
		{"mar", "Mar", QColor(0xff, 0xff, 0xff), QColor(0x12, 0x35, 0x5b), QColor(0x24, 0x32, 0x3f), QColor(0x1f, 0x8a, 0x8a),
		 QColor(0x6b, 0x7a, 0x88),
		 {QColor(0x12, 0x35, 0x5b), QColor(0x1f, 0x8a, 0x8a), QColor(0xe0, 0x8e, 0x2b), QColor(0x7b, 0x9e, 0xc4), QColor(0xb8, 0x4a, 0x4a)},
		 "IBM Plex Sans", "IBM Plex Sans", true},
		{"terracota", "Terracota", QColor(0xf3, 0xe6, 0xd8), QColor(0x7a, 0x2e, 0x15), QColor(0x3b, 0x2a, 0x20), QColor(0xc4, 0x57, 0x2e),
		 QColor(0x8a, 0x6f, 0x5e),
		 {QColor(0xc4, 0x57, 0x2e), QColor(0x7a, 0x2e, 0x15), QColor(0x6f, 0x8a, 0x4b), QColor(0xd9, 0xa4, 0x41), QColor(0x4f, 0x6d, 0x7a)},
		 "IBM Plex Serif", "IBM Plex Sans", false},
	};
	return t;
}

inline Tema tema(const QString &id)
{
	for (const Tema &t : temas())
		if (t.id == id)
			return t;
	return temas().front();
}

// --- o documento -----------------------------------------------------------

struct Doc {
	double larg = 960, alt = 540;
	Tema tema = temas().front();
	std::vector<Slide> slides;
	QMap<QString, QByteArray> midias; // "imagem3.png" -> bytes
	QString titulo, autor;

	QString guardarMidia(const QByteArray &dados, const QString &ext)
	{
		for (auto it = midias.cbegin(); it != midias.cend(); ++it)
			if (it.value() == dados)
				return it.key();
		int n = int(midias.size()) + 1;
		QString chave;
		const QString e = ext.toLower();
		const bool video = QStringList{"mp4", "m4v", "mov", "webm", "mkv", "avi", "ogv", "mpg", "mpeg", "wmv"}.contains(e);
		do
			chave = QString("%1%2.%3").arg(video ? "video" : "imagem").arg(n++).arg(e);
		while (midias.contains(chave));
		midias.insert(chave, dados);
		return chave;
	}
	// Tira da lista as imagens que nenhum slide usa mais.
	void limparMidias()
	{
		QStringList usadas;
		for (const Slide &s : slides) {
			usadas << s.fundoMidia;
			for (const Objeto &o : s.objs)
				usadas << o.midia << o.capa;
		}
		for (const QString &k : midias.keys())
			if (!usadas.contains(k))
				midias.remove(k);
	}
};

inline QColor corTexto(const Doc &d, const Objeto &o)
{
	if (o.cor.isValid())
		return o.cor;
	if (o.fundoCel.isValid())
		return o.fundoCel.lightness() < 140 ? QColor(0xff, 0xff, 0xff) : d.tema.texto;
	if (o.forma() && o.preench.isValid())
		return o.preench.lightness() < 140 ? QColor(0xff, 0xff, 0xff) : d.tema.titulo;
	return o.papel == Papel::Titulo ? d.tema.titulo : d.tema.texto;
}

inline QString fonteDe(const Doc &d, const Objeto &o)
{
	if (!o.fonte.isEmpty())
		return o.fonte;
	return o.papel == Papel::Titulo ? d.tema.fonteTitulo : d.tema.fonteTexto;
}

inline QColor fundoDe(const Doc &d, const Slide &s) { return s.fundo.isValid() ? s.fundo : d.tema.fundo; }

// --- os leiautes -----------------------------------------------------------
//
// Onde ficam o título e o texto em cada leiaute, em proporções do palco
// (vale para 16:9 e para 4:3).

inline Objeto caixa(Papel p, QRectF r, double tamanho, int alin, int vAlin, bool marcador = false)
{
	Objeto o;
	o.tipo = Tipo::Texto;
	o.papel = p;
	o.r = r;
	o.tamanho = tamanho;
	o.vAlin = vAlin;
	o.encolher = p == Papel::Corpo || p == Papel::Corpo2;
	Paragrafo par;
	par.alin = alin;
	par.marcador = marcador;
	o.pars.push_back(par);
	return o;
}

inline std::vector<Objeto> moldes(Layout l, double W, double H)
{
	const double m = W * 0.075; // margem lateral
	auto R = [&](double x, double y, double w, double h) { return QRectF(x * W, y * H, w * W, h * H); };
	std::vector<Objeto> r;
	switch (l) {
	case Layout::Titulo:
		r.push_back(caixa(Papel::Titulo, R(0.08, 0.26, 0.84, 0.28), 54, 1, 2));
		r.push_back(caixa(Papel::Subtitulo, R(0.12, 0.58, 0.76, 0.16), 24, 1, 0));
		break;
	case Layout::TituloConteudo:
		r.push_back(caixa(Papel::Titulo, QRectF(m, H * 0.07, W - 2 * m, H * 0.17), 38, 0, 2));
		r.push_back(caixa(Papel::Corpo, QRectF(m, H * 0.28, W - 2 * m, H * 0.62), 24, 0, 0, true));
		break;
	case Layout::Secao:
		r.push_back(caixa(Papel::Titulo, R(0.08, 0.36, 0.84, 0.22), 48, 0, 2));
		r.push_back(caixa(Papel::Subtitulo, R(0.08, 0.60, 0.84, 0.12), 22, 0, 0));
		break;
	case Layout::DuasColunas: {
		const double meio = W * 0.03;
		const double w = (W - 2 * m - meio) / 2;
		r.push_back(caixa(Papel::Titulo, QRectF(m, H * 0.07, W - 2 * m, H * 0.17), 38, 0, 2));
		r.push_back(caixa(Papel::Corpo, QRectF(m, H * 0.28, w, H * 0.62), 22, 0, 0, true));
		r.push_back(caixa(Papel::Corpo2, QRectF(m + w + meio, H * 0.28, w, H * 0.62), 22, 0, 0, true));
		break;
	}
	case Layout::SoTitulo:
		r.push_back(caixa(Papel::Titulo, QRectF(m, H * 0.07, W - 2 * m, H * 0.17), 38, 0, 2));
		break;
	case Layout::Citacao: {
		Objeto c = caixa(Papel::Corpo, R(0.12, 0.22, 0.76, 0.42), 34, 1, 2);
		c.italico = true;
		c.fonte = "IBM Plex Serif";
		c.encolher = true;
		r.push_back(c);
		r.push_back(caixa(Papel::Subtitulo, R(0.12, 0.68, 0.76, 0.1), 20, 1, 0));
		break;
	}
	case Layout::Grande: {
		Objeto n = caixa(Papel::Titulo, R(0.08, 0.18, 0.84, 0.42), 120, 1, 2);
		r.push_back(n);
		r.push_back(caixa(Papel::Subtitulo, R(0.12, 0.62, 0.76, 0.16), 26, 1, 0));
		break;
	}
	case Layout::Branco:
		break;
	}
	return r;
}

// O leiaute de um slide de fora, pelo que ele tem.
inline Layout inferirLeiaute(const Slide &s)
{
	int t = 0, sub = 0, corpo = 0;
	for (const Objeto &o : s.objs) {
		t += o.papel == Papel::Titulo;
		sub += o.papel == Papel::Subtitulo;
		corpo += o.papel == Papel::Corpo || o.papel == Papel::Corpo2;
	}
	if (t && corpo >= 2) return Layout::DuasColunas;
	if (t && corpo) return Layout::TituloConteudo;
	if (t && sub) return Layout::Titulo;
	if (t) return Layout::SoTitulo;
	if (corpo && sub) return Layout::Citacao;
	return Layout::Branco;
}

inline Slide novoSlide(const Doc &d, Layout l)
{
	Slide s;
	s.layout = l;
	s.objs = moldes(l, d.larg, d.alt);
	return s;
}

// Troca o leiaute: o título e os textos vão para os lugares novos; o que não
// cabe no leiaute novo vira um objeto livre (nada se perde).
inline void aplicarLeiaute(const Doc &d, Slide &s, Layout l)
{
	std::vector<Objeto> novos = moldes(l, d.larg, d.alt);
	std::vector<Objeto> resto;
	std::vector<bool> usado(novos.size(), false);
	for (Objeto &o : s.objs) {
		bool foi = false;
		if (o.papel != Papel::Livre) {
			for (size_t i = 0; i < novos.size(); i++) {
				if (!usado[i] && novos[i].papel == o.papel) {
					Objeto n = novos[i];
					if (o.temTexto()) {
						n.pars = o.pars;
						for (Paragrafo &p : n.pars) {
							p.alin = novos[i].pars.front().alin;
							p.marcador = novos[i].pars.front().marcador;
						}
					}
					usado[i] = true;
					novos[i] = n;
					foi = true;
					break;
				}
			}
		}
		if (!foi) {
			if (o.papel != Papel::Livre && !o.temTexto())
				continue; // um lugar vazio que não existe no leiaute novo
			o.papel = Papel::Livre;
			resto.push_back(o);
		}
	}
	s.objs = novos;
	for (Objeto &o : resto)
		s.objs.push_back(o);
	s.layout = l;
}

// Muda o tamanho do palco (16:9 <-> 4:3): tudo se ajusta na horizontal.
inline void mudarFormato(Doc &d, double larg)
{
	const double f = larg / d.larg;
	for (Slide &s : d.slides)
		for (Objeto &o : s.objs) {
			if (o.tipo == Tipo::Imagem) {
				const double cx = o.r.center().x() * f;
				o.r.moveCenter(QPointF(cx, o.r.center().y()));
				if (o.r.width() > larg) {
					const double k = larg / o.r.width();
					o.r = QRectF(0, o.r.center().y() - o.r.height() * k / 2, larg, o.r.height() * k);
				}
			} else
				o.r = QRectF(o.r.x() * f, o.r.y(), o.r.width() * f, o.r.height());
		}
	d.larg = larg;
}

// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: o documento. Uma apresentação é uma lista de slides; cada
// slide tem objetos (caixas de texto, formas, imagens, gráficos) com
// posição em pontos (1/72 de polegada) sobre um palco de 960 x 540 (16:9)
// ou 720 x 540 (4:3), as mesmas medidas do PowerPoint e do Keynote.
//
// O tema dá o fundo, as cores do texto e as fontes. Uma cor inválida num
// objeto quer dizer "a do tema", e por isso trocar o tema repinta tudo.

#pragma once

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

// --- gráficos --------------------------------------------------------------

enum class TipoGrafico { Colunas, Barras, Linhas, Pizza };

struct DadosGrafico {
	TipoGrafico tipo = TipoGrafico::Colunas;
	QString titulo;
	QStringList categorias;
	QStringList series;
	std::vector<std::vector<double>> valores; // [série][categoria]
	bool legenda = true;
};

// Os dados em texto (vão no "texto alternativo" da imagem que representa o
// gráfico no .pptx e no .odp, e voltam a ser um gráfico quando o Palco lê
// o arquivo de novo):  tipo;título|cat1;cat2|série1;v;v|série2;v;v
inline QString gravarDados(const DadosGrafico &g)
{
	static const char *nomes[] = {"colunas", "barras", "linhas", "pizza"};
	auto limpa = [](QString s) { return s.replace('|', '/').replace(';', ','); };
	QStringList partes;
	partes << QString(nomes[int(g.tipo)]) + ";" + limpa(g.titulo) + (g.legenda ? "" : ";semlegenda");
	QStringList cats;
	for (const QString &c : g.categorias)
		cats << limpa(c);
	partes << cats.join(';');
	for (int s = 0; s < g.series.size(); s++) {
		QStringList l{limpa(g.series[s])};
		for (int c = 0; c < g.categorias.size(); c++) {
			const double v = s < int(g.valores.size()) && c < int(g.valores[s].size()) ? g.valores[s][c] : 0;
			l << QString::number(v, 'g', 12);
		}
		partes << l.join(';');
	}
	return "lume-grafico:" + partes.join('|');
}

inline bool lerDados(const QString &txt, DadosGrafico &g)
{
	if (!txt.startsWith("lume-grafico:"))
		return false;
	const QStringList partes = txt.mid(13).split('|');
	if (partes.size() < 2)
		return false;
	const QStringList cab = partes[0].split(';');
	const QString t = cab.value(0);
	g = DadosGrafico();
	g.tipo = t == "barras" ? TipoGrafico::Barras : t == "linhas" ? TipoGrafico::Linhas : t == "pizza" ? TipoGrafico::Pizza : TipoGrafico::Colunas;
	g.titulo = cab.value(1);
	g.legenda = cab.value(2) != "semlegenda";
	g.categorias = partes[1].split(';');
	for (int i = 2; i < partes.size(); i++) {
		const QStringList l = partes[i].split(';');
		g.series << l.value(0);
		std::vector<double> v;
		for (int c = 1; c < l.size(); c++)
			v.push_back(l[c].toDouble());
		v.resize(size_t(g.categorias.size()), 0);
		g.valores.push_back(v);
	}
	return true;
}

// --- objetos ---------------------------------------------------------------

enum class Tipo { Texto, Retangulo, Arredondado, Elipse, Triangulo, Seta, Linha, Imagem, Grafico };
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
	// Imagem e gráfico.
	QString midia;            // a chave em Doc::midias
	DadosGrafico graf;
	QString nome;             // o nome no arquivo (só para guardar)

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
	bool forma() const { return tipo != Tipo::Texto && tipo != Tipo::Imagem && tipo != Tipo::Grafico; }
	bool aceitaTexto() const { return tipo != Tipo::Imagem && tipo != Tipo::Grafico && tipo != Tipo::Linha; }
};

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
		do
			chave = QString("imagem%1.%2").arg(n++).arg(ext.toLower());
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
				usadas << o.midia;
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

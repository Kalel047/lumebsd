// SPDX-License-Identifier: BSD-2-Clause
//
// lume-numeros: a planilha. Células (valor, fórmula, formato), folhas,
// gráficos, o recálculo, o que a pessoa digita e o que aparece, e as
// operações (inserir e apagar linhas e colunas, ordenar, copiar e colar,
// preencher). Sem nada de tela: os arquivos e a janela usam daqui.

#pragma once

#include "formulas.h"

#include <QColor>
#include <QLocale>

#include <map>
#include <optional>

// --- formato ---------------------------------------------------------------

struct Formato {
	enum Tipo { Geral, Numero, Moeda, Porcento, Data, Texto } tipo = Geral;
	int casas = 2;
	bool milhar = true;
	bool negrito = false, italico = false, sublinhado = false;
	QColor cor, fundo;     // inválidas: as do tema
	int alin = 0;          // 0 automático (números à direita), 1 esq, 2 centro, 3 dir
	bool borda = false;    // as quatro bordas
	double tamanho = 0;    // 0: o padrão

	bool operator==(const Formato &o) const
	{
		return tipo == o.tipo && casas == o.casas && milhar == o.milhar && negrito == o.negrito &&
		       italico == o.italico && sublinhado == o.sublinhado && cor == o.cor && fundo == o.fundo &&
		       alin == o.alin && borda == o.borda && tamanho == o.tamanho;
	}
	bool padrao() const { return *this == Formato(); }
};

struct Celula {
	PExpr f;     // a fórmula (nula: um valor digitado)
	Valor v;     // o valor (o calculado, na fórmula)
	Formato fmt;
	bool vazia() const { return !f && v.t == Valor::Vazio; }
};

// --- gráficos --------------------------------------------------------------

struct Grafico {
	enum Tipo { Colunas, Barras, Linhas, Pizza, Dispersao, Area } tipo = Colunas;
	QString titulo;
	int r1 = 0, c1 = 0, r2 = 0, c2 = 0; // os dados, na mesma folha: a 1ª linha
	                                    // tem os nomes das séries, a 1ª coluna
	                                    // as categorias
	double x = 0, y = 0, w = 480, h = 300; // onde fica (px desde o canto de A1)
	std::vector<int> series; // as colunas das séries (vazio: todas depois das categorias)
};

// Como os dados do gráfico se arrumam no retângulo.
struct Arranjo {
	bool cab = false;   // a 1ª linha tem os nomes das séries
	bool cats = false;  // a coluna c1 tem as categorias
	int r0 = 0;         // a primeira linha de dados
	std::vector<int> cols; // as colunas das séries
};

struct Serie {
	QString nome;
	std::vector<double> v;
	std::vector<bool> tem;
};

// --- folha -----------------------------------------------------------------

inline quint64 chave(int r, int c) { return (quint64(quint32(r)) << 20) | quint32(c); }
inline int linhaDe(quint64 k) { return int(k >> 20); }
inline int colunaDeChave(quint64 k) { return int(k & 0xFFFFF); }

static constexpr int LARGURA_PADRAO = 96;  // px
static constexpr int ALTURA_PADRAO = 24;

struct Folha {
	QString nome;
	QHash<quint64, Celula> cel;
	std::map<int, int> largura, altura; // só as que mudaram (px)
	int congelarLinhas = 0, congelarColunas = 0;
	int larguraPadrao = LARGURA_PADRAO; // a das colunas sem largura própria
	std::vector<Grafico> graficos;
	int ultLinha = -1, ultColuna = -1;

	const Celula *em(int r, int c) const
	{
		auto it = cel.find(chave(r, c));
		return it == cel.end() ? nullptr : &*it;
	}
	int larguraDe(int c) const
	{
		auto it = largura.find(c);
		return it == largura.end() ? larguraPadrao : it->second;
	}
	int alturaDe(int r) const
	{
		auto it = altura.find(r);
		return it == altura.end() ? ALTURA_PADRAO : it->second;
	}
	void recontar()
	{
		ultLinha = ultColuna = -1;
		for (auto it = cel.begin(); it != cel.end(); ++it) {
			ultLinha = std::max(ultLinha, linhaDe(it.key()));
			ultColuna = std::max(ultColuna, colunaDeChave(it.key()));
		}
	}
};

// --- o que aparece na célula -----------------------------------------------

inline const QLocale &br()
{
	static const QLocale l(QLocale::Portuguese, QLocale::Brazil);
	return l;
}

inline QString numeroBrasil(double n, int casas, bool milhar)
{
	QString s = br().toString(n, 'f', casas);
	if (!milhar)
		s.remove(br().groupSeparator());
	return s;
}

inline QString mostrar(const Valor &v, const Formato &f)
{
	switch (v.t) {
	case Valor::Vazio:
		return QString();
	case Valor::Texto:
		return v.s;
	case Valor::Logico:
		return v.n ? "VERDADEIRO" : "FALSO";
	case Valor::Erro:
		return erroBrasil(v.s);
	case Valor::Numero:
		break;
	}
	double n = v.n;
	if (n == 0)
		n = 0; // sem o "-0"
	switch (f.tipo) {
	case Formato::Numero:
		return numeroBrasil(n, f.casas, f.milhar);
	case Formato::Moeda:
		return (n < 0 ? "-R$ " : "R$ ") + numeroBrasil(std::fabs(n), f.casas, true);
	case Formato::Porcento:
		return numeroBrasil(n * 100, f.casas, false) + "%";
	case Formato::Data:
		return dataDe(n).toString("dd/MM/yyyy");
	default: {
		// Geral: até 10 algarismos, sem separador de milhar.
		if (std::fabs(n) >= 1e11 || (std::fabs(n) < 1e-5 && n != 0))
			return QString::number(n, 'E', 4).replace('.', ',');
		QString s = QString::number(n, 'f', 10);
		if (s.contains('.')) {
			while (s.endsWith('0'))
				s.chop(1);
			if (s.endsWith('.'))
				s.chop(1);
		}
		// No máximo 10 algarismos significativos.
		const int alg = int(s.count(QRegularExpression("[0-9]")));
		if (alg > 10 && s.contains('.')) {
			const int inteiros = int(s.indexOf('.')) - (s.startsWith('-') ? 1 : 0);
			s = QString::number(n, 'f', std::max(0, 10 - inteiros));
			if (s.contains('.')) {
				while (s.endsWith('0')) s.chop(1);
				if (s.endsWith('.')) s.chop(1);
			}
		}
		return s.replace('.', ',');
	}
	}
}

// O texto para editar: a fórmula em português, ou o valor como se digita.
inline QString paraEditar(const Celula &c)
{
	if (c.f)
		return "=" + textoDe(c.f, Dialeto::Brasil);
	if (c.v.t == Valor::Numero) {
		if (c.fmt.tipo == Formato::Porcento)
			return QString::number(c.v.n * 100, 'g', 15).replace('.', ',') + "%";
		if (c.fmt.tipo == Formato::Data)
			return dataDe(c.v.n).toString("dd/MM/yyyy");
		return QString::number(c.v.n, 'g', 15).replace('.', ',');
	}
	if (c.v.t == Valor::Texto) {
		// Um texto que pareceria número leva o apóstrofo.
		double x;
		if (numeroDeTexto(c.v.s, x) || c.v.s.startsWith('='))
			return "'" + c.v.s;
		return c.v.s;
	}
	return mostrar(c.v, c.fmt);
}

// O que a pessoa digitou: número (1.234,56), porcentagem (12,5%), moeda
// (R$ 10,00), data (21/11/2026), VERDADEIRO/FALSO, fórmula (=SOMA(A1:A3))
// ou texto. Muda o formato quando o jeito de digitar diz (12% fica em %).
struct Interpretado {
	PExpr f;
	Valor v;
	std::optional<Formato::Tipo> tipo;
	int casas = -1;
	QString erro; // fórmula que não deu para entender
};

inline Interpretado interpretar(const QString &digitado, const Formato &atual)
{
	Interpretado r;
	const QString s = digitado.trimmed();
	if (digitado.isEmpty())
		return r;
	if (digitado.startsWith('\'')) {
		r.v = Valor::txt(digitado.mid(1));
		return r;
	}
	if (atual.tipo == Formato::Texto) {
		r.v = Valor::txt(digitado);
		return r;
	}
	if (s.startsWith('=') && s.size() > 1) {
		r.f = analisar(s, Dialeto::Brasil, &r.erro);
		if (!r.f)
			r.v = Valor::erro("#NAME?");
		return r;
	}
	const QString u = s.toUpper();
	if (u == "VERDADEIRO" || u == "FALSO" || u == "TRUE" || u == "FALSE") {
		r.v = Valor::log(u == "VERDADEIRO" || u == "TRUE");
		return r;
	}
	static const QRegularExpression milhar(R"(^[-+]?\d{1,3}(\.\d{3})+(,\d+)?$)");
	static const QRegularExpression simples(R"(^[-+]?\d+(,\d+)?$)");
	static const QRegularExpression ponto(R"(^[-+]?\d*\.\d+$)");
	static const QRegularExpression cientifico(R"(^[-+]?\d+([.,]\d+)?[eE][-+]?\d+$)");
	auto numero = [&](QString t, double &n, int &casas) -> bool {
		t = t.trimmed();
		casas = 0;
		if (milhar.match(t).hasMatch() || simples.match(t).hasMatch()) {
			const int v = int(t.indexOf(','));
			casas = v < 0 ? 0 : int(t.size()) - v - 1;
			t.remove('.').replace(',', '.');
			n = t.toDouble();
			return true;
		}
		if (ponto.match(t).hasMatch() || cientifico.match(t).hasMatch()) {
			t.replace(',', '.');
			const int v = int(t.indexOf('.'));
			casas = v < 0 ? 0 : int(t.size()) - v - 1;
			bool ok;
			n = t.toDouble(&ok);
			return ok;
		}
		return false;
	};
	double n;
	int casas;
	if (s.endsWith('%') && numero(s.chopped(1), n, casas)) {
		r.v = Valor::num(n / 100);
		if (atual.tipo != Formato::Porcento) {
			r.tipo = Formato::Porcento;
			r.casas = casas;
		}
		return r;
	}
	QString m = s;
	bool negativo = false;
	if (m.startsWith('-')) {
		negativo = true;
		m = m.mid(1).trimmed();
	}
	if (m.startsWith("R$")) {
		if (numero(m.mid(2), n, casas)) {
			r.v = Valor::num(negativo ? -n : n);
			if (atual.tipo != Formato::Moeda) {
				r.tipo = Formato::Moeda;
				r.casas = 2;
			}
			return r;
		}
	}
	if (numero(s, n, casas)) {
		r.v = Valor::num(n);
		return r;
	}
	static const QRegularExpression data(R"(^(\d{1,2})/(\d{1,2})(?:/(\d{2,4}))?$)");
	const auto md = data.match(s);
	if (md.hasMatch()) {
		int a = md.captured(3).isEmpty() ? QDate::currentDate().year() : md.captured(3).toInt();
		if (a < 100)
			a += a < 50 ? 2000 : 1900;
		const QDate d(a, md.captured(2).toInt(), md.captured(1).toInt());
		if (d.isValid()) {
			r.v = Valor::num(serialDe(d));
			if (atual.tipo != Formato::Data)
				r.tipo = Formato::Data;
			return r;
		}
	}
	static const QRegularExpression iso(R"(^(\d{4})-(\d{2})-(\d{2})$)");
	const auto mi = iso.match(s);
	if (mi.hasMatch()) {
		const QDate d(mi.captured(1).toInt(), mi.captured(2).toInt(), mi.captured(3).toInt());
		if (d.isValid()) {
			r.v = Valor::num(serialDe(d));
			r.tipo = Formato::Data;
			return r;
		}
	}
	r.v = Valor::txt(digitado);
	return r;
}

// --- a pasta ---------------------------------------------------------------

class Pasta : public Fonte
{
public:
	std::vector<Folha> folhas;
	QString fonte = "IBM Plex Sans";
	double tamanho = 10.5;

	Pasta() { novaFolha(); }

	Folha &folha(int i) { return folhas[size_t(i)]; }
	const Folha &folha(int i) const { return folhas[size_t(i)]; }
	int quantas() const { return int(folhas.size()); }

	int novaFolha(const QString &nome = QString())
	{
		Folha f;
		f.nome = nome.isEmpty() ? nomeLivre() : nome;
		folhas.push_back(f);
		return int(folhas.size()) - 1;
	}

	QString nomeLivre() const
	{
		for (int i = int(folhas.size()) + 1;; i++) {
			const QString n = "Planilha " + QString::number(i);
			if (folhaPorNome(n) < 0)
				return n;
		}
	}

	// --- Fonte ---
	int folhaPorNome(const QString &nome) const override
	{
		for (size_t i = 0; i < folhas.size(); i++)
			if (folhas[i].nome.compare(nome, Qt::CaseInsensitive) == 0)
				return int(i);
		return -1;
	}
	int ultimaLinha(int f) const override { return f >= 0 && f < quantas() ? folha(f).ultLinha : -1; }
	int ultimaColuna(int f) const override { return f >= 0 && f < quantas() ? folha(f).ultColuna : -1; }

	Valor valor(int f, int r, int c) override
	{
		if (f < 0 || f >= quantas())
			return Valor::erro("#REF!");
		Folha &fo = folha(f);
		auto it = fo.cel.find(chave(r, c));
		if (it == fo.cel.end())
			return Valor();
		if (!it->f)
			return it->v;
		if (!calculando_)
			return it->v;
		const quint64 k = (quint64(f) << 48) | chave(r, c);
		const int e = estado_.value(k, 0);
		if (e == 2)
			return it->v;
		if (e == 1)
			return Valor::erro("#CIRC!");
		if (++profundidade_ > 20000) {
			profundidade_--;
			return Valor::erro("#CIRC!");
		}
		estado_.insert(k, 1);
		const PExpr expr = it->f; // a tabela pode mudar de lugar na memória
		Calculo calc(*this, f);
		Valor v = calc.calcular(expr);
		profundidade_--;
		if (v.t == Valor::Vazio)
			v = Valor::num(0);
		// "it" pode ter sido invalidado por inserções no QHash: procura de novo.
		fo.cel[chave(r, c)].v = v;
		estado_.insert(k, 2);
		return v;
	}

	void recalcular()
	{
		calculando_ = true;
		estado_.clear();
		profundidade_ = 0;
		for (int f = 0; f < quantas(); f++) {
			const QList<quint64> ks = folha(f).cel.keys();
			for (quint64 k : ks)
				if (folha(f).cel.value(k).f)
					valor(f, linhaDe(k), colunaDeChave(k));
		}
		calculando_ = false;
	}

	// --- editar ---
	void por(int f, int r, int c, const Celula &cel)
	{
		Folha &fo = folha(f);
		if (cel.vazia() && cel.fmt.padrao())
			fo.cel.remove(chave(r, c));
		else
			fo.cel.insert(chave(r, c), cel);
	}

	// Digitar numa célula. Devolve a mensagem de erro, se a fórmula não fizer sentido.
	QString digitar(int f, int r, int c, const QString &texto)
	{
		const Celula *at = folha(f).em(r, c);
		Celula nova;
		if (at)
			nova.fmt = at->fmt;
		const Interpretado in = interpretar(texto, nova.fmt);
		nova.f = in.f;
		nova.v = in.v;
		if (in.tipo) {
			nova.fmt.tipo = *in.tipo;
			if (in.casas >= 0)
				nova.fmt.casas = *in.tipo == Formato::Porcento ? std::min(in.casas, 4) : in.casas;
		}
		por(f, r, c, nova);
		folha(f).recontar();
		return in.erro;
	}

	// --- linhas e colunas ---
	void inserir(int f, bool colunas, int em, int n)
	{
		Folha &fo = folha(f);
		const QString nome = fo.nome;
		QHash<quint64, Celula> novo;
		for (auto it = fo.cel.begin(); it != fo.cel.end(); ++it) {
			int r = linhaDe(it.key()), c = colunaDeChave(it.key());
			int &v = colunas ? c : r;
			if (n < 0 && v >= em && v < em - n)
				continue; // apagada
			if (v >= em)
				v += n;
			novo.insert(chave(r, c), *it);
		}
		fo.cel = novo;
		auto &medidas = colunas ? fo.largura : fo.altura;
		std::map<int, int> m2;
		for (auto [k, val] : medidas) {
			if (n < 0 && k >= em && k < em - n)
				continue;
			m2[k >= em ? k + n : k] = val;
		}
		medidas = m2;
		for (Grafico &g : fo.graficos) {
			if (colunas && !g.series.empty()) {
				std::vector<int> s2;
				for (int c : g.series) {
					if (n < 0 && c >= em && c < em - n)
						continue;
					s2.push_back(c >= em ? c + n : c);
				}
				g.series = s2;
			}
			int &a1 = colunas ? g.c1 : g.r1;
			int &a2 = colunas ? g.c2 : g.r2;
			if (a1 >= em) a1 = std::max(em, a1 + n);
			if (a2 >= em) a2 = std::max(a1, a2 + n);
		}
		// As fórmulas de todas as folhas.
		for (Folha &o : folhas)
			for (auto it = o.cel.begin(); it != o.cel.end(); ++it)
				if (it->f)
					it->f = comLinhas(it->f, o.nome, nome, colunas, em, n);
		fo.recontar();
	}

	void renomear(int f, const QString &novo)
	{
		const QString antes = folha(f).nome;
		for (Folha &o : folhas)
			for (auto it = o.cel.begin(); it != o.cel.end(); ++it)
				if (it->f)
					it->f = comFolhaRenomeada(it->f, antes, novo);
		folha(f).nome = novo;
	}

	void apagarFolha(int f)
	{
		const QString nome = folha(f).nome;
		folhas.erase(folhas.begin() + f);
		if (folhas.empty())
			novaFolha();
		(void)nome; // as fórmulas que apontavam para ela dão #REF! no cálculo
	}

	// --- copiar e colar ---
	struct Recorte {
		int linhas = 0, colunas = 0;
		int r0 = 0, c0 = 0; // de onde veio (para deslocar as fórmulas)
		std::vector<std::optional<Celula>> cel; // linha a linha
	};

	Recorte copiar(int f, int r1, int c1, int r2, int c2) const
	{
		Recorte x;
		x.linhas = r2 - r1 + 1;
		x.colunas = c2 - c1 + 1;
		x.r0 = r1;
		x.c0 = c1;
		for (int r = r1; r <= r2; r++)
			for (int c = c1; c <= c2; c++) {
				const Celula *p = folha(f).em(r, c);
				x.cel.push_back(p ? std::optional<Celula>(*p) : std::nullopt);
			}
		return x;
	}

	void colar(int f, int r, int c, const Recorte &x, bool soValores = false)
	{
		for (int i = 0; i < x.linhas; i++)
			for (int j = 0; j < x.colunas; j++) {
				const auto &o = x.cel[size_t(i * x.colunas + j)];
				Celula n;
				if (o) {
					n = *o;
					if (n.f) {
						if (soValores)
							n.f = nullptr;
						else
							n.f = deslocada(n.f, r - x.r0, c - x.c0);
					}
				}
				por(f, r + i, c + j, n);
			}
		folha(f).recontar();
	}

	// Preencher: a primeira linha (ou coluna) da seleção vai para as outras.
	void preencher(int f, int r1, int c1, int r2, int c2, bool paraBaixo)
	{
		if (paraBaixo) {
			const Recorte x = copiar(f, r1, c1, r1, c2);
			for (int r = r1 + 1; r <= r2; r++)
				colar(f, r, c1, x);
		} else {
			const Recorte x = copiar(f, r1, c1, r2, c1);
			for (int c = c1 + 1; c <= c2; c++)
				colar(f, r1, c, x);
		}
	}

	// Ordenar as linhas r1..r2 (colunas c1..c2) pela coluna "por".
	void ordenar(int f, int r1, int c1, int r2, int c2, int por, bool crescente)
	{
		Folha &fo = folha(f);
		r2 = std::min(r2, fo.ultLinha);
		if (r2 <= r1)
			return;
		std::vector<int> ordem;
		for (int r = r1; r <= r2; r++)
			ordem.push_back(r);
		auto val = [&](int r) {
			const Celula *p = fo.em(r, por);
			return p ? p->v : Valor();
		};
		std::stable_sort(ordem.begin(), ordem.end(), [&](int a, int b) {
			const Valor x = val(a), y = val(b);
			// Os vazios sempre no fim.
			if (x.t == Valor::Vazio || y.t == Valor::Vazio)
				return x.t != Valor::Vazio && y.t == Valor::Vazio;
			auto ordemTipo = [](const Valor &v) { return v.t == Valor::Numero ? 0 : v.t == Valor::Texto ? 1 : 2; };
			if (ordemTipo(x) != ordemTipo(y))
				return crescente ? ordemTipo(x) < ordemTipo(y) : ordemTipo(x) > ordemTipo(y);
			int c = x.t == Valor::Texto ? QString::localeAwareCompare(x.s.toLower(), y.s.toLower()) : (x.n < y.n ? -1 : x.n > y.n ? 1 : 0);
			return crescente ? c < 0 : c > 0;
		});
		std::vector<Recorte> linhas;
		for (int r : ordem)
			linhas.push_back(copiar(f, r, c1, r, c2));
		for (size_t i = 0; i < linhas.size(); i++)
			colar(f, r1 + int(i), c1, linhas[i]);
	}

	// --- gráficos ---
	Arranjo arranjo(int f, const Grafico &g) const
	{
		const Folha &fo = folha(f);
		auto v = [&](int r, int c) { const Celula *p = fo.em(r, c); return p ? p->v : Valor(); };
		Arranjo a;
		// A primeira linha é de nomes se não tiver números (fora a 1ª coluna).
		bool cab = true;
		for (int c = g.c1 + 1; c <= g.c2; c++)
			if (v(g.r1, c).t == Valor::Numero)
				cab = false;
		if (g.r1 == g.r2)
			cab = false;
		// A primeira coluna é de categorias se não tiver números, ou se for
		// a única além dos dados (anos, por exemplo, numa coluna de 2+).
		bool cats = g.c2 > g.c1;
		if (cats && g.tipo != Grafico::Dispersao) {
			bool soNum = true;
			for (int r = g.r1 + (cab ? 1 : 0); r <= g.r2; r++)
				if (v(r, g.c1).t != Valor::Numero && v(r, g.c1).t != Valor::Vazio)
					soNum = false;
			if (soNum && g.c2 - g.c1 >= 1) {
				// Números na 1ª coluna: categorias se parecerem anos ou datas.
				const Celula *p = fo.em(g.r1 + (cab ? 1 : 0), g.c1);
				const double x = p ? p->v.n : 0;
				cats = (p && p->fmt.tipo == Formato::Data) || (x >= 1900 && x <= 2200 && x == std::floor(x));
				// Ou se forem períodos: inteiros em sequência (0, 1, 2... ou
				// 1, 2, 3...) debaixo de um título (Ano, Mês, Período).
				if (!cats && cab) {
					bool seq = true;
					int n = 0;
					for (int r = g.r1 + 1; r <= g.r2 && seq; r++, n++) {
						const Valor w = v(r, g.c1);
						seq = w.t == Valor::Numero && w.n == x + n;
					}
					cats = seq && n >= 2 && x == std::floor(x);
				}
			}
		}
		if (!g.series.empty())
			cats = std::find(g.series.begin(), g.series.end(), g.c1) == g.series.end();
		a.cab = cab;
		a.cats = cats;
		a.r0 = g.r1 + (cab ? 1 : 0);
		if (!g.series.empty())
			a.cols = g.series;
		else
			for (int c = g.c1 + (cats ? 1 : 0); c <= g.c2; c++)
				a.cols.push_back(c);
		return a;
	}

	void dadosDoGrafico(int f, const Grafico &g, QStringList &categorias, std::vector<Serie> &series) const
	{
		const Folha &fo = folha(f);
		auto v = [&](int r, int c) { const Celula *p = fo.em(r, c); return p ? p->v : Valor(); };
		auto txt = [&](int r, int c) { const Celula *p = fo.em(r, c); return p ? mostrar(p->v, p->fmt) : QString(); };
		const Arranjo a = arranjo(f, g);
		const bool cab = a.cab, cats = a.cats;
		const int r0 = a.r0;
		categorias.clear();
		series.clear();
		for (int r = r0; r <= g.r2; r++)
			categorias << (cats ? txt(r, g.c1) : QString::number(r - r0 + 1));
		int n = 0;
		for (int c : a.cols) {
			Serie s;
			s.nome = cab ? txt(g.r1, c) : "Série " + QString::number(++n);
			for (int r = r0; r <= g.r2; r++) {
				const Valor x = v(r, c);
				s.v.push_back(x.t == Valor::Numero ? x.n : 0);
				s.tem.push_back(x.t == Valor::Numero);
			}
			series.push_back(s);
		}
	}

	// A posição (px) de uma linha e de uma coluna, desde A1.
	double xDe(int f, int c) const
	{
		double x = 0;
		for (int i = 0; i < c; i++)
			x += folha(f).larguraDe(i);
		return x;
	}
	double yDe(int f, int r) const
	{
		const Folha &fo = folha(f);
		double y = double(r) * ALTURA_PADRAO;
		for (auto [k, h] : fo.altura)
			if (k < r)
				y += h - ALTURA_PADRAO;
		return y;
	}
	// O contrário: a célula que contém o ponto (e o resto, em px).
	void celulaEm(int f, double x, double y, int &r, int &c, double &dx, double &dy) const
	{
		const Folha &fo = folha(f);
		c = 0;
		double acc = 0;
		while (acc + fo.larguraDe(c) <= x && c < MAX_COLUNAS - 1)
			acc += fo.larguraDe(c++);
		dx = x - acc;
		r = 0;
		acc = 0;
		while (acc + fo.alturaDe(r) <= y && r < MAX_LINHAS - 1)
			acc += fo.alturaDe(r++);
		dy = y - acc;
	}

private:
	bool calculando_ = false;
	int profundidade_ = 0;
	QHash<quint64, int> estado_;
};

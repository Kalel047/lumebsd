// SPDX-License-Identifier: BSD-2-Clause
//
// lume-numeros: as fórmulas. Um analisador (texto -> árvore), o cálculo e a
// volta para o texto em três dialetos:
//
//   Canonico  o do arquivo .xlsx: nomes em inglês, "," entre os
//             argumentos, "." decimal  (=SUM(A1:A3,0.5))
//   Brasil    o que a pessoa digita e vê: nomes em português, ";" e ","
//             (=SOMA(A1:A3;0,5)), como o Excel e o LibreOffice em pt-BR
//   Odf       o do .ods: of:=SUM([.A1:.A3];0.5)
//
// A árvore guarda as referências (A1, $B$2, Plan2!C3, A1:B9, A:A) com o
// que é absoluto; copiar e colar, inserir e apagar linhas e colunas
// deslocam só o que é relativo (ou o que fica depois do ponto).

#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

// --- valores ---------------------------------------------------------------

struct Valor {
	enum Tipo { Vazio, Numero, Texto, Logico, Erro } t = Vazio;
	double n = 0;
	QString s; // o texto, ou o código do erro (em inglês, como no arquivo)

	static Valor num(double x)
	{
		Valor v;
		if (!std::isfinite(x))
			return erro("#NUM!");
		v.t = Numero;
		v.n = x;
		return v;
	}
	static Valor txt(const QString &x)
	{
		Valor v;
		v.t = Texto;
		v.s = x;
		return v;
	}
	static Valor log(bool b)
	{
		Valor v;
		v.t = Logico;
		v.n = b ? 1 : 0;
		return v;
	}
	static Valor erro(const QString &c)
	{
		Valor v;
		v.t = Erro;
		v.s = c;
		return v;
	}
	bool operator==(const Valor &o) const { return t == o.t && n == o.n && s == o.s; }
};

// Os erros: no arquivo em inglês, na tela como o Excel em português.
inline QString erroBrasil(const QString &c)
{
	static const QHash<QString, QString> m = {
		{"#VALUE!", "#VALOR!"}, {"#NAME?", "#NOME?"}, {"#N/A", "#N/D"}, {"#NUM!", "#NÚM!"},
		{"#NULL!", "#NULO!"}, {"#CIRC!", "#CICLO!"}};
	return m.value(c, c);
}
inline QString erroCanonico(const QString &c)
{
	static const QHash<QString, QString> m = {
		{"#VALOR!", "#VALUE!"}, {"#NOME?", "#NAME?"}, {"#N/D", "#N/A"}, {"#NÚM!", "#NUM!"},
		{"#NULO!", "#NULL!"}, {"#CICLO!", "#CIRC!"}};
	return m.value(c.toUpper(), c.toUpper());
}

// As datas são números de dias desde 30/12/1899 (como no Excel e no ODF).
inline QDate dataBase() { return QDate(1899, 12, 30); }
inline double serialDe(const QDate &d) { return dataBase().daysTo(d); }
inline QDate dataDe(double serial) { return dataBase().addDays(qint64(std::floor(serial))); }

// --- colunas ---------------------------------------------------------------

inline QString nomeColuna(int c)
{
	QString r;
	c++;
	while (c > 0) {
		const int m = (c - 1) % 26;
		r.prepend(QChar('A' + m));
		c = (c - 1) / 26;
	}
	return r;
}

inline int colunaDe(const QString &s)
{
	int c = 0;
	for (QChar ch : s) {
		const int v = ch.toUpper().unicode() - 'A' + 1;
		if (v < 1 || v > 26)
			return -1;
		c = c * 26 + v;
	}
	return c - 1;
}

inline QString nomeCelula(int r, int c) { return nomeColuna(c) + QString::number(r + 1); }

// --- a árvore --------------------------------------------------------------

struct Expr;
using PExpr = std::shared_ptr<Expr>;

struct Expr {
	enum K { Num, Str, Bool, Err, Ref, Area, Func, Un, Bin, Pct } k = Num;
	double n = 0;
	QString s;      // texto, código do erro, nome da função (canônico), operador
	QString folha;  // Ref/Area: vazio é a folha da própria célula
	int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
	bool ar1 = false, ac1 = false, ar2 = false, ac2 = false; // absolutos ($)
	bool colunas = false; // A:A (as linhas inteiras)
	std::vector<PExpr> a;
};

static constexpr int MAX_LINHAS = 1048576;
static constexpr int MAX_COLUNAS = 16384;

// --- os nomes das funções --------------------------------------------------

struct Catalogo {
	QHash<QString, QString> deBrasil;  // ÉNÚM -> ISNUMBER
	QHash<QString, QString> paraBrasil; // ISNUMBER -> ÉNÚM
	QStringList nomes;                 // os canônicos, em ordem (para a ajuda)
};

inline QString semAcento(QString s)
{
	s = s.normalized(QString::NormalizationForm_D);
	s.remove(QRegularExpression("[\\x{0300}-\\x{036f}]"));
	return s;
}

inline const Catalogo &catalogo()
{
	static Catalogo c = [] {
		Catalogo k;
		const char *pares[][2] = {
			{"SOMA", "SUM"}, {"MÉDIA", "AVERAGE"}, {"MÍNIMO", "MIN"}, {"MÁXIMO", "MAX"},
			{"CONT.NÚM", "COUNT"}, {"CONT.VALORES", "COUNTA"}, {"CONTAR.VAZIO", "COUNTBLANK"},
			{"SE", "IF"}, {"SEERRO", "IFERROR"}, {"E", "AND"}, {"OU", "OR"}, {"NÃO", "NOT"},
			{"VERDADEIRO", "TRUE"}, {"FALSO", "FALSE"},
			{"ARRED", "ROUND"}, {"ARREDONDAR.PARA.CIMA", "ROUNDUP"}, {"ARREDONDAR.PARA.BAIXO", "ROUNDDOWN"},
			{"TRUNCAR", "TRUNC"}, {"INT", "INT"}, {"ABS", "ABS"}, {"RAIZ", "SQRT"}, {"POTÊNCIA", "POWER"},
			{"EXP", "EXP"}, {"LN", "LN"}, {"LOG10", "LOG10"}, {"LOG", "LOG"}, {"MOD", "MOD"}, {"PI", "PI"},
			{"SINAL", "SIGN"}, {"PRODUTO", "PRODUCT"}, {"SOMAQUAD", "SUMSQ"},
			{"ALEATÓRIO", "RAND"}, {"ALEATÓRIOENTRE", "RANDBETWEEN"},
			{"SOMASE", "SUMIF"}, {"CONT.SE", "COUNTIF"}, {"MÉDIASE", "AVERAGEIF"},
			{"SOMASES", "SUMIFS"}, {"CONT.SES", "COUNTIFS"}, {"SOMARPRODUTO", "SUMPRODUCT"},
			{"PROCV", "VLOOKUP"}, {"PROCH", "HLOOKUP"}, {"ÍNDICE", "INDEX"}, {"CORRESP", "MATCH"},
			{"HOJE", "TODAY"}, {"AGORA", "NOW"}, {"DATA", "DATE"}, {"ANO", "YEAR"}, {"MÊS", "MONTH"},
			{"DIA", "DAY"}, {"DIA.DA.SEMANA", "WEEKDAY"}, {"DIAS", "DAYS"}, {"DATAM", "EDATE"},
			{"CONCATENAR", "CONCATENATE"}, {"CONCAT", "CONCAT"}, {"ESQUERDA", "LEFT"}, {"DIREITA", "RIGHT"},
			{"EXT.TEXTO", "MID"}, {"NÚM.CARACT", "LEN"}, {"MAIÚSCULA", "UPPER"}, {"MINÚSCULA", "LOWER"},
			{"ARRUMAR", "TRIM"}, {"TEXTO", "TEXT"}, {"VALOR", "VALUE"}, {"SUBSTITUIR", "SUBSTITUTE"},
			{"VPL", "NPV"}, {"TIR", "IRR"}, {"PGTO", "PMT"}, {"VP", "PV"}, {"VF", "FV"}, {"TAXA", "RATE"},
			{"NPER", "NPER"}, {"IPGTO", "IPMT"}, {"PPGTO", "PPMT"},
			{"DESVPAD", "STDEV"}, {"DESVPAD.A", "STDEV.S"}, {"DESVPADP", "STDEVP"}, {"DESVPAD.P", "STDEV.P"},
			{"VAR", "VAR"}, {"VAR.A", "VAR.S"}, {"VARP", "VARP"}, {"VAR.P", "VAR.P"},
			{"MED", "MEDIAN"}, {"MODO", "MODE"}, {"CORREL", "CORREL"}, {"INCLINAÇÃO", "SLOPE"},
			{"INTERCEPÇÃO", "INTERCEPT"}, {"RQUAD", "RSQ"}, {"PREVISÃO", "FORECAST"},
			{"PREVISÃO.LINEAR", "FORECAST.LINEAR"}, {"MAIOR", "LARGE"}, {"MENOR", "SMALL"},
			{"ORDEM", "RANK"}, {"COVAR", "COVAR"}, {"PEARSON", "PEARSON"},
			{"ÉCÉL.VAZIA", "ISBLANK"}, {"ÉNÚM", "ISNUMBER"}, {"ÉTEXTO", "ISTEXT"}, {"ÉERRO", "ISERROR"},
			{"ÉERROS", "ISERR"}, {"NÃO.DISP", "NA"},
		};
		for (const auto &p : pares) {
			const QString br = QString::fromUtf8(p[0]), en = p[1];
			k.deBrasil.insert(br, en);
			k.deBrasil.insert(semAcento(br), en);
			k.paraBrasil.insert(en, br);
			k.nomes << en;
		}
		return k;
	}();
	return c;
}

// As do Excel 2010 em diante vão no arquivo com "_xlfn.".
inline bool precisaXlfn(const QString &f)
{
	static const QStringList l = {"STDEV.S", "STDEV.P", "VAR.S", "VAR.P", "CONCAT", "FORECAST.LINEAR", "DAYS"};
	return l.contains(f);
}

// --- o analisador ----------------------------------------------------------

enum class Dialeto { Canonico, Brasil, Odf };

class Analisador
{
public:
	Analisador(const QString &t, Dialeto d) : t_(t), d_(d) {}

	// O texto sem o "=" do começo (e sem o "of:" do ODF).
	PExpr analisar(QString *erro)
	{
		try {
			pular();
			PExpr e = comparacao();
			pular();
			if (i_ < t_.size())
				throw QString("sobra \"" + t_.mid(i_, 10) + "\"");
			return e;
		} catch (const QString &m) {
			if (erro)
				*erro = m;
			return nullptr;
		}
	}

private:
	QChar sep() const { return d_ == Dialeto::Brasil ? ';' : (d_ == Dialeto::Odf ? ';' : ','); }
	QChar dec() const { return d_ == Dialeto::Brasil ? ',' : '.'; }
	void pular()
	{
		while (i_ < t_.size() && t_[i_].isSpace())
			i_++;
	}
	bool ve(const QString &s)
	{
		pular();
		if (t_.mid(i_, s.size()).compare(s, Qt::CaseInsensitive) == 0) {
			i_ += int(s.size());
			return true;
		}
		return false;
	}
	static PExpr no(Expr::K k)
	{
		auto e = std::make_shared<Expr>();
		e->k = k;
		return e;
	}
	static PExpr bin(const QString &op, PExpr a, PExpr b)
	{
		auto e = no(Expr::Bin);
		e->s = op;
		e->a = {std::move(a), std::move(b)};
		return e;
	}

	PExpr comparacao()
	{
		PExpr e = concatenacao();
		for (;;) {
			pular();
			QString op;
			for (const char *o : {"<>", "<=", ">=", "=", "<", ">"})
				if (t_.mid(i_, int(strlen(o))) == o) {
					op = o;
					break;
				}
			if (op.isEmpty())
				return e;
			i_ += int(op.size());
			e = bin(op, e, concatenacao());
		}
	}
	PExpr concatenacao()
	{
		PExpr e = soma();
		while (ve("&"))
			e = bin("&", e, soma());
		return e;
	}
	PExpr soma()
	{
		PExpr e = produto();
		for (;;) {
			pular();
			if (i_ < t_.size() && (t_[i_] == '+' || t_[i_] == '-')) {
				const QString op = t_[i_++];
				e = bin(op, e, produto());
			} else {
				return e;
			}
		}
	}
	PExpr produto()
	{
		PExpr e = potencia();
		for (;;) {
			pular();
			if (i_ < t_.size() && (t_[i_] == '*' || t_[i_] == '/')) {
				const QString op = t_[i_++];
				e = bin(op, e, potencia());
			} else {
				return e;
			}
		}
	}
	PExpr potencia()
	{
		PExpr e = unario();
		while (ve("^"))
			e = bin("^", e, unario());
		return e;
	}
	PExpr unario()
	{
		pular();
		if (i_ < t_.size() && (t_[i_] == '-' || t_[i_] == '+')) {
			auto e = no(Expr::Un);
			e->s = t_[i_++];
			e->a = {unario()};
			return e;
		}
		PExpr e = primario();
		while (ve("%")) {
			auto p = no(Expr::Pct);
			p->a = {e};
			e = p;
		}
		return e;
	}

	// Uma célula: $A$1 (sem a folha). Devolve false se não for.
	bool celula(int &r, int &c, bool &ar, bool &ac, bool soColuna = false)
	{
		int j = i_;
		ac = j < t_.size() && t_[j] == '$';
		if (ac)
			j++;
		const int ini = j;
		while (j < t_.size() && j - ini < 3 && t_[j].isLetter() && t_[j].unicode() < 128)
			j++;
		if (j == ini)
			return false;
		c = colunaDe(t_.mid(ini, j - ini));
		if (c < 0 || c >= MAX_COLUNAS)
			return false;
		if (soColuna) {
			if (j < t_.size() && (t_[j].isLetterOrNumber() || t_[j] == '$' || t_[j] == '('))
				return false;
			ar = false;
			r = -1;
			i_ = j;
			return true;
		}
		ar = j < t_.size() && t_[j] == '$';
		if (ar)
			j++;
		const int ni = j;
		while (j < t_.size() && t_[j].isDigit())
			j++;
		if (j == ni)
			return false;
		// "A1(" é uma função (LOG10, ATAN2...), não uma célula.
		if (j < t_.size() && (t_[j].isLetter() || t_[j] == '(' || t_[j] == '.' || t_[j] == '_'))
			return false;
		r = t_.mid(ni, j - ni).toInt() - 1;
		if (r < 0 || r >= MAX_LINHAS)
			return false;
		i_ = j;
		return true;
	}

	// Uma referência: [folha!]A1[:B2] ou A:B. Devolve nullptr se não for.
	PExpr referencia()
	{
		const int ini = i_;
		QString folha;
		// 'Nome com espaço'!A1  ou  Plan2!A1
		if (t_[i_] == '\'') {
			int j = i_ + 1;
			QString n;
			while (j < t_.size()) {
				if (t_[j] == '\'') {
					if (j + 1 < t_.size() && t_[j + 1] == '\'') {
						n += '\'';
						j += 2;
						continue;
					}
					break;
				}
				n += t_[j++];
			}
			if (j + 1 < t_.size() && t_[j + 1] == '!') {
				folha = n;
				i_ = j + 2;
			} else {
				return nullptr;
			}
		} else {
			int j = i_;
			while (j < t_.size() && (t_[j].isLetterOrNumber() || t_[j] == '_' || t_[j] == '.'))
				j++;
			if (j < t_.size() && j > i_ && t_[j] == '!') {
				folha = t_.mid(i_, j - i_);
				i_ = j + 1;
			}
		}
		auto e = no(Expr::Ref);
		e->folha = folha;
		const int antes = i_;
		if (celula(e->r1, e->c1, e->ar1, e->ac1)) {
			if (i_ < t_.size() && t_[i_] == ':') {
				const int k = i_;
				i_++;
				if (celula(e->r2, e->c2, e->ar2, e->ac2)) {
					e->k = Expr::Area;
					return e;
				}
				i_ = k;
			}
			return e;
		}
		i_ = antes;
		// A:B, as colunas inteiras.
		int r;
		bool ar;
		if (celula(r, e->c1, ar, e->ac1, true) && i_ < t_.size() && t_[i_] == ':') {
			i_++;
			if (celula(r, e->c2, ar, e->ac2, true)) {
				e->k = Expr::Area;
				e->colunas = true;
				e->r1 = 0;
				e->r2 = MAX_LINHAS - 1;
				return e;
			}
		}
		i_ = ini;
		return nullptr;
	}

	// [.A1], [.A1:.B2], [$Folha.A1], ['Nome'.A1:.B2]  (ODF)
	PExpr referenciaOdf()
	{
		const int fim = int(t_.indexOf(']', i_));
		if (fim < 0)
			throw QString("falta o ]");
		QString dentro = t_.mid(i_ + 1, fim - i_ - 1);
		i_ = fim + 1;
		auto parte = [](QString p, QString &folha, QString &cel) {
			p = p.trimmed();
			if (p.startsWith('$'))
				p = p.mid(1);
			int ponto;
			if (p.startsWith('\'')) {
				const int q = int(p.indexOf("'.", 1));
				folha = p.mid(1, q - 1).replace("''", "'");
				ponto = q + 1;
			} else {
				ponto = int(p.lastIndexOf('.'));
				folha = p.left(std::max(0, ponto));
			}
			cel = p.mid(ponto + 1);
		};
		const QStringList partes = dentro.split(':');
		QString f1, c1, f2, c2;
		parte(partes[0], f1, c1);
		Analisador a1(c1, Dialeto::Canonico);
		auto e = no(Expr::Ref);
		e->folha = f1;
		if (!a1.celula(e->r1, e->c1, e->ar1, e->ac1)) {
			// [.A:.A]
			Analisador ac(c1, Dialeto::Canonico);
			int r;
			bool ar;
			if (partes.size() == 2 && ac.celula(r, e->c1, ar, e->ac1, true)) {
				parte(partes[1], f2, c2);
				Analisador ac2(c2, Dialeto::Canonico);
				if (ac2.celula(r, e->c2, ar, e->ac2, true)) {
					e->k = Expr::Area;
					e->colunas = true;
					e->r2 = MAX_LINHAS - 1;
					return e;
				}
			}
			auto er = no(Expr::Err);
			er->s = "#REF!";
			return er;
		}
		if (partes.size() == 2) {
			parte(partes[1], f2, c2);
			Analisador a2(c2, Dialeto::Canonico);
			if (a2.celula(e->r2, e->c2, e->ar2, e->ac2))
				e->k = Expr::Area;
		}
		return e;
	}

	PExpr primario()
	{
		pular();
		if (i_ >= t_.size())
			throw QString("a fórmula acabou no meio");
		const QChar ch = t_[i_];
		if (ch == '(') {
			i_++;
			PExpr e = comparacao();
			if (!ve(")"))
				throw QString("falta fechar um parêntese");
			return e;
		}
		if (ch == '"') {
			QString s;
			i_++;
			while (i_ < t_.size()) {
				if (t_[i_] == '"') {
					if (i_ + 1 < t_.size() && t_[i_ + 1] == '"') {
						s += '"';
						i_ += 2;
						continue;
					}
					break;
				}
				s += t_[i_++];
			}
			if (i_ >= t_.size())
				throw QString("falta fechar as aspas");
			i_++;
			auto e = no(Expr::Str);
			e->s = s;
			return e;
		}
		if (ch == '#') {
			static const QStringList erros = {"#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#N/A", "#NUM!", "#NULL!",
			                                  "#VALOR!", "#NOME?", "#N/D", "#NÚM!", "#NULO!"};
			for (const QString &x : erros)
				if (t_.mid(i_, x.size()).compare(x, Qt::CaseInsensitive) == 0) {
					i_ += int(x.size());
					auto e = no(Expr::Err);
					e->s = erroCanonico(x);
					return e;
				}
			throw QString("erro desconhecido");
		}
		if (ch == '[' && d_ == Dialeto::Odf)
			return referenciaOdf();
		if (ch.isDigit() || ch == dec()) {
			int j = i_;
			while (j < t_.size() && t_[j].isDigit())
				j++;
			if (j < t_.size() && t_[j] == dec()) {
				j++;
				while (j < t_.size() && t_[j].isDigit())
					j++;
			}
			if (j < t_.size() && (t_[j] == 'e' || t_[j] == 'E') && j + 1 < t_.size() &&
			    (t_[j + 1].isDigit() || ((t_[j + 1] == '+' || t_[j + 1] == '-') && j + 2 < t_.size() && t_[j + 2].isDigit()))) {
				j += 2;
				while (j < t_.size() && t_[j].isDigit())
					j++;
			}
			// "1:1" (linhas inteiras) não: é um número.
			QString num = t_.mid(i_, j - i_);
			if (dec() != '.')
				num.replace(dec(), '.');
			bool ok;
			const double v = num.toDouble(&ok);
			if (!ok)
				throw QString("número estranho");
			i_ = j;
			auto e = no(Expr::Num);
			e->n = v;
			return e;
		}
		if (ch.isLetter() || ch == '$' || ch == '\'' || ch == '_') {
			if (PExpr r = referencia())
				return r;
			// Um nome: função ou VERDADEIRO/FALSO.
			int j = i_;
			while (j < t_.size() && (t_[j].isLetterOrNumber() || t_[j] == '.' || t_[j] == '_'))
				j++;
			QString nome = t_.mid(i_, j - i_).toUpper();
			i_ = j;
			if (nome.startsWith("_XLFN."))
				nome = nome.mid(6);
			if (nome.startsWith("COM.MICROSOFT."))
				nome = nome.mid(14);
			pular();
			const bool chamada = i_ < t_.size() && t_[i_] == '(';
			QString can = nome;
			if (d_ == Dialeto::Brasil) {
				const auto &k = catalogo().deBrasil;
				if (k.contains(nome))
					can = k.value(nome);
				else if (k.contains(semAcento(nome)))
					can = k.value(semAcento(nome));
			}
			if (!chamada) {
				if (can == "TRUE" || can == "FALSE") {
					auto e = no(Expr::Bool);
					e->n = can == "TRUE";
					return e;
				}
				throw QString("não conheço \"" + nome + "\"");
			}
			i_++;
			auto e = no(Expr::Func);
			e->s = can;
			pular();
			if (ve(")")) {
				// TRUE() e FALSE() (o ODF escreve assim) são os valores.
				if (can == "TRUE" || can == "FALSE") {
					auto b = no(Expr::Bool);
					b->n = can == "TRUE";
					return b;
				}
				return e;
			}
			for (;;) {
				pular();
				// Argumento vazio: f(1;;2)
				if (i_ < t_.size() && (t_[i_] == sep() || t_[i_] == ')')) {
					e->a.push_back(no(Expr::Str)); // vazio
					e->a.back()->k = Expr::Err;
					e->a.back()->s = "";
				} else {
					e->a.push_back(comparacao());
				}
				pular();
				if (i_ < t_.size() && t_[i_] == sep()) {
					i_++;
					continue;
				}
				if (ve(")"))
					break;
				throw QString("esperava \"" + QString(sep()) + "\" ou \")\" em " + nome);
			}
			return e;
		}
		throw QString("não entendi \"" + QString(ch) + "\"");
	}

	QString t_;
	int i_ = 0;
	Dialeto d_;
};

inline PExpr analisar(const QString &formula, Dialeto d, QString *erro = nullptr)
{
	QString f = formula.trimmed();
	if (f.startsWith("of:", Qt::CaseInsensitive))
		f = f.mid(3);
	if (f.startsWith('='))
		f = f.mid(1);
	Analisador a(f, d);
	return a.analisar(erro);
}

// --- de volta ao texto -----------------------------------------------------

inline QString folhaTexto(const QString &f, bool odf)
{
	static const QRegularExpression simples("^[A-Za-z_][A-Za-z0-9_]*$");
	const bool pareceCelula = QRegularExpression("^[A-Za-z]{1,3}[0-9]+$").match(f).hasMatch();
	if (simples.match(f).hasMatch() && !pareceCelula)
		return odf ? "$" + f : f;
	return (odf ? "$'" : "'") + QString(f).replace("'", "''") + "'";
}

inline QString numeroTexto(double n, Dialeto d)
{
	QString s = QString::number(n, 'g', 15);
	if (d == Dialeto::Brasil)
		s.replace('.', ',');
	return s;
}

inline QString textoDe(const PExpr &e, Dialeto d, bool xlfn = false);

inline QString celulaTexto(int r, int c, bool ar, bool ac)
{
	return (ac ? "$" : "") + nomeColuna(c) + (ar ? "$" : "") + QString::number(r + 1);
}

inline QString refTexto(const Expr &e, Dialeto d)
{
	if (e.k == Expr::Ref && (e.r1 < 0 || e.c1 < 0))
		return d == Dialeto::Brasil ? "#REF!" : "#REF!";
	if (d == Dialeto::Odf) {
		const QString pre = e.folha.isEmpty() ? "." : folhaTexto(e.folha, true) + ".";
		if (e.k == Expr::Ref)
			return "[" + pre + celulaTexto(e.r1, e.c1, e.ar1, e.ac1) + "]";
		if (e.colunas)
			return "[" + pre + (e.ac1 ? "$" : "") + nomeColuna(e.c1) + ":." + (e.ac2 ? "$" : "") + nomeColuna(e.c2) + "]";
		return "[" + pre + celulaTexto(e.r1, e.c1, e.ar1, e.ac1) + ":." + celulaTexto(e.r2, e.c2, e.ar2, e.ac2) + "]";
	}
	const QString pre = e.folha.isEmpty() ? QString() : folhaTexto(e.folha, false) + "!";
	if (e.k == Expr::Ref)
		return pre + celulaTexto(e.r1, e.c1, e.ar1, e.ac1);
	if (e.colunas)
		return pre + (e.ac1 ? "$" : "") + nomeColuna(e.c1) + ":" + (e.ac2 ? "$" : "") + nomeColuna(e.c2);
	return pre + celulaTexto(e.r1, e.c1, e.ar1, e.ac1) + ":" + celulaTexto(e.r2, e.c2, e.ar2, e.ac2);
}

inline int precedencia(const QString &op)
{
	if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") return 1;
	if (op == "&") return 2;
	if (op == "+" || op == "-") return 3;
	if (op == "*" || op == "/") return 4;
	if (op == "^") return 5;
	return 9;
}

inline QString textoDe(const PExpr &e, Dialeto d, bool xlfn)
{
	if (!e)
		return QString();
	const QString sep = d == Dialeto::Canonico ? "," : ";";
	switch (e->k) {
	case Expr::Num:
		return numeroTexto(e->n, d);
	case Expr::Str:
		return "\"" + QString(e->s).replace("\"", "\"\"") + "\"";
	case Expr::Bool:
		if (d == Dialeto::Brasil)
			return e->n ? "VERDADEIRO" : "FALSO";
		return d == Dialeto::Odf ? (e->n ? "TRUE()" : "FALSE()") : (e->n ? "TRUE" : "FALSE");
	case Expr::Err:
		if (e->s.isEmpty())
			return QString(); // argumento vazio
		return d == Dialeto::Brasil ? erroBrasil(e->s) : e->s;
	case Expr::Ref:
	case Expr::Area:
		return refTexto(*e, d);
	case Expr::Func: {
		QString nome = e->s;
		if (d == Dialeto::Brasil)
			nome = catalogo().paraBrasil.value(e->s, e->s);
		else if (d == Dialeto::Odf) {
			static const QHash<QString, QString> odf = {{"STDEV.S", "STDEV"}, {"STDEV.P", "STDEVP"}, {"VAR.S", "VAR"},
			                                            {"VAR.P", "VARP"}, {"FORECAST.LINEAR", "FORECAST"},
			                                            {"CONCAT", "COM.MICROSOFT.CONCAT"}};
			nome = odf.value(e->s, e->s);
		} else if (xlfn && precisaXlfn(e->s))
			nome = "_xlfn." + e->s;
		QStringList as;
		for (const PExpr &x : e->a)
			as << textoDe(x, d, xlfn);
		return nome + "(" + as.join(sep) + ")";
	}
	case Expr::Un:
	case Expr::Pct: {
		QString t = textoDe(e->a[0], d, xlfn);
		if (e->a[0]->k == Expr::Bin || (e->k == Expr::Pct && e->a[0]->k == Expr::Un))
			t = "(" + t + ")";
		return e->k == Expr::Un ? e->s + t : t + "%";
	}
	case Expr::Bin: {
		auto lado = [&](const PExpr &x, bool direita) {
			QString t = textoDe(x, d, xlfn);
			if (x->k == Expr::Bin) {
				const int px = precedencia(x->s), pe = precedencia(e->s);
				if (px < pe || (direita && px == pe))
					t = "(" + t + ")";
			}
			return t;
		};
		return lado(e->a[0], false) + e->s + lado(e->a[1], true);
	}
	}
	return QString();
}

// --- deslocar as referências -----------------------------------------------

// Copiar de uma célula para outra: o relativo anda (dr, dc). Fora da folha:
// #REF!.
inline PExpr deslocada(const PExpr &e, int dr, int dc)
{
	if (!e)
		return e;
	auto x = std::make_shared<Expr>(*e);
	if (x->k == Expr::Ref || x->k == Expr::Area) {
		auto anda = [&](int &v, bool abs, int d, int max) {
			if (!abs)
				v += d;
			return v >= 0 && v < max;
		};
		bool ok = anda(x->c1, x->ac1, dc, MAX_COLUNAS);
		if (!x->colunas)
			ok = anda(x->r1, x->ar1, dr, MAX_LINHAS) && ok;
		if (x->k == Expr::Area) {
			ok = anda(x->c2, x->ac2, dc, MAX_COLUNAS) && ok;
			if (!x->colunas)
				ok = anda(x->r2, x->ar2, dr, MAX_LINHAS) && ok;
		}
		if (!ok) {
			auto er = std::make_shared<Expr>();
			er->k = Expr::Err;
			er->s = "#REF!";
			return er;
		}
		return x;
	}
	for (PExpr &a : x->a)
		a = deslocada(a, dr, dc);
	return x;
}

// Inserir (n > 0) ou apagar (n < 0) linhas (ou colunas) a partir de "em",
// na folha "folha" (as fórmulas de outras folhas que apontam para ela
// também mudam). As referências depois do ponto andam, absolutas ou não; as
// que caem nas linhas apagadas viram #REF!.
inline PExpr comLinhas(const PExpr &e, const QString &folhaDaFormula, const QString &folha, bool colunas, int em, int n)
{
	if (!e)
		return e;
	auto x = std::make_shared<Expr>(*e);
	if (x->k == Expr::Ref || x->k == Expr::Area) {
		const QString alvo = x->folha.isEmpty() ? folhaDaFormula : x->folha;
		if (alvo != folha || (x->colunas && !colunas))
			return x;
		auto anda = [&](int &v) -> bool {
			if (v < em)
				return true;
			if (n < 0 && v < em - n)
				return false; // apagada
			v += n;
			return true;
		};
		if (x->k == Expr::Ref) {
			if (!anda(colunas ? x->c1 : x->r1)) {
				auto er = std::make_shared<Expr>();
				er->k = Expr::Err;
				er->s = "#REF!";
				return er;
			}
			return x;
		}
		int &a1 = colunas ? x->c1 : x->r1;
		int &a2 = colunas ? x->c2 : x->r2;
		if (n > 0) {
			if (a1 >= em) a1 += n;
			if (a2 >= em) a2 += n;
		} else {
			const int fim = em - n; // [em, fim) apagadas
			auto novo = [&](int v) { return v < em ? v : (v < fim ? em : v + n); };
			const int b1 = novo(a1);
			int b2 = a2 < em ? a2 : (a2 < fim ? em - 1 : a2 + n);
			if (b2 < b1) {
				auto er = std::make_shared<Expr>();
				er->k = Expr::Err;
				er->s = "#REF!";
				return er;
			}
			a1 = b1;
			a2 = b2;
		}
		return x;
	}
	for (PExpr &a : x->a)
		a = comLinhas(a, folhaDaFormula, folha, colunas, em, n);
	return x;
}

// Trocar o nome de uma folha nas referências.
inline PExpr comFolhaRenomeada(const PExpr &e, const QString &antes, const QString &depois)
{
	if (!e)
		return e;
	auto x = std::make_shared<Expr>(*e);
	if ((x->k == Expr::Ref || x->k == Expr::Area) && x->folha == antes)
		x->folha = depois;
	for (PExpr &a : x->a)
		a = comFolhaRenomeada(a, antes, depois);
	return x;
}

// --- o cálculo -------------------------------------------------------------

// De onde vêm os valores das células (a pasta, em planilha.h).
class Fonte
{
public:
	virtual ~Fonte() = default;
	virtual Valor valor(int folha, int r, int c) = 0;
	virtual int folhaPorNome(const QString &nome) const = 0;
	virtual int ultimaLinha(int folha) const = 0;  // a última com algo (para A:A)
	virtual int ultimaColuna(int folha) const = 0;
};

struct Intervalo {
	int folha = 0, r1 = 0, c1 = 0, r2 = 0, c2 = 0;
	int linhas() const { return r2 - r1 + 1; }
	int colunas() const { return c2 - c1 + 1; }
};

// Um argumento já calculado: um valor ou um intervalo.
struct Arg {
	bool area = false;
	Valor v;
	Intervalo i;
};

inline bool numeroDeTexto(const QString &s0, double &n)
{
	QString s = s0.trimmed();
	if (s.isEmpty())
		return false;
	bool ok;
	n = s.toDouble(&ok);
	if (ok)
		return true;
	// 1.234,56 (pt-BR)
	QString b = s;
	b.remove('.').replace(',', '.');
	n = b.toDouble(&ok);
	return ok;
}

inline bool comoNumero(const Valor &v, double &n)
{
	switch (v.t) {
	case Valor::Numero:
	case Valor::Logico:
		n = v.n;
		return true;
	case Valor::Vazio:
		n = 0;
		return true;
	case Valor::Texto:
		return numeroDeTexto(v.s, n);
	default:
		return false;
	}
}

inline QString comoTexto(const Valor &v)
{
	switch (v.t) {
	case Valor::Numero: {
		QString s = QString::number(v.n, 'g', 15);
		return s.replace('.', ',');
	}
	case Valor::Texto:
		return v.s;
	case Valor::Logico:
		return v.n ? "VERDADEIRO" : "FALSO";
	case Valor::Erro:
		return erroBrasil(v.s);
	default:
		return QString();
	}
}

class Calculo
{
public:
	Calculo(Fonte &f, int folha) : f_(f), folha_(folha) {}

	Valor calcular(const PExpr &e)
	{
		Arg a = avaliar(e);
		if (a.area)
			return unico(a.i);
		return a.v;
	}

private:
	int folhaDe(const Expr &e) const { return e.folha.isEmpty() ? folha_ : f_.folhaPorNome(e.folha); }

	Valor unico(const Intervalo &i)
	{
		// Um intervalo onde se espera um valor: o primeiro (simplificado).
		return f_.valor(i.folha, i.r1, i.c1);
	}

	Arg avaliar(const PExpr &e)
	{
		Arg r;
		switch (e->k) {
		case Expr::Num:
			r.v = Valor::num(e->n);
			return r;
		case Expr::Str:
			r.v = Valor::txt(e->s);
			return r;
		case Expr::Bool:
			r.v = Valor::log(e->n != 0);
			return r;
		case Expr::Err:
			r.v = e->s.isEmpty() ? Valor() : Valor::erro(e->s);
			return r;
		case Expr::Ref: {
			const int fo = folhaDe(*e);
			if (fo < 0) {
				r.v = Valor::erro("#REF!");
				return r;
			}
			r.v = f_.valor(fo, e->r1, e->c1);
			return r;
		}
		case Expr::Area: {
			const int fo = folhaDe(*e);
			if (fo < 0) {
				r.v = Valor::erro("#REF!");
				return r;
			}
			r.area = true;
			r.i = {fo, std::min(e->r1, e->r2), std::min(e->c1, e->c2), std::max(e->r1, e->r2), std::max(e->c1, e->c2)};
			if (e->colunas)
				r.i.r2 = std::max(0, f_.ultimaLinha(fo));
			return r;
		}
		case Expr::Pct: {
			double n;
			const Valor v = calcular(e->a[0]);
			if (v.t == Valor::Erro) { r.v = v; return r; }
			r.v = comoNumero(v, n) ? Valor::num(n / 100) : Valor::erro("#VALUE!");
			return r;
		}
		case Expr::Un: {
			double n;
			const Valor v = calcular(e->a[0]);
			if (v.t == Valor::Erro) { r.v = v; return r; }
			r.v = comoNumero(v, n) ? Valor::num(e->s == "-" ? -n : n) : Valor::erro("#VALUE!");
			return r;
		}
		case Expr::Bin:
			r.v = binario(e->s, calcular(e->a[0]), calcular(e->a[1]));
			return r;
		case Expr::Func:
			r.v = funcao(*e);
			return r;
		}
		return r;
	}

	static int compara(const Valor &a, const Valor &b)
	{
		auto ordem = [](const Valor &v) { return v.t == Valor::Numero || v.t == Valor::Vazio ? 0 : v.t == Valor::Texto ? 1 : 2; };
		Valor x = a, y = b;
		if (x.t == Valor::Vazio) { if (y.t == Valor::Texto) x = Valor::txt(""); else x = Valor::num(0); }
		if (y.t == Valor::Vazio) { if (x.t == Valor::Texto) y = Valor::txt(""); else y = Valor::num(0); }
		const int ox = ordem(x), oy = ordem(y);
		if (ox != oy)
			return ox < oy ? -1 : 1;
		if (x.t == Valor::Texto)
			return QString::compare(x.s, y.s, Qt::CaseInsensitive);
		return x.n < y.n ? -1 : (x.n > y.n ? 1 : 0);
	}

	static Valor binario(const QString &op, const Valor &a, const Valor &b)
	{
		if (a.t == Valor::Erro) return a;
		if (b.t == Valor::Erro) return b;
		if (op == "&")
			return Valor::txt(comoTexto(a) + comoTexto(b));
		if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
			const int c = compara(a, b);
			if (op == "=") return Valor::log(c == 0);
			if (op == "<>") return Valor::log(c != 0);
			if (op == "<") return Valor::log(c < 0);
			if (op == ">") return Valor::log(c > 0);
			if (op == "<=") return Valor::log(c <= 0);
			return Valor::log(c >= 0);
		}
		double x, y;
		if (!comoNumero(a, x) || !comoNumero(b, y))
			return Valor::erro("#VALUE!");
		if (op == "+") return Valor::num(x + y);
		if (op == "-") return Valor::num(x - y);
		if (op == "*") return Valor::num(x * y);
		if (op == "/") return y == 0 ? Valor::erro("#DIV/0!") : Valor::num(x / y);
		if (op == "^") return Valor::num(std::pow(x, y));
		return Valor::erro("#VALUE!");
	}

	// Os valores de um argumento (as células de um intervalo, uma a uma).
	void cada(const Arg &a, const std::function<void(const Valor &, bool daArea)> &f)
	{
		if (!a.area) {
			f(a.v, false);
			return;
		}
		const int ul = std::min(a.i.r2, f_.ultimaLinha(a.i.folha));
		const int uc = std::min(a.i.c2, f_.ultimaColuna(a.i.folha));
		for (int r = a.i.r1; r <= ul; r++)
			for (int c = a.i.c1; c <= uc; c++)
				f(f_.valor(a.i.folha, r, c), true);
	}

	// Os números dos argumentos, como o SOMA faz: no intervalo, só os
	// números; direto no argumento, também o texto que parece número.
	bool numeros(const std::vector<Arg> &as, std::vector<double> &out, Valor &erro, bool contarLogicos = false)
	{
		bool ok = true;
		for (const Arg &a : as)
			cada(a, [&](const Valor &v, bool daArea) {
				if (!ok)
					return;
				if (v.t == Valor::Erro) {
					erro = v;
					ok = false;
				} else if (v.t == Valor::Numero) {
					out.push_back(v.n);
				} else if (!daArea && (v.t == Valor::Logico || v.t == Valor::Texto)) {
					double n;
					if (comoNumero(v, n))
						out.push_back(n);
					else {
						erro = Valor::erro("#VALUE!");
						ok = false;
					}
				} else if (daArea && contarLogicos && v.t == Valor::Logico) {
					out.push_back(v.n);
				}
			});
		return ok;
	}

	// Um critério do SOMASE: ">=10", "<>x", "maria", 5, "a*".
	static std::function<bool(const Valor &)> criterio(const Valor &crit)
	{
		QString op = "=";
		QString t;
		if (crit.t == Valor::Texto) {
			t = crit.s;
			for (const char *o : {"<>", "<=", ">=", "=", "<", ">"})
				if (t.startsWith(o)) {
					op = o;
					t = t.mid(int(strlen(o)));
					break;
				}
		} else {
			t = crit.t == Valor::Numero ? QString::number(crit.n, 'g', 15) : comoTexto(crit);
		}
		double alvo;
		const bool numerico = numeroDeTexto(t, alvo) && crit.t != Valor::Logico;
		QRegularExpression curinga;
		const bool usaCuringa = !numerico && (t.contains('*') || t.contains('?'));
		if (usaCuringa) {
			QString re = QRegularExpression::escape(t);
			re.replace("\\*", ".*").replace("\\?", ".");
			curinga = QRegularExpression("^" + re + "$", QRegularExpression::CaseInsensitiveOption);
		}
		return [=](const Valor &v) -> bool {
			int c;
			if (numerico) {
				if (v.t != Valor::Numero) {
					return op == "<>";
				}
				c = v.n < alvo ? -1 : (v.n > alvo ? 1 : 0);
			} else {
				const QString s = v.t == Valor::Vazio ? QString() : comoTexto(v);
				if (usaCuringa && (op == "=" || op == "<>")) {
					const bool m = curinga.match(s).hasMatch();
					return op == "=" ? m : !m;
				}
				if (t.isEmpty() && op == "=")
					return v.t == Valor::Vazio || s.isEmpty();
				c = QString::compare(s, t, Qt::CaseInsensitive);
			}
			if (op == "=") return c == 0;
			if (op == "<>") return c != 0;
			if (op == "<") return c < 0;
			if (op == ">") return c > 0;
			if (op == "<=") return c <= 0;
			return c >= 0;
		};
	}

	Valor funcao(const Expr &e)
	{
		const QString f = e.s;
		const size_t n = e.a.size();
		// As que não calculam todos os argumentos antes.
		if (f == "IF") {
			if (n < 2 || n > 3) return Valor::erro("#VALUE!");
			const Valor c = calcular(e.a[0]);
			if (c.t == Valor::Erro) return c;
			double x;
			if (!comoNumero(c, x)) return Valor::erro("#VALUE!");
			if (x != 0) return calcular(e.a[1]);
			return n == 3 ? calcular(e.a[2]) : Valor::log(false);
		}
		if (f == "IFERROR") {
			if (n != 2) return Valor::erro("#VALUE!");
			const Valor v = calcular(e.a[0]);
			return v.t == Valor::Erro ? calcular(e.a[1]) : v;
		}
		if (f == "ISERROR" || f == "ISERR") {
			if (n != 1) return Valor::erro("#VALUE!");
			const Valor v = calcular(e.a[0]);
			return Valor::log(v.t == Valor::Erro && (f == "ISERROR" || v.s != "#N/A"));
		}
		if (f == "ISBLANK" || f == "ISNUMBER" || f == "ISTEXT") {
			if (n != 1) return Valor::erro("#VALUE!");
			const Valor v = calcular(e.a[0]);
			return Valor::log(f == "ISBLANK" ? v.t == Valor::Vazio : f == "ISNUMBER" ? v.t == Valor::Numero : v.t == Valor::Texto);
		}
		std::vector<Arg> as;
		as.reserve(n);
		for (const PExpr &x : e.a)
			as.push_back(avaliar(x));
		auto val = [&](size_t i) -> Valor { return i < as.size() ? (as[i].area ? unico(as[i].i) : as[i].v) : Valor(); };
		auto num = [&](size_t i, double &x, double padrao = 0) -> bool {
			if (i >= as.size() || (e.a[i]->k == Expr::Err && e.a[i]->s.isEmpty())) {
				x = padrao;
				return true;
			}
			const Valor v = val(i);
			if (v.t == Valor::Erro) return false;
			return comoNumero(v, x);
		};
		auto erroDe = [&](size_t i) -> Valor {
			const Valor v = val(i);
			return v.t == Valor::Erro ? v : Valor::erro("#VALUE!");
		};
		Valor erro;
		std::vector<double> xs;

		// Agregações.
		if (f == "SUM" || f == "AVERAGE" || f == "MIN" || f == "MAX" || f == "COUNT" || f == "PRODUCT" ||
		    f == "SUMSQ" || f == "MEDIAN" || f == "STDEV" || f == "STDEV.S" || f == "STDEVP" || f == "STDEV.P" ||
		    f == "VAR" || f == "VAR.S" || f == "VARP" || f == "VAR.P" || f == "MODE") {
			if (f == "COUNT") {
				int k = 0;
				for (const Arg &a : as)
					cada(a, [&](const Valor &v, bool daArea) {
						double x;
						if (v.t == Valor::Numero || (!daArea && v.t != Valor::Erro && comoNumero(v, x) && v.t != Valor::Vazio))
							k++;
					});
				return Valor::num(k);
			}
			if (!numeros(as, xs, erro))
				return erro;
			double s = 0;
			for (double x : xs)
				s += x;
			if (f == "SUM") return Valor::num(s);
			if (f == "SUMSQ") {
				double q = 0;
				for (double x : xs) q += x * x;
				return Valor::num(q);
			}
			if (f == "PRODUCT") {
				double p = 1;
				for (double x : xs) p *= x;
				return Valor::num(xs.empty() ? 0 : p);
			}
			if (xs.empty())
				return f == "MIN" || f == "MAX" ? Valor::num(0) : Valor::erro("#DIV/0!");
			if (f == "AVERAGE") return Valor::num(s / xs.size());
			if (f == "MIN") return Valor::num(*std::min_element(xs.begin(), xs.end()));
			if (f == "MAX") return Valor::num(*std::max_element(xs.begin(), xs.end()));
			if (f == "MEDIAN") {
				std::sort(xs.begin(), xs.end());
				const size_t m = xs.size() / 2;
				return Valor::num(xs.size() % 2 ? xs[m] : (xs[m - 1] + xs[m]) / 2);
			}
			if (f == "MODE") {
				std::sort(xs.begin(), xs.end());
				double melhor = 0;
				size_t mx = 1;
				for (size_t i = 0; i < xs.size();) {
					size_t j = i;
					while (j < xs.size() && xs[j] == xs[i]) j++;
					if (j - i > mx) { mx = j - i; melhor = xs[i]; }
					i = j;
				}
				return mx > 1 ? Valor::num(melhor) : Valor::erro("#N/A");
			}
			const double media = s / xs.size();
			double q = 0;
			for (double x : xs) q += (x - media) * (x - media);
			const bool pop = f == "STDEVP" || f == "STDEV.P" || f == "VARP" || f == "VAR.P";
			if (!pop && xs.size() < 2) return Valor::erro("#DIV/0!");
			const double var = q / (pop ? xs.size() : xs.size() - 1);
			return Valor::num(f.startsWith("VAR") ? var : std::sqrt(var));
		}
		if (f == "COUNTA" || f == "COUNTBLANK") {
			int k = 0, vazias = 0;
			for (const Arg &a : as) {
				if (a.area) {
					const int total = a.i.linhas() * a.i.colunas();
					int cheias = 0;
					cada(a, [&](const Valor &v, bool) {
						if (v.t != Valor::Vazio && !(v.t == Valor::Texto && v.s.isEmpty())) cheias++;
					});
					k += cheias;
					vazias += total - cheias;
				} else if (a.v.t != Valor::Vazio) {
					k++;
				}
			}
			return Valor::num(f == "COUNTA" ? k : vazias);
		}
		if (f == "AND" || f == "OR") {
			bool tudo = true, algum = false, viu = false;
			for (const Arg &a : as)
				cada(a, [&](const Valor &v, bool daArea) {
					if (v.t == Valor::Erro) { erro = v; return; }
					if (daArea && (v.t == Valor::Texto || v.t == Valor::Vazio)) return;
					double x;
					if (!comoNumero(v, x)) { erro = Valor::erro("#VALUE!"); return; }
					viu = true;
					tudo = tudo && x != 0;
					algum = algum || x != 0;
				});
			if (erro.t == Valor::Erro) return erro;
			if (!viu) return Valor::erro("#VALUE!");
			return Valor::log(f == "AND" ? tudo : algum);
		}
		if (f == "NOT") {
			double x;
			if (n != 1 || !num(0, x)) return erroDe(0);
			return Valor::log(x == 0);
		}
		if (f == "TRUE" || f == "FALSE")
			return Valor::log(f == "TRUE");
		if (f == "NA")
			return Valor::erro("#N/A");
		if (f == "PI")
			return Valor::num(M_PI);
		if (f == "RAND")
			return Valor::num(QRandomGenerator::global()->generateDouble());
		if (f == "TODAY")
			return Valor::num(serialDe(QDate::currentDate()));
		if (f == "NOW") {
			const QDateTime t = QDateTime::currentDateTime();
			return Valor::num(serialDe(t.date()) + t.time().msecsSinceStartOfDay() / 86400000.0);
		}

		// Matemática de um ou dois números.
		{
			static const QHash<QString, std::function<double(double)>> um = {
				{"ABS", [](double x) { return std::fabs(x); }}, {"INT", [](double x) { return std::floor(x); }},
				{"EXP", [](double x) { return std::exp(x); }}, {"SIGN", [](double x) { return double((x > 0) - (x < 0)); }},
			};
			if (um.contains(f)) {
				double x;
				if (n != 1 || !num(0, x)) return erroDe(0);
				return Valor::num(um.value(f)(x));
			}
		}
		if (f == "SQRT" || f == "LN" || f == "LOG10") {
			double x;
			if (n != 1 || !num(0, x)) return erroDe(0);
			if (f == "SQRT") return x < 0 ? Valor::erro("#NUM!") : Valor::num(std::sqrt(x));
			if (x <= 0) return Valor::erro("#NUM!");
			return Valor::num(f == "LN" ? std::log(x) : std::log10(x));
		}
		if (f == "LOG") {
			double x, b;
			if (n < 1 || !num(0, x) || !num(1, b, 10)) return erroDe(0);
			if (x <= 0 || b <= 0 || b == 1) return Valor::erro("#NUM!");
			return Valor::num(std::log(x) / std::log(b));
		}
		if (f == "POWER" || f == "MOD") {
			double x, y;
			if (n != 2 || !num(0, x) || !num(1, y)) return Valor::erro("#VALUE!");
			if (f == "POWER") return Valor::num(std::pow(x, y));
			if (y == 0) return Valor::erro("#DIV/0!");
			return Valor::num(x - y * std::floor(x / y));
		}
		if (f == "ROUND" || f == "ROUNDUP" || f == "ROUNDDOWN" || f == "TRUNC") {
			double x, d;
			if (n < 1 || !num(0, x) || !num(1, d, 0)) return erroDe(0);
			const double p = std::pow(10.0, std::floor(d));
			const double y = x * p;
			double r;
			if (f == "ROUND") r = std::round(y + (y >= 0 ? 1e-9 : -1e-9));
			else if (f == "ROUNDUP") r = y >= 0 ? std::ceil(y - 1e-9) : std::floor(y + 1e-9);
			else r = std::trunc(y);
			return Valor::num(r / p);
		}
		if (f == "RANDBETWEEN") {
			double a, b;
			if (n != 2 || !num(0, a) || !num(1, b) || b < a) return Valor::erro("#NUM!");
			return Valor::num(std::ceil(a) + QRandomGenerator::global()->bounded(int(std::floor(b) - std::ceil(a) + 1)));
		}

		// Condicionais.
		if (f == "SUMIF" || f == "COUNTIF" || f == "AVERAGEIF") {
			if (n < 2 || !as[0].area) return Valor::erro("#VALUE!");
			auto teste = criterio(val(1));
			const Intervalo &ir = as[0].i;
			Intervalo is = ir;
			if (n == 3 && as[2].area) {
				is.folha = as[2].i.folha;
				is.r1 = as[2].i.r1;
				is.c1 = as[2].i.c1;
				is.r2 = is.r1 + ir.linhas() - 1;
				is.c2 = is.c1 + ir.colunas() - 1;
			}
			double s = 0;
			int k = 0;
			const int ul = std::min(ir.r2, f_.ultimaLinha(ir.folha));
			for (int r = ir.r1; r <= ul; r++)
				for (int c = ir.c1; c <= ir.c2; c++)
					if (teste(f_.valor(ir.folha, r, c))) {
						k++;
						const Valor v = f_.valor(is.folha, is.r1 + (r - ir.r1), is.c1 + (c - ir.c1));
						if (v.t == Valor::Numero)
							s += v.n;
					}
			if (f == "COUNTIF") return Valor::num(k);
			if (f == "SUMIF") return Valor::num(s);
			return k ? Valor::num(s / k) : Valor::erro("#DIV/0!");
		}
		if (f == "SUMIFS" || f == "COUNTIFS") {
			const size_t ini = f == "SUMIFS" ? 1 : 0;
			if (n < ini + 2 || (n - ini) % 2 != 0) return Valor::erro("#VALUE!");
			const Intervalo base = as[ini].i;
			double s = 0;
			int k = 0;
			const int ul = std::min(base.r2, f_.ultimaLinha(base.folha));
			std::vector<std::function<bool(const Valor &)>> testes;
			for (size_t i = ini; i + 1 < n; i += 2) {
				if (!as[i].area) return Valor::erro("#VALUE!");
				testes.push_back(criterio(val(i + 1)));
			}
			for (int dr = 0; dr <= ul - base.r1; dr++)
				for (int dc = 0; dc < base.colunas(); dc++) {
					bool ok = true;
					for (size_t i = ini, t = 0; i + 1 < n && ok; i += 2, t++)
						ok = testes[t](f_.valor(as[i].i.folha, as[i].i.r1 + dr, as[i].i.c1 + dc));
					if (!ok) continue;
					k++;
					if (f == "SUMIFS") {
						const Valor v = f_.valor(as[0].i.folha, as[0].i.r1 + dr, as[0].i.c1 + dc);
						if (v.t == Valor::Numero) s += v.n;
					}
				}
			return Valor::num(f == "SUMIFS" ? s : k);
		}
		if (f == "SUMPRODUCT") {
			if (n == 0) return Valor::erro("#VALUE!");
			for (const Arg &a : as)
				if (!a.area || a.i.linhas() != as[0].i.linhas() || a.i.colunas() != as[0].i.colunas())
					return Valor::erro("#VALUE!");
			double s = 0;
			for (int r = 0; r < as[0].i.linhas(); r++)
				for (int c = 0; c < as[0].i.colunas(); c++) {
					double p = 1;
					for (const Arg &a : as) {
						const Valor v = f_.valor(a.i.folha, a.i.r1 + r, a.i.c1 + c);
						if (v.t == Valor::Erro) return v;
						p *= v.t == Valor::Numero ? v.n : 0;
					}
					s += p;
				}
			return Valor::num(s);
		}

		// Procura.
		if (f == "VLOOKUP" || f == "HLOOKUP") {
			if (n < 3 || !as[1].area) return Valor::erro("#VALUE!");
			const Valor alvo = val(0);
			double idx, aprox = 1;
			if (!num(2, idx)) return erroDe(2);
			if (n >= 4) {
				const Valor v = val(3);
				comoNumero(v, aprox);
			}
			const Intervalo &t = as[1].i;
			const bool v = f == "VLOOKUP";
			const int tam = v ? std::min(t.r2, f_.ultimaLinha(t.folha)) - t.r1 + 1 : t.colunas();
			if (idx < 1 || idx > (v ? t.colunas() : t.linhas())) return Valor::erro("#REF!");
			auto chave = [&](int i) { return v ? f_.valor(t.folha, t.r1 + i, t.c1) : f_.valor(t.folha, t.r1, t.c1 + i); };
			int achou = -1;
			if (aprox == 0) {
				auto teste = criterio(alvo.t == Valor::Texto ? Valor::txt("=" + alvo.s) : alvo);
				for (int i = 0; i < tam; i++)
					if (alvo.t == Valor::Texto ? teste(chave(i)) : compara(chave(i), alvo) == 0) {
						achou = i;
						break;
					}
			} else {
				for (int i = 0; i < tam; i++) {
					const Valor k = chave(i);
					if (k.t == Valor::Vazio) break;
					if (compara(k, alvo) <= 0) achou = i;
					else break;
				}
			}
			if (achou < 0) return Valor::erro("#N/A");
			const int k = int(idx) - 1;
			return v ? f_.valor(t.folha, t.r1 + achou, t.c1 + k) : f_.valor(t.folha, t.r1 + k, t.c1 + achou);
		}
		if (f == "MATCH") {
			if (n < 2 || !as[1].area) return Valor::erro("#VALUE!");
			const Valor alvo = val(0);
			double tipo = 1;
			if (n >= 3) num(2, tipo, 1);
			const Intervalo &t = as[1].i;
			const bool col = t.colunas() == 1;
			const int tam = col ? std::min(t.r2, f_.ultimaLinha(t.folha)) - t.r1 + 1 : t.colunas();
			int achou = -1;
			for (int i = 0; i < tam; i++) {
				const Valor k = col ? f_.valor(t.folha, t.r1 + i, t.c1) : f_.valor(t.folha, t.r1, t.c1 + i);
				const int c = compara(k, alvo);
				if (tipo == 0) {
					if (c == 0) { achou = i; break; }
				} else if (tipo > 0) {
					if (c <= 0) achou = i; else break;
				} else {
					if (c >= 0) achou = i; else break;
				}
			}
			return achou < 0 ? Valor::erro("#N/A") : Valor::num(achou + 1);
		}
		if (f == "INDEX") {
			if (n < 2 || !as[0].area) return Valor::erro("#VALUE!");
			double l, c = 1;
			if (!num(1, l) || !num(2, c, 1)) return Valor::erro("#VALUE!");
			const Intervalo &t = as[0].i;
			if (t.linhas() == 1 && n == 2) { c = l; l = 1; }
			if (l < 1 || c < 1 || l > t.linhas() || c > t.colunas()) return Valor::erro("#REF!");
			return f_.valor(t.folha, t.r1 + int(l) - 1, t.c1 + int(c) - 1);
		}

		// Datas.
		if (f == "DATE") {
			double a, m, d;
			if (n != 3 || !num(0, a) || !num(1, m) || !num(2, d)) return Valor::erro("#VALUE!");
			if (a < 1900) a += 1900;
			QDate q(int(a), 1, 1);
			q = q.addMonths(int(m) - 1).addDays(qint64(d) - 1);
			return Valor::num(serialDe(q));
		}
		if (f == "YEAR" || f == "MONTH" || f == "DAY" || f == "WEEKDAY") {
			double x;
			if (n < 1 || !num(0, x)) return erroDe(0);
			const QDate q = dataDe(x);
			if (f == "YEAR") return Valor::num(q.year());
			if (f == "MONTH") return Valor::num(q.month());
			if (f == "DAY") return Valor::num(q.day());
			double tipo = 1;
			num(1, tipo, 1);
			const int dw = q.dayOfWeek(); // 1 = segunda ... 7 = domingo
			if (tipo == 2) return Valor::num(dw);
			if (tipo == 3) return Valor::num(dw - 1);
			return Valor::num(dw % 7 + 1);
		}
		if (f == "DAYS") {
			double a, b;
			if (n != 2 || !num(0, a) || !num(1, b)) return Valor::erro("#VALUE!");
			return Valor::num(std::floor(a) - std::floor(b));
		}
		if (f == "EDATE") {
			double a, m;
			if (n != 2 || !num(0, a) || !num(1, m)) return Valor::erro("#VALUE!");
			return Valor::num(serialDe(dataDe(a).addMonths(int(m))));
		}

		// Texto.
		if (f == "CONCATENATE" || f == "CONCAT") {
			QString s;
			for (const Arg &a : as)
				cada(a, [&](const Valor &v, bool) {
					if (v.t == Valor::Erro) erro = v;
					s += comoTexto(v);
				});
			if (erro.t == Valor::Erro) return erro;
			return Valor::txt(s);
		}
		if (f == "LEFT" || f == "RIGHT") {
			const Valor v = val(0);
			if (v.t == Valor::Erro) return v;
			double k;
			if (!num(1, k, 1) || k < 0) return Valor::erro("#VALUE!");
			const QString s = comoTexto(v);
			return Valor::txt(f == "LEFT" ? s.left(int(k)) : s.right(int(k)));
		}
		if (f == "MID") {
			const Valor v = val(0);
			if (v.t == Valor::Erro) return v;
			double i, k;
			if (n != 3 || !num(1, i) || !num(2, k) || i < 1 || k < 0) return Valor::erro("#VALUE!");
			return Valor::txt(comoTexto(v).mid(int(i) - 1, int(k)));
		}
		if (f == "LEN" || f == "UPPER" || f == "LOWER" || f == "TRIM") {
			const Valor v = val(0);
			if (v.t == Valor::Erro) return v;
			const QString s = comoTexto(v);
			if (f == "LEN") return Valor::num(s.size());
			if (f == "UPPER") return Valor::txt(s.toUpper());
			if (f == "LOWER") return Valor::txt(s.toLower());
			return Valor::txt(s.simplified());
		}
		if (f == "SUBSTITUTE") {
			if (n < 3) return Valor::erro("#VALUE!");
			QString s = comoTexto(val(0));
			return Valor::txt(s.replace(comoTexto(val(1)), comoTexto(val(2))));
		}
		if (f == "VALUE") {
			const Valor v = val(0);
			double x;
			if (v.t == Valor::Numero) return v;
			QString s = comoTexto(v).trimmed();
			if (s.endsWith('%') && numeroDeTexto(s.chopped(1), x)) return Valor::num(x / 100);
			s.remove("R$").remove(' ');
			return numeroDeTexto(s, x) ? Valor::num(x) : Valor::erro("#VALUE!");
		}
		if (f == "TEXT") {
			double x;
			if (n != 2 || !num(0, x)) return erroDe(0);
			return Valor::txt(textoComCodigo(x, comoTexto(val(1))));
		}

		// Finanças.
		if (f == "NPV") {
			double taxa;
			if (n < 2 || !num(0, taxa)) return Valor::erro("#VALUE!");
			std::vector<Arg> resto(as.begin() + 1, as.end());
			if (!numeros(resto, xs, erro)) return erro;
			double s = 0;
			for (size_t i = 0; i < xs.size(); i++)
				s += xs[i] / std::pow(1 + taxa, double(i + 1));
			return Valor::num(s);
		}
		if (f == "IRR") {
			if (n < 1) return Valor::erro("#VALUE!");
			std::vector<Arg> fl(as.begin(), as.begin() + 1);
			if (!numeros(fl, xs, erro)) return erro;
			double chute = 0.1;
			num(1, chute, 0.1);
			const double r = tir(xs, chute);
			return std::isnan(r) ? Valor::erro("#NUM!") : Valor::num(r);
		}
		if (f == "PMT" || f == "PV" || f == "FV" || f == "NPER" || f == "RATE" || f == "IPMT" || f == "PPMT") {
			double a0, a1, a2, a3 = 0, a4 = 0, a5 = 0;
			if (n < 3 || !num(0, a0) || !num(1, a1) || !num(2, a2) || !num(3, a3, 0) || !num(4, a4, 0) || !num(5, a5, 0))
				return Valor::erro("#VALUE!");
			double r;
			if (f == "PMT") r = pgto(a0, a1, a2, a3, a4 != 0);
			else if (f == "PV") r = vp(a0, a1, a2, a3, a4 != 0);
			else if (f == "FV") r = vf(a0, a1, a2, a3, a4 != 0);
			else if (f == "NPER") r = nper(a0, a1, a2, a3, a4 != 0);
			else if (f == "RATE") r = taxaDe(a0, a1, a2, a3, a4 != 0, n >= 6 ? a5 : 0.1);
			else {
				// IPGTO(taxa; período; nper; vp; [vf]; [tipo]) e PPGTO
				if (n < 4) return Valor::erro("#VALUE!");
				const double taxa = a0, per = a1, np = a2, pv = a3, fv = a4;
				const bool tipo = a5 != 0;
				const double p = pgto(taxa, np, pv, fv, tipo);
				double juros;
				if (per == 1 && tipo) juros = 0;
				else {
					const double saldo = vf(taxa, per - 1, p, pv, tipo);
					juros = saldo * taxa;
					if (tipo) juros /= 1 + taxa;
				}
				r = f == "IPMT" ? juros : p - juros;
			}
			return std::isfinite(r) ? Valor::num(r) : Valor::erro("#NUM!");
		}

		// Estatística de pares.
		if (f == "CORREL" || f == "PEARSON" || f == "SLOPE" || f == "INTERCEPT" || f == "RSQ" || f == "COVAR" || f == "FORECAST" || f == "FORECAST.LINEAR") {
			const bool prev = f.startsWith("FORECAST");
			const size_t iy = prev ? 1 : 0, ix = prev ? 2 : 1;
			if (n != (prev ? 3u : 2u) || !as[iy].area || !as[ix].area) return Valor::erro("#VALUE!");
			std::vector<double> ys, xs2;
			const Intervalo &a = as[iy].i, &b = as[ix].i;
			if (a.linhas() * a.colunas() != b.linhas() * b.colunas()) return Valor::erro("#N/A");
			const int tot = a.linhas() * a.colunas();
			for (int k = 0; k < tot; k++) {
				const Valor y = f_.valor(a.folha, a.r1 + k / a.colunas(), a.c1 + k % a.colunas());
				const Valor x = f_.valor(b.folha, b.r1 + k / b.colunas(), b.c1 + k % b.colunas());
				if (y.t == Valor::Numero && x.t == Valor::Numero) {
					ys.push_back(y.n);
					xs2.push_back(x.n);
				}
			}
			const size_t m = ys.size();
			if (m < 2) return Valor::erro("#DIV/0!");
			double sx = 0, sy = 0;
			for (size_t i = 0; i < m; i++) { sx += xs2[i]; sy += ys[i]; }
			const double mx = sx / m, my = sy / m;
			double sxx = 0, syy = 0, sxy = 0;
			for (size_t i = 0; i < m; i++) {
				sxx += (xs2[i] - mx) * (xs2[i] - mx);
				syy += (ys[i] - my) * (ys[i] - my);
				sxy += (xs2[i] - mx) * (ys[i] - my);
			}
			if (f == "COVAR") return Valor::num(sxy / m);
			if (sxx == 0) return Valor::erro("#DIV/0!");
			const double incl = sxy / sxx;
			if (f == "SLOPE") return Valor::num(incl);
			if (f == "INTERCEPT") return Valor::num(my - incl * mx);
			if (prev) {
				double x;
				if (!num(0, x)) return erroDe(0);
				return Valor::num(my - incl * mx + incl * x);
			}
			if (syy == 0) return Valor::erro("#DIV/0!");
			const double r = sxy / std::sqrt(sxx * syy);
			return Valor::num(f == "RSQ" ? r * r : r);
		}
		if (f == "LARGE" || f == "SMALL") {
			double k;
			if (n != 2 || !num(1, k)) return Valor::erro("#VALUE!");
			std::vector<Arg> a1(as.begin(), as.begin() + 1);
			if (!numeros(a1, xs, erro)) return erro;
			if (k < 1 || k > xs.size()) return Valor::erro("#NUM!");
			std::sort(xs.begin(), xs.end());
			return Valor::num(f == "SMALL" ? xs[size_t(k) - 1] : xs[xs.size() - size_t(k)]);
		}
		if (f == "RANK") {
			double x, ordem = 0;
			if (n < 2 || !num(0, x) || !as[1].area) return Valor::erro("#VALUE!");
			num(2, ordem, 0);
			std::vector<Arg> a1(as.begin() + 1, as.begin() + 2);
			if (!numeros(a1, xs, erro)) return erro;
			int pos = 1;
			bool tem = false;
			for (double y : xs) {
				if (y == x) tem = true;
				if (ordem == 0 ? y > x : y < x) pos++;
			}
			return tem ? Valor::num(pos) : Valor::erro("#N/A");
		}
		return Valor::erro("#NAME?");
	}

public:
	// As contas de finanças (como na HP 12C e no Excel: o que sai é negativo).
	static double vf(double r, double np, double p, double pv, bool inicio)
	{
		if (r == 0) return -(pv + p * np);
		const double f = std::pow(1 + r, np);
		return -(pv * f + p * (1 + r * inicio) * (f - 1) / r);
	}
	static double vp(double r, double np, double p, double fv, bool inicio)
	{
		if (r == 0) return -(fv + p * np);
		const double f = std::pow(1 + r, np);
		return -(fv + p * (1 + r * inicio) * (f - 1) / r) / f;
	}
	static double pgto(double r, double np, double pv, double fv, bool inicio)
	{
		if (np == 0) return std::numeric_limits<double>::quiet_NaN();
		if (r == 0) return -(pv + fv) / np;
		const double f = std::pow(1 + r, np);
		return -(r * (pv * f + fv)) / ((1 + r * inicio) * (f - 1));
	}
	static double nper(double r, double p, double pv, double fv, bool inicio)
	{
		if (r == 0) return p == 0 ? std::numeric_limits<double>::quiet_NaN() : -(pv + fv) / p;
		const double z = p * (1 + r * inicio) / r;
		return std::log((z - fv) / (pv + z)) / std::log(1 + r);
	}
	// TAXA: a taxa r em que vf(r) == fv (Newton, a partir do chute).
	static double taxaDe(double np, double p, double pv, double fv, bool inicio, double chute)
	{
		auto g = [&](double r) { return vf(r, np, p, pv, inicio) - fv; };
		double r = chute;
		for (int i = 0; i < 200; i++) {
			const double y = g(r), h = 1e-7;
			const double d = (g(r + h) - y) / h;
			if (d == 0 || !std::isfinite(d))
				break;
			double nr = r - y / d;
			if (nr <= -1)
				nr = (r - 1) / 2;
			if (std::fabs(nr - r) < 1e-13) {
				r = nr;
				break;
			}
			r = nr;
		}
		return std::fabs(g(r)) < 1e-6 * std::max(1.0, std::fabs(pv) + std::fabs(fv)) ? r : std::numeric_limits<double>::quiet_NaN();
	}
	static double tir(const std::vector<double> &fl, double chute)
	{
		auto vpl = [&](double r) {
			double s = 0;
			for (size_t i = 0; i < fl.size(); i++) s += fl[i] / std::pow(1 + r, double(i));
			return s;
		};
		double r = chute;
		for (int i = 0; i < 100; i++) {
			const double y = vpl(r), h = 1e-7;
			const double d = (vpl(r + h) - y) / h;
			if (d == 0) break;
			const double nr = r - y / d;
			if (!std::isfinite(nr)) break;
			if (std::fabs(nr - r) < 1e-12) return nr;
			r = std::max(nr, -0.9999);
		}
		// Sem convergir: bisseção entre -99% e 1000%.
		double a = -0.9999, b = 10;
		if (vpl(a) * vpl(b) > 0) return std::numeric_limits<double>::quiet_NaN();
		for (int i = 0; i < 200; i++) {
			const double m = (a + b) / 2;
			if (vpl(a) * vpl(m) <= 0) b = m; else a = m;
		}
		return (a + b) / 2;
	}

	// TEXTO(valor; "0,00") e companhia, simplificado: casas, %, milhar, data.
	static QString textoComCodigo(double x, QString cod)
	{
		const QLocale br(QLocale::Portuguese, QLocale::Brazil);
		const QString c = cod.toLower();
		if (c.contains('d') || c.contains('y') || c.contains('a') || (c.contains('m') && !c.contains('0'))) {
			QString q = cod;
			q.replace("aaaa", "yyyy").replace("AAAA", "yyyy").replace("aa", "yy").replace("YYYY", "yyyy").replace("DD", "dd").replace("mmmm", "MMMM").replace("mmm", "MMM").replace("mm", "MM");
			return br.toString(dataDe(x), q);
		}
		const bool pct = cod.contains('%');
		if (pct) x *= 100;
		int casas = 0;
		const int virg = int(cod.indexOf(','));
		if (virg >= 0)
			for (int i = virg + 1; i < cod.size() && (cod[i] == '0' || cod[i] == '#'); i++) casas++;
		const bool milhar = cod.contains('.') || cod.contains("#,#");
		QString s = milhar ? br.toString(x, 'f', casas) : QString::number(x, 'f', casas).replace('.', ',');
		return s + (pct ? "%" : "");
	}

private:
	Fonte &f_;
	int folha_;
};

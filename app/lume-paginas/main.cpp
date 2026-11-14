// SPDX-License-Identifier: BSD-2-Clause
//
// lume-paginas - "Páginas do LumeBSD": um editor de texto com a cara do
// Pages (o do iWork) e o papel do LumeBSD. A folha aparece como vai sair na
// impressão, página por página; à direita, o painel de formatação (Formatar
// e Documento), como no Pages; ao abrir, uma galeria de modelos para quem
// estuda e ensina: trabalho acadêmico nas normas da ABNT, plano de aula,
// relatório, carta e currículo.
//
// Abre e grava os formatos de todo mundo, com leitores e gravadores daqui
// (sem o LibreOffice):
//
//   .odt   OpenDocument (o padrão; o do LibreOffice)
//   .docx  Word
//   .pdf   exportar, com os números de página
//   .md .txt .html  abrir e exportar
//
// O que vai e volta: estilos de parágrafo (título, cabeçalhos, corpo,
// citação, legenda), fonte, tamanho, negrito, itálico, sublinhado,
// riscado, cor, alinhamento, espaçamento, recuos, listas com marcadores e
// numeradas (com níveis), tabelas (com células mescladas), imagens,
// quebras de página, o tamanho do papel, as margens e os números de página.
//
// A linha de comando, sem janela (é como os testes conferem):
//
//   lume-paginas [ARQUIVO]
//   lume-paginas --converter ENTRADA SAIDA     (pela extensão: odt, docx,
//                                              pdf, md, txt, html)
//   lume-paginas --modelo NOME SAIDA          (abnt, aula, relatorio,
//                                              carta, curriculo, branco)
//   lume-paginas --resumo ARQUIVO             (a estrutura, para os testes)
//   lume-paginas --foto SAIDA.png [ARQUIVO | --modelo NOME | --galeria]
//
// Sem moc: só sinais do próprio Qt ligados a lambdas, como os outros apps
// do LumeBSD. O zip (os .odt e .docx são zips) é o zlib da base.

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSessionManager>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTextFrame>
#include <QTextList>
#include <QTextStream>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// --- medidas ---------------------------------------------------------------
//
// O documento é medido em pixels de 96 por polegada (o "px" do Qt e do CSS):
// a folha na tela tem o tamanho de verdade, e o PDF sai com a mesma conta.

static constexpr double PX_MM = 96.0 / 25.4;
static double mm(double px) { return px / PX_MM; }
static double px(double mm_) { return mm_ * PX_MM; }
static double pxDePt(double pt) { return pt * 96.0 / 72.0; }

// "1.5cm", "15mm", "0.59in", "12pt", "1pc", "100px", "3.2" (cm) -> px.
static double medida(const QString &s0, double padrao = 0)
{
	const QString s = s0.trimmed();
	if (s.isEmpty())
		return padrao;
	int i = 0;
	while (i < s.size() && (s[i].isDigit() || s[i] == '.' || s[i] == '-' || s[i] == '+' || s[i] == 'e'))
		i++;
	bool ok = false;
	const double v = s.left(i).toDouble(&ok);
	if (!ok)
		return padrao;
	const QString u = s.mid(i).trimmed().toLower();
	if (u == "mm") return px(v);
	if (u == "in") return v * 96.0;
	if (u == "pt") return pxDePt(v);
	if (u == "pc") return pxDePt(v * 12);
	if (u == "px") return v;
	return px(v * 10); // cm
}

static QString cm(double pxv) { return QString::number(mm(pxv) / 10.0, 'f', 3) + "cm"; }
static QString pt(double pts) { return QString::number(pts, 'g', 4) + "pt"; }
static int twips(double pxv) { return int(std::lround(pxv * 15.0)); } // 1440/96
static double pxDeTwips(double t) { return t / 15.0; }
static qint64 emu(double pxv) { return qint64(std::llround(pxv * 9525.0)); }

static QString corHex(const QColor &c) { return c.name(QColor::HexRgb); }

// --- zip -------------------------------------------------------------------
//
// Os .odt e os .docx são arquivos zip. Ler: o diretório central no fim do
// arquivo e cada entrada guardada (0) ou comprimida (8, deflate). Gravar:
// deflate cru do zlib, com o CRC-32.

static quint16 le16(const QByteArray &d, qint64 o)
{
	if (o < 0 || o + 2 > d.size()) return 0;
	return quint16(uchar(d[int(o)]) | (uchar(d[int(o + 1)]) << 8));
}
static quint32 le32(const QByteArray &d, qint64 o)
{
	if (o < 0 || o + 4 > d.size()) return 0;
	return quint32(le16(d, o)) | (quint32(le16(d, o + 2)) << 16);
}

class ZipLeitor
{
public:
	bool abrir(const QByteArray &dados)
	{
		d_ = dados;
		ents_.clear();
		const qint64 fim = d_.size();
		qint64 eocd = -1;
		for (qint64 o = fim - 22; o >= 0 && o >= fim - 22 - 65535; o--) {
			if (le32(d_, o) == 0x06054b50) {
				eocd = o;
				break;
			}
		}
		if (eocd < 0)
			return false;
		const int n = le16(d_, eocd + 10);
		qint64 o = le32(d_, eocd + 16);
		for (int k = 0; k < n; k++) {
			if (le32(d_, o) != 0x02014b50)
				return false;
			Ent e;
			e.metodo = le16(d_, o + 10);
			e.comp = le32(d_, o + 20);
			e.tam = le32(d_, o + 24);
			const int ln = le16(d_, o + 28), le = le16(d_, o + 30), lc = le16(d_, o + 32);
			e.local = le32(d_, o + 42);
			const QString nome = QString::fromUtf8(d_.mid(int(o + 46), ln));
			ents_.insert(nome, e);
			o += 46 + ln + le + lc;
		}
		return !ents_.isEmpty();
	}
	bool tem(const QString &nome) const { return ents_.contains(nome); }
	QStringList nomes() const { return ents_.keys(); }
	QByteArray ler(const QString &nome) const
	{
		auto it = ents_.find(nome);
		if (it == ents_.end())
			return {};
		const Ent &e = *it;
		if (le32(d_, e.local) != 0x04034b50)
			return {};
		const qint64 ini = e.local + 30 + le16(d_, e.local + 26) + le16(d_, e.local + 28);
		const QByteArray bruto = d_.mid(int(ini), int(e.comp));
		if (e.metodo == 0)
			return bruto;
		if (e.metodo != 8)
			return {};
		QByteArray saida(int(e.tam), Qt::Uninitialized);
		z_stream z{};
		if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
			return {};
		z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bruto.constData()));
		z.avail_in = uInt(bruto.size());
		z.next_out = reinterpret_cast<Bytef *>(saida.data());
		z.avail_out = uInt(saida.size());
		const int r = inflate(&z, Z_FINISH);
		inflateEnd(&z);
		if (r != Z_STREAM_END)
			return {};
		return saida;
	}

private:
	struct Ent {
		quint16 metodo = 0;
		quint32 comp = 0, tam = 0, local = 0;
	};
	QByteArray d_;
	QMap<QString, Ent> ents_;
};

class ZipEscritor
{
public:
	void add(const QString &nome, const QByteArray &dados, bool comprimir = true)
	{
		const QByteArray n = nome.toUtf8();
		const quint32 crc = quint32(crc32(0L, reinterpret_cast<const Bytef *>(dados.constData()), uInt(dados.size())));
		QByteArray corpo = dados;
		quint16 metodo = 0;
		if (comprimir && !dados.isEmpty()) {
			QByteArray c(int(compressBound(uLong(dados.size()))) + 64, Qt::Uninitialized);
			z_stream z{};
			deflateInit2(&z, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
			z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(dados.constData()));
			z.avail_in = uInt(dados.size());
			z.next_out = reinterpret_cast<Bytef *>(c.data());
			z.avail_out = uInt(c.size());
			deflate(&z, Z_FINISH);
			c.resize(int(z.total_out));
			deflateEnd(&z);
			corpo = c;
			metodo = 8;
		}
		const quint32 local = quint32(out_.size());
		auto cabecalho = [&](QByteArray &b, bool central) {
			put32(b, central ? 0x02014b50 : 0x04034b50);
			if (central)
				put16(b, 20); // feito por
			put16(b, 20);     // versão para extrair
			put16(b, 0x0800); // nomes em UTF-8
			put16(b, metodo);
			put16(b, 0);      // hora
			put16(b, 0x21);   // data: 1/1/1980
			put32(b, crc);
			put32(b, quint32(corpo.size()));
			put32(b, quint32(dados.size()));
			put16(b, quint16(n.size()));
			put16(b, 0);
			if (central) {
				put16(b, 0); put16(b, 0); put16(b, 0);
				put32(b, 0);
				put32(b, local);
			}
			b += n;
		};
		cabecalho(out_, false);
		out_ += corpo;
		cabecalho(central_, true);
		n_++;
	}
	QByteArray fechar()
	{
		QByteArray r = out_;
		const quint32 ini = quint32(r.size());
		r += central_;
		put32(r, 0x06054b50);
		put16(r, 0); put16(r, 0);
		put16(r, quint16(n_)); put16(r, quint16(n_));
		put32(r, quint32(central_.size()));
		put32(r, ini);
		put16(r, 0);
		return r;
	}

private:
	static void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); }
	static void put32(QByteArray &b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); }
	QByteArray out_, central_;
	int n_ = 0;
};

// --- XML -------------------------------------------------------------------
//
// Uma árvore pequena para ler o ODF e o OOXML. Os nomes vão com o prefixo
// de costume do espaço de nomes ("text:p", "w:r"), qualquer que seja o
// prefixo que o arquivo usou.

struct No {
	QString nome;  // vazio: um pedaço de texto
	QString texto;
	QHash<QString, QString> atr;
	std::vector<No> filhos;

	QString a(const QString &n, const QString &padrao = QString()) const { return atr.value(n, padrao); }
	const No *filho(const QString &n) const
	{
		for (const No &f : filhos)
			if (f.nome == n)
				return &f;
		return nullptr;
	}
	std::vector<const No *> todos(const QString &n) const
	{
		std::vector<const No *> r;
		for (const No &f : filhos)
			if (f.nome == n)
				r.push_back(&f);
		return r;
	}
	// Primeiro descendente com esse nome.
	const No *achar(const QString &n) const
	{
		for (const No &f : filhos) {
			if (f.nome == n)
				return &f;
			if (const No *x = f.achar(n))
				return x;
		}
		return nullptr;
	}
	QString textoTodo() const
	{
		if (nome.isEmpty())
			return texto;
		QString r;
		for (const No &f : filhos)
			r += f.textoTodo();
		return r;
	}
};

static QString prefixo(const QStringView uri, const QStringView dado)
{
	static const QHash<QString, QString> m = {
		{"urn:oasis:names:tc:opendocument:xmlns:office:1.0", "office"},
		{"urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style"},
		{"urn:oasis:names:tc:opendocument:xmlns:text:1.0", "text"},
		{"urn:oasis:names:tc:opendocument:xmlns:table:1.0", "table"},
		{"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0", "draw"},
		{"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0", "fo"},
		{"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0", "svg"},
		{"urn:oasis:names:tc:opendocument:xmlns:meta:1.0", "meta"},
		{"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0", "manifest"},
		{"http://www.w3.org/1999/xlink", "xlink"},
		{"http://purl.org/dc/elements/1.1/", "dc"},
		{"http://schemas.openxmlformats.org/wordprocessingml/2006/main", "w"},
		{"http://purl.oclc.org/ooxml/wordprocessingml/main", "w"},
		{"http://schemas.openxmlformats.org/officeDocument/2006/relationships", "r"},
		{"http://purl.oclc.org/ooxml/officeDocument/relationships", "r"},
		{"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing", "wp"},
		{"http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing", "wp"},
		{"http://schemas.openxmlformats.org/drawingml/2006/main", "a"},
		{"http://purl.oclc.org/ooxml/drawingml/main", "a"},
		{"http://schemas.openxmlformats.org/drawingml/2006/picture", "pic"},
		{"http://schemas.openxmlformats.org/package/2006/relationships", "rel"},
		{"urn:schemas-microsoft-com:vml", "v"},
		{"http://schemas.openxmlformats.org/officeDocument/2006/custom-properties", "cp"},
		{"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes", "vt"},
		{"http://www.w3.org/XML/1998/namespace", "xml"},
	};
	const QString u = uri.toString();
	auto it = m.find(u);
	if (it != m.end())
		return *it;
	return dado.toString();
}

static bool lerXml(const QByteArray &dados, No &raiz)
{
	if (dados.isEmpty())
		return false;
	QXmlStreamReader x(dados);
	std::vector<No *> pilha;
	raiz = No();
	raiz.nome = "#doc";
	pilha.push_back(&raiz);
	while (!x.atEnd()) {
		x.readNext();
		if (x.isStartElement()) {
			No n;
			const QString p = prefixo(x.namespaceUri(), x.prefix());
			n.nome = p.isEmpty() ? x.name().toString() : p + ":" + x.name().toString();
			for (const QXmlStreamAttribute &at : x.attributes()) {
				const QString pa = prefixo(at.namespaceUri(), at.prefix());
				n.atr.insert(pa.isEmpty() ? at.name().toString() : pa + ":" + at.name().toString(), at.value().toString());
			}
			pilha.back()->filhos.push_back(std::move(n));
			pilha.push_back(&pilha.back()->filhos.back());
		} else if (x.isEndElement()) {
			if (pilha.size() > 1)
				pilha.pop_back();
		} else if (x.isCharacters() && !x.isComment()) {
			No t;
			t.texto = x.text().toString();
			pilha.back()->filhos.push_back(std::move(t));
		}
	}
	return !x.hasError() && !raiz.filhos.empty();
}

// Um texto para dentro de um XML.
static QString esc(const QString &s)
{
	QString r;
	r.reserve(s.size());
	for (QChar c : s) {
		switch (c.unicode()) {
		case '&': r += "&amp;"; break;
		case '<': r += "&lt;"; break;
		case '>': r += "&gt;"; break;
		case '"': r += "&quot;"; break;
		default:
			if (c.unicode() < 0x20 && c != '\t' && c != '\n' && c != '\r')
				break;
			r += c;
		}
	}
	return r;
}

// --- a folha ---------------------------------------------------------------
//
// O tamanho do papel e as margens, em milímetros, guardados como
// propriedades do próprio QTextDocument (os leitores gravam, os gravadores
// e a tela leem). Com a altura da página, o QTextDocument pagina sozinho:
// as margens da moldura raiz valem para cada página.

struct Folha {
	double l = 210, a = 297;                     // A4
	double sup = 25, inf = 25, esq = 25, dir = 25;
	bool numeros = false;                        // números de página no rodapé
	QString esquema = "padrao";                  // "padrao" ou "abnt"
};

static Folha folhaDe(const QTextDocument *d)
{
	Folha f;
	auto le = [&](const char *n, double &v) {
		const QVariant x = d->property(n);
		if (x.isValid())
			v = x.toDouble();
	};
	le("folha.l", f.l); le("folha.a", f.a);
	le("folha.sup", f.sup); le("folha.inf", f.inf); le("folha.esq", f.esq); le("folha.dir", f.dir);
	if (d->property("folha.numeros").isValid())
		f.numeros = d->property("folha.numeros").toBool();
	if (d->property("folha.esquema").isValid())
		f.esquema = d->property("folha.esquema").toString();
	return f;
}

static void porFolha(QTextDocument *d, const Folha &f)
{
	d->setProperty("folha.l", f.l); d->setProperty("folha.a", f.a);
	d->setProperty("folha.sup", f.sup); d->setProperty("folha.inf", f.inf);
	d->setProperty("folha.esq", f.esq); d->setProperty("folha.dir", f.dir);
	d->setProperty("folha.numeros", f.numeros);
	d->setProperty("folha.esquema", f.esquema);
	QTextFrame *raiz = d->rootFrame();
	QTextFrameFormat ff = raiz->frameFormat();
	ff.setTopMargin(px(f.sup));
	ff.setBottomMargin(px(f.inf));
	ff.setLeftMargin(px(f.esq));
	ff.setRightMargin(px(f.dir));
	raiz->setFrameFormat(ff);
	d->setPageSize(QSizeF(px(f.l), px(f.a)));
}

struct Papel {
	const char *nome;
	double l, a;
};
static const Papel PAPEIS[] = {
	{"A4 (210 × 297 mm)", 210, 297},
	{"Carta (216 × 279 mm)", 215.9, 279.4},
	{"Ofício (216 × 356 mm)", 215.9, 355.6},
	{"A5 (148 × 210 mm)", 148, 210},
};

// --- estilos de parágrafo --------------------------------------------------
//
// Como no Pages: um estilo por parágrafo (o nome fica numa propriedade do
// bloco), com o tamanho em proporção à fonte do corpo. No esquema "abnt"
// (NBR 14724): títulos no tamanho do corpo, em negrito, os de primeiro nível
// em maiúsculas; o corpo com 1,5 de entrelinha, recuo de 1,25 cm na primeira
// linha e justificado; a citação longa em 10 pt, com recuo de 4 cm.

static constexpr int P_ESTILO = QTextFormat::UserProperty + 1;

struct Estilo {
	QString id, nome;
	int nivel = 0;       // 1 a 3 nos cabeçalhos
	double rel = 1.0;    // tamanho / tamanho do corpo
	bool negrito = false, italico = false, maiusculas = false;
	double antes = 0, depois = 0; // pt
	Qt::Alignment alin = Qt::AlignLeft | Qt::AlignAbsolute;
	double entrelinha = 100;      // %
	double recuo1 = 0;            // mm, a primeira linha
	double recuoEsq = 0;          // mm
	QColor cor;                   // inválida: a tinta
};

static const QStringList ESTILOS = {"titulo", "subtitulo", "cab1", "cab2", "cab3", "corpo", "citacao", "legenda"};

static Estilo estilo(const QString &id, const QString &esquema)
{
	Estilo e;
	e.id = id;
	const bool abnt = esquema == "abnt";
	if (id == "titulo") {
		e.nome = "Título";
		e.rel = abnt ? 1.0 : 2.3; e.negrito = true; e.maiusculas = abnt;
		e.depois = 12; e.alin = abnt ? Qt::AlignHCenter : (Qt::AlignLeft | Qt::AlignAbsolute);
		e.entrelinha = abnt ? 150 : 100;
	} else if (id == "subtitulo") {
		e.nome = "Subtítulo";
		e.rel = abnt ? 1.0 : 1.35; e.italico = !abnt;
		e.depois = 18; e.cor = abnt ? QColor() : QColor(0x5d, 0x59, 0x50);
		e.alin = abnt ? Qt::AlignHCenter : (Qt::AlignLeft | Qt::AlignAbsolute);
		e.entrelinha = abnt ? 150 : 100;
	} else if (id == "cab1") {
		e.nome = "Cabeçalho 1"; e.nivel = 1;
		e.rel = abnt ? 1.0 : 1.6; e.negrito = true; e.maiusculas = abnt;
		e.antes = abnt ? 12 : 18; e.depois = abnt ? 12 : 6;
		e.entrelinha = abnt ? 150 : 100;
	} else if (id == "cab2") {
		e.nome = "Cabeçalho 2"; e.nivel = 2;
		e.rel = abnt ? 1.0 : 1.3; e.negrito = true;
		e.antes = 12; e.depois = abnt ? 12 : 4;
		e.entrelinha = abnt ? 150 : 100;
	} else if (id == "cab3") {
		e.nome = "Cabeçalho 3"; e.nivel = 3;
		e.rel = abnt ? 1.0 : 1.1; e.negrito = !abnt; e.italico = abnt;
		e.antes = 10; e.depois = abnt ? 12 : 2;
		e.entrelinha = abnt ? 150 : 100;
	} else if (id == "citacao") {
		e.nome = "Citação";
		e.rel = abnt ? 10.0 / 12.0 : 1.0; e.italico = !abnt;
		e.recuoEsq = abnt ? 40 : 10;
		e.antes = abnt ? 12 : 6; e.depois = abnt ? 12 : 10;
		e.alin = abnt ? Qt::AlignJustify : (Qt::AlignLeft | Qt::AlignAbsolute);
		e.cor = abnt ? QColor() : QColor(0x3d, 0x3a, 0x34);
	} else if (id == "legenda") {
		e.nome = "Legenda";
		e.rel = abnt ? 10.0 / 12.0 : 0.85; e.italico = !abnt;
		e.antes = 4; e.depois = 12; e.alin = Qt::AlignHCenter;
		e.cor = abnt ? QColor() : QColor(0x5d, 0x59, 0x50);
	} else {
		e.id = "corpo";
		e.nome = "Corpo";
		e.depois = abnt ? 0 : 8;
		if (abnt) {
			e.entrelinha = 150; e.recuo1 = 12.5; e.alin = Qt::AlignJustify;
		} else {
			e.entrelinha = 115;
		}
	}
	return e;
}

static QString estiloDe(const QTextBlock &b)
{
	const QString s = b.blockFormat().stringProperty(P_ESTILO);
	return s.isEmpty() ? QStringLiteral("corpo") : s;
}

// O formato de letra e o de parágrafo de um estilo, na fonte do corpo.
static QTextCharFormat letraDoEstilo(const Estilo &e, const QFont &base)
{
	QTextCharFormat c;
	c.setFontFamilies(QStringList{base.family()});
	c.setFontPointSize(std::round(base.pointSizeF() * e.rel * 2) / 2);
	c.setFontWeight(e.negrito ? QFont::Bold : QFont::Normal);
	c.setFontItalic(e.italico);
	c.setFontCapitalization(e.maiusculas ? QFont::AllUppercase : QFont::MixedCase);
	c.setForeground(e.cor.isValid() ? QBrush(e.cor) : QBrush(QColor(0x1c, 0x1b, 0x19)));
	return c;
}

static QTextBlockFormat paragrafoDoEstilo(const Estilo &e, QTextBlockFormat b = QTextBlockFormat())
{
	b.setProperty(P_ESTILO, e.id);
	b.setHeadingLevel(e.nivel);
	b.setAlignment(e.alin);
	b.setTopMargin(pxDePt(e.antes));
	b.setBottomMargin(pxDePt(e.depois));
	b.setLineHeight(e.entrelinha, QTextBlockFormat::ProportionalHeight);
	b.setTextIndent(px(e.recuo1));
	b.setLeftMargin(px(e.recuoEsq));
	return b;
}

// Aplica o estilo aos parágrafos da seleção (ou ao do cursor). Com
// "manter" (mudou o esquema ou a fonte do corpo), só a fonte, o tamanho e o
// parágrafo mudam: o negrito e o itálico que a pessoa pôs ficam, e as células
// das tabelas guardam o alinhamento.
static void aplicarEstilo(QTextCursor c, const QString &id, bool manter = false)
{
	QTextDocument *d = c.document();
	const Estilo e = estilo(id, folhaDe(d).esquema);
	const QTextCharFormat letra = letraDoEstilo(e, d->defaultFont());
	int ini = c.selectionStart(), fim = c.selectionEnd();
	QTextBlock b = d->findBlock(ini);
	const QTextBlock ultimo = d->findBlock(fim);
	c.beginEditBlock();
	while (b.isValid()) {
		QTextCursor k(b);
		const bool naTabela = k.currentTable() != nullptr;
		QTextBlockFormat bf = b.blockFormat();
		// A quebra de página e a lista ficam.
		if (!(manter && naTabela)) {
			const Qt::Alignment alin = bf.alignment();
			bf = paragrafoDoEstilo(e, bf);
			if (b.textList() || naTabela)
				bf.setTextIndent(0);
			if (naTabela) {
				bf.setAlignment(alin);
				bf.setBottomMargin(0);
				bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
			}
			k.setBlockFormat(bf);
		}
		QTextCharFormat l = letra;
		if (manter) {
			l = QTextCharFormat();
			l.setFontFamilies(letra.fontFamilies().toStringList());
			l.setFontPointSize(letra.fontPointSize());
			l.setFontCapitalization(letra.fontCapitalization());
		}
		k.setBlockCharFormat(manter ? QTextCharFormat(b.charFormat()) : letra);
		if (manter)
			k.mergeBlockCharFormat(l);
		k.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
		k.mergeCharFormat(l);
		if (b == ultimo)
			break;
		b = b.next();
	}
	c.endEditBlock();
}

// Reaplica os estilos de todos os parágrafos (mudou o esquema ou a fonte).
static void reaplicarEstilos(QTextDocument *d)
{
	QTextCursor c(d);
	c.beginEditBlock();
	for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
		QTextCursor k(b);
		aplicarEstilo(k, estiloDe(b), true);
	}
	c.endEditBlock();
}

// --- modelos ---------------------------------------------------------------

struct Modelo {
	QString id, nome, descricao;
};

static const QList<Modelo> &modelos()
{
	static const QList<Modelo> m = {
		{"branco", "Em branco", "Uma folha A4 limpa"},
		{"abnt", "Trabalho acadêmico", "Nas normas da ABNT: capa, seções e referências"},
		{"aula", "Plano de aula", "Tema, objetivos, conteúdo, metodologia e avaliação"},
		{"relatorio", "Relatório", "Resumo, seções e uma tabela de indicadores"},
		{"carta", "Carta", "Local e data, destinatário e assinatura"},
		{"curriculo", "Currículo", "Formação, experiência e habilidades"},
	};
	return m;
}

// Monta um documento parágrafo por parágrafo, cada um com um estilo.
class Montador
{
public:
	explicit Montador(QTextDocument *d) : d_(d), c_(d) {}
	void par(const QString &estiloId, const QString &texto, Qt::Alignment alin = {}, bool quebra = false)
	{
		const Estilo e = estilo(estiloId, folhaDe(d_).esquema);
		QTextBlockFormat bf = paragrafoDoEstilo(e);
		if (alin)
			bf.setAlignment(alin);
		if (quebra)
			bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
		const QTextCharFormat cf = letraDoEstilo(e, d_->defaultFont());
		if (primeiro_) {
			c_.setBlockFormat(bf);
			c_.setBlockCharFormat(cf);
			primeiro_ = false;
		} else {
			c_.insertBlock(bf, cf);
		}
		inserirRico(texto, cf);
	}
	// **negrito** e _itálico_ dentro do texto.
	void inserirRico(const QString &t, const QTextCharFormat &base)
	{
		QTextCharFormat f = base;
		const bool negritoBase = base.fontWeight() >= QFont::DemiBold;
		QString acum;
		auto solta = [&] {
			if (!acum.isEmpty())
				c_.insertText(acum, f);
			acum.clear();
		};
		for (int i = 0; i < t.size(); i++) {
			if (t.mid(i, 2) == "**") {
				solta();
				f.setFontWeight(f.fontWeight() >= QFont::DemiBold && !negritoBase ? QFont::Normal : QFont::Bold);
				i++;
			} else if (t[i] == '_' && (i == 0 || !t[i - 1].isLetterOrNumber())) {
				solta();
				f.setFontItalic(!f.fontItalic());
			} else if (t[i] == '_' && i + 1 <= t.size() && (i + 1 == t.size() || !t[i + 1].isLetterOrNumber())) {
				solta();
				f.setFontItalic(!f.fontItalic());
			} else if (t[i] == '\n') {
				solta();
				c_.insertText(QString(QChar::LineSeparator), f);
			} else {
				acum += t[i];
			}
		}
		solta();
	}
	void lista(const QStringList &itens, bool numerada = false)
	{
		QTextList *l = nullptr;
		for (const QString &i : itens) {
			par("corpo", i);
			QTextBlockFormat bf = c_.blockFormat();
			bf.setTextIndent(0);
			bf.setBottomMargin(pxDePt(2));
			c_.setBlockFormat(bf);
			if (!l) {
				QTextListFormat lf;
				lf.setStyle(numerada ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
				lf.setIndent(1);
				l = c_.createList(lf);
			} else {
				l->add(c_.block());
			}
		}
	}
	void tabela(const QList<QStringList> &linhas)
	{
		par("corpo", QString());
		QTextTableFormat tf = formatoTabela();
		QTextTable *t = c_.insertTable(int(linhas.size()), int(linhas[0].size()), tf);
		const Estilo e = estilo("corpo", folhaDe(d_).esquema);
		for (int r = 0; r < linhas.size(); r++) {
			for (int k = 0; k < linhas[r].size(); k++) {
				QTextCursor cc = t->cellAt(r, k).firstCursorPosition();
				QTextBlockFormat bf = paragrafoDoEstilo(e);
				bf.setTextIndent(0);
				bf.setBottomMargin(0);
				bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
				if (k > 0)
					bf.setAlignment(Qt::AlignRight | Qt::AlignAbsolute);
				cc.setBlockFormat(bf);
				QTextCharFormat cf = letraDoEstilo(e, d_->defaultFont());
				if (r == 0)
					cf.setFontWeight(QFont::Bold);
				cc.insertText(linhas[r][k], cf);
			}
		}
		c_ = QTextCursor(t->lastCursorPosition());
		c_.movePosition(QTextCursor::NextBlock);
		primeiro_ = true;
	}
	static QTextTableFormat formatoTabela()
	{
		QTextTableFormat tf;
		tf.setBorder(1);
		tf.setBorderBrush(QColor(0x1c, 0x1b, 0x19));
		tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
		tf.setBorderCollapse(true);
		tf.setCellPadding(5);
		tf.setCellSpacing(0);
		tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
		tf.setTopMargin(4);
		tf.setBottomMargin(10);
		return tf;
	}

private:
	QTextDocument *d_;
	QTextCursor c_;
	bool primeiro_ = true;
};

static QString hojePorExtenso()
{
	return QLocale(QLocale::Portuguese, QLocale::Brazil).toString(QDate::currentDate(), "d 'de' MMMM 'de' yyyy");
}

static void documentoNovo(QTextDocument *d, const QString &fonte, double tamanho, const Folha &f)
{
	d->clear();
	QFont base(fonte);
	base.setPointSizeF(tamanho);
	d->setDefaultFont(base);
	d->setDocumentMargin(0);
	porFolha(d, f);
	d->setUndoRedoEnabled(false);
}

static void montarModelo(QTextDocument *d, const QString &id)
{
	Folha f;
	if (id == "abnt") {
		// NBR 14724: margens de 3 cm em cima e à esquerda, 2 cm embaixo e à
		// direita; fonte 12; números de página.
		f.sup = 30; f.esq = 30; f.inf = 20; f.dir = 20;
		f.esquema = "abnt";
		f.numeros = true;
		documentoNovo(d, "Times New Roman", 12, f);
	} else if (id == "carta" || id == "curriculo") {
		f.sup = 25; f.inf = 25; f.esq = 25; f.dir = 25;
		documentoNovo(d, "IBM Plex Serif", 11, f);
	} else {
		f.numeros = id == "relatorio" || id == "aula";
		documentoNovo(d, "IBM Plex Serif", 11.5, f);
	}
	Montador m(d);
	const int ano = QDate::currentDate().year();
	if (id == "abnt") {
		const Qt::Alignment centro = Qt::AlignHCenter;
		m.par("titulo", "Nome da instituição", centro);
		m.par("subtitulo", "Curso de Graduação em Economia", centro);
		for (int i = 0; i < 4; i++)
			m.par("corpo", QString(), centro);
		m.par("subtitulo", "NOME DO AUTOR", centro);
		for (int i = 0; i < 4; i++)
			m.par("corpo", QString(), centro);
		m.par("titulo", "Título do trabalho: subtítulo, se houver", centro);
		for (int i = 0; i < 9; i++)
			m.par("corpo", QString(), centro);
		m.par("subtitulo", QString("Cidade\n%1").arg(ano), centro);
		m.par("cab1", "1 Introdução", {}, true);
		m.par("corpo", "Apresente o tema, o problema de pesquisa, os objetivos e a justificativa. "
		               "O texto vai em fonte 12, com espaçamento de 1,5 entre as linhas e recuo de "
		               "1,25 cm na primeira linha de cada parágrafo, como pede a NBR 14724.");
		m.par("corpo", "As citações diretas de até três linhas ficam no corpo do texto, entre aspas, "
		               "com o autor, o ano e a página (SOBRENOME, " + QString::number(ano) + ", p. 12).");
		m.par("cab1", "2 Desenvolvimento");
		m.par("cab2", "2.1 Referencial teórico");
		m.par("corpo", "Escreva aqui a revisão da literatura. Uma citação com mais de três linhas "
		               "vai em parágrafo próprio, com recuo de 4 cm, fonte menor e sem aspas:");
		m.par("citacao", "A citação longa fica destacada do texto, com recuo de 4 cm da margem "
		                 "esquerda, em fonte 10 e espaçamento simples entre as linhas, terminando "
		                 "com a indicação da fonte (SOBRENOME, " + QString::number(ano) + ", p. 45).");
		m.par("cab2", "2.2 Metodologia");
		m.par("corpo", "Descreva os dados, o método e as ferramentas usadas.");
		m.par("legenda", "Tabela 1 — Indicadores selecionados");
		m.tabela({{"Indicador", "2024", "2025"}, {"PIB (variação %)", "3,4", "2,3"}, {"IPCA (%)", "4,83", "4,26"}});
		m.par("legenda", "Fonte: elaborada pelo autor com dados do IBGE.");
		m.par("cab1", "3 Conclusão");
		m.par("corpo", "Retome os objetivos e diga o que o trabalho encontrou.");
		m.par("cab1", "Referências", {}, true);
		QTextCursor c(d);
		c.movePosition(QTextCursor::End);
		QTextBlockFormat bf = c.blockFormat();
		bf.setAlignment(Qt::AlignHCenter);
		c.setBlockFormat(bf);
		auto ref = [&](const QString &t) {
			m.par("corpo", t);
			QTextCursor k(d);
			k.movePosition(QTextCursor::End);
			QTextBlockFormat b = k.blockFormat();
			b.setTextIndent(0);
			b.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
			b.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
			b.setBottomMargin(pxDePt(12));
			k.setBlockFormat(b);
		};
		ref("SOBRENOME, Nome. **Título do livro**: subtítulo. 2. ed. São Paulo: Editora, " + QString::number(ano - 2) + ".");
		ref("SOBRENOME, Nome; SOBRENOME, Nome. Título do artigo. **Nome da Revista**, Rio de Janeiro, v. 10, n. 2, p. 15-30, " + QString::number(ano - 1) + ".");
	} else if (id == "aula") {
		m.par("titulo", "Plano de aula");
		m.par("subtitulo", "Disciplina · Turma · " + hojePorExtenso());
		m.par("cab2", "Tema");
		m.par("corpo", "Juros simples e compostos");
		m.par("cab2", "Objetivos");
		m.lista({"Diferenciar juros simples de juros compostos.",
		         "Calcular o montante de uma aplicação mês a mês.",
		         "Ler a taxa de um financiamento e compará-la com a de uma aplicação."});
		m.par("cab2", "Conteúdo");
		m.par("corpo", "Capital, taxa, prazo e montante; a fórmula _M = C (1 + i)^n_; taxas equivalentes.");
		m.par("cab2", "Metodologia");
		m.lista({"Exposição dialogada com exemplos do dia a dia (cartão de crédito, poupança).",
		         "Exercícios em dupla com a calculadora (ou as Finanças do LumeBSD).",
		         "Correção coletiva no quadro."}, true);
		m.par("cab2", "Recursos");
		m.par("corpo", "Quadro, projetor e a lista de exercícios impressa.");
		m.par("cab2", "Avaliação");
		m.par("corpo", "Participação nos exercícios e uma questão de saída, entregue ao fim da aula.");
	} else if (id == "relatorio") {
		m.par("titulo", "Relatório trimestral");
		m.par("subtitulo", "Equipe ou departamento · " + hojePorExtenso());
		m.par("cab1", "Resumo");
		m.par("corpo", "Em poucas linhas, o que aconteceu no período e o que se recomenda. Quem ler só "
		               "este parágrafo precisa sair sabendo o principal.");
		m.par("cab1", "Indicadores");
		m.tabela({{"Indicador", "Meta", "Resultado"}, {"Receita (R$ mil)", "1.200", "1.315"},
		          {"Clientes novos", "80", "74"}, {"Satisfação (0 a 10)", "8,5", "8,9"}});
		m.par("legenda", "Tabela 1 — Indicadores do trimestre");
		m.par("cab1", "Análise");
		m.par("corpo", "Explique os números: o que superou a meta, o que ficou abaixo e por quê.");
		m.par("cab1", "Próximos passos");
		m.lista({"Primeira ação, com responsável e prazo.", "Segunda ação.", "Terceira ação."});
	} else if (id == "carta") {
		m.par("corpo", "Cidade, " + hojePorExtenso() + ".", Qt::AlignRight | Qt::AlignAbsolute);
		m.par("corpo", QString());
		m.par("corpo", "À\n**Nome do destinatário**\nCargo · Empresa\nEndereço");
		m.par("corpo", QString());
		m.par("corpo", "**Assunto:** escreva o assunto em uma linha");
		m.par("corpo", QString());
		m.par("corpo", "Prezado(a) Senhor(a),");
		m.par("corpo", "Escreva aqui o motivo da carta. Seja direto: o primeiro parágrafo diz o que "
		               "você quer; os seguintes, por quê.");
		m.par("corpo", "Agradeço desde já a atenção e fico à disposição.");
		m.par("corpo", QString());
		m.par("corpo", "Atenciosamente,");
		m.par("corpo", QString());
		m.par("corpo", QString());
		m.par("corpo", "**Seu nome**\nTelefone · e-mail");
	} else if (id == "curriculo") {
		m.par("titulo", "Seu Nome Completo");
		m.par("subtitulo", "Cidade, UF · (00) 00000-0000 · voce@exemplo.com.br");
		m.par("cab2", "Objetivo");
		m.par("corpo", "Uma frase sobre a vaga ou a área em que você quer trabalhar.");
		m.par("cab2", "Formação");
		m.par("corpo", "**Bacharelado em Ciências Econômicas** · Universidade · " + QString::number(ano - 4) + "–" + QString::number(ano));
		m.par("cab2", "Experiência");
		m.par("corpo", "**Cargo** · Empresa · " + QString::number(ano - 2) + "–hoje");
		m.lista({"O que você fez, com um número quando der (reduziu o prazo em 20%).",
		         "Outra realização."});
		m.par("cab2", "Habilidades");
		m.lista({"Excel e estatística (regressão, séries temporais)", "Inglês avançado",
		         "Comunicação e trabalho em equipe"});
	} else {
		m.par("corpo", QString());
	}
	d->setUndoRedoEnabled(true);
	d->setModified(false);
}

// --- o que os gravadores leem do documento ---------------------------------

struct Pedaco {
	QString texto;
	QTextCharFormat f;
	bool imagem = false;
};

static QList<Pedaco> pedacos(const QTextBlock &b)
{
	QList<Pedaco> r;
	for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
		const QTextFragment fr = it.fragment();
		if (!fr.isValid())
			continue;
		const QTextCharFormat f = fr.charFormat();
		if (f.isImageFormat()) {
			// Um fragmento pode ter várias imagens seguidas.
			for (int i = 0; i < fr.text().size(); i++) {
				Pedaco p;
				p.imagem = true;
				p.f = f;
				r << p;
			}
			continue;
		}
		Pedaco p;
		p.texto = fr.text();
		p.f = f;
		r << p;
	}
	return r;
}

// A imagem de um formato de imagem, e o tamanho com que aparece (px).
static QImage imagemDe(const QTextDocument *d, const QTextImageFormat &f, QSizeF *tam)
{
	QImage img;
	const QVariant v = d->resource(QTextDocument::ImageResource, QUrl(f.name()));
	if (v.canConvert<QImage>())
		img = v.value<QImage>();
	if (img.isNull() && v.canConvert<QPixmap>())
		img = v.value<QPixmap>().toImage();
	if (img.isNull())
		img.load(QUrl(f.name()).isLocalFile() ? QUrl(f.name()).toLocalFile() : f.name());
	if (tam) {
		double w = f.width(), h = f.height();
		if (img.isNull()) {
			*tam = QSizeF(w > 0 ? w : 32, h > 0 ? h : 32);
		} else if (w > 0 && h <= 0) {
			*tam = QSizeF(w, w * img.height() / std::max(1, img.width()));
		} else if (h > 0 && w <= 0) {
			*tam = QSizeF(h * img.width() / std::max(1, img.height()), h);
		} else if (w > 0 && h > 0) {
			*tam = QSizeF(w, h);
		} else {
			*tam = QSizeF(img.width(), img.height());
		}
	}
	return img;
}

static QByteArray png(const QImage &img)
{
	QByteArray b;
	QBuffer buf(&b);
	buf.open(QIODevice::WriteOnly);
	img.save(&buf, "PNG");
	return b;
}

static QString familiaDe(const QTextCharFormat &f)
{
	if (f.hasProperty(QTextFormat::FontFamilies)) {
		const QStringList l = f.fontFamilies().toStringList();
		if (!l.isEmpty())
			return l.first();
	}
	return QString();
}

static bool listaNumerada(const QTextListFormat &lf)
{
	switch (lf.style()) {
	case QTextListFormat::ListDisc:
	case QTextListFormat::ListCircle:
	case QTextListFormat::ListSquare:
	case QTextListFormat::ListStyleUndefined:
		return false;
	default:
		return true;
	}
}

// Os nomes dos estilos nos outros programas.
static QString nomeOdf(const QString &id)
{
	static const QHash<QString, QString> m = {
		{"titulo", "Title"}, {"subtitulo", "Subtitle"}, {"cab1", "Heading_20_1"},
		{"cab2", "Heading_20_2"}, {"cab3", "Heading_20_3"}, {"citacao", "Quotations"},
		{"legenda", "Caption"}, {"corpo", "Text_20_body"}};
	return m.value(id, "Text_20_body");
}
static QString nomeDocx(const QString &id)
{
	static const QHash<QString, QString> m = {
		{"titulo", "Title"}, {"subtitulo", "Subtitle"}, {"cab1", "Heading1"},
		{"cab2", "Heading2"}, {"cab3", "Heading3"}, {"citacao", "Quote"},
		{"legenda", "Caption"}, {"corpo", "Normal"}};
	return m.value(id, "Normal");
}
// O contrário: de um nome (de qualquer um dos dois, ou do Word em português)
// para o nosso estilo; vazio se não for nenhum.
static QString idDeNome(QString n)
{
	n = n.toLower().replace("_20_", " ").replace('_', ' ').trimmed();
	if (n == "title" || n == "título" || n == "titulo") return "titulo";
	if (n == "subtitle" || n == "subtítulo" || n == "subtitulo") return "subtitulo";
	if (n == "heading 1" || n == "heading1" || n == "título 1" || n == "titulo 1" || n == "cabeçalho 1") return "cab1";
	if (n == "heading 2" || n == "heading2" || n == "título 2" || n == "titulo 2" || n == "cabeçalho 2") return "cab2";
	if (n.startsWith("heading") || n.startsWith("título ") || n.startsWith("titulo ") || n.startsWith("cabeçalho ")) return "cab3";
	if (n == "quotations" || n == "quote" || n == "citação" || n == "citacao" || n == "intense quote" || n == "block quotation" || n == "blockquotation" || n == "block text" || n == "citações") return "citacao";
	if (n == "caption" || n == "legenda") return "legenda";
	if (n == "text body" || n == "body text" || n == "normal" || n == "standard" || n == "corpo") return "corpo";
	return QString();
}

// --- ODT: gravar -----------------------------------------------------------

class EscritorOdt
{
public:
	explicit EscritorOdt(const QTextDocument *d) : d_(d) {}

	QByteArray gravar()
	{
		QString corpo;
		{
			QXmlStreamWriter x(&corpo);
			x.setAutoFormatting(false);
			fluxo(x, d_->rootFrame()->begin(), d_->rootFrame()->end());
			fecharListas(x);
		}
		const Folha f = folhaDe(d_);
		QString c = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		            "<office:document-content " + NS + " office:version=\"1.3\">"
		            "<office:automatic-styles>" + autos_ + "</office:automatic-styles>"
		            "<office:body><office:text>" + corpo + "</office:text></office:body>"
		            "</office:document-content>";
		ZipEscritor z;
		z.add("mimetype", "application/vnd.oasis.opendocument.text", false);
		z.add("content.xml", c.toUtf8());
		z.add("styles.xml", estilos(f).toUtf8());
		z.add("meta.xml", meta(f).toUtf8());
		QString man = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		              "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
		              "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
		              "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
		              "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
		              "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>";
		for (const auto &[nome, dados] : imagens_) {
			z.add(nome, dados, false);
			man += "<manifest:file-entry manifest:full-path=\"" + nome + "\" manifest:media-type=\"image/png\"/>";
		}
		man += "</manifest:manifest>";
		z.add("META-INF/manifest.xml", man.toUtf8());
		return z.fechar();
	}

private:
	inline static const QString NS =
		"xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
		"xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
		"xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
		"xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
		"xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
		"xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
		"xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
		"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
		"xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" "
		"xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"";

	static QString propsTexto(const QTextCharFormat &f)
	{
		QString r;
		const QString fam = familiaDe(f);
		if (!fam.isEmpty())
			r += " fo:font-family=\"'" + esc(fam) + "'\" style:font-name=\"" + esc(fam) + "\"";
		if (f.hasProperty(QTextFormat::FontPointSize))
			r += " fo:font-size=\"" + pt(f.fontPointSize()) + "\"";
		if (f.hasProperty(QTextFormat::FontWeight))
			r += f.fontWeight() >= QFont::DemiBold ? " fo:font-weight=\"bold\"" : " fo:font-weight=\"normal\"";
		if (f.hasProperty(QTextFormat::FontItalic))
			r += f.fontItalic() ? " fo:font-style=\"italic\"" : " fo:font-style=\"normal\"";
		if (f.hasProperty(QTextFormat::FontUnderline) || f.hasProperty(QTextFormat::TextUnderlineStyle)) {
			if (f.fontUnderline())
				r += " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"";
			else
				r += " style:text-underline-style=\"none\"";
		}
		if (f.hasProperty(QTextFormat::FontStrikeOut))
			r += f.fontStrikeOut() ? " style:text-line-through-style=\"solid\"" : " style:text-line-through-style=\"none\"";
		if (f.hasProperty(QTextFormat::ForegroundBrush) && f.foreground().style() != Qt::NoBrush)
			r += " fo:color=\"" + corHex(f.foreground().color()) + "\"";
		if (f.hasProperty(QTextFormat::BackgroundBrush) && f.background().style() != Qt::NoBrush)
			r += " fo:background-color=\"" + corHex(f.background().color()) + "\"";
		if (f.hasProperty(QTextFormat::TextVerticalAlignment)) {
			if (f.verticalAlignment() == QTextCharFormat::AlignSuperScript)
				r += " style:text-position=\"super 58%\"";
			else if (f.verticalAlignment() == QTextCharFormat::AlignSubScript)
				r += " style:text-position=\"sub 58%\"";
		}
		if (f.hasProperty(QTextFormat::FontCapitalization) && f.fontCapitalization() == QFont::AllUppercase)
			r += " fo:text-transform=\"uppercase\"";
		return r;
	}

	static QString alinhamento(Qt::Alignment a)
	{
		if (a & Qt::AlignHCenter) return "center";
		if (a & Qt::AlignJustify) return "justify";
		if (a & Qt::AlignRight) return "end";
		return "start";
	}

	static QString propsParagrafo(const QTextBlockFormat &b, bool comLista)
	{
		QString r;
		if (b.hasProperty(QTextFormat::BlockAlignment))
			r += " fo:text-align=\"" + alinhamento(b.alignment()) + "\"";
		if (b.hasProperty(QTextFormat::BlockTopMargin))
			r += " fo:margin-top=\"" + cm(b.topMargin()) + "\"";
		if (b.hasProperty(QTextFormat::BlockBottomMargin))
			r += " fo:margin-bottom=\"" + cm(b.bottomMargin()) + "\"";
		if (b.hasProperty(QTextFormat::BlockLeftMargin) && !comLista)
			r += " fo:margin-left=\"" + cm(b.leftMargin()) + "\"";
		if (b.hasProperty(QTextFormat::TextIndent) && !comLista)
			r += " fo:text-indent=\"" + cm(b.textIndent()) + "\"";
		if (b.hasProperty(QTextFormat::LineHeight)) {
			const double h = b.lineHeight();
			switch (b.lineHeightType()) {
			case QTextBlockFormat::ProportionalHeight:
				r += " fo:line-height=\"" + QString::number(h, 'g', 4) + "%\"";
				break;
			case QTextBlockFormat::FixedHeight:
				r += " fo:line-height=\"" + cm(h) + "\"";
				break;
			case QTextBlockFormat::MinimumHeight:
				r += " style:line-height-at-least=\"" + cm(h) + "\"";
				break;
			default:
				break;
			}
		}
		if (b.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
			r += " fo:break-before=\"page\"";
		return r;
	}

	// Um estilo automático (P1, T1...) por combinação, sem repetir.
	QString automatico(const QString &familia, const QString &pai, const QString &corpo, QMap<QString, QString> &mapa, const QString &prefixo)
	{
		const QString chave = familia + "|" + pai + "|" + corpo;
		auto it = mapa.find(chave);
		if (it != mapa.end())
			return *it;
		const QString nome = prefixo + QString::number(mapa.size() + 1);
		mapa.insert(chave, nome);
		autos_ += "<style:style style:name=\"" + nome + "\" style:family=\"" + familia + "\"" +
		          (pai.isEmpty() ? QString() : " style:parent-style-name=\"" + pai + "\"") + ">" + corpo + "</style:style>";
		return nome;
	}

	QString estiloParagrafo(const QTextBlock &b, bool comLista)
	{
		const QString pp = propsParagrafo(b.blockFormat(), comLista);
		QString corpo;
		if (!pp.isEmpty())
			corpo += "<style:paragraph-properties" + pp + "/>";
		return automatico("paragraph", nomeOdf(estiloDe(b)), corpo, ps_, "P");
	}

	QString estiloTexto(const QTextCharFormat &f)
	{
		const QString tp = propsTexto(f);
		if (tp.isEmpty())
			return QString();
		return automatico("text", QString(), "<style:text-properties" + tp + "/>", ts_, "T");
	}

	static void textoOdf(QXmlStreamWriter &x, const QString &s, bool &inicio)
	{
		QString acum;
		auto solta = [&] {
			if (!acum.isEmpty())
				x.writeCharacters(acum);
			acum.clear();
		};
		for (int i = 0; i < s.size(); i++) {
			const QChar c = s[i];
			if (c == '\t') {
				solta();
				x.writeEmptyElement("text:tab");
				inicio = false;
			} else if (c == QChar::LineSeparator || c == '\n') {
				solta();
				x.writeEmptyElement("text:line-break");
				inicio = true;
			} else if (c == ' ') {
				int n = 1;
				while (i + n < s.size() && s[i + n] == ' ')
					n++;
				if (!inicio)
					acum += ' ';
				const int resto = inicio ? n : n - 1;
				if (resto > 0) {
					solta();
					x.writeEmptyElement("text:s");
					if (resto > 1)
						x.writeAttribute("text:c", QString::number(resto));
				}
				i += n - 1;
				inicio = false;
			} else if (c == QChar::ObjectReplacementCharacter) {
				// (as imagens vêm separadas)
			} else {
				acum += c;
				inicio = false;
			}
		}
		solta();
	}

	void paragrafo(QXmlStreamWriter &x, const QTextBlock &b, bool comLista)
	{
		const int nivel = b.blockFormat().headingLevel();
		x.writeStartElement(nivel > 0 ? "text:h" : "text:p");
		x.writeAttribute("text:style-name", estiloParagrafo(b, comLista));
		if (nivel > 0)
			x.writeAttribute("text:outline-level", QString::number(nivel));
		bool inicio = true;
		for (const Pedaco &p : pedacos(b)) {
			if (p.imagem) {
				imagem(x, p.f.toImageFormat());
				inicio = false;
				continue;
			}
			const QString t = estiloTexto(p.f);
			if (!t.isEmpty()) {
				x.writeStartElement("text:span");
				x.writeAttribute("text:style-name", t);
			}
			textoOdf(x, p.texto, inicio);
			if (!t.isEmpty())
				x.writeEndElement();
		}
		x.writeEndElement();
	}

	void imagem(QXmlStreamWriter &x, const QTextImageFormat &f)
	{
		QSizeF tam;
		const QImage img = imagemDe(d_, f, &tam);
		if (img.isNull())
			return;
		const int n = int(imagens_.size()) + 1;
		const QString nome = QString("Pictures/imagem%1.png").arg(n);
		imagens_.push_back({nome, png(img)});
		x.writeStartElement("draw:frame");
		x.writeAttribute("draw:name", QString("Imagem %1").arg(n));
		x.writeAttribute("text:anchor-type", "as-char");
		x.writeAttribute("svg:width", cm(tam.width()));
		x.writeAttribute("svg:height", cm(tam.height()));
		x.writeAttribute("draw:z-index", "0");
		x.writeEmptyElement("draw:image");
		x.writeAttribute("xlink:href", nome);
		x.writeAttribute("xlink:type", "simple");
		x.writeAttribute("xlink:show", "embed");
		x.writeAttribute("xlink:actuate", "onLoad");
		x.writeEndElement();
	}

	// As listas: uma pilha de níveis abertos (text:list dentro de
	// text:list-item, como o ODF pede).
	struct Nivel {
		QTextList *lista;
		bool itemAberto;
	};
	std::vector<Nivel> pilha_;

	void fecharListas(QXmlStreamWriter &x, size_t ate = 0)
	{
		while (pilha_.size() > ate) {
			if (pilha_.back().itemAberto)
				x.writeEndElement(); // list-item
			x.writeEndElement();     // list
			pilha_.pop_back();
		}
	}

	QString estiloLista(bool numerada)
	{
		const QString nome = numerada ? "LN" : "LB";
		if (!listasFeitas_.contains(nome)) {
			listasFeitas_.insert(nome);
			QString s = "<text:list-style style:name=\"" + nome + "\">";
			static const QString bolas[] = {"•", "◦", "▪"};
			for (int n = 1; n <= 9; n++) {
				const QString pos = "<style:list-level-properties text:list-level-position-and-space-mode=\"label-alignment\">"
				                    "<style:list-level-label-alignment text:label-followed-by=\"listtab\" text:list-tab-stop-position=\"" +
				                    cm(px(6.35 * n + 1)) + "\" fo:text-indent=\"-0.635cm\" fo:margin-left=\"" + cm(px(6.35 * n + 1)) +
				                    "\"/></style:list-level-properties>";
				if (numerada)
					s += "<text:list-level-style-number text:level=\"" + QString::number(n) + "\" style:num-suffix=\".\" style:num-format=\"1\">" + pos + "</text:list-level-style-number>";
				else
					s += "<text:list-level-style-bullet text:level=\"" + QString::number(n) + "\" text:bullet-char=\"" + bolas[(n - 1) % 3] + "\">" + pos + "</text:list-level-style-bullet>";
			}
			s += "</text:list-style>";
			autos_ += s;
		}
		return nome;
	}

	void itemDeLista(QXmlStreamWriter &x, const QTextBlock &b, QTextList *l)
	{
		const size_t nivel = size_t(std::max(1, l->format().indent()));
		fecharListas(x, nivel);
		// O mesmo nível com outra lista (de marcadores para numerada): fecha.
		if (pilha_.size() == nivel && pilha_.back().lista && pilha_.back().lista != l)
			fecharListas(x, nivel - 1);
		if (pilha_.size() == nivel && pilha_.back().itemAberto) {
			x.writeEndElement();
			pilha_.back().itemAberto = false;
		}
		while (pilha_.size() < nivel) {
			if (!pilha_.empty() && !pilha_.back().itemAberto) {
				x.writeStartElement("text:list-item");
				pilha_.back().itemAberto = true;
			}
			x.writeStartElement("text:list");
			x.writeAttribute("text:style-name", estiloLista(listaNumerada(l->format())));
			// Os níveis do meio (pulados) não são de lista nenhuma.
			pilha_.push_back({pilha_.size() + 1 == nivel ? l : nullptr, false});
		}
		x.writeStartElement("text:list-item");
		pilha_.back().itemAberto = true;
		paragrafo(x, b, true);
	}

	void fluxo(QXmlStreamWriter &x, QTextFrame::iterator it, QTextFrame::iterator fim)
	{
		for (; it != fim && !it.atEnd(); ++it) {
			if (QTextFrame *f = it.currentFrame()) {
				fecharListas(x);
				if (auto *t = qobject_cast<QTextTable *>(f))
					tabela(x, t);
				else
					fluxo(x, f->begin(), f->end());
				continue;
			}
			const QTextBlock b = it.currentBlock();
			if (QTextList *l = b.textList()) {
				itemDeLista(x, b, l);
			} else {
				fecharListas(x);
				paragrafo(x, b, false);
			}
		}
	}

	void tabela(QXmlStreamWriter &x, QTextTable *t)
	{
		const int n = ++tabelas_;
		const QString ts = "Tabela" + QString::number(n);
		const Folha fo = folhaDe(d_);
		autos_ += "<style:style style:name=\"" + ts + "\" style:family=\"table\"><style:table-properties style:width=\"" +
		          cm(px(fo.l - fo.esq - fo.dir)) + "\" table:align=\"margins\" fo:margin-top=\"0.1cm\" fo:margin-bottom=\"0.3cm\"/></style:style>";
		if (!celula_) {
			celula_ = true;
			autos_ += "<style:style style:name=\"Celula\" style:family=\"table-cell\"><style:table-cell-properties "
			          "fo:padding=\"0.13cm\" fo:border=\"0.5pt solid #1c1b19\"/></style:style>";
		}
		// As larguras das colunas, se o documento disser.
		const QVector<QTextLength> larg = t->format().columnWidthConstraints();
		x.writeStartElement("table:table");
		x.writeAttribute("table:name", ts);
		x.writeAttribute("table:style-name", ts);
		for (int c = 0; c < t->columns(); c++) {
			const QString cs = ts + "." + QString::number(c + 1);
			double rel = 1000;
			if (c < larg.size() && larg[c].type() == QTextLength::PercentageLength)
				rel = larg[c].rawValue() * 100;
			autos_ += "<style:style style:name=\"" + cs + "\" style:family=\"table-column\"><style:table-column-properties style:rel-column-width=\"" +
			          QString::number(qRound(rel)) + "*\"/></style:style>";
			x.writeEmptyElement("table:table-column");
			x.writeAttribute("table:style-name", cs);
		}
		for (int r = 0; r < t->rows(); r++) {
			x.writeStartElement("table:table-row");
			for (int c = 0; c < t->columns(); c++) {
				const QTextTableCell cel = t->cellAt(r, c);
				if (cel.row() != r || cel.column() != c) {
					x.writeEmptyElement("table:covered-table-cell");
					continue;
				}
				x.writeStartElement("table:table-cell");
				x.writeAttribute("table:style-name", "Celula");
				x.writeAttribute("office:value-type", "string");
				if (cel.columnSpan() > 1)
					x.writeAttribute("table:number-columns-spanned", QString::number(cel.columnSpan()));
				if (cel.rowSpan() > 1)
					x.writeAttribute("table:number-rows-spanned", QString::number(cel.rowSpan()));
				std::vector<Nivel> guardada;
				guardada.swap(pilha_);
				fluxo(x, cel.begin(), cel.end());
				fecharListas(x);
				pilha_.swap(guardada);
				x.writeEndElement();
			}
			x.writeEndElement();
		}
		x.writeEndElement();
	}

	QString estiloComum(const QString &id, const QString &nome, const QString &exibido) const
	{
		const Folha f = folhaDe(d_);
		const Estilo e = estilo(id, f.esquema);
		QTextBlockFormat b = paragrafoDoEstilo(e);
		QTextCharFormat c = letraDoEstilo(e, d_->defaultFont());
		QString r = "<style:style style:name=\"" + nome + "\" style:display-name=\"" + exibido + "\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"" +
		            (e.nivel > 0 ? " style:default-outline-level=\"" + QString::number(e.nivel) + "\"" : QString()) +
		            " style:next-style-name=\"Text_20_body\" style:class=\"" + (e.nivel > 0 ? "text" : "text") + "\">";
		b.clearProperty(P_ESTILO);
		r += "<style:paragraph-properties" + propsParagrafo(b, false) + "/>";
		r += "<style:text-properties" + propsTexto(c) + "/>";
		return r + "</style:style>";
	}

	QString estilos(const Folha &f) const
	{
		const QFont base = d_->defaultFont();
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		            "<office:document-styles " + NS + " office:version=\"1.3\"><office:styles>"
		            "<style:default-style style:family=\"paragraph\"><style:paragraph-properties fo:hyphenation-ladder-count=\"no-limit\" style:writing-mode=\"page\"/>"
		            "<style:text-properties fo:font-family=\"'" + esc(base.family()) + "'\" style:font-name=\"" + esc(base.family()) +
		            "\" fo:font-size=\"" + pt(base.pointSizeF()) + "\" fo:language=\"pt\" fo:country=\"BR\" fo:color=\"#1c1b19\"/></style:default-style>"
		            "<style:style style:name=\"Standard\" style:family=\"paragraph\" style:class=\"text\"/>";
		s += estiloComum("corpo", "Text_20_body", "Text body");
		s += estiloComum("titulo", "Title", "Title");
		s += estiloComum("subtitulo", "Subtitle", "Subtitle");
		s += estiloComum("cab1", "Heading_20_1", "Heading 1");
		s += estiloComum("cab2", "Heading_20_2", "Heading 2");
		s += estiloComum("cab3", "Heading_20_3", "Heading 3");
		s += estiloComum("citacao", "Quotations", "Quotations");
		s += estiloComum("legenda", "Caption", "Caption");
		s += "<style:style style:name=\"Footer\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:class=\"extra\">"
		     "<style:paragraph-properties fo:text-align=\"end\"/><style:text-properties fo:font-size=\"" + pt(std::max(8.0, base.pointSizeF() - 2)) + "\"/></style:style>";
		s += "</office:styles><office:automatic-styles><style:page-layout style:name=\"pm1\"><style:page-layout-properties "
		     "fo:page-width=\"" + cm(px(f.l)) + "\" fo:page-height=\"" + cm(px(f.a)) + "\" style:print-orientation=\"" +
		     (f.l > f.a ? "landscape" : "portrait") + "\" fo:margin-top=\"" + cm(px(f.sup)) + "\" fo:margin-bottom=\"" +
		     cm(px(f.numeros ? std::max(5.0, f.inf - 8) : f.inf)) + "\" fo:margin-left=\"" + cm(px(f.esq)) + "\" fo:margin-right=\"" + cm(px(f.dir)) + "\"/>";
		if (f.numeros)
			s += "<style:footer-style><style:header-footer-properties fo:min-height=\"0cm\" fo:margin-top=\"0.3cm\"/></style:footer-style>";
		s += "</style:page-layout></office:automatic-styles><office:master-styles>"
		     "<style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\">";
		if (f.numeros)
			s += "<style:footer><text:p text:style-name=\"Footer\"><text:page-number text:select-page=\"current\">1</text:page-number></text:p></style:footer>";
		s += "</style:master-page></office:master-styles></office:document-styles>";
		return s;
	}

	QString meta(const Folha &f) const
	{
		return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		       "<office:document-meta " + NS + " office:version=\"1.3\"><office:meta>"
		       "<meta:generator>LumeBSD Páginas</meta:generator>"
		       "<meta:creation-date>" + QDateTime::currentDateTime().toString(Qt::ISODate) + "</meta:creation-date>"
		       "<meta:user-defined meta:name=\"lume-esquema\" meta:value-type=\"string\">" + esc(f.esquema) + "</meta:user-defined>"
		       "</office:meta></office:document-meta>";
	}

	const QTextDocument *d_;
	QString autos_;
	QMap<QString, QString> ps_, ts_;
	QSet<QString> listasFeitas_;
	std::vector<std::pair<QString, QByteArray>> imagens_;
	int tabelas_ = 0;
	bool celula_ = false;
};

// --- ODT: ler --------------------------------------------------------------

// Os estilos do ODF: o de parágrafo e o de texto, com o pai.
struct EstiloLido {
	QString familia, pai, exibido, lista;
	int nivel = 0;
	QTextBlockFormat b;
	QTextCharFormat c;
	bool temB = false, temC = false;
};

static Qt::Alignment alinhamentoDe(const QString &v)
{
	if (v == "center") return Qt::AlignHCenter;
	if (v == "justify" || v == "both" || v == "distribute") return Qt::AlignJustify;
	if (v == "end" || v == "right") return Qt::AlignRight | Qt::AlignAbsolute;
	return Qt::AlignLeft | Qt::AlignAbsolute;
}

static QColor corDe(const QString &v)
{
	if (v.isEmpty() || v == "transparent" || v == "auto")
		return QColor();
	return QColor(v.startsWith('#') ? v : "#" + v);
}

static void lerPropsTextoOdf(const No &n, QTextCharFormat &c)
{
	QString fam = n.a("fo:font-family");
	if (fam.isEmpty())
		fam = n.a("style:font-name");
	fam = fam.remove('\'').remove('"').trimmed();
	if (!fam.isEmpty())
		c.setFontFamilies(QStringList{fam});
	const QString tam = n.a("fo:font-size");
	if (tam.endsWith("pt"))
		c.setFontPointSize(tam.chopped(2).toDouble());
	else if (!tam.isEmpty() && !tam.endsWith('%'))
		c.setFontPointSize(medida(tam) * 72.0 / 96.0);
	const QString peso = n.a("fo:font-weight");
	if (!peso.isEmpty())
		c.setFontWeight(peso == "bold" || peso.toInt() >= 600 ? QFont::Bold : QFont::Normal);
	const QString est = n.a("fo:font-style");
	if (!est.isEmpty())
		c.setFontItalic(est == "italic" || est == "oblique");
	const QString sub = n.a("style:text-underline-style");
	if (!sub.isEmpty())
		c.setFontUnderline(sub != "none");
	const QString ris = n.a("style:text-line-through-style");
	if (!ris.isEmpty())
		c.setFontStrikeOut(ris != "none");
	const QColor cor = corDe(n.a("fo:color"));
	if (cor.isValid())
		c.setForeground(cor);
	const QColor fundo = corDe(n.a("fo:background-color"));
	if (fundo.isValid())
		c.setBackground(fundo);
	// "super 58%", "sub 58%" ou "33% 80%" (o deslocamento e o tamanho).
	const QString pos = n.a("style:text-position").trimmed().section(' ', 0, 0);
	const double desl = QString(pos).remove('%').toDouble();
	if (pos == "super" || desl > 0)
		c.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
	else if (pos == "sub" || desl < 0)
		c.setVerticalAlignment(QTextCharFormat::AlignSubScript);
	if (n.a("fo:text-transform") == "uppercase")
		c.setFontCapitalization(QFont::AllUppercase);
}

static void lerPropsParagrafoOdf(const No &n, QTextBlockFormat &b)
{
	if (n.atr.contains("fo:text-align"))
		b.setAlignment(alinhamentoDe(n.a("fo:text-align")));
	if (n.atr.contains("fo:margin-top"))
		b.setTopMargin(medida(n.a("fo:margin-top")));
	if (n.atr.contains("fo:margin-bottom"))
		b.setBottomMargin(medida(n.a("fo:margin-bottom")));
	if (n.atr.contains("fo:margin-left"))
		b.setLeftMargin(medida(n.a("fo:margin-left")));
	if (n.atr.contains("fo:text-indent"))
		b.setTextIndent(medida(n.a("fo:text-indent")));
	const QString lh = n.a("fo:line-height");
	if (lh.endsWith('%'))
		b.setLineHeight(lh.chopped(1).toDouble(), QTextBlockFormat::ProportionalHeight);
	else if (!lh.isEmpty() && lh != "normal")
		b.setLineHeight(medida(lh), QTextBlockFormat::FixedHeight);
	else if (lh == "normal")
		b.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
	if (n.atr.contains("style:line-height-at-least"))
		b.setLineHeight(medida(n.a("style:line-height-at-least")), QTextBlockFormat::MinimumHeight);
	if (n.a("fo:break-before") == "page")
		b.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
}

class LeitorOdt
{
public:
	QString pasta; // a do arquivo: imagens ligadas ("../foto.png") ficam fora do pacote

	bool ler(const QByteArray &dados, QTextDocument *d, QString *erro)
	{
		d_ = d;
		if (!z_.abrir(dados) || !z_.tem("content.xml")) {
			*erro = "não é um documento OpenDocument (.odt)";
			return false;
		}
		No conteudo, estilos;
		if (!lerXml(z_.ler("content.xml"), conteudo)) {
			*erro = "o content.xml está danificado";
			return false;
		}
		lerXml(z_.ler("styles.xml"), estilos);
		Folha f;
		f.esquema = "padrao";
		// O esquema que o Páginas gravou.
		No meta;
		if (lerXml(z_.ler("meta.xml"), meta))
			if (const No *m = meta.achar("office:meta"))
				for (const No &u : m->filhos)
					if (u.nome == "meta:user-defined" && u.a("meta:name") == "lume-esquema")
						f.esquema = u.textoTodo().trimmed();
		for (const No *raiz : {&estilos, &conteudo}) {
			for (const char *sec : {"office:styles", "office:automatic-styles"}) {
				if (const No *s = raiz->achar(sec))
					coletarEstilos(*s);
			}
		}
		// A fonte e o tamanho do corpo: o estilo padrão de parágrafo.
		QFont base("Liberation Serif");
		base.setPointSizeF(12);
		if (padrao_.hasProperty(QTextFormat::FontFamilies))
			base.setFamily(familiaDe(padrao_));
		if (padrao_.hasProperty(QTextFormat::FontPointSize))
			base.setPointSizeF(padrao_.fontPointSize());
		// A página: o leiaute da página-mestra "Standard" (ou a primeira).
		if (const No *ms = estilos.achar("office:master-styles")) {
			const No *mestra = nullptr;
			for (const No &m : ms->filhos)
				if (m.nome == "style:master-page" && (!mestra || m.a("style:name") == "Standard"))
					mestra = &m;
			if (mestra) {
				const QString pl = mestra->a("style:page-layout-name");
				for (const No *raiz : {&estilos, &conteudo})
					if (const No *as = raiz->achar("office:automatic-styles"))
						for (const No &l : as->filhos)
							if (l.nome == "style:page-layout" && l.a("style:name") == pl)
								if (const No *p = l.filho("style:page-layout-properties")) {
									f.l = mm(medida(p->a("fo:page-width"), px(210)));
									f.a = mm(medida(p->a("fo:page-height"), px(297)));
									f.sup = mm(medida(p->a("fo:margin-top"), px(20)));
									f.inf = mm(medida(p->a("fo:margin-bottom"), px(20)));
									f.esq = mm(medida(p->a("fo:margin-left"), px(20)));
									f.dir = mm(medida(p->a("fo:margin-right"), px(20)));
								}
				if (const No *rod = mestra->filho("style:footer"))
					if (rod->achar("text:page-number")) {
						f.numeros = true;
						f.inf += 8;
					}
			}
		}
		documentoNovo(d, base.family(), base.pointSizeF(), f);
		c_ = QTextCursor(d);
		reaproveitar_ = true;
		const No *corpo = conteudo.achar("office:text");
		if (!corpo) {
			*erro = "o documento não tem texto (é uma planilha ou uma apresentação?)";
			return false;
		}
		fluxo(*corpo);
		d->setUndoRedoEnabled(true);
		d->setModified(false);
		return true;
	}

private:
	void coletarEstilos(const No &sec)
	{
		for (const No &s : sec.filhos) {
			if (s.nome == "style:default-style" && s.a("style:family") == "paragraph") {
				if (const No *t = s.filho("style:text-properties"))
					lerPropsTextoOdf(*t, padrao_);
				continue;
			}
			if (s.nome == "text:list-style") {
				std::map<int, bool> niveis;
				for (const No &n : s.filhos) {
					const int lv = n.a("text:level", "1").toInt();
					if (n.nome == "text:list-level-style-number")
						niveis[lv] = !n.a("style:num-format").isEmpty();
					else if (n.nome == "text:list-level-style-bullet" || n.nome == "text:list-level-style-image")
						niveis[lv] = false;
				}
				listas_[s.a("style:name")] = niveis;
				continue;
			}
			if (s.nome != "style:style")
				continue;
			EstiloLido e;
			e.familia = s.a("style:family");
			e.pai = s.a("style:parent-style-name");
			e.exibido = s.a("style:display-name", s.a("style:name"));
			e.lista = s.a("style:list-style-name");
			e.nivel = s.a("style:default-outline-level").toInt();
			if (const No *p = s.filho("style:paragraph-properties")) {
				lerPropsParagrafoOdf(*p, e.b);
				e.temB = true;
			}
			if (const No *t = s.filho("style:text-properties")) {
				lerPropsTextoOdf(*t, e.c);
				e.temC = true;
			}
			if (const No *t = s.filho("style:table-column-properties"))
				colunas_[s.a("style:name")] = t->a("style:rel-column-width", t->a("style:column-width"));
			estilos_[s.a("style:name")] = e;
		}
	}

	// O estilo com os pais (até 20 gerações), e o nosso estilo pelo nome.
	void efetivo(const QString &nome, QTextBlockFormat &b, QTextCharFormat &c, QString &id, int prof = 0)
	{
		auto it = estilos_.find(nome);
		if (it == estilos_.end() || prof > 20)
			return;
		const EstiloLido &e = it->second;
		if (!e.pai.isEmpty())
			efetivo(e.pai, b, c, id, prof + 1);
		b.merge(e.b);
		c.merge(e.c);
		const QString x = idDeNome(e.exibido);
		const QString y = idDeNome(nome);
		if (!x.isEmpty() && x != "corpo")
			id = x;
		else if (!y.isEmpty() && y != "corpo")
			id = y;
		else if (e.nivel > 0 && id.isEmpty())
			id = e.nivel == 1 ? "cab1" : e.nivel == 2 ? "cab2" : "cab3";
	}

	void novoBloco(const QTextBlockFormat &b, const QTextCharFormat &c)
	{
		if (reaproveitar_) {
			c_.setBlockFormat(b);
			c_.setBlockCharFormat(c);
			reaproveitar_ = false;
		} else {
			c_.insertBlock(b, c);
		}
	}

	void paragrafo(const No &p, int nivelLista, bool numerada)
	{
		QTextBlockFormat b;
		QTextCharFormat c;
		QString id;
		efetivo(p.a("text:style-name"), b, c, id);
		if (p.nome == "text:h") {
			const int nv = p.a("text:outline-level", "1").toInt();
			if (id.isEmpty() || id == "corpo")
				id = nv <= 1 ? "cab1" : nv == 2 ? "cab2" : "cab3";
		}
		if (id.isEmpty())
			id = "corpo";
		b.setProperty(P_ESTILO, id);
		b.setHeadingLevel(id == "cab1" ? 1 : id == "cab2" ? 2 : id == "cab3" ? 3 : 0);
		if (nivelLista > 0) {
			b.setTextIndent(0);
			b.clearProperty(QTextFormat::BlockLeftMargin);
		}
		novoBloco(b, c);
		inicio_ = true;
		inline_(p, c);
		if (nivelLista > 0) {
			QTextList *&l = listaAtual_[nivelLista];
			if (l && listaNumerada(l->format()) == numerada) {
				l->add(c_.block());
			} else {
				QTextListFormat lf;
				lf.setStyle(numerada ? QTextListFormat::ListDecimal : (nivelLista == 2 ? QTextListFormat::ListCircle : nivelLista >= 3 ? QTextListFormat::ListSquare : QTextListFormat::ListDisc));
				lf.setIndent(nivelLista);
				l = c_.createList(lf);
			}
		}
	}

	void textoCorrido(QString t, const QTextCharFormat &c)
	{
		// O ODF junta os espaços (como o HTML).
		t.replace('\n', ' ').replace('\r', ' ').replace('\t', ' ');
		QString r;
		bool ini = inicio_;
		for (QChar ch : t) {
			if (ch == ' ' && (ini || (!r.isEmpty() && r.back() == ' ') || (r.isEmpty() && espaco_)))
				continue;
			r += ch;
			ini = false;
		}
		if (r.isEmpty())
			return;
		c_.insertText(r, c);
		inicio_ = false;
		espaco_ = r.back() == ' ';
	}

	void inline_(const No &n, const QTextCharFormat &c)
	{
		for (const No &f : n.filhos) {
			if (f.nome.isEmpty()) {
				textoCorrido(f.texto, c);
			} else if (f.nome == "text:span") {
				QTextCharFormat c2 = c;
				QTextBlockFormat lixo;
				QString id;
				QTextCharFormat cs;
				efetivo(f.a("text:style-name"), lixo, cs, id);
				c2.merge(cs);
				inline_(f, c2);
			} else if (f.nome == "text:s") {
				c_.insertText(QString(std::max(1, f.a("text:c", "1").toInt()), ' '), c);
				inicio_ = false;
				espaco_ = true;
			} else if (f.nome == "text:tab") {
				c_.insertText("\t", c);
				inicio_ = false;
				espaco_ = false;
			} else if (f.nome == "text:line-break") {
				c_.insertText(QString(QChar::LineSeparator), c);
				inicio_ = true;
			} else if (f.nome == "draw:frame") {
				imagem(f);
			} else if (f.nome == "text:note" || f.nome == "office:annotation" || f.nome == "text:bookmark" ||
			           f.nome == "text:soft-page-break" || f.nome == "text:bookmark-start" || f.nome == "text:bookmark-end") {
				// notas de rodapé e comentários ficam de fora
			} else {
				inline_(f, c); // text:a, text:date, text:sequence...
			}
		}
	}

	void imagem(const No &frame)
	{
		const No *im = frame.filho("draw:image");
		if (!im)
			return;
		const QString href = im->a("xlink:href");
		QImage img;
		img.loadFromData(z_.ler(href));
		if (img.isNull() && !pasta.isEmpty()) {
			// Uma imagem ligada, fora do pacote (relativa ao .odt).
			QString caminho = QUrl(href).isLocalFile() ? QUrl(href).toLocalFile() : QUrl::fromPercentEncoding(href.toUtf8());
			if (caminho.startsWith("../"))
				caminho = QDir(pasta).filePath(caminho.mid(3));
			else if (QDir::isRelativePath(caminho))
				caminho = QDir(pasta).filePath(caminho);
			img.load(caminho);
		}
		if (img.isNull())
			return;
		const QString nome = "lume://imagem/" + QString::number(++imagens_);
		d_->addResource(QTextDocument::ImageResource, QUrl(nome), img);
		QTextImageFormat f;
		f.setName(nome);
		const double w = medida(frame.a("svg:width")), h = medida(frame.a("svg:height"));
		if (w > 0) f.setWidth(w);
		if (h > 0) f.setHeight(h);
		c_.insertImage(f);
		inicio_ = false;
	}

	void lista(const No &l, int nivel, const QString &estiloPai)
	{
		const QString est = l.a("text:style-name", estiloPai);
		bool numerada = false;
		auto it = listas_.find(est);
		if (it != listas_.end()) {
			auto n = it->second.find(nivel);
			if (n != it->second.end())
				numerada = n->second;
		}
		// Uma lista nova de primeiro nível começa a numeração de novo.
		if (nivel == 1)
			listaAtual_.clear();
		listaAtual_.erase(nivel + 1);
		for (const No &item : l.filhos) {
			if (item.nome != "text:list-item" && item.nome != "text:list-header")
				continue;
			for (const No &f : item.filhos) {
				if (f.nome == "text:p" || f.nome == "text:h")
					paragrafo(f, nivel, numerada);
				else if (f.nome == "text:list")
					lista(f, nivel + 1, est);
				else if (f.nome == "table:table")
					tabela(f);
			}
		}
		if (nivel == 1)
			listaAtual_.clear();
	}

	void tabela(const No &t)
	{
		// As linhas (dentro de grupos e cabeçalhos também) e as colunas.
		std::vector<const No *> linhas;
		std::function<void(const No &)> juntar = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (f.nome == "table:table-row") {
					const int rep = std::clamp(f.a("table:number-rows-repeated", "1").toInt(), 1, 50);
					for (int i = 0; i < rep; i++)
						linhas.push_back(&f);
				} else if (f.nome == "table:table-header-rows" || f.nome == "table:table-rows" || f.nome == "table:table-row-group") {
					juntar(f);
				}
			}
		};
		juntar(t);
		QVector<QTextLength> larguras;
		std::function<void(const No &)> cols = [&](const No &n) {
			for (const No &f : n.filhos) {
				if (f.nome == "table:table-column") {
					const int rep = std::clamp(f.a("table:number-columns-repeated", "1").toInt(), 1, 64);
					const QString w = colunas_.count(f.a("table:style-name")) ? colunas_[f.a("table:style-name")] : QString();
					for (int i = 0; i < rep; i++)
						larguras << QTextLength(QTextLength::FixedLength, w.endsWith('*') ? w.chopped(1).toDouble() : medida(w));
				} else if (f.nome == "table:table-columns" || f.nome == "table:table-header-columns" || f.nome == "table:table-column-group") {
					cols(f);
				}
			}
		};
		cols(t);
		int ncol = 0;
		for (const No *r : linhas) {
			int n = 0;
			for (const No &c : r->filhos)
				if (c.nome == "table:table-cell" || c.nome == "table:covered-table-cell")
					n += std::clamp(c.a("table:number-columns-repeated", "1").toInt(), 1, 64);
			ncol = std::max(ncol, n);
		}
		if (linhas.empty() || ncol == 0)
			return;
		ncol = std::min(ncol, 64);
		QTextTableFormat tf = Montador::formatoTabela();
		double soma = 0;
		for (int i = 0; i < std::min<int>(ncol, larguras.size()); i++)
			soma += larguras[i].rawValue();
		if (larguras.size() >= ncol && soma > 0) {
			QVector<QTextLength> pct;
			for (int i = 0; i < ncol; i++)
				pct << QTextLength(QTextLength::PercentageLength, larguras[i].rawValue() * 100.0 / soma);
			tf.setColumnWidthConstraints(pct);
		}
		QTextTable *tab = c_.insertTable(int(linhas.size()), ncol, tf);
		struct Mescla { int r, c, rs, cs; };
		std::vector<Mescla> mesclas;
		auto guardaLista = listaAtual_;
		for (size_t r = 0; r < linhas.size(); r++) {
			int col = 0;
			for (const No &cel : linhas[r]->filhos) {
				if (cel.nome != "table:table-cell" && cel.nome != "table:covered-table-cell")
					continue;
				const int rep = std::clamp(cel.a("table:number-columns-repeated", "1").toInt(), 1, 64);
				for (int k = 0; k < rep && col < ncol; k++, col++) {
					if (cel.nome == "table:covered-table-cell")
						continue;
					const int rs = cel.a("table:number-rows-spanned", "1").toInt();
					const int cs = cel.a("table:number-columns-spanned", "1").toInt();
					if (rs > 1 || cs > 1)
						mesclas.push_back({int(r), col, rs, cs});
					c_ = tab->cellAt(int(r), col).firstCursorPosition();
					reaproveitar_ = true;
					listaAtual_.clear();
					fluxo(cel);
				}
			}
		}
		for (const Mescla &m : mesclas)
			tab->mergeCells(m.r, m.c, std::min(m.rs, tab->rows() - m.r), std::min(m.cs, tab->columns() - m.c));
		listaAtual_ = guardaLista;
		c_ = QTextCursor(tab->lastCursorPosition());
		c_.movePosition(QTextCursor::NextBlock);
		reaproveitar_ = true;
	}

	void fluxo(const No &n)
	{
		for (const No &f : n.filhos) {
			if (f.nome == "text:p" || f.nome == "text:h")
				paragrafo(f, 0, false);
			else if (f.nome == "text:list")
				lista(f, 1, QString());
			else if (f.nome == "table:table")
				tabela(f);
			else if (f.nome == "text:section" || f.nome == "text:index-body" || f.nome == "text:table-of-content" ||
			         f.nome == "text:alphabetical-index" || f.nome == "text:illustration-index" || f.nome == "text:bibliography")
				fluxo(f);
		}
	}

	QTextDocument *d_ = nullptr;
	ZipLeitor z_;
	QTextCursor c_;
	bool reaproveitar_ = true, inicio_ = true, espaco_ = false;
	std::map<QString, EstiloLido> estilos_;
	std::map<QString, std::map<int, bool>> listas_;
	std::map<QString, QString> colunas_;
	std::map<int, QTextList *> listaAtual_;
	QTextCharFormat padrao_;
	int imagens_ = 0;
};

// --- DOCX: gravar ----------------------------------------------------------

class EscritorDocx
{
public:
	explicit EscritorDocx(const QTextDocument *d) : d_(d) {}

	QByteArray gravar()
	{
		const Folha f = folhaDe(d_);
		QString corpo;
		fluxo(corpo, d_->rootFrame()->begin(), d_->rootFrame()->end());
		const int alturaRodape = 8;
		corpo += "<w:sectPr>";
		if (f.numeros)
			corpo += "<w:footerReference w:type=\"default\" r:id=\"rIdRodape\"/>";
		corpo += "<w:pgSz w:w=\"" + QString::number(twips(px(f.l))) + "\" w:h=\"" + QString::number(twips(px(f.a))) + "\"" +
		         (f.l > f.a ? " w:orient=\"landscape\"" : "") + "/>"
		         "<w:pgMar w:top=\"" + QString::number(twips(px(f.sup))) + "\" w:right=\"" + QString::number(twips(px(f.dir))) +
		         "\" w:bottom=\"" + QString::number(twips(px(f.inf))) + "\" w:left=\"" + QString::number(twips(px(f.esq))) +
		         "\" w:header=\"709\" w:footer=\"" + QString::number(twips(px(std::max(4.0, f.inf - alturaRodape)))) + "\" w:gutter=\"0\"/></w:sectPr>";
		const QString doc = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		                    "<w:document " + NS + "><w:body>" + corpo + "</w:body></w:document>";
		ZipEscritor z;
		QString tipos = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		                "<Default Extension=\"png\" ContentType=\"image/png\"/>"
		                "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
		                "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
		                "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>"
		                "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
		                "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
		                "<Override PartName=\"/docProps/custom.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.custom-properties+xml\"/>";
		if (f.numeros)
			tipos += "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>";
		tipos += "</Types>";
		z.add("[Content_Types].xml", tipos.toUtf8());
		z.add("_rels/.rels",
		      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
		      "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
		      "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
		      "<Relationship Id=\"rId4\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties\" Target=\"docProps/custom.xml\"/>"
		      "</Relationships>");
		z.add("docProps/core.xml",
		      ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		       "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
		       "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
		       "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dcterms:created xsi:type=\"dcterms:W3CDTF\">" +
		       QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + "</dcterms:created></cp:coreProperties>").toUtf8());
		z.add("docProps/app.xml",
		      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		      "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
		      "<Application>LumeBSD Páginas</Application></Properties>");
		z.add("docProps/custom.xml",
		      ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		       "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/custom-properties\" "
		       "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">"
		       "<property fmtid=\"{D5CDD505-2E9C-101B-9397-08002B2CF9AE}\" pid=\"2\" name=\"lume-esquema\"><vt:lpwstr>" +
		       esc(f.esquema) + "</vt:lpwstr></property></Properties>").toUtf8());
		z.add("word/document.xml", doc.toUtf8());
		z.add("word/styles.xml", estilos().toUtf8());
		z.add("word/numbering.xml", numeracao().toUtf8());
		QString rels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		               "<Relationship Id=\"rIdEstilos\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
		               "<Relationship Id=\"rIdNumeros\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>";
		if (f.numeros) {
			rels += "<Relationship Id=\"rIdRodape\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>";
			z.add("word/footer1.xml",
			      ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
			       "<w:ftr " + NS + "><w:p><w:pPr><w:jc w:val=\"right\"/></w:pPr>"
			       "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> PAGE </w:instrText></w:r>"
			       "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>1</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
			       "</w:p></w:ftr>").toUtf8());
		}
		for (size_t i = 0; i < imagens_.size(); i++) {
			const QString n = QString("image%1.png").arg(i + 1);
			z.add("word/media/" + n, imagens_[i], false);
			rels += "<Relationship Id=\"rIdImg" + QString::number(i + 1) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/" + n + "\"/>";
		}
		rels += "</Relationships>";
		z.add("word/_rels/document.xml.rels", rels.toUtf8());
		return z.fechar();
	}

private:
	inline static const QString NS =
		"xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
		"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
		"xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
		"xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
		"xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"";

	static QString val(const QString &tag, const QString &v) { return "<" + tag + " w:val=\"" + esc(v) + "\"/>"; }

	static QString propsLetra(const QTextCharFormat &f)
	{
		QString r;
		const QString fam = familiaDe(f);
		if (!fam.isEmpty())
			r += "<w:rFonts w:ascii=\"" + esc(fam) + "\" w:hAnsi=\"" + esc(fam) + "\" w:cs=\"" + esc(fam) + "\"/>";
		if (f.hasProperty(QTextFormat::FontWeight))
			r += f.fontWeight() >= QFont::DemiBold ? "<w:b/>" : "<w:b w:val=\"0\"/>";
		if (f.hasProperty(QTextFormat::FontItalic))
			r += f.fontItalic() ? "<w:i/>" : "<w:i w:val=\"0\"/>";
		if (f.hasProperty(QTextFormat::FontCapitalization) && f.fontCapitalization() == QFont::AllUppercase)
			r += "<w:caps/>";
		if (f.hasProperty(QTextFormat::FontStrikeOut) && f.fontStrikeOut())
			r += "<w:strike/>";
		if (f.hasProperty(QTextFormat::ForegroundBrush) && f.foreground().style() != Qt::NoBrush)
			r += val("w:color", corHex(f.foreground().color()).mid(1).toUpper());
		if (f.hasProperty(QTextFormat::FontPointSize)) {
			const QString hp = QString::number(qRound(f.fontPointSize() * 2));
			r += val("w:sz", hp) + val("w:szCs", hp);
		}
		if ((f.hasProperty(QTextFormat::FontUnderline) || f.hasProperty(QTextFormat::TextUnderlineStyle)) && f.fontUnderline())
			r += val("w:u", "single");
		if (f.hasProperty(QTextFormat::BackgroundBrush) && f.background().style() != Qt::NoBrush)
			r += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + corHex(f.background().color()).mid(1).toUpper() + "\"/>";
		if (f.hasProperty(QTextFormat::TextVerticalAlignment)) {
			if (f.verticalAlignment() == QTextCharFormat::AlignSuperScript)
				r += val("w:vertAlign", "superscript");
			else if (f.verticalAlignment() == QTextCharFormat::AlignSubScript)
				r += val("w:vertAlign", "subscript");
		}
		return r;
	}

	static QString alinhamento(Qt::Alignment a)
	{
		if (a & Qt::AlignHCenter) return "center";
		if (a & Qt::AlignJustify) return "both";
		if (a & Qt::AlignRight) return "right";
		return "left";
	}

	static QString propsParagrafo(const QTextBlockFormat &b, const QString &numero)
	{
		QString r;
		const QString id = b.stringProperty(P_ESTILO);
		if (!id.isEmpty() && id != "corpo")
			r += val("w:pStyle", nomeDocx(id));
		if (b.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
			r += "<w:pageBreakBefore/>";
		r += numero;
		QString esp;
		if (b.hasProperty(QTextFormat::BlockTopMargin))
			esp += " w:before=\"" + QString::number(twips(b.topMargin())) + "\"";
		if (b.hasProperty(QTextFormat::BlockBottomMargin))
			esp += " w:after=\"" + QString::number(twips(b.bottomMargin())) + "\"";
		if (b.hasProperty(QTextFormat::LineHeight)) {
			switch (b.lineHeightType()) {
			case QTextBlockFormat::ProportionalHeight:
				esp += " w:line=\"" + QString::number(qRound(b.lineHeight() * 2.4)) + "\" w:lineRule=\"auto\"";
				break;
			case QTextBlockFormat::FixedHeight:
				esp += " w:line=\"" + QString::number(twips(b.lineHeight())) + "\" w:lineRule=\"exact\"";
				break;
			case QTextBlockFormat::MinimumHeight:
				esp += " w:line=\"" + QString::number(twips(b.lineHeight())) + "\" w:lineRule=\"atLeast\"";
				break;
			default:
				break;
			}
		}
		if (!esp.isEmpty())
			r += "<w:spacing" + esp + "/>";
		if (numero.isEmpty() && (b.hasProperty(QTextFormat::BlockLeftMargin) || b.hasProperty(QTextFormat::TextIndent))) {
			QString ind;
			if (b.hasProperty(QTextFormat::BlockLeftMargin))
				ind += " w:left=\"" + QString::number(twips(b.leftMargin())) + "\"";
			const double t = b.textIndent();
			if (t > 0)
				ind += " w:firstLine=\"" + QString::number(twips(t)) + "\"";
			else if (t < 0)
				ind += " w:hanging=\"" + QString::number(twips(-t)) + "\"";
			r += "<w:ind" + ind + "/>";
		}
		if (b.hasProperty(QTextFormat::BlockAlignment))
			r += val("w:jc", alinhamento(b.alignment()));
		return r;
	}

	static QString texto(const QString &s)
	{
		QString r, acum;
		auto solta = [&] {
			if (!acum.isEmpty())
				r += "<w:t xml:space=\"preserve\">" + esc(acum) + "</w:t>";
			acum.clear();
		};
		for (QChar c : s) {
			if (c == '\t') {
				solta();
				r += "<w:tab/>";
			} else if (c == QChar::LineSeparator || c == '\n') {
				solta();
				r += "<w:br/>";
			} else if (c != QChar::ObjectReplacementCharacter) {
				acum += c;
			}
		}
		solta();
		return r;
	}

	QString imagem(const QTextImageFormat &f)
	{
		QSizeF tam;
		const QImage img = imagemDe(d_, f, &tam);
		if (img.isNull())
			return QString();
		imagens_.push_back(png(img));
		const int n = int(imagens_.size());
		const QString cx = QString::number(emu(tam.width())), cy = QString::number(emu(tam.height()));
		const QString id = QString::number(n);
		return "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"" + cx + "\" cy=\"" + cy + "\"/>"
		       "<wp:docPr id=\"" + id + "\" name=\"Imagem " + id + "\"/><wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
		       "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic>"
		       "<pic:nvPicPr><pic:cNvPr id=\"" + id + "\" name=\"image" + id + ".png\"/><pic:cNvPicPr/></pic:nvPicPr>"
		       "<pic:blipFill><a:blip r:embed=\"rIdImg" + id + "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
		       "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + cx + "\" cy=\"" + cy + "\"/></a:xfrm>"
		       "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic>"
		       "</wp:inline></w:drawing></w:r>";
	}

	void paragrafo(QString &o, const QTextBlock &b)
	{
		QString numero;
		if (QTextList *l = b.textList()) {
			int &id = listas_[l];
			if (id == 0) {
				id = int(listas_.size());
				numerada_[id] = listaNumerada(l->format());
			}
			numero = "<w:numPr>" + val("w:ilvl", QString::number(std::clamp(l->format().indent() - 1, 0, 8))) +
			         val("w:numId", QString::number(id)) + "</w:numPr>";
		}
		o += "<w:p>";
		const QString pp = propsParagrafo(b.blockFormat(), numero);
		if (!pp.isEmpty())
			o += "<w:pPr>" + pp + "</w:pPr>";
		for (const Pedaco &p : pedacos(b)) {
			if (p.imagem) {
				o += imagem(p.f.toImageFormat());
				continue;
			}
			const QString rp = propsLetra(p.f);
			o += "<w:r>" + (rp.isEmpty() ? QString() : "<w:rPr>" + rp + "</w:rPr>") + texto(p.texto) + "</w:r>";
		}
		o += "</w:p>";
	}

	void fluxo(QString &o, QTextFrame::iterator it, QTextFrame::iterator fim)
	{
		for (; it != fim && !it.atEnd(); ++it) {
			if (QTextFrame *f = it.currentFrame()) {
				if (auto *t = qobject_cast<QTextTable *>(f))
					tabela(o, t);
				else
					fluxo(o, f->begin(), f->end());
				continue;
			}
			paragrafo(o, it.currentBlock());
		}
	}

	void tabela(QString &o, QTextTable *t)
	{
		const Folha f = folhaDe(d_);
		const int total = twips(px(f.l - f.esq - f.dir));
		const QVector<QTextLength> larg = t->format().columnWidthConstraints();
		QVector<int> cols;
		for (int c = 0; c < t->columns(); c++) {
			double p = 100.0 / t->columns();
			if (c < larg.size() && larg[c].type() == QTextLength::PercentageLength)
				p = larg[c].rawValue();
			cols << qRound(total * p / 100.0);
		}
		o += "<w:tbl><w:tblPr><w:tblW w:w=\"5000\" w:type=\"pct\"/><w:tblBorders>";
		for (const char *l : {"top", "left", "bottom", "right", "insideH", "insideV"})
			o += QString("<w:%1 w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"1C1B19\"/>").arg(l);
		o += "</w:tblBorders><w:tblCellMar><w:left w:w=\"85\" w:type=\"dxa\"/><w:right w:w=\"85\" w:type=\"dxa\"/></w:tblCellMar>"
		     "<w:tblLook w:val=\"04A0\"/></w:tblPr><w:tblGrid>";
		for (int w : cols)
			o += "<w:gridCol w:w=\"" + QString::number(w) + "\"/>";
		o += "</w:tblGrid>";
		for (int r = 0; r < t->rows(); r++) {
			o += "<w:tr>";
			for (int c = 0; c < t->columns();) {
				const QTextTableCell cel = t->cellAt(r, c);
				const int cs = std::max(1, cel.columnSpan());
				int w = 0;
				for (int k = c; k < c + cs && k < cols.size(); k++)
					w += cols[k];
				QString pr = "<w:tcW w:w=\"" + QString::number(w) + "\" w:type=\"dxa\"/>";
				if (cs > 1)
					pr += val("w:gridSpan", QString::number(cs));
				if (cel.row() != r) {
					// Continuação de uma célula mesclada para baixo.
					o += "<w:tc><w:tcPr>" + pr + "<w:vMerge/></w:tcPr><w:p/></w:tc>";
				} else {
					if (cel.rowSpan() > 1)
						pr += val("w:vMerge", "restart");
					o += "<w:tc><w:tcPr>" + pr + "</w:tcPr>";
					QString dentro;
					fluxo(dentro, cel.begin(), cel.end());
					if (!dentro.contains("<w:p>") && !dentro.contains("<w:p "))
						dentro += "<w:p/>";
					o += dentro + "</w:tc>";
				}
				c += cs;
			}
			o += "</w:tr>";
		}
		o += "</w:tbl>";
	}

	QString estiloComum(const QString &id, const QString &nome) const
	{
		const Estilo e = estilo(id, folhaDe(d_).esquema);
		QTextBlockFormat b = paragrafoDoEstilo(e);
		b.clearProperty(P_ESTILO);
		QTextCharFormat c = letraDoEstilo(e, d_->defaultFont());
		c.clearProperty(QTextFormat::FontFamilies);
		const QString wid = nomeDocx(id);
		QString s = "<w:style w:type=\"paragraph\" w:styleId=\"" + wid + "\"" + (id == "corpo" ? " w:default=\"1\"" : "") + ">" +
		            val("w:name", nome) + (id == "corpo" ? QString() : val("w:basedOn", "Normal") + val("w:next", "Normal")) + "<w:qFormat/>";
		s += "<w:pPr>" + propsParagrafo(b, QString()).replace(QRegularExpression("<w:pStyle[^>]*>"), QString()) +
		     (e.nivel > 0 ? val("w:outlineLvl", QString::number(e.nivel - 1)) : QString()) + "</w:pPr>";
		s += "<w:rPr>" + propsLetra(c) + "</w:rPr></w:style>";
		// (o w:outlineLvl vem depois do w:jc no esquema; o Word aceita)
		return s;
	}

	QString estilos() const
	{
		const QFont base = d_->defaultFont();
		const QString fam = esc(base.family());
		const QString hp = QString::number(qRound(base.pointSizeF() * 2));
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		            "<w:styles " + NS + "><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"" + fam + "\" w:hAnsi=\"" + fam +
		            "\" w:cs=\"" + fam + "\" w:eastAsia=\"" + fam + "\"/><w:color w:val=\"1C1B19\"/><w:sz w:val=\"" + hp + "\"/><w:szCs w:val=\"" + hp +
		            "\"/><w:lang w:val=\"pt-BR\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>";
		s += estiloComum("corpo", "Normal");
		s += estiloComum("titulo", "Title");
		s += estiloComum("subtitulo", "Subtitle");
		s += estiloComum("cab1", "heading 1");
		s += estiloComum("cab2", "heading 2");
		s += estiloComum("cab3", "heading 3");
		s += estiloComum("citacao", "Quote");
		s += estiloComum("legenda", "caption");
		s += "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/><w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/>"
		     "<w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"108\" w:type=\"dxa\"/><w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style>";
		return s + "</w:styles>";
	}

	QString numeracao() const
	{
		QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:numbering " + NS + ">";
		static const QString bolas[] = {"•", "◦", "▪"};
		for (int a = 0; a < 2; a++) {
			s += "<w:abstractNum w:abstractNumId=\"" + QString::number(a) + "\"><w:multiLevelType w:val=\"hybridMultilevel\"/>";
			for (int l = 0; l < 9; l++) {
				s += "<w:lvl w:ilvl=\"" + QString::number(l) + "\">" + val("w:start", "1") +
				     (a == 0 ? val("w:numFmt", "bullet") + val("w:lvlText", bolas[l % 3])
				             : val("w:numFmt", "decimal") + val("w:lvlText", "%" + QString::number(l + 1) + ".")) +
				     val("w:lvlJc", "left") + "<w:pPr><w:ind w:left=\"" + QString::number(360 * (l + 2)) + "\" w:hanging=\"360\"/></w:pPr></w:lvl>";
			}
			s += "</w:abstractNum>";
		}
		for (auto it = numerada_.begin(); it != numerada_.end(); ++it)
			s += "<w:num w:numId=\"" + QString::number(it.key()) + "\"><w:abstractNumId w:val=\"" + (it.value() ? "1" : "0") + "\"/>"
			     "<w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"1\"/></w:lvlOverride></w:num>";
		return s + "</w:numbering>";
	}

	const QTextDocument *d_;
	std::vector<QByteArray> imagens_;
	QHash<QTextList *, int> listas_;
	QMap<int, bool> numerada_;
};

// --- DOCX: ler -------------------------------------------------------------

static bool ligado(const No *n)
{
	if (!n)
		return false;
	const QString v = n->a("w:val");
	return v.isEmpty() || (v != "0" && v != "false" && v != "off" && v != "none");
}

// As fontes do tema do documento (word/theme/theme1.xml): o Word põe a do
// corpo como "minorHAnsi" em vez do nome.
static QString temaMenor_, temaMaior_;

static void lerLetraDocx(const No &rpr, QTextCharFormat &c)
{
	if (const No *f = rpr.filho("w:rFonts")) {
		QString fam = f->a("w:ascii", f->a("w:hAnsi", f->a("w:cs")));
		if (fam.isEmpty()) {
			const QString tema = f->a("w:asciiTheme", f->a("w:hAnsiTheme"));
			fam = tema.startsWith("minor") ? temaMenor_ : tema.startsWith("major") ? temaMaior_ : QString();
		}
		if (!fam.isEmpty())
			c.setFontFamilies(QStringList{fam});
	}
	if (const No *b = rpr.filho("w:b"))
		c.setFontWeight(ligado(b) ? QFont::Bold : QFont::Normal);
	if (const No *i = rpr.filho("w:i"))
		c.setFontItalic(ligado(i));
	if (const No *k = rpr.filho("w:caps"))
		if (ligado(k))
			c.setFontCapitalization(QFont::AllUppercase);
	if (const No *s = rpr.filho("w:strike"))
		c.setFontStrikeOut(ligado(s));
	if (const No *s = rpr.filho("w:dstrike"))
		if (ligado(s))
			c.setFontStrikeOut(true);
	if (const No *u = rpr.filho("w:u"))
		c.setFontUnderline(ligado(u));
	if (const No *k = rpr.filho("w:color")) {
		const QColor cor = corDe(k->a("w:val"));
		if (cor.isValid())
			c.setForeground(cor);
	}
	if (const No *s = rpr.filho("w:sz"))
		c.setFontPointSize(s->a("w:val").toDouble() / 2.0);
	if (const No *h = rpr.filho("w:highlight")) {
		static const QHash<QString, QString> cores = {
			{"yellow", "#ffff00"}, {"green", "#00ff00"}, {"cyan", "#00ffff"}, {"magenta", "#ff00ff"},
			{"blue", "#0000ff"}, {"red", "#ff0000"}, {"darkBlue", "#000080"}, {"darkCyan", "#008080"},
			{"darkGreen", "#008000"}, {"darkMagenta", "#800080"}, {"darkRed", "#800000"},
			{"darkYellow", "#808000"}, {"darkGray", "#808080"}, {"lightGray", "#c0c0c0"}, {"black", "#000000"}};
		if (cores.contains(h->a("w:val")))
			c.setBackground(QColor(cores.value(h->a("w:val"))));
	}
	if (const No *s = rpr.filho("w:shd")) {
		const QColor cor = corDe(s->a("w:fill"));
		if (cor.isValid() && cor != Qt::white)
			c.setBackground(cor);
	}
	// O LibreOffice grava o sobrescrito como deslocamento (meios pontos).
	if (const No *p = rpr.filho("w:position")) {
		const int v = p->a("w:val").toInt();
		if (v > 0)
			c.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
		else if (v < 0)
			c.setVerticalAlignment(QTextCharFormat::AlignSubScript);
	}
	if (const No *v = rpr.filho("w:vertAlign")) {
		if (v->a("w:val") == "superscript")
			c.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
		else if (v->a("w:val") == "subscript")
			c.setVerticalAlignment(QTextCharFormat::AlignSubScript);
	}
}

static void lerParagrafoDocx(const No &ppr, QTextBlockFormat &b)
{
	if (const No *j = ppr.filho("w:jc"))
		b.setAlignment(alinhamentoDe(j->a("w:val")));
	if (const No *s = ppr.filho("w:spacing")) {
		if (s->atr.contains("w:before"))
			b.setTopMargin(pxDeTwips(s->a("w:before").toDouble()));
		if (s->atr.contains("w:after"))
			b.setBottomMargin(pxDeTwips(s->a("w:after").toDouble()));
		if (s->atr.contains("w:line")) {
			const double l = s->a("w:line").toDouble();
			const QString regra = s->a("w:lineRule", "auto");
			if (regra == "exact")
				b.setLineHeight(pxDeTwips(l), QTextBlockFormat::FixedHeight);
			else if (regra == "atLeast")
				b.setLineHeight(pxDeTwips(l), QTextBlockFormat::MinimumHeight);
			else
				b.setLineHeight(l / 2.4, QTextBlockFormat::ProportionalHeight);
		}
	}
	if (const No *i = ppr.filho("w:ind")) {
		const QString esq = i->a("w:left", i->a("w:start"));
		if (!esq.isEmpty())
			b.setLeftMargin(pxDeTwips(esq.toDouble()));
		if (i->atr.contains("w:firstLine"))
			b.setTextIndent(pxDeTwips(i->a("w:firstLine").toDouble()));
		if (i->atr.contains("w:hanging"))
			b.setTextIndent(-pxDeTwips(i->a("w:hanging").toDouble()));
	}
	if (const No *q = ppr.filho("w:pageBreakBefore"))
		if (ligado(q))
			b.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
}

class LeitorDocx
{
public:
	QString pasta; // a do arquivo: imagens ligadas ficam fora do pacote

	bool ler(const QByteArray &dados, QTextDocument *d, QString *erro)
	{
		d_ = d;
		if (!z_.abrir(dados)) {
			*erro = "não é um documento do Word (.docx)";
			return false;
		}
		// O documento principal, pelo _rels/.rels (quase sempre word/document.xml).
		QString principal = "word/document.xml";
		No rr;
		if (lerXml(z_.ler("_rels/.rels"), rr))
			for (const No *r : rr.achar("rel:Relationships") ? rr.achar("rel:Relationships")->todos("rel:Relationship") : std::vector<const No *>())
				if (r->a("Type").endsWith("/officeDocument"))
					principal = r->a("Target").remove(QRegularExpression("^/"));
		const QString interna = principal.contains('/') ? principal.left(principal.lastIndexOf('/') + 1) : QString();
		No doc;
		if (!lerXml(z_.ler(principal), doc)) {
			*erro = "não é um documento do Word (.docx), ou está danificado";
			return false;
		}
		// As relações (imagens).
		No rels;
		const QString arqRels = interna + "_rels/" + principal.mid(interna.size()) + ".rels";
		if (lerXml(z_.ler(arqRels), rels))
			if (const No *r = rels.achar("rel:Relationships"))
				for (const No *x : r->todos("rel:Relationship")) {
					QString alvo = x->a("Target");
					if (x->a("TargetMode") == "External") {
						rels_[x->a("Id")] = "externo:" + alvo;
						continue;
					}
					alvo = alvo.startsWith('/') ? alvo.mid(1) : interna + alvo;
					// "../media/x.png"
					alvo = QDir::cleanPath(alvo);
					rels_[x->a("Id")] = alvo;
				}
		temaMenor_.clear();
		temaMaior_.clear();
		No tema;
		if (lerXml(z_.ler(interna + "theme/theme1.xml"), tema)) {
			if (const No *m = tema.achar("a:minorFont"))
				if (const No *l = m->filho("a:latin"))
					temaMenor_ = l->a("typeface");
			if (const No *m = tema.achar("a:majorFont"))
				if (const No *l = m->filho("a:latin"))
					temaMaior_ = l->a("typeface");
		}
		lerEstilos(interna + "styles.xml");
		lerNumeracao(interna + "numbering.xml");
		Folha f;
		No custom;
		if (lerXml(z_.ler("docProps/custom.xml"), custom))
			if (const No *p = custom.achar("cp:Properties"))
				for (const No &x : p->filhos)
					if (x.a("name") == "lume-esquema")
						f.esquema = x.textoTodo().trimmed();
		const No *corpo = doc.achar("w:body");
		if (!corpo) {
			*erro = "o documento do Word está vazio";
			return false;
		}
		if (const No *s = corpo->filho("w:sectPr")) {
			if (const No *pg = s->filho("w:pgSz")) {
				f.l = mm(pxDeTwips(pg->a("w:w", "11906").toDouble()));
				f.a = mm(pxDeTwips(pg->a("w:h", "16838").toDouble()));
			}
			if (const No *m = s->filho("w:pgMar")) {
				f.sup = mm(pxDeTwips(m->a("w:top", "1417").toDouble()));
				f.inf = mm(pxDeTwips(m->a("w:bottom", "1417").toDouble()));
				f.esq = mm(pxDeTwips(m->a("w:left", m->a("w:start", "1701")).toDouble()));
				f.dir = mm(pxDeTwips(m->a("w:right", m->a("w:end", "1701")).toDouble()));
			}
			for (const No *fr : s->todos("w:footerReference")) {
				No rod;
				if (lerXml(z_.ler(rels_.value(fr->a("r:id"))), rod)) {
					for (const No *t : {rod.achar("w:instrText"), rod.achar("w:fldSimple")}) {
						const QString instr = t ? (t->nome == "w:fldSimple" ? t->a("w:instr") : t->textoTodo()) : QString();
						if (instr.contains("PAGE"))
							f.numeros = true;
					}
				}
			}
		}
		QFont base(padraoLetra_.hasProperty(QTextFormat::FontFamilies) ? familiaDe(padraoLetra_) : QString("Liberation Serif"));
		base.setPointSizeF(padraoLetra_.hasProperty(QTextFormat::FontPointSize) ? padraoLetra_.fontPointSize() : 11);
		documentoNovo(d, base.family(), base.pointSizeF(), f);
		c_ = QTextCursor(d);
		reaproveitar_ = true;
		fluxo(*corpo);
		d->setUndoRedoEnabled(true);
		d->setModified(false);
		return true;
	}

private:
	struct EstiloW {
		bool contextual = false, temContextual = false;
		QString nome, base;
		int nivel = -1;
		QTextBlockFormat b;
		QTextCharFormat c;
		QString numId;
		int ilvl = 0;
	};

	void lerEstilos(const QString &arq)
	{
		No s;
		if (!lerXml(z_.ler(arq), s))
			return;
		const No *raiz = s.achar("w:styles");
		if (!raiz)
			return;
		if (const No *dd = raiz->filho("w:docDefaults")) {
			if (const No *r = dd->achar("w:rPr"))
				lerLetraDocx(*r, padraoLetra_);
			if (const No *p = dd->achar("w:pPr"))
				lerParagrafoDocx(*p, padraoPar_);
		}
		for (const No *e : raiz->todos("w:style")) {
			EstiloW w;
			if (const No *n = e->filho("w:name"))
				w.nome = n->a("w:val");
			if (const No *b = e->filho("w:basedOn"))
				w.base = b->a("w:val");
			if (const No *p = e->filho("w:pPr")) {
				lerParagrafoDocx(*p, w.b);
				if (const No *cs = p->filho("w:contextualSpacing")) {
					w.temContextual = true;
					w.contextual = ligado(cs);
				}
				if (const No *o = p->filho("w:outlineLvl"))
					w.nivel = o->a("w:val").toInt();
				if (const No *np = p->filho("w:numPr")) {
					if (const No *n = np->filho("w:numId")) w.numId = n->a("w:val");
					if (const No *l = np->filho("w:ilvl")) w.ilvl = l->a("w:val").toInt();
				}
			}
			if (const No *r = e->filho("w:rPr"))
				lerLetraDocx(*r, w.c);
			estilos_[e->a("w:styleId")] = w;
			if (e->a("w:type") == "paragraph" && (e->a("w:default") == "1" || e->a("w:default") == "true"))
				padraoId_ = e->a("w:styleId");
		}
	}

	void lerNumeracao(const QString &arq)
	{
		No s;
		if (!lerXml(z_.ler(arq), s))
			return;
		const No *raiz = s.achar("w:numbering");
		if (!raiz)
			return;
		std::map<QString, std::map<int, int>> abstratos;
		for (const No *a : raiz->todos("w:abstractNum")) {
			std::map<int, int> lv;
			for (const No *l : a->todos("w:lvl")) {
				const No *f = l->filho("w:numFmt");
				const QString fmt = f ? f->a("w:val") : "decimal";
				// 0: sem marca (a numeração dos títulos do LibreOffice), 1: marcador, 2: número.
				lv[l->a("w:ilvl").toInt()] = fmt == "none" ? 0 : fmt == "bullet" ? 1 : 2;
			}
			abstratos[a->a("w:abstractNumId")] = lv;
		}
		for (const No *n : raiz->todos("w:num"))
			if (const No *a = n->filho("w:abstractNumId"))
				numeros_[n->a("w:numId")] = abstratos[a->a("w:val")];
	}

	bool contextual(const QString &id, int prof = 0)
	{
		auto it = estilos_.find(id);
		if (it == estilos_.end() || prof > 20)
			return false;
		if (it->second.temContextual)
			return it->second.contextual;
		return it->second.base.isEmpty() ? false : contextual(it->second.base, prof + 1);
	}

	void efetivo(const QString &id, QTextBlockFormat &b, QTextCharFormat &c, QString &nosso, int &nivel, QString &numId, int &ilvl, int prof = 0)
	{
		auto it = estilos_.find(id);
		if (it == estilos_.end() || prof > 20)
			return;
		const EstiloW &w = it->second;
		if (!w.base.isEmpty())
			efetivo(w.base, b, c, nosso, nivel, numId, ilvl, prof + 1);
		b.merge(w.b);
		c.merge(w.c);
		if (w.nivel >= 0)
			nivel = w.nivel;
		if (!w.numId.isEmpty()) {
			numId = w.numId;
			ilvl = w.ilvl;
		}
		QString x = idDeNome(w.nome);
		if (x.isEmpty())
			x = idDeNome(id);
		if (!x.isEmpty())
			nosso = x;
	}

	void novoBloco(const QTextBlockFormat &b, const QTextCharFormat &c)
	{
		if (reaproveitar_) {
			c_.setBlockFormat(b);
			c_.setBlockCharFormat(c);
			reaproveitar_ = false;
		} else {
			c_.insertBlock(b, c);
		}
	}

	void paragrafo(const No &p)
	{
		QTextBlockFormat b = padraoPar_;
		QTextCharFormat c;
		QString nosso, numId;
		int nivel = -1, ilvl = 0;
		const No *ppr = p.filho("w:pPr");
		QString sid = padraoId_;
		if (ppr)
			if (const No *ps = ppr->filho("w:pStyle"))
				sid = ps->a("w:val");
		efetivo(sid, b, c, nosso, nivel, numId, ilvl);
		if (ppr) {
			lerParagrafoDocx(*ppr, b);
			if (const No *o = ppr->filho("w:outlineLvl"))
				nivel = o->a("w:val").toInt();
			if (const No *np = ppr->filho("w:numPr")) {
				if (const No *n = np->filho("w:numId")) numId = n->a("w:val");
				if (const No *l = np->filho("w:ilvl")) ilvl = l->a("w:val").toInt();
			}
			if (const No *r = ppr->filho("w:rPr"))
				lerLetraDocx(*r, c);
		}
		// Espaçamento contextual: entre parágrafos do mesmo estilo, nenhum.
		bool ctx = contextual(sid);
		if (ppr)
			if (const No *cs = ppr->filho("w:contextualSpacing"))
				ctx = ligado(cs);
		if (ctx && sid == ultimoEstilo_ && !reaproveitar_) {
			b.setTopMargin(0);
			QTextCursor ant(c_.block());
			QTextBlockFormat af = ant.blockFormat();
			af.setBottomMargin(0);
			ant.setBlockFormat(af);
		}
		ultimoEstilo_ = ctx ? sid : QString();
		// Dentro das tabelas, o estilo de tabela do Word tira o espaço.
		if (naTabela_ > 0) {
			b.setTopMargin(0);
			b.setBottomMargin(0);
			b.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
		}
		if ((nosso.isEmpty() || nosso == "corpo") && nivel >= 0 && nivel < 9)
			nosso = nivel == 0 ? "cab1" : nivel == 1 ? "cab2" : "cab3";
		if (nosso.isEmpty())
			nosso = "corpo";
		b.setProperty(P_ESTILO, nosso);
		b.setHeadingLevel(nosso == "cab1" ? 1 : nosso == "cab2" ? 2 : nosso == "cab3" ? 3 : 0);
		if (quebraPendente_) {
			b.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
			quebraPendente_ = false;
		}
		const int tipo = !numId.isEmpty() && numId != "0" && numeros_.count(numId) && numeros_[numId].count(ilvl)
		                     ? numeros_[numId][ilvl] : (!numId.isEmpty() && numId != "0" && numeros_.count(numId) ? 1 : 0);
		const bool lista = tipo > 0;
		if (lista) {
			b.setTextIndent(0);
			b.clearProperty(QTextFormat::BlockLeftMargin);
		}
		novoBloco(b, c);
		corridos(p, c);
		if (lista) {
			const bool numerada = tipo == 2;
			QTextList *&l = listas_[numId + "/" + QString::number(ilvl)];
			if (l) {
				l->add(c_.block());
			} else {
				QTextListFormat lf;
				lf.setStyle(numerada ? QTextListFormat::ListDecimal : (ilvl == 1 ? QTextListFormat::ListCircle : ilvl >= 2 ? QTextListFormat::ListSquare : QTextListFormat::ListDisc));
				lf.setIndent(ilvl + 1);
				l = c_.createList(lf);
			}
		}
	}

	void corridos(const No &n, const QTextCharFormat &base)
	{
		for (const No &f : n.filhos) {
			if (f.nome == "w:r") {
				corrida(f, base);
			} else if (f.nome == "w:hyperlink" || f.nome == "w:ins" || f.nome == "w:smartTag" || f.nome == "w:fldSimple" ||
			           f.nome == "w:customXml" || f.nome == "w:dir" || f.nome == "w:bdo") {
				corridos(f, base);
			} else if (f.nome == "w:sdt") {
				if (const No *c = f.filho("w:sdtContent"))
					corridos(*c, base);
			}
		}
	}

	void corrida(const No &r, const QTextCharFormat &base)
	{
		QTextCharFormat c = base;
		if (const No *rp = r.filho("w:rPr")) {
			if (const No *rs = rp->filho("w:rStyle")) {
				QTextBlockFormat lixo;
				QString a, b;
				int n = -1, i = 0;
				QTextCharFormat cs;
				efetivo(rs->a("w:val"), lixo, cs, a, n, b, i);
				c.merge(cs);
			}
			lerLetraDocx(*rp, c);
		}
		for (const No &f : r.filhos) {
			if (f.nome == "w:t") {
				c_.insertText(f.textoTodo(), c);
			} else if (f.nome == "w:tab") {
				c_.insertText("\t", c);
			} else if (f.nome == "w:br" || f.nome == "w:cr") {
				if (f.a("w:type") == "page")
					quebraPendente_ = true;
				else
					c_.insertText(QString(QChar::LineSeparator), c);
			} else if (f.nome == "w:noBreakHyphen") {
				c_.insertText(QString(QChar(0x2011)), c);
			} else if (f.nome == "w:drawing") {
				imagem(f);
			} else if (f.nome == "w:pict" || f.nome == "mc:AlternateContent") {
				if (const No *b = f.achar("a:blip"))
					imagemPorId(b->a("r:embed"), f.achar("wp:extent"));
				else if (const No *v = f.achar("v:imagedata"))
					imagemPorId(v->a("r:id"), nullptr);
			}
		}
	}

	void imagem(const No &desenho)
	{
		const No *b = desenho.achar("a:blip");
		if (b)
			imagemPorId(b->a("r:embed", b->a("r:link")), desenho.achar("wp:extent"));
	}

	void imagemPorId(const QString &id, const No *ext)
	{
		QImage img;
		const QString alvo = rels_.value(id);
		if (alvo.startsWith("externo:")) {
			QString c = alvo.mid(8);
			c = QUrl(c).isLocalFile() ? QUrl(c).toLocalFile() : QUrl::fromPercentEncoding(c.toUtf8());
			if (QDir::isRelativePath(c) && !this->pasta.isEmpty())
				c = QDir(this->pasta).filePath(c);
			img.load(c);
		} else {
			img.loadFromData(z_.ler(alvo));
		}
		if (img.isNull())
			return;
		const QString nome = "lume://imagem/" + QString::number(++imagens_);
		d_->addResource(QTextDocument::ImageResource, QUrl(nome), img);
		QTextImageFormat f;
		f.setName(nome);
		if (ext) {
			const double w = ext->a("cx").toDouble() / 9525.0, h = ext->a("cy").toDouble() / 9525.0;
			if (w > 0) f.setWidth(w);
			if (h > 0) f.setHeight(h);
		}
		c_.insertImage(f);
	}

	void tabela(const No &t)
	{
		std::vector<const No *> linhas;
		for (const No &f : t.filhos)
			if (f.nome == "w:tr")
				linhas.push_back(&f);
		QVector<double> grade;
		if (const No *g = t.filho("w:tblGrid"))
			for (const No *c : g->todos("w:gridCol"))
				grade << c->a("w:w").toDouble();
		int ncol = int(grade.size());
		for (const No *r : linhas) {
			int n = 0;
			for (const No *c : r->todos("w:tc")) {
				const No *pr = c->filho("w:tcPr");
				const No *gs = pr ? pr->filho("w:gridSpan") : nullptr;
				n += gs ? std::max(1, gs->a("w:val").toInt()) : 1;
			}
			ncol = std::max(ncol, n);
		}
		if (linhas.empty() || ncol == 0)
			return;
		ncol = std::min(ncol, 64);
		QTextTableFormat tf = Montador::formatoTabela();
		double soma = 0;
		for (double w : grade)
			soma += w;
		if (grade.size() == ncol && soma > 0) {
			QVector<QTextLength> pct;
			for (double w : grade)
				pct << QTextLength(QTextLength::PercentageLength, w * 100.0 / soma);
			tf.setColumnWidthConstraints(pct);
		}
		QTextTable *tab = c_.insertTable(int(linhas.size()), ncol, tf);
		struct Mescla { int r, c, rs, cs; };
		std::vector<Mescla> mesclas;
		std::map<int, size_t> abertaNaColuna; // vMerge: a coluna -> a mescla
		for (size_t r = 0; r < linhas.size(); r++) {
			int col = 0;
			for (const No *cel : linhas[r]->todos("w:tc")) {
				if (col >= ncol)
					break;
				const No *pr = cel->filho("w:tcPr");
				const No *gs = pr ? pr->filho("w:gridSpan") : nullptr;
				const int cs = gs ? std::max(1, gs->a("w:val").toInt()) : 1;
				const No *vm = pr ? pr->filho("w:vMerge") : nullptr;
				if (vm && vm->a("w:val") != "restart" && abertaNaColuna.count(col)) {
					mesclas[abertaNaColuna[col]].rs++;
					col += cs;
					continue;
				}
				if (vm && vm->a("w:val") == "restart") {
					abertaNaColuna[col] = mesclas.size();
					mesclas.push_back({int(r), col, 1, cs});
				} else {
					abertaNaColuna.erase(col);
					if (cs > 1)
						mesclas.push_back({int(r), col, 1, cs});
				}
				c_ = tab->cellAt(int(r), col).firstCursorPosition();
				reaproveitar_ = true;
				naTabela_++;
				ultimoEstilo_.clear();
				fluxo(*cel);
				naTabela_--;
				col += cs;
			}
		}
		for (const Mescla &m : mesclas)
			if (m.rs > 1 || m.cs > 1)
				tab->mergeCells(m.r, m.c, std::min(m.rs, tab->rows() - m.r), std::min(m.cs, tab->columns() - m.c));
		c_ = QTextCursor(tab->lastCursorPosition());
		c_.movePosition(QTextCursor::NextBlock);
		reaproveitar_ = true;
	}

	void fluxo(const No &n)
	{
		for (const No &f : n.filhos) {
			if (f.nome == "w:p")
				paragrafo(f);
			else if (f.nome == "w:tbl")
				tabela(f);
			else if (f.nome == "w:sdt") {
				if (const No *c = f.filho("w:sdtContent"))
					fluxo(*c);
			} else if (f.nome == "w:customXml" || f.nome == "w:ins")
				fluxo(f);
		}
	}

	QTextDocument *d_ = nullptr;
	ZipLeitor z_;
	QTextCursor c_;
	bool reaproveitar_ = true, quebraPendente_ = false;
	int naTabela_ = 0;
	QString ultimoEstilo_;
	std::map<QString, EstiloW> estilos_;
	std::map<QString, std::map<int, int>> numeros_;
	std::map<QString, QTextList *> listas_;
	QHash<QString, QString> rels_;
	QTextCharFormat padraoLetra_;
	QTextBlockFormat padraoPar_;
	QString padraoId_ = "Normal";
	int imagens_ = 0;
};

// --- PDF e a folha desenhada -----------------------------------------------

// Desenha a página n (0, 1, ...) do documento em p, com a origem no canto da
// folha. O documento já tem o tamanho da página (porFolha).
static void desenharPagina(QPainter &p, QTextDocument *d, int n, bool comNumero)
{
	const QSizeF pg = d->pageSize();
	p.save();
	p.translate(0, -n * pg.height());
	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.clip = QRectF(0, n * pg.height(), pg.width(), pg.height());
	ctx.palette.setColor(QPalette::Text, QColor(0x1c, 0x1b, 0x19));
	p.setClipRect(ctx.clip);
	d->documentLayout()->draw(&p, ctx);
	p.restore();
	if (comNumero) {
		const Folha f = folhaDe(d);
		QFont fn = d->defaultFont();
		fn.setPointSizeF(std::max(8.0, fn.pointSizeF() - 2));
		p.save();
		p.setFont(fn);
		p.setPen(QColor(0x1c, 0x1b, 0x19));
		const double base = pg.height() - px(f.inf) / 2.0;
		p.drawText(QRectF(px(f.esq), base - 12, pg.width() - px(f.esq) - px(f.dir), 24),
		           Qt::AlignRight | Qt::AlignVCenter, QString::number(n + 1));
		p.restore();
	}
}

static int paginas(QTextDocument *d)
{
	const double h = d->pageSize().height();
	if (h <= 0)
		return 1;
	return std::max(1, int(std::ceil(d->documentLayout()->documentSize().height() / h - 0.001)));
}

static bool exportarPdf(QTextDocument *orig, const QString &arq, QString *erro)
{
	std::unique_ptr<QTextDocument> d(orig->clone());
	const Folha f = folhaDe(orig);
	porFolha(d.get(), f);
	QPdfWriter pdf(arq);
	pdf.setCreator("LumeBSD Páginas");
	pdf.setResolution(96);
	pdf.setPageSize(QPageSize(QSizeF(f.l, f.a), QPageSize::Millimeter));
	pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
	QPainter p;
	if (!p.begin(&pdf)) {
		*erro = "não deu para gravar o PDF";
		return false;
	}
	const int n = paginas(d.get());
	for (int i = 0; i < n; i++) {
		if (i > 0)
			pdf.newPage();
		desenharPagina(p, d.get(), i, f.numeros);
	}
	p.end();
	return true;
}

// --- abrir e gravar pela extensão ------------------------------------------

static QString extensao(const QString &arq) { return QFileInfo(arq).suffix().toLower(); }

static bool abrirArquivo(const QString &arq, QTextDocument *d, QString *erro)
{
	QFile f(arq);
	if (!f.open(QIODevice::ReadOnly)) {
		*erro = f.errorString();
		return false;
	}
	const QByteArray dados = f.readAll();
	const QString ext = extensao(arq);
	if (ext == "odt" || ext == "ott" || (dados.startsWith("PK") && dados.contains("opendocument.text"))) {
		LeitorOdt l;
		l.pasta = QFileInfo(arq).absolutePath();
		return l.ler(dados, d, erro);
	}
	if (ext == "docx" || ext == "docm" || ext == "dotx" || dados.startsWith("PK")) {
		LeitorDocx l;
		l.pasta = QFileInfo(arq).absolutePath();
		return l.ler(dados, d, erro);
	}
	if (ext == "doc" || dados.startsWith("\xD0\xCF\x11\xE0")) {
		*erro = "é um .doc do Word antigo (anterior a 2007), que o Páginas não lê; abra no LibreOffice e salve como .docx ou .odt";
		return false;
	}
	Folha fo;
	documentoNovo(d, "IBM Plex Serif", 11.5, fo);
	const QString texto = QString::fromUtf8(dados);
	if (ext == "md" || ext == "markdown")
		d->setMarkdown(texto);
	else if (ext == "html" || ext == "htm")
		d->setHtml(texto);
	else
		d->setPlainText(texto);
	porFolha(d, fo);
	// O Markdown e o HTML não têm os nossos estilos: os cabeçalhos viram cab1-3.
	for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
		const int h = b.blockFormat().headingLevel();
		QTextCursor k(b);
		QTextBlockFormat bf = b.blockFormat();
		bf.setProperty(P_ESTILO, h == 1 ? "cab1" : h == 2 ? "cab2" : h >= 3 ? "cab3" : "corpo");
		k.setBlockFormat(bf);
	}
	d->setUndoRedoEnabled(true);
	d->setModified(false);
	return true;
}

static bool gravarArquivo(const QString &arq, QTextDocument *d, QString *erro)
{
	const QString ext = extensao(arq);
	if (ext == "pdf")
		return exportarPdf(d, arq, erro);
	QByteArray dados;
	if (ext == "odt") {
		EscritorOdt e(d);
		dados = e.gravar();
	} else if (ext == "docx") {
		EscritorDocx e(d);
		dados = e.gravar();
	} else if (ext == "md" || ext == "markdown") {
		dados = d->toMarkdown().toUtf8();
	} else if (ext == "html" || ext == "htm") {
		dados = d->toHtml().toUtf8();
	} else if (ext == "txt") {
		dados = d->toPlainText().toUtf8();
	} else {
		*erro = "formato desconhecido: ." + ext + " (use .odt, .docx, .pdf, .md, .html ou .txt)";
		return false;
	}
	// Grava ao lado e troca: um erro no meio não estraga o arquivo que havia.
	const QString tmp = arq + ".gravando";
	QFile f(tmp);
	if (!f.open(QIODevice::WriteOnly) || f.write(dados) != dados.size() || !f.flush()) {
		*erro = f.errorString();
		f.remove();
		return false;
	}
	f.close();
	QFile::remove(arq);
	if (!QFile::rename(tmp, arq)) {
		*erro = "não deu para gravar " + arq;
		return false;
	}
	return true;
}

// A estrutura do documento em texto (os testes comparam).
static QString resumo(QTextDocument *d)
{
	QString r;
	QTextStream o(&r);
	const Folha f = folhaDe(d);
	o << "folha " << qRound(f.l) << "x" << qRound(f.a) << " margens " << qRound(f.sup) << "/" << qRound(f.dir) << "/" << qRound(f.inf)
	  << "/" << qRound(f.esq) << (f.numeros ? " numeros" : "") << " esquema " << f.esquema << "\n";
	o << "fonte " << d->defaultFont().family() << " " << d->defaultFont().pointSizeF() << "\n";
	std::function<void(QTextFrame::iterator, QTextFrame::iterator, const QString &)> fl;
	fl = [&](QTextFrame::iterator it, QTextFrame::iterator fim, const QString &rec) {
		for (; it != fim && !it.atEnd(); ++it) {
			if (QTextFrame *fr = it.currentFrame()) {
				if (auto *t = qobject_cast<QTextTable *>(fr)) {
					o << rec << "tabela " << t->rows() << "x" << t->columns() << "\n";
					for (int i = 0; i < t->rows(); i++)
						for (int j = 0; j < t->columns(); j++) {
							const QTextTableCell c = t->cellAt(i, j);
							if (c.row() != i || c.column() != j)
								continue;
							o << rec << " celula " << i << "," << j;
							if (c.rowSpan() > 1 || c.columnSpan() > 1)
								o << " mescla " << c.rowSpan() << "x" << c.columnSpan();
							o << "\n";
							fl(c.begin(), c.end(), rec + "  ");
						}
				}
				continue;
			}
			const QTextBlock b = it.currentBlock();
			const QTextBlockFormat bf = b.blockFormat();
			o << rec << "[" << estiloDe(b) << "]";
			if (QTextList *l = b.textList())
				o << " lista" << (listaNumerada(l->format()) ? "-num" : "-mar") << l->format().indent();
			if (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
				o << " quebra";
			if (bf.alignment() & Qt::AlignHCenter) o << " centro";
			else if (bf.alignment() & Qt::AlignJustify) o << " justif";
			else if (bf.alignment() & Qt::AlignRight) o << " dir";
			for (const Pedaco &p : pedacos(b)) {
				if (p.imagem) {
					QSizeF tam;
					imagemDe(d, p.f.toImageFormat(), &tam);
					o << " {imagem " << qRound(tam.width()) << "x" << qRound(tam.height()) << "}";
					continue;
				}
				QString marca;
				if (p.f.fontWeight() >= QFont::DemiBold) marca += "N";
				if (p.f.fontItalic()) marca += "I";
				if (p.f.fontUnderline()) marca += "S";
				if (p.f.fontStrikeOut()) marca += "R";
				if (p.f.verticalAlignment() == QTextCharFormat::AlignSuperScript) marca += "^";
				if (p.f.verticalAlignment() == QTextCharFormat::AlignSubScript) marca += "_";
				QString t = p.texto;
				t.replace(QChar::LineSeparator, "⏎").replace('\t', "⇥");
				o << " " << (marca.isEmpty() ? "" : marca + ":") << "\"" << t << "\"";
			}
			o << "\n";
		}
	};
	fl(d->rootFrame()->begin(), d->rootFrame()->end(), QString());
	return r;
}

// --- cores -----------------------------------------------------------------

struct Cores {
	bool noite = false;
	QColor papel, tinta, cartao, mesa, folha, fio, apagado;
};
static Cores cores_;

// --- a folha na tela -------------------------------------------------------
//
// Um QTextEdit com a largura da página e a altura de todas as páginas (quem
// rola é a mesa, em volta). O documento já vem paginado (porFolha); aqui se
// desenham as folhas por baixo do texto, com uma faixa da mesa entre uma e
// outra (no meio das margens de baixo e de cima, onde não há texto) e o
// número de cada página.

class Pagina : public QTextEdit
{
public:
	std::function<bool(QKeyEvent *)> teclas;
	std::function<void(QMenu *, const QPoint &)> menu;

	explicit Pagina(QWidget *pai) : QTextEdit(pai)
	{
		setFrameStyle(QFrame::NoFrame);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		viewport()->setAutoFillBackground(false);
		setAutoFillBackground(false);
		setAcceptRichText(true);
		setTabChangesFocus(false);
		QPalette p = palette();
		p.setColor(QPalette::Base, Qt::transparent);
		p.setColor(QPalette::Text, QColor(0x1c, 0x1b, 0x19));
		p.setColor(QPalette::Highlight, QColor(0xc9, 0xd8, 0xee));
		p.setColor(QPalette::HighlightedText, QColor(0x1c, 0x1b, 0x19));
		setPalette(p);
		setCursorWidth(2);
		connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] { ajustar(); });
	}

	void ajustar()
	{
		const Folha f = folhaDe(document());
		const QSizeF alvo(px(f.l), px(f.a));
		if (document()->pageSize() != alvo)
			document()->setPageSize(alvo);
		const int w = qRound(alvo.width());
		if (width() != w)
			setFixedWidth(w);
		const int h = qRound(paginas(document()) * alvo.height());
		if (height() != h)
			setFixedHeight(h);
		viewport()->update();
	}

	int paginaDoCursor() const
	{
		const double h = document()->pageSize().height();
		return h > 0 ? int(cursorRect().center().y() / h) + 1 : 1;
	}

protected:
	void resizeEvent(QResizeEvent *e) override
	{
		QTextEdit::resizeEvent(e);
		// O QTextEdit põe a altura da página em -1 quando a largura muda: volta.
		const Folha f = folhaDe(document());
		const QSizeF alvo(px(f.l), px(f.a));
		if (document()->pageSize() != alvo)
			document()->setPageSize(alvo);
	}

	void paintEvent(QPaintEvent *e) override
	{
		{
			QPainter p(viewport());
			const Folha f = folhaDe(document());
			const double H = px(f.a), W = viewport()->width();
			const int n = paginas(document());
			const QRect area = e->rect();
			p.fillRect(area, cores_.mesa);
			const double vao = 18;
			for (int i = 0; i < n; i++) {
				QRectF r(0, i * H + (i > 0 ? vao / 2 : 0), W, H - (i > 0 ? vao / 2 : 0) - (i < n - 1 ? vao / 2 : 0));
				if (!r.intersects(area))
					continue;
				p.fillRect(r, cores_.folha);
				p.setPen(QPen(cores_.fio, 1));
				p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
				if (f.numeros) {
					QFont fn = document()->defaultFont();
					fn.setPointSizeF(std::max(8.0, fn.pointSizeF() - 2));
					p.setFont(fn);
					p.setPen(QColor(0x1c, 0x1b, 0x19));
					const double base = (i + 1) * H - px(f.inf) / 2.0;
					p.drawText(QRectF(px(f.esq), base - 12, W - px(f.esq) - px(f.dir), 24),
					           Qt::AlignRight | Qt::AlignVCenter, QString::number(i + 1));
				}
			}
		}
		QTextEdit::paintEvent(e);
	}

	void keyPressEvent(QKeyEvent *e) override
	{
		if (teclas && teclas(e))
			return;
		QTextEdit::keyPressEvent(e);
	}

	void contextMenuEvent(QContextMenuEvent *e) override
	{
		QMenu *m = createStandardContextMenu(e->pos());
		if (menu)
			menu(m, e->pos());
		m->exec(e->globalPos());
		delete m;
	}
};

// --- a janela --------------------------------------------------------------

static QString rotulo(const QString &t)
{
	return t.toUpper();
}

class Janela : public QMainWindow
{
public:
	Janela()
	{
		setWindowIcon(QIcon::fromTheme("lume-paginas", QIcon("/usr/share/lume/app/lume-paginas.svg")));
		pilha_ = new QStackedWidget(this);
		setCentralWidget(pilha_);
		montarGaleria();
		montarEditor();
		montarBarra();
		montarInspetor();
		statusBar()->setSizeGripEnabled(false);
		palavras_ = new QLabel(this);
		palavras_->setObjectName("rodape");
		statusBar()->addPermanentWidget(palavras_);
		contagem_.setSingleShot(true);
		contagem_.setInterval(350);
		connect(&contagem_, &QTimer::timeout, this, [this] { contar(); });
		resize(1260, 900);
		mostrarGaleria();
	}

	void abrir(const QString &arq)
	{
		QString erro;
		if (!abrirArquivo(arq, doc(), &erro)) {
			QMessageBox::warning(this, "Páginas", "Não deu para abrir " + QFileInfo(arq).fileName() + ":\n" + erro);
			return;
		}
		arquivo_ = QFileInfo(arq).absoluteFilePath();
		recente(arquivo_);
		depoisDeCarregar();
	}

	void modelo(const QString &id)
	{
		montarModelo(doc(), id);
		arquivo_.clear();
		depoisDeCarregar();
	}

	void mostrarGaleria()
	{
		atualizarRecentes();
		pilha_->setCurrentIndex(0);
		barra_->setVisible(false);
		statusBar()->setVisible(false);
		setWindowTitle("Páginas");
	}

	QTextDocument *doc() { return edit_->document(); }

	// Digita e formata como uma pessoa (eventos de teclado de verdade no
	// editor), grava e devolve o resumo: os testes conferem a edição.
	QString autoteste(const QString &saida)
	{
		modelo("branco");
		auto digita = [this](const QString &s) {
			for (QChar c : s) {
				QKeyEvent k(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
				QApplication::sendEvent(edit_, &k);
			}
		};
		auto tecla = [this](int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
			QKeyEvent e(QEvent::KeyPress, k, m, k == Qt::Key_Return ? QString("\r") : QString());
			QApplication::sendEvent(edit_, &e);
		};
		estiloNaSelecao("titulo");
		digita("Meu trabalho");
		tecla(Qt::Key_Return);              // volta ao corpo
		digita("Um texto com ");
		alternar(QTextFormat::FontWeight);
		digita("negrito");
		alternar(QTextFormat::FontWeight);
		digita(" e o resto.");
		tecla(Qt::Key_Return);
		estiloNaSelecao("cab1");
		digita("Lista");
		tecla(Qt::Key_Return);
		porLista(1);
		digita("primeiro");
		tecla(Qt::Key_Return);
		tecla(Qt::Key_Tab);                 // um nível para dentro
		digita("dentro");
		tecla(Qt::Key_Return);
		tecla(Qt::Key_Backtab, Qt::ShiftModifier);
		digita("segundo");
		tecla(Qt::Key_Return);
		porLista(0);
		alinhar(Qt::AlignHCenter);
		digita("centro");
		quebraDePagina();
		alinhar(Qt::AlignLeft | Qt::AlignAbsolute);
		digita("depois da quebra");
		tecla(Qt::Key_Return);
		QTextCursor k = edit_->textCursor();
		QTextTable *tb = k.insertTable(2, 2, Montador::formatoTabela());
		edit_->setTextCursor(tb->cellAt(0, 0).firstCursorPosition());
		digita("a");
		tecla(Qt::Key_Tab);
		digita("b");
		tecla(Qt::Key_Tab);
		digita("c");
		tecla(Qt::Key_Tab);
		digita("d");
		tecla(Qt::Key_Tab);                 // na última célula: linha nova
		digita("e");
		QString erro;
		if (!gravarArquivo(saida, doc(), &erro))
			return "ERRO " + erro;
		QTextDocument d;
		abrirArquivo(saida, &d, &erro);
		return resumo(&d);
	}

protected:
	void closeEvent(QCloseEvent *e) override
	{
		if (pilha_->currentIndex() == 1 && !podeFechar()) {
			e->ignore();
			return;
		}
		e->accept();
	}

private:
	// --- galeria ---
	void montarGaleria()
	{
		auto *g = new QWidget;
		g->setObjectName("galeria");
		auto *v = new QVBoxLayout(g);
		v->setContentsMargins(56, 40, 56, 32);
		auto *t = new QLabel("Escolha um modelo");
		t->setObjectName("marca");
		auto *s = new QLabel("Para começar um documento novo. Todos abrem e salvam em .odt e .docx.");
		s->setObjectName("sub");
		v->addWidget(t);
		v->addWidget(s);
		v->addSpacing(18);
		auto *grade = new QGridLayout;
		grade->setHorizontalSpacing(26);
		grade->setVerticalSpacing(18);
		int i = 0;
		for (const Modelo &m : modelos()) {
			auto *b = new QToolButton;
			b->setObjectName("cartaoModelo");
			b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
			b->setIcon(QIcon(QPixmap::fromImage(miniatura(m.id))));
			b->setIconSize(QSize(150, 212));
			b->setText(m.nome);
			b->setToolTip(m.descricao);
			b->setCursor(Qt::PointingHandCursor);
			const QString id = m.id;
			connect(b, &QToolButton::clicked, this, [this, id] { modelo(id); });
			grade->addWidget(b, i / 6, i % 6, Qt::AlignTop | Qt::AlignLeft);
			i++;
		}
		grade->setColumnStretch(6, 1);
		v->addLayout(grade);
		v->addSpacing(22);
		auto *linha = new QHBoxLayout;
		auto *ab = new QPushButton("Abrir um documento…");
		ab->setObjectName("botaoPrincipal");
		connect(ab, &QPushButton::clicked, this, [this] { escolherEAbrir(); });
		linha->addWidget(ab);
		linha->addStretch();
		v->addLayout(linha);
		v->addSpacing(14);
		auto *r = new QLabel(rotulo("Recentes"));
		r->setObjectName("rotulo");
		v->addWidget(r);
		recentes_ = new QVBoxLayout;
		recentes_->setSpacing(2);
		v->addLayout(recentes_);
		v->addStretch();
		auto *rolar = new QScrollArea;
		rolar->setWidget(g);
		rolar->setWidgetResizable(true);
		rolar->setFrameStyle(QFrame::NoFrame);
		pilha_->addWidget(rolar);
	}

	static QImage miniatura(const QString &id)
	{
		QTextDocument d;
		montarModelo(&d, id);
		const QSizeF pg = d.pageSize();
		QImage img(pg.toSize(), QImage::Format_ARGB32_Premultiplied);
		img.fill(QColor(0xff, 0xfd, 0xf8));
		{
			QPainter p(&img);
			p.setRenderHint(QPainter::Antialiasing);
			p.setRenderHint(QPainter::TextAntialiasing);
			desenharPagina(p, &d, 0, false);
			p.setPen(QPen(QColor(0x1c, 0x1b, 0x19), 3));
			p.drawRect(QRectF(img.rect()).adjusted(1.5, 1.5, -1.5, -1.5));
		}
		return img.scaled(150, 212, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	void atualizarRecentes()
	{
		while (QLayoutItem *it = recentes_->takeAt(0)) {
			delete it->widget();
			delete it;
		}
		const QStringList l = QSettings("LumeBSD", "lume-paginas").value("recentes").toStringList();
		int n = 0;
		for (const QString &a : l) {
			if (!QFileInfo::exists(a))
				continue;
			auto *b = new QPushButton(QFileInfo(a).fileName() + "   —   " + QDir::toNativeSeparators(QFileInfo(a).absolutePath()));
			b->setObjectName("recente");
			b->setFlat(true);
			b->setCursor(Qt::PointingHandCursor);
			connect(b, &QPushButton::clicked, this, [this, a] { abrir(a); });
			recentes_->addWidget(b, 0, Qt::AlignLeft);
			if (++n == 8)
				break;
		}
		if (n == 0) {
			auto *v = new QLabel("Os documentos que você abrir aparecem aqui.");
			v->setObjectName("vazio");
			recentes_->addWidget(v);
		}
	}

	void recente(const QString &a)
	{
		QSettings s("LumeBSD", "lume-paginas");
		QStringList l = s.value("recentes").toStringList();
		l.removeAll(a);
		l.prepend(a);
		while (l.size() > 12)
			l.removeLast();
		s.setValue("recentes", l);
	}

	// --- editor ---
	void montarEditor()
	{
		auto *raiz = new QWidget;
		auto *h = new QHBoxLayout(raiz);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(0);
		auto *coluna = new QWidget;
		auto *cv = new QVBoxLayout(coluna);
		cv->setContentsMargins(0, 0, 0, 0);
		cv->setSpacing(0);
		area_ = new QScrollArea;
		area_->setObjectName("mesa");
		area_->setFrameStyle(QFrame::NoFrame);
		area_->setWidgetResizable(true);
		area_->setAlignment(Qt::AlignHCenter);
		auto *mesa = new QWidget;
		mesa->setObjectName("mesaDentro");
		auto *mh = new QHBoxLayout(mesa);
		mh->setContentsMargins(24, 28, 24, 40);
		edit_ = new Pagina(mesa);
		mh->addStretch();
		mh->addWidget(edit_, 0, Qt::AlignTop);
		mh->addStretch();
		area_->setWidget(mesa);
		cv->addWidget(area_, 1);
		cv->addWidget(montarProcura());
		h->addWidget(coluna, 1);
		inspetor_ = new QStackedWidget;
		inspetor_->setObjectName("inspetor");
		inspetor_->setFixedWidth(290);
		h->addWidget(inspetor_);
		pilha_->addWidget(raiz);

		connect(edit_, &QTextEdit::cursorPositionChanged, this, [this] {
			atualizarInspetor();
			const QRect r = edit_->cursorRect();
			const QPoint p = edit_->viewport()->mapTo(area_->widget(), r.center());
			area_->ensureVisible(p.x(), p.y(), 40, 90);
			contagem_.start();
		});
		connect(edit_, &QTextEdit::currentCharFormatChanged, this, [this](const QTextCharFormat &) { atualizarInspetor(); });
		connect(doc(), &QTextDocument::modificationChanged, this, [this](bool m) { setWindowModified(m); });
		connect(doc(), &QTextDocument::contentsChanged, this, [this] { contagem_.start(); });
		edit_->teclas = [this](QKeyEvent *e) { return tecla(e); };
		edit_->menu = [this](QMenu *m, const QPoint &p) { menuDeContexto(m, p); };
	}

	QWidget *montarProcura()
	{
		procura_ = new QWidget;
		procura_->setObjectName("procura");
		auto *h = new QHBoxLayout(procura_);
		h->setContentsMargins(14, 6, 14, 6);
		auto *pr = new QLineEdit;
		pr->setPlaceholderText("Procurar");
		auto *sb = new QLineEdit;
		sb->setPlaceholderText("Substituir por");
		auto *prox = new QPushButton("Próximo");
		auto *um = new QPushButton("Substituir");
		auto *todos = new QPushButton("Substituir todos");
		auto *fechar = new QToolButton;
		fechar->setText("×");
		fechar->setAutoRaise(true);
		auto *res = new QLabel;
		res->setObjectName("rodape");
		h->addWidget(pr, 2);
		h->addWidget(prox);
		h->addWidget(sb, 2);
		h->addWidget(um);
		h->addWidget(todos);
		h->addWidget(res);
		h->addWidget(fechar);
		procuraCampo_ = pr;
		auto achar = [this, pr, res] {
			if (pr->text().isEmpty())
				return;
			QTextCursor c = doc()->find(pr->text(), edit_->textCursor());
			if (c.isNull())
				c = doc()->find(pr->text(), 0);
			if (c.isNull()) {
				res->setText("nada");
				return;
			}
			res->clear();
			edit_->setTextCursor(c);
		};
		connect(pr, &QLineEdit::returnPressed, this, achar);
		connect(prox, &QPushButton::clicked, this, achar);
		connect(um, &QPushButton::clicked, this, [this, pr, sb, achar] {
			QTextCursor c = edit_->textCursor();
			if (c.hasSelection() && c.selectedText().compare(pr->text(), Qt::CaseInsensitive) == 0)
				c.insertText(sb->text());
			achar();
		});
		connect(todos, &QPushButton::clicked, this, [this, pr, sb, res] {
			if (pr->text().isEmpty())
				return;
			int n = 0;
			QTextCursor c(doc());
			c.beginEditBlock();
			QTextCursor k = doc()->find(pr->text(), 0);
			while (!k.isNull()) {
				k.insertText(sb->text());
				n++;
				k = doc()->find(pr->text(), k);
			}
			c.endEditBlock();
			res->setText(QString::number(n) + (n == 1 ? " troca" : " trocas"));
		});
		connect(fechar, &QToolButton::clicked, this, [this] {
			procura_->hide();
			edit_->setFocus();
		});
		procura_->hide();
		return procura_;
	}

	QAction *acao(const QString &texto, const QString &icone, const QKeySequence &tecla, std::function<void()> f)
	{
		auto *a = new QAction(QIcon::fromTheme(icone), texto, this);
		if (!tecla.isEmpty()) {
			a->setShortcut(tecla);
			a->setShortcutContext(Qt::WindowShortcut);
		}
		connect(a, &QAction::triggered, this, [f] { f(); });
		addAction(a);
		return a;
	}

	QToolButton *botao(const QString &texto, const QString &icone, std::function<void()> f, QMenu *m = nullptr)
	{
		auto *b = new QToolButton;
		b->setText(texto);
		b->setIcon(QIcon::fromTheme(icone));
		b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		b->setIconSize(QSize(22, 22));
		b->setAutoRaise(true);
		if (m) {
			b->setMenu(m);
			b->setPopupMode(QToolButton::InstantPopup);
		} else {
			connect(b, &QToolButton::clicked, this, [f] { f(); });
		}
		barra_->addWidget(b);
		return b;
	}

	void montarBarra()
	{
		barra_ = new QToolBar(this);
		barra_->setMovable(false);
		barra_->setFloatable(false);
		barra_->setObjectName("barra");
		addToolBar(Qt::TopToolBarArea, barra_);

		acao("Novo", "document-new", QKeySequence::New, [this] { if (podeFechar()) mostrarGaleria(); });
		acao("Abrir", "document-open", QKeySequence::Open, [this] { escolherEAbrir(); });
		acao("Salvar", "document-save", QKeySequence::Save, [this] { salvar(); });
		acao("Salvar como", "document-save-as", QKeySequence::SaveAs, [this] { salvarComo(); });
		acao("Exportar PDF", "application-pdf", QKeySequence(Qt::CTRL | Qt::Key_P), [this] { exportar("pdf"); });
		acao("Procurar", "edit-find", QKeySequence::Find, [this] {
			procura_->show();
			procuraCampo_->setFocus();
			procuraCampo_->selectAll();
		});
		acao("Quebra de página", "", QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { quebraDePagina(); });
		acao("Negrito", "", QKeySequence::Bold, [this] { alternar(QTextFormat::FontWeight); });
		acao("Itálico", "", QKeySequence::Italic, [this] { alternar(QTextFormat::FontItalic); });
		acao("Sublinhado", "", QKeySequence::Underline, [this] { alternar(QTextFormat::FontUnderline); });
		acao("Título", "", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T), [this] { estiloNaSelecao("titulo"); });
		acao("Corpo", "", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0), [this] { estiloNaSelecao("corpo"); });
		acao("Cabeçalho 1", "", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1), [this] { estiloNaSelecao("cab1"); });
		acao("Cabeçalho 2", "", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2), [this] { estiloNaSelecao("cab2"); });
		acao("Cabeçalho 3", "", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3), [this] { estiloNaSelecao("cab3"); });
		acao("Alinhar à esquerda", "", QKeySequence(Qt::CTRL | Qt::Key_L), [this] { alinhar(Qt::AlignLeft | Qt::AlignAbsolute); });
		acao("Centralizar", "", QKeySequence(Qt::CTRL | Qt::Key_E), [this] { alinhar(Qt::AlignHCenter); });
		acao("Alinhar à direita", "", QKeySequence(Qt::CTRL | Qt::Key_R), [this] { alinhar(Qt::AlignRight | Qt::AlignAbsolute); });
		acao("Justificar", "", QKeySequence(Qt::CTRL | Qt::Key_J), [this] { alinhar(Qt::AlignJustify); });

		auto *doc_ = new QMenu(this);
		doc_->addAction(QIcon::fromTheme("document-new"), "Novo…", this, [this] { if (podeFechar()) mostrarGaleria(); });
		doc_->addAction(QIcon::fromTheme("document-open"), "Abrir…", this, [this] { escolherEAbrir(); });
		doc_->addSeparator();
		doc_->addAction(QIcon::fromTheme("document-save"), "Salvar", this, [this] { salvar(); });
		doc_->addAction(QIcon::fromTheme("document-save-as"), "Salvar como…", this, [this] { salvarComo(); });
		botao("Documento", "document-open-folder", nullptr, doc_);
		barra_->addSeparator();
		auto *ins = new QMenu(this);
		ins->addAction(QIcon::fromTheme("insert-table"), "Tabela…", this, [this] { inserirTabela(); });
		ins->addAction(QIcon::fromTheme("insert-image"), "Imagem…", this, [this] { inserirImagem(); });
		ins->addAction(QIcon::fromTheme("insert-page-break"), "Quebra de página", this, [this] { quebraDePagina(); });
		ins->addAction(QIcon::fromTheme("view-calendar-day"), "Data de hoje", this, [this] { edit_->textCursor().insertText(hojePorExtenso()); });
		ins->addAction(QIcon::fromTheme("format-list-unordered"), "Lista com marcadores", this, [this] { porLista(1); });
		ins->addAction(QIcon::fromTheme("format-list-ordered"), "Lista numerada", this, [this] { porLista(2); });
		botao("Inserir", "list-add", nullptr, ins);
		botao("Tabela", "insert-table", [this] { inserirTabela(); });
		botao("Imagem", "insert-image", [this] { inserirImagem(); });
		auto *espaco = new QWidget;
		espaco->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		barra_->addWidget(espaco);
		titulo_ = new QLabel;
		titulo_->setObjectName("tituloDoc");
		barra_->addWidget(titulo_);
		auto *espaco2 = new QWidget;
		espaco2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		barra_->addWidget(espaco2);
		auto *exp = new QMenu(this);
		exp->addAction(QIcon::fromTheme("application-pdf"), "PDF…", this, [this] { exportar("pdf"); });
		exp->addAction(QIcon::fromTheme("x-office-document"), "Word (.docx)…", this, [this] { exportar("docx"); });
		exp->addAction(QIcon::fromTheme("text-markdown"), "Markdown (.md)…", this, [this] { exportar("md"); });
		exp->addAction(QIcon::fromTheme("text-plain"), "Texto puro (.txt)…", this, [this] { exportar("txt"); });
		botao("Exportar", "document-export", nullptr, exp);
		barra_->addSeparator();
		bFormatar_ = botao("Formatar", "format-text-bold", [this] { painel(0); });
		bDocumento_ = botao("Folha", "document-properties", [this] { painel(1); });
		bFormatar_->setCheckable(true);
		bDocumento_->setCheckable(true);
		bFormatar_->setChecked(true);
	}

	void painel(int i)
	{
		const bool fechar = inspetor_->isVisible() && inspetor_->currentIndex() == i;
		inspetor_->setVisible(!fechar);
		inspetor_->setCurrentIndex(i);
		bFormatar_->setChecked(!fechar && i == 0);
		bDocumento_->setChecked(!fechar && i == 1);
	}

	// --- inspetor ---
	static QLabel *secao(QVBoxLayout *v, const QString &t)
	{
		auto *l = new QLabel(rotulo(t));
		l->setObjectName("rotulo");
		v->addSpacing(10);
		v->addWidget(l);
		return l;
	}

	static QFrame *fio()
	{
		auto *f = new QFrame;
		f->setObjectName("fio");
		return f;
	}

	QToolButton *alterna(const QString &texto, const QString &dica, QHBoxLayout *h, std::function<void()> f)
	{
		auto *b = new QToolButton;
		b->setText(texto);
		b->setToolTip(dica);
		b->setCheckable(true);
		b->setObjectName("alterna");
		b->setMinimumSize(36, 30);
		connect(b, &QToolButton::clicked, this, [f] { f(); });
		h->addWidget(b);
		return b;
	}

	void montarInspetor()
	{
		// Formatar: o parágrafo e as letras.
		auto *fw = new QWidget;
		auto *v = new QVBoxLayout(fw);
		v->setContentsMargins(18, 14, 18, 14);
		v->setSpacing(6);
		auto *cab = new QLabel("Formatar");
		cab->setObjectName("parte");
		v->addWidget(cab);
		v->addWidget(fio());
		secao(v, "Estilo do parágrafo");
		estilo_ = new QComboBox;
		for (const QString &id : ESTILOS)
			estilo_->addItem(estilo(id, "padrao").nome, id);
		connect(estilo_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			estiloNaSelecao(estilo_->itemData(i).toString());
		});
		v->addWidget(estilo_);

		secao(v, "Fonte");
		fonte_ = new QFontComboBox;
		connect(fonte_, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
			if (atualizando_) return;
			QTextCharFormat c;
			c.setFontFamilies(QStringList{f.family()});
			misturar(c);
		});
		v->addWidget(fonte_);
		auto *th = new QHBoxLayout;
		tamanho_ = new QComboBox;
		tamanho_->setEditable(true);
		for (int s : {8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36, 48, 64, 72})
			tamanho_->addItem(QString::number(s));
		tamanho_->setValidator(new QDoubleValidator(4, 400, 1, tamanho_));
		auto aplicaTam = [this] {
			if (atualizando_) return;
			const double t = QLocale::c().toDouble(tamanho_->currentText().replace(',', '.'));
			if (t >= 4 && t <= 400) {
				QTextCharFormat c;
				c.setFontPointSize(t);
				misturar(c);
			}
		};
		connect(tamanho_, &QComboBox::textActivated, this, aplicaTam);
		connect(tamanho_->lineEdit(), &QLineEdit::returnPressed, this, aplicaTam);
		th->addWidget(tamanho_, 1);
		auto *cor = new QToolButton;
		cor->setText("Cor");
		cor->setObjectName("alterna");
		cor->setMinimumHeight(30);
		connect(cor, &QToolButton::clicked, this, [this] {
			const QColor c = QColorDialog::getColor(edit_->textColor(), this, "Cor do texto");
			if (c.isValid()) {
				QTextCharFormat f;
				f.setForeground(c);
				misturar(f);
			}
		});
		th->addWidget(cor);
		v->addLayout(th);
		auto *bh = new QHBoxLayout;
		bh->setSpacing(4);
		negrito_ = alterna("N", "Negrito (Ctrl+B)", bh, [this] { alternar(QTextFormat::FontWeight); });
		italico_ = alterna("I", "Itálico (Ctrl+I)", bh, [this] { alternar(QTextFormat::FontItalic); });
		sublinhado_ = alterna("S", "Sublinhado (Ctrl+U)", bh, [this] { alternar(QTextFormat::FontUnderline); });
		riscado_ = alterna("R", "Riscado", bh, [this] { alternar(QTextFormat::FontStrikeOut); });
		sobrescrito_ = alterna("x²", "Sobrescrito", bh, [this] { alternarPosicao(QTextCharFormat::AlignSuperScript); });
		subscrito_ = alterna("x₂", "Subscrito", bh, [this] { alternarPosicao(QTextCharFormat::AlignSubScript); });
		{
			QFont f = negrito_->font(); f.setBold(true); negrito_->setFont(f);
			f = italico_->font(); f.setItalic(true); italico_->setFont(f);
			f = sublinhado_->font(); f.setUnderline(true); sublinhado_->setFont(f);
			f = riscado_->font(); f.setStrikeOut(true); riscado_->setFont(f);
		}
		v->addLayout(bh);

		secao(v, "Alinhamento");
		auto *ah = new QHBoxLayout;
		ah->setSpacing(4);
		alin_[0] = alterna("Esquerda", "Alinhar à esquerda (Ctrl+L)", ah, [this] { alinhar(Qt::AlignLeft | Qt::AlignAbsolute); });
		alin_[1] = alterna("Centro", "Centralizar (Ctrl+E)", ah, [this] { alinhar(Qt::AlignHCenter); });
		alin_[2] = alterna("Direita", "Alinhar à direita (Ctrl+R)", ah, [this] { alinhar(Qt::AlignRight | Qt::AlignAbsolute); });
		alin_[3] = alterna("Justif.", "Justificar (Ctrl+J)", ah, [this] { alinhar(Qt::AlignJustify); });
		for (QToolButton *b : alin_)
			b->setMinimumWidth(52);
		v->addLayout(ah);

		secao(v, "Espaçamento");
		auto *ef = new QFormLayout;
		ef->setLabelAlignment(Qt::AlignLeft);
		entrelinha_ = new QComboBox;
		for (const auto &[n, val] : std::vector<std::pair<QString, double>>{{"Simples (1,0)", 100}, {"1,15", 115}, {"1,5", 150}, {"Duplo (2,0)", 200}})
			entrelinha_->addItem(n, val);
		connect(entrelinha_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			paragrafo([&](QTextBlockFormat &b) { b.setLineHeight(entrelinha_->itemData(i).toDouble(), QTextBlockFormat::ProportionalHeight); });
		});
		ef->addRow("Entre as linhas", entrelinha_);
		depois_ = new QDoubleSpinBox;
		depois_->setRange(0, 72);
		depois_->setSuffix(" pt");
		depois_->setDecimals(0);
		connect(depois_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double x) {
			if (!atualizando_) paragrafo([&](QTextBlockFormat &b) { b.setBottomMargin(pxDePt(x)); });
		});
		ef->addRow("Depois do parágrafo", depois_);
		recuo_ = new QDoubleSpinBox;
		recuo_->setRange(0, 10);
		recuo_->setSingleStep(0.25);
		recuo_->setSuffix(" cm");
		recuo_->setDecimals(2);
		connect(recuo_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double x) {
			if (!atualizando_) paragrafo([&](QTextBlockFormat &b) { b.setTextIndent(px(x * 10)); });
		});
		ef->addRow("Recuo da 1ª linha", recuo_);
		recuoEsq_ = new QDoubleSpinBox;
		recuoEsq_->setRange(0, 15);
		recuoEsq_->setSingleStep(0.5);
		recuoEsq_->setSuffix(" cm");
		recuoEsq_->setDecimals(2);
		connect(recuoEsq_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double x) {
			if (!atualizando_) paragrafo([&](QTextBlockFormat &b) { b.setLeftMargin(px(x * 10)); });
		});
		ef->addRow("Recuo à esquerda", recuoEsq_);
		v->addLayout(ef);

		secao(v, "Listas");
		auto *lh = new QHBoxLayout;
		lista_ = new QComboBox;
		lista_->addItems({"Nenhuma", "Com marcadores", "Numerada"});
		connect(lista_, qOverload<int>(&QComboBox::activated), this, [this](int i) { porLista(i); });
		lh->addWidget(lista_, 1);
		auto *menos = new QToolButton;
		menos->setText("‹");
		menos->setToolTip("Um nível para fora (Shift+Tab)");
		menos->setObjectName("alterna");
		connect(menos, &QToolButton::clicked, this, [this] { nivel(-1); });
		auto *mais = new QToolButton;
		mais->setText("›");
		mais->setToolTip("Um nível para dentro (Tab)");
		mais->setObjectName("alterna");
		connect(mais, &QToolButton::clicked, this, [this] { nivel(+1); });
		lh->addWidget(menos);
		lh->addWidget(mais);
		v->addLayout(lh);
		v->addStretch();
		auto *rf = new QScrollArea;
		rf->setWidget(fw);
		rf->setWidgetResizable(true);
		rf->setFrameStyle(QFrame::NoFrame);
		inspetor_->addWidget(rf);

		// Folha: o papel, as margens, a fonte do corpo e o esquema.
		auto *dw = new QWidget;
		auto *dv = new QVBoxLayout(dw);
		dv->setContentsMargins(18, 14, 18, 14);
		dv->setSpacing(6);
		auto *dc = new QLabel("Folha");
		dc->setObjectName("parte");
		dv->addWidget(dc);
		dv->addWidget(fio());
		secao(dv, "Papel");
		papel_ = new QComboBox;
		for (const Papel &p : PAPEIS)
			papel_->addItem(p.nome);
		papel_->addItem("Outro");
		dv->addWidget(papel_);
		orientacao_ = new QComboBox;
		orientacao_->addItems({"Retrato", "Paisagem"});
		dv->addWidget(orientacao_);
		secao(dv, "Margens");
		auto *mg = new QGridLayout;
		const char *nomes[] = {"Em cima", "Embaixo", "Esquerda", "Direita"};
		for (int i = 0; i < 4; i++) {
			margem_[i] = new QDoubleSpinBox;
			margem_[i]->setRange(0, 8);
			margem_[i]->setSingleStep(0.25);
			margem_[i]->setDecimals(2);
			margem_[i]->setSuffix(" cm");
			mg->addWidget(new QLabel(nomes[i]), (i / 2) * 2, i % 2);
			mg->addWidget(margem_[i], (i / 2) * 2 + 1, i % 2);
		}
		dv->addLayout(mg);
		numeros_ = new QCheckBox("Números de página no rodapé");
		dv->addWidget(numeros_);
		secao(dv, "Texto do corpo");
		fonteCorpo_ = new QFontComboBox;
		dv->addWidget(fonteCorpo_);
		tamCorpo_ = new QDoubleSpinBox;
		tamCorpo_->setRange(6, 36);
		tamCorpo_->setSingleStep(0.5);
		tamCorpo_->setDecimals(1);
		tamCorpo_->setSuffix(" pt");
		dv->addWidget(tamCorpo_);
		secao(dv, "Normas");
		esquema_ = new QComboBox;
		esquema_->addItem("Estilos do Páginas", "padrao");
		esquema_->addItem("ABNT (NBR 14724)", "abnt");
		esquema_->setToolTip("Com a ABNT, os títulos ficam no tamanho do corpo, o corpo com 1,5 de entrelinha e recuo de 1,25 cm, e a citação longa com recuo de 4 cm.");
		dv->addWidget(esquema_);
		auto *abnt = new QPushButton("Pôr as margens e a fonte da ABNT");
		abnt->setToolTip("Margens de 3 cm (em cima e à esquerda) e 2 cm, Times New Roman 12, números de página.");
		dv->addWidget(abnt);
		dv->addStretch();
		auto *rd = new QScrollArea;
		rd->setWidget(dw);
		rd->setWidgetResizable(true);
		rd->setFrameStyle(QFrame::NoFrame);
		inspetor_->addWidget(rd);

		auto mudarFolha = [this] {
			if (atualizando_) return;
			Folha f = folhaDe(doc());
			const int p = papel_->currentIndex();
			if (p >= 0 && p < int(std::size(PAPEIS))) {
				f.l = PAPEIS[p].l;
				f.a = PAPEIS[p].a;
			}
			const bool paisagem = orientacao_->currentIndex() == 1;
			if (paisagem != (f.l > f.a))
				std::swap(f.l, f.a);
			f.sup = margem_[0]->value() * 10;
			f.inf = margem_[1]->value() * 10;
			f.esq = margem_[2]->value() * 10;
			f.dir = margem_[3]->value() * 10;
			f.numeros = numeros_->isChecked();
			porFolha(doc(), f);
			doc()->setModified(true);
			edit_->ajustar();
		};
		connect(papel_, qOverload<int>(&QComboBox::activated), this, mudarFolha);
		connect(orientacao_, qOverload<int>(&QComboBox::activated), this, mudarFolha);
		for (auto *m : margem_)
			connect(m, qOverload<double>(&QDoubleSpinBox::valueChanged), this, mudarFolha);
		connect(numeros_, &QCheckBox::toggled, this, mudarFolha);
		auto mudarCorpo = [this] {
			if (atualizando_) return;
			QFont f(fonteCorpo_->currentFont().family());
			f.setPointSizeF(tamCorpo_->value());
			doc()->setDefaultFont(f);
			reaplicarEstilos(doc());
			doc()->setModified(true);
		};
		connect(fonteCorpo_, &QFontComboBox::currentFontChanged, this, mudarCorpo);
		connect(tamCorpo_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, mudarCorpo);
		connect(esquema_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			Folha f = folhaDe(doc());
			f.esquema = esquema_->itemData(i).toString();
			porFolha(doc(), f);
			reaplicarEstilos(doc());
			doc()->setModified(true);
			atualizarInspetor();
		});
		connect(abnt, &QPushButton::clicked, this, [this] {
			Folha f = folhaDe(doc());
			f.sup = 30; f.esq = 30; f.inf = 20; f.dir = 20;
			f.numeros = true;
			f.esquema = "abnt";
			porFolha(doc(), f);
			QFont fn("Times New Roman");
			fn.setPointSizeF(12);
			doc()->setDefaultFont(fn);
			reaplicarEstilos(doc());
			doc()->setModified(true);
			edit_->ajustar();
			lerFolha();
		});
	}

	void lerFolha()
	{
		atualizando_ = true;
		const Folha f = folhaDe(doc());
		int p = int(std::size(PAPEIS));
		for (int i = 0; i < int(std::size(PAPEIS)); i++) {
			const double l = std::min(f.l, f.a), a = std::max(f.l, f.a);
			if (std::abs(PAPEIS[i].l - l) < 1 && std::abs(PAPEIS[i].a - a) < 1)
				p = i;
		}
		papel_->setCurrentIndex(p);
		orientacao_->setCurrentIndex(f.l > f.a ? 1 : 0);
		margem_[0]->setValue(f.sup / 10);
		margem_[1]->setValue(f.inf / 10);
		margem_[2]->setValue(f.esq / 10);
		margem_[3]->setValue(f.dir / 10);
		numeros_->setChecked(f.numeros);
		fonteCorpo_->setCurrentFont(doc()->defaultFont());
		tamCorpo_->setValue(doc()->defaultFont().pointSizeF());
		esquema_->setCurrentIndex(std::max(0, esquema_->findData(f.esquema)));
		atualizando_ = false;
	}

	void atualizarInspetor()
	{
		if (pilha_->currentIndex() != 1)
			return;
		atualizando_ = true;
		const QTextCursor c = edit_->textCursor();
		const QTextCharFormat f = edit_->currentCharFormat();
		const QTextBlockFormat b = c.blockFormat();
		const int i = estilo_->findData(estiloDe(c.block()));
		estilo_->setCurrentIndex(std::max(0, i));
		const QString fam = familiaDe(f).isEmpty() ? doc()->defaultFont().family() : familiaDe(f);
		fonte_->setCurrentFont(QFont(fam));
		const double t = f.fontPointSize() > 0 ? f.fontPointSize() : doc()->defaultFont().pointSizeF();
		tamanho_->setEditText(QLocale(QLocale::Portuguese, QLocale::Brazil).toString(t, 'g', 3));
		negrito_->setChecked(f.fontWeight() >= QFont::DemiBold);
		italico_->setChecked(f.fontItalic());
		sublinhado_->setChecked(f.fontUnderline());
		riscado_->setChecked(f.fontStrikeOut());
		sobrescrito_->setChecked(f.verticalAlignment() == QTextCharFormat::AlignSuperScript);
		subscrito_->setChecked(f.verticalAlignment() == QTextCharFormat::AlignSubScript);
		const Qt::Alignment a = b.alignment();
		alin_[0]->setChecked(!(a & (Qt::AlignHCenter | Qt::AlignRight | Qt::AlignJustify)));
		alin_[1]->setChecked(a & Qt::AlignHCenter);
		alin_[2]->setChecked((a & Qt::AlignRight) && !(a & Qt::AlignHCenter));
		alin_[3]->setChecked(a & Qt::AlignJustify);
		const double lh = b.lineHeightType() == QTextBlockFormat::ProportionalHeight ? b.lineHeight() : 100;
		int melhor = 0;
		for (int k = 0; k < entrelinha_->count(); k++)
			if (std::abs(entrelinha_->itemData(k).toDouble() - lh) < std::abs(entrelinha_->itemData(melhor).toDouble() - lh))
				melhor = k;
		entrelinha_->setCurrentIndex(melhor);
		depois_->setValue(b.bottomMargin() * 72.0 / 96.0);
		recuo_->setValue(mm(b.textIndent()) / 10);
		recuoEsq_->setValue(mm(b.leftMargin()) / 10);
		const QTextList *l = c.currentList();
		lista_->setCurrentIndex(!l ? 0 : listaNumerada(l->format()) ? 2 : 1);
		atualizando_ = false;
	}

	// --- ações de formato ---
	void misturar(const QTextCharFormat &f)
	{
		QTextCursor c = edit_->textCursor();
		if (c.hasSelection())
			c.mergeCharFormat(f);
		edit_->mergeCurrentCharFormat(f);
		edit_->setFocus();
	}

	void alternar(QTextFormat::Property p)
	{
		const QTextCharFormat atual = edit_->currentCharFormat();
		QTextCharFormat f;
		switch (p) {
		case QTextFormat::FontWeight:
			f.setFontWeight(atual.fontWeight() >= QFont::DemiBold ? QFont::Normal : QFont::Bold);
			break;
		case QTextFormat::FontItalic:
			f.setFontItalic(!atual.fontItalic());
			break;
		case QTextFormat::FontUnderline:
			f.setFontUnderline(!atual.fontUnderline());
			break;
		case QTextFormat::FontStrikeOut:
			f.setFontStrikeOut(!atual.fontStrikeOut());
			break;
		default:
			return;
		}
		misturar(f);
		atualizarInspetor();
	}

	void alternarPosicao(QTextCharFormat::VerticalAlignment v)
	{
		QTextCharFormat f;
		f.setVerticalAlignment(edit_->currentCharFormat().verticalAlignment() == v ? QTextCharFormat::AlignNormal : v);
		misturar(f);
		atualizarInspetor();
	}

	void paragrafo(const std::function<void(QTextBlockFormat &)> &f)
	{
		if (atualizando_)
			return;
		QTextCursor c = edit_->textCursor();
		c.beginEditBlock();
		QTextBlock b = doc()->findBlock(c.selectionStart());
		const QTextBlock fim = doc()->findBlock(c.selectionEnd());
		while (b.isValid()) {
			QTextCursor k(b);
			QTextBlockFormat bf = b.blockFormat();
			f(bf);
			k.setBlockFormat(bf);
			if (b == fim)
				break;
			b = b.next();
		}
		c.endEditBlock();
	}

	void alinhar(Qt::Alignment a)
	{
		paragrafo([a](QTextBlockFormat &b) { b.setAlignment(a); });
		atualizarInspetor();
	}

	void estiloNaSelecao(const QString &id)
	{
		aplicarEstilo(edit_->textCursor(), id);
		edit_->setCurrentCharFormat(letraDoEstilo(estilo(id, folhaDe(doc()).esquema), doc()->defaultFont()));
		edit_->setFocus();
		atualizarInspetor();
	}

	// 0: sem lista, 1: marcadores, 2: numerada.
	void porLista(int tipo)
	{
		QTextCursor c = edit_->textCursor();
		c.beginEditBlock();
		if (tipo == 0) {
			QTextBlock b = doc()->findBlock(c.selectionStart());
			const QTextBlock fim = doc()->findBlock(c.selectionEnd());
			while (b.isValid()) {
				if (QTextList *l = b.textList()) {
					l->remove(b);
					QTextCursor k(b);
					QTextBlockFormat bf = b.blockFormat();
					bf.setIndent(0);
					k.setBlockFormat(bf);
				}
				if (b == fim)
					break;
				b = b.next();
			}
		} else {
			const auto estiloLista = tipo == 2 ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
			if (QTextList *l = c.currentList()) {
				QTextListFormat lf = l->format();
				lf.setStyle(estiloLista);
				l->setFormat(lf);
			} else {
				QTextListFormat lf;
				lf.setStyle(estiloLista);
				lf.setIndent(1);
				QTextBlockFormat bf = c.blockFormat();
				bf.setTextIndent(0);
				c.setBlockFormat(bf);
				c.createList(lf);
			}
		}
		c.endEditBlock();
		atualizarInspetor();
		edit_->setFocus();
	}

	void nivel(int passo)
	{
		QTextCursor c = edit_->textCursor();
		QTextList *l = c.currentList();
		if (!l)
			return;
		QTextListFormat lf = l->format();
		const int novo = lf.indent() + passo;
		const QTextBlock b = c.block();
		c.beginEditBlock();
		l->remove(b);
		QTextCursor k(b);
		QTextBlockFormat bf = b.blockFormat();
		bf.setIndent(0);
		k.setBlockFormat(bf);
		if (novo >= 1) {
			// Junta à lista do mesmo nível logo acima, se houver.
			QTextList *vizinha = nullptr;
			for (QTextBlock p = b.previous(); p.isValid(); p = p.previous()) {
				QTextList *pl = p.textList();
				if (!pl)
					break;
				if (pl->format().indent() == novo) {
					vizinha = pl;
					break;
				}
				if (pl->format().indent() < novo)
					break;
			}
			if (vizinha) {
				vizinha->add(b);
			} else {
				const bool num = listaNumerada(lf);
				lf.setIndent(novo);
				lf.setStyle(num ? QTextListFormat::ListDecimal : novo == 2 ? QTextListFormat::ListCircle : novo >= 3 ? QTextListFormat::ListSquare : QTextListFormat::ListDisc);
				k.createList(lf);
			}
		}
		c.endEditBlock();
		atualizarInspetor();
	}

	bool tecla(QKeyEvent *e)
	{
		QTextCursor c = edit_->textCursor();
		if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) && c.currentList() && !c.currentTable() &&
		    c.position() == c.block().position()) {
			nivel(e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier) ? -1 : 1);
			return true;
		}
		if (e->key() == Qt::Key_Tab && c.currentTable() && !(e->modifiers() & Qt::ShiftModifier)) {
			QTextTable *t = c.currentTable();
			const QTextTableCell cel = t->cellAt(c);
			int r = cel.row(), k = cel.column() + cel.columnSpan();
			if (k >= t->columns()) {
				k = 0;
				r++;
			}
			if (r >= t->rows())
				t->appendRows(1);
			edit_->setTextCursor(t->cellAt(r, k).firstCursorPosition());
			return true;
		}
		// Enter num parágrafo que começa uma página: o seguinte não começa outra.
		if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
		    (c.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) && estiloDe(c.block()) == "corpo") {
			QTextBlockFormat bf = c.blockFormat();
			bf.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
			c.beginEditBlock();
			if (c.hasSelection())
				c.removeSelectedText();
			c.insertBlock(bf, edit_->currentCharFormat());
			c.endEditBlock();
			edit_->setTextCursor(c);
			return true;
		}
		// Enter num cabeçalho: o parágrafo seguinte volta ao corpo.
		if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
		    !c.hasSelection() && c.atBlockEnd() && !c.currentList()) {
			const QString id = estiloDe(c.block());
			if (id != "corpo" && id != "citacao") {
				c.beginEditBlock();
				const Estilo es = estilo("corpo", folhaDe(doc()).esquema);
				const QTextCharFormat letra = letraDoEstilo(es, doc()->defaultFont());
				c.insertBlock(paragrafoDoEstilo(es), letra);
				c.endEditBlock();
				edit_->setTextCursor(c);
				edit_->setCurrentCharFormat(letra);
				return true;
			}
		}
		return false;
	}

	void quebraDePagina()
	{
		QTextCursor c = edit_->textCursor();
		c.beginEditBlock();
		QTextBlockFormat bf = c.blockFormat();
		bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
		if (c.atBlockStart() && !c.block().text().isEmpty()) {
			c.setBlockFormat(bf);
		} else {
			c.insertBlock(bf);
		}
		c.endEditBlock();
		edit_->setTextCursor(c);
	}

	void inserirTabela()
	{
		QDialog d(this);
		d.setWindowTitle("Inserir tabela");
		auto *f = new QFormLayout(&d);
		auto *l = new QSpinBox;
		l->setRange(1, 200);
		l->setValue(3);
		auto *c = new QSpinBox;
		c->setRange(1, 20);
		c->setValue(3);
		auto *cab = new QCheckBox("A primeira linha é o cabeçalho (em negrito)");
		cab->setChecked(true);
		f->addRow("Linhas", l);
		f->addRow("Colunas", c);
		f->addRow(cab);
		auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		bb->button(QDialogButtonBox::Ok)->setText("Inserir");
		bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
		connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
		connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
		f->addRow(bb);
		if (d.exec() != QDialog::Accepted)
			return;
		QTextCursor k = edit_->textCursor();
		k.beginEditBlock();
		if (!k.block().text().isEmpty()) {
			k.movePosition(QTextCursor::EndOfBlock);
			QTextBlockFormat bf = k.blockFormat();
			bf.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
			k.insertBlock(bf);
		}
		QTextTable *t = k.insertTable(l->value(), c->value(), Montador::formatoTabela());
		for (int r = 0; r < t->rows(); r++)
			for (int j = 0; j < t->columns(); j++) {
				QTextCursor cc = t->cellAt(r, j).firstCursorPosition();
				QTextBlockFormat bf = cc.blockFormat();
				bf.setTextIndent(0);
				bf.setBottomMargin(0);
				bf.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
				bf.setProperty(P_ESTILO, "corpo");
				cc.setBlockFormat(bf);
				if (r == 0 && cab->isChecked()) {
					QTextCharFormat cf;
					cf.setFontWeight(QFont::Bold);
					cc.setBlockCharFormat(cf);
				}
			}
		k.endEditBlock();
		edit_->setTextCursor(t->cellAt(0, 0).firstCursorPosition());
	}

	double larguraDoTexto()
	{
		const Folha f = folhaDe(doc());
		return px(f.l - f.esq - f.dir);
	}

	void inserirImagem()
	{
		const QString arq = QFileDialog::getOpenFileName(this, "Inserir imagem", QDir::homePath() + "/Imagens",
		                                                 "Imagens (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)");
		if (arq.isEmpty())
			return;
		QImageReader leitor(arq);
		leitor.setAutoTransform(true);
		const QImage img = leitor.read();
		if (img.isNull()) {
			QMessageBox::warning(this, "Páginas", "Não deu para abrir a imagem: " + leitor.errorString());
			return;
		}
		static int n = 0;
		const QString nome = QString("lume://imagem/nova-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++n);
		doc()->addResource(QTextDocument::ImageResource, QUrl(nome), img);
		QTextImageFormat f;
		f.setName(nome);
		const double w = std::min<double>(img.width(), larguraDoTexto());
		f.setWidth(w);
		f.setHeight(w * img.height() / std::max(1, img.width()));
		edit_->textCursor().insertImage(f);
	}

	void menuDeContexto(QMenu *m, const QPoint &pos)
	{
		QTextCursor c = edit_->cursorForPosition(pos);
		// Uma imagem logo antes ou depois do clique.
		for (int d : {0, 1}) {
			QTextCursor k(doc());
			k.setPosition(std::max(0, c.position() - d));
			k.setPosition(std::max(0, c.position() - d) + 1, QTextCursor::KeepAnchor);
			const QTextCharFormat f = k.charFormat();
			if (k.selectedText() == QString(QChar::ObjectReplacementCharacter) && f.isImageFormat()) {
				m->addSeparator();
				auto *tm = m->addMenu("Tamanho da imagem");
				for (int p : {25, 50, 75, 100}) {
					tm->addAction(QString("%1% da largura do texto").arg(p), this, [this, k, p] {
						QTextCursor kk = k;
						QTextImageFormat imf = kk.charFormat().toImageFormat();
						QSizeF tam;
						const QImage img = imagemDe(doc(), imf, &tam);
						const double w = larguraDoTexto() * p / 100.0;
						imf.setWidth(w);
						imf.setHeight(img.isNull() ? tam.height() * w / std::max(1.0, tam.width()) : w * img.height() / std::max(1, img.width()));
						kk.setCharFormat(imf);
					});
				}
				break;
			}
		}
		QTextTable *t = edit_->textCursor().currentTable();
		if (!t)
			t = c.currentTable();
		if (!t)
			return;
		QTextCursor alvo = edit_->textCursor().currentTable() ? edit_->textCursor() : c;
		const QTextTableCell cel = t->cellAt(alvo);
		m->addSeparator();
		auto *tm = m->addMenu("Tabela");
		tm->addAction("Linha acima", this, [t, cel] { t->insertRows(cel.row(), 1); });
		tm->addAction("Linha abaixo", this, [t, cel] { t->insertRows(cel.row() + cel.rowSpan(), 1); });
		tm->addAction("Coluna à esquerda", this, [t, cel] { t->insertColumns(cel.column(), 1); });
		tm->addAction("Coluna à direita", this, [t, cel] { t->insertColumns(cel.column() + cel.columnSpan(), 1); });
		tm->addSeparator();
		tm->addAction("Apagar a linha", this, [t, cel] {
			if (t->rows() > 1) t->removeRows(cel.row(), 1);
		});
		tm->addAction("Apagar a coluna", this, [t, cel] {
			if (t->columns() > 1) t->removeColumns(cel.column(), 1);
		});
		tm->addSeparator();
		const QTextCursor sel = edit_->textCursor();
		if (sel.hasComplexSelection() || sel.hasSelection())
			tm->addAction("Mesclar as células", this, [this, t] {
				QTextCursor k = edit_->textCursor();
				t->mergeCells(k);
			});
		if (cel.rowSpan() > 1 || cel.columnSpan() > 1)
			tm->addAction("Separar as células", this, [t, cel] { t->splitCell(cel.row(), cel.column(), 1, 1); });
		tm->addAction("Apagar a tabela", this, [this, t] {
			QTextCursor k = t->firstCursorPosition();
			k.movePosition(QTextCursor::PreviousCharacter);
			k.setPosition(t->lastCursorPosition().position() + 1, QTextCursor::KeepAnchor);
			k.removeSelectedText();
			(void)this;
		});
	}

	// --- arquivos ---
	void depoisDeCarregar()
	{
		pilha_->setCurrentIndex(1);
		barra_->setVisible(true);
		statusBar()->setVisible(true);
		edit_->ajustar();
		lerFolha();
		QTextCursor c(doc());
		edit_->setTextCursor(c);
		area_->verticalScrollBar()->setValue(0);
		titulo();
		atualizarInspetor();
		contar();
		edit_->setFocus();
	}

	void titulo()
	{
		const QString nome = arquivo_.isEmpty() ? QString("Sem título") : QFileInfo(arquivo_).fileName();
		setWindowTitle(nome + "[*] — Páginas");
		titulo_->setText(nome);
		setWindowModified(doc()->isModified());
	}

	void contar()
	{
		if (pilha_->currentIndex() != 1)
			return;
		const QString t = doc()->toPlainText();
		int palavras = 0;
		bool dentro = false;
		for (QChar c : t) {
			const bool letra = c.isLetterOrNumber() || c == '-' || c == '\'';
			if (letra && !dentro)
				palavras++;
			dentro = letra;
		}
		const QLocale br(QLocale::Portuguese, QLocale::Brazil);
		const int n = paginas(doc());
		palavras_->setText(QString("Página %1 de %2   ·   %3 palavras   ·   %4 caracteres")
		                       .arg(std::min(edit_->paginaDoCursor(), n)).arg(n).arg(br.toString(palavras)).arg(br.toString(t.size())));
	}

	void escolherEAbrir()
	{
		if (pilha_->currentIndex() == 1 && !podeFechar())
			return;
		const QString arq = QFileDialog::getOpenFileName(this, "Abrir", pastaPadrao(),
		    "Documentos (*.odt *.docx *.md *.txt *.html *.htm);;OpenDocument (*.odt);;Word (*.docx);;Markdown (*.md);;Texto (*.txt);;Todos os arquivos (*)");
		if (!arq.isEmpty())
			abrir(arq);
	}

	static QString pastaPadrao()
	{
		const QString d = QDir::homePath() + "/Documentos";
		return QDir(d).exists() ? d : QDir::homePath();
	}

	bool salvar()
	{
		const QString ext = extensao(arquivo_);
		if (arquivo_.isEmpty() || (ext != "odt" && ext != "docx"))
			return salvarComo();
		return gravar(arquivo_);
	}

	bool salvarComo()
	{
		QString sugestao = arquivo_.isEmpty() ? pastaPadrao() + "/" + nomeSugerido() + ".odt" : arquivo_;
		if (!arquivo_.isEmpty() && extensao(arquivo_) != "odt" && extensao(arquivo_) != "docx")
			sugestao = QFileInfo(arquivo_).path() + "/" + QFileInfo(arquivo_).completeBaseName() + ".odt";
		QString filtro;
		QString arq = QFileDialog::getSaveFileName(this, "Salvar", sugestao,
		                                           "OpenDocument (*.odt);;Word (*.docx)", &filtro);
		if (arq.isEmpty())
			return false;
		const QString ext = extensao(arq);
		if (ext != "odt" && ext != "docx")
			arq += filtro.contains("docx") ? ".docx" : ".odt";
		if (!gravar(arq))
			return false;
		arquivo_ = arq;
		recente(arq);
		titulo();
		return true;
	}

	QString nomeSugerido()
	{
		QString t = doc()->begin().text().trimmed();
		t.remove(QRegularExpression("[/\\\\:*?\"<>|]"));
		if (t.size() > 60)
			t = t.left(60).trimmed();
		return t.isEmpty() ? QString("Sem título") : t;
	}

	bool gravar(const QString &arq)
	{
		QString erro;
		if (!gravarArquivo(arq, doc(), &erro)) {
			QMessageBox::warning(this, "Páginas", "Não deu para salvar:\n" + erro);
			return false;
		}
		doc()->setModified(false);
		statusBar()->showMessage("Salvo em " + QDir::toNativeSeparators(arq), 4000);
		return true;
	}

	void exportar(const QString &ext)
	{
		const QString base = arquivo_.isEmpty() ? pastaPadrao() + "/" + nomeSugerido()
		                                        : QFileInfo(arquivo_).path() + "/" + QFileInfo(arquivo_).completeBaseName();
		static const QHash<QString, QString> filtros = {{"pdf", "PDF (*.pdf)"}, {"docx", "Word (*.docx)"},
		                                                {"md", "Markdown (*.md)"}, {"txt", "Texto (*.txt)"}};
		QString arq = QFileDialog::getSaveFileName(this, "Exportar", base + "." + ext, filtros.value(ext));
		if (arq.isEmpty())
			return;
		if (extensao(arq) != ext)
			arq += "." + ext;
		QString erro;
		if (!gravarArquivo(arq, doc(), &erro))
			QMessageBox::warning(this, "Páginas", "Não deu para exportar:\n" + erro);
		else
			statusBar()->showMessage("Exportado: " + QDir::toNativeSeparators(arq), 5000);
	}

	bool podeFechar()
	{
		if (!doc()->isModified())
			return true;
		QMessageBox m(this);
		m.setWindowTitle("Páginas");
		m.setText("Salvar as mudanças em \"" + (arquivo_.isEmpty() ? QString("Sem título") : QFileInfo(arquivo_).fileName()) + "\"?");
		m.setInformativeText("Sem salvar, as mudanças se perdem.");
		auto *s = m.addButton("Salvar", QMessageBox::AcceptRole);
		auto *n = m.addButton("Não salvar", QMessageBox::DestructiveRole);
		m.addButton("Cancelar", QMessageBox::RejectRole);
		m.setDefaultButton(s);
		m.exec();
		if (m.clickedButton() == s)
			return salvar();
		return m.clickedButton() == n;
	}

	QStackedWidget *pilha_ = nullptr, *inspetor_ = nullptr;
	QScrollArea *area_ = nullptr;
	Pagina *edit_ = nullptr;
	QToolBar *barra_ = nullptr;
	QToolButton *bFormatar_ = nullptr, *bDocumento_ = nullptr;
	QLabel *titulo_ = nullptr, *palavras_ = nullptr;
	QVBoxLayout *recentes_ = nullptr;
	QWidget *procura_ = nullptr;
	QLineEdit *procuraCampo_ = nullptr;
	QComboBox *estilo_ = nullptr, *tamanho_ = nullptr, *entrelinha_ = nullptr, *lista_ = nullptr;
	QComboBox *papel_ = nullptr, *orientacao_ = nullptr, *esquema_ = nullptr;
	QFontComboBox *fonte_ = nullptr, *fonteCorpo_ = nullptr;
	QToolButton *negrito_ = nullptr, *italico_ = nullptr, *sublinhado_ = nullptr, *riscado_ = nullptr;
	QToolButton *sobrescrito_ = nullptr, *subscrito_ = nullptr;
	QToolButton *alin_[4] = {};
	QDoubleSpinBox *depois_ = nullptr, *recuo_ = nullptr, *recuoEsq_ = nullptr, *tamCorpo_ = nullptr;
	QDoubleSpinBox *margem_[4] = {};
	QCheckBox *numeros_ = nullptr;
	QTimer contagem_;
	QString arquivo_;
	bool atualizando_ = false;
};

// --- a aparência -----------------------------------------------------------

static QString folhaDeEstilo(const Cores &c)
{
	QString s = R"QSS(
* { font-family: "IBM Plex Sans", "Noto Sans", sans-serif; font-size: 13px; color: @tinta; }
QMainWindow, QWidget#galeria, QStackedWidget { background: @papel; }
QScrollArea#mesa, QWidget#mesaDentro { background: @mesa; }
QScrollArea { background: transparent; }
QLabel { background: transparent; }
QLabel#marca { font-family: "IBM Plex Serif", serif; font-size: 30px; }
QLabel#sub, QLabel#vazio { font-family: "IBM Plex Serif", serif; font-style: italic; font-size: 15px; color: @apagado; }
QLabel#parte { font-family: "IBM Plex Serif", serif; font-size: 21px; }
QLabel#rotulo { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: @apagado; letter-spacing: 1px; }
QLabel#rodape { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: @apagado; }
QLabel#tituloDoc { font-family: "IBM Plex Serif", serif; font-size: 15px; }
QFrame#fio { background: @tinta; border: none; min-height: 1px; max-height: 1px; }
QToolBar#barra { background: @papel; border: none; border-bottom: 1px solid @tinta; padding: 4px 10px; spacing: 4px; }
QToolBar#barra QToolButton { font-size: 11px; padding: 3px 8px; border: 1px solid transparent; border-radius: 0; }
QToolBar#barra QToolButton:hover { border: 1px solid @tinta; }
QToolBar#barra QToolButton:checked { background: @tinta; color: @papel; }
QToolBar#barra QToolButton::menu-indicator { image: none; width: 0; }
QToolBar::separator { background: @fio; width: 1px; margin: 6px 6px; }
QStackedWidget#inspetor { background: @cartao; border-left: 1px solid @tinta; }
QStackedWidget#inspetor QScrollArea, QStackedWidget#inspetor QScrollArea > QWidget > QWidget { background: @cartao; }
QToolButton#alterna { background: @cartao; border: 1px solid @tinta; padding: 3px 6px; }
QToolButton#alterna:checked { background: @tinta; color: @cartao; }
QToolButton#alterna:hover { background: @fio; }
QToolButton#alterna:checked:hover { background: @tinta; }
QToolButton#cartaoModelo { border: 1px solid transparent; padding: 8px; font-family: "IBM Plex Serif", serif; font-size: 14px; }
QToolButton#cartaoModelo:hover { border: 1px solid @tinta; background: @cartao; }
QPushButton { background: @cartao; border: 1px solid @tinta; padding: 6px 14px; }
QPushButton:hover { background: @fio; }
QPushButton#botaoPrincipal { background: @tinta; color: @papel; font-weight: 600; }
QPushButton#recente { background: transparent; border: none; text-align: left; padding: 3px 0; font-family: "IBM Plex Serif", serif; font-size: 14px; }
QPushButton#recente:hover { text-decoration: underline; }
QComboBox, QFontComboBox, QLineEdit {
	background: @papel; border: 1px solid @tinta; padding: 3px 6px; min-height: 22px; selection-background-color: @tinta; selection-color: @papel; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: @cartao; border: 1px solid @tinta; selection-background-color: @tinta; selection-color: @cartao; }
QCheckBox { spacing: 8px; }
QWidget#procura { background: @cartao; border-top: 1px solid @tinta; }
QStatusBar { background: @papel; border-top: 1px solid @fio; }
QMenu { background: @cartao; border: 1px solid @tinta; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 12px; }
QMenu::item:selected { background: @tinta; color: @cartao; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: @fio; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
)QSS";
	s.replace("@papel", c.papel.name()).replace("@tinta", c.tinta.name()).replace("@cartao", c.cartao.name())
	 .replace("@mesa", c.mesa.name()).replace("@fio", c.fio.name()).replace("@apagado", c.apagado.name());
	return s;
}

static void aparencia(QApplication &app)
{
	Cores &c = cores_;
	c.noite = app.palette().color(QPalette::Window).lightness() < 128 || qEnvironmentVariable("LUME_TEMA") == "escuro";
	c.papel = c.noite ? QColor(0x1e, 0x1d, 0x1b) : QColor(0xef, 0xea, 0xdf);
	c.tinta = c.noite ? QColor(0xde, 0xda, 0xd0) : QColor(0x1c, 0x1b, 0x19);
	c.cartao = c.noite ? QColor(0x27, 0x26, 0x23) : QColor(0xf6, 0xf2, 0xe9);
	c.mesa = c.noite ? QColor(0x14, 0x13, 0x12) : QColor(0xd9, 0xd3, 0xc5);
	c.folha = QColor(0xff, 0xfd, 0xf8);
	c.fio = c.noite ? QColor(0x45, 0x42, 0x3c) : QColor(0xc4, 0xbd, 0xad);
	c.apagado = c.noite ? QColor(0x9a, 0x94, 0x88) : QColor(0x5d, 0x59, 0x50);
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette pal;
	pal.setColor(QPalette::Window, c.papel);
	pal.setColor(QPalette::Base, c.cartao);
	pal.setColor(QPalette::AlternateBase, c.papel);
	pal.setColor(QPalette::Button, c.papel);
	for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipBase})
		pal.setColor(r, c.tinta);
	pal.setColor(QPalette::ToolTipText, c.papel);
	pal.setColor(QPalette::Highlight, c.tinta);
	pal.setColor(QPalette::HighlightedText, c.papel);
	pal.setColor(QPalette::PlaceholderText, c.apagado);
	app.setPalette(pal);
	app.setStyleSheet(folhaDeEstilo(c));
}

// --- main ------------------------------------------------------------------

static int uso()
{
	std::fprintf(stderr,
	             "uso: lume-paginas [ARQUIVO]\n"
	             "     lume-paginas --converter ENTRADA SAIDA   (odt, docx, pdf, md, txt, html)\n"
	             "     lume-paginas --modelo NOME SAIDA         (abnt, aula, relatorio, carta, curriculo, branco)\n"
	             "     lume-paginas --resumo ARQUIVO\n"
	             "     lume-paginas --foto SAIDA.png [ARQUIVO | --modelo NOME | --galeria]\n");
	return 2;
}

int main(int argc, char **argv)
{
	// Sem janela (testes, terminal): a plataforma "offscreen" do Qt.
	bool semJanela = false;
	for (int i = 1; i < argc; i++) {
		const QByteArray a(argv[i]);
		if (a == "--converter" || a == "--resumo" || a == "--autoteste" || (a == "--modelo" && i == 1))
			semJanela = true;
		if (a == "--foto" && qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
			semJanela = true;
	}
	if (semJanela && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	QTranslator qt;
	if (qt.load(QLocale(), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qt);
	app.setApplicationName("lume-paginas");
	app.setApplicationDisplayName("Páginas");
	app.setDesktopFileName("lume-paginas");
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	                 [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });
	QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));
	const QStringList a = app.arguments();

	if (a.size() >= 4 && a[1] == "--converter") {
		QTextDocument d;
		QString erro;
		if (!abrirArquivo(a[2], &d, &erro) || !gravarArquivo(a[3], &d, &erro)) {
			std::fprintf(stderr, "lume-paginas: %s\n", qPrintable(erro));
			return 1;
		}
		return 0;
	}
	if (a.size() >= 4 && a[1] == "--modelo") {
		QTextDocument d;
		const bool existe = std::any_of(modelos().begin(), modelos().end(), [&](const Modelo &m) { return m.id == a[2]; });
		if (!existe)
			return uso();
		montarModelo(&d, a[2]);
		QString erro;
		if (!gravarArquivo(a[3], &d, &erro)) {
			std::fprintf(stderr, "lume-paginas: %s\n", qPrintable(erro));
			return 1;
		}
		return 0;
	}
	if (a.size() >= 3 && a[1] == "--resumo") {
		QTextDocument d;
		QString erro;
		if (!abrirArquivo(a[2], &d, &erro)) {
			std::fprintf(stderr, "lume-paginas: %s\n", qPrintable(erro));
			return 1;
		}
		std::fputs(resumo(&d).toUtf8().constData(), stdout);
		std::fprintf(stdout, "paginas %d\n", paginas(&d));
		return 0;
	}

	aparencia(app);
	Janela j;
	if (a.size() >= 3 && a[1] == "--autoteste") {
		std::fputs(j.autoteste(a[2]).toUtf8().constData(), stdout);
		return 0;
	}
	if (a.size() >= 3 && a[1] == "--foto") {
		if (a.size() >= 5 && a[3] == "--modelo")
			j.modelo(a[4]);
		else if (a.size() >= 4 && a[3] != "--galeria")
			j.abrir(a[3]);
		j.resize(1366, 860);
		j.show();
		QTimer::singleShot(600, &app, [&] {
			j.grab().save(a[2]);
			app.quit();
		});
		return app.exec();
	}
	if (a.size() >= 2 && !a[1].startsWith("--"))
		j.abrir(a[1]);
	j.show();
	return app.exec();
}

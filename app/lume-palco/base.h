// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: a base. O zip (os .pptx e os .odp são zips, pelo zlib da
// base) e uma árvore pequena para ler XML, os mesmos do Páginas e dos Números.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include <zlib.h>

#include <vector>

inline quint16 le16(const QByteArray &d, qint64 o)
{
	if (o < 0 || o + 2 > d.size()) return 0;
	return quint16(uchar(d[int(o)]) | (uchar(d[int(o + 1)]) << 8));
}
inline quint32 le32(const QByteArray &d, qint64 o)
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

inline QString prefixo(const QStringView uri, const QStringView dado)
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
		{"http://schemas.openxmlformats.org/spreadsheetml/2006/main", "x"},
		{"http://purl.oclc.org/ooxml/spreadsheetml/main", "x"},
		{"http://schemas.openxmlformats.org/drawingml/2006/chart", "c"},
		{"http://purl.oclc.org/ooxml/drawingml/chart", "c"},
		{"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing", "xdr"},
		{"http://purl.oclc.org/ooxml/drawingml/spreadsheetDrawing", "xdr"},
		{"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0", "number"},
		{"urn:oasis:names:tc:opendocument:xmlns:chart:1.0", "chart"},
		{"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0", "presentation"},
		{"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0", "smil"},
		{"urn:oasis:names:tc:opendocument:xmlns:animation:1.0", "anim"},
		{"http://schemas.openxmlformats.org/presentationml/2006/main", "p"},
		{"http://purl.oclc.org/ooxml/presentationml/main", "p"},
		{"http://schemas.openxmlformats.org/markup-compatibility/2006", "mc"},
		{"http://schemas.openxmlformats.org/package/2006/metadata/core-properties", "cpp"},
		{"http://purl.org/dc/terms/", "dcterms"},
	};
	const QString u = uri.toString();
	auto it = m.find(u);
	if (it != m.end())
		return *it;
	return dado.toString();
}

inline bool lerXml(const QByteArray &dados, No &raiz)
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
inline QString esc(const QString &s)
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


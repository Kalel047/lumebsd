// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco: abrir e gravar. .pptx (o padrão), .odp e o PDF (um slide por
// página, do tamanho do palco). Depois de ler, o texto passa por uma
// arrumação: o que é igual ao do objeto ou do tema volta a ser "o do tema"
// (assim, trocar o tema depois ainda repinta o que veio do arquivo).

#pragma once

#include "odp.h"

#include <QDir>
#include <QFile>
#include <QPageSize>
#include <QPdfWriter>

// O tema de um arquivo de fora: as cores e as letras que os slides mais usam.
inline void temaPeloUso(Doc &d)
{
	QHash<QString, int> fundo, corT, corX, fonteT, fonteX;
	for (const Slide &s : d.slides) {
		fundo[fundoDe(d, s).name()]++;
		for (const Objeto &o : s.objs) {
			if (o.tipo != Tipo::Texto)
				continue;
			const bool tit = o.papel == Papel::Titulo;
			for (const Paragrafo &p : o.pars)
				for (const Trecho &t : p.trechos) {
					if (t.texto.trimmed().isEmpty())
						continue;
					if (t.cor.isValid())
						(tit ? corT : corX)[t.cor.name()] += t.texto.size();
					if (!t.fonte.isEmpty())
						(tit ? fonteT : fonteX)[t.fonte] += t.texto.size();
				}
		}
	}
	auto maior = [](const QHash<QString, int> &h) {
		QString r;
		int n = -1;
		for (auto it = h.cbegin(); it != h.cend(); ++it)
			if (it.value() > n || (it.value() == n && it.key() < r)) {
				n = it.value();
				r = it.key();
			}
		return r;
	};
	if (!fundo.isEmpty()) d.tema.fundo = QColor(maior(fundo));
	if (!corX.isEmpty()) d.tema.texto = QColor(maior(corX));
	if (!corT.isEmpty()) d.tema.titulo = QColor(maior(corT));
	else if (!corX.isEmpty()) d.tema.titulo = d.tema.texto;
	if (!fonteX.isEmpty()) d.tema.fonteTexto = maior(fonteX);
	if (!fonteT.isEmpty()) d.tema.fonteTitulo = maior(fonteT);
	d.tema.apagado = QColor((d.tema.texto.red() + d.tema.fundo.red()) / 2, (d.tema.texto.green() + d.tema.fundo.green()) / 2,
	                        (d.tema.texto.blue() + d.tema.fundo.blue()) / 2);
	for (Slide &s : d.slides)
		if (s.fundo == d.tema.fundo)
			s.fundo = QColor();
}

inline void normalizar(Doc &d)
{
	bool nosso = false;
	for (const Tema &t : temas())
		nosso = nosso || (t.id == d.tema.id);
	if (!nosso)
		temaPeloUso(d);
	for (Slide &s : d.slides)
		for (Objeto &o : s.objs) {
			if (!o.aceitaTexto())
				continue;
			// O tamanho do objeto: o do primeiro trecho com texto.
			[&] {
				for (const Paragrafo &p : o.pars)
					for (const Trecho &t : p.trechos)
						if (!t.texto.trimmed().isEmpty() && t.tamanho > 0) {
							o.tamanho = t.tamanho;
							return;
						}
			}();
			Objeto semCor = o;
			semCor.cor = QColor();
			const QColor tema = corTexto(d, semCor);
			const QString fonteTema = o.papel == Papel::Titulo ? d.tema.fonteTitulo : d.tema.fonteTexto;
			bool todosNegrito = true, todosItalico = true, algum = false;
			QString fonte;
			bool fonteIgual = true, corIgual = true;
			QColor corTodos;
			for (Paragrafo &p : o.pars)
				for (Trecho &t : p.trechos) {
					if (t.cor.isValid() && t.cor == tema)
						t.cor = QColor();
					if (std::fabs(t.tamanho - o.tamanho) < 0.01)
						t.tamanho = 0;
					if (t.texto.trimmed().isEmpty())
						continue;
					algum = true;
					todosNegrito = todosNegrito && t.negrito;
					todosItalico = todosItalico && t.italico;
					if (!corTodos.isValid() && corIgual)
						corTodos = t.cor;
					if (t.cor != corTodos)
						corIgual = false;
					if (fonte.isEmpty())
						fonte = t.fonte;
					else if (t.fonte != fonte)
						fonteIgual = false;
				}
			const bool temaNegrito = o.papel == Papel::Titulo && d.tema.tituloNegrito;
			if (algum && fonteIgual && !fonte.isEmpty() && fonte != fonteTema)
				o.fonte = fonte;
			for (Paragrafo &p : o.pars) {
				for (Trecho &t : p.trechos) {
					if (algum && todosNegrito)
						t.negrito = false;
					if (algum && todosItalico)
						t.italico = false;
					if (algum && corIgual && corTodos.isValid() && !o.cor.isValid())
						t.cor = QColor();
					if (t.fonte == fonteTema || (!o.fonte.isEmpty() && t.fonte == o.fonte))
						t.fonte.clear();
				}
				// Junta os trechos seguidos com a mesma letra.
				std::vector<Trecho> j;
				for (const Trecho &t : p.trechos) {
					if (!j.empty()) {
						Trecho &u = j.back();
						if (u.negrito == t.negrito && u.italico == t.italico && u.sublinhado == t.sublinhado && u.cor == t.cor &&
						    std::fabs(u.tamanho - t.tamanho) < 0.01 && u.fonte == t.fonte) {
							u.texto += t.texto;
							continue;
						}
					}
					if (!t.texto.isEmpty())
						j.push_back(t);
				}
				// Um parágrafo vazio guarda o tamanho num trecho vazio.
				if (j.empty() && !p.trechos.empty()) {
					Trecho t;
					t.tamanho = p.trechos.front().tamanho;
					if (std::fabs(t.tamanho - o.tamanho) < 0.01)
						t.tamanho = 0;
					if (t.tamanho > 0)
						j.push_back(t);
				}
				p.trechos = j;
			}
			if (algum && corIgual && corTodos.isValid() && !o.cor.isValid())
				o.cor = corTodos;
			if (algum && todosNegrito && !temaNegrito)
				o.negrito = true;
			if (algum && todosItalico)
				o.italico = true;
		}
	d.limparMidias();
}

inline bool abrirArquivo(const QString &arq, Doc &d, QString *erro)
{
	QFile f(arq);
	if (!f.open(QIODevice::ReadOnly)) {
		*erro = f.errorString();
		return false;
	}
	const QByteArray dados = f.readAll();
	const QString ext = QFileInfo(arq).suffix().toLower();
	bool ok = false;
	if (ext == "odp" || ext == "otp" || (!dados.isEmpty() && ext != "pptx" && dados.mid(30, 47).contains("opendocument"))) {
		LeitorOdp l;
		ok = l.ler(dados, d, erro);
	} else {
		LeitorPptx l;
		ok = l.ler(dados, d, erro);
	}
	if (ok) {
		normalizar(d);
		if (d.titulo.isEmpty() && !d.slides.empty())
			d.titulo = d.slides.front().titulo();
	}
	return ok;
}

inline bool exportarPdf(const Doc &d, const QString &arq, QString *erro)
{
	QPdfWriter pdf(arq);
	pdf.setCreator("LumeBSD Palco");
	pdf.setTitle(d.titulo);
	pdf.setResolution(72);
	pdf.setPageSize(QPageSize(QSizeF(d.larg, d.alt), QPageSize::Point, QString(), QPageSize::ExactMatch));
	pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
	QPainter p;
	if (!p.begin(&pdf)) {
		*erro = "não deu para criar o PDF";
		return false;
	}
	bool primeira = true;
	for (const Slide &s : d.slides) {
		if (s.pular)
			continue;
		if (!primeira)
			pdf.newPage();
		primeira = false;
		desenharSlide(p, d, s, QRectF(0, 0, d.larg, d.alt));
	}
	p.end();
	return true;
}

inline bool gravarArquivo(const QString &arq, const Doc &d, QString *erro)
{
	const QString ext = QFileInfo(arq).suffix().toLower();
	if (ext == "pdf")
		return exportarPdf(d, arq, erro);
	QByteArray dados;
	if (ext == "pptx") {
		EscritorPptx e(d);
		dados = e.gravar();
	} else if (ext == "odp") {
		EscritorOdp e(d);
		dados = e.gravar();
	} else {
		*erro = "formato desconhecido: ." + ext + " (use .pptx, .odp ou .pdf)";
		return false;
	}
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
		*erro = "não deu para trocar o arquivo";
		return false;
	}
	return true;
}

// --- o resumo (para os testes e para quem quer ver o arquivo por dentro) ---

inline QString nomeTipo(const Objeto &o)
{
	switch (o.tipo) {
	case Tipo::Texto: return "texto";
	case Tipo::Retangulo: return "retângulo";
	case Tipo::Arredondado: return "arredondado";
	case Tipo::Elipse: return "elipse";
	case Tipo::Triangulo: return "triângulo";
	case Tipo::Seta: return "seta";
	case Tipo::Linha: return "linha";
	case Tipo::Imagem: return "imagem";
	case Tipo::Grafico: return "gráfico";
	}
	return {};
}

inline QString nomePapel(Papel p)
{
	switch (p) {
	case Papel::Titulo: return "título";
	case Papel::Subtitulo: return "subtítulo";
	case Papel::Corpo: return "tópicos";
	case Papel::Corpo2: return "tópicos 2";
	default: return {};
	}
}

inline QString resumo(const Doc &d)
{
	QString r;
	int imagens = 0, graficos = 0;
	for (const Slide &s : d.slides)
		for (const Objeto &o : s.objs) {
			imagens += o.tipo == Tipo::Imagem;
			graficos += o.tipo == Tipo::Grafico;
		}
	r += QString("palco %1x%2, tema %3, %4 slides, %5 imagens, %6 gráficos\n")
	         .arg(std::lround(d.larg))
	         .arg(std::lround(d.alt))
	         .arg(d.tema.nome)
	         .arg(d.slides.size())
	         .arg(imagens)
	         .arg(graficos);
	auto rr = [](const QRectF &q) {
		return QString("%1,%2 %3x%4").arg(std::lround(q.x())).arg(std::lround(q.y())).arg(std::lround(q.width())).arg(std::lround(q.height()));
	};
	for (size_t i = 0; i < d.slides.size(); i++) {
		const Slide &s = d.slides[i];
		r += QString("slide %1 [%2]").arg(i + 1).arg(nomeLeiaute(s.layout));
		if (s.transicao != Transicao::Nenhuma)
			r += " transição " + nomesTransicao().value(int(s.transicao)).toLower();
		if (s.fundo.isValid())
			r += " fundo " + s.fundo.name();
		if (!s.fundoMidia.isEmpty())
			r += " fundo com imagem";
		if (s.pular)
			r += " (pulado)";
		r += "\n";
		for (const Objeto &o : s.objs) {
			QString l = "  " + nomeTipo(o);
			if (o.papel != Papel::Livre)
				l += " (" + nomePapel(o.papel) + ")";
			l += " " + rr(o.r);
			if (o.giro != 0)
				l += QString(" girado %1°").arg(std::lround(o.giro));
			if (o.forma() && o.preench.isValid())
				l += " " + o.preench.name();
			if (o.borda.isValid() && o.espBorda > 0)
				l += QString(" borda %1 %2pt").arg(o.borda.name()).arg(QString::number(o.espBorda, 'f', 1));
			if (o.tipo == Tipo::Imagem) {
				const QImage img = imagemDe(d, o.midia);
				l += QString(" %1x%2").arg(img.width()).arg(img.height());
			}
			if (o.tipo == Tipo::Grafico) {
				static const char *nt[] = {"colunas", "barras", "linhas", "pizza"};
				double soma = 0;
				for (const auto &v : o.graf.valores)
					for (double x : v)
						soma += x;
				l += QString(" %1 \"%2\" %3 séries x %4 categorias, soma %5")
				         .arg(nt[int(o.graf.tipo)])
				         .arg(o.graf.titulo)
				         .arg(o.graf.series.size())
				         .arg(o.graf.categorias.size())
				         .arg(QString::number(soma, 'f', 2));
			}
			if (o.aceitaTexto() && o.temTexto()) {
				l += QString(" %1pt").arg(QString::number(o.tamanho, 'g', 4));
				if (o.negrito) l += " negrito";
				if (o.italico) l += " itálico";
				if (!o.fonte.isEmpty()) l += " " + o.fonte;
				if (o.cor.isValid()) l += " " + o.cor.name();
				r += l + "\n";
				for (const Paragrafo &p : o.pars) {
					QString pl = "    " + QString(p.nivel * 2, ' ') + (p.numerado ? "1. " : p.marcador ? "• " : "");
					if (p.alin == 1) pl += "[centro] ";
					else if (p.alin == 2) pl += "[direita] ";
					else if (p.alin == 3) pl += "[justificado] ";
					for (const Trecho &t : p.trechos) {
						QString x = QString(t.texto).replace(QChar(0x2028), "↵");
						QStringList m;
						if (t.negrito) m << "n";
						if (t.italico) m << "i";
						if (t.sublinhado) m << "s";
						if (t.cor.isValid()) m << t.cor.name();
						if (t.tamanho > 0) m << QString::number(t.tamanho, 'g', 4) + "pt";
						if (!t.fonte.isEmpty()) m << t.fonte;
						pl += m.isEmpty() ? x : "{" + m.join(',') + ":" + x + "}";
					}
					r += pl + "\n";
				}
			} else
				r += l + "\n";
		}
		if (!s.notas.trimmed().isEmpty())
			r += "  notas: " + QString(s.notas).replace('\n', " / ") + "\n";
	}
	return r;
}

// SPDX-License-Identifier: BSD-2-Clause
//
// lume-palco - "Palco do LumeBSD": as apresentações, com a cara do Keynote
// (o do iWork) e o papel do LumeBSD. Os slides à esquerda, o palco no meio
// (o texto se escreve direto no slide, e nas células das tabelas), as notas
// do apresentador embaixo e o painel à direita para formatar, animar,
// escolher a transição e o tema. Apresenta em tela cheia, com transições,
// as animações um clique de cada vez, os vídeos (Qt Multimedia) e a tela do
// apresentador (o próximo clique, as notas e o relógio) quando há um
// segundo monitor.
//
//   .pptx  PowerPoint (o padrão)    .odp  OpenDocument (o do Impress)
//   .pdf   exportar
//
// Os modelos da galeria são para quem ensina (uma aula), estuda (um
// seminário), lida com economia (a conjuntura, com gráficos) e para o dia a
// dia (uma reunião).
//
// A linha de comando, sem janela (é como os testes conferem):
//
//   lume-palco [ARQUIVO]
//   lume-palco --converter ENTRADA SAIDA   (pptx, odp, pdf)
//   lume-palco --modelo NOME [SAIDA]          (sem SAIDA: abre a janela)
//   lume-palco --resumo ARQUIVO
//   lume-palco --autoteste SAIDA
//   lume-palco --foto SAIDA.png [ARQUIVO | --modelo NOME | --galeria]
//
// Sem moc, como os outros apps do LumeBSD: os sinais do Qt ligados a
// lambdas. As partes: palco.h (o documento), desenho.h (o slide no papel),
// pptx.h e odp.h (os formatos), arquivos.h (abrir, gravar, PDF), modelos.h
// (a galeria).

#include "arquivos.h"
#include "modelos.h"

#include <QAction>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QSessionManager>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleFactory>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>
#include <QUrl>
#include <QVariantAnimation>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWheelEvent>
#include <QWindow>

#include <cstdio>
#include <functional>
#include <map>
#include <memory>

// --- cores -----------------------------------------------------------------

struct Cores {
	bool noite = false;
	QColor papel, tinta, cartao, mesa, fio, apagado, selecao;
};
static Cores cores_;

static QString rotulo(const QString &t) { return t.toUpper(); }

// --- o texto em edição -----------------------------------------------------
//
// O texto se escreve direto no slide: um QGraphicsTextItem com o mesmo
// QTextDocument que desenha o objeto (na escala K), por cima dele.

class EditorTexto : public QGraphicsTextItem
{
public:
	std::function<void()> aoSair, aoMexer;
	std::function<void(int)> aoNivel;

protected:
	void keyPressEvent(QKeyEvent *e) override
	{
		if (e->key() == Qt::Key_Escape) {
			if (aoSair) aoSair();
			return;
		}
		if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) && aoNivel) {
			aoNivel(e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier) ? -1 : 1);
			return;
		}
		QGraphicsTextItem::keyPressEvent(e);
		if (aoMexer) aoMexer();
	}
	void keyReleaseEvent(QKeyEvent *e) override
	{
		QGraphicsTextItem::keyReleaseEvent(e);
		if (aoMexer) aoMexer();
	}
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
	{
		QGraphicsTextItem::mouseReleaseEvent(e);
		if (aoMexer) aoMexer();
	}
};

// O documento do editor de volta para parágrafos.
static std::vector<Paragrafo> lerDocumento(const QTextDocument &td, const Doc &d, const Objeto &o, double escala)
{
	std::vector<Paragrafo> r;
	const QColor corPadrao = corTexto(d, o);
	const QString famPadrao = fonteDe(d, o);
	const bool negritoBase = o.negrito || (o.papel == Papel::Titulo && d.tema.tituloNegrito);
	for (QTextBlock b = td.begin(); b.isValid(); b = b.next()) {
		Paragrafo p;
		const Qt::Alignment al = b.blockFormat().alignment();
		p.alin = (al & Qt::AlignHCenter) ? 1 : (al & Qt::AlignRight) ? 2 : (al & Qt::AlignJustify) ? 3 : 0;
		if (QTextList *l = b.textList()) {
			const QTextListFormat::Style st = l->format().style();
			p.numerado = st == QTextListFormat::ListDecimal || st == QTextListFormat::ListLowerAlpha || st == QTextListFormat::ListUpperAlpha ||
			             st == QTextListFormat::ListLowerRoman || st == QTextListFormat::ListUpperRoman;
			p.marcador = !p.numerado;
			p.nivel = std::clamp(l->format().indent() - 1, 0, 4);
		} else
			p.nivel = std::clamp(b.blockFormat().indent(), 0, 4);
		for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
			const QTextFragment f = it.fragment();
			if (!f.isValid())
				continue;
			const QTextCharFormat cf = f.charFormat();
			Trecho t;
			t.texto = f.text();
			t.negrito = cf.fontWeight() >= QFont::DemiBold && !negritoBase;
			t.italico = cf.fontItalic() && !o.italico;
			t.sublinhado = cf.fontUnderline();
			if (cf.foreground().style() != Qt::NoBrush && cf.foreground().color() != corPadrao)
				t.cor = cf.foreground().color();
			const QFont fn = cf.font();
			const double px = fn.pixelSize() > 0 ? fn.pixelSize() : fn.pointSizeF() * K;
			const double pt = std::round(px / K / escala * 2) / 2;
			if (std::fabs(pt - o.tamanho) > 0.3)
				t.tamanho = pt;
			const QString fam = fn.family();
			if (!fam.isEmpty() && fam != famPadrao)
				t.fonte = fam;
			if (!p.trechos.empty()) {
				Trecho &u = p.trechos.back();
				if (u.negrito == t.negrito && u.italico == t.italico && u.sublinhado == t.sublinhado && u.cor == t.cor && u.tamanho == t.tamanho &&
				    u.fonte == t.fonte) {
					u.texto += t.texto;
					continue;
				}
			}
			p.trechos.push_back(t);
		}
		r.push_back(p);
	}
	return r;
}

// --- o palco na tela -------------------------------------------------------

class Janela;

// O fundo do slide (com a sombra do papel na mesa).
class ItemFundo : public QGraphicsItem
{
public:
	ItemFundo(const Doc *d, int *atual) : d_(d), atual_(atual) {}
	QRectF boundingRect() const override { return QRectF(-8, -8, d_->larg + 20, d_->alt + 20); }
	void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
	{
		const QRectF r(0, 0, d_->larg, d_->alt);
		for (int k = 6; k >= 1; k--)
			p->fillRect(r.translated(k * 0.6, k * 0.9), QColor(0, 0, 0, 7));
		if (*atual_ >= 0 && *atual_ < int(d_->slides.size())) {
			p->save();
			p->setClipRect(r);
			desenharFundo(*p, *d_, d_->slides[size_t(*atual_)]);
			p->restore();
		}
	}

private:
	const Doc *d_;
	int *atual_;
};

class ItemObjeto : public QGraphicsItem
{
public:
	ItemObjeto(const Doc *d, int *atual, int i) : d_(d), atual_(atual), i_(i) {}
	const Objeto &o() const { return d_->slides[size_t(*atual_)].objs[size_t(i_)]; }
	QRectF boundingRect() const override
	{
		const Objeto &x = o();
		QTransform t;
		t.translate(x.r.center().x(), x.r.center().y());
		t.rotate(x.giro);
		t.translate(-x.r.center().x(), -x.r.center().y());
		return t.mapRect(x.r).adjusted(-6 - x.espBorda, -6 - x.espBorda, 6 + x.espBorda, 6 + x.espBorda);
	}
	void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
	{
		p->save();
		p->setClipRect(QRectF(0, 0, d_->larg, d_->alt));
		ComoDesenhar c;
		c.comTexto = !semTexto;
		c.dicas = true;
		c.celulaSemTexto = celula;
		c.quadroVideo = quadro && !quadro->isNull() ? quadro : nullptr;
		desenharObjeto(*p, *d_, o(), c);
		p->restore();
	}
	void mudou()
	{
		prepareGeometryChange();
		update();
	}
	bool semTexto = false;
	int celula = -1;                 // a célula da tabela em edição (sem o texto)
	const QImage *quadro = nullptr;  // o quadro do vídeo tocando no editor

private:
	const Doc *d_;
	int *atual_;
	int i_;
};

// As alças da seleção, a caixa de seleção e as guias.
class ItemSelecao : public QGraphicsItem
{
public:
	explicit ItemSelecao(std::function<double()> escala) : escala_(std::move(escala)) { setZValue(1000); }
	QRectF boundingRect() const override { return QRectF(-4000, -4000, 9000, 9000); }
	void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
	{
		const double k = 1 / std::max(0.01, escala_());
		p->setRenderHint(QPainter::Antialiasing);
		const QColor azul(0x2f, 0x6f, 0xd6);
		for (const QLineF &g : guias) {
			p->setPen(QPen(QColor(0xd6, 0x3a, 0x8f), 1 * k));
			p->drawLine(g);
		}
		for (const Caixa &cx : caixas) {
			const QRectF &r = cx.r;
			const double giro = cx.giro;
			const int linha = cx.linha;
			const bool alcas = cx.alcas;
			p->save();
			p->translate(r.center());
			p->rotate(giro);
			p->translate(-r.center());
			p->setBrush(Qt::NoBrush);
			if (!linha) {
				p->setPen(QPen(azul, 1.2 * k));
				p->drawRect(r);
			}
			const bool girar = cx.girar;
			if (alcas) {
				const double h = 4.5 * k;
				p->setPen(QPen(azul, 1.2 * k));
				p->setBrush(Qt::white);
				for (const QPointF &c : pontos(r, linha))
					p->drawEllipse(c, h, h);
				if (!linha && girar) {
					const QPointF topo(r.center().x(), r.top());
					const QPointF giroP(r.center().x(), r.top() - 22 * k);
					p->drawLine(topo, giroP + QPointF(0, h));
					p->setBrush(azul);
					p->drawEllipse(giroP, h, h);
				}
			}
			p->restore();
		}
		if (!banda.isNull()) {
			p->setPen(QPen(azul, 1 * k));
			p->setBrush(QColor(0x2f, 0x6f, 0xd6, 30));
			p->drawRect(banda);
		}
		// As marcas da ordem das animações (com o painel Animar aberto).
		QFont f("IBM Plex Mono");
		f.setPixelSize(std::max(1, int(std::lround(11 * k))));
		f.setBold(true);
		p->setFont(f);
		const QFontMetricsF fm(f);
		for (const auto &[pt, txt] : marcas) {
			const double w = fm.horizontalAdvance(txt) + 10 * k, h = 17 * k;
			const QRectF c(pt.x() - w / 2, pt.y() - h / 2, w, h);
			p->setPen(Qt::NoPen);
			p->setBrush(QColor(0xd6, 0x3a, 0x8f));
			p->drawRoundedRect(c, h / 2, h / 2);
			p->setPen(Qt::white);
			p->drawText(c, Qt::AlignCenter, txt);
		}
	}
	// As oito alças (ou as duas pontas de uma linha).
	static std::vector<QPointF> pontos(const QRectF &r, int linha)
	{
		if (linha == 1)
			return {r.topLeft(), r.bottomRight()};
		if (linha == 2)
			return {r.bottomLeft(), r.topRight()};
		return {r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(), QPointF(r.right(), r.center().y()), r.bottomRight(),
		        QPointF(r.center().x(), r.bottom()), r.bottomLeft(), QPointF(r.left(), r.center().y())};
	}
	struct Caixa {
		QRectF r;
		double giro;
		int linha; // 0 não; 1 de cima à esquerda para baixo à direita; 2 a outra diagonal
		bool alcas;
		bool girar = true; // a alça de girar (a tabela não gira)
	};
	std::vector<Caixa> caixas;
	std::vector<QLineF> guias;
	std::vector<std::pair<QPointF, QString>> marcas;
	QRectF banda;

private:
	std::function<double()> escala_;
};

class Tela : public QGraphicsView
{
public:
	std::function<bool(QMouseEvent *, QPointF)> aoApertar, aoMover, aoSoltar, aoDuplo;
	std::function<bool(QKeyEvent *)> teclas;
	std::function<void(QPoint, QPointF)> menu;
	std::function<void()> aoRedimensionar;
	std::function<void(int)> aoRolar;
	bool editando = false;

	explicit Tela(QWidget *pai = nullptr) : QGraphicsView(pai)
	{
		setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
		setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
		setTransformationAnchor(QGraphicsView::AnchorViewCenter);
		setResizeAnchor(QGraphicsView::AnchorViewCenter);
		setFrameStyle(QFrame::NoFrame);
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
	}

protected:
	void mousePressEvent(QMouseEvent *e) override
	{
		setFocus();
		if (!(aoApertar && aoApertar(e, mapToScene(e->position().toPoint()))))
			QGraphicsView::mousePressEvent(e);
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!(aoMover && aoMover(e, mapToScene(e->position().toPoint()))))
			QGraphicsView::mouseMoveEvent(e);
	}
	void mouseReleaseEvent(QMouseEvent *e) override
	{
		if (!(aoSoltar && aoSoltar(e, mapToScene(e->position().toPoint()))))
			QGraphicsView::mouseReleaseEvent(e);
	}
	void mouseDoubleClickEvent(QMouseEvent *e) override
	{
		if (!(aoDuplo && aoDuplo(e, mapToScene(e->position().toPoint()))))
			QGraphicsView::mouseDoubleClickEvent(e);
	}
	void keyPressEvent(QKeyEvent *e) override
	{
		if (!editando && teclas && teclas(e))
			return;
		QGraphicsView::keyPressEvent(e);
	}
	void contextMenuEvent(QContextMenuEvent *e) override
	{
		if (menu)
			menu(e->globalPos(), mapToScene(e->pos()));
	}
	void resizeEvent(QResizeEvent *e) override
	{
		QGraphicsView::resizeEvent(e);
		if (aoRedimensionar)
			aoRedimensionar();
	}
	void wheelEvent(QWheelEvent *e) override
	{
		// Sem barra de rolagem (o slide inteiro cabe): a roda troca de slide.
		if (!(e->modifiers() & Qt::ControlModifier) && aoRolar && !verticalScrollBar()->isVisible()) {
			acumulado_ += e->angleDelta().y();
			if (std::abs(acumulado_) >= 120) {
				aoRolar(acumulado_ > 0 ? -1 : 1);
				acumulado_ = 0;
			}
			return;
		}
		QGraphicsView::wheelEvent(e);
	}

private:
	int acumulado_ = 0;
};

// A coluna dos slides: arrastar muda a ordem.
class Navegador : public QListWidget
{
public:
	std::function<void()> aoSoltar;
	std::function<bool(QKeyEvent *)> teclas;

protected:
	void dropEvent(QDropEvent *e) override
	{
		QListWidget::dropEvent(e);
		if (aoSoltar)
			QTimer::singleShot(0, this, [this] { aoSoltar(); });
	}
	void keyPressEvent(QKeyEvent *e) override
	{
		if (teclas && teclas(e))
			return;
		QListWidget::keyPressEvent(e);
	}
};

// --- a apresentação --------------------------------------------------------

// Desenha a passagem de um slide para o outro (p de 0 a 1).
static void pintarTransicao(QPainter &p, const QImage &antes, const QImage &depois, Transicao t, double p0, const QRect &r)
{
	const double q = p0 < 0.5 ? 4 * p0 * p0 * p0 : 1 - std::pow(-2 * p0 + 2, 3) / 2; // ease in-out
	const int w = r.width();
	p.fillRect(r, Qt::black);
	switch (t) {
	case Transicao::Dissolver:
		p.drawImage(r, antes);
		p.setOpacity(q);
		p.drawImage(r, depois);
		break;
	case Transicao::PeloPreto:
		if (p0 < 0.5) {
			p.setOpacity(1 - p0 * 2);
			p.drawImage(r, antes);
		} else {
			p.setOpacity(p0 * 2 - 1);
			p.drawImage(r, depois);
		}
		break;
	case Transicao::Empurrar:
		p.drawImage(r.translated(int(-q * w), 0), antes);
		p.drawImage(r.translated(int((1 - q) * w), 0), depois);
		break;
	case Transicao::Cobrir:
		p.drawImage(r, antes);
		p.drawImage(r.translated(int((1 - q) * w), 0), depois);
		break;
	case Transicao::Descobrir:
		p.drawImage(r, depois);
		p.drawImage(r.translated(int(-q * w), 0), antes);
		break;
	case Transicao::Ampliar: {
		p.drawImage(r, antes);
		const double k = 0.55 + 0.45 * q;
		QRectF z(0, 0, r.width() * k, r.height() * k);
		z.moveCenter(QRectF(r).center());
		p.setOpacity(std::min(1.0, q * 1.6));
		p.drawImage(z, depois);
		break;
	}
	default:
		p.drawImage(r, depois);
	}
	p.setOpacity(1);
}

// Um vídeo tocando (na apresentação ou no editor): o QMediaPlayer lê de um
// arquivo temporário com os bytes do documento e entrega os quadros.
class Tocador : public QObject
{
public:
	Tocador(const QByteArray &dados, const QString &ext, bool mudo, bool repetir, QObject *pai) : QObject(pai)
	{
		arq_.setFileTemplate(QDir::tempPath() + "/lume-palco-XXXXXX." + (ext.isEmpty() ? QString("mp4") : ext));
		if (arq_.open()) {
			arq_.write(dados);
			arq_.flush();
		}
		player_ = new QMediaPlayer(this);
		sink_ = new QVideoSink(this);
		// Sem som, sem a saída de áudio (nem abre o dispositivo).
		if (!mudo) {
			audio_ = new QAudioOutput(this);
			player_->setAudioOutput(audio_);
		}
		player_->setVideoSink(sink_);
		player_->setLoops(repetir ? QMediaPlayer::Infinite : 1);
		connect(sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
			const QImage img = f.toImage();
			if (!img.isNull())
				quadro = img;
			if (aoQuadro)
				aoQuadro();
		});
		connect(player_, &QMediaPlayer::playbackStateChanged, this, [this] {
			if (aoQuadro)
				aoQuadro();
		});
		player_->setSource(QUrl::fromLocalFile(arq_.fileName()));
	}
	void tocar() { player_->play(); }
	void pausar() { player_->pause(); }
	bool tocando() const { return player_->playbackState() == QMediaPlayer::PlayingState; }
	void alternar() { tocando() ? pausar() : tocar(); }
	// O primeiro quadro (para a capa): toca sem som e para no primeiro.
	void primeiroQuadro(std::function<void(const QImage &)> f)
	{
		if (audio_)
			audio_->setMuted(true);
		auto *c = new QMetaObject::Connection;
		*c = connect(sink_, &QVideoSink::videoFrameChanged, this, [this, f, c](const QVideoFrame &v) {
			const QImage img = v.toImage();
			if (img.isNull())
				return;
			disconnect(*c);
			delete c;
			player_->stop();
			f(img);
		});
		player_->play();
	}
	QImage quadro;
	std::function<void()> aoQuadro;

private:
	QTemporaryFile arq_;
	QMediaPlayer *player_;
	QVideoSink *sink_;
	QAudioOutput *audio_ = nullptr;
};

// O que a apresentação e a tela do apresentador dividem. Em cada slide, as
// animações andam em grupos: o grupo 0 roda sozinho quando o slide aparece;
// cada clique roda o seguinte (palco.h, sequencia).
struct Sessao {
	const Doc *doc = nullptr;
	std::vector<int> ordem; // os slides que aparecem
	int pos = 0;
	bool preto = false, branco = false, fim = false;
	QElapsedTimer relogio;
	QString digitos;
	std::function<void(int de)> aoMudar; // de: o slide anterior (-1 sem transição)
	std::function<void()> aoAnimar;      // começou um grupo de animações
	std::function<void()> aoSair;
	bool comTela = true;                 // há a tela da apresentação (senão, só o apresentador)
	// O grupo de animações do slide e o tempo dele.
	int grupo = 0;
	bool esperando = true;               // o grupo ainda não começou (a transição está passando)
	bool rodando = false;                // false: o grupo já terminou
	QElapsedTimer tGrupo;

	int slide() const { return ordem.empty() ? -1 : ordem[size_t(std::clamp(pos, 0, int(ordem.size()) - 1))]; }
	int seguinte() const { return pos + 1 < int(ordem.size()) ? ordem[size_t(pos + 1)] : -1; }
	std::vector<std::vector<Evento>> grupos() const
	{
		const int a = slide();
		return a < 0 ? std::vector<std::vector<Evento>>(1) : sequencia(doc->slides[size_t(a)]);
	}
	double tempo() const
	{
		if (esperando)
			return -1;
		return rodando ? tGrupo.elapsed() / 1000.0 : 1e9;
	}
	bool animando() const
	{
		if (esperando || !rodando)
			return false;
		const auto g = grupos();
		return grupo < int(g.size()) && tempo() < duracaoGrupo(g[size_t(grupo)]) + 0.05;
	}
	std::vector<EstadoObj> estado() const
	{
		const int a = slide();
		return a < 0 ? std::vector<EstadoObj>() : estadoDoSlide(doc->slides[size_t(a)], grupo, tempo());
	}
	// O estado depois de tudo (para a imagem de um slide que sai).
	std::vector<EstadoObj> estadoFinal(int i) const
	{
		const Slide &s = doc->slides[size_t(i)];
		return estadoDoSlide(s, int(sequencia(s).size()) - 1, 1e9);
	}
	void comecarGrupo()
	{
		esperando = false;
		rodando = true;
		tGrupo.start();
		if (aoAnimar)
			aoAnimar();
	}
	void ir(int nova, bool comTransicao)
	{
		preto = branco = false;
		if (nova >= int(ordem.size())) {
			if (!fim) {
				fim = true;
				if (aoMudar) aoMudar(-1);
			}
			return;
		}
		nova = std::max(0, nova);
		const bool voltou = !fim && nova == pos - 1;
		if (fim) {
			fim = false;
			pos = nova;
			grupo = int(grupos().size()) - 1;
			esperando = rodando = false;
			if (aoMudar) aoMudar(-1);
			return;
		}
		if (nova == pos)
			return;
		const int de = slide();
		const bool avancou = nova == pos + 1;
		pos = nova;
		if (voltou) {
			// Voltando, o slide aparece como terminou.
			grupo = int(grupos().size()) - 1;
			esperando = rodando = false;
		} else {
			grupo = 0;
			esperando = true;
			rodando = false;
		}
		if (aoMudar) aoMudar(comTransicao && avancou ? de : -1);
	}
	// O próximo passo: o próximo grupo de animações ou o próximo slide.
	void avancar()
	{
		if (!fim && !preto && !branco && !esperando && grupo + 1 < int(grupos().size())) {
			grupo++;
			comecarGrupo();
			return;
		}
		ir(pos + 1, true);
	}
	void voltar()
	{
		if (!fim && grupo > 0) {
			grupo--;
			esperando = rodando = false;
			if (aoMudar) aoMudar(-1);
			return;
		}
		ir(fim ? pos : pos - 1, false);
	}
	bool tecla(QKeyEvent *e)
	{
		const int k = e->key();
		if (k >= Qt::Key_0 && k <= Qt::Key_9) {
			digitos += QChar('0' + (k - Qt::Key_0));
			return true;
		}
		if ((k == Qt::Key_Return || k == Qt::Key_Enter) && !digitos.isEmpty()) {
			// O número do slide no documento.
			const int n = digitos.toInt() - 1;
			digitos.clear();
			for (size_t i = 0; i < ordem.size(); i++)
				if (ordem[i] == n)
					ir(int(i), false);
			return true;
		}
		digitos.clear();
		switch (k) {
		case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: case Qt::Key_PageDown: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_N:
			avancar();
			return true;
		case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_Backspace: case Qt::Key_P:
			voltar();
			return true;
		case Qt::Key_Home:
			ir(0, false);
			return true;
		case Qt::Key_End:
			ir(int(ordem.size()) - 1, false);
			return true;
		case Qt::Key_B: case Qt::Key_Period:
			preto = !preto;
			branco = false;
			if (aoMudar) aoMudar(-1);
			return true;
		case Qt::Key_W: case Qt::Key_Comma:
			branco = !branco;
			preto = false;
			if (aoMudar) aoMudar(-1);
			return true;
		case Qt::Key_Escape: case Qt::Key_Q:
			if (aoSair) aoSair();
			return true;
		default:
			return false;
		}
	}
};

class Apresentacao : public QWidget
{
public:
	explicit Apresentacao(Sessao *s) : s_(s)
	{
		setAttribute(Qt::WA_OpaquePaintEvent);
		setMouseTracking(true);
		setCursor(Qt::BlankCursor);
		setWindowTitle("Palco — apresentação");
		anim_ = new QVariantAnimation(this);
		anim_->setStartValue(0.0);
		anim_->setEndValue(1.0);
		connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
			progresso_ = v.toDouble();
			update();
		});
		connect(anim_, &QVariantAnimation::finished, this, [this] {
			de_ = -1;
			comecarSlide();
			update();
		});
		cursor_ = new QTimer(this);
		cursor_->setSingleShot(true);
		connect(cursor_, &QTimer::timeout, this, [this] { setCursor(Qt::BlankCursor); });
		// Os quadros das animações e dos vídeos.
		quadros_ = new QTimer(this);
		quadros_->setInterval(16);
		connect(quadros_, &QTimer::timeout, this, [this] {
			update();
			if (!s_->animando() && !algumTocando())
				quadros_->stop();
		});
	}
	void mudou(int de)
	{
		anim_->stop();
		de_ = de;
		const int a = s_->slide();
		prepararVideos();
		if (de >= 0 && a >= 0) {
			const Slide &sl = s_->doc->slides[size_t(a)];
			if (sl.transicao != Transicao::Nenhuma) {
				const auto fim = s_->estadoFinal(de);
				antes_ = imagem(de, &fim, -1);
				anim_->setDuration(std::clamp(int(sl.duracao * 1000), 150, 4000));
				progresso_ = 0;
				anim_->start();
			} else {
				de_ = -1;
				comecarSlide();
			}
		} else {
			de_ = -1;
			if (s_->esperando)
				comecarSlide();
		}
		// Já prepara o seguinte.
		if (s_->seguinte() >= 0) {
			const auto e0 = estadoInicial(s_->seguinte());
			imagem(s_->seguinte(), &e0, 0);
		}
		update();
	}
	// Um grupo de animações começou: os quadros andam.
	void animou()
	{
		quadros_->start();
		update();
	}
	// Os vídeos que começam sozinhos (para as fotos, que não passam pelo começo do slide).
	void tocarSozinhos()
	{
		const int a = s_->slide();
		if (a < 0)
			return;
		for (auto &[k, t] : videos_)
			if (s_->doc->slides[size_t(a)].objs[size_t(k)].sozinho)
				t->tocar();
		quadros_->start();
	}

protected:
	std::vector<EstadoObj> estadoInicial(int i) const { return estadoDoSlide(s_->doc->slides[size_t(i)], 0, -1); }
	// A imagem de um slide num estado (com cache por slide e grupo).
	QImage imagem(int i, const std::vector<EstadoObj> *estado, int grupoChave)
	{
		const QSize t = size() * devicePixelRatioF();
		const QString chave = QString("%1|%2|%3x%4").arg(i).arg(grupoChave).arg(t.width()).arg(t.height());
		auto it = cache_.constFind(chave);
		if (it != cache_.cend())
			return *it;
		const Doc &d = *s_->doc;
		const double k = std::min(t.width() / d.larg, t.height() / d.alt);
		QImage img = imagemSlide(d, d.slides[size_t(i)], std::max(1, int(d.larg * k)), false, estado);
		if (cache_.size() > 24)
			cache_.clear();
		cache_.insert(chave, img);
		return img;
	}
	void comecarSlide()
	{
		if (s_->esperando)
			s_->comecarGrupo();
		// Os vídeos que começam sozinhos.
		const int a = s_->slide();
		if (a >= 0 && !s_->fim)
			for (auto &[k, t] : videos_)
				if (s_->doc->slides[size_t(a)].objs[size_t(k)].sozinho)
					t->tocar();
		if (s_->animando() || algumTocando())
			quadros_->start();
	}
	void prepararVideos()
	{
		const int a = s_->slide();
		if (a == videosDe_)
			return;
		videos_.clear();
		videosDe_ = a;
		if (a < 0)
			return;
		const Slide &sl = s_->doc->slides[size_t(a)];
		for (size_t k = 0; k < sl.objs.size(); k++) {
			const Objeto &o = sl.objs[k];
			if (o.tipo != Tipo::Video || !s_->doc->midias.contains(o.midia))
				continue;
			auto t = std::make_unique<Tocador>(s_->doc->midias.value(o.midia), QFileInfo(o.midia).suffix(), o.mudo, o.repetir, this);
			t->aoQuadro = [this] {
				if (!quadros_->isActive())
					quadros_->start();
			};
			videos_[int(k)] = std::move(t);
		}
	}
	bool algumTocando() const
	{
		for (const auto &[k, t] : videos_)
			if (t->tocando())
				return true;
		return false;
	}
	QRect alvo() const
	{
		const Doc &d = *s_->doc;
		const double k = std::min(width() / d.larg, height() / d.alt);
		QRect r(0, 0, int(d.larg * k), int(d.alt * k));
		r.moveCenter(rect().center());
		return r;
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.fillRect(rect(), Qt::black);
		if (s_->branco) {
			p.fillRect(rect(), Qt::white);
			return;
		}
		if (s_->preto)
			return;
		if (s_->fim) {
			p.setPen(QColor(0x9a, 0x94, 0x88));
			QFont f("IBM Plex Sans");
			f.setPixelSize(std::max(14, height() / 40));
			p.setFont(f);
			p.drawText(rect(), Qt::AlignCenter, "Fim da apresentação. Esc para sair.");
			return;
		}
		const int a = s_->slide();
		if (a < 0)
			return;
		const Doc &d = *s_->doc;
		const QRect r = alvo();
		const auto estado = s_->estado();
		if (de_ >= 0 && anim_->state() == QAbstractAnimation::Running) {
			pintarTransicao(p, antes_, imagem(a, &estado, -1 - s_->grupo), d.slides[size_t(a)].transicao, progresso_, r);
		} else if (s_->animando() || !videos_.empty()) {
			// Ao vivo: as animações andando ou os vídeos.
			QHash<int, QImage> qs;
			for (const auto &[k, t] : videos_)
				if (!t->quadro.isNull())
					qs.insert(k, t->quadro);
			p.setRenderHint(QPainter::Antialiasing);
			p.setRenderHint(QPainter::TextAntialiasing);
			desenharSlide(p, d, d.slides[size_t(a)], r, false, &estado, &qs);
		} else
			p.drawImage(r, imagem(a, &estado, s_->esperando ? -1000 : s_->grupo));
		if (!s_->digitos.isEmpty()) {
			QFont f("IBM Plex Mono");
			f.setPixelSize(std::max(16, height() / 24));
			p.setFont(f);
			const QRect c(width() - 220, height() - 90, 200, 70);
			p.fillRect(c, QColor(0, 0, 0, 170));
			p.setPen(Qt::white);
			p.drawText(c, Qt::AlignCenter, "slide " + s_->digitos);
		}
	}
	void keyPressEvent(QKeyEvent *e) override
	{
		if (!s_->tecla(e))
			QWidget::keyPressEvent(e);
		update();
	}
	// O vídeo sob o mouse (o número do objeto) ou -1.
	int videoEm(QPoint pt) const
	{
		const int a = s_->slide();
		if (a < 0 || s_->fim)
			return -1;
		const Doc &d = *s_->doc;
		const QRect r = alvo();
		const double k = r.width() / d.larg;
		const QPointF ps((pt.x() - r.left()) / k, (pt.y() - r.top()) / k);
		for (const auto &[i, t] : videos_)
			if (d.slides[size_t(a)].objs[size_t(i)].r.contains(ps))
				return i;
		return -1;
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton) {
			// Um clique no vídeo toca (ou pausa); fora dele, avança.
			const int v = videoEm(e->position().toPoint());
			if (v >= 0) {
				videos_[v]->alternar();
				quadros_->start();
				return;
			}
			s_->avancar();
		} else if (e->button() == Qt::RightButton)
			s_->voltar();
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		setCursor(videoEm(e->position().toPoint()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		cursor_->start(1500);
	}
	void wheelEvent(QWheelEvent *e) override
	{
		acc_ += e->angleDelta().y();
		if (std::abs(acc_) >= 120) {
			if (acc_ < 0)
				s_->avancar();
			else
				s_->voltar();
			acc_ = 0;
		}
	}
	void resizeEvent(QResizeEvent *) override { cache_.clear(); }

private:
	Sessao *s_;
	QVariantAnimation *anim_;
	QTimer *cursor_, *quadros_;
	QHash<QString, QImage> cache_;
	QImage antes_;
	int de_ = -1;
	double progresso_ = 1;
	int acc_ = 0;
	std::map<int, std::unique_ptr<Tocador>> videos_;
	int videosDe_ = -2;
};

// A tela do apresentador: o slide, o que vem a seguir, as notas e o tempo.
class Apresentador : public QWidget
{
public:
	explicit Apresentador(Sessao *s) : s_(s)
	{
		setAttribute(Qt::WA_OpaquePaintEvent);
		setWindowTitle("Palco — apresentador");
		auto *t = new QTimer(this);
		connect(t, &QTimer::timeout, this, [this] { update(); });
		t->start(500);
		quadros_ = new QTimer(this);
		quadros_->setInterval(33);
		connect(quadros_, &QTimer::timeout, this, [this] {
			update();
			if (!s_->animando())
				quadros_->stop();
		});
	}
	void mudou()
	{
		// Sem a tela da apresentação, quem começa as animações é este.
		if (!s_->comTela && s_->esperando && !s_->fim)
			s_->comecarGrupo();
		update();
	}
	void animou()
	{
		quadros_->start();
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const QColor fundo(0x1e, 0x1d, 0x1b), tinta(0xf2, 0xef, 0xe8), apagado(0x9a, 0x94, 0x88), destaque(0xe0, 0xa4, 0x58);
		p.fillRect(rect(), fundo);
		const Doc &d = *s_->doc;
		const int W = width(), H = height();
		const int m = std::max(16, W / 60);
		auto fonte = [](const char *familia, int px, bool negrito = false) {
			QFont f(familia);
			f.setPixelSize(std::max(10, px));
			f.setBold(negrito);
			return f;
		};
		// O tempo e o relógio, em cima.
		const qint64 seg = s_->relogio.isValid() ? s_->relogio.elapsed() / 1000 : 0;
		p.setFont(fonte("IBM Plex Mono", H / 18, true));
		p.setPen(destaque);
		const QRect topo(m, m / 2, W - 2 * m, H / 12);
		p.drawText(topo, Qt::AlignLeft | Qt::AlignVCenter, QString("%1:%2:%3").arg(seg / 3600, 2, 10, QChar('0')).arg(seg / 60 % 60, 2, 10, QChar('0')).arg(seg % 60, 2, 10, QChar('0')));
		p.setPen(tinta);
		p.drawText(topo, Qt::AlignRight | Qt::AlignVCenter, QTime::currentTime().toString("HH:mm"));
		p.setFont(fonte("IBM Plex Sans", H / 34));
		p.setPen(apagado);
		const int a = s_->slide();
		const int nGrupos = int(s_->grupos().size());
		const int faltam = s_->fim ? 0 : nGrupos - 1 - s_->grupo;
		QString cont = s_->fim ? "fim" : QString("slide %1 de %2").arg(s_->pos + 1).arg(s_->ordem.size());
		if (faltam > 0) cont += faltam == 1 ? " · falta 1 clique neste slide" : QString(" · faltam %1 cliques neste slide").arg(faltam);
		if (s_->preto) cont += " · tela preta (B)";
		if (s_->branco) cont += " · tela branca (W)";
		p.drawText(topo, Qt::AlignHCenter | Qt::AlignVCenter, cont);
		// O slide atual (grande) e o que vem a seguir (menor): o próximo
		// clique deste slide ou o slide seguinte.
		const int y0 = topo.bottom() + m / 2;
		const int largA = int((W - 3 * m) * 0.62), largB = W - 3 * m - largA;
		const QRect ra(m, y0, largA, int(largA * d.alt / d.larg));
		const QRect rb(2 * m + largA, y0 + H / 30, largB, int(largB * d.alt / d.larg));
		auto mostra = [&](int i, const QRect &r, const std::vector<EstadoObj> *e) {
			if (i < 0) {
				p.setPen(QPen(apagado, 1, Qt::DashLine));
				p.drawRect(r);
				p.drawText(r, Qt::AlignCenter, "fim");
				return;
			}
			p.drawImage(r, imagemSlide(d, d.slides[size_t(i)], r.width() * 2, false, e));
		};
		const auto agora = s_->estado();
		mostra(s_->fim ? -1 : a, ra, &agora);
		p.setFont(fonte("IBM Plex Sans", H / 40));
		p.setPen(apagado);
		p.drawText(QRect(rb.left(), y0, rb.width(), H / 30), Qt::AlignLeft | Qt::AlignVCenter, faltam > 0 ? "NO PRÓXIMO CLIQUE" : "A SEGUIR");
		if (faltam > 0) {
			const auto prox = estadoDoSlide(d.slides[size_t(a)], s_->grupo + 1, 1e9);
			mostra(a, rb, &prox);
		} else {
			const int seg2 = s_->fim ? -1 : s_->seguinte();
			std::vector<EstadoObj> e0;
			if (seg2 >= 0)
				e0 = estadoDoSlide(d.slides[size_t(seg2)], 0, 1e9);
			mostra(seg2, rb, &e0);
		}
		// As notas.
		const QRect rn(rb.left(), rb.bottom() + m, rb.width(), H - rb.bottom() - 2 * m);
		const QString notas = a >= 0 && !s_->fim ? d.slides[size_t(a)].notas : QString();
		QTextDocument td;
		td.setDefaultFont(fonte("IBM Plex Sans", H / 26));
		td.setTextWidth(rn.width());
		td.setPlainText(notas.isEmpty() ? QString("Sem notas neste slide.") : notas);
		p.save();
		p.translate(rn.topLeft());
		p.setClipRect(QRect(0, 0, rn.width(), rn.height()));
		QAbstractTextDocumentLayout::PaintContext ctx;
		ctx.palette.setColor(QPalette::Text, notas.isEmpty() ? apagado : tinta);
		td.documentLayout()->draw(&p, ctx);
		p.restore();
		p.setFont(fonte("IBM Plex Sans", H / 48));
		p.setPen(apagado);
		p.drawText(QRect(m, H - m - H / 30, ra.width(), H / 30), Qt::AlignLeft | Qt::AlignVCenter,
		           "→ espaço: avança   ← : volta   número + Enter: vai ao slide   B: tela preta   Esc: sai");
	}
	void keyPressEvent(QKeyEvent *e) override
	{
		if (!s_->tecla(e))
			QWidget::keyPressEvent(e);
		update();
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton)
			s_->avancar();
	}

private:
	Sessao *s_;
	QTimer *quadros_;
};

// --- a janela --------------------------------------------------------------

class Janela : public QMainWindow
{
public:
	Janela()
	{
		setWindowTitle("Palco");
		setAcceptDrops(true);
		pilha_ = new QStackedWidget;
		setCentralWidget(pilha_);
		montarGaleria();
		montarEditor();
		montarBarra();
		montarInspetor();
		mostrarGaleria();
		miniaturas_ = new QTimer(this);
		miniaturas_->setSingleShot(true);
		connect(miniaturas_, &QTimer::timeout, this, [this] { atualizarMiniatura(atual_); });
	}

	// --- abrir e salvar ---
	bool abrir(const QString &arq)
	{
		Doc d;
		QString erro;
		if (!abrirArquivo(arq, d, &erro)) {
			QMessageBox::warning(this, "Palco", "Não deu para abrir \"" + QFileInfo(arq).fileName() + "\":\n" + erro);
			return false;
		}
		arquivo_ = QFileInfo(arq).absoluteFilePath();
		recente(arquivo_);
		carregar(d);
		return true;
	}

	void modelo(const QString &id)
	{
		Doc d;
		montarModelo(d, id);
		arquivo_.clear();
		carregar(d);
	}

	void comTema(const QString &id)
	{
		Doc d;
		montarModelo(d, "branco");
		d.tema = tema(id);
		arquivo_.clear();
		carregar(d);
	}

	QString autoteste(const QString &saida);
	void irParaSlide(int i) { mostrarSlide(std::clamp(i, 0, int(doc_.slides.size()) - 1)); }
	void escolherObjeto(int i)
	{
		sel_.clear();
		if (i >= 0 && i < int(slide().objs.size()))
			sel_.push_back(i);
		atualizarSelecao();
		atualizarInspetor();
	}
	void mostrarPainel(int i) { mostrarInspetor(i); }
	void apresentarAgora(bool ensaio) { apresentar(atual_, ensaio); }
	Sessao *sessaoParaFoto(bool apresentador, QWidget *&w)
	{
		sessao_ = std::make_unique<Sessao>();
		sessao_->doc = &doc_;
		for (size_t i = 0; i < doc_.slides.size(); i++)
			if (!doc_.slides[i].pular)
				sessao_->ordem.push_back(int(i));
		sessao_->relogio.start();
		for (size_t i = 0; i < sessao_->ordem.size(); i++)
			if (sessao_->ordem[i] == atual_)
				sessao_->pos = int(i);
		// O slide terminado (ou depois de LUME_FOTO_PASSO cliques).
		const int grupos = int(sessao_->grupos().size());
		sessao_->grupo = qEnvironmentVariableIsSet("LUME_FOTO_PASSO") ? std::clamp(qEnvironmentVariableIntValue("LUME_FOTO_PASSO"), 0, grupos - 1)
		                                                                : grupos - 1;
		sessao_->esperando = sessao_->rodando = false;
		sessao_->comTela = !apresentador;
		if (apresentador)
			w = new Apresentador(sessao_.get());
		else {
			auto *ap = new Apresentacao(sessao_.get());
			ap->mudou(-1);
			ap->tocarSozinhos();
			w = ap;
		}
		return sessao_.get();
	}

protected:
	void closeEvent(QCloseEvent *e) override
	{
		terminarEdicao();
		if (pilha_->currentIndex() == 1 && !podeFechar()) {
			e->ignore();
			return;
		}
		e->accept();
	}
	void dragEnterEvent(QDragEnterEvent *e) override
	{
		if (e->mimeData()->hasUrls() || e->mimeData()->hasImage())
			e->acceptProposedAction();
	}
	void dropEvent(QDropEvent *e) override
	{
		for (const QUrl &u : e->mimeData()->urls()) {
			const QString f = u.toLocalFile();
			const QString ext = QFileInfo(f).suffix().toLower();
			if (ext == "pptx" || ext == "odp") {
				if (pilha_->currentIndex() == 0 || podeFechar())
					abrir(f);
				return;
			}
			if (pilha_->currentIndex() == 1) {
				if (QStringList{"mp4", "m4v", "mov", "webm", "mkv", "avi", "ogv", "mpg", "mpeg", "wmv"}.contains(ext))
					inserirVideo(f);
				else
					inserirImagem(f);
			}
		}
		if (e->mimeData()->hasImage() && pilha_->currentIndex() == 1)
			inserirImagemDados(qvariant_cast<QImage>(e->mimeData()->imageData()));
	}

private:
	// --- galeria ---
	void montarGaleria()
	{
		auto *g = new QWidget;
		g->setObjectName("galeria");
		auto *v = new QVBoxLayout(g);
		v->setContentsMargins(56, 36, 56, 32);
		auto *t = new QLabel("Escolha um tema");
		t->setObjectName("marca");
		auto *s = new QLabel("Para começar uma apresentação nova. Todas abrem e salvam em .pptx e .odp.");
		s->setObjectName("sub");
		v->addWidget(t);
		v->addWidget(s);
		v->addSpacing(14);
		auto cartao = [this](const QString &nome, const QString &dica, const QImage &img, std::function<void()> f) {
			auto *b = new QToolButton;
			b->setObjectName("cartaoModelo");
			b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
			QImage moldura = img;
			{
				QPainter p(&moldura);
				p.setPen(QPen(QColor(0x1c, 0x1b, 0x19), 3));
				p.drawRect(QRectF(moldura.rect()).adjusted(1.5, 1.5, -1.5, -1.5));
			}
			b->setIcon(QIcon(QPixmap::fromImage(moldura)));
			b->setIconSize(QSize(208, 117));
			b->setText(nome);
			b->setToolTip(dica);
			b->setCursor(Qt::PointingHandCursor);
			connect(b, &QToolButton::clicked, this, [f] { f(); });
			return b;
		};
		auto *gt = new QGridLayout;
		gt->setHorizontalSpacing(22);
		gt->setVerticalSpacing(14);
		int i = 0;
		for (const Tema &tm : temas()) {
			Doc d;
			montarModelo(d, "branco");
			d.tema = tm;
			for (Objeto &o : d.slides[0].objs)
				Roteiro::texto(o, o.papel == Papel::Titulo ? tm.nome : "Uma apresentação nova");
			const QString id = tm.id;
			gt->addWidget(cartao(tm.nome, "Um slide de título no tema " + tm.nome, imagemSlide(d, d.slides[0], 416), [this, id] { comTema(id); }), 0, i++,
			              Qt::AlignTop | Qt::AlignLeft);
		}
		gt->setColumnStretch(i, 1);
		v->addLayout(gt);
		v->addSpacing(18);
		auto *t2 = new QLabel("Ou um modelo pronto");
		t2->setObjectName("parte");
		v->addWidget(t2);
		v->addSpacing(8);
		auto *gm = new QGridLayout;
		gm->setHorizontalSpacing(22);
		gm->setVerticalSpacing(14);
		i = 0;
		for (const Modelo &m : modelos()) {
			if (m.id == "branco")
				continue;
			Doc d;
			montarModelo(d, m.id);
			const QString id = m.id;
			// Um slide que mostra o modelo (o título já está no nome do cartão).
			const size_t vitrine = std::min(d.slides.size() - 1, size_t(m.id == "aula" ? 3 : 2));
			gm->addWidget(cartao(m.nome, m.descricao, imagemSlide(d, d.slides[vitrine], 416), [this, id] { modelo(id); }), 0, i++,
			              Qt::AlignTop | Qt::AlignLeft);
		}
		gm->setColumnStretch(i, 1);
		v->addLayout(gm);
		v->addSpacing(20);
		auto *linha = new QHBoxLayout;
		auto *ab = new QPushButton("Abrir uma apresentação…");
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

	void atualizarRecentes()
	{
		while (QLayoutItem *it = recentes_->takeAt(0)) {
			delete it->widget();
			delete it;
		}
		const QStringList l = QSettings("LumeBSD", "lume-palco").value("recentes").toStringList();
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
			auto *v = new QLabel("As apresentações que você abrir aparecem aqui.");
			v->setObjectName("vazio");
			recentes_->addWidget(v);
		}
	}

	void recente(const QString &a)
	{
		QSettings s("LumeBSD", "lume-palco");
		QStringList l = s.value("recentes").toStringList();
		l.removeAll(a);
		l.prepend(a);
		while (l.size() > 12)
			l.removeLast();
		s.setValue("recentes", l);
	}

	void mostrarGaleria()
	{
		atualizarRecentes();
		pilha_->setCurrentIndex(0);
		barra_->hide();
		setWindowTitle("Palco");
	}

	// --- editor ---
	void montarEditor()
	{
		auto *raiz = new QWidget;
		auto *h = new QHBoxLayout(raiz);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(0);
		nav_ = new Navegador;
		nav_->setObjectName("navegador");
		nav_->setViewMode(QListView::ListMode);
		nav_->setIconSize(QSize(160, 90));
		nav_->setFixedWidth(214);
		nav_->setDragDropMode(QAbstractItemView::InternalMove);
		nav_->setDefaultDropAction(Qt::MoveAction);
		nav_->setSelectionMode(QAbstractItemView::ExtendedSelection);
		nav_->setSpacing(4);
		nav_->setContextMenuPolicy(Qt::CustomContextMenu);
		nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		h->addWidget(nav_);
		auto *meio = new QSplitter(Qt::Vertical);
		meio->setObjectName("meio");
		meio->setChildrenCollapsible(false);
		cena_ = new QGraphicsScene(this);
		tela_ = new Tela;
		tela_->setObjectName("tela");
		tela_->setScene(cena_);
		tela_->setBackgroundBrush(cores_.mesa);
		meio->addWidget(tela_);
		auto *nw = new QWidget;
		nw->setObjectName("faixaNotas");
		auto *nv = new QVBoxLayout(nw);
		nv->setContentsMargins(14, 6, 14, 8);
		nv->setSpacing(2);
		auto *nl = new QLabel(rotulo("Notas do apresentador"));
		nl->setObjectName("rotulo");
		nv->addWidget(nl);
		notas_ = new QPlainTextEdit;
		notas_->setObjectName("notas");
		notas_->setPlaceholderText("O que você quer lembrar de falar neste slide (aparece só na sua tela)");
		nv->addWidget(notas_);
		meio->addWidget(nw);
		meio->setStretchFactor(0, 5);
		meio->setStretchFactor(1, 1);
		meio->setSizes({700, 84});
		h->addWidget(meio, 1);
		inspetor_ = new QStackedWidget;
		inspetor_->setObjectName("inspetor");
		inspetor_->setFixedWidth(300);
		h->addWidget(inspetor_);
		pilha_->addWidget(raiz);

		auto *selecao = new ItemSelecao([this] { return tela_->transform().m11(); });
		selecao_ = selecao;

		connect(nav_, &QListWidget::currentRowChanged, this, [this](int r) {
			if (!ajustando_ && r >= 0 && r < int(doc_.slides.size()) && r != atual_)
				mostrarSlide(r);
		});
		nav_->aoSoltar = [this] { reordenarPeloNavegador(); };
		nav_->teclas = [this](QKeyEvent *e) { return teclaNavegador(e); };
		connect(nav_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) { menuNavegador(nav_->viewport()->mapToGlobal(p)); });
		connect(notas_, &QPlainTextEdit::textChanged, this, [this] {
			if (ajustando_ || atual_ < 0)
				return;
			slide().notas = notas_->toPlainText();
			marcarMudanca(false);
		});
		tela_->aoApertar = [this](QMouseEvent *e, QPointF p) { return apertar(e, p); };
		tela_->aoMover = [this](QMouseEvent *e, QPointF p) { return mover(e, p); };
		tela_->aoSoltar = [this](QMouseEvent *e, QPointF p) { return soltar(e, p); };
		tela_->aoDuplo = [this](QMouseEvent *e, QPointF p) { return duplo(e, p); };
		tela_->teclas = [this](QKeyEvent *e) { return teclaTela(e); };
		tela_->menu = [this](QPoint g, QPointF p) { menuTela(g, p); };
		tela_->aoRedimensionar = [this] { ajustarZoom(); };
		tela_->aoRolar = [this](int d) {
			terminarEdicao();
			mostrarSlide(std::clamp(atual_ + d, 0, int(doc_.slides.size()) - 1));
		};
	}

	QAction *acao(const QString &texto, const QKeySequence &tecla, std::function<void()> f)
	{
		auto *a = new QAction(texto, this);
		a->setShortcut(tecla);
		a->setShortcutContext(Qt::WindowShortcut);
		connect(a, &QAction::triggered, this, [this, f] {
			if (pilha_->currentIndex() == 1)
				f();
		});
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
		b->setFocusPolicy(Qt::NoFocus);
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
		acao("Novo", QKeySequence::New, [this] { if (podeFechar()) mostrarGaleria(); });
		{
			// Abrir funciona também na galeria.
			auto *a = new QAction("Abrir", this);
			a->setShortcut(QKeySequence::Open);
			connect(a, &QAction::triggered, this, [this] { escolherEAbrir(); });
			addAction(a);
		}
		acao("Salvar", QKeySequence::Save, [this] { salvar(); });
		acao("Salvar como", QKeySequence::SaveAs, [this] { salvarComo(); });
		acao("Exportar PDF", QKeySequence(Qt::CTRL | Qt::Key_P), [this] { exportarPdfDialogo(); });
		acao("Desfazer", QKeySequence::Undo, [this] { if (!editando()) desfazer(); });
		acao("Refazer", QKeySequence::Redo, [this] { if (!editando()) refazer(); });
		acao("Refazer", QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { if (!editando()) refazer(); });
		acao("Copiar", QKeySequence::Copy, [this] { if (!editando()) copiar(false); });
		acao("Recortar", QKeySequence::Cut, [this] { if (!editando()) copiar(true); });
		acao("Colar", QKeySequence::Paste, [this] { if (!editando()) colar(); });
		acao("Duplicar", QKeySequence(Qt::CTRL | Qt::Key_D), [this] { if (!editando()) { if (sel_.empty()) duplicarSlide(); else duplicarObjetos(); } });
		acao("Negrito", QKeySequence::Bold, [this] { alternarLetra(0); });
		acao("Itálico", QKeySequence::Italic, [this] { alternarLetra(1); });
		acao("Sublinhado", QKeySequence::Underline, [this] { alternarLetra(2); });
		acao("Apresentar", QKeySequence(Qt::Key_F5), [this] { apresentar(0, false); });
		acao("Apresentar deste slide", QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { apresentar(atual_, false); });
		acao("Novo slide", QKeySequence(Qt::CTRL | Qt::Key_M), [this] { novoSlideDepois(proximoLeiaute()); });

		auto *doc = new QMenu(this);
		doc->addAction(QIcon::fromTheme("document-new"), "Novo…", this, [this] { if (podeFechar()) mostrarGaleria(); });
		doc->addAction(QIcon::fromTheme("document-open"), "Abrir…", this, [this] { escolherEAbrir(); });
		doc->addSeparator();
		doc->addAction(QIcon::fromTheme("document-save"), "Salvar", this, [this] { salvar(); });
		doc->addAction(QIcon::fromTheme("document-save-as"), "Salvar como…", this, [this] { salvarComo(); });
		doc->addSeparator();
		doc->addAction("Exportar PDF…", this, [this] { exportarPdfDialogo(); });
		doc->addAction("Exportar como PowerPoint (.pptx)…", this, [this] { exportar("pptx"); });
		doc->addAction("Exportar como OpenDocument (.odp)…", this, [this] { exportar("odp"); });
		botao("Apresentação", "document-open-folder", nullptr, doc);
		barra_->addSeparator();
		auto *novo = new QMenu(this);
		for (const Leiaute &l : leiautes()) {
			const Layout id = l.id;
			novo->addAction(QIcon(QPixmap::fromImage(miniaturaLeiaute(id))), l.nome, this, [this, id] { novoSlideDepois(id); });
		}
		botao("Slide", "list-add", nullptr, novo)->setToolTip("Novo slide (Ctrl+M)");
		barra_->addSeparator();
		botao("Texto", "insert-text", [this] { inserirTexto(); })->setToolTip("Uma caixa de texto");
		auto *formas = new QMenu(this);
		const QList<QPair<QString, Tipo>> fs = {{"Retângulo", Tipo::Retangulo}, {"Retângulo arredondado", Tipo::Arredondado}, {"Elipse", Tipo::Elipse},
		                                        {"Triângulo", Tipo::Triangulo}, {"Seta", Tipo::Seta}, {"Linha", Tipo::Linha}};
		for (const auto &[n, t] : fs) {
			const Tipo tt = t;
			formas->addAction(n, this, [this, tt] { inserirForma(tt); });
		}
		botao("Forma", "draw-rectangle", nullptr, formas);
		botao("Imagem", "insert-image", [this] { escolherImagem(); });
		auto *gr = new QMenu(this);
		const QList<QPair<QString, TipoGrafico>> gs = {{"Colunas", TipoGrafico::Colunas}, {"Barras", TipoGrafico::Barras}, {"Linhas", TipoGrafico::Linhas},
		                                              {"Pizza", TipoGrafico::Pizza}};
		for (const auto &[n, t] : gs) {
			const TipoGrafico tt = t;
			gr->addAction(n, this, [this, tt] { inserirGrafico(tt); });
		}
		botao("Gráfico", "office-chart-bar", nullptr, gr);
		auto *tb = new QMenu(this);
		for (const auto &[l, c] : std::vector<std::pair<int, int>>{{2, 2}, {3, 3}, {4, 3}, {4, 4}, {5, 4}, {6, 5}}) {
			const int ll = l, cc = c;
			tb->addAction(QString("%1 linhas × %2 colunas").arg(l).arg(c), this, [this, ll, cc] { inserirTabela(ll, cc); });
		}
		botao("Tabela", "insert-table", nullptr, tb);
		botao("Vídeo", "video-x-generic", [this] { escolherVideo(); })->setToolTip("Um vídeo (toca na apresentação)");
		auto *espaco = new QWidget;
		espaco->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		barra_->addWidget(espaco);
		titulo_ = new QLabel;
		titulo_->setObjectName("tituloDoc");
		barra_->addWidget(titulo_);
		auto *espaco2 = new QWidget;
		espaco2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		barra_->addWidget(espaco2);
		zoom_ = new QComboBox;
		zoom_->setFocusPolicy(Qt::NoFocus);
		zoom_->addItems({"Ajustar", "50%", "75%", "100%", "150%", "200%"});
		zoom_->setToolTip("Zoom");
		connect(zoom_, qOverload<int>(&QComboBox::activated), this, [this] { ajustarZoom(); });
		barra_->addWidget(zoom_);
		barra_->addSeparator();
		auto *ap = new QMenu(this);
		ap->addAction("Do começo (F5)", this, [this] { apresentar(0, false); });
		ap->addAction("Deste slide (Shift+F5)", this, [this] { apresentar(atual_, false); });
		ap->addSeparator();
		ap->addAction("Ensaiar (com a tela do apresentador)", this, [this] { apresentar(0, true); });
		auto *bAp = botao("Apresentar", "media-playback-start", [this] { apresentar(0, false); }, ap);
		bAp->setPopupMode(QToolButton::MenuButtonPopup);
		connect(bAp, &QToolButton::clicked, this, [this] { apresentar(0, false); });
		barra_->addSeparator();
		// Os painéis, na ordem da barra (o índice é o da pilha do inspetor).
		const QStringList paineis{"Formatar", "Animar", "Transição", "Documento"};
		const QStringList icones{"format-text-bold", "media-playlist-shuffle", "transform-move", "document-properties"};
		const int indice[] = {0, 3, 1, 2};
		for (int j = 0; j < 4; j++) {
			const int i = indice[j];
			QToolButton *b = botao(paineis[j], icones[j], [this, i] { mostrarInspetor(i, true); });
			b->setCheckable(true);
			bPainel_[i] = b;
		}
	}

	// --- inspetor ---
	static void secao(QVBoxLayout *v, const QString &t)
	{
		auto *l = new QLabel(rotulo(t));
		l->setObjectName("rotulo");
		v->addSpacing(10);
		v->addWidget(l);
	}

	QToolButton *alterna(const QString &texto, const QString &dica, QHBoxLayout *h, std::function<void()> f, bool marcavel = true)
	{
		auto *b = new QToolButton;
		b->setText(texto);
		b->setToolTip(dica);
		b->setCheckable(marcavel);
		b->setObjectName("alterna");
		b->setFocusPolicy(Qt::NoFocus);
		b->setMinimumSize(30, 30);
		b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		connect(b, &QToolButton::clicked, this, [f] { f(); });
		h->addWidget(b);
		return b;
	}

	QWidget *pagina(const QString &titulo, QVBoxLayout *&v)
	{
		auto *w = new QWidget;
		v = new QVBoxLayout(w);
		v->setContentsMargins(18, 14, 18, 14);
		v->setSpacing(6);
		auto *cab = new QLabel(titulo);
		cab->setObjectName("parte");
		v->addWidget(cab);
		auto *fio = new QFrame;
		fio->setObjectName("fio");
		v->addWidget(fio);
		auto *rolar = new QScrollArea;
		rolar->setWidget(w);
		rolar->setWidgetResizable(true);
		rolar->setFrameStyle(QFrame::NoFrame);
		rolar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		inspetor_->addWidget(rolar);
		return w;
	}

	// Um botão de cor: abre um menu com as cores do tema, "outra cor" e (se der) "nenhuma".
	QToolButton *botaoCor(const QString &texto, std::function<void(QColor)> f, const QString &nenhuma = QString())
	{
		auto *b = new QToolButton;
		b->setText(texto);
		b->setObjectName("amostra");
		b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		b->setPopupMode(QToolButton::InstantPopup);
		b->setFocusPolicy(Qt::NoFocus);
		b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		auto *m = new QMenu(b);
		connect(m, &QMenu::aboutToShow, this, [this, m, f, nenhuma] {
			m->clear();
			std::vector<QColor> cs{doc_.tema.titulo, doc_.tema.texto, doc_.tema.fundo, doc_.tema.destaque, doc_.tema.apagado};
			for (const QColor &c : doc_.tema.serie)
				cs.push_back(c);
			cs.push_back(Qt::white);
			cs.push_back(Qt::black);
			std::vector<QColor> unicas;
			for (const QColor &c : cs)
				if (std::find(unicas.begin(), unicas.end(), c) == unicas.end())
					unicas.push_back(c);
			for (const QColor &c : unicas) {
				QPixmap px(16, 16);
				px.fill(c);
				{
					QPainter p(&px);
					p.setPen(QColor(0, 0, 0, 90));
					p.drawRect(0, 0, 15, 15);
				}
				m->addAction(QIcon(px), c.name(), this, [f, c] { f(c); });
			}
			m->addSeparator();
			m->addAction("Outra cor…", this, [this, f] {
				const QColor c = QColorDialog::getColor(doc_.tema.destaque, this, "Cor");
				if (c.isValid())
					f(c);
			});
			if (!nenhuma.isEmpty())
				m->addAction(nenhuma, this, [f] { f(QColor()); });
		});
		b->setMenu(m);
		return b;
	}

	static void pintarAmostra(QToolButton *b, const QColor &c)
	{
		QPixmap px(18, 18);
		px.fill(c.isValid() ? c : QColor(0, 0, 0, 0));
		{
			QPainter p(&px);
			p.setPen(QColor(0x1c, 0x1b, 0x19));
			p.drawRect(0, 0, 17, 17);
			if (!c.isValid()) {
				p.setPen(QPen(QColor(0xb0, 0x30, 0x30), 2));
				p.drawLine(2, 16, 16, 2);
			}
		}
		b->setIcon(QIcon(px));
	}

	QDoubleSpinBox *numero(double min, double max, double passo, const QString &sufixo, int casas = 0)
	{
		auto *s = new QDoubleSpinBox;
		s->setRange(min, max);
		s->setSingleStep(passo);
		s->setDecimals(casas);
		s->setSuffix(sufixo);
		s->setKeyboardTracking(false);
		return s;
	}

	void montarInspetor()
	{
		QVBoxLayout *v = nullptr;
		// 0: Formatar.
		pagina("Formatar", v);
		quadroVazio_ = new QLabel;
		quadroVazio_->setObjectName("vazio");
		quadroVazio_->setWordWrap(true);
		v->addWidget(quadroVazio_);

		// O slide.
		secSlide_ = new QWidget;
		{
			auto *sv = new QVBoxLayout(secSlide_);
			sv->setContentsMargins(0, 0, 0, 0);
			sv->setSpacing(6);
			secao(sv, "Leiaute");
			leiaute_ = new QComboBox;
			for (const Leiaute &l : leiautes())
				leiaute_->addItem(QIcon(QPixmap::fromImage(miniaturaLeiaute(l.id))), l.nome);
			leiaute_->setIconSize(QSize(48, 27));
			connect(leiaute_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
				guardar();
				aplicarLeiaute(doc_, slide(), leiautes()[size_t(i)].id);
				sel_.clear();
				refazerCena();
				marcarMudanca();
			});
			sv->addWidget(leiaute_);
			secao(sv, "Fundo");
			auto *fh = new QHBoxLayout;
			corFundo_ = botaoCor("Cor do fundo", [this](QColor c) {
				guardar();
				slide().fundo = c;
				refazerCena();
				marcarMudanca();
			}, "A do tema");
			fh->addWidget(corFundo_);
			sv->addLayout(fh);
			auto *ih = new QHBoxLayout;
			auto *img = new QPushButton("Imagem de fundo…");
			connect(img, &QPushButton::clicked, this, [this] { escolherFundo(); });
			auto *semImg = new QPushButton("Tirar");
			connect(semImg, &QPushButton::clicked, this, [this] {
				guardar();
				slide().fundoMidia.clear();
				doc_.limparMidias();
				refazerCena();
				marcarMudanca();
			});
			ih->addWidget(img, 1);
			ih->addWidget(semImg);
			sv->addLayout(ih);
			pular_ = new QCheckBox("Pular este slide ao apresentar");
			connect(pular_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				guardar();
				slide().pular = b;
				atualizarMiniatura(atual_);
				marcarMudanca();
			});
			sv->addSpacing(6);
			sv->addWidget(pular_);
		}
		v->addWidget(secSlide_);

		// O texto.
		secTexto_ = new QWidget;
		{
			auto *tv = new QVBoxLayout(secTexto_);
			tv->setContentsMargins(0, 0, 0, 0);
			tv->setSpacing(6);
			secao(tv, "Texto");
			familia_ = new QFontComboBox;
			connect(familia_, &QFontComboBox::activated, this, [this] { mudarLetra([f = familia_->currentFont().family()](Trecho &t, Objeto &o, bool todo) {
				if (todo) { o.fonte = f; t.fonte.clear(); } else t.fonte = f;
			}, [f = familia_->currentFont().family()](QTextCharFormat &cf) { cf.setFontFamilies({f}); }); });
			tv->addWidget(familia_);
			auto *th = new QHBoxLayout;
			th->setSpacing(4);
			tamanho_ = numero(6, 300, 1, " pt", 1);
			tamanho_->setFixedWidth(104);
			connect(tamanho_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
				if (atualizando_) return;
				mudarTamanho(v);
			});
			th->addWidget(tamanho_);
			negrito_ = alterna("N", "Negrito (Ctrl+B)", th, [this] { alternarLetra(0); });
			italico_ = alterna("I", "Itálico (Ctrl+I)", th, [this] { alternarLetra(1); });
			sublinhado_ = alterna("S", "Sublinhado (Ctrl+U)", th, [this] { alternarLetra(2); });
			{
				QFont f = negrito_->font(); f.setBold(true); negrito_->setFont(f);
				f = italico_->font(); f.setItalic(true); italico_->setFont(f);
				f = sublinhado_->font(); f.setUnderline(true); sublinhado_->setFont(f);
			}
			tv->addLayout(th);
			corTexto_ = botaoCor("Cor do texto", [this](QColor c) {
				mudarLetra([c](Trecho &t, Objeto &o, bool todo) {
					if (todo) { o.cor = c; t.cor = QColor(); } else t.cor = c;
				}, [this, c](QTextCharFormat &cf) {
					cf.setForeground(c.isValid() ? c : corTexto(doc_, objetoEditado()));
				});
			}, "A do tema");
			tv->addWidget(corTexto_);
			auto *ah = new QHBoxLayout;
			ah->setSpacing(4);
			const QStringList al{"⯇", "≡", "⯈", "☰"};
			const QStringList dicasAl{"Alinhar à esquerda", "Centralizar", "Alinhar à direita", "Justificar"};
			const QStringList txAl{"Esq.", "Centro", "Dir.", "Just."};
			for (int i = 0; i < 4; i++)
				alin_[i] = alterna(txAl[i], dicasAl[i], ah, [this, i] { alinhar(i); });
			tv->addLayout(ah);
			auto *vh = new QHBoxLayout;
			vh->setSpacing(4);
			const QStringList tvA{"Topo", "Meio", "Base"};
			for (int i = 0; i < 3; i++)
				valin_[i] = alterna(tvA[i], "Alinhamento vertical", vh, [this, i] { alinharVertical(i); });
			tv->addLayout(vh);
			secao(tv, "Tópicos");
			auto *lh = new QHBoxLayout;
			lh->setSpacing(4);
			marcador_ = alterna("• Marcadores", "Tópicos com marcador", lh, [this] { listar(1); });
			numerado_ = alterna("1. Numerados", "Lista numerada", lh, [this] { listar(2); });
			tv->addLayout(lh);
			auto *nh = new QHBoxLayout;
			nh->setSpacing(4);
			alterna("⇤ Subir nível", "Subir um nível (Shift+Tab)", nh, [this] { nivel(-1); }, false);
			alterna("Descer nível ⇥", "Descer um nível (Tab)", nh, [this] { nivel(1); }, false);
			tv->addLayout(nh);
			encolher_ = new QCheckBox("Diminuir a letra para caber");
			connect(encolher_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarObjetos([b](Objeto &o) { o.encolher = b; });
			});
			tv->addWidget(encolher_);
		}
		v->addWidget(secTexto_);

		// A forma (e a borda da imagem).
		secForma_ = new QWidget;
		{
			auto *fv = new QVBoxLayout(secForma_);
			fv->setContentsMargins(0, 0, 0, 0);
			fv->setSpacing(6);
			secao(fv, "Preenchimento e borda");
			preench_ = botaoCor("Preenchimento", [this](QColor c) {
				mudarObjetos([c](Objeto &o) {
					if (o.tipo == Tipo::Tabela)
						for (Objeto &x : o.cel) x.preench = c;
					else if (o.tipo != Tipo::Imagem)
						o.preench = c;
				});
			}, "Sem preenchimento");
			fv->addWidget(preench_);
			auto *bh = new QHBoxLayout;
			borda_ = botaoCor("Borda", [this](QColor c) {
				mudarObjetos([c](Objeto &o) {
					o.borda = c;
					if (c.isValid() && o.espBorda <= 0) o.espBorda = 2;
				});
			}, "Sem borda");
			espBorda_ = numero(0, 40, 0.5, " pt", 1);
			connect(espBorda_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
				if (atualizando_) return;
				mudarObjetos([v](Objeto &o) {
					o.espBorda = v;
					if (v > 0 && !o.borda.isValid()) o.borda = QColor(0x1c, 0x1b, 0x19);
				});
			});
			bh->addWidget(borda_, 1);
			bh->addWidget(espBorda_);
			fv->addLayout(bh);
		}
		v->addWidget(secForma_);

		// A imagem.
		secImagem_ = new QWidget;
		{
			auto *iv = new QVBoxLayout(secImagem_);
			iv->setContentsMargins(0, 0, 0, 0);
			iv->setSpacing(6);
			secao(iv, "Imagem");
			auto *tr = new QPushButton("Trocar a imagem…");
			connect(tr, &QPushButton::clicked, this, [this] { trocarImagem(); });
			iv->addWidget(tr);
			auto *orig = new QPushButton("Tamanho original (sem recorte)");
			connect(orig, &QPushButton::clicked, this, [this] {
				mudarObjetos([this](Objeto &o) {
					if (o.tipo != Tipo::Imagem) return;
					const QImage img = imagemDe(doc_, o.midia);
					o.recorte = QRectF(0, 0, 1, 1);
					if (!img.isNull()) {
						const double k = std::min({1.0, doc_.larg * 0.8 / img.width(), doc_.alt * 0.8 / img.height()});
						const QPointF c = o.r.center();
						o.r = QRectF(0, 0, img.width() * k, img.height() * k);
						o.r.moveCenter(c);
					}
				});
			});
			iv->addWidget(orig);
		}
		v->addWidget(secImagem_);

		// O gráfico.
		secGrafico_ = new QWidget;
		{
			auto *gv = new QVBoxLayout(secGrafico_);
			gv->setContentsMargins(0, 0, 0, 0);
			gv->setSpacing(6);
			secao(gv, "Gráfico");
			gTipo_ = new QComboBox;
			gTipo_->addItems({"Colunas", "Barras", "Linhas", "Pizza"});
			connect(gTipo_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
				mudarObjetos([i](Objeto &o) { if (o.tipo == Tipo::Grafico) o.graf.tipo = TipoGrafico(i); });
			});
			gv->addWidget(gTipo_);
			gTitulo_ = new QLineEdit;
			gTitulo_->setPlaceholderText("Título do gráfico");
			connect(gTitulo_, &QLineEdit::editingFinished, this, [this] {
				if (atualizando_) return;
				const QString t = gTitulo_->text();
				const Objeto *o = umObjeto();
				if (!o || o->tipo != Tipo::Grafico || o->graf.titulo == t) return;
				mudarObjetos([t](Objeto &x) { if (x.tipo == Tipo::Grafico) x.graf.titulo = t; });
			});
			gv->addWidget(gTitulo_);
			gLegenda_ = new QCheckBox("Mostrar a legenda");
			connect(gLegenda_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarObjetos([b](Objeto &o) { if (o.tipo == Tipo::Grafico) o.graf.legenda = b; });
			});
			gv->addWidget(gLegenda_);
			auto *dl = new QLabel("Os dados (a primeira linha tem os nomes das séries; a primeira coluna, as categorias):");
			dl->setWordWrap(true);
			dl->setObjectName("dica");
			gv->addWidget(dl);
			dados_ = new QTableWidget;
			dados_->setObjectName("dados");
			dados_->horizontalHeader()->hide();
			dados_->verticalHeader()->hide();
			dados_->setMinimumHeight(220);
			dados_->horizontalHeader()->setDefaultSectionSize(70);
			dados_->verticalHeader()->setDefaultSectionSize(24);
			connect(dados_, &QTableWidget::cellChanged, this, [this](int, int) {
				if (atualizando_) return;
				lerTabelaDoGrafico();
			});
			gv->addWidget(dados_);
			auto *dh = new QHBoxLayout;
			auto mais = [&](const QString &t, std::function<void()> f) {
				auto *b = new QPushButton(t);
				connect(b, &QPushButton::clicked, this, [f] { f(); });
				dh->addWidget(b);
			};
			mais("+ linha", [this] { dados_->insertRow(dados_->rowCount()); lerTabelaDoGrafico(); });
			mais("− linha", [this] { if (dados_->rowCount() > 2) { dados_->removeRow(dados_->rowCount() - 1); lerTabelaDoGrafico(); } });
			mais("+ série", [this] { dados_->insertColumn(dados_->columnCount()); lerTabelaDoGrafico(); });
			mais("− série", [this] { if (dados_->columnCount() > 2) { dados_->removeColumn(dados_->columnCount() - 1); lerTabelaDoGrafico(); } });
			gv->addLayout(dh);
		}
		v->addWidget(secGrafico_);

		// A tabela.
		secTabela_ = new QWidget;
		{
			auto *tv = new QVBoxLayout(secTabela_);
			tv->setContentsMargins(0, 0, 0, 0);
			tv->setSpacing(6);
			secao(tv, "Tabela");
			auto *d = new QLabel("Dois cliques numa célula para escrever; Tab passa para a próxima (na última, cria uma linha).");
			d->setObjectName("dica");
			d->setWordWrap(true);
			tv->addWidget(d);
			auto *lh = new QHBoxLayout;
			lh->setSpacing(4);
			auto *ll = new QLabel("Linha");
			ll->setFixedWidth(52);
			lh->addWidget(ll);
			alterna("+ Acima", "Uma linha acima da célula", lh, [this] { mexerTabela(0); }, false);
			alterna("+ Abaixo", "Uma linha abaixo da célula", lh, [this] { mexerTabela(1); }, false);
			alterna("Tirar", "Tirar a linha da célula", lh, [this] { mexerTabela(2); }, false);
			tv->addLayout(lh);
			auto *ch = new QHBoxLayout;
			ch->setSpacing(4);
			auto *lc = new QLabel("Coluna");
			lc->setFixedWidth(52);
			ch->addWidget(lc);
			alterna("+ Esq.", "Uma coluna à esquerda da célula", ch, [this] { mexerTabela(3); }, false);
			alterna("+ Dir.", "Uma coluna à direita da célula", ch, [this] { mexerTabela(4); }, false);
			alterna("Tirar", "Tirar a coluna da célula", ch, [this] { mexerTabela(5); }, false);
			tv->addLayout(ch);
			auto *ig = new QPushButton("Colunas da mesma largura");
			connect(ig, &QPushButton::clicked, this, [this] { mexerTabela(6); });
			tv->addWidget(ig);
			tCabecalho_ = new QCheckBox("A primeira linha é o cabeçalho");
			connect(tCabecalho_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarTabela([b](Objeto &t) { t.cabecalho = b; });
			});
			tv->addWidget(tCabecalho_);
			tFaixas_ = new QCheckBox("Linhas em faixas (uma sim, outra não)");
			connect(tFaixas_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarTabela([b](Objeto &t) { t.faixas = b; });
			});
			tv->addWidget(tFaixas_);
			tFundo_ = botaoCor("Fundo da célula", [this](QColor c) {
				// Editando: só a célula; senão, todas.
				const int cel = edCel_;
				mudarTabela([c, cel](Objeto &t) {
					for (int i = 0; i < int(t.cel.size()); i++)
						if (cel < 0 || i == cel)
							t.cel[size_t(i)].preench = c;
				});
			}, "A do tema");
			tv->addWidget(tFundo_);
		}
		v->addWidget(secTabela_);

		// O vídeo.
		secVideo_ = new QWidget;
		{
			auto *vv = new QVBoxLayout(secVideo_);
			vv->setContentsMargins(0, 0, 0, 0);
			vv->setSpacing(6);
			secao(vv, "Vídeo");
			auto *vh = new QHBoxLayout;
			auto *tocar = new QPushButton("▶ Tocar aqui");
			connect(tocar, &QPushButton::clicked, this, [this] { if (!sel_.empty()) previaVideo(sel_[0]); });
			auto *capa = new QPushButton("Quadro como capa");
			capa->setToolTip("A imagem que aparece antes de o vídeo tocar: o quadro em que ele está pausado aqui");
			connect(capa, &QPushButton::clicked, this, [this] { capaDoQuadro(); });
			vh->addWidget(tocar);
			vh->addWidget(capa, 1);
			vv->addLayout(vh);
			vSozinho_ = new QCheckBox("Começar sozinho quando o slide aparecer");
			connect(vSozinho_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarObjetos([b](Objeto &o) { if (o.tipo == Tipo::Video) o.sozinho = b; });
			});
			vv->addWidget(vSozinho_);
			vRepetir_ = new QCheckBox("Repetir até mudar de slide");
			connect(vRepetir_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarObjetos([b](Objeto &o) { if (o.tipo == Tipo::Video) o.repetir = b; });
			});
			vv->addWidget(vRepetir_);
			vMudo_ = new QCheckBox("Sem som");
			connect(vMudo_, &QCheckBox::toggled, this, [this](bool b) {
				if (atualizando_) return;
				mudarObjetos([b](Objeto &o) { if (o.tipo == Tipo::Video) o.mudo = b; });
			});
			vv->addWidget(vMudo_);
			auto *tv = new QPushButton("Trocar o vídeo…");
			connect(tv, &QPushButton::clicked, this, [this] { trocarVideo(); });
			vv->addWidget(tv);
			auto *d = new QLabel("Na apresentação, um clique no vídeo toca e pausa. Para abrir no PowerPoint, prefira MP4.");
			d->setObjectName("dica");
			d->setWordWrap(true);
			vv->addWidget(d);
		}
		v->addWidget(secVideo_);

		// Organizar (qualquer objeto).
		secOrganizar_ = new QWidget;
		{
			auto *ov = new QVBoxLayout(secOrganizar_);
			ov->setContentsMargins(0, 0, 0, 0);
			ov->setSpacing(6);
			secao(ov, "Organizar");
			auto *grid = new QGridLayout;
			grid->setHorizontalSpacing(6);
			grid->setVerticalSpacing(4);
			px_ = numero(-5000, 5000, 1, " pt");
			py_ = numero(-5000, 5000, 1, " pt");
			pw_ = numero(1, 5000, 1, " pt");
			ph_ = numero(1, 5000, 1, " pt");
			giro_ = numero(-360, 360, 5, "°");
			grid->addWidget(new QLabel("X"), 0, 0);
			grid->addWidget(px_, 0, 1);
			grid->addWidget(new QLabel("Y"), 0, 2);
			grid->addWidget(py_, 0, 3);
			grid->addWidget(new QLabel("L"), 1, 0);
			grid->addWidget(pw_, 1, 1);
			grid->addWidget(new QLabel("A"), 1, 2);
			grid->addWidget(ph_, 1, 3);
			grid->addWidget(new QLabel("Girar"), 2, 0, 1, 1);
			grid->addWidget(giro_, 2, 1);
			ov->addLayout(grid);
			auto ligar = [this](QDoubleSpinBox *s, int qual) {
				connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, qual](double v) {
					if (atualizando_) return;
					mudarObjetos([qual, v](Objeto &o) {
						switch (qual) {
						case 0: o.r.moveLeft(v); break;
						case 1: o.r.moveTop(v); break;
						case 2: o.r.setWidth(v); break;
						case 3:
							if (o.tipo == Tipo::Tabela && o.r.height() > 0)
								for (double &h : o.alts) h *= v / o.r.height();
							o.r.setHeight(v);
							break;
						default: o.giro = std::fmod(v + 360, 360); break;
						}
					});
				});
			};
			ligar(px_, 0);
			ligar(py_, 1);
			ligar(pw_, 2);
			ligar(ph_, 3);
			ligar(giro_, 4);
			auto *oh = new QHBoxLayout;
			oh->setSpacing(4);
			alterna("Frente", "Trazer para a frente", oh, [this] { camada(true); }, false);
			alterna("Trás", "Mandar para trás", oh, [this] { camada(false); }, false);
			alterna("↔", "Espelhar na horizontal", oh, [this] { mudarObjetos([](Objeto &o) { o.viraH = !o.viraH; }); }, false);
			alterna("↕", "Espelhar na vertical", oh, [this] { mudarObjetos([](Objeto &o) { o.viraV = !o.viraV; }); }, false);
			ov->addLayout(oh);
			auto *ap = new QPushButton("Apagar");
			connect(ap, &QPushButton::clicked, this, [this] { apagarObjetos(); });
			ov->addWidget(ap);
		}
		v->addWidget(secOrganizar_);
		v->addStretch();

		// 1: Transição.
		pagina("Transição", v);
		auto *tl = new QLabel("Como este slide entra na apresentação.");
		tl->setObjectName("dica");
		tl->setWordWrap(true);
		v->addWidget(tl);
		secao(v, "Efeito");
		transicao_ = new QComboBox;
		transicao_->addItems(nomesTransicao());
		connect(transicao_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			guardar();
			slide().transicao = Transicao(i);
			marcarMudanca();
			previa();
		});
		v->addWidget(transicao_);
		secao(v, "Duração");
		duracao_ = numero(0.2, 4, 0.1, " s", 1);
		connect(duracao_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double d) {
			if (atualizando_) return;
			guardar();
			slide().duracao = d;
			marcarMudanca();
		});
		v->addWidget(duracao_);
		previa_ = new QLabel;
		previa_->setObjectName("previa");
		previa_->setFixedSize(256, 144);
		v->addSpacing(10);
		v->addWidget(previa_, 0, Qt::AlignHCenter);
		auto *ph = new QHBoxLayout;
		auto *ver = new QPushButton("Ver de novo");
		connect(ver, &QPushButton::clicked, this, [this] { previa(); });
		auto *todos = new QPushButton("Usar em todos");
		connect(todos, &QPushButton::clicked, this, [this] {
			guardar();
			for (Slide &s : doc_.slides) {
				s.transicao = slide().transicao;
				s.duracao = slide().duracao;
			}
			marcarMudanca();
			statusBar()->showMessage("A transição vale para todos os slides.", 4000);
		});
		ph->addWidget(ver);
		ph->addWidget(todos);
		v->addLayout(ph);
		v->addStretch();
		animPrevia_ = new QVariantAnimation(this);
		animPrevia_->setStartValue(0.0);
		animPrevia_->setEndValue(1.0);
		connect(animPrevia_, &QVariantAnimation::valueChanged, this, [this](const QVariant &x) {
			QImage img(previa_->size(), QImage::Format_ARGB32_Premultiplied);
			QPainter p(&img);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			pintarTransicao(p, previaAntes_, previaDepois_, slide().transicao, x.toDouble(), img.rect());
			p.end();
			previa_->setPixmap(QPixmap::fromImage(img));
		});

		// 2: Documento.
		pagina("Documento", v);
		secao(v, "Tema");
		auto *tg = new QGridLayout;
		tg->setSpacing(8);
		int i = 0;
		for (const Tema &t : temas()) {
			Doc d;
			montarModelo(d, "branco");
			d.tema = t;
			for (Objeto &o : d.slides[0].objs)
				Roteiro::texto(o, o.papel == Papel::Titulo ? t.nome : QString());
			auto *b = new QToolButton;
			b->setObjectName("cartaoModelo");
			b->setFocusPolicy(Qt::NoFocus);
			b->setToolButtonStyle(Qt::ToolButtonIconOnly);
			b->setIcon(QIcon(QPixmap::fromImage(imagemSlide(d, d.slides[0], 240))));
			b->setIconSize(QSize(112, 63));
			b->setToolTip(t.nome);
			const QString id = t.id;
			connect(b, &QToolButton::clicked, this, [this, id] {
				guardar();
				doc_.tema = tema(id);
				refazerCena();
				atualizarMiniaturas();
				marcarMudanca();
			});
			tg->addWidget(b, i / 2, i % 2);
			i++;
		}
		v->addLayout(tg);
		secao(v, "Tamanho do slide");
		formato_ = new QComboBox;
		formato_->addItems({"Largo (16:9)", "Padrão (4:3)"});
		connect(formato_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			const double larg = i == 0 ? 960 : 720;
			if (std::fabs(larg - doc_.larg) < 1) return;
			guardar();
			mudarFormato(doc_, larg);
			cena_->setSceneRect(QRectF(-40, -40, doc_.larg + 80, doc_.alt + 80));
			refazerCena();
			atualizarMiniaturas();
			ajustarZoom();
			marcarMudanca();
		});
		v->addWidget(formato_);
		secao(v, "Título e autor");
		dTitulo_ = new QLineEdit;
		dTitulo_->setPlaceholderText("O título (vai nas propriedades do arquivo)");
		connect(dTitulo_, &QLineEdit::editingFinished, this, [this] {
			if (dTitulo_->text() != doc_.titulo) { doc_.titulo = dTitulo_->text(); marcarMudanca(false); }
		});
		v->addWidget(dTitulo_);
		dAutor_ = new QLineEdit;
		dAutor_->setPlaceholderText("Quem fez");
		connect(dAutor_, &QLineEdit::editingFinished, this, [this] {
			if (dAutor_->text() != doc_.autor) { doc_.autor = dAutor_->text(); marcarMudanca(false); }
		});
		v->addWidget(dAutor_);
		v->addStretch();

		// 3: Animar.
		pagina("Animar", v);
		auto *al = new QLabel("Como cada objeto entra e sai do slide. Na apresentação, cada clique roda o passo seguinte.");
		al->setObjectName("dica");
		al->setWordWrap(true);
		v->addWidget(al);
		aVazio_ = new QLabel("Clique num objeto do slide para animá-lo.");
		aVazio_->setObjectName("vazio");
		aVazio_->setWordWrap(true);
		v->addWidget(aVazio_);
		aCorpo_ = new QWidget;
		{
			auto *cv = new QVBoxLayout(aCorpo_);
			cv->setContentsMargins(0, 0, 0, 0);
			cv->setSpacing(6);
			for (int q = 0; q < 2; q++) {
				secao(cv, q == 0 ? "Entrada" : "Saída");
				aEf_[q] = new QComboBox;
				for (const QString &n : nomesEfeito())
					aEf_[q]->addItem(n == "Nenhum" ? (q == 0 ? "Nenhuma (já está no slide)" : "Nenhuma (fica no slide)") : n);
				cv->addWidget(aEf_[q]);
				auto *g = new QGridLayout;
				g->setHorizontalSpacing(6);
				g->setVerticalSpacing(4);
				aDir_[q] = new QComboBox;
				aDir_[q]->addItems(q == 0 ? QStringList{"De baixo", "Da esquerda", "Da direita", "De cima"}
				                          : QStringList{"Para baixo", "Para a esquerda", "Para a direita", "Para cima"});
				aIni_[q] = new QComboBox;
				aIni_[q]->addItems({"Ao clicar", "Junto com a anterior", "Depois da anterior"});
				aDur_[q] = numero(0.1, 10, 0.1, " s", 1);
				g->addWidget(new QLabel("Começa"), 0, 0);
				g->addWidget(aIni_[q], 0, 1);
				g->addWidget(new QLabel("Direção"), 1, 0);
				g->addWidget(aDir_[q], 1, 1);
				g->addWidget(new QLabel("Duração"), 2, 0);
				g->addWidget(aDur_[q], 2, 1);
				g->setColumnStretch(1, 1);
				cv->addLayout(g);
				if (q == 0) {
					aPar_ = new QCheckBox("Um parágrafo (tópico) por vez");
					cv->addWidget(aPar_);
					connect(aPar_, &QCheckBox::toggled, this, [this](bool b) {
						if (atualizando_) return;
						mudarAnimacao(0, [b](Animacao &a) { a.porParagrafo = b; });
					});
				}
				connect(aEf_[q], qOverload<int>(&QComboBox::activated), this, [this, q](int i) {
					mudarAnimacao(q, [i](Animacao &a) { a.ef = Efeito(i); });
				});
				connect(aDir_[q], qOverload<int>(&QComboBox::activated), this, [this, q](int i) {
					mudarAnimacao(q, [i](Animacao &a) { a.dir = i; });
				});
				connect(aIni_[q], qOverload<int>(&QComboBox::activated), this, [this, q](int i) {
					mudarAnimacao(q, [i](Animacao &a) { a.inicio = i; });
				});
				connect(aDur_[q], qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, q](double d) {
					if (atualizando_) return;
					mudarAnimacao(q, [d](Animacao &a) { a.dur = d; });
				});
			}
		}
		v->addWidget(aCorpo_);
		secao(v, "A ordem neste slide");
		aOrdem_ = new QListWidget;
		aOrdem_->setObjectName("ordem");
		aOrdem_->setMinimumHeight(140);
		connect(aOrdem_, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
			const int k = it->data(Qt::UserRole).toInt();
			if (k >= 0 && k < int(slide().objs.size()) && !(sel_.size() == 1 && sel_[0] == k)) {
				terminarEdicao();
				sel_ = {k};
				atualizarSelecao();
				atualizarInspetor();
			}
		});
		v->addWidget(aOrdem_);
		auto *oh = new QHBoxLayout;
		auto *subir = new QPushButton("Mais cedo");
		connect(subir, &QPushButton::clicked, this, [this] { moverAnimacao(-1); });
		auto *descer = new QPushButton("Mais tarde");
		connect(descer, &QPushButton::clicked, this, [this] { moverAnimacao(1); });
		oh->addWidget(subir);
		oh->addWidget(descer);
		v->addLayout(oh);
		aPrevia_ = new QLabel;
		aPrevia_->setObjectName("previa");
		aPrevia_->setFixedSize(256, 144);
		v->addSpacing(10);
		v->addWidget(aPrevia_, 0, Qt::AlignHCenter);
		auto *verA = new QPushButton("Ver o slide de novo");
		connect(verA, &QPushButton::clicked, this, [this] { previaAnimacao(); });
		v->addWidget(verA);
		v->addStretch();
		aTempo_ = new QTimer(this);
		aTempo_->setInterval(33);
		connect(aTempo_, &QTimer::timeout, this, [this] { quadroDaPrevia(); });
	}

	// --- as animações ---
	// As animações do slide em ordem: (objeto, saída?).
	std::vector<std::pair<int, bool>> listaAnimacoes()
	{
		std::vector<std::tuple<int, int, bool>> l;
		for (int k = 0; k < int(slide().objs.size()); k++) {
			const Objeto &o = obj(k);
			if (o.entrada.tem()) l.push_back({o.entrada.ordem, k, false});
			if (o.saida.tem()) l.push_back({o.saida.ordem, k, true});
		}
		std::stable_sort(l.begin(), l.end());
		std::vector<std::pair<int, bool>> r;
		for (const auto &[o, k, sai] : l)
			r.push_back({k, sai});
		return r;
	}
	int proximaOrdem()
	{
		int n = 0;
		for (const Objeto &o : slide().objs) {
			if (o.entrada.tem()) n = std::max(n, o.entrada.ordem + 1);
			if (o.saida.tem()) n = std::max(n, o.saida.ordem + 1);
		}
		return n;
	}
	// Uma mudança na entrada (0) ou na saída (1) dos objetos selecionados.
	void mudarAnimacao(int q, const std::function<void(Animacao &)> &f)
	{
		if (sel_.empty() || atualizando_)
			return;
		terminarEdicao();
		guardar();
		for (int k : sel_) {
			Animacao &a = q == 0 ? obj(k).entrada : obj(k).saida;
			const bool tinha = a.tem();
			f(a);
			if (!tinha && a.tem()) {
				// Uma animação nova vai para o fim da fila.
				a.ordem = proximaOrdem();
				if (a.dur <= 0) a.dur = 0.5;
			}
		}
		// A ordem volta a ser 0, 1, 2...
		const auto l = listaAnimacoes();
		for (size_t i = 0; i < l.size(); i++)
			(l[i].second ? obj(l[i].first).saida : obj(l[i].first).entrada).ordem = int(i);
		atualizarSelecao();
		atualizarInspetor();
		atualizarMiniatura(atual_);
		marcarMudanca(false);
		previaAnimacao();
	}
	void moverAnimacao(int d)
	{
		auto l = listaAnimacoes();
		const int i = aOrdem_->currentRow();
		const int j = i + d;
		if (i < 0 || j < 0 || j >= int(l.size()))
			return;
		guardar();
		std::swap(l[size_t(i)], l[size_t(j)]);
		for (size_t n = 0; n < l.size(); n++)
			(l[n].second ? obj(l[n].first).saida : obj(l[n].first).entrada).ordem = int(n);
		atualizarSelecao();
		atualizarInspetor();
		aOrdem_->setCurrentRow(j);
		marcarMudanca(false);
	}
	// A prévia: o slide inteiro, um clique a cada meio segundo depois do anterior.
	void previaAnimacao()
	{
		if (!aPrevia_ || doc_.slides.empty())
			return;
		aRelogio_.start();
		aTempo_->start();
		quadroDaPrevia();
	}
	void quadroDaPrevia()
	{
		if (doc_.slides.empty())
			return;
		const auto g = sequencia(slide());
		double t = aRelogio_.elapsed() / 1000.0;
		int grupo = 0;
		for (; grupo < int(g.size()); grupo++) {
			const double d = duracaoGrupo(g[size_t(grupo)]) + (grupo + 1 < int(g.size()) ? 0.5 : 0);
			if (t < d || grupo + 1 == int(g.size()))
				break;
			t -= d;
		}
		const auto e = estadoDoSlide(slide(), grupo, t);
		aPrevia_->setPixmap(QPixmap::fromImage(imagemSlide(doc_, slide(), aPrevia_->width(), false, &e)));
		if (grupo + 1 >= int(g.size()) && t > duracaoGrupo(g.back()) + 0.1)
			aTempo_->stop();
	}

	// --- a tabela ---
	// Uma mudança na tabela selecionada (ou na da célula em edição).
	void mudarTabela(const std::function<void(Objeto &)> &f)
	{
		const int k = editor_ && edCel_ >= 0 ? edObj_ : (umObjeto() && umObjeto()->tipo == Tipo::Tabela ? sel_[0] : -1);
		if (k < 0)
			return;
		const int cel = edCel_;
		terminarEdicao();
		guardar();
		f(obj(k));
		arrumarTabela(doc_, obj(k));
		itens_[size_t(k)]->mudou();
		atualizarSelecao();
		marcarMudanca();
		if (cel >= 0 && cel < int(obj(k).cel.size()))
			editar(k, QPointF(), false, cel);
		else
			atualizarInspetor();
	}
	// 0 linha acima, 1 linha abaixo, 2 tira a linha, 3 coluna à esquerda,
	// 4 à direita, 5 tira a coluna, 6 colunas iguais.
	void mexerTabela(int op)
	{
		const int k = editor_ && edCel_ >= 0 ? edObj_ : (umObjeto() && umObjeto()->tipo == Tipo::Tabela ? sel_[0] : -1);
		if (k < 0)
			return;
		const int cel = edCel_;
		terminarEdicao();
		guardar();
		Objeto &t = obj(k);
		const int l = cel >= 0 ? cel / t.cols : t.lins - 1, c = cel >= 0 ? cel % t.cols : t.cols - 1;
		int nl = l, nc = c;
		switch (op) {
		case 0: tabelaLinha(t, l, false); nl = l; break;
		case 1: tabelaLinha(t, l + 1, false); nl = l + 1; break;
		case 2: tabelaLinha(t, l, true); nl = std::min(l, t.lins - 1); break;
		case 3: tabelaColuna(t, c, false); nc = c; break;
		case 4: tabelaColuna(t, c + 1, false); nc = c + 1; break;
		case 5: tabelaColuna(t, c, true); nc = std::min(c, t.cols - 1); break;
		default:
			for (double &w : t.largs)
				w = t.r.width() / t.cols;
		}
		arrumarTabela(doc_, t);
		itens_[size_t(k)]->mudou();
		sel_ = {k};
		atualizarSelecao();
		marcarMudanca();
		if (cel >= 0)
			editar(k, QPointF(), false, nl * t.cols + nc);
		else
			atualizarInspetor();
	}
	void inserirTabela(int lins, int cols)
	{
		const double w = doc_.larg * 0.7;
		QRectF r(0, 0, w, lins * 36.0);
		r.moveCenter(QPointF(doc_.larg / 2, doc_.alt * 0.58));
		Objeto t = novaTabela(lins, cols, r, 18);
		for (int c = 0; c < t.cols; c++)
			t.celula(0, c).negrito = true;
		arrumarTabela(doc_, t);
		const int k = acrescentar(t);
		editar(k, QPointF(), false, 0);
	}

	// --- o vídeo ---
	static QString filtroVideos() { return "Vídeos (*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.ogv *.mpg *.mpeg *.wmv)"; }
	void escolherVideo()
	{
		const QString f = QFileDialog::getOpenFileName(this, "Inserir vídeo", QDir::homePath(), filtroVideos());
		if (!f.isEmpty())
			inserirVideo(f);
	}
	void inserirVideo(const QString &arq)
	{
		QFile f(arq);
		if (!f.open(QIODevice::ReadOnly))
			return;
		const QByteArray b = f.readAll();
		if (b.isEmpty())
			return;
		Objeto o;
		o.tipo = Tipo::Video;
		o.midia = doc_.guardarMidia(b, QFileInfo(arq).suffix().toLower());
		o.r = QRectF(0, 0, doc_.larg * 0.6, doc_.larg * 0.6 * 9 / 16);
		o.r.moveCenter(centroLivre(o.r.size()));
		acrescentar(o);
		buscarCapa(o.midia);
	}
	// A capa: o primeiro quadro do vídeo (quando chegar), nos objetos com esse vídeo.
	void buscarCapa(const QString &midia)
	{
		auto *t = new Tocador(doc_.midias.value(midia), QFileInfo(midia).suffix(), true, false, this);
		t->primeiroQuadro([this, t, midia](const QImage &img) {
			QByteArray png;
			QBuffer buf(&png);
			buf.open(QIODevice::WriteOnly);
			img.save(&buf, "PNG");
			const QString capa = doc_.guardarMidia(png, "png");
			for (Slide &s : doc_.slides)
				for (Objeto &o : s.objs)
					if (o.tipo == Tipo::Video && o.midia == midia && o.capa.isEmpty()) {
						o.capa = capa;
						// A moldura com a proporção do vídeo (largura igual).
						if (img.width() > 0) {
							const QPointF c = o.r.center();
							o.r.setHeight(o.r.width() * img.height() / img.width());
							o.r.moveCenter(c);
						}
					}
			refazerCena();
			atualizarMiniaturas();
			t->deleteLater();
		});
	}
	void trocarVideo()
	{
		const Objeto *o = umObjeto();
		if (!o || o->tipo != Tipo::Video)
			return;
		const QString f = QFileDialog::getOpenFileName(this, "Trocar o vídeo", QDir::homePath(), filtroVideos());
		if (f.isEmpty())
			return;
		QFile arq(f);
		if (!arq.open(QIODevice::ReadOnly))
			return;
		const QByteArray b = arq.readAll();
		const QString m = doc_.guardarMidia(b, QFileInfo(f).suffix().toLower());
		mudarObjetos([m](Objeto &x) {
			if (x.tipo != Tipo::Video) return;
			x.midia = m;
			x.capa.clear();
		});
		doc_.limparMidias();
		buscarCapa(m);
	}
	// Toca (ou pausa) o vídeo no próprio slide do editor.
	void previaVideo(int k)
	{
		if (k < 0 || k >= int(slide().objs.size()) || obj(k).tipo != Tipo::Video)
			return;
		auto it = tocadores_.find(k);
		if (it != tocadores_.end()) {
			it->second->alternar();
			return;
		}
		const Objeto &o = obj(k);
		auto t = std::make_unique<Tocador>(doc_.midias.value(o.midia), QFileInfo(o.midia).suffix(), o.mudo, o.repetir, this);
		t->aoQuadro = [this, k] {
			if (k < int(itens_.size()))
				itens_[size_t(k)]->update();
		};
		itens_[size_t(k)]->quadro = &t->quadro;
		t->tocar();
		tocadores_[k] = std::move(t);
	}
	void capaDoQuadro()
	{
		if (sel_.size() != 1)
			return;
		auto it = tocadores_.find(sel_[0]);
		if (it == tocadores_.end() || it->second->quadro.isNull()) {
			statusBar()->showMessage("Toque o vídeo aqui (▶) e pause no quadro que quer como capa.", 5000);
			return;
		}
		QByteArray png;
		QBuffer buf(&png);
		buf.open(QIODevice::WriteOnly);
		it->second->quadro.save(&buf, "PNG");
		const QString capa = doc_.guardarMidia(png, "png");
		mudarObjetos([capa](Objeto &o) { if (o.tipo == Tipo::Video) o.capa = capa; });
		doc_.limparMidias();
		atualizarMiniatura(atual_);
	}

	void mostrarInspetor(int i, bool alternar = false)
	{
		if (alternar && inspetor_->isVisible() && inspetor_->currentIndex() == i)
			inspetor_->hide();
		else {
			inspetor_->show();
			inspetor_->setCurrentIndex(i);
			if (i == 1)
				previa();
		}
		for (int k = 0; k < 4; k++)
			bPainel_[k]->setChecked(inspetor_->isVisible() && inspetor_->currentIndex() == k);
		if (i == 3)
			previaAnimacao();
		atualizarSelecao();
	}

	static QImage miniaturaLeiaute(Layout l)
	{
		Doc d;
		d.tema = tema("papel");
		Slide s = novoSlide(d, l);
		QImage img(96, 54, QImage::Format_ARGB32_Premultiplied);
		img.fill(d.tema.fundo);
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing);
		p.scale(96 / d.larg, 54 / d.alt);
		for (const Objeto &o : s.objs) {
			const QRectF in = interior(o);
			const double h = std::min(in.height(), o.tamanho * 1.1);
			double y = o.vAlin == 2 ? in.bottom() - h : o.vAlin == 1 ? in.center().y() - h / 2 : in.top();
			const int linhas = o.papel == Papel::Corpo || o.papel == Papel::Corpo2 ? 4 : 1;
			for (int k = 0; k < linhas; k++) {
				const double w = in.width() * (o.papel == Papel::Titulo ? 0.7 : 0.85) * (k % 2 ? 0.8 : 1);
				const double x = o.pars.front().alin == 1 ? in.center().x() - w / 2 : in.left();
				p.fillRect(QRectF(x, y, w, h * 0.7), o.papel == Papel::Titulo ? QColor(0x1c, 0x1b, 0x19) : QColor(0x9a, 0x94, 0x88));
				y += h * 1.4;
			}
		}
		p.setPen(QPen(QColor(0x1c, 0x1b, 0x19), 6));
		p.setBrush(Qt::NoBrush);
		p.drawRect(QRectF(0, 0, d.larg, d.alt));
		return img;
	}

	// --- o documento na tela ---
	Slide &slide() { return doc_.slides[size_t(atual_)]; }
	Objeto &obj(int i) { return slide().objs[size_t(i)]; }
	const Objeto *umObjeto() { return sel_.size() == 1 ? &obj(sel_[0]) : nullptr; }
	bool editando() const { return editor_ != nullptr; }
	Objeto &objetoEditado()
	{
		Objeto &o = obj(edObj_);
		return edCel_ >= 0 && edCel_ < int(o.cel.size()) ? o.cel[size_t(edCel_)] : o;
	}
	// O texto que representa um objeto no inspetor (a primeira célula da tabela).
	static const Objeto *textoDe(const Objeto *o) { return o && o->tipo == Tipo::Tabela && !o->cel.empty() ? &o->cel.front() : o; }
	void ajustarTabelas()
	{
		if (doc_.slides.empty())
			return;
		for (Objeto &o : slide().objs)
			if (o.tipo == Tipo::Tabela)
				arrumarTabela(doc_, o);
	}

	void carregar(const Doc &d)
	{
		terminarEdicao();
		doc_ = d;
		if (doc_.slides.empty())
			doc_.slides.push_back(novoSlide(doc_, Layout::Titulo));
		antes_.clear();
		depois_.clear();
		mudou_ = false;
		atual_ = 0;
		sel_.clear();
		cena_->setSceneRect(QRectF(-40, -40, doc_.larg + 80, doc_.alt + 80));
		pilha_->setCurrentIndex(1);
		barra_->show();
		atualizarNavegador();
		mostrarSlide(0);
		ajustarZoom();
		atualizarTitulo();
		mostrarInspetor(0);
		tela_->setFocus();
	}

	void atualizarTitulo()
	{
		const QString nome = arquivo_.isEmpty() ? QString("Sem título") : QFileInfo(arquivo_).fileName();
		titulo_->setText(nome + (mudou_ ? "  •" : ""));
		setWindowTitle(nome + (mudou_ ? " (modificado)" : "") + " — Palco");
	}

	void marcarMudanca(bool cena = true)
	{
		mudou_ = true;
		atualizarTitulo();
		if (cena)
			miniaturas_->start(120);
	}

	void ajustarZoom()
	{
		if (!tela_ || doc_.slides.empty())
			return;
		const int z = zoom_ ? zoom_->currentIndex() : 0;
		if (z <= 0) {
			const QRectF r(-24, -24, doc_.larg + 48, doc_.alt + 48);
			tela_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			tela_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			tela_->fitInView(r, Qt::KeepAspectRatio);
		} else {
			static const double fatores[] = {1, 0.5, 0.75, 1.0, 1.5, 2.0};
			tela_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
			tela_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
			// 100% = o slide com 96 pontos por polegada de tela (1 pt = 4/3 px).
			const double k = fatores[z] * 96.0 / 72.0;
			tela_->setTransform(QTransform::fromScale(k, k));
		}
		selecao_->update();
	}

	void atualizarNavegador()
	{
		ajustando_ = true;
		nav_->clear();
		for (size_t i = 0; i < doc_.slides.size(); i++) {
			auto *it = new QListWidgetItem;
			it->setData(Qt::UserRole, int(i));
			nav_->addItem(it);
		}
		atualizarMiniaturas();
		nav_->setCurrentRow(atual_);
		ajustando_ = false;
	}

	QIcon iconeSlide(int i)
	{
		const Slide &s = doc_.slides[size_t(i)];
		QImage img = imagemSlide(doc_, s, 320);
		QImage moldura(img.width() + 44, img.height() + 8, QImage::Format_ARGB32_Premultiplied);
		moldura.fill(Qt::transparent);
		QPainter p(&moldura);
		p.setRenderHint(QPainter::Antialiasing);
		QFont f("IBM Plex Mono");
		f.setPixelSize(20);
		p.setFont(f);
		p.setPen(cores_.apagado);
		p.drawText(QRect(0, 0, 38, 36), Qt::AlignRight | Qt::AlignVCenter, QString::number(i + 1));
		if (s.pular) {
			p.setOpacity(0.35);
		}
		p.drawImage(40, 4, img);
		p.setOpacity(1);
		p.setPen(QPen(cores_.fio, 2));
		p.drawRect(QRectF(40, 4, img.width(), img.height()));
		if (s.transicao != Transicao::Nenhuma) {
			p.setPen(cores_.apagado);
			p.drawText(QRect(0, img.height() - 30, 38, 30), Qt::AlignRight | Qt::AlignVCenter, "◆");
		}
		// O mesmo desenho escolhido ou não (a moldura azul é do item).
		QIcon ic;
		const QPixmap px = QPixmap::fromImage(moldura);
		ic.addPixmap(px, QIcon::Normal);
		ic.addPixmap(px, QIcon::Selected);
		return ic;
	}

	void atualizarMiniaturas()
	{
		nav_->setIconSize(QSize(204, int(160 * doc_.alt / doc_.larg) + 8));
		for (int i = 0; i < nav_->count() && i < int(doc_.slides.size()); i++)
			nav_->item(i)->setIcon(iconeSlide(i));
	}

	void atualizarMiniatura(int i)
	{
		if (i >= 0 && i < nav_->count() && i < int(doc_.slides.size()))
			nav_->item(i)->setIcon(iconeSlide(i));
	}

	void mostrarSlide(int i)
	{
		terminarEdicao();
		if (i < 0 || i >= int(doc_.slides.size()))
			return;
		if (miniaturas_->isActive()) {
			miniaturas_->stop();
			atualizarMiniatura(atual_);
		}
		atual_ = i;
		sel_.clear();
		refazerCena();
		ajustando_ = true;
		nav_->setCurrentRow(i);
		notas_->setPlainText(slide().notas);
		ajustando_ = false;
		atualizarInspetor();
	}

	void refazerCena()
	{
		if (editor_)
			terminarEdicao();
		ajustarTabelas();
		itens_.clear();
		for (QGraphicsItem *it : cena_->items())
			if (it != selecao_) {
				cena_->removeItem(it);
				delete it;
			}
		if (!selecao_->scene())
			cena_->addItem(selecao_);
		auto *f = new ItemFundo(&doc_, &atual_);
		f->setZValue(-1);
		cena_->addItem(f);
		for (size_t k = 0; k < slide().objs.size(); k++) {
			auto *it = new ItemObjeto(&doc_, &atual_, int(k));
			it->setZValue(double(k));
			cena_->addItem(it);
			itens_.push_back(it);
		}
		// Os vídeos tocando no editor param (os objetos mudaram).
		tocadores_.clear();
		sel_.erase(std::remove_if(sel_.begin(), sel_.end(), [this](int k) { return k >= int(slide().objs.size()); }), sel_.end());
		atualizarSelecao();
		cena_->update();
	}

	void atualizarSelecao()
	{
		selecao_->caixas.clear();
		for (int k : sel_) {
			const Objeto &o = obj(k);
			const int linha = o.tipo == Tipo::Linha ? (o.viraV != o.viraH ? 2 : 1) : 0;
			selecao_->caixas.push_back({o.r, o.giro, linha, sel_.size() == 1 && !editando(), o.tipo != Tipo::Tabela});
		}
		// Com o painel Animar aberto, o número de cada animação no objeto.
		selecao_->marcas.clear();
		if (inspetor_ && inspetor_->isVisible() && inspetor_->currentIndex() == 3) {
			const auto l = listaAnimacoes();
			QMap<int, QStringList> por;
			for (size_t i = 0; i < l.size(); i++)
				por[l[i].first] << QString::number(i + 1);
			for (auto it = por.cbegin(); it != por.cend(); ++it)
				selecao_->marcas.push_back({obj(it.key()).r.topLeft(), it.value().join("·")});
		}
		selecao_->update();
		cena_->update();
	}

	// --- as mudanças (com desfazer) ---
	void guardar()
	{
		antes_.push_back({doc_, atual_});
		if (antes_.size() > 100)
			antes_.erase(antes_.begin());
		depois_.clear();
	}

	void desfazer()
	{
		if (antes_.empty())
			return;
		depois_.push_back({doc_, atual_});
		restaurar(antes_.back());
		antes_.pop_back();
	}

	void refazer()
	{
		if (depois_.empty())
			return;
		antes_.push_back({doc_, atual_});
		restaurar(depois_.back());
		depois_.pop_back();
	}

	void restaurar(const std::pair<Doc, int> &e)
	{
		terminarEdicao();
		const bool mesmoTamanho = e.first.slides.size() == doc_.slides.size();
		doc_ = e.first;
		atual_ = std::clamp(e.second, 0, int(doc_.slides.size()) - 1);
		sel_.clear();
		cena_->setSceneRect(QRectF(-40, -40, doc_.larg + 80, doc_.alt + 80));
		if (mesmoTamanho)
			atualizarMiniaturas();
		else
			atualizarNavegador();
		mostrarSlide(atual_);
		marcarMudanca(false);
	}

	// Uma mudança no texto: nos objetos com texto e nas células das tabelas.
	void mudarTextos(const std::function<void(Objeto &)> &f)
	{
		mudarObjetos([&f](Objeto &o) {
			if (o.tipo == Tipo::Tabela)
				for (Objeto &c : o.cel)
					f(c);
			else
				f(o);
		});
	}

	void mudarObjetos(const std::function<void(Objeto &)> &f)
	{
		if (sel_.empty())
			return;
		guardar();
		for (int k : sel_) {
			f(obj(k));
			if (obj(k).tipo == Tipo::Tabela)
				arrumarTabela(doc_, obj(k));
		}
		for (int k : sel_)
			itens_[size_t(k)]->mudou();
		if (editor_) {
			const int e = edObj_, c = edCel_;
			terminarEdicao();
			editar(e, QPointF(), false, c);
		}
		atualizarSelecao();
		atualizarInspetor();
		marcarMudanca();
	}

	// --- seleção e mouse ---
	static QPointF paraLocal(const Objeto &o, QPointF p)
	{
		if (o.giro == 0)
			return p;
		QTransform t;
		t.translate(o.r.center().x(), o.r.center().y());
		t.rotate(-o.giro);
		t.translate(-o.r.center().x(), -o.r.center().y());
		return t.map(p);
	}
	static QPointF paraCena(const Objeto &o, QPointF p)
	{
		if (o.giro == 0)
			return p;
		QTransform t;
		t.translate(o.r.center().x(), o.r.center().y());
		t.rotate(o.giro);
		t.translate(-o.r.center().x(), -o.r.center().y());
		return t.map(p);
	}

	double px1() const { return 1 / std::max(0.01, tela_->transform().m11()); }

	int objetoEm(QPointF p)
	{
		const double tol = 5 * px1();
		for (int k = int(slide().objs.size()) - 1; k >= 0; k--) {
			const Objeto &o = obj(k);
			const QPointF l = paraLocal(o, p);
			if (o.tipo == Tipo::Linha) {
				QPointF a = o.r.topLeft(), b = o.r.bottomRight();
				if (o.viraV != o.viraH) {
					a = o.r.bottomLeft();
					b = o.r.topRight();
				}
				const QPointF ab = b - a;
				const double len2 = QPointF::dotProduct(ab, ab);
				const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(l - a, ab) / len2, 0.0, 1.0) : 0;
				const QPointF q = a + t * ab;
				if (QLineF(q, l).length() <= tol + o.espBorda)
					return k;
				continue;
			}
			if (o.r.adjusted(-tol, -tol, tol, tol).contains(l))
				return k;
		}
		return -1;
	}

	// Qual alça está sob o ponto: 0-7 as de tamanho, 8 a de girar, -1 nenhuma.
	int alcaEm(QPointF p)
	{
		if (sel_.size() != 1 || editando())
			return -1;
		const Objeto &o = obj(sel_[0]);
		const double tol = 7 * px1();
		const QPointF l = paraLocal(o, p);
		if (o.tipo == Tipo::Linha) {
			const auto pts = ItemSelecao::pontos(o.r, o.viraV != o.viraH ? 2 : 1);
			for (int k = 0; k < 2; k++)
				if (QLineF(pts[size_t(k)], l).length() <= tol)
					return 10 + k;
			return -1;
		}
		if (o.tipo != Tipo::Tabela && QLineF(QPointF(o.r.center().x(), o.r.top() - 22 * px1()), l).length() <= tol)
			return 8;
		const auto pts = ItemSelecao::pontos(o.r, 0);
		for (int k = 0; k < 8; k++)
			if (QLineF(pts[size_t(k)], l).length() <= tol)
				return k;
		// Na tabela: os fios entre as colunas (20+) e entre as linhas (60+).
		if (o.tipo == Tipo::Tabela && o.r.contains(l) && int(o.cel.size()) == o.lins * o.cols) {
			for (int c = 1; c < o.cols; c++)
				if (std::fabs(l.x() - o.celula(0, c).r.left()) <= tol * 0.6)
					return 20 + c;
			for (int r = 1; r < o.lins; r++)
				if (std::fabs(l.y() - o.celula(r, 0).r.top()) <= tol * 0.6)
					return 60 + r;
		}
		return -1;
	}

	bool apertar(QMouseEvent *e, QPointF p)
	{
		if (editor_) {
			// Dentro do texto em edição: o próprio editor cuida.
			const Objeto &o = objetoEditado();
			if (o.r.adjusted(-4, -4, 4, 4).contains(paraLocal(o, p)))
				return false;
			// Outra célula da mesma tabela: escreve nela.
			const int k0 = edObj_;
			const bool eraCelula = edCel_ >= 0;
			terminarEdicao();
			if (eraCelula && e->button() == Qt::LeftButton && k0 < int(slide().objs.size()) && obj(k0).r.contains(p)) {
				editar(k0, p);
				return true;
			}
		}
		if (e->button() != Qt::LeftButton) {
			const int k = objetoEm(p);
			if (k >= 0 && std::find(sel_.begin(), sel_.end(), k) == sel_.end()) {
				sel_ = {k};
				atualizarSelecao();
				atualizarInspetor();
			}
			return true;
		}
		arrasto_ = Arrasto();
		arrasto_.inicio = p;
		const int alca = alcaEm(p);
		if (alca >= 0) {
			arrasto_.modo = alca == 8 ? Arrasto::Girar : alca >= 60 ? Arrasto::LinhaTab : alca >= 20 ? Arrasto::Coluna : alca >= 10 ? Arrasto::Ponta : Arrasto::Tamanho;
			arrasto_.alca = alca;
			arrasto_.originais = {obj(sel_[0])};
			guardar();
			return true;
		}
		const int k = objetoEm(p);
		if (k < 0) {
			if (!(e->modifiers() & Qt::ShiftModifier))
				sel_.clear();
			arrasto_.modo = Arrasto::Banda;
			atualizarSelecao();
			atualizarInspetor();
			return true;
		}
		const bool ja = std::find(sel_.begin(), sel_.end(), k) != sel_.end();
		if (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
			if (ja)
				sel_.erase(std::find(sel_.begin(), sel_.end(), k));
			else
				sel_.push_back(k);
			atualizarSelecao();
			atualizarInspetor();
			return true;
		}
		if (!ja)
			sel_ = {k};
		arrasto_.modo = Arrasto::Mover;
		for (int s : sel_)
			arrasto_.originais.push_back(obj(s));
		atualizarSelecao();
		atualizarInspetor();
		return true;
	}

	// As guias: as bordas e o meio do slide e dos outros objetos.
	QPointF encaixar(const QRectF &r, std::vector<QLineF> &guias)
	{
		const double tol = 6 * px1();
		std::vector<double> xs{0, doc_.larg / 2, doc_.larg}, ys{0, doc_.alt / 2, doc_.alt};
		for (int k = 0; k < int(slide().objs.size()); k++) {
			if (std::find(sel_.begin(), sel_.end(), k) != sel_.end())
				continue;
			const QRectF o = obj(k).r;
			xs.insert(xs.end(), {o.left(), o.center().x(), o.right()});
			ys.insert(ys.end(), {o.top(), o.center().y(), o.bottom()});
		}
		double dx = 0, dy = 0, mx = tol + 1, my = tol + 1, gx = 0, gy = 0;
		for (double a : {r.left(), r.center().x(), r.right()})
			for (double x : xs)
				if (std::fabs(x - a) < mx) {
					mx = std::fabs(x - a);
					dx = x - a;
					gx = x;
				}
		for (double a : {r.top(), r.center().y(), r.bottom()})
			for (double y : ys)
				if (std::fabs(y - a) < my) {
					my = std::fabs(y - a);
					dy = y - a;
					gy = y;
				}
		if (mx > tol) dx = 0;
		else guias.push_back(QLineF(gx, -20, gx, doc_.alt + 20));
		if (my > tol) dy = 0;
		else guias.push_back(QLineF(-20, gy, doc_.larg + 20, gy));
		return QPointF(dx, dy);
	}

	bool mover(QMouseEvent *e, QPointF p)
	{
		if (editor_)
			return false;
		if (!(e->buttons() & Qt::LeftButton) || arrasto_.modo == Arrasto::Nada) {
			// O cursor conforme o que está embaixo.
			const int a = alcaEm(p);
			if (a == 8) tela_->viewport()->setCursor(Qt::CrossCursor);
			else if (a >= 60) tela_->viewport()->setCursor(Qt::SplitVCursor);
			else if (a >= 20) tela_->viewport()->setCursor(Qt::SplitHCursor);
			else if (a >= 10) tela_->viewport()->setCursor(Qt::SizeAllCursor);
			else if (a >= 0) {
				static const Qt::CursorShape c[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
				                                    Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
				tela_->viewport()->setCursor(c[a]);
			} else if (objetoEm(p) >= 0)
				tela_->viewport()->setCursor(Qt::SizeAllCursor);
			else
				tela_->viewport()->unsetCursor();
			return true;
		}
		QPointF d = p - arrasto_.inicio;
		selecao_->guias.clear();
		switch (arrasto_.modo) {
		case Arrasto::Mover: {
			if (!arrasto_.mexeu && QLineF(QPointF(), d).length() < 3 * px1())
				return true;
			if (!arrasto_.mexeu) {
				guardar();
				arrasto_.mexeu = true;
			}
			if (e->modifiers() & Qt::ShiftModifier) {
				if (std::fabs(d.x()) > std::fabs(d.y())) d.setY(0);
				else d.setX(0);
			}
			QRectF caixa;
			for (const Objeto &o : arrasto_.originais)
				caixa = caixa.isNull() ? o.r : caixa.united(o.r);
			if (!(e->modifiers() & Qt::AltModifier))
				d += encaixar(caixa.translated(d), selecao_->guias);
			for (size_t k = 0; k < sel_.size(); k++) {
				obj(sel_[k]).r = arrasto_.originais[k].r.translated(d);
				itens_[size_t(sel_[k])]->mudou();
			}
			break;
		}
		case Arrasto::Tamanho: {
			const Objeto &o0 = arrasto_.originais[0];
			Objeto &o = obj(sel_[0]);
			const QPointF l = paraLocal(o0, p);
			QRectF r = o0.r;
			const int a = arrasto_.alca;
			if (a == 0 || a == 6 || a == 7) r.setLeft(std::min(l.x(), r.right() - 4));
			if (a == 2 || a == 3 || a == 4) r.setRight(std::max(l.x(), r.left() + 4));
			if (a == 0 || a == 1 || a == 2) r.setTop(std::min(l.y(), r.bottom() - 4));
			if (a == 4 || a == 5 || a == 6) r.setBottom(std::max(l.y(), r.top() + 4));
			// As imagens (ou com Shift) mantêm a proporção pelos cantos.
			const bool canto = a % 2 == 0;
			if (canto && ((o0.tipo == Tipo::Imagem) != bool(e->modifiers() & Qt::ShiftModifier)) && o0.r.height() > 0) {
				const double prop = o0.r.width() / o0.r.height();
				if (r.width() / prop > r.height()) {
					const double h = r.width() / prop;
					if (a == 0 || a == 2) r.setTop(r.bottom() - h); else r.setBottom(r.top() + h);
				} else {
					const double w = r.height() * prop;
					if (a == 0 || a == 6) r.setLeft(r.right() - w); else r.setRight(r.left() + w);
				}
			}
			if (o0.giro == 0 && !(e->modifiers() & Qt::AltModifier)) {
				// As guias também valem para a borda que se mexe.
				std::vector<QLineF> g;
				const QPointF s = encaixar(r, g);
				if (a == 0 || a == 6 || a == 7) r.setLeft(r.left() + s.x());
				else if (a == 2 || a == 3 || a == 4) r.setRight(r.right() + s.x());
				if (a == 0 || a == 1 || a == 2) r.setTop(r.top() + s.y());
				else if (a == 4 || a == 5 || a == 6) r.setBottom(r.bottom() + s.y());
				selecao_->guias = g;
			}
			// Girado: o centro novo também gira.
			const QPointF c = paraCena(o0, r.center());
			r.moveCenter(c);
			o.r = r;
			if (o0.tipo == Tipo::Tabela && o0.r.width() > 0 && o0.r.height() > 0 && int(o0.cel.size()) == o0.lins * o0.cols) {
				// A tabela: as colunas e as linhas na mesma proporção.
				const double kx = r.width() / o0.r.width(), ky = r.height() / o0.r.height();
				for (int i = 0; i < o.cols; i++)
					o.largs[size_t(i)] = o0.largs[size_t(i)] * kx;
				for (int i = 0; i < o.lins; i++)
					o.alts[size_t(i)] = std::max(8.0, o0.celula(i, 0).r.height() * ky);
				arrumarTabela(doc_, o);
			}
			itens_[size_t(sel_[0])]->mudou();
			break;
		}
		case Arrasto::Coluna: {
			Objeto &o = obj(sel_[0]);
			const int i = arrasto_.alca - 20;
			const double esq = o.celula(0, i - 1).r.left(), dir = o.celula(0, i).r.right();
			const double x = std::clamp(p.x(), esq + 16, dir - 16);
			o.largs[size_t(i - 1)] = x - esq;
			o.largs[size_t(i)] = dir - x;
			arrumarTabela(doc_, o);
			itens_[size_t(sel_[0])]->mudou();
			break;
		}
		case Arrasto::LinhaTab: {
			Objeto &o = obj(sel_[0]);
			const int i = arrasto_.alca - 60;
			const double topo = o.celula(i - 1, 0).r.top();
			o.alts[size_t(i - 1)] = std::max(10.0, p.y() - topo);
			arrumarTabela(doc_, o);
			itens_[size_t(sel_[0])]->mudou();
			break;
		}
		case Arrasto::Girar: {
			Objeto &o = obj(sel_[0]);
			const QPointF c = o.r.center();
			double ang = std::atan2(p.y() - c.y(), p.x() - c.x()) * 180 / M_PI + 90;
			if (e->modifiers() & Qt::ShiftModifier)
				ang = std::round(ang / 15) * 15;
			else
				for (double m : {0.0, 90.0, 180.0, 270.0, 360.0, -90.0})
					if (std::fabs(ang - m) < 3)
						ang = m;
			o.giro = std::fmod(ang + 720, 360);
			itens_[size_t(sel_[0])]->mudou();
			break;
		}
		case Arrasto::Ponta: {
			Objeto &o = obj(sel_[0]);
			const Objeto &o0 = arrasto_.originais[0];
			const auto pts = ItemSelecao::pontos(o0.r, o0.viraV != o0.viraH ? 2 : 1);
			QPointF fixo = pts[size_t(arrasto_.alca == 10 ? 1 : 0)];
			QPointF solto = p;
			if (e->modifiers() & Qt::ShiftModifier) {
				const double ang = std::round(std::atan2(solto.y() - fixo.y(), solto.x() - fixo.x()) / (M_PI / 4)) * (M_PI / 4);
				const double len = QLineF(fixo, solto).length();
				solto = fixo + QPointF(std::cos(ang) * len, std::sin(ang) * len);
			}
			o.r = QRectF(fixo, solto).normalized();
			o.viraH = false;
			o.viraV = (solto.x() - fixo.x()) * (solto.y() - fixo.y()) < 0;
			itens_[size_t(sel_[0])]->mudou();
			break;
		}
		case Arrasto::Banda: {
			selecao_->banda = QRectF(arrasto_.inicio, p).normalized();
			std::vector<int> s;
			for (int k = 0; k < int(slide().objs.size()); k++)
				if (selecao_->banda.intersects(obj(k).r))
					s.push_back(k);
			sel_ = s;
			break;
		}
		default:
			break;
		}
		atualizarSelecao();
		return true;
	}

	bool soltar(QMouseEvent *, QPointF)
	{
		if (editor_)
			return false;
		const bool mudou = (arrasto_.modo == Arrasto::Mover && arrasto_.mexeu) || arrasto_.modo == Arrasto::Tamanho ||
		                   arrasto_.modo == Arrasto::Girar || arrasto_.modo == Arrasto::Ponta || arrasto_.modo == Arrasto::Coluna ||
		                   arrasto_.modo == Arrasto::LinhaTab;
		arrasto_.modo = Arrasto::Nada;
		selecao_->guias.clear();
		selecao_->banda = QRectF();
		atualizarSelecao();
		atualizarInspetor();
		if (mudou)
			marcarMudanca();
		return true;
	}

	bool duplo(QMouseEvent *e, QPointF p)
	{
		if (editor_)
			return false;
		const int k = objetoEm(p);
		if (k < 0)
			return true;
		if (obj(k).aceitaTexto() || obj(k).tipo == Tipo::Tabela)
			editar(k, p);
		else if (obj(k).tipo == Tipo::Video)
			previaVideo(k);
		else if (obj(k).tipo == Tipo::Grafico) {
			mostrarInspetor(0);
			dados_->setFocus();
		} else if (obj(k).tipo == Tipo::Imagem)
			trocarImagem();
		Q_UNUSED(e);
		return true;
	}

	// --- o texto ---
	// A célula da tabela sob um ponto (-1: nenhuma).
	static int celulaEm(const Objeto &t, QPointF p)
	{
		for (int i = 0; i < int(t.cel.size()); i++)
			if (t.cel[size_t(i)].r.contains(p))
				return i;
		return -1;
	}

	void editar(int k, QPointF onde = QPointF(), bool tudo = false, int cel = -1)
	{
		terminarEdicao();
		if (k < 0 || k >= int(slide().objs.size()))
			return;
		if (obj(k).tipo == Tipo::Tabela) {
			Objeto &t = obj(k);
			arrumarTabela(doc_, t);
			if (cel < 0 && !onde.isNull())
				cel = celulaEm(t, onde);
			if (cel < 0 || cel >= int(t.cel.size()))
				cel = 0;
		} else {
			cel = -1;
			if (!obj(k).aceitaTexto())
				return;
		}
		antesEdicao_ = doc_;
		edObj_ = k;
		edCel_ = cel;
		sel_ = {k};
		Objeto &o = objetoEditado();
		edEscala_ = escalaQueCabe(doc_, o);
		editor_ = new EditorTexto;
		if (o.pars.empty())
			o.pars.push_back(Paragrafo());
		montarDocumento(*editor_->document(), doc_, o, edEscala_);
		editor_->setTextWidth(interior(o).width() * K);
		editor_->setDefaultTextColor(corTexto(doc_, o));
		editor_->setTextInteractionFlags(Qt::TextEditorInteraction);
		editor_->setZValue(500);
		editor_->aoSair = [this] { QTimer::singleShot(0, this, [this] { terminarEdicao(); }); };
		editor_->aoMexer = [this] { atualizarInspetor(); };
		editor_->aoNivel = [this](int d) {
			if (edCel_ >= 0)
				irCelula(d);
			else
				nivel(d);
		};
		connect(editor_->document(), &QTextDocument::contentsChanged, this, [this] {
			if (edCel_ >= 0)
				crescerCelula();
			posicionarEditor();
		});
		cena_->addItem(editor_);
		itens_[size_t(k)]->semTexto = cel < 0;
		itens_[size_t(k)]->celula = cel;
		itens_[size_t(k)]->update();
		posicionarEditor();
		tela_->editando = true;
		editor_->setFocus();
		QTextCursor c(editor_->document());
		if (tudo)
			c.select(QTextCursor::Document);
		else if (!onde.isNull()) {
			const QPointF l = editor_->mapFromScene(onde);
			const int pos = editor_->document()->documentLayout()->hitTest(l, Qt::FuzzyHit);
			c.setPosition(std::max(0, pos));
		} else
			c.movePosition(QTextCursor::End);
		editor_->setTextCursor(c);
		atualizarSelecao();
		atualizarInspetor();
	}

	void posicionarEditor()
	{
		if (!editor_)
			return;
		const Objeto &o = objetoEditado();
		const QRectF in = interior(o);
		const double h = editor_->document()->size().height() / K;
		double y = in.top();
		if (o.vAlin == 1) y = in.top() + (in.height() - h) / 2;
		else if (o.vAlin == 2) y = in.bottom() - h;
		QTransform t;
		t.translate(o.r.center().x(), o.r.center().y());
		t.rotate(o.giro);
		t.translate(-o.r.center().x(), -o.r.center().y());
		t.translate(in.left(), y);
		t.scale(1 / K, 1 / K);
		editor_->setTransform(t);
	}

	void terminarEdicao()
	{
		if (!editor_)
			return;
		EditorTexto *ed = editor_;
		editor_ = nullptr;
		tela_->editando = false;
		const int k = edObj_, cel = edCel_;
		edObj_ = -1;
		edCel_ = -1;
		ed->disconnect(this);
		ed->document()->disconnect(this);
		if (k >= 0 && k < int(slide().objs.size())) {
			Objeto &dono = obj(k);
			const bool ehCel = cel >= 0 && cel < int(dono.cel.size());
			Objeto &o = ehCel ? dono.cel[size_t(cel)] : dono;
			std::vector<Paragrafo> novo = lerDocumento(*ed->document(), doc_, o, edEscala_);
			const QString antes = Objeto(o).texto();
			o.pars = novo;
			bool igual = antes == o.texto();
			if (igual) {
				// O texto é o mesmo; a letra pode ter mudado.
				const Objeto &velhoDono = antesEdicao_.slides[size_t(atual_)].objs[size_t(k)];
				const Objeto &velho = ehCel && cel < int(velhoDono.cel.size()) ? velhoDono.cel[size_t(cel)] : velhoDono;
				igual = resumoDe(velho) == resumoDe(o);
			}
			if (ehCel)
				arrumarTabela(doc_, dono);
			if (!igual) {
				antes_.push_back({antesEdicao_, atual_});
				if (antes_.size() > 100)
					antes_.erase(antes_.begin());
				depois_.clear();
				marcarMudanca();
			}
			// Uma caixa de texto livre que ficou vazia some.
			if (!ehCel && o.tipo == Tipo::Texto && o.papel == Papel::Livre && !o.temTexto()) {
				slide().objs.erase(slide().objs.begin() + k);
				sel_.clear();
				cena_->removeItem(ed);
				delete ed;
				refazerCena();
				atualizarInspetor();
				return;
			}
			itens_[size_t(k)]->semTexto = false;
			itens_[size_t(k)]->celula = -1;
			itens_[size_t(k)]->mudou();
		}
		cena_->removeItem(ed);
		ed->deleteLater();
		atualizarSelecao();
		atualizarInspetor();
		tela_->setFocus();
	}

	// A linha da célula em edição cresce com o texto (a tabela toda se arruma).
	void crescerCelula()
	{
		if (!editor_ || edCel_ < 0)
			return;
		Objeto &t = obj(edObj_);
		Objeto &c = t.cel[size_t(edCel_)];
		c.pars = lerDocumento(*editor_->document(), doc_, c, edEscala_);
		arrumarTabela(doc_, t);
		itens_[size_t(edObj_)]->mudou();
		atualizarSelecao();
	}

	// Tab: a próxima célula (Shift+Tab, a anterior); no fim, uma linha nova.
	void irCelula(int d)
	{
		const int k = edObj_;
		int c = edCel_ + d;
		terminarEdicao();
		if (k < 0)
			return;
		Objeto &t = obj(k);
		if (c >= int(t.cel.size())) {
			guardar();
			tabelaLinha(t, t.lins, false);
			arrumarTabela(doc_, t);
			itens_[size_t(k)]->mudou();
			marcarMudanca();
		}
		c = std::clamp(c, 0, int(t.cel.size()) - 1);
		editar(k, QPointF(), true, c);
	}

	static QString resumoDe(const Objeto &o)
	{
		Doc d;
		Slide s;
		s.objs.push_back(o);
		d.slides.push_back(s);
		return resumo(d);
	}

	// A letra no editor: na seleção; sem seleção, na palavra (ou no texto
	// todo) e também no que se digitar daqui em diante.
	void aplicarNoEditor(const QTextCharFormat &f, QTextCursor::SelectionType semSelecao)
	{
		QTextCursor c = editor_->textCursor();
		if (c.hasSelection()) {
			c.mergeCharFormat(f);
			editor_->setTextCursor(c);
			return;
		}
		QTextCursor p = c;
		p.select(semSelecao);
		if (p.hasSelection())
			p.mergeCharFormat(f);
		c.mergeCharFormat(f);
		editor_->setTextCursor(c);
	}

	// Aplica uma mudança de letra: no texto selecionado (editando) ou no objeto todo.
	void mudarLetra(const std::function<void(Trecho &, Objeto &, bool)> &noObjeto, const std::function<void(QTextCharFormat &)> &noEditor)
	{
		if (atualizando_)
			return;
		if (editor_) {
			QTextCursor c = editor_->textCursor();
			QTextCharFormat f;
			noEditor(f);
			aplicarNoEditor(f, QTextCursor::WordUnderCursor);
			atualizarInspetor();
			return;
		}
		mudarTextos([&](Objeto &o) {
			if (!o.aceitaTexto()) return;
			for (Paragrafo &p : o.pars)
				for (Trecho &t : p.trechos)
					noObjeto(t, o, true);
			Trecho t;
			noObjeto(t, o, true);
		});
	}

	void mudarTamanho(double v)
	{
		if (editor_) {
			QTextCursor c = editor_->textCursor();
			QTextCharFormat f;
			QFont fn;
			fn.setPixelSize(std::max(1, int(std::lround(v * edEscala_ * K))));
			f.setFont(fn, QTextCharFormat::FontPropertiesSpecifiedOnly);
			Q_UNUSED(c);
			aplicarNoEditor(f, QTextCursor::Document);
			posicionarEditor();
			return;
		}
		mudarTextos([v](Objeto &o) {
			if (!o.aceitaTexto()) return;
			// Os trechos com tamanho próprio mudam na mesma proporção.
			const double k = v / std::max(1.0, o.tamanho);
			for (Paragrafo &p : o.pars)
				for (Trecho &t : p.trechos)
					if (t.tamanho > 0) t.tamanho = std::round(t.tamanho * k * 2) / 2;
			o.tamanho = v;
		});
	}

	void alternarLetra(int qual)
	{
		if (pilha_->currentIndex() != 1)
			return;
		if (editor_) {
			const QTextCharFormat atual = editor_->textCursor().charFormat();
			QTextCharFormat f;
			if (qual == 0) f.setFontWeight(atual.fontWeight() >= QFont::DemiBold ? QFont::Normal : QFont::Bold);
			if (qual == 1) f.setFontItalic(!atual.fontItalic());
			if (qual == 2) f.setFontUnderline(!atual.fontUnderline());
			aplicarNoEditor(f, QTextCursor::WordUnderCursor);
			atualizarInspetor();
			return;
		}
		const Objeto *o = textoDe(umObjeto());
		if (!o && sel_.empty())
			return;
		bool liga = true;
		if (o) {
			bool todos = true;
			for (const Paragrafo &p : o->pars)
				for (const Trecho &t : p.trechos)
					todos = todos && (qual == 0 ? (t.negrito || o->negrito) : qual == 1 ? (t.italico || o->italico) : t.sublinhado);
			liga = !todos || !o->temTexto();
		}
		mudarTextos([qual, liga](Objeto &x) {
			if (!x.aceitaTexto()) return;
			if (qual == 0) x.negrito = liga;
			if (qual == 1) x.italico = liga;
			for (Paragrafo &p : x.pars)
				for (Trecho &t : p.trechos) {
					if (qual == 0) t.negrito = false;
					if (qual == 1) t.italico = false;
					if (qual == 2) t.sublinhado = liga;
				}
		});
	}

	// Os blocos selecionados no editor (ou todos).
	template <typename F> void blocosDoEditor(F f)
	{
		QTextCursor c = editor_->textCursor();
		QTextBlock b = editor_->document()->findBlock(c.selectionStart());
		const QTextBlock fim = editor_->document()->findBlock(c.selectionEnd());
		while (b.isValid()) {
			f(b);
			if (b == fim)
				break;
			b = b.next();
		}
	}

	void alinhar(int a)
	{
		if (editor_) {
			const Qt::Alignment al = a == 1 ? Qt::AlignHCenter : a == 2 ? Qt::AlignRight : a == 3 ? Qt::AlignJustify : Qt::AlignLeft;
			blocosDoEditor([al](QTextBlock &b) {
				QTextCursor c(b);
				QTextBlockFormat f = b.blockFormat();
				f.setAlignment(al);
				c.setBlockFormat(f);
			});
			atualizarInspetor();
			return;
		}
		mudarTextos([a](Objeto &o) {
			for (Paragrafo &p : o.pars)
				p.alin = a;
		});
	}

	void alinharVertical(int a)
	{
		if (editor_ && edCel_ >= 0) {
			// Só a célula em edição.
			const int k = edObj_, c = edCel_;
			terminarEdicao();
			guardar();
			obj(k).cel[size_t(c)].vAlin = a;
			itens_[size_t(k)]->mudou();
			marcarMudanca();
			editar(k, QPointF(), false, c);
			return;
		}
		mudarTextos([a](Objeto &o) { o.vAlin = a; });
	}

	void listar(int tipo)
	{
		// tipo: 1 marcadores, 2 números (de novo: tira).
		if (editor_) {
			std::vector<Paragrafo> pars = lerDocumento(*editor_->document(), doc_, objetoEditado(), edEscala_);
			QTextCursor c = editor_->textCursor();
			const int b0 = editor_->document()->findBlock(c.selectionStart()).blockNumber();
			const int b1 = editor_->document()->findBlock(c.selectionEnd()).blockNumber();
			bool ja = true;
			for (int i = b0; i <= b1 && i < int(pars.size()); i++)
				ja = ja && (tipo == 1 ? pars[size_t(i)].marcador : pars[size_t(i)].numerado);
			for (int i = b0; i <= b1 && i < int(pars.size()); i++) {
				pars[size_t(i)].marcador = !ja && tipo == 1;
				pars[size_t(i)].numerado = !ja && tipo == 2;
			}
			reescreverEditor(pars, b0, b1);
			return;
		}
		const Objeto *o = textoDe(umObjeto());
		bool ja = o != nullptr;
		if (o)
			for (const Paragrafo &p : o->pars)
				ja = ja && (tipo == 1 ? p.marcador : p.numerado);
		mudarTextos([tipo, ja](Objeto &x) {
			if (!x.aceitaTexto()) return;
			for (Paragrafo &p : x.pars) {
				p.marcador = !ja && tipo == 1;
				p.numerado = !ja && tipo == 2;
			}
		});
	}

	void nivel(int d)
	{
		if (editor_) {
			std::vector<Paragrafo> pars = lerDocumento(*editor_->document(), doc_, objetoEditado(), edEscala_);
			QTextCursor c = editor_->textCursor();
			const int b0 = editor_->document()->findBlock(c.selectionStart()).blockNumber();
			const int b1 = editor_->document()->findBlock(c.selectionEnd()).blockNumber();
			for (int i = b0; i <= b1 && i < int(pars.size()); i++)
				pars[size_t(i)].nivel = std::clamp(pars[size_t(i)].nivel + d, 0, 4);
			reescreverEditor(pars, b0, b1);
			return;
		}
		mudarTextos([d](Objeto &o) {
			for (Paragrafo &p : o.pars)
				p.nivel = std::clamp(p.nivel + d, 0, 4);
		});
	}

	// Monta o editor de novo com os parágrafos mudados (e o cursor no lugar).
	void reescreverEditor(const std::vector<Paragrafo> &pars, int b0, int b1)
	{
		Objeto o = objetoEditado();
		const int pos = editor_->textCursor().position();
		const int ini = editor_->textCursor().selectionStart(), fim = editor_->textCursor().selectionEnd();
		o.pars = pars;
		montarDocumento(*editor_->document(), doc_, o, edEscala_);
		QTextCursor c(editor_->document());
		if (b1 > b0 || ini != fim) {
			c.setPosition(std::min(ini, editor_->document()->characterCount() - 1));
			c.setPosition(std::min(fim, editor_->document()->characterCount() - 1), QTextCursor::KeepAnchor);
		} else
			c.setPosition(std::min(pos, editor_->document()->characterCount() - 1));
		editor_->setTextCursor(c);
		posicionarEditor();
		atualizarInspetor();
	}

	// --- o inspetor conforme a seleção ---
	void atualizarInspetor()
	{
		if (!secTexto_ || atual_ < 0 || doc_.slides.empty())
			return;
		atualizando_ = true;
		const Objeto *dono = editor_ ? &obj(edObj_) : umObjeto();
		const Objeto *o = editor_ ? &objetoEditado() : textoDe(umObjeto());
		bool texto = false, forma = false, imagem = false, grafico = false, tabela = false, video = false;
		for (int k : sel_) {
			const Objeto &x = obj(k);
			texto = texto || x.aceitaTexto() || x.tipo == Tipo::Tabela;
			forma = forma || x.forma() || x.tipo == Tipo::Imagem || x.tipo == Tipo::Tabela || x.tipo == Tipo::Video;
			imagem = imagem || x.tipo == Tipo::Imagem;
			grafico = grafico || x.tipo == Tipo::Grafico;
			tabela = tabela || x.tipo == Tipo::Tabela;
			video = video || x.tipo == Tipo::Video;
		}
		if (editor_)
			texto = true;
		secSlide_->setVisible(sel_.empty() && !editor_);
		secTexto_->setVisible(texto);
		secForma_->setVisible(forma && !editor_);
		secImagem_->setVisible(imagem && sel_.size() == 1 && !editor_);
		secGrafico_->setVisible(grafico && sel_.size() == 1 && !editor_);
		secTabela_->setVisible(tabela && sel_.size() == 1);
		secVideo_->setVisible(video && sel_.size() == 1 && !editor_);
		secOrganizar_->setVisible(!sel_.empty() && !editor_);
		if (dono && dono->tipo == Tipo::Tabela) {
			tCabecalho_->setChecked(dono->cabecalho);
			tFaixas_->setChecked(dono->faixas);
			const Objeto *c = editor_ && edCel_ >= 0 ? &objetoEditado() : nullptr;
			pintarAmostra(tFundo_, c ? c->fundoCel : QColor());
		}
		if (dono && dono->tipo == Tipo::Video && !editor_) {
			vSozinho_->setChecked(dono->sozinho);
			vRepetir_->setChecked(dono->repetir);
			vMudo_->setChecked(dono->mudo);
		}
		// O painel Animar.
		{
			const Objeto *a = sel_.size() == 1 ? &obj(sel_[0]) : nullptr;
			aVazio_->setVisible(!a);
			aCorpo_->setVisible(a != nullptr);
			if (a)
				for (int q = 0; q < 2; q++) {
					const Animacao &x = q == 0 ? a->entrada : a->saida;
					aEf_[q]->setCurrentIndex(int(x.ef));
					aDir_[q]->setCurrentIndex(std::clamp(x.dir, 0, 3));
					aIni_[q]->setCurrentIndex(std::clamp(x.inicio, 0, 2));
					aDur_[q]->setValue(x.dur);
					const bool tem = x.tem();
					aDir_[q]->setEnabled(tem && (x.ef == Efeito::Voar || x.ef == Efeito::Revelar));
					aIni_[q]->setEnabled(tem);
					aDur_[q]->setEnabled(tem && x.ef != Efeito::Aparecer);
					if (q == 0) {
						int pars = 0;
						for (const Paragrafo &p : a->pars)
							pars += !p.texto().trimmed().isEmpty();
						aPar_->setChecked(x.porParagrafo);
						aPar_->setEnabled(tem && a->aceitaTexto() && pars > 1);
					}
				}
			aOrdem_->clear();
			const auto l = listaAnimacoes();
			for (size_t i = 0; i < l.size(); i++) {
				const Objeto &x = obj(l[i].first);
				const Animacao &an = l[i].second ? x.saida : x.entrada;
				QString nome = x.temTexto() ? x.texto().section('\n', 0, 0).left(28) : nomeTipo(x);
				if (nome.trimmed().isEmpty()) nome = nomeTipo(x);
				static const char *ini[] = {"", " · com a anterior", " · depois"};
				auto *it = new QListWidgetItem(QString("%1. %2 %3: %4%5")
				                                   .arg(i + 1)
				                                   .arg(l[i].second ? "Sai" : "Entra")
				                                   .arg(nomesEfeito().value(int(an.ef)).toLower())
				                                   .arg(nome)
				                                   .arg(ini[std::clamp(an.inicio, 0, 2)]));
				it->setData(Qt::UserRole, l[i].first);
				aOrdem_->addItem(it);
				if (a && &x == a)
					aOrdem_->setCurrentItem(it);
			}
		}
		quadroVazio_->setVisible(sel_.empty() && !editor_);
		quadroVazio_->setText(QString("Slide %1 de %2. Clique num objeto para formatá-lo; clique duas vezes num texto para escrever.")
		                          .arg(atual_ + 1)
		                          .arg(doc_.slides.size()));
		if (sel_.empty() && !editor_) {
			const Slide &s = slide();
			leiaute_->setCurrentIndex(int(std::find_if(leiautes().begin(), leiautes().end(), [&](const Leiaute &l) { return l.id == s.layout; }) - leiautes().begin()));
			pintarAmostra(corFundo_, fundoDe(doc_, s));
			pular_->setChecked(s.pular);
		}
		if (texto && o) {
			QString fam = fonteDe(doc_, *o);
			double tam = o->tamanho;
			bool n = o->negrito || (o->papel == Papel::Titulo && doc_.tema.tituloNegrito), i = o->italico, s = false;
			QColor cor = corTexto(doc_, *o);
			int al = o->pars.empty() ? 0 : o->pars.front().alin;
			bool marc = !o->pars.empty() && o->pars.front().marcador, num = !o->pars.empty() && o->pars.front().numerado;
			if (editor_) {
				const QTextCursor c = editor_->textCursor();
				const QTextCharFormat cf = c.charFormat();
				fam = cf.font().family();
				const double px = cf.font().pixelSize() > 0 ? cf.font().pixelSize() : cf.font().pointSizeF() * K;
				tam = std::round(px / K / edEscala_ * 2) / 2;
				n = cf.fontWeight() >= QFont::DemiBold;
				i = cf.fontItalic();
				s = cf.fontUnderline();
				if (cf.foreground().style() != Qt::NoBrush)
					cor = cf.foreground().color();
				const Qt::Alignment a = c.blockFormat().alignment();
				al = (a & Qt::AlignHCenter) ? 1 : (a & Qt::AlignRight) ? 2 : (a & Qt::AlignJustify) ? 3 : 0;
				const QTextList *l = c.block().textList();
				num = l && l->format().style() == QTextListFormat::ListDecimal;
				marc = l && !num;
			} else if (!o->pars.empty() && !o->pars.front().trechos.empty()) {
				const Trecho &t = o->pars.front().trechos.front();
				n = n || t.negrito;
				i = i || t.italico;
				s = t.sublinhado;
				if (t.cor.isValid()) cor = t.cor;
				if (!t.fonte.isEmpty()) fam = t.fonte;
			}
			familia_->setCurrentFont(QFont(fam));
			tamanho_->setValue(tam);
			negrito_->setChecked(n);
			italico_->setChecked(i);
			sublinhado_->setChecked(s);
			pintarAmostra(corTexto_, cor);
			for (int k = 0; k < 4; k++)
				alin_[k]->setChecked(k == al);
			for (int k = 0; k < 3; k++)
				valin_[k]->setChecked(k == o->vAlin);
			marcador_->setChecked(marc);
			numerado_->setChecked(num);
			encolher_->setChecked(o->encolher);
		}
		if (dono && !editor_) {
			o = dono;
			pintarAmostra(preench_, o->tipo == Tipo::Imagem || o->tipo == Tipo::Tabela ? QColor() : o->preench);
			pintarAmostra(borda_, o->espBorda > 0 ? o->borda : QColor());
			espBorda_->setValue(o->espBorda);
			px_->setValue(o->r.x());
			py_->setValue(o->r.y());
			pw_->setValue(o->r.width());
			ph_->setValue(o->r.height());
			giro_->setValue(o->giro);
			if (o->tipo == Tipo::Grafico) {
				gTipo_->setCurrentIndex(int(o->graf.tipo));
				gTitulo_->setText(o->graf.titulo);
				gLegenda_->setChecked(o->graf.legenda);
				preencherTabelaDoGrafico(o->graf);
			}
		}
		for (QDoubleSpinBox *s : {px_, py_, pw_, ph_, giro_})
			s->setEnabled(sel_.size() == 1);
		giro_->setEnabled(sel_.size() == 1 && obj(sel_[0]).tipo != Tipo::Tabela);
		// A transição e o documento.
		transicao_->setCurrentIndex(int(slide().transicao));
		duracao_->setValue(slide().duracao);
		formato_->setCurrentIndex(std::fabs(doc_.larg - 720) < 1 ? 1 : 0);
		dTitulo_->setText(doc_.titulo);
		dAutor_->setText(doc_.autor);
		atualizando_ = false;
	}

	void preencherTabelaDoGrafico(const DadosGrafico &g)
	{
		const QSignalBlocker b(dados_);
		dados_->clear();
		dados_->setRowCount(int(g.categorias.size()) + 1);
		dados_->setColumnCount(int(g.series.size()) + 1);
		dados_->setItem(0, 0, new QTableWidgetItem(""));
		dados_->item(0, 0)->setFlags(Qt::ItemIsEnabled);
		const QLocale br(QLocale::Portuguese, QLocale::Brazil);
		for (int s = 0; s < g.series.size(); s++)
			dados_->setItem(0, s + 1, new QTableWidgetItem(g.series[s]));
		for (int c = 0; c < g.categorias.size(); c++) {
			dados_->setItem(c + 1, 0, new QTableWidgetItem(g.categorias[c]));
			for (int s = 0; s < g.series.size(); s++) {
				const double v = s < int(g.valores.size()) && c < int(g.valores[size_t(s)].size()) ? g.valores[size_t(s)][size_t(c)] : 0;
				dados_->setItem(c + 1, s + 1, new QTableWidgetItem(br.toString(v, 'g', 12)));
			}
		}
		for (int c = 0; c < dados_->columnCount(); c++) {
			QFont f = dados_->font();
			f.setBold(true);
			if (dados_->item(0, c)) dados_->item(0, c)->setFont(f);
		}
	}

	void lerTabelaDoGrafico()
	{
		const QLocale br(QLocale::Portuguese, QLocale::Brazil);
		auto txt = [this](int r, int c) { return dados_->item(r, c) ? dados_->item(r, c)->text().trimmed() : QString(); };
		DadosGrafico g;
		if (const Objeto *o = umObjeto())
			g = o->graf;
		g.categorias.clear();
		g.series.clear();
		g.valores.clear();
		for (int s = 1; s < dados_->columnCount(); s++)
			g.series << (txt(0, s).isEmpty() ? QString("Série %1").arg(s) : txt(0, s));
		for (int c = 1; c < dados_->rowCount(); c++)
			g.categorias << txt(c, 0);
		g.valores.assign(size_t(g.series.size()), std::vector<double>(size_t(g.categorias.size()), 0));
		for (int s = 0; s < g.series.size(); s++)
			for (int c = 0; c < g.categorias.size(); c++) {
				QString t = txt(c + 1, s + 1);
				bool ok = false;
				double v = br.toDouble(t, &ok);
				if (!ok) v = t.replace(',', '.').toDouble();
				g.valores[size_t(s)][size_t(c)] = v;
			}
		const bool ant = atualizando_;
		atualizando_ = true;
		mudarObjetos([&g](Objeto &o) { if (o.tipo == Tipo::Grafico) o.graf = g; });
		atualizando_ = ant;
	}

	// --- inserir ---
	QPointF centroLivre(QSizeF t)
	{
		// O meio do slide, um pouco ao lado se já houver algo ali.
		QPointF c(doc_.larg / 2, doc_.alt / 2);
		for (int k = 0; k < 8; k++) {
			bool ocupado = false;
			for (const Objeto &o : slide().objs)
				if (o.papel == Papel::Livre && QLineF(o.r.center(), c).length() < 4)
					ocupado = true;
			if (!ocupado)
				break;
			c += QPointF(18, 18);
		}
		Q_UNUSED(t);
		return c;
	}

	int acrescentar(const Objeto &o)
	{
		terminarEdicao();
		guardar();
		slide().objs.push_back(o);
		const int k = int(slide().objs.size()) - 1;
		sel_ = {k};
		refazerCena();
		atualizarInspetor();
		marcarMudanca();
		return k;
	}

	void inserirTexto()
	{
		Objeto o;
		o.tipo = Tipo::Texto;
		o.tamanho = 24;
		o.r = QRectF(0, 0, doc_.larg * 0.36, 48);
		o.r.moveCenter(centroLivre(o.r.size()));
		o.pars = paragrafos("Texto");
		const int k = acrescentar(o);
		editar(k, QPointF(), true);
	}

	void inserirForma(Tipo t)
	{
		Objeto o;
		o.tipo = t;
		o.tamanho = 20;
		o.vAlin = 1;
		Paragrafo p;
		p.alin = 1;
		o.pars = {p};
		if (t == Tipo::Linha) {
			o.r = QRectF(0, 0, doc_.larg * 0.3, 0);
			o.borda = doc_.tema.texto;
			o.espBorda = 3;
		} else {
			o.r = QRectF(0, 0, t == Tipo::Seta ? 220 : 180, t == Tipo::Seta ? 110 : 180);
			o.preench = doc_.tema.destaque;
		}
		o.r.moveCenter(centroLivre(o.r.size()));
		acrescentar(o);
	}

	void inserirGrafico(TipoGrafico t)
	{
		Objeto o;
		o.tipo = Tipo::Grafico;
		o.graf.tipo = t;
		o.graf.categorias = QStringList{"1º tri", "2º tri", "3º tri", "4º tri"};
		if (t == TipoGrafico::Pizza) {
			o.graf.series = QStringList{"Parte"};
			o.graf.valores = {{40, 25, 20, 15}};
			o.graf.categorias = QStringList{"A", "B", "C", "D"};
		} else {
			o.graf.series = QStringList{"2025", "2026"};
			o.graf.valores = {{12, 15, 14, 18}, {14, 17, 19, 22}};
		}
		o.r = QRectF(0, 0, doc_.larg * 0.6, doc_.alt * 0.6);
		o.r.moveCenter(QPointF(doc_.larg / 2, doc_.alt * 0.58));
		acrescentar(o);
		mostrarInspetor(0);
	}

	void escolherImagem()
	{
		const QString f = QFileDialog::getOpenFileName(this, "Inserir imagem", QDir::homePath(), "Imagens (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
		if (!f.isEmpty())
			inserirImagem(f);
	}

	void inserirImagem(const QString &arq)
	{
		QFile f(arq);
		if (!f.open(QIODevice::ReadOnly))
			return;
		const QByteArray b = f.readAll();
		QImage img;
		if (!img.loadFromData(b)) {
			QMessageBox::warning(this, "Palco", "Não deu para ler a imagem \"" + QFileInfo(arq).fileName() + "\".");
			return;
		}
		QByteArray dados = b;
		QString ext = QFileInfo(arq).suffix().toLower();
		if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "gif") {
			// Outros formatos viram PNG (o .pptx e o .odp levam esses).
			dados.clear();
			QBuffer buf(&dados);
			buf.open(QIODevice::WriteOnly);
			img.save(&buf, "PNG");
			ext = "png";
		}
		inserirImagemBytes(dados, ext, img.size());
	}

	void inserirImagemDados(const QImage &img)
	{
		if (img.isNull())
			return;
		QByteArray dados;
		QBuffer buf(&dados);
		buf.open(QIODevice::WriteOnly);
		img.save(&buf, "PNG");
		inserirImagemBytes(dados, "png", img.size());
	}

	void inserirImagemBytes(const QByteArray &dados, const QString &ext, QSize t)
	{
		Objeto o;
		o.tipo = Tipo::Imagem;
		o.midia = doc_.guardarMidia(dados, ext);
		const double k = std::min({1.0, doc_.larg * 0.6 / std::max(1, t.width()), doc_.alt * 0.6 / std::max(1, t.height())});
		o.r = QRectF(0, 0, t.width() * k, t.height() * k);
		o.r.moveCenter(centroLivre(o.r.size()));
		acrescentar(o);
	}

	void trocarImagem()
	{
		const Objeto *o = umObjeto();
		if (!o || o->tipo != Tipo::Imagem)
			return;
		const QString f = QFileDialog::getOpenFileName(this, "Trocar a imagem", QDir::homePath(), "Imagens (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
		if (f.isEmpty())
			return;
		QFile arq(f);
		if (!arq.open(QIODevice::ReadOnly))
			return;
		const QByteArray b = arq.readAll();
		QImage img;
		if (!img.loadFromData(b))
			return;
		const QString chave = doc_.guardarMidia(b, QFileInfo(f).suffix());
		mudarObjetos([&](Objeto &x) {
			x.midia = chave;
			x.recorte = QRectF(0, 0, 1, 1);
			const double h = x.r.width() * img.height() / std::max(1, img.width());
			const QPointF c = x.r.center();
			x.r.setHeight(h);
			x.r.moveCenter(c);
		});
		doc_.limparMidias();
	}

	void escolherFundo()
	{
		const QString f = QFileDialog::getOpenFileName(this, "Imagem de fundo", QDir::homePath(), "Imagens (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
		if (f.isEmpty())
			return;
		QFile arq(f);
		if (!arq.open(QIODevice::ReadOnly))
			return;
		const QByteArray b = arq.readAll();
		QImage img;
		if (!img.loadFromData(b))
			return;
		guardar();
		slide().fundoMidia = doc_.guardarMidia(b, QFileInfo(f).suffix());
		refazerCena();
		marcarMudanca();
	}

	// --- objetos: apagar, camadas, copiar ---
	void apagarObjetos()
	{
		if (sel_.empty())
			return;
		terminarEdicao();
		guardar();
		std::vector<int> s = sel_;
		std::sort(s.rbegin(), s.rend());
		for (int k : s)
			slide().objs.erase(slide().objs.begin() + k);
		sel_.clear();
		doc_.limparMidias();
		refazerCena();
		atualizarInspetor();
		marcarMudanca();
	}

	void camada(bool frente)
	{
		if (sel_.empty())
			return;
		guardar();
		std::vector<Objeto> escolhidos, outros;
		for (int k = 0; k < int(slide().objs.size()); k++)
			(std::find(sel_.begin(), sel_.end(), k) != sel_.end() ? escolhidos : outros).push_back(obj(k));
		slide().objs.clear();
		sel_.clear();
		if (!frente) {
			for (const Objeto &o : escolhidos) { sel_.push_back(int(slide().objs.size())); slide().objs.push_back(o); }
			for (const Objeto &o : outros) slide().objs.push_back(o);
		} else {
			for (const Objeto &o : outros) slide().objs.push_back(o);
			for (const Objeto &o : escolhidos) { sel_.push_back(int(slide().objs.size())); slide().objs.push_back(o); }
		}
		refazerCena();
		marcarMudanca();
	}

	void copiar(bool recortar)
	{
		if (sel_.empty())
			return;
		recorte_.clear();
		midiasCopiadas_.clear();
		for (int k : sel_) {
			recorte_.push_back(obj(k));
			for (const QString &m : {obj(k).midia, obj(k).capa})
				if (!m.isEmpty())
					midiasCopiadas_.insert(m, doc_.midias.value(m));
		}
		// Também vai como texto para outros programas.
		QStringList t;
		for (const Objeto &o : recorte_)
			if (o.temTexto())
				t << o.texto();
		QApplication::clipboard()->setText(t.join("\n\n"));
		textoCopiado_ = QApplication::clipboard()->text();
		if (recortar)
			apagarObjetos();
	}

	void colar()
	{
		const QMimeData *m = QApplication::clipboard()->mimeData();
		if (!recorte_.empty() && m && m->text() == textoCopiado_ && !m->hasImage()) {
			guardar();
			sel_.clear();
			for (Objeto o : recorte_) {
				if (!o.midia.isEmpty())
					o.midia = doc_.guardarMidia(midiasCopiadas_.value(o.midia), QFileInfo(o.midia).suffix());
				if (!o.capa.isEmpty())
					o.capa = doc_.guardarMidia(midiasCopiadas_.value(o.capa), QFileInfo(o.capa).suffix());
				o.r.translate(14, 14);
				if (o.papel != Papel::Livre && slide().com(o.papel))
					o.papel = Papel::Livre;
				sel_.push_back(int(slide().objs.size()));
				slide().objs.push_back(o);
			}
			for (Objeto &o : recorte_)
				o.r.translate(14, 14);
			refazerCena();
			atualizarInspetor();
			marcarMudanca();
			return;
		}
		if (m && m->hasImage()) {
			inserirImagemDados(qvariant_cast<QImage>(m->imageData()));
			return;
		}
		if (m && m->hasText() && !m->text().trimmed().isEmpty()) {
			Objeto o;
			o.tipo = Tipo::Texto;
			o.tamanho = 20;
			o.pars = paragrafos(m->text().trimmed());
			o.r = QRectF(0, 0, doc_.larg * 0.5, std::min(doc_.alt * 0.6, 34.0 * o.pars.size() + 16));
			o.r.moveCenter(centroLivre(o.r.size()));
			acrescentar(o);
		}
	}

	void duplicarObjetos()
	{
		if (sel_.empty())
			return;
		guardar();
		std::vector<int> novos;
		for (int k : sel_) {
			Objeto o = obj(k);
			o.r.translate(14, 14);
			o.papel = Papel::Livre;
			novos.push_back(int(slide().objs.size()));
			slide().objs.push_back(o);
		}
		sel_ = novos;
		refazerCena();
		atualizarInspetor();
		marcarMudanca();
	}

	// --- slides ---
	Layout proximoLeiaute() const
	{
		// Depois do título vêm os tópicos; depois, o mesmo leiaute de antes.
		if (doc_.slides.empty()) return Layout::Titulo;
		const Layout l = doc_.slides[size_t(atual_)].layout;
		return l == Layout::Titulo || l == Layout::Secao ? Layout::TituloConteudo : l;
	}

	void novoSlideDepois(Layout l)
	{
		terminarEdicao();
		guardar();
		Slide s = novoSlide(doc_, l);
		s.transicao = slide().transicao;
		s.duracao = slide().duracao;
		doc_.slides.insert(doc_.slides.begin() + atual_ + 1, s);
		atual_++;
		atualizarNavegador();
		mostrarSlide(atual_);
		marcarMudanca();
	}

	void duplicarSlide()
	{
		terminarEdicao();
		guardar();
		doc_.slides.insert(doc_.slides.begin() + atual_ + 1, slide());
		atual_++;
		atualizarNavegador();
		mostrarSlide(atual_);
		marcarMudanca();
	}

	void apagarSlides()
	{
		terminarEdicao();
		std::vector<int> l;
		for (QListWidgetItem *it : nav_->selectedItems())
			l.push_back(nav_->row(it));
		if (l.empty())
			l.push_back(atual_);
		if (l.size() >= doc_.slides.size()) {
			statusBar()->showMessage("A apresentação precisa de pelo menos um slide.", 4000);
			return;
		}
		guardar();
		std::sort(l.rbegin(), l.rend());
		for (int i : l)
			doc_.slides.erase(doc_.slides.begin() + i);
		doc_.limparMidias();
		atual_ = std::clamp(l.back(), 0, int(doc_.slides.size()) - 1);
		atualizarNavegador();
		mostrarSlide(atual_);
		marcarMudanca();
	}

	void moverSlide(int de, int para)
	{
		if (de == para || de < 0 || para < 0 || de >= int(doc_.slides.size()) || para >= int(doc_.slides.size()))
			return;
		terminarEdicao();
		guardar();
		Slide s = doc_.slides[size_t(de)];
		doc_.slides.erase(doc_.slides.begin() + de);
		doc_.slides.insert(doc_.slides.begin() + para, s);
		atual_ = para;
		atualizarNavegador();
		mostrarSlide(atual_);
		marcarMudanca();
	}

	void reordenarPeloNavegador()
	{
		std::vector<Slide> novos;
		int novoAtual = 0;
		for (int i = 0; i < nav_->count(); i++) {
			const int velho = nav_->item(i)->data(Qt::UserRole).toInt();
			if (velho < 0 || velho >= int(doc_.slides.size()))
				return atualizarNavegador();
			novos.push_back(doc_.slides[size_t(velho)]);
			if (velho == atual_)
				novoAtual = i;
		}
		if (novos.size() != doc_.slides.size())
			return atualizarNavegador();
		guardar();
		doc_.slides = novos;
		atual_ = novoAtual;
		atualizarNavegador();
		mostrarSlide(atual_);
		marcarMudanca();
	}

	bool teclaNavegador(QKeyEvent *e)
	{
		if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
			apagarSlides();
			return true;
		}
		if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
			novoSlideDepois(proximoLeiaute());
			return true;
		}
		if ((e->modifiers() & Qt::AltModifier) && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
			moverSlide(atual_, atual_ + (e->key() == Qt::Key_Up ? -1 : 1));
			return true;
		}
		return false;
	}

	void menuNavegador(const QPoint &g)
	{
		QMenu m(this);
		QMenu *novo = m.addMenu("Novo slide");
		for (const Leiaute &l : leiautes()) {
			const Layout id = l.id;
			novo->addAction(l.nome, this, [this, id] { novoSlideDepois(id); });
		}
		m.addAction("Duplicar", this, [this] { duplicarSlide(); });
		m.addAction(slide().pular ? "Não pular ao apresentar" : "Pular ao apresentar", this, [this] {
			guardar();
			slide().pular = !slide().pular;
			atualizarMiniatura(atual_);
			atualizarInspetor();
			marcarMudanca();
		});
		m.addSeparator();
		m.addAction("Mover para cima", this, [this] { moverSlide(atual_, atual_ - 1); })->setEnabled(atual_ > 0);
		m.addAction("Mover para baixo", this, [this] { moverSlide(atual_, atual_ + 1); })->setEnabled(atual_ + 1 < int(doc_.slides.size()));
		m.addSeparator();
		m.addAction("Apresentar deste slide", this, [this] { apresentar(atual_, false); });
		m.addSeparator();
		m.addAction("Apagar", this, [this] { apagarSlides(); })->setEnabled(doc_.slides.size() > 1);
		m.exec(g);
	}

	bool teclaTela(QKeyEvent *e)
	{
		const bool shift = e->modifiers() & Qt::ShiftModifier;
		switch (e->key()) {
		case Qt::Key_Delete:
		case Qt::Key_Backspace:
			if (!sel_.empty()) apagarObjetos();
			return true;
		case Qt::Key_Escape:
			sel_.clear();
			atualizarSelecao();
			atualizarInspetor();
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			if (sel_.size() == 1 && obj(sel_[0]).aceitaTexto())
				editar(sel_[0]);
			return true;
		case Qt::Key_Tab: {
			const int n = int(slide().objs.size());
			if (n == 0) return true;
			const int k = sel_.empty() ? 0 : (sel_.back() + 1) % n;
			sel_ = {k};
			atualizarSelecao();
			atualizarInspetor();
			return true;
		}
		case Qt::Key_PageDown:
			mostrarSlide(std::min(atual_ + 1, int(doc_.slides.size()) - 1));
			return true;
		case Qt::Key_PageUp:
			mostrarSlide(std::max(atual_ - 1, 0));
			return true;
		case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down: {
			if (sel_.empty()) {
				if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Right) mostrarSlide(std::min(atual_ + 1, int(doc_.slides.size()) - 1));
				else mostrarSlide(std::max(atual_ - 1, 0));
				return true;
			}
			const double p = shift ? 10 : 1;
			const QPointF d = e->key() == Qt::Key_Left ? QPointF(-p, 0) : e->key() == Qt::Key_Right ? QPointF(p, 0)
			                  : e->key() == Qt::Key_Up   ? QPointF(0, -p) : QPointF(0, p);
			mudarObjetos([d](Objeto &o) { o.r.translate(d); });
			return true;
		}
		default:
			break;
		}
		if (e->matches(QKeySequence::SelectAll)) {
			sel_.clear();
			for (int k = 0; k < int(slide().objs.size()); k++)
				sel_.push_back(k);
			atualizarSelecao();
			atualizarInspetor();
			return true;
		}
		// Digitar com um texto escolhido: começa a escrever nele.
		if (sel_.size() == 1 && obj(sel_[0]).aceitaTexto() && !e->text().isEmpty() && e->text()[0].isPrint() &&
		    !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
			editar(sel_[0], QPointF(), true);
			if (editor_) {
				QTextCursor c = editor_->textCursor();
				c.insertText(e->text());
				editor_->setTextCursor(c);
			}
			return true;
		}
		return false;
	}

	void menuTela(const QPoint &g, QPointF p)
	{
		terminarEdicao();
		QMenu m(this);
		const int k = objetoEm(p);
		if (k >= 0) {
			if (std::find(sel_.begin(), sel_.end(), k) == sel_.end()) {
				sel_ = {k};
				atualizarSelecao();
				atualizarInspetor();
			}
			if (obj(k).aceitaTexto())
				m.addAction("Escrever", this, [this, k] { editar(k); });
			m.addAction("Recortar", this, [this] { copiar(true); });
			m.addAction("Copiar", this, [this] { copiar(false); });
			m.addAction("Colar", this, [this] { colar(); });
			m.addAction("Duplicar", this, [this] { duplicarObjetos(); });
			m.addSeparator();
			m.addAction("Trazer para a frente", this, [this] { camada(true); });
			m.addAction("Mandar para trás", this, [this] { camada(false); });
			m.addSeparator();
			m.addAction("Apagar", this, [this] { apagarObjetos(); });
		} else {
			m.addAction("Colar", this, [this] { colar(); });
			m.addSeparator();
			m.addAction("Caixa de texto", this, [this] { inserirTexto(); });
			m.addAction("Imagem…", this, [this] { escolherImagem(); });
			m.addSeparator();
			m.addAction("Novo slide", this, [this] { novoSlideDepois(proximoLeiaute()); });
			m.addAction("Apresentar deste slide", this, [this] { apresentar(atual_, false); });
		}
		m.exec(g);
	}

	// --- a prévia da transição ---
	void previa()
	{
		if (!previa_ || doc_.slides.empty())
			return;
		const int ant = atual_ > 0 ? atual_ - 1 : -1;
		QImage branco(previa_->size(), QImage::Format_ARGB32_Premultiplied);
		branco.fill(Qt::black);
		previaAntes_ = ant >= 0 ? imagemSlide(doc_, doc_.slides[size_t(ant)], previa_->width()) : branco;
		previaDepois_ = imagemSlide(doc_, slide(), previa_->width());
		animPrevia_->stop();
		animPrevia_->setDuration(std::clamp(int(slide().duracao * 1000), 150, 4000));
		if (slide().transicao == Transicao::Nenhuma) {
			previa_->setPixmap(QPixmap::fromImage(previaDepois_));
			return;
		}
		animPrevia_->start();
	}

	// --- apresentar ---
	void apresentar(int de, bool ensaio)
	{
		terminarEdicao();
		sessao_ = std::make_unique<Sessao>();
		Sessao &s = *sessao_;
		s.doc = &doc_;
		for (size_t i = 0; i < doc_.slides.size(); i++)
			if (!doc_.slides[i].pular || int(i) == de)
				s.ordem.push_back(int(i));
		if (s.ordem.empty()) {
			statusBar()->showMessage("Todos os slides estão marcados para pular.", 5000);
			return;
		}
		for (size_t i = 0; i < s.ordem.size(); i++)
			if (s.ordem[i] <= de)
				s.pos = int(i);
		s.relogio.start();
		const QList<QScreen *> telas = QGuiApplication::screens();
		QScreen *principal = QGuiApplication::primaryScreen();
		QScreen *outra = nullptr;
		for (QScreen *t : telas)
			if (t != principal)
				outra = t;
		auto *ap = (!ensaio) ? new Apresentacao(&s) : nullptr;
		auto *pr = (ensaio || outra) ? new Apresentador(&s) : nullptr;
		auto cheia = [](QWidget *w, QScreen *t) {
			w->setAttribute(Qt::WA_DeleteOnClose);
			w->winId();
			if (t && w->windowHandle()) {
				w->windowHandle()->setScreen(t);
				w->setGeometry(t->geometry());
			}
			w->showFullScreen();
			w->raise();
			w->activateWindow();
		};
		s.comTela = ap != nullptr;
		s.aoMudar = [ap, pr](int de) {
			if (ap) ap->mudou(de);
			if (pr) pr->mudou();
		};
		s.aoAnimar = [ap, pr] {
			if (ap) ap->animou();
			if (pr) pr->animou();
		};
		s.aoSair = [this, ap, pr] {
			const int fim = sessao_ ? sessao_->slide() : atual_;
			if (ap) ap->close();
			if (pr) pr->close();
			QTimer::singleShot(0, this, [this, fim] {
				sessao_.reset();
				if (fim >= 0)
					mostrarSlide(fim);
				activateWindow();
			});
		};
		if (ap) {
			cheia(ap, outra ? outra : principal);
			ap->mudou(-1);
		}
		if (pr) {
			cheia(pr, principal);
			pr->mudou();
		}
		if (ap)
			ap->activateWindow();
	}

	// --- arquivos ---
	void escolherEAbrir()
	{
		if (pilha_->currentIndex() == 1 && !podeFechar())
			return;
		const QString arq = QFileDialog::getOpenFileName(this, "Abrir uma apresentação", ultimaPasta(),
		                                                 "Apresentações (*.pptx *.odp);;PowerPoint (*.pptx);;OpenDocument (*.odp)");
		if (!arq.isEmpty())
			abrir(arq);
	}

	QString ultimaPasta() const
	{
		if (!arquivo_.isEmpty())
			return QFileInfo(arquivo_).absolutePath();
		const QString d = QDir::homePath() + "/Documentos";
		return QFileInfo::exists(d) ? d : QDir::homePath();
	}

	bool salvar()
	{
		terminarEdicao();
		if (arquivo_.isEmpty())
			return salvarComo();
		QString erro;
		if (!gravarArquivo(arquivo_, doc_, &erro)) {
			QMessageBox::warning(this, "Palco", "Não deu para salvar:\n" + erro);
			return false;
		}
		mudou_ = false;
		atualizarTitulo();
		statusBar()->showMessage("Salvo: " + QDir::toNativeSeparators(arquivo_), 4000);
		return true;
	}

	bool salvarComo()
	{
		terminarEdicao();
		QString nome = arquivo_;
		if (nome.isEmpty()) {
			QString t = doc_.titulo.isEmpty() && !doc_.slides.empty() ? doc_.slides.front().titulo() : doc_.titulo;
			t.remove(QRegularExpression("[/\\\\:*?\"<>|]"));
			nome = ultimaPasta() + "/" + (t.isEmpty() ? QString("Apresentação") : t.left(60)) + ".pptx";
		}
		QString filtro;
		QString arq = QFileDialog::getSaveFileName(this, "Salvar a apresentação", nome, "PowerPoint (*.pptx);;OpenDocument (*.odp)", &filtro);
		if (arq.isEmpty())
			return false;
		const QString ext = QFileInfo(arq).suffix().toLower();
		if (ext != "pptx" && ext != "odp")
			arq += filtro.contains("odp") ? ".odp" : ".pptx";
		arquivo_ = arq;
		if (!salvar())
			return false;
		recente(arquivo_);
		return true;
	}

	void exportar(const QString &ext)
	{
		terminarEdicao();
		QString base = arquivo_.isEmpty() ? ultimaPasta() + "/Apresentação" : arquivo_.left(arquivo_.lastIndexOf('.'));
		const QString arq = QFileDialog::getSaveFileName(this, "Exportar", base + "." + ext,
		                                                 ext == "pdf" ? "PDF (*.pdf)" : ext == "odp" ? "OpenDocument (*.odp)" : "PowerPoint (*.pptx)");
		if (arq.isEmpty())
			return;
		QString erro;
		if (!gravarArquivo(arq, doc_, &erro))
			QMessageBox::warning(this, "Palco", "Não deu para exportar:\n" + erro);
		else
			statusBar()->showMessage("Exportado: " + QDir::toNativeSeparators(arq), 5000);
	}

	void exportarPdfDialogo() { exportar("pdf"); }

	bool podeFechar()
	{
		terminarEdicao();
		if (!mudou_)
			return true;
		QMessageBox m(this);
		m.setWindowTitle("Palco");
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

	struct Arrasto {
		enum Modo { Nada, Mover, Tamanho, Girar, Ponta, Banda, Coluna, LinhaTab } modo = Nada;
		QPointF inicio;
		int alca = -1;
		bool mexeu = false;
		std::vector<Objeto> originais;
	};

	Doc doc_;
	int atual_ = 0;
	std::vector<int> sel_;
	std::vector<std::pair<Doc, int>> antes_, depois_;
	Doc antesEdicao_;
	Arrasto arrasto_;
	std::vector<Objeto> recorte_;
	QMap<QString, QByteArray> midiasCopiadas_;
	QString textoCopiado_;
	QString arquivo_;
	bool mudou_ = false, atualizando_ = false, ajustando_ = false;
	std::unique_ptr<Sessao> sessao_;

	QStackedWidget *pilha_ = nullptr, *inspetor_ = nullptr;
	QToolBar *barra_ = nullptr;
	QLabel *titulo_ = nullptr, *quadroVazio_ = nullptr, *previa_ = nullptr;
	QComboBox *zoom_ = nullptr, *leiaute_ = nullptr, *transicao_ = nullptr, *formato_ = nullptr, *gTipo_ = nullptr;
	QToolButton *bPainel_[4] = {};
	QVBoxLayout *recentes_ = nullptr;
	Navegador *nav_ = nullptr;
	Tela *tela_ = nullptr;
	QGraphicsScene *cena_ = nullptr;
	ItemSelecao *selecao_ = nullptr;
	std::vector<ItemObjeto *> itens_;
	EditorTexto *editor_ = nullptr;
	int edObj_ = -1;
	int edCel_ = -1; // a célula da tabela em edição
	std::map<int, std::unique_ptr<Tocador>> tocadores_; // os vídeos tocando no editor
	QWidget *secTabela_ = nullptr, *secVideo_ = nullptr, *aCorpo_ = nullptr;
	QCheckBox *tCabecalho_ = nullptr, *tFaixas_ = nullptr, *vSozinho_ = nullptr, *vRepetir_ = nullptr, *vMudo_ = nullptr, *aPar_ = nullptr;
	QToolButton *tFundo_ = nullptr;
	QComboBox *aEf_[2] = {}, *aDir_[2] = {}, *aIni_[2] = {};
	QDoubleSpinBox *aDur_[2] = {};
	QListWidget *aOrdem_ = nullptr;
	QLabel *aVazio_ = nullptr, *aPrevia_ = nullptr;
	QTimer *aTempo_ = nullptr;
	QElapsedTimer aRelogio_;
	double edEscala_ = 1;
	QPlainTextEdit *notas_ = nullptr;
	QTimer *miniaturas_ = nullptr;
	QWidget *secSlide_ = nullptr, *secTexto_ = nullptr, *secForma_ = nullptr, *secImagem_ = nullptr, *secGrafico_ = nullptr, *secOrganizar_ = nullptr;
	QToolButton *corFundo_ = nullptr, *corTexto_ = nullptr, *preench_ = nullptr, *borda_ = nullptr;
	QCheckBox *pular_ = nullptr, *encolher_ = nullptr, *gLegenda_ = nullptr;
	QFontComboBox *familia_ = nullptr;
	QDoubleSpinBox *tamanho_ = nullptr, *espBorda_ = nullptr, *px_ = nullptr, *py_ = nullptr, *pw_ = nullptr, *ph_ = nullptr, *giro_ = nullptr, *duracao_ = nullptr;
	QToolButton *negrito_ = nullptr, *italico_ = nullptr, *sublinhado_ = nullptr, *marcador_ = nullptr, *numerado_ = nullptr;
	QToolButton *alin_[4] = {}, *valin_[3] = {};
	QLineEdit *gTitulo_ = nullptr, *dTitulo_ = nullptr, *dAutor_ = nullptr;
	QTableWidget *dados_ = nullptr;
	QVariantAnimation *animPrevia_ = nullptr;
	QImage previaAntes_, previaDepois_;
};

// O autoteste: faz uma apresentação pelos mesmos caminhos da janela (escrever
// no slide, formatar um pedaço, níveis, formas, gráfico, transição, tema,
// desfazer), grava, lê de volta e devolve o resumo.
QString Janela::autoteste(const QString &saida)
{
	modelo("branco");
	// O título, escrito no slide.
	int tit = -1, sub = -1;
	for (int k = 0; k < int(slide().objs.size()); k++) {
		if (obj(k).papel == Papel::Titulo) tit = k;
		if (obj(k).papel == Papel::Subtitulo) sub = k;
	}
	editar(tit);
	editor_->textCursor().insertText("Minha aula");
	terminarEdicao();
	editar(sub);
	QTextCursor c = editor_->textCursor();
	c.insertText("Ciências, ");
	c.insertText("7º ano");
	editor_->setTextCursor(c);
	// "7º ano" em negrito e itálico.
	c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 6);
	editor_->setTextCursor(c);
	alternarLetra(0);
	alternarLetra(1);
	terminarEdicao();
	// Um slide de tópicos, com um subnível e um tópico numerado.
	novoSlideDepois(Layout::TituloConteudo);
	for (int k = 0; k < int(slide().objs.size()); k++) {
		if (obj(k).papel == Papel::Titulo) {
			editar(k);
			editor_->textCursor().insertText("O ciclo da água");
			terminarEdicao();
		} else if (obj(k).papel == Papel::Corpo) {
			editar(k);
			QTextCursor t = editor_->textCursor();
			t.insertText("Evaporação");
			t.insertBlock();
			t.insertText("o sol aquece a água");
			editor_->setTextCursor(t);
			nivel(1);
			t = editor_->textCursor();
			t.movePosition(QTextCursor::End);
			t.insertBlock();
			editor_->setTextCursor(t);
			nivel(-1);
			t = editor_->textCursor();
			t.insertText("Condensação");
			editor_->setTextCursor(t);
			terminarEdicao();
		}
	}
	slide().transicao = Transicao::Dissolver;
	slide().notas = "Mostrar o vídeo da chuva.";
	// Uma forma com texto, uma caixa de texto e um gráfico.
	novoSlideDepois(Layout::SoTitulo);
	for (int k = 0; k < int(slide().objs.size()); k++)
		if (obj(k).papel == Papel::Titulo) {
			editar(k);
			editor_->textCursor().insertText("Chuva por mês");
			terminarEdicao();
		}
	inserirForma(Tipo::Elipse);
	obj(sel_[0]).r = QRectF(60, 160, 200, 200);
	editar(sel_[0]);
	editor_->textCursor().insertText("Nuvem");
	terminarEdicao();
	mudarObjetos([](Objeto &o) { o.preench = QColor("#2f4b6e"); });
	inserirGrafico(TipoGrafico::Colunas);
	obj(sel_[0]).r = QRectF(320, 140, 580, 360);
	lerTabelaDoGrafico();
	gTitulo_->setText("Chuva (mm)");
	emit gTitulo_->editingFinished();
	slide().transicao = Transicao::Empurrar;
	slide().duracao = 1.0;
	// Uma tabela, escrita célula a célula (Tab passa para a próxima; na
	// última, cria uma linha), e as animações pelo painel Animar.
	novoSlideDepois(Layout::SoTitulo);
	int titulo = -1;
	for (int k = 0; k < int(slide().objs.size()); k++)
		if (obj(k).papel == Papel::Titulo) {
			titulo = k;
			editar(k);
			editor_->textCursor().insertText("Notas da turma");
			terminarEdicao();
		}
	inserirTabela(2, 2);
	const int tab = edObj_;
	for (const char *t : {"Aluno", "Nota", "Ana", "9,5", "Bruno"}) {
		editor_->textCursor().insertText(QString::fromUtf8(t));
		irCelula(1);
	}
	editor_->textCursor().insertText("8");
	alinhar(2);
	terminarEdicao();
	editar(tab, QPointF(), false, 3);
	alinhar(2);
	terminarEdicao();
	mostrarInspetor(3);
	sel_ = {titulo};
	atualizarInspetor();
	aEf_[0]->setCurrentIndex(int(Efeito::Dissolver));
	emit aEf_[0]->activated(int(Efeito::Dissolver));
	sel_ = {tab};
	atualizarInspetor();
	aEf_[0]->setCurrentIndex(int(Efeito::Voar));
	emit aEf_[0]->activated(int(Efeito::Voar));
	aDir_[0]->setCurrentIndex(1);
	emit aDir_[0]->activated(1);
	aIni_[0]->setCurrentIndex(2);
	emit aIni_[0]->activated(2);
	aDur_[0]->setValue(0.8);
	aEf_[1]->setCurrentIndex(int(Efeito::Dissolver));
	emit aEf_[1]->activated(int(Efeito::Dissolver));
	mostrarInspetor(0);
	// Um vídeo (os bytes de um MP4; a capa chegaria depois, tocando), que
	// começa sozinho, repete e não tem som.
	{
		QTemporaryFile v(QDir::tempPath() + "/lume-palco-XXXXXX.mp4");
		if (v.open()) {
			QByteArray b("\x00\x00\x00\x18" "ftypmp42", 12);
			b += QByteArray(4096, '\x01');
			v.write(b);
			v.flush();
			inserirVideo(v.fileName());
			obj(sel_[0]).r = QRectF(600, 400, 320, 120);
			atualizarInspetor();
			vSozinho_->setChecked(true);
			vRepetir_->setChecked(true);
			vMudo_->setChecked(true);
		}
	}
	// Duplicar o último slide, apagar a cópia; desfazer a cópia apagada e refazer.
	duplicarSlide();
	apagarSlides();
	desfazer();
	refazer();
	// Um slide que não aparece, e o tema da lousa.
	novoSlideDepois(Layout::Secao);
	slide().pular = true;
	doc_.tema = tema("giz");
	doc_.titulo = "Minha aula";
	doc_.autor = "LumeBSD";
	QString erro;
	if (!gravarArquivo(saida, doc_, &erro))
		return "ERRO " + erro;
	Doc lido;
	abrirArquivo(saida, lido, &erro);
	return resumo(lido);
}

// --- a aparência -----------------------------------------------------------

static QString folhaDeEstilo(const Cores &c)
{
	QString s = R"QSS(
* { font-family: "IBM Plex Sans", "Noto Sans", sans-serif; font-size: 13px; color: @tinta; }
QMainWindow, QWidget#galeria, QStackedWidget { background: @papel; }
QScrollArea { background: transparent; }
QLabel { background: transparent; }
QLabel#marca { font-family: "IBM Plex Serif", serif; font-size: 30px; }
QLabel#sub, QLabel#vazio { font-family: "IBM Plex Serif", serif; font-style: italic; font-size: 15px; color: @apagado; }
QLabel#dica { font-size: 12px; color: @apagado; }
QLabel#parte { font-family: "IBM Plex Serif", serif; font-size: 21px; }
QLabel#rotulo { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: @apagado; letter-spacing: 1px; }
QLabel#tituloDoc { font-family: "IBM Plex Serif", serif; font-size: 15px; }
QLabel#previa { background: #000; border: 1px solid @tinta; }
QFrame#fio { background: @tinta; border: none; min-height: 1px; max-height: 1px; }
QToolBar#barra { background: @papel; border: none; border-bottom: 1px solid @tinta; padding: 4px 10px; spacing: 4px; }
QToolBar#barra QToolButton { font-size: 11px; padding: 3px 8px; border: 1px solid transparent; border-radius: 0; }
QToolBar#barra QToolButton:hover { border: 1px solid @tinta; }
QToolBar#barra QToolButton:checked { background: @tinta; color: @papel; }
QToolBar#barra QToolButton::menu-indicator { image: none; width: 0; }
QToolBar::separator { background: @fio; width: 1px; margin: 6px 6px; }
QListWidget#navegador { background: @cartao; border: none; border-right: 1px solid @tinta; padding: 6px 2px; outline: none; }
QListWidget#navegador::item { padding: 2px; border: 2px solid transparent; }
QListWidget#navegador::item:selected { background: transparent; border: 2px solid @selecao; }
QGraphicsView#tela { background: @mesa; border: none; }
QWidget#faixaNotas { background: @cartao; border-top: 1px solid @tinta; }
QPlainTextEdit#notas { background: @cartao; border: none; font-size: 14px; selection-background-color: @tinta; selection-color: @papel; }
QSplitter::handle { background: @fio; height: 1px; }
QStackedWidget#inspetor { background: @cartao; border-left: 1px solid @tinta; }
QStackedWidget#inspetor QScrollArea, QStackedWidget#inspetor QScrollArea > QWidget > QWidget, QStackedWidget#inspetor > QWidget { background: @cartao; }
QToolButton#alterna { background: @cartao; border: 1px solid @tinta; padding: 3px 6px; }
QToolButton#alterna:checked { background: @tinta; color: @cartao; }
QToolButton#alterna:hover { background: @fio; }
QToolButton#alterna:checked:hover { background: @tinta; }
QToolButton#amostra { border: 1px solid @tinta; padding: 4px 8px; background: @papel; text-align: left; }
QToolButton#amostra::menu-indicator { image: none; width: 0; }
QToolButton#cartaoModelo { border: 1px solid transparent; padding: 8px; font-family: "IBM Plex Serif", serif; font-size: 14px; }
QToolButton#cartaoModelo:hover { border: 1px solid @tinta; background: @cartao; }
QPushButton { background: @cartao; border: 1px solid @tinta; padding: 6px 14px; }
QPushButton:hover { background: @fio; }
QPushButton#botaoPrincipal { background: @tinta; color: @papel; font-weight: 600; }
QPushButton#recente { background: transparent; border: none; text-align: left; padding: 3px 0; font-family: "IBM Plex Serif", serif; font-size: 14px; }
QPushButton#recente:hover { text-decoration: underline; }
QComboBox, QLineEdit, QDoubleSpinBox, QFontComboBox { background: @papel; border: 1px solid @tinta; padding: 3px 6px; min-height: 22px; selection-background-color: @tinta; selection-color: @papel; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: @cartao; border: 1px solid @tinta; selection-background-color: @tinta; selection-color: @cartao; }
QTableWidget#dados { background: @papel; border: 1px solid @tinta; gridline-color: @fio; selection-background-color: @tinta; selection-color: @papel; }
QStatusBar { background: @papel; border-top: 1px solid @fio; }
QMenu { background: @cartao; border: 1px solid @tinta; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 12px; }
QMenu::item:selected { background: @tinta; color: @cartao; }
QScrollBar:vertical { background: transparent; width: 11px; }
QScrollBar::handle:vertical { background: @fio; min-height: 30px; }
QScrollBar:horizontal { background: transparent; height: 11px; }
QScrollBar::handle:horizontal { background: @fio; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
)QSS";
	s.replace("@papel", c.papel.name()).replace("@tinta", c.tinta.name()).replace("@cartao", c.cartao.name())
	 .replace("@mesa", c.mesa.name()).replace("@fio", c.fio.name()).replace("@apagado", c.apagado.name())
	 .replace("@selecao", c.selecao.name());
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
	c.fio = c.noite ? QColor(0x45, 0x42, 0x3c) : QColor(0xc4, 0xbd, 0xad);
	c.apagado = c.noite ? QColor(0x9a, 0x94, 0x88) : QColor(0x5d, 0x59, 0x50);
	c.selecao = QColor(0x2f, 0x6f, 0xd6);
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette pal;
	pal.setColor(QPalette::Window, c.papel);
	pal.setColor(QPalette::Base, c.cartao);
	pal.setColor(QPalette::AlternateBase, c.papel);
	pal.setColor(QPalette::Button, c.papel);
	for (auto r : {QPalette::WindowText, QPalette::ButtonText, QPalette::ToolTipBase})
		pal.setColor(r, c.tinta);
	pal.setColor(QPalette::Text, c.tinta);
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
	             "uso: lume-palco [ARQUIVO]\n"
	             "     lume-palco --converter ENTRADA SAIDA   (pptx, odp, pdf)\n"
	             "     lume-palco --modelo NOME [SAIDA]       (branco, aula, seminario, conjuntura, reuniao)\n"
	             "     lume-palco --resumo ARQUIVO\n"
	             "     lume-palco --autoteste SAIDA\n"
	             "     lume-palco --foto SAIDA.png [ARQUIVO | --modelo NOME | --galeria]\n");
	return 2;
}

int main(int argc, char **argv)
{
	bool semJanela = false;
	for (int i = 1; i < argc; i++) {
		const QByteArray a(argv[i]);
		if (a == "--converter" || a == "--resumo" || a == "--autoteste" || (a == "--modelo" && i == 1 && argc > 3))
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
	app.setApplicationName("lume-palco");
	app.setApplicationDisplayName("Palco");
	app.setDesktopFileName("lume-palco");
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	                 [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });
	QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));
	const QStringList a = app.arguments();

	if (a.size() >= 4 && a[1] == "--converter") {
		Doc d;
		QString erro;
		if (!abrirArquivo(a[2], d, &erro) || !gravarArquivo(a[3], d, &erro)) {
			std::fprintf(stderr, "lume-palco: %s\n", qPrintable(erro));
			return 1;
		}
		return 0;
	}
	if (a.size() >= 4 && a[1] == "--modelo") {
		const bool existe = std::any_of(modelos().begin(), modelos().end(), [&](const Modelo &m) { return m.id == a[2]; });
		if (!existe)
			return uso();
		Doc d;
		montarModelo(d, a[2]);
		QString erro;
		if (!gravarArquivo(a[3], d, &erro)) {
			std::fprintf(stderr, "lume-palco: %s\n", qPrintable(erro));
			return 1;
		}
		return 0;
	}
	if (a.size() >= 3 && a[1] == "--resumo") {
		Doc d;
		QString erro;
		if (!abrirArquivo(a[2], d, &erro)) {
			std::fprintf(stderr, "lume-palco: %s\n", qPrintable(erro));
			return 1;
		}
		std::fputs(resumo(d).toUtf8().constData(), stdout);
		return 0;
	}
	if (a.size() >= 2 && a[1].startsWith("--") && a[1] != "--autoteste" && a[1] != "--foto" && a[1] != "--modelo")
		return uso();

	aparencia(app);
	Janela j;
	if (a.size() >= 3 && a[1] == "--autoteste") {
		std::fputs(j.autoteste(a[2]).toUtf8().constData(), stdout);
		return 0;
	}
	if (a.size() >= 3 && a[1] == "--foto") {
		// LUME_FOTO_SLIDE (o número), LUME_FOTO_OBJETO (escolhe um objeto),
		// LUME_FOTO_PAINEL (0 Formatar, 1 Transição, 2 Documento, 3 Animar),
		// LUME_FOTO_MODO (apresentacao, apresentador), LUME_FOTO_PASSO (os
		// cliques já dados no slide, na apresentação), LUME_FOTO_ESPERA (ms).
		if (a.size() >= 5 && a[3] == "--modelo")
			j.modelo(a[4]);
		else if (a.size() >= 4 && a[3] != "--galeria")
			j.abrir(a[3]);
		const QStringList t = qEnvironmentVariable("LUME_FOTO_TAMANHO", "1366x860").split('x');
		const QSize tam(t.value(0).toInt(), t.value(1).toInt());
		if (qEnvironmentVariableIsSet("LUME_FOTO_SLIDE"))
			j.irParaSlide(qEnvironmentVariableIntValue("LUME_FOTO_SLIDE") - 1);
		if (qEnvironmentVariableIsSet("LUME_FOTO_PAINEL"))
			j.mostrarPainel(qEnvironmentVariableIntValue("LUME_FOTO_PAINEL"));
		const QString modo = qEnvironmentVariable("LUME_FOTO_MODO");
		QWidget *alvo = &j;
		if (modo == "apresentacao" || modo == "apresentador") {
			QWidget *w = nullptr;
			Sessao *s = j.sessaoParaFoto(modo == "apresentador", w);
			Q_UNUSED(s);
			alvo = w;
		}
		alvo->resize(tam);
		alvo->show();
		if (alvo == &j && qEnvironmentVariableIsSet("LUME_FOTO_OBJETO"))
			QTimer::singleShot(200, &app, [&] { j.escolherObjeto(qEnvironmentVariableIntValue("LUME_FOTO_OBJETO") - 1); });
		QTimer::singleShot(qEnvironmentVariableIsSet("LUME_FOTO_ESPERA") ? qEnvironmentVariableIntValue("LUME_FOTO_ESPERA") : 900, &app, [&] {
			alvo->grab().save(a[2]);
			app.quit();
		});
		return app.exec();
	}
	if (a.size() >= 3 && a[1] == "--modelo")
		j.modelo(a[2]);
	else if (a.size() >= 2 && !a[1].startsWith("--"))
		j.abrir(a[1]);
	const QRect tela = app.primaryScreen() ? app.primaryScreen()->availableGeometry() : QRect(0, 0, 1920, 1080);
	if (tela.width() < 1500 || tela.height() < 950)
		j.showMaximized();
	else
		j.show();
	// As mesmas escolhas das fotos valem na janela de verdade (para as fotos
	// da documentação tiradas na sessão do KDE).
	if (qEnvironmentVariableIsSet("LUME_FOTO_SLIDE"))
		j.irParaSlide(qEnvironmentVariableIntValue("LUME_FOTO_SLIDE") - 1);
	if (qEnvironmentVariableIsSet("LUME_FOTO_PAINEL"))
		j.mostrarPainel(qEnvironmentVariableIntValue("LUME_FOTO_PAINEL"));
	if (qEnvironmentVariableIsSet("LUME_FOTO_OBJETO"))
		QTimer::singleShot(400, &app, [&] { j.escolherObjeto(qEnvironmentVariableIntValue("LUME_FOTO_OBJETO") - 1); });
	if (qEnvironmentVariable("LUME_FOTO_MODO") == "apresentador")
		QTimer::singleShot(1500, &app, [&] { j.apresentarAgora(true); });
	else if (qEnvironmentVariable("LUME_FOTO_MODO") == "apresentacao")
		QTimer::singleShot(1500, &app, [&] { j.apresentarAgora(false); });
	return app.exec();
}

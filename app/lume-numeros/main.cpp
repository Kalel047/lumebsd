// SPDX-License-Identifier: BSD-2-Clause
//
// lume-numeros - "Números do LumeBSD": a planilha, com a cara do Numbers (o
// do iWork) e o papel do LumeBSD. Fórmulas em português como no Excel
// (=SOMA(A1:A3), =PROCV(...), =VPL(...)), gráficos sobre a folha, e os
// formatos de todo mundo, com leitores e gravadores daqui:
//
//   .xlsx  Excel (o padrão)        .ods  OpenDocument (o do LibreOffice)
//   .csv   texto separado          .pdf  exportar
//
// Os modelos da galeria são para quem estuda, ensina e cuida das contas:
// orçamento pessoal, notas da turma, juros compostos, fluxo de caixa (VPL e
// TIR) e indicadores econômicos.
//
// A linha de comando, sem janela (é como os testes conferem):
//
//   lume-numeros [ARQUIVO]
//   lume-numeros --converter ENTRADA SAIDA   (xlsx, ods, csv, pdf)
//   lume-numeros --modelo NOME SAIDA
//   lume-numeros --resumo ARQUIVO
//   lume-numeros --autoteste SAIDA
//   lume-numeros --foto SAIDA.png [ARQUIVO | --modelo NOME | --galeria]
//
// Sem moc, como os outros apps do LumeBSD: os sinais do Qt ligados a
// lambdas. As partes: formulas.h (o analisador e o cálculo), planilha.h (as
// células e as operações), desenho.h (as células e os gráficos no papel),
// arquivos.h (xlsx, ods, csv, pdf), modelos.h (a galeria).

#include "arquivos.h"
#include "modelos.h"

#include "../comum/grafico.h"
#include "../comum/replicante.h"
#include "../comum/vinculo.h"

// Os dados de um gráfico da folha, como os outros apps do escritório colam
// (a área e a dispersão vão como linhas).
static DadosGrafico dadosParaColar(const Pasta &p, int f, const Grafico &g)
{
	DadosGrafico d;
	d.tipo = g.tipo == Grafico::Barras ? TipoGrafico::Barras : g.tipo == Grafico::Pizza ? TipoGrafico::Pizza
	         : g.tipo == Grafico::Colunas ? TipoGrafico::Colunas : TipoGrafico::Linhas;
	d.titulo = g.titulo;
	std::vector<Serie> series;
	p.dadosDoGrafico(f, g, d.categorias, series);
	for (const Serie &s : series) {
		d.series << s.nome;
		d.valores.push_back(s.v);
	}
	d.legenda = series.size() > 1 || d.tipo == TipoGrafico::Pizza;
	return d;
}

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSessionManager>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>

#include <cstdio>

// --- cores -----------------------------------------------------------------

struct Cores {
	bool noite = false;
	QColor papel, tinta, cartao, mesa, fio, apagado, folha, grade, cabecalho, selecao;
};
static Cores cores_;

// --- o modelo da grade -----------------------------------------------------

class Grade : public QAbstractTableModel
{
public:
	Grade(Pasta &p) : p_(p) {}

	void folha(int f)
	{
		beginResetModel();
		f_ = f;
		medir();
		endResetModel();
	}
	int folha() const { return f_; }

	// Cresce quando se chega perto do fim (sempre um tanto de linhas e de
	// colunas vazias além do que tem dado).
	void garantir(int r, int c)
	{
		const int nr = std::max(linhas_, r + 60), nc = std::max(colunas_, c + 8);
		if (nr > linhas_) {
			beginInsertRows(QModelIndex(), linhas_, nr - 1);
			linhas_ = nr;
			endInsertRows();
		}
		if (nc > colunas_) {
			beginInsertColumns(QModelIndex(), colunas_, nc - 1);
			colunas_ = nc;
			endInsertColumns();
		}
	}

	void tudoMudou()
	{
		const Folha &fo = p_.folha(f_);
		garantir(fo.ultLinha, fo.ultColuna);
		emit dataChanged(index(0, 0), index(linhas_ - 1, colunas_ - 1));
	}

	int rowCount(const QModelIndex &pai = QModelIndex()) const override { return pai.isValid() ? 0 : linhas_; }
	int columnCount(const QModelIndex &pai = QModelIndex()) const override { return pai.isValid() ? 0 : colunas_; }

	QVariant data(const QModelIndex &i, int papel) const override
	{
		const Celula *k = p_.folha(f_).em(i.row(), i.column());
		switch (papel) {
		case Qt::DisplayRole:
			return k ? mostrar(k->v, k->fmt) : QString();
		case Qt::EditRole:
			return k ? paraEditar(*k) : QString();
		case Qt::FontRole: {
			QFont f(p_.fonte);
			f.setPointSizeF(p_.tamanho);
			if (k) {
				f.setBold(k->fmt.negrito);
				f.setItalic(k->fmt.italico);
				f.setUnderline(k->fmt.sublinhado);
				if (k->fmt.tamanho > 0)
					f.setPointSizeF(k->fmt.tamanho);
			}
			return f;
		}
		case Qt::ForegroundRole:
			if (k && k->v.t == Valor::Erro)
				return QColor(0x9a, 0x2a, 0x2a);
			if (k && k->fmt.cor.isValid())
				return k->fmt.cor;
			return QVariant();
		case Qt::BackgroundRole:
			if (k && k->fmt.fundo.isValid())
				return k->fmt.fundo;
			return QVariant();
		case Qt::TextAlignmentRole: {
			int al = k ? k->fmt.alin : 0;
			if (al == 0)
				al = k && k->v.t == Valor::Numero ? 3 : k && (k->v.t == Valor::Logico || k->v.t == Valor::Erro) ? 2 : 1;
			return int((al == 2 ? Qt::AlignHCenter : al == 3 ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
		}
		case Qt::ToolTipRole:
			if (k && k->v.t == Valor::Erro) {
				static const QHash<QString, QString> m = {
					{"#DIV/0!", "Divisão por zero."}, {"#VALUE!", "Um valor do tipo errado (texto onde se esperava número?)."},
					{"#REF!", "A fórmula aponta para uma célula que não existe mais."}, {"#NAME?", "Uma função ou um nome que não existe."},
					{"#N/A", "Não achou o que procurava."}, {"#NUM!", "Um número fora do possível."},
					{"#CIRC!", "Referência circular: a fórmula depende dela mesma."}};
				return m.value(k->v.s, erroBrasil(k->v.s));
			}
			return QVariant();
		}
		return QVariant();
	}

	QVariant headerData(int s, Qt::Orientation o, int papel) const override
	{
		if (papel == Qt::DisplayRole)
			return o == Qt::Horizontal ? nomeColuna(s) : QString::number(s + 1);
		if (papel == Qt::TextAlignmentRole)
			return int(Qt::AlignCenter);
		return QVariant();
	}

	Qt::ItemFlags flags(const QModelIndex &) const override
	{
		return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
	}

	std::function<void(int, int, const QString &)> aoDigitar;

	bool setData(const QModelIndex &i, const QVariant &v, int papel) override
	{
		if (papel != Qt::EditRole || !aoDigitar)
			return false;
		aoDigitar(i.row(), i.column(), v.toString());
		return true;
	}

private:
	void medir()
	{
		const Folha &fo = p_.folha(f_);
		linhas_ = std::max(200, fo.ultLinha + 60);
		colunas_ = std::max(26, fo.ultColuna + 8);
		for (const Grafico &g : fo.graficos) {
			int r, c;
			double dx, dy;
			p_.celulaEm(f_, g.x + g.w, g.y + g.h, r, c, dx, dy);
			linhas_ = std::max(linhas_, r + 20);
			colunas_ = std::max(colunas_, c + 4);
		}
	}

	Pasta &p_;
	int f_ = 0;
	int linhas_ = 200, colunas_ = 26;
};

// Desenha cada célula inteira (sem a grade do QTableView): o fundo, a
// grade, as bordas e o texto. Texto comprido à esquerda transborda para as
// vizinhas vazias, como no Excel e no Numbers; cada célula desenha só o seu
// pedaço (assim um repintar parcial não apaga nada).
class Pincel : public QStyledItemDelegate
{
public:
	Pincel(Pasta &p, Grade &g, QTableView *v) : QStyledItemDelegate(v), p_(p), g_(g), v_(v) {}

	void paint(QPainter *pt, const QStyleOptionViewItem &op, const QModelIndex &i) const override
	{
		const Folha &fo = p_.folha(g_.folha());
		const int r = i.row(), c = i.column();
		const QRect rc = op.rect;
		const Celula *k = fo.em(r, c);
		pt->save();
		if (k && k->fmt.fundo.isValid())
			pt->fillRect(rc, k->fmt.fundo);
		// De quem é o texto que aparece aqui: desta célula ou de uma à
		// esquerda que transborda.
		int fonte = -1;
		if (k && !k->vazia())
			fonte = c;
		else
			for (int cc = c - 1; cc >= std::max(0, c - 30); cc--) {
				const Celula *kk = fo.em(r, cc);
				if (kk && !kk->vazia()) {
					fonte = cc;
					break;
				}
			}
		int direita = rc.right(); // até onde vai o texto desta linha
		if (fonte >= 0) {
			const Celula &kf = *fo.em(r, fonte);
			const QString t = mostrar(kf.v, kf.fmt);
			QFont f = g_.data(g_.index(r, fonte), Qt::FontRole).value<QFont>();
			const int al = g_.data(g_.index(r, fonte), Qt::TextAlignmentRole).toInt();
			const int x0 = v_->columnViewportPosition(fonte);
			int x1 = x0 + v_->columnWidth(fonte);
			const int precisa = QFontMetrics(f).horizontalAdvance(t) + 8;
			// Só o texto alinhado à esquerda transborda (e só por vazias).
			if ((al & Qt::AlignLeft) && kf.v.t == Valor::Texto)
				for (int cc = fonte + 1; x1 - x0 < precisa && cc < g_.columnCount(); cc++) {
					const Celula *kk = fo.em(r, cc);
					if (kk && !kk->vazia())
						break;
					x1 += v_->columnWidth(cc);
				}
			if (fonte == c || x1 > rc.left()) {
				const QRect alvo(x0 + 4, rc.top(), x1 - x0 - 8, rc.height());
				pt->setClipRect(rc.adjusted(0, 0, fonte == c && x1 <= rc.right() + 1 ? -3 : 0, 0));
				pt->setFont(f);
				const QVariant cor = g_.data(g_.index(r, fonte), Qt::ForegroundRole);
				pt->setPen(cor.isValid() ? cor.value<QColor>() : QColor(0x1c, 0x1b, 0x19));
				const int alH = al & Qt::AlignHorizontal_Mask;
				// Número que não cabe: "###", como no Excel (um número
				// cortado engana; o valor aparece na barra de fórmula).
				QString escrito = t;
				if (kf.v.t == Valor::Numero && precisa > x1 - x0)
					escrito = QString(std::max(1, (x1 - x0 - 8) / std::max(1, QFontMetrics(f).horizontalAdvance('#'))), '#');
				pt->drawText(alvo, alH | Qt::AlignVCenter | Qt::TextSingleLine, escrito);
				pt->setClipping(false);
				direita = x1 - 1;
			}
		}
		// A grade (embaixo e à direita, menos onde o texto passa).
		pt->setPen(QPen(cores_.grade, 1));
		pt->drawLine(rc.left(), rc.bottom(), rc.right(), rc.bottom());
		if (direita <= rc.right())
			pt->drawLine(rc.right(), rc.top(), rc.right(), rc.bottom());
		if (k && k->fmt.borda) {
			pt->setPen(QPen(cores_.tinta, 1));
			pt->drawRect(QRectF(rc).adjusted(-0.5, -0.5, -0.5, -0.5));
		}
		if (op.state & QStyle::State_Selected)
			pt->fillRect(rc, cores_.selecao);
		pt->restore();
	}

	QWidget *createEditor(QWidget *pai, const QStyleOptionViewItem &op, const QModelIndex &i) const override
	{
		QWidget *e = QStyledItemDelegate::createEditor(pai, op, i);
		if (auto *l = qobject_cast<QLineEdit *>(e)) {
			l->setFrame(false);
			l->setObjectName("editorCelula");
		}
		return e;
	}

private:
	Pasta &p_;
	Grade &g_;
	QTableView *v_;
};

// --- a tabela na tela ------------------------------------------------------

class Tabela : public QTableView
{
public:
	std::function<bool(QKeyEvent *)> teclas;
	std::function<void(const QPoint &)> menu;
	bool editando() const { return state() == QAbstractItemView::EditingState; }
	std::function<QAbstractItemDelegate *(QTableView *)> pincel;

	// Os painéis congelados: as primeiras linhas e colunas ficam paradas,
	// em vistas por cima desta (o mesmo modelo, a mesma seleção), como no
	// exemplo "Frozen Column" do Qt.
	void congelar(int linhas, int colunas)
	{
		fr_ = std::max(0, linhas);
		fc_ = std::max(0, colunas);
		if ((fr_ || fc_) && !topo_) {
			topo_ = nova(false, true);
			lado_ = nova(true, false);
			canto_ = nova(false, false);
			connect(horizontalScrollBar(), &QScrollBar::valueChanged, topo_->horizontalScrollBar(), &QScrollBar::setValue);
			connect(verticalScrollBar(), &QScrollBar::valueChanged, lado_->verticalScrollBar(), &QScrollBar::setValue);
		}
		sincronizar();
	}

	void sincronizar()
	{
		if (!topo_)
			return;
		const bool l = fr_ > 0, c = fc_ > 0;
		topo_->setVisible(l);
		lado_->setVisible(c);
		canto_->setVisible(l && c);
		if (!l && !c)
			return;
		for (QTableView *t : {topo_, lado_, canto_}) {
			t->horizontalHeader()->setDefaultSectionSize(horizontalHeader()->defaultSectionSize());
			for (int k = 0; k < model()->columnCount(); k++)
				if (t->columnWidth(k) != columnWidth(k))
					t->setColumnWidth(k, columnWidth(k));
			const int nr = t == lado_ ? model()->rowCount() : fr_;
			for (int k = 0; k < nr; k++)
				if (t->rowHeight(k) != rowHeight(k))
					t->setRowHeight(k, rowHeight(k));
			t->verticalHeader()->setFixedWidth(verticalHeader()->width());
			t->horizontalHeader()->setFixedHeight(horizontalHeader()->height());
		}
		int alt = 0, larg = 0;
		for (int k = 0; k < fr_; k++) alt += rowHeight(k);
		for (int k = 0; k < fc_; k++) larg += columnWidth(k);
		const int f = frameWidth(), vh = verticalHeader()->width(), hh = horizontalHeader()->height();
		const QRect vp = viewport()->geometry();
		topo_->setGeometry(f, hh + f, vh + vp.width(), alt);
		lado_->setGeometry(vh + f, f, larg, hh + vp.height());
		canto_->setGeometry(vh + f, hh + f, larg, alt);
		topo_->horizontalScrollBar()->setValue(horizontalScrollBar()->value());
		lado_->verticalScrollBar()->setValue(verticalScrollBar()->value());
		for (QTableView *t : {topo_, lado_, canto_})
			if (t->isVisible()) t->raise();
	}

	void scrollTo(const QModelIndex &i, ScrollHint dica = EnsureVisible) override
	{
		const int h = horizontalScrollBar()->value(), v = verticalScrollBar()->value();
		QTableView::scrollTo(i, dica);
		if (!i.isValid())
			return;
		// Uma célula parada não rola; uma das outras não fica embaixo das paradas.
		if (i.row() < fr_)
			verticalScrollBar()->setValue(v);
		else if (fr_) {
			int alt = 0;
			for (int k = 0; k < fr_; k++) alt += rowHeight(k);
			const int y = rowViewportPosition(i.row());
			if (y < alt)
				verticalScrollBar()->setValue(verticalScrollBar()->value() - (alt - y));
		}
		if (i.column() < fc_)
			horizontalScrollBar()->setValue(h);
		else if (fc_) {
			int larg = 0;
			for (int k = 0; k < fc_; k++) larg += columnWidth(k);
			const int x = columnViewportPosition(i.column());
			if (x < larg)
				horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (larg - x));
		}
	}

	explicit Tabela(QWidget *pai) : QTableView(pai)
	{
		setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
		setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		setEditTriggers(QAbstractItemView::AnyKeyPressed | QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
		setSelectionMode(QAbstractItemView::ExtendedSelection);
		setSelectionBehavior(QAbstractItemView::SelectItems);
		verticalHeader()->setDefaultSectionSize(ALTURA_PADRAO);
		verticalHeader()->setMinimumSectionSize(14);
		verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);
		horizontalHeader()->setDefaultSectionSize(LARGURA_PADRAO);
		horizontalHeader()->setMinimumSectionSize(20);
		horizontalHeader()->setHighlightSections(true);
		verticalHeader()->setHighlightSections(true);
		setCornerButtonEnabled(true);
		setShowGrid(false); // o Pincel desenha a grade
		setWordWrap(false);
		setContextMenuPolicy(Qt::DefaultContextMenu);
	}

protected:
	void keyPressEvent(QKeyEvent *e) override
	{
		if (teclas && teclas(e))
			return;
		QTableView::keyPressEvent(e);
	}

	void resizeEvent(QResizeEvent *e) override
	{
		QTableView::resizeEvent(e);
		sincronizar();
	}

	void updateGeometries() override
	{
		QTableView::updateGeometries();
		sincronizar();
	}

	// Enter confirma e desce; Tab confirma e vai para o lado (como no Excel).
	void closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint dica) override
	{
		QTableView::closeEditor(editor, QAbstractItemDelegate::NoHint);
		const QModelIndex i = currentIndex();
		if (dica == QAbstractItemDelegate::SubmitModelCache) {
			setCurrentIndex(model()->index(std::min(i.row() + 1, model()->rowCount() - 1), i.column()));
		} else if (dica == QAbstractItemDelegate::EditNextItem) {
			setCurrentIndex(model()->index(i.row(), std::min(i.column() + 1, model()->columnCount() - 1)));
		} else if (dica == QAbstractItemDelegate::EditPreviousItem) {
			setCurrentIndex(model()->index(i.row(), std::max(i.column() - 1, 0)));
		}
	}

	void contextMenuEvent(QContextMenuEvent *e) override
	{
		if (menu)
			menu(e->globalPos());
	}

private:
	QTableView *nova(bool cabecalhoH, bool cabecalhoV)
	{
		auto *t = new QTableView(this);
		t->setObjectName("tabela");
		t->setModel(model());
		t->setSelectionModel(selectionModel());
		if (pincel)
			t->setItemDelegate(pincel(t));
		t->setFocusPolicy(Qt::NoFocus);
		t->setAutoScroll(false); // quem rola é a de baixo (a seleção é a mesma)
		t->setShowGrid(false);
		t->setWordWrap(false);
		t->setFrameStyle(QFrame::NoFrame);
		t->setEditTriggers(editTriggers());
		t->setSelectionMode(selectionMode());
		t->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
		t->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		t->horizontalHeader()->setVisible(cabecalhoH);
		t->verticalHeader()->setVisible(cabecalhoV);
		t->horizontalHeader()->setHighlightSections(true);
		t->verticalHeader()->setHighlightSections(true);
		t->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
		t->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
		t->verticalHeader()->setDefaultSectionSize(ALTURA_PADRAO);
		t->setContextMenuPolicy(Qt::NoContextMenu);
		viewport()->stackUnder(t);
		t->hide();
		return t;
	}

	QTableView *topo_ = nullptr, *lado_ = nullptr, *canto_ = nullptr;
	int fr_ = 0, fc_ = 0;
};

// --- um gráfico sobre a folha ----------------------------------------------

class Quadro : public QWidget
{
public:
	std::function<void()> mexeu, escolhido;
	std::function<void(const QPoint &)> menu;

	Quadro(Pasta &p, int f, int i, QWidget *pai) : QWidget(pai), p_(p), f_(f), i_(i)
	{
		setMouseTracking(true);
		setFocusPolicy(Qt::ClickFocus);
	}
	int indice() const { return i_; }
	void indice(int i) { i_ = i; }
	void marcar(bool m)
	{
		marcado_ = m;
		update();
	}
	bool marcado() const { return marcado_; }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter pt(this);
		const Grafico &g = p_.folha(f_).graficos[size_t(i_)];
		Tinta t;
		desenharGrafico(pt, rect(), p_, f_, g, t);
		if (marcado_) {
			pt.setPen(QPen(QColor(0x2f, 0x4b, 0x6e), 2));
			pt.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
			pt.fillRect(QRect(width() - 10, height() - 10, 9, 9), QColor(0x2f, 0x4b, 0x6e));
		}
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		if (escolhido)
			escolhido();
		if (e->button() != Qt::LeftButton)
			return;
		ini_ = e->globalPosition().toPoint();
		const Grafico &g = p_.folha(f_).graficos[size_t(i_)];
		orig_ = QRectF(g.x, g.y, g.w, g.h);
		redim_ = e->position().x() > width() - 14 && e->position().y() > height() - 14;
		arrastando_ = true;
	}

	void mouseMoveEvent(QMouseEvent *e) override
	{
		setCursor(e->position().x() > width() - 14 && e->position().y() > height() - 14 ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
		if (!arrastando_)
			return;
		const QPoint d = e->globalPosition().toPoint() - ini_;
		Grafico &g = p_.folha(f_).graficos[size_t(i_)];
		if (redim_) {
			g.w = std::max(160.0, orig_.width() + d.x());
			g.h = std::max(120.0, orig_.height() + d.y());
		} else {
			g.x = std::max(0.0, orig_.x() + d.x());
			g.y = std::max(0.0, orig_.y() + d.y());
		}
		if (mexeu)
			mexeu();
	}

	void mouseReleaseEvent(QMouseEvent *) override { arrastando_ = false; }

	void contextMenuEvent(QContextMenuEvent *e) override
	{
		if (menu)
			menu(e->globalPos());
	}

private:
	Pasta &p_;
	int f_, i_;
	bool marcado_ = false, arrastando_ = false, redim_ = false;
	QPoint ini_;
	QRectF orig_;
};

// --- a janela --------------------------------------------------------------

static QString rotulo(const QString &t) { return t.toUpper(); }

class Janela : public QMainWindow
{
public:
	Janela() : grade_(pasta_)
	{
		setWindowIcon(QIcon::fromTheme("lume-numeros", QIcon("/usr/share/lume/app/lume-numeros.svg")));
		pilha_ = new QStackedWidget(this);
		setCentralWidget(pilha_);
		montarGaleria();
		montarEditor();
		montarBarra();
		montarInspetor();
		statusBar()->setSizeGripEnabled(false);
		contas_ = new QLabel(this);
		contas_->setObjectName("rodape");
		statusBar()->addPermanentWidget(contas_);
		resize(1280, 860);
		mostrarGaleria();
	}

	void abrir(const QString &arq)
	{
		Pasta nova;
		QString erro;
		if (!abrirArquivo(arq, nova, &erro)) {
			QMessageBox::warning(this, "Números", "Não deu para abrir " + QFileInfo(arq).fileName() + ":\n" + erro);
			return;
		}
		pasta_ = nova;
		arquivo_ = QFileInfo(arq).absoluteFilePath();
		if (extensao(arquivo_) == "csv")
			arquivo_ = QFileInfo(arquivo_).path() + "/" + QFileInfo(arquivo_).completeBaseName() + ".xlsx", csv_ = true;
		recente(QFileInfo(arq).absoluteFilePath());
		depoisDeCarregar();
	}

	void modelo(const QString &id)
	{
		montarModelo(pasta_, id);
		arquivo_.clear();
		depoisDeCarregar();
	}

	void mostrarGaleria()
	{
		atualizarRecentes();
		pilha_->setCurrentIndex(0);
		barra_->setVisible(false);
		statusBar()->setVisible(false);
		setWindowTitle("Números");
	}

	// Edita como uma pessoa (pelos mesmos caminhos da janela), grava e
	// devolve o resumo do que voltou do arquivo.
	QString autoteste(const QString &saida)
	{
		modelo("branco");
		auto ir = [&](const QString &c) {
			int r, k;
			Montador::cel(c, r, k);
			tabela_->setCurrentIndex(grade_.index(r, k));
		};
		auto digita = [&](const QString &c, const QString &t) {
			ir(c);
			grade_.setData(tabela_->currentIndex(), t, Qt::EditRole);
		};
		digita("A1", "Produto");
		digita("B1", "Preço");
		digita("C1", "Qtd.");
		digita("D1", "Total");
		digita("A2", "Café");
		digita("B2", "R$ 18,90");
		digita("C2", "3");
		digita("D2", "=B2*C2");
		digita("A3", "Pão");
		digita("B3", "0,75");
		digita("C3", "12");
		digita("A4", "Leite");
		digita("B4", "5,49");
		digita("C4", "6");
		// Preencher para baixo (Ctrl+D) a fórmula do total.
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 3), grade_.index(3, 3)), QItemSelectionModel::ClearAndSelect);
		tabela_->setCurrentIndex(grade_.index(1, 3));
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 3), grade_.index(3, 3)), QItemSelectionModel::ClearAndSelect);
		preencher(true);
		digita("A5", "Soma");
		digita("D5", "=SOMA(D2:D4)");
		// Negrito no cabeçalho, moeda no preço e no total.
		tabela_->selectionModel()->select(QItemSelection(grade_.index(0, 0), grade_.index(0, 3)), QItemSelectionModel::ClearAndSelect);
		formatar([](Formato &f) { f.negrito = true; });
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 1), grade_.index(4, 1)), QItemSelectionModel::ClearAndSelect);
		formatar([](Formato &f) { f.tipo = Formato::Moeda; f.casas = 2; });
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 3), grade_.index(4, 3)), QItemSelectionModel::ClearAndSelect);
		formatar([](Formato &f) { f.tipo = Formato::Moeda; f.casas = 2; });
		// Copiar A2:D2 e colar em A7 (a fórmula anda).
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 0), grade_.index(1, 3)), QItemSelectionModel::ClearAndSelect);
		copiar(false);
		ir("A7");
		colar();
		// Inserir uma linha antes da 3 (a soma acompanha).
		ir("A3");
		inserirLinhas(false, 1);
		digita("A3", "Açúcar");
		digita("B3", "4,2");
		digita("C3", "2");
		digita("D3", "=B3*C3");
		// Ordenar A2:D5 pelo total, do maior para o menor.
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 0), grade_.index(4, 3)), QItemSelectionModel::ClearAndSelect);
		ir("D2");
		tabela_->selectionModel()->select(QItemSelection(grade_.index(1, 0), grade_.index(4, 3)), QItemSelectionModel::ClearAndSelect);
		ordenar(false);
		// Um gráfico de colunas de A1:A5 e D1:D5.
		tabela_->selectionModel()->select(QItemSelection(grade_.index(0, 0), grade_.index(4, 3)), QItemSelectionModel::ClearAndSelect);
		inserirGrafico(Grafico::Colunas);
		// Desfazer e refazer a última digitação.
		digita("F1", "apagar");
		desfazer();
		// Outra folha, com referência para a primeira.
		novaFolha();
		digita("A1", "='Planilha 1'!D6*2");
		QString erro;
		if (!gravarArquivo(saida, pasta_, &erro))
			return "ERRO " + erro;
		Pasta lida;
		abrirArquivo(saida, lida, &erro);
		return resumo(lida);
	}

	// Para as fotos e os testes: escolhe uma célula (rola até ela).
	void irPara(const QString &cel)
	{
		PExpr e = analisar(cel.trimmed().toUpper(), Dialeto::Canonico);
		if (!e || e->k != Expr::Ref)
			return;
		grade_.garantir(e->r1, e->c1);
		tabela_->setCurrentIndex(grade_.index(e->r1, e->c1));
		tabela_->scrollTo(grade_.index(e->r1, e->c1));
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
		auto *s = new QLabel("Para começar uma planilha nova. Todas abrem e salvam em .xlsx e .ods.");
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
			b->setIconSize(QSize(220, 150));
			b->setText(m.nome);
			b->setToolTip(m.descricao);
			b->setCursor(Qt::PointingHandCursor);
			const QString id = m.id;
			connect(b, &QToolButton::clicked, this, [this, id] { modelo(id); });
			grade->addWidget(b, i / 3, i % 3, Qt::AlignTop | Qt::AlignLeft);
			i++;
		}
		grade->setColumnStretch(3, 1);
		v->addLayout(grade);
		v->addSpacing(22);
		auto *linha = new QHBoxLayout;
		auto *ab = new QPushButton("Abrir uma planilha…");
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
		Pasta p;
		montarModelo(p, id);
		QImage img(660, 450, QImage::Format_ARGB32_Premultiplied);
		img.fill(QColor(0xff, 0xfd, 0xf8));
		{
			QPainter pt(&img);
			pt.setRenderHint(QPainter::Antialiasing);
			pt.setRenderHint(QPainter::TextAntialiasing);
			pt.scale(0.62, 0.62);
			desenharCelulas(pt, p, 0, 0, 0, 30, 14);
			for (const Grafico &g : p.folha(0).graficos)
				desenharGrafico(pt, QRectF(g.x, g.y, g.w, g.h), p, 0, g);
			pt.resetTransform();
			pt.setPen(QPen(QColor(0x1c, 0x1b, 0x19), 3));
			pt.drawRect(QRectF(img.rect()).adjusted(1.5, 1.5, -1.5, -1.5));
		}
		return img.scaled(220, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	void atualizarRecentes()
	{
		while (QLayoutItem *it = recentes_->takeAt(0)) {
			delete it->widget();
			delete it;
		}
		const QStringList l = QSettings("LumeBSD", "lume-numeros").value("recentes").toStringList();
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
			auto *v = new QLabel("As planilhas que você abrir aparecem aqui.");
			v->setObjectName("vazio");
			recentes_->addWidget(v);
		}
	}

	void recente(const QString &a)
	{
		QSettings s("LumeBSD", "lume-numeros");
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
		// A barra de fórmulas.
		auto *fb = new QWidget;
		fb->setObjectName("barraFormula");
		auto *fh = new QHBoxLayout(fb);
		fh->setContentsMargins(10, 5, 10, 5);
		fh->setSpacing(8);
		nomeCel_ = new QLineEdit;
		nomeCel_->setObjectName("nomeCelula");
		nomeCel_->setFixedWidth(84);
		nomeCel_->setAlignment(Qt::AlignCenter);
		auto *fx = new QLabel("fx");
		fx->setObjectName("fx");
		formula_ = new QLineEdit;
		formula_->setObjectName("formula");
		formula_->setPlaceholderText("Digite um valor ou uma fórmula (=SOMA(A1:A10))");
		fh->addWidget(nomeCel_);
		fh->addWidget(fx);
		fh->addWidget(formula_, 1);
		cv->addWidget(fb);
		tabela_ = new Tabela(coluna);
		tabela_->setObjectName("tabela");
		tabela_->setModel(&grade_);
		tabela_->setItemDelegate(new Pincel(pasta_, grade_, tabela_));
		tabela_->pincel = [this](QTableView *v) { return new Pincel(pasta_, grade_, v); };
		cv->addWidget(tabela_, 1);
		abas_ = new QTabBar;
		abas_->setObjectName("abas");
		abas_->setExpanding(false);
		abas_->setDocumentMode(true);
		abas_->setDrawBase(false);
		abas_->setMinimumHeight(30);
		abas_->setUsesScrollButtons(false);
		abas_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		auto *ab = new QWidget;
		ab->setObjectName("faixaAbas");
		auto *abl = new QHBoxLayout(ab);
		abl->setContentsMargins(8, 0, 8, 0);
		abl->setSpacing(4);
		auto *mais = new QToolButton;
		mais->setText("+");
		mais->setToolTip("Nova folha");
		mais->setAutoRaise(true);
		abl->addWidget(mais);
		abl->addWidget(abas_);
		abl->addStretch();
		cv->addWidget(ab);
		h->addWidget(coluna, 1);
		inspetor_ = new QStackedWidget;
		inspetor_->setObjectName("inspetor");
		inspetor_->setFixedWidth(290);
		h->addWidget(inspetor_);
		pilha_->addWidget(raiz);

		grade_.aoDigitar = [this](int r, int c, const QString &t) { digitar(r, c, t); };
		connect(tabela_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &i, const QModelIndex &) {
			grade_.garantir(i.row() + 1, i.column() + 1);
			atualizarBarra();
		});
		connect(tabela_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
			contar();
			atualizarInspetor();
		});
		connect(formula_, &QLineEdit::returnPressed, this, [this] {
			const QModelIndex i = tabela_->currentIndex();
			if (!i.isValid())
				return;
			digitar(i.row(), i.column(), formula_->text());
			tabela_->setCurrentIndex(grade_.index(i.row() + 1, i.column()));
			tabela_->setFocus();
		});
		connect(nomeCel_, &QLineEdit::returnPressed, this, [this] {
			PExpr e = analisar(nomeCel_->text().trimmed().toUpper(), Dialeto::Canonico);
			if (e && (e->k == Expr::Ref || e->k == Expr::Area)) {
				grade_.garantir(e->r1, e->c1);
				tabela_->setCurrentIndex(grade_.index(e->r1, e->c1));
				if (e->k == Expr::Area) {
					grade_.garantir(e->r2, e->c2);
					tabela_->selectionModel()->select(QItemSelection(grade_.index(e->r1, e->c1), grade_.index(e->r2, e->c2)), QItemSelectionModel::ClearAndSelect);
				}
				tabela_->setFocus();
			}
		});
		connect(tabela_->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int c, int, int novo) {
			if (ajustando_) return;
			Folha &fo = pasta_.folha(grade_.folha());
			if (novo == fo.larguraPadrao) fo.largura.erase(c); else fo.largura[c] = novo;
			marcarMudanca();
			posicionarQuadros();
			tabela_->sincronizar();
		});
		connect(tabela_->verticalHeader(), &QHeaderView::sectionResized, this, [this](int r, int, int novo) {
			if (ajustando_) return;
			Folha &fo = pasta_.folha(grade_.folha());
			if (novo == ALTURA_PADRAO) fo.altura.erase(r); else fo.altura[r] = novo;
			marcarMudanca();
			posicionarQuadros();
			tabela_->sincronizar();
		});
		connect(tabela_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { posicionarQuadros(); });
		connect(tabela_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
			posicionarQuadros();
			// Rolou até perto do fim: mais linhas.
			if (v > tabela_->verticalScrollBar()->maximum() - 200)
				grade_.garantir(grade_.rowCount() + 40, 0);
		});
		connect(abas_, &QTabBar::currentChanged, this, [this](int i) {
			if (i >= 0 && i < pasta_.quantas() && !ajustando_)
				mostrarFolha(i);
		});
		connect(abas_, &QTabBar::tabBarDoubleClicked, this, [this](int i) { renomearFolha(i); });
		abas_->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(abas_, &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
			const int i = abas_->tabAt(pos);
			if (i < 0) return;
			QMenu m(this);
			m.addAction("Renomear…", this, [this, i] { renomearFolha(i); });
			m.addAction("Nova folha", this, [this] { novaFolha(); });
			m.addSeparator();
			auto *ap = m.addAction("Apagar a folha", this, [this, i] {
				if (QMessageBox::question(this, "Números", "Apagar a folha \"" + pasta_.folha(i).nome + "\"?") != QMessageBox::Yes)
					return;
				guardar();
				pasta_.apagarFolha(i);
				pasta_.recalcular();
				atualizarAbas(std::max(0, i - 1));
			});
			ap->setEnabled(pasta_.quantas() > 1);
			m.exec(abas_->mapToGlobal(pos));
		});
		connect(mais, &QToolButton::clicked, this, [this] { novaFolha(); });
		tabela_->teclas = [this](QKeyEvent *e) { return tecla(e); };
		tabela_->menu = [this](const QPoint &p) { menuDaTabela(p); };
	}

	QAction *acao(const QString &texto, const QKeySequence &tecla, std::function<void()> f)
	{
		auto *a = new QAction(texto, this);
		a->setShortcut(tecla);
		a->setShortcutContext(Qt::WindowShortcut);
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
		acao("Novo", QKeySequence::New, [this] { if (podeFechar()) mostrarGaleria(); });
		acao("Abrir", QKeySequence::Open, [this] { escolherEAbrir(); });
		acao("Salvar", QKeySequence::Save, [this] { salvar(); });
		acao("Salvar como", QKeySequence::SaveAs, [this] { salvarComo(); });
		acao("Exportar PDF", QKeySequence(Qt::CTRL | Qt::Key_P), [this] { exportar("pdf"); });
		acao("Desfazer", QKeySequence::Undo, [this] { desfazer(); });
		acao("Refazer", QKeySequence::Redo, [this] { refazer(); });
		acao("Copiar", QKeySequence::Copy, [this] { copiar(false); });
		acao("Recortar", QKeySequence::Cut, [this] { copiar(true); });
		acao("Colar", QKeySequence::Paste, [this] { colar(); });
		acao("Preencher para baixo", QKeySequence(Qt::CTRL | Qt::Key_D), [this] { preencher(true); });
		acao("Preencher para a direita", QKeySequence(Qt::CTRL | Qt::Key_R), [this] { preencher(false); });
		acao("Negrito", QKeySequence::Bold, [this] { alternar(0); });
		acao("Itálico", QKeySequence::Italic, [this] { alternar(1); });
		acao("Sublinhado", QKeySequence::Underline, [this] { alternar(2); });
		acao("Soma automática", QKeySequence(Qt::ALT | Qt::Key_Equal), [this] { somaAutomatica(); });

		auto *doc = new QMenu(this);
		doc->addAction(QIcon::fromTheme("document-new"), "Novo…", this, [this] { if (podeFechar()) mostrarGaleria(); });
		doc->addAction(QIcon::fromTheme("document-open"), "Abrir…", this, [this] { escolherEAbrir(); });
		doc->addSeparator();
		doc->addAction(QIcon::fromTheme("document-save"), "Salvar", this, [this] { salvar(); });
		doc->addAction(QIcon::fromTheme("document-save-as"), "Salvar como…", this, [this] { salvarComo(); });
		botao("Planilha", "document-open-folder", nullptr, doc);
		barra_->addSeparator();
		auto *ins = new QMenu(this);
		ins->addAction("Linha acima", this, [this] { inserirLinhas(false, 1); });
		ins->addAction("Linha abaixo", this, [this] { inserirLinhas(false, 1, true); });
		ins->addAction("Coluna à esquerda", this, [this] { inserirLinhas(true, 1); });
		ins->addAction("Coluna à direita", this, [this] { inserirLinhas(true, 1, true); });
		ins->addSeparator();
		ins->addAction("Nova folha", this, [this] { novaFolha(); });
		ins->addAction("Data de hoje", this, [this] {
			const QModelIndex i = tabela_->currentIndex();
			if (i.isValid()) digitar(i.row(), i.column(), QDate::currentDate().toString("dd/MM/yyyy"));
		});
		botao("Inserir", "list-add", nullptr, ins);
		auto *fun = new QMenu(this);
		auto grupoF = [&](const QString &nome, const QStringList &fs) {
			QMenu *m = fun->addMenu(nome);
			for (const QString &f : fs)
				m->addAction(f, this, [this, f] { inserirFuncao(f); });
		};
		grupoF("Contas", {"SOMA", "MÉDIA", "MÍNIMO", "MÁXIMO", "CONT.NÚM", "CONT.VALORES", "ARRED", "PRODUTO", "RAIZ", "ABS"});
		grupoF("Condições", {"SE", "SEERRO", "E", "OU", "SOMASE", "CONT.SE", "MÉDIASE", "SOMASES", "CONT.SES"});
		grupoF("Procura", {"PROCV", "PROCH", "ÍNDICE", "CORRESP"});
		grupoF("Finanças", {"VPL", "TIR", "PGTO", "VP", "VF", "TAXA", "NPER", "IPGTO", "PPGTO"});
		grupoF("Estatística", {"MED", "MODO", "DESVPAD", "VAR", "CORREL", "INCLINAÇÃO", "INTERCEPÇÃO", "RQUAD", "PREVISÃO", "MAIOR", "MENOR", "ORDEM"});
		grupoF("Datas", {"HOJE", "AGORA", "DATA", "ANO", "MÊS", "DIA", "DIA.DA.SEMANA", "DIAS", "DATAM"});
		grupoF("Texto", {"CONCAT", "ESQUERDA", "DIREITA", "EXT.TEXTO", "NÚM.CARACT", "MAIÚSCULA", "MINÚSCULA", "ARRUMAR", "TEXTO", "VALOR"});
		botao("Função", "", nullptr, fun);
		botao("Soma", "", [this] { somaAutomatica(); })->setToolTip("Soma automática (Alt+=)");
		auto *gr = new QMenu(this);
		const QList<QPair<QString, Grafico::Tipo>> tipos = {{"Colunas", Grafico::Colunas}, {"Barras", Grafico::Barras}, {"Linhas", Grafico::Linhas},
		                                                    {"Área", Grafico::Area}, {"Pizza", Grafico::Pizza}, {"Dispersão", Grafico::Dispersao}};
		for (const auto &[n, t] : tipos) {
			const Grafico::Tipo tt = t;
			gr->addAction(n, this, [this, tt] { inserirGrafico(tt); });
		}
		botao("Gráfico", "office-chart-bar", nullptr, gr);
		auto *ord = new QMenu(this);
		ord->addAction("Do menor para o maior (A → Z)", this, [this] { ordenar(true); });
		ord->addAction("Do maior para o menor (Z → A)", this, [this] { ordenar(false); });
		botao("Ordenar", "view-sort-ascending", nullptr, ord);
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
		exp->addAction("PDF…", this, [this] { exportar("pdf"); });
		exp->addAction("Excel (.xlsx)…", this, [this] { exportar("xlsx"); });
		exp->addAction("OpenDocument (.ods)…", this, [this] { exportar("ods"); });
		exp->addAction("CSV (esta folha)…", this, [this] { exportar("csv"); });
		botao("Exportar", "document-export", nullptr, exp);
		barra_->addSeparator();
		bFormatar_ = botao("Formatar", "format-text-bold", [this] {
			inspetor_->setVisible(!inspetor_->isVisible());
			bFormatar_->setChecked(inspetor_->isVisible());
		});
		bFormatar_->setCheckable(true);
		bFormatar_->setChecked(true);
	}

	// --- inspetor ---
	static void secao(QVBoxLayout *v, const QString &t)
	{
		auto *l = new QLabel(rotulo(t));
		l->setObjectName("rotulo");
		v->addSpacing(10);
		v->addWidget(l);
	}

	QToolButton *alterna(const QString &texto, const QString &dica, QHBoxLayout *h, std::function<void()> f)
	{
		auto *b = new QToolButton;
		b->setText(texto);
		b->setToolTip(dica);
		b->setCheckable(true);
		b->setObjectName("alterna");
		b->setMinimumSize(30, 30);
		b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		connect(b, &QToolButton::clicked, this, [f] { f(); });
		h->addWidget(b);
		return b;
	}

	void montarInspetor()
	{
		// Células.
		auto *cw = new QWidget;
		auto *v = new QVBoxLayout(cw);
		v->setContentsMargins(18, 14, 18, 14);
		v->setSpacing(6);
		auto *cab = new QLabel("Formatar");
		cab->setObjectName("parte");
		v->addWidget(cab);
		auto *fio = new QFrame;
		fio->setObjectName("fio");
		v->addWidget(fio);
		secao(v, "Formato dos dados");
		tipo_ = new QComboBox;
		tipo_->addItems({"Automático", "Número", "Moeda (R$)", "Porcentagem", "Data", "Texto"});
		connect(tipo_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			formatar([i](Formato &f) { f.tipo = Formato::Tipo(i); });
		});
		v->addWidget(tipo_);
		auto *ch = new QHBoxLayout;
		casas_ = new QSpinBox;
		casas_->setRange(0, 10);
		casas_->setPrefix("Casas: ");
		connect(casas_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int n) {
			if (!atualizando_) formatar([n](Formato &f) { f.casas = n; if (f.tipo == Formato::Geral) f.tipo = Formato::Numero; });
		});
		milhar_ = new QCheckBox("Milhar (1.000)");
		connect(milhar_, &QCheckBox::toggled, this, [this](bool b) {
			if (!atualizando_) formatar([b](Formato &f) { f.milhar = b; });
		});
		ch->addWidget(casas_, 1);
		ch->addWidget(milhar_);
		v->addLayout(ch);
		secao(v, "Texto");
		auto *bh = new QHBoxLayout;
		bh->setSpacing(4);
		negrito_ = alterna("N", "Negrito (Ctrl+B)", bh, [this] { alternar(0); });
		italico_ = alterna("I", "Itálico (Ctrl+I)", bh, [this] { alternar(1); });
		sublinhado_ = alterna("S", "Sublinhado (Ctrl+U)", bh, [this] { alternar(2); });
		{
			QFont f = negrito_->font(); f.setBold(true); negrito_->setFont(f);
			f = italico_->font(); f.setItalic(true); italico_->setFont(f);
			f = sublinhado_->font(); f.setUnderline(true); sublinhado_->setFont(f);
		}
		v->addLayout(bh);
		auto *th = new QHBoxLayout;
		auto *cor = new QPushButton("Cor do texto…");
		connect(cor, &QPushButton::clicked, this, [this] {
			const QColor c = QColorDialog::getColor(cores_.tinta, this, "Cor do texto");
			if (c.isValid()) formatar([c](Formato &f) { f.cor = c; });
		});
		auto *semCor = new QToolButton;
		semCor->setText("×");
		semCor->setToolTip("A cor padrão");
		semCor->setObjectName("alterna");
		connect(semCor, &QToolButton::clicked, this, [this] { formatar([](Formato &f) { f.cor = QColor(); }); });
		th->addWidget(cor, 1);
		th->addWidget(semCor);
		v->addLayout(th);
		secao(v, "Preenchimento");
		auto *pg = new QGridLayout;
		pg->setSpacing(4);
		const QList<QColor> fundos = {QColor(), QColor(0xe9, 0xe3, 0xd6), QColor(0xf3, 0xe6, 0xc4), QColor(0xdc, 0xe8, 0xd2),
		                              QColor(0xd6, 0xe2, 0xee), QColor(0xf2, 0xd7, 0xd2), QColor(0xe4, 0xdc, 0xee), QColor(0x2f, 0x4b, 0x6e)};
		for (int i = 0; i < fundos.size(); i++) {
			auto *b = new QToolButton;
			b->setObjectName("amostra");
			b->setFixedSize(28, 24);
			const QColor c = fundos[i];
			b->setToolTip(c.isValid() ? c.name() : "Sem preenchimento");
			b->setStyleSheet(c.isValid() ? "background:" + c.name() + ";" : "");
			if (!c.isValid()) b->setText("∅");
			connect(b, &QToolButton::clicked, this, [this, c] {
				formatar([c](Formato &f) {
					f.fundo = c;
					if (c == QColor(0x2f, 0x4b, 0x6e)) f.cor = Qt::white;
				});
			});
			pg->addWidget(b, 0, i);
		}
		v->addLayout(pg);
		secao(v, "Alinhamento");
		auto *ah = new QHBoxLayout;
		ah->setSpacing(4);
		alin_[0] = alterna("Auto", "Números à direita, texto à esquerda", ah, [this] { formatar([](Formato &f) { f.alin = 0; }); });
		alin_[1] = alterna("Esq.", "À esquerda", ah, [this] { formatar([](Formato &f) { f.alin = 1; }); });
		alin_[2] = alterna("Centro", "Centralizado", ah, [this] { formatar([](Formato &f) { f.alin = 2; }); });
		alin_[3] = alterna("Dir.", "À direita", ah, [this] { formatar([](Formato &f) { f.alin = 3; }); });
		v->addLayout(ah);
		secao(v, "Bordas");
		borda_ = new QCheckBox("Bordas em volta de cada célula");
		connect(borda_, &QCheckBox::toggled, this, [this](bool b) {
			if (!atualizando_) formatar([b](Formato &f) { f.borda = b; });
		});
		v->addWidget(borda_);
		v->addStretch();
		auto *rc = new QScrollArea;
		rc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		rc->setWidget(cw);
		rc->setWidgetResizable(true);
		rc->setFrameStyle(QFrame::NoFrame);
		inspetor_->addWidget(rc);

		// Gráfico.
		auto *gw = new QWidget;
		auto *gv = new QVBoxLayout(gw);
		gv->setContentsMargins(18, 14, 18, 14);
		gv->setSpacing(6);
		auto *gc = new QLabel("Gráfico");
		gc->setObjectName("parte");
		gv->addWidget(gc);
		auto *fio2 = new QFrame;
		fio2->setObjectName("fio");
		gv->addWidget(fio2);
		secao(gv, "Tipo");
		gTipo_ = new QComboBox;
		gTipo_->addItems({"Colunas", "Barras", "Linhas", "Pizza", "Dispersão", "Área"});
		connect(gTipo_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
			if (Grafico *g = graficoEscolhido()) { guardar(); g->tipo = Grafico::Tipo(i); redesenharQuadros(); marcarMudanca(); }
		});
		gv->addWidget(gTipo_);
		secao(gv, "Título");
		gTitulo_ = new QLineEdit;
		connect(gTitulo_, &QLineEdit::editingFinished, this, [this] {
			if (Grafico *g = graficoEscolhido()) if (g->titulo != gTitulo_->text()) { guardar(); g->titulo = gTitulo_->text(); redesenharQuadros(); marcarMudanca(); }
		});
		gv->addWidget(gTitulo_);
		secao(gv, "Dados");
		gDados_ = new QLineEdit;
		gDados_->setToolTip("O intervalo: a 1ª linha com os nomes das séries, a 1ª coluna com as categorias.");
		connect(gDados_, &QLineEdit::editingFinished, this, [this] {
			Grafico *g = graficoEscolhido();
			PExpr e = analisar(gDados_->text().trimmed().toUpper(), Dialeto::Canonico);
			if (g && e && e->k == Expr::Area) {
				guardar();
				g->r1 = e->r1; g->c1 = e->c1; g->r2 = e->r2; g->c2 = e->c2;
				g->series.clear();
				redesenharQuadros();
				marcarMudanca();
			}
		});
		gv->addWidget(gDados_);
		auto *ap = new QPushButton("Apagar o gráfico");
		connect(ap, &QPushButton::clicked, this, [this] { apagarGrafico(); });
		gv->addSpacing(12);
		gv->addWidget(ap);
		gv->addStretch();
		inspetor_->addWidget(gw);
	}

	void atualizarInspetor()
	{
		if (pilha_->currentIndex() != 1)
			return;
		if (graficoEscolhido()) {
			inspetor_->setCurrentIndex(1);
			const Grafico &g = *graficoEscolhido();
			gTipo_->setCurrentIndex(g.tipo);
			gTitulo_->setText(g.titulo);
			gDados_->setText(nomeCelula(g.r1, g.c1) + ":" + nomeCelula(g.r2, g.c2));
			return;
		}
		inspetor_->setCurrentIndex(0);
		atualizando_ = true;
		const QModelIndex i = tabela_->currentIndex();
		const Celula *k = i.isValid() ? pasta_.folha(grade_.folha()).em(i.row(), i.column()) : nullptr;
		const Formato f = k ? k->fmt : Formato();
		tipo_->setCurrentIndex(f.tipo);
		casas_->setValue(f.casas);
		milhar_->setChecked(f.milhar);
		negrito_->setChecked(f.negrito);
		italico_->setChecked(f.italico);
		sublinhado_->setChecked(f.sublinhado);
		for (int a = 0; a < 4; a++)
			alin_[a]->setChecked(f.alin == a);
		borda_->setChecked(f.borda);
		atualizando_ = false;
	}

	// --- desfazer ---
	void guardar()
	{
		antes_.push_back(pasta_.folhas);
		if (antes_.size() > 80)
			antes_.erase(antes_.begin());
		depois_.clear();
	}

	void desfazer()
	{
		if (antes_.empty())
			return;
		depois_.push_back(pasta_.folhas);
		pasta_.folhas = antes_.back();
		antes_.pop_back();
		restaurado();
	}

	void refazer()
	{
		if (depois_.empty())
			return;
		antes_.push_back(pasta_.folhas);
		pasta_.folhas = depois_.back();
		depois_.pop_back();
		restaurado();
	}

	void restaurado()
	{
		const QModelIndex i = tabela_->currentIndex();
		const int f = std::min(grade_.folha(), pasta_.quantas() - 1);
		pasta_.recalcular();
		atualizarAbas(f);
		if (i.isValid())
			tabela_->setCurrentIndex(grade_.index(i.row(), i.column()));
		marcarMudanca();
	}

	// --- editar ---
	void digitar(int r, int c, const QString &texto)
	{
		const Celula *at = pasta_.folha(grade_.folha()).em(r, c);
		if (!at && texto.isEmpty())
			return;
		if (at && paraEditar(*at) == texto)
			return;
		guardar();
		const QString erro = pasta_.digitar(grade_.folha(), r, c, texto);
		pasta_.recalcular();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
		atualizarBarra();
		if (!erro.isEmpty())
			statusBar()->showMessage("Fórmula com problema: " + erro, 6000);
	}

	// A seleção (o retângulo que contém tudo o que está selecionado).
	bool selecao(int &r1, int &c1, int &r2, int &c2) const
	{
		const QModelIndexList l = tabela_->selectionModel()->selectedIndexes();
		if (l.isEmpty()) {
			const QModelIndex i = tabela_->currentIndex();
			if (!i.isValid()) return false;
			r1 = r2 = i.row();
			c1 = c2 = i.column();
			return true;
		}
		r1 = c1 = INT_MAX;
		r2 = c2 = -1;
		for (const QModelIndex &i : l) {
			r1 = std::min(r1, i.row()); c1 = std::min(c1, i.column());
			r2 = std::max(r2, i.row()); c2 = std::max(c2, i.column());
		}
		return true;
	}

	void formatar(const std::function<void(Formato &)> &mudar)
	{
		if (atualizando_)
			return;
		if (Grafico *g = graficoEscolhido()) {
			(void)g;
			return;
		}
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		guardar();
		Folha &fo = pasta_.folha(grade_.folha());
		// Colunas e linhas inteiras: só até o fim dos dados.
		r2 = std::min(r2, std::max(fo.ultLinha, r1));
		c2 = std::min(c2, std::max(fo.ultColuna, c1));
		for (int r = r1; r <= r2; r++)
			for (int c = c1; c <= c2; c++) {
				const Celula *at = fo.em(r, c);
				Celula k = at ? *at : Celula();
				mudar(k.fmt);
				pasta_.por(grade_.folha(), r, c, k);
			}
		fo.recontar();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
		atualizarInspetor();
	}

	void alternar(int qual)
	{
		const QModelIndex i = tabela_->currentIndex();
		const Celula *k = i.isValid() ? pasta_.folha(grade_.folha()).em(i.row(), i.column()) : nullptr;
		const Formato f = k ? k->fmt : Formato();
		const bool novo = !(qual == 0 ? f.negrito : qual == 1 ? f.italico : f.sublinhado);
		formatar([qual, novo](Formato &x) {
			if (qual == 0) x.negrito = novo;
			else if (qual == 1) x.italico = novo;
			else x.sublinhado = novo;
		});
	}

	void limparSelecao()
	{
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		guardar();
		Folha &fo = pasta_.folha(grade_.folha());
		r2 = std::min(r2, fo.ultLinha);
		c2 = std::min(c2, fo.ultColuna);
		for (int r = r1; r <= r2; r++)
			for (int c = c1; c <= c2; c++) {
				const Celula *at = fo.em(r, c);
				if (!at) continue;
				Celula k;
				k.fmt = at->fmt; // apagar o conteúdo deixa o formato (como no Excel)
				pasta_.por(grade_.folha(), r, c, k);
			}
		fo.recontar();
		pasta_.recalcular();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
		atualizarBarra();
	}

	DadosGrafico dadosParaColar(const Grafico &g) const { return ::dadosParaColar(pasta_, grade_.folha(), g); }

	// Copiar com um gráfico escolhido: o gráfico (os dados, para o Palco e o
	// Páginas colarem como gráfico, e a imagem, para os outros programas).
	void copiarGrafico()
	{
		const Grafico &g = pasta_.folha(grade_.folha()).graficos[size_t(graficoEscolhido_)];
		QImage img(int(g.w * 2), int(g.h * 2), QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::white);
		{
			QPainter p(&img);
			p.scale(2, 2);
			Tinta t;
			desenharGrafico(p, QRectF(0, 0, g.w, g.h), pasta_, grade_.folha(), g, t);
		}
		auto *m = new QMimeData;
		m->setData(MIME_GRAFICO, gravarDados(dadosParaColar(g)).toUtf8());
		// De onde veio (a planilha gravada): colado no Páginas ou no Palco,
		// o gráfico se atualiza quando ela é gravada de novo.
		const QString ext = extensao(arquivo_);
		if (!arquivo_.isEmpty() && !csv_ && (ext == "xlsx" || ext == "ods")) {
			Vinculo v;
			v.arq = arquivo_;
			v.n = graficoEscolhido_ + 1;
			for (int f = 0; f < grade_.folha(); f++)
				v.n += int(pasta_.folha(f).graficos.size());
			m->setData(MIME_VINCULO, v.texto().toUtf8());
		}
		m->setImageData(img);
		const bool vinculado = m->hasFormat(MIME_VINCULO);
		QApplication::clipboard()->setMimeData(m);
		textoCopiado_.clear();
		statusBar()->showMessage(vinculado
		    ? "Gráfico copiado: colado no Palco ou no Páginas, ele continua com os dados e se atualiza quando você grava a planilha."
		    : "Gráfico copiado: cole no Palco ou no Páginas (Ctrl+V) e ele continua com os dados.", 6000);
	}

	// Um replicante na área de trabalho (lume-mesa): o gráfico vivo, que se
	// redesenha quando a planilha é gravada. Ele lê o arquivo: grava antes.
	void porNaMesa(int indice)
	{
		if (!salvar() || arquivo_.isEmpty())
			return;
		int n = indice + 1;
		for (int f = 0; f < grade_.folha(); f++)
			n += int(pasta_.folha(f).graficos.size());
		porNaMesaEAbrir("lume-numeros", {"grafico", arquivo_, QString::number(n)});
		statusBar()->showMessage("O gráfico está na mesa: ele se redesenha quando você grava a planilha.", 5000);
	}

	void copiar(bool recortar)
	{
		if (graficoEscolhido_ >= 0 && graficoEscolhido_ < int(pasta_.folha(grade_.folha()).graficos.size())) {
			copiarGrafico();
			if (recortar)
				apagarGrafico();
			return;
		}
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		const Folha &fo = pasta_.folha(grade_.folha());
		r2 = std::min(r2, std::max(fo.ultLinha, r1));
		c2 = std::min(c2, std::max(fo.ultColuna, c1));
		recorte_ = pasta_.copiar(grade_.folha(), r1, c1, r2, c2);
		// O texto, para os outros programas (separado por tabulação).
		QString t;
		for (int r = r1; r <= r2; r++) {
			QStringList l;
			for (int c = c1; c <= c2; c++) {
				const Celula *k = fo.em(r, c);
				l << (k ? mostrar(k->v, k->fmt) : QString());
			}
			t += l.join('\t') + "\n";
		}
		textoCopiado_ = t;
		QApplication::clipboard()->setText(t);
		if (recortar)
			limparSelecao();
	}

	void colar()
	{
		const QModelIndex i = tabela_->currentIndex();
		if (!i.isValid())
			return;
		const QString t = QApplication::clipboard()->text();
		guardar();
		if (recorte_.linhas > 0 && (t == textoCopiado_ || t.isEmpty())) {
			pasta_.colar(grade_.folha(), i.row(), i.column(), recorte_);
		} else {
			// De outro programa: linhas e tabulações (ou ";").
			QStringList linhas = t.split('\n');
			while (!linhas.isEmpty() && linhas.last().isEmpty())
				linhas.removeLast();
			for (int r = 0; r < linhas.size(); r++) {
				QString l = linhas[r];
				if (l.endsWith('\r')) l.chop(1);
				const QStringList cs = l.contains('\t') ? l.split('\t') : QStringList{l};
				for (int c = 0; c < cs.size(); c++)
					pasta_.digitar(grade_.folha(), i.row() + r, i.column() + c, cs[c]);
			}
		}
		pasta_.recalcular();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
		atualizarBarra();
	}

	void preencher(bool paraBaixo)
	{
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		if ((paraBaixo && r2 == r1) || (!paraBaixo && c2 == c1))
			return;
		guardar();
		pasta_.preencher(grade_.folha(), r1, c1, r2, c2, paraBaixo);
		pasta_.recalcular();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
		atualizarBarra();
	}

	void inserirLinhas(bool colunas, int n, bool depois = false)
	{
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		guardar();
		const int em = colunas ? (depois ? c2 + 1 : c1) : (depois ? r2 + 1 : r1);
		pasta_.inserir(grade_.folha(), colunas, em, n);
		// As novas herdam o formato da de cima (ou da da esquerda), como no Excel.
		if (em > 0) {
			Folha &fo = pasta_.folha(grade_.folha());
			const int fim = colunas ? fo.ultLinha : fo.ultColuna;
			for (int j = 0; j <= fim; j++) {
				const Celula *mod = colunas ? fo.em(j, em - 1) : fo.em(em - 1, j);
				if (!mod || mod->fmt.padrao())
					continue;
				for (int k = 0; k < n; k++) {
					Celula nova;
					nova.fmt = mod->fmt;
					if (colunas) pasta_.por(grade_.folha(), j, em + k, nova);
					else pasta_.por(grade_.folha(), em + k, j, nova);
				}
			}
			fo.recontar();
		}
		pasta_.recalcular();
		aplicarMedidas();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
	}

	void apagarLinhas(bool colunas)
	{
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		guardar();
		if (colunas)
			pasta_.inserir(grade_.folha(), true, c1, -(c2 - c1 + 1));
		else
			pasta_.inserir(grade_.folha(), false, r1, -(r2 - r1 + 1));
		pasta_.recalcular();
		aplicarMedidas();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
	}

	// O bloco de dados em volta de uma célula (para ordenar e para o gráfico).
	void bloco(int r, int c, int &r1, int &c1, int &r2, int &c2)
	{
		const Folha &fo = pasta_.folha(grade_.folha());
		auto tem = [&](int rr, int cc) { return rr >= 0 && cc >= 0 && fo.em(rr, cc) && !fo.em(rr, cc)->vazia(); };
		r1 = r2 = r;
		c1 = c2 = c;
		bool cresceu = true;
		while (cresceu) {
			cresceu = false;
			for (int k = c1; k <= c2; k++) {
				if (tem(r1 - 1, k)) { r1--; cresceu = true; break; }
			}
			for (int k = c1; k <= c2; k++) {
				if (tem(r2 + 1, k)) { r2++; cresceu = true; break; }
			}
			for (int k = r1; k <= r2; k++) {
				if (tem(k, c1 - 1)) { c1--; cresceu = true; break; }
			}
			for (int k = r1; k <= r2; k++) {
				if (tem(k, c2 + 1)) { c2++; cresceu = true; break; }
			}
		}
	}

	void ordenar(bool crescente)
	{
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		const int por = tabela_->currentIndex().column();
		if (r1 == r2 && c1 == c2) {
			bloco(r1, c1, r1, c1, r2, c2);
			// A primeira linha é de títulos se for texto em cima de números.
			const Celula *a = pasta_.folha(grade_.folha()).em(r1, por);
			const Celula *b = pasta_.folha(grade_.folha()).em(r1 + 1, por);
			if (a && b && a->v.t == Valor::Texto && b->v.t == Valor::Numero)
				r1++;
		}
		guardar();
		pasta_.ordenar(grade_.folha(), r1, c1, r2, c2, por, crescente);
		pasta_.recalcular();
		grade_.tudoMudou();
		redesenharQuadros();
		marcarMudanca();
	}

	void somaAutomatica()
	{
		const QModelIndex i = tabela_->currentIndex();
		if (!i.isValid())
			return;
		const Folha &fo = pasta_.folha(grade_.folha());
		auto num = [&](int r, int c) { const Celula *k = fo.em(r, c); return k && k->v.t == Valor::Numero; };
		int r = i.row(), c = i.column();
		QString faixa;
		if (r > 0 && num(r - 1, c)) {
			int a = r - 1;
			while (a > 0 && num(a - 1, c)) a--;
			faixa = nomeCelula(a, c) + ":" + nomeCelula(r - 1, c);
		} else if (c > 0 && num(r, c - 1)) {
			int a = c - 1;
			while (a > 0 && num(r, a - 1)) a--;
			faixa = nomeCelula(r, a) + ":" + nomeCelula(r, c - 1);
		}
		if (faixa.isEmpty()) {
			inserirFuncao("SOMA");
			return;
		}
		digitar(r, c, "=SOMA(" + faixa + ")");
	}

	void inserirFuncao(const QString &f)
	{
		const QModelIndex i = tabela_->currentIndex();
		if (!i.isValid())
			return;
		formula_->setText("=" + f + "()");
		formula_->setFocus();
		formula_->setCursorPosition(int(formula_->text().size()) - 1);
	}

	// --- folhas ---
	void novaFolha()
	{
		guardar();
		const int f = pasta_.novaFolha();
		atualizarAbas(f);
		marcarMudanca();
	}

	void renomearFolha(int i)
	{
		if (i < 0)
			return;
		bool ok;
		const QString n = QInputDialog::getText(this, "Renomear a folha", "Nome:", QLineEdit::Normal, pasta_.folha(i).nome, &ok).trimmed();
		if (!ok || n.isEmpty() || n == pasta_.folha(i).nome)
			return;
		if (pasta_.folhaPorNome(n) >= 0) {
			QMessageBox::warning(this, "Números", "Já existe uma folha com esse nome.");
			return;
		}
		guardar();
		pasta_.renomear(i, n);
		pasta_.recalcular();
		atualizarAbas(i);
		marcarMudanca();
	}

	void atualizarAbas(int ativa)
	{
		ajustando_ = true;
		while (abas_->count())
			abas_->removeTab(0);
		for (const Folha &f : pasta_.folhas)
			abas_->addTab(f.nome);
		abas_->setMinimumWidth(abas_->sizeHint().width());
		ajustando_ = false;
		abas_->setCurrentIndex(ativa);
		mostrarFolha(ativa);
	}

	void mostrarFolha(int f)
	{
		graficoEscolhido_ = -1;
		grade_.folha(f);
		aplicarMedidas();
		montarQuadros();
		tabela_->congelar(pasta_.folha(f).congelarLinhas, pasta_.folha(f).congelarColunas);
		tabela_->setCurrentIndex(grade_.index(0, 0));
		atualizarBarra();
		atualizarInspetor();
		contar();
	}

	void aplicarMedidas()
	{
		ajustando_ = true;
		const Folha &fo = pasta_.folha(grade_.folha());
		tabela_->horizontalHeader()->setDefaultSectionSize(fo.larguraPadrao);
		for (int c = 0; c < grade_.columnCount(); c++)
			tabela_->setColumnWidth(c, fo.larguraDe(c));
		for (int r = 0; r < grade_.rowCount(); r++)
			if (tabela_->rowHeight(r) != fo.alturaDe(r))
				tabela_->setRowHeight(r, fo.alturaDe(r));
		ajustando_ = false;
		tabela_->sincronizar();
	}

	// --- gráficos ---
	Grafico *graficoEscolhido()
	{
		auto &gs = pasta_.folha(grade_.folha()).graficos;
		return graficoEscolhido_ >= 0 && graficoEscolhido_ < int(gs.size()) ? &gs[size_t(graficoEscolhido_)] : nullptr;
	}

	void montarQuadros()
	{
		for (Quadro *q : quadros_)
			q->deleteLater();
		quadros_.clear();
		const auto &gs = pasta_.folha(grade_.folha()).graficos;
		for (size_t i = 0; i < gs.size(); i++) {
			auto *q = new Quadro(pasta_, grade_.folha(), int(i), tabela_->viewport());
			q->mexeu = [this] { posicionarQuadros(); marcarMudanca(); };
			q->escolhido = [this, q] {
				if (graficoEscolhido_ != q->indice())
					guardar();
				graficoEscolhido_ = q->indice();
				for (Quadro *o : quadros_) o->marcar(o == q);
				atualizarInspetor();
			};
			q->menu = [this, q](const QPoint &p) {
				QMenu m(this);
				m.addAction("Copiar o gráfico", this, [this, q] { graficoEscolhido_ = q->indice(); copiarGrafico(); });
				m.addAction("Pôr na mesa", this, [this, q] { porNaMesa(q->indice()); });
				m.addAction("Apagar o gráfico", this, [this, q] { graficoEscolhido_ = q->indice(); apagarGrafico(); });
				m.exec(p);
			};
			q->show();
			quadros_.push_back(q);
		}
		posicionarQuadros();
	}

	void posicionarQuadros()
	{
		const auto &gs = pasta_.folha(grade_.folha()).graficos;
		const int dx = tabela_->horizontalHeader()->offset(), dy = tabela_->verticalHeader()->offset();
		for (Quadro *q : quadros_) {
			if (q->indice() >= int(gs.size())) continue;
			const Grafico &g = gs[size_t(q->indice())];
			// A tela usa as larguras do QTableView (iguais às da folha).
			q->setGeometry(int(g.x) - dx, int(g.y) - dy, int(g.w), int(g.h));
			q->update();
		}
	}

	void redesenharQuadros()
	{
		if (quadros_.size() != pasta_.folha(grade_.folha()).graficos.size()) {
			montarQuadros();
			return;
		}
		for (Quadro *q : quadros_)
			q->update();
		posicionarQuadros();
	}

	void inserirGrafico(Grafico::Tipo tipo)
	{
		int r1, c1, r2, c2;
		if (!selecao(r1, c1, r2, c2))
			return;
		if (r1 == r2 && c1 == c2)
			bloco(r1, c1, r1, c1, r2, c2);
		const Folha &fo = pasta_.folha(grade_.folha());
		r2 = std::min(r2, std::max(fo.ultLinha, r1));
		c2 = std::min(c2, std::max(fo.ultColuna, c1));
		if (r2 <= r1 && c2 <= c1) {
			QMessageBox::information(this, "Números", "Selecione os dados do gráfico (com os títulos na primeira linha).");
			return;
		}
		guardar();
		Grafico g;
		g.tipo = tipo;
		g.r1 = r1; g.c1 = c1; g.r2 = r2; g.c2 = c2;
		// Do lado direito dos dados, na altura da primeira linha.
		g.x = pasta_.xDe(grade_.folha(), c2 + 1) + 24;
		g.y = pasta_.yDe(grade_.folha(), r1);
		const Celula *t = fo.em(r1, c1 + 1 <= c2 ? c1 + 1 : c1);
		if (t && t->v.t == Valor::Texto && c2 == c1 + 1)
			g.titulo = t->v.s;
		pasta_.folha(grade_.folha()).graficos.push_back(g);
		graficoEscolhido_ = int(pasta_.folha(grade_.folha()).graficos.size()) - 1;
		montarQuadros();
		for (Quadro *q : quadros_) q->marcar(q->indice() == graficoEscolhido_);
		atualizarInspetor();
		marcarMudanca();
	}

	void apagarGrafico()
	{
		auto &gs = pasta_.folha(grade_.folha()).graficos;
		if (graficoEscolhido_ < 0 || graficoEscolhido_ >= int(gs.size()))
			return;
		guardar();
		gs.erase(gs.begin() + graficoEscolhido_);
		graficoEscolhido_ = -1;
		montarQuadros();
		atualizarInspetor();
		marcarMudanca();
	}

	// --- teclado e menu ---
	bool tecla(QKeyEvent *e)
	{
		if (tabela_->editando())
			return false;
		// Um clique na grade tira a escolha do gráfico.
		if (graficoEscolhido_ >= 0 && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
			apagarGrafico();
			return true;
		}
		if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
			limparSelecao();
			return true;
		}
		if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::AltModifier)) {
			const QModelIndex i = tabela_->currentIndex();
			const int d = e->modifiers() & Qt::ShiftModifier ? -1 : 1;
			tabela_->setCurrentIndex(grade_.index(std::clamp(i.row() + d, 0, grade_.rowCount() - 1), i.column()));
			return true;
		}
		if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
			const QModelIndex i = tabela_->currentIndex();
			const int d = e->key() == Qt::Key_Backtab ? -1 : 1;
			tabela_->setCurrentIndex(grade_.index(i.row(), std::clamp(i.column() + d, 0, grade_.columnCount() - 1)));
			return true;
		}
		return false;
	}

	void menuDaTabela(const QPoint &p)
	{
		graficoEscolhido_ = -1;
		for (Quadro *q : quadros_) q->marcar(false);
		QMenu m(this);
		m.addAction("Recortar", this, [this] { copiar(true); });
		m.addAction("Copiar", this, [this] { copiar(false); });
		m.addAction("Colar", this, [this] { colar(); });
		m.addAction("Apagar o conteúdo", this, [this] { limparSelecao(); });
		m.addSeparator();
		m.addAction("Inserir linha acima", this, [this] { inserirLinhas(false, 1); });
		m.addAction("Inserir linha abaixo", this, [this] { inserirLinhas(false, 1, true); });
		m.addAction("Apagar as linhas", this, [this] { apagarLinhas(false); });
		m.addSeparator();
		m.addAction("Inserir coluna à esquerda", this, [this] { inserirLinhas(true, 1); });
		m.addAction("Inserir coluna à direita", this, [this] { inserirLinhas(true, 1, true); });
		m.addAction("Apagar as colunas", this, [this] { apagarLinhas(true); });
		m.addSeparator();
		m.addAction("Ordenar do menor para o maior", this, [this] { ordenar(true); });
		m.addAction("Ordenar do maior para o menor", this, [this] { ordenar(false); });
		m.addSeparator();
		const QModelIndex i = tabela_->currentIndex();
		Folha &fo = pasta_.folha(grade_.folha());
		if (fo.congelarLinhas || fo.congelarColunas)
			m.addAction("Descongelar", this, [this] { congelar(0, 0); });
		if (i.isValid() && (i.row() || i.column()))
			m.addAction("Congelar acima e à esquerda de " + nomeCelula(i.row(), i.column()), this, [this, i] { congelar(i.row(), i.column()); });
		m.exec(p);
	}

	void congelar(int linhas, int colunas)
	{
		guardar();
		Folha &fo = pasta_.folha(grade_.folha());
		fo.congelarLinhas = linhas;
		fo.congelarColunas = colunas;
		tabela_->scrollToTop();
		tabela_->horizontalScrollBar()->setValue(0);
		tabela_->congelar(linhas, colunas);
		marcarMudanca();
	}

	// --- barra de fórmula e contas ---
	void atualizarBarra()
	{
		const QModelIndex i = tabela_->currentIndex();
		if (!i.isValid())
			return;
		nomeCel_->setText(nomeCelula(i.row(), i.column()));
		const Celula *k = pasta_.folha(grade_.folha()).em(i.row(), i.column());
		formula_->setText(k ? paraEditar(*k) : QString());
		// Um clique numa célula deixa de escolher o gráfico.
		if (graficoEscolhido_ >= 0 && tabela_->hasFocus()) {
			graficoEscolhido_ = -1;
			for (Quadro *q : quadros_) q->marcar(false);
			atualizarInspetor();
		}
	}

	void contar()
	{
		int r1, c1, r2, c2;
		if (pilha_->currentIndex() != 1 || !selecao(r1, c1, r2, c2)) {
			contas_->clear();
			return;
		}
		const Folha &fo = pasta_.folha(grade_.folha());
		double s = 0;
		int n = 0, nn = 0;
		for (const QModelIndex &i : tabela_->selectionModel()->selectedIndexes()) {
			const Celula *k = fo.em(i.row(), i.column());
			if (!k || k->v.t == Valor::Vazio) continue;
			nn++;
			if (k->v.t == Valor::Numero) { s += k->v.n; n++; }
		}
		if (nn < 2) {
			contas_->setText(pasta_.folha(grade_.folha()).nome);
			return;
		}
		const Celula *k = fo.em(r1, c1);
		Formato f = k ? k->fmt : Formato();
		if (f.tipo == Formato::Texto || f.tipo == Formato::Data) f = Formato();
		contas_->setText(QString("Soma: %1   ·   Média: %2   ·   Contagem: %3")
		                     .arg(mostrar(Valor::num(s), f), n ? mostrar(Valor::num(s / n), f) : QString("—"))
		                     .arg(nn));
	}

	// --- arquivos ---
	void depoisDeCarregar()
	{
		antes_.clear();
		depois_.clear();
		mudou_ = false;
		pilha_->setCurrentIndex(1);
		barra_->setVisible(true);
		statusBar()->setVisible(true);
		atualizarAbas(0);
		titulo();
		tabela_->setFocus();
	}

	void marcarMudanca()
	{
		if (!mudou_) {
			mudou_ = true;
			titulo();
		}
	}

	void titulo()
	{
		const QString nome = arquivo_.isEmpty() ? QString("Sem título") : QFileInfo(arquivo_).fileName();
		setWindowTitle(nome + "[*] — Números");
		titulo_->setText(nome);
		setWindowModified(mudou_);
	}

	static QString pastaPadrao()
	{
		const QString d = QDir::homePath() + "/Documentos";
		return QDir(d).exists() ? d : QDir::homePath();
	}

	void escolherEAbrir()
	{
		if (pilha_->currentIndex() == 1 && !podeFechar())
			return;
		const QString arq = QFileDialog::getOpenFileName(this, "Abrir", pastaPadrao(),
		    "Planilhas (*.xlsx *.ods *.csv);;Excel (*.xlsx);;OpenDocument (*.ods);;CSV (*.csv *.txt);;Todos os arquivos (*)");
		if (!arq.isEmpty())
			abrir(arq);
	}

	bool salvar()
	{
		const QString ext = extensao(arquivo_);
		if (arquivo_.isEmpty() || csv_ || (ext != "xlsx" && ext != "ods"))
			return salvarComo();
		return gravar(arquivo_);
	}

	bool salvarComo()
	{
		QString sugestao = arquivo_.isEmpty() ? pastaPadrao() + "/" + pasta_.folha(0).nome + ".xlsx" : arquivo_;
		QString filtro;
		QString arq = QFileDialog::getSaveFileName(this, "Salvar", sugestao, "Excel (*.xlsx);;OpenDocument (*.ods)", &filtro);
		if (arq.isEmpty())
			return false;
		const QString ext = extensao(arq);
		if (ext != "xlsx" && ext != "ods")
			arq += filtro.contains("ods") ? ".ods" : ".xlsx";
		if (!gravar(arq))
			return false;
		arquivo_ = arq;
		csv_ = false;
		recente(arq);
		titulo();
		return true;
	}

	bool gravar(const QString &arq)
	{
		QString erro;
		if (!gravarArquivo(arq, pasta_, &erro)) {
			QMessageBox::warning(this, "Números", "Não deu para salvar:\n" + erro);
			return false;
		}
		mudou_ = false;
		titulo();
		statusBar()->showMessage("Salvo em " + QDir::toNativeSeparators(arq), 4000);
		return true;
	}

	void exportar(const QString &ext)
	{
		const QString base = arquivo_.isEmpty() ? pastaPadrao() + "/" + pasta_.folha(0).nome
		                                        : QFileInfo(arquivo_).path() + "/" + QFileInfo(arquivo_).completeBaseName();
		static const QHash<QString, QString> filtros = {{"pdf", "PDF (*.pdf)"}, {"xlsx", "Excel (*.xlsx)"},
		                                                {"ods", "OpenDocument (*.ods)"}, {"csv", "CSV (*.csv)"}};
		QString arq = QFileDialog::getSaveFileName(this, "Exportar", base + "." + ext, filtros.value(ext));
		if (arq.isEmpty())
			return;
		if (extensao(arq) != ext)
			arq += "." + ext;
		QString erro;
		if (!gravarArquivo(arq, pasta_, &erro, grade_.folha()))
			QMessageBox::warning(this, "Números", "Não deu para exportar:\n" + erro);
		else
			statusBar()->showMessage("Exportado: " + QDir::toNativeSeparators(arq), 5000);
	}

	bool podeFechar()
	{
		if (!mudou_)
			return true;
		QMessageBox m(this);
		m.setWindowTitle("Números");
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

	Pasta pasta_;
	Grade grade_;
	QStackedWidget *pilha_ = nullptr, *inspetor_ = nullptr;
	Tabela *tabela_ = nullptr;
	QTabBar *abas_ = nullptr;
	QLineEdit *nomeCel_ = nullptr, *formula_ = nullptr;
	QToolBar *barra_ = nullptr;
	QToolButton *bFormatar_ = nullptr;
	QLabel *titulo_ = nullptr, *contas_ = nullptr;
	QVBoxLayout *recentes_ = nullptr;
	QComboBox *tipo_ = nullptr, *gTipo_ = nullptr;
	QSpinBox *casas_ = nullptr;
	QCheckBox *milhar_ = nullptr, *borda_ = nullptr;
	QToolButton *negrito_ = nullptr, *italico_ = nullptr, *sublinhado_ = nullptr;
	QToolButton *alin_[4] = {};
	QLineEdit *gTitulo_ = nullptr, *gDados_ = nullptr;
	std::vector<Quadro *> quadros_;
	int graficoEscolhido_ = -1;
	std::vector<std::vector<Folha>> antes_, depois_;
	Pasta::Recorte recorte_;
	QString textoCopiado_;
	QString arquivo_;
	bool csv_ = false, mudou_ = false, atualizando_ = false, ajustando_ = false;
};

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
QLabel#parte { font-family: "IBM Plex Serif", serif; font-size: 21px; }
QLabel#rotulo { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: @apagado; letter-spacing: 1px; }
QLabel#rodape { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: @apagado; }
QLabel#tituloDoc { font-family: "IBM Plex Serif", serif; font-size: 15px; }
QLabel#fx { font-family: "IBM Plex Serif", serif; font-style: italic; font-size: 15px; color: @apagado; }
QFrame#fio { background: @tinta; border: none; min-height: 1px; max-height: 1px; }
QToolBar#barra { background: @papel; border: none; border-bottom: 1px solid @tinta; padding: 4px 10px; spacing: 4px; }
QToolBar#barra QToolButton { font-size: 11px; padding: 3px 8px; border: 1px solid transparent; border-radius: 0; }
QToolBar#barra QToolButton:hover { border: 1px solid @tinta; }
QToolBar#barra QToolButton:checked { background: @tinta; color: @papel; }
QToolBar#barra QToolButton::menu-indicator { image: none; width: 0; }
QToolBar::separator { background: @fio; width: 1px; margin: 6px 6px; }
QWidget#barraFormula { background: @cartao; border-bottom: 1px solid @fio; }
QLineEdit#nomeCelula { font-family: "IBM Plex Mono", monospace; }
QLineEdit#formula { font-family: "IBM Plex Mono", monospace; }
QTableView#tabela { background: @folha; gridline-color: @grade; border: none; selection-background-color: transparent; }
QTableView#tabela QTableCornerButton::section { background: @cabecalho; border: none; border-right: 1px solid @fio; border-bottom: 1px solid @fio; }
QHeaderView { background: @cabecalho; }
QHeaderView::section { background: @cabecalho; color: @apagado; border: none; border-right: 1px solid @fio; border-bottom: 1px solid @fio; padding: 0 4px; font-family: "IBM Plex Mono", monospace; font-size: 11px; }
QHeaderView::section:checked { background: @fio; color: @tinta; }
QLineEdit#editorCelula { background: @folha; border: 2px solid #2f4b6e; padding: 0 2px; color: #1c1b19; }
QWidget#faixaAbas { background: @papel; border-top: 1px solid @tinta; }
QTabBar#abas::tab { background: transparent; padding: 5px 16px; border: 1px solid transparent; font-family: "IBM Plex Serif", serif; font-size: 13px; color: @apagado; }
QTabBar#abas::tab:selected { background: @folha; border: 1px solid @tinta; border-top: 1px solid @folha; color: #1c1b19; }
QStackedWidget#inspetor { background: @cartao; border-left: 1px solid @tinta; }
QStackedWidget#inspetor QScrollArea, QStackedWidget#inspetor QScrollArea > QWidget > QWidget, QStackedWidget#inspetor > QWidget { background: @cartao; }
QToolButton#alterna { background: @cartao; border: 1px solid @tinta; padding: 3px 6px; }
QToolButton#alterna:checked { background: @tinta; color: @cartao; }
QToolButton#alterna:hover { background: @fio; }
QToolButton#alterna:checked:hover { background: @tinta; }
QToolButton#amostra { border: 1px solid @tinta; }
QToolButton#cartaoModelo { border: 1px solid transparent; padding: 8px; font-family: "IBM Plex Serif", serif; font-size: 14px; }
QToolButton#cartaoModelo:hover { border: 1px solid @tinta; background: @cartao; }
QPushButton { background: @cartao; border: 1px solid @tinta; padding: 6px 14px; }
QPushButton:hover { background: @fio; }
QPushButton#botaoPrincipal { background: @tinta; color: @papel; font-weight: 600; }
QPushButton#recente { background: transparent; border: none; text-align: left; padding: 3px 0; font-family: "IBM Plex Serif", serif; font-size: 14px; }
QPushButton#recente:hover { text-decoration: underline; }
QComboBox, QLineEdit { background: @papel; border: 1px solid @tinta; padding: 3px 6px; min-height: 22px; selection-background-color: @tinta; selection-color: @papel; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: @cartao; border: 1px solid @tinta; selection-background-color: @tinta; selection-color: @cartao; }
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
	 .replace("@folha", c.folha.name()).replace("@grade", c.grade.name()).replace("@cabecalho", c.cabecalho.name());
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
	// A folha é sempre papel claro (como no Numbers), também no tema escuro.
	c.folha = QColor(0xff, 0xfd, 0xf8);
	c.grade = QColor(0xe4, 0xdf, 0xd3);
	c.cabecalho = c.noite ? QColor(0x27, 0x26, 0x23) : QColor(0xf3, 0xee, 0xe3);
	c.selecao = QColor(0x2f, 0x4b, 0x6e, 40);
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

// Um replicante (lume-mesa): o gráfico N (contado em todas as folhas, a
// partir de 1) de uma planilha, que se redesenha quando ela é gravada. Com
// SAIDA.png, desenha uma vez nela e diz o que mostrou (os testes).
struct Vivo {
	QString arq;
	int quer = 1;
	Pasta p;
	int f = -1;
	int g = -1;
	QString aviso;
	void carregar()
	{
		p = Pasta();
		f = g = -1;
		aviso.clear();
		QString erro;
		if (!QFileInfo::exists(arq)) {
			aviso = "A planilha " + QFileInfo(arq).fileName() + " não está mais aqui.";
			return;
		}
		if (!abrirArquivo(arq, p, &erro)) {
			aviso = erro;
			return;
		}
		int n = 0;
		for (int i = 0; i < int(p.folhas.size()); i++)
			for (int k = 0; k < int(p.folha(i).graficos.size()); k++)
				if (++n == quer) {
					f = i;
					g = k;
					return;
				}
		aviso = QFileInfo(arq).fileName() + " não tem mais esse gráfico.";
	}
	QString resumo() const
	{
		if (f < 0)
			return "aviso: " + aviso;
		static const char *tipos[] = {"colunas", "barras", "linhas", "pizza", "dispersão", "área"};
		const Grafico &gr = p.folha(f).graficos[size_t(g)];
		QStringList cats;
		std::vector<Serie> series;
		p.dadosDoGrafico(f, gr, cats, series);
		double soma = 0;
		for (const Serie &s : series)
			for (double v : s.v)
				soma += v;
		return QString("gráfico %1 \"%2\" %3 séries x %4, soma %5").arg(tipos[gr.tipo], gr.titulo).arg(series.size()).arg(cats.size()).arg(soma, 0, 'f', 2);
	}
};

static int replicante(QApplication &app, const QString &id, const QString &foto)
{
	Replicante r;
	if (!acharNaMesa(id, r) || r.dados.value(0) != "grafico") {
		std::fprintf(stderr, "lume-numeros: não há o replicante %s na mesa\n", qPrintable(id));
		return foto.isEmpty() ? 0 : 1;
	}
	auto *v = new Vivo;
	v->arq = r.dados.value(1);
	v->quer = std::max(1, r.dados.value(2).toInt());
	v->carregar();
	auto *j = new JanelaReplicante(r);
	j->textoAbrir = "Abrir nos Números";
	if (v->f >= 0) {
		const Grafico &g = v->p.folha(v->f).graficos[size_t(v->g)];
		j->tamanhoPadrao = QSize(int(g.w), int(g.h));
	}
	j->desenhar = [v](QPainter &p, const QRectF &r) {
		if (v->f >= 0) {
			desenharGrafico(p, r, v->p, v->f, v->p.folha(v->f).graficos[size_t(v->g)]);
			return;
		}
		const Tinta t;
		p.fillRect(r, t.papel);
		p.setPen(QPen(t.tinta, 1));
		p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
		QFont fn("IBM Plex Sans");
		fn.setPixelSize(13);
		p.setFont(fn);
		p.setPen(t.apagado);
		p.drawText(r.adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, v->aviso);
	};
	j->atualizar = [v] { v->carregar(); };
	j->abrir = [v] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {v->arq}); };
	if (!foto.isEmpty()) {
		j->resize(r.geo.isEmpty() ? j->tamanhoPadrao : r.geo.size());
		j->grab().save(foto);
		std::fputs((v->resumo() + "\n").toUtf8().constData(), stdout);
		return v->f >= 0 ? 0 : 1;
	}
	// Vivo: o arquivo gravado de novo (os apps gravam num arquivo novo e
	// trocam: o olho volta a ele) redesenha.
	// (LUME_REPLICANTE_DIZER: diz o que mostra a cada vez, para os testes.)
	auto dizer = [v] {
		if (qEnvironmentVariableIsSet("LUME_REPLICANTE_DIZER")) {
			std::fputs((v->resumo() + "\n").toUtf8().constData(), stdout);
			std::fflush(stdout);
		}
	};
	dizer();
	auto *olho = new QFileSystemWatcher(&app);
	olho->addPath(v->arq);
	olho->addPath(QFileInfo(v->arq).absolutePath());
	auto *espera = new QTimer(&app);
	espera->setSingleShot(true);
	espera->setInterval(400);
	QObject::connect(espera, &QTimer::timeout, &app, [v, j, olho, dizer] {
		if (QFileInfo::exists(v->arq) && !olho->files().contains(v->arq))
			olho->addPath(v->arq);
		v->carregar();
		j->update();
		dizer();
	});
	QObject::connect(olho, &QFileSystemWatcher::fileChanged, &app, [espera] { espera->start(); });
	QObject::connect(olho, &QFileSystemWatcher::directoryChanged, &app, [v, olho, espera] {
		if (QFileInfo::exists(v->arq) && !olho->files().contains(v->arq))
			espera->start();
	});
	j->mostrar();
	return app.exec();
}

static int uso()
{
	std::fprintf(stderr,
	             "uso: lume-numeros [ARQUIVO]\n"
	             "     lume-numeros --converter ENTRADA SAIDA   (xlsx, ods, csv, pdf)\n"
	             "     lume-numeros --modelo NOME SAIDA         (branco, orcamento, notas, juros, fluxo, indicadores)\n"
	             "     lume-numeros --resumo ARQUIVO\n"
	             "     lume-numeros --autoteste SAIDA\n"
	             "     lume-numeros --replicante ID [--foto SAIDA.png]   (da mesa: lume-mesa)\n"
	             "     lume-numeros --foto SAIDA.png [ARQUIVO | --modelo NOME | --galeria]\n");
	return 2;
}

int main(int argc, char **argv)
{
	bool semJanela = false;
	for (int i = 1; i < argc; i++) {
		const QByteArray a(argv[i]);
		if (a == "--replicante" && i + 2 < argc && QByteArray(argv[i + 2]) == "--foto")
			semJanela = true;
		if (a == "--converter" || a == "--resumo" || a == "--autoteste" || a == "--copiar-grafico" || (a == "--modelo" && i == 1))
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
	app.setApplicationName("lume-numeros");
	app.setApplicationDisplayName("Números");
	app.setDesktopFileName("lume-numeros");
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	                 [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });
	QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));
	const QStringList a = app.arguments();

	if (a.size() >= 4 && a[1] == "--converter") {
		Pasta p;
		QString erro;
		if (!abrirArquivo(a[2], p, &erro) || !gravarArquivo(a[3], p, &erro)) {
			std::fprintf(stderr, "lume-numeros: %s\n", qPrintable(erro));
			return 1;
		}
		return 0;
	}
	if (a.size() >= 4 && a[1] == "--modelo") {
		const bool existe = std::any_of(modelos().begin(), modelos().end(), [&](const Modelo &m) { return m.id == a[2]; });
		if (!existe)
			return uso();
		Pasta p;
		montarModelo(p, a[2]);
		QString erro;
		if (!gravarArquivo(a[3], p, &erro)) {
			std::fprintf(stderr, "lume-numeros: %s\n", qPrintable(erro));
			return 1;
		}
		return 0;
	}
	// O que o Copiar de um gráfico põe na área de transferência (o Palco e o
	// Páginas colam como gráfico): os gráficos de todas as folhas, contados
	// a partir de 1.
	if (a.size() >= 3 && a[1] == "--copiar-grafico") {
		Pasta p;
		QString erro;
		if (!abrirArquivo(a[2], p, &erro)) {
			std::fprintf(stderr, "lume-numeros: %s\n", qPrintable(erro));
			return 1;
		}
		const int quer = a.size() >= 4 ? a[3].toInt() : 1;
		int n = 0;
		for (int f = 0; f < int(p.folhas.size()); f++)
			for (const Grafico &g : p.folha(f).graficos)
				if (++n == quer) {
					std::fputs((gravarDados(dadosParaColar(p, f, g)) + "\n").toUtf8().constData(), stdout);
					return 0;
				}
		std::fprintf(stderr, "lume-numeros: não há o gráfico %d\n", quer);
		return 1;
	}
	if (a.size() >= 3 && a[1] == "--resumo") {
		Pasta p;
		QString erro;
		if (!abrirArquivo(a[2], p, &erro)) {
			std::fprintf(stderr, "lume-numeros: %s\n", qPrintable(erro));
			return 1;
		}
		std::fputs(resumo(p).toUtf8().constData(), stdout);
		return 0;
	}

	if (a.size() >= 3 && a[1] == "--replicante") {
		aparencia(app);
		return replicante(app, a[2], a.size() >= 5 && a[3] == "--foto" ? a[4] : QString());
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
		const QStringList t = qEnvironmentVariable("LUME_FOTO_TAMANHO", "1366x860").split('x');
		j.resize(t.value(0).toInt(), t.value(1).toInt());
		j.show();
		if (qEnvironmentVariableIsSet("LUME_FOTO_CELULA"))
			QTimer::singleShot(300, &app, [&] { j.irPara(qEnvironmentVariable("LUME_FOTO_CELULA")); });
		QTimer::singleShot(700, &app, [&] {
			j.grab().save(a[2]);
			app.quit();
		});
		return app.exec();
	}
	if (a.size() >= 2 && !a[1].startsWith("--"))
		j.abrir(a[1]);
	const QRect tela = app.primaryScreen() ? app.primaryScreen()->availableGeometry() : QRect(0, 0, 1920, 1080);
	if (tela.width() < 1500 || tela.height() < 950)
		j.showMaximized();
	else
		j.show();
	return app.exec();
}

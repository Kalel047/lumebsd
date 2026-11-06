// SPDX-License-Identifier: BSD-2-Clause
//
// lume-programas - "Programas do LumeBSD": a loja de programas, em
// português, para quem não quer decorar nomes de pacote.
//
//   lume-programas                 a janela: kits por área (Uso geral,
//                                  Exatas, Sociais aplicadas), categorias e
//                                  busca, que acha também pelo programa que
//                                  a pessoa usava ("matlab" acha o Octave)
//   lume-programas --instalar ID   abre já instalando um programa e fecha
//                                  no fim (o lume-compat usa para o Wine)
//   lume-programas --procurar TEXTO
//                                  abre já procurando (o lume-compat usa para
//                                  um app do Mac: o substituto dele)
//
// O catálogo é o mesmo do lume loja (/usr/share/lume/loja/catalogo), lido
// aqui direto. Instalar e remover passam pelo lume-loja --maquina; o que
// precisa de root vai pelo pkexec (polkit: org.lumebsd.programas). Apps da
// web não precisam de senha: viram um atalho no menu do usuário.
//
// Sem moc: só sinais do próprio Qt ligados a lambdas, para compilar com o
// clang de fora do FreeBSD contra os cabeçalhos do pacote qt6-base.

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSessionManager>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

static const char *LOJA = "/usr/sbin/lume-loja";

// LUME_LOJA troca o lume-loja (para testar a interface fora do LumeBSD).
static QString
loja()
{
	return qEnvironmentVariable("LUME_LOJA", LOJA);
}
static const char *CATALOGO = "/usr/share/lume/loja/catalogo";
static const char *ICONE = "/usr/share/lume/app/lume-programas.svg";
static const char *AJUDA = "/usr/share/lume/ajuda/index.html";

struct Programa {
	QString id, categoria, nome, parecido, descricao, icone;
	QStringList pacotes, kits;
	bool web = false;
	bool abre = true;	// "-" no catálogo: fontes, linguagens...
};

struct Categoria {
	QString id, nome, icone;
};

struct Kit {
	QString id, nome, descricao;
	QStringList inclui;
};

// Sem acentos e em minúsculas, para a busca ("matematica" acha
// "Matemática").
static QString
simples(const QString &s)
{
	QString d = s.normalized(QString::NormalizationForm_D);
	QString r;
	for (QChar c : d)
		if (c.category() != QChar::Mark_NonSpacing)
			r += c.toLower();
	return r;
}

static QString
tamanho(qint64 b)
{
	if (b >= 1000LL * 1000 * 1000)
		return QString::number(b / 1e9, 'f', 1).replace('.', ',') + " GB";
	return QString::number(qMax<qint64>(1, b / 1000000)) + " MB";
}

static QIcon
icone(const QString &nome, const QString &reserva)
{
	if (!nome.isEmpty() && QIcon::hasThemeIcon(nome))
		return QIcon::fromTheme(nome);
	return QIcon::fromTheme(reserva, QIcon::fromTheme("application-x-executable"));
}

static bool
perguntar(QWidget *pai, const QString &titulo, const QString &texto,
    const QString &sim)
{
	QMessageBox m(QMessageBox::Question, titulo, texto, QMessageBox::NoButton, pai);
	m.setTextFormat(Qt::RichText);
	QPushButton *s = m.addButton(sim, QMessageBox::AcceptRole);
	m.addButton("Cancelar", QMessageBox::RejectRole);
	m.setDefaultButton(s);
	m.exec();
	return m.clickedButton() == s;
}

// Um programa na lista: ícone, nome, "parecido com", descrição e o botão.
struct Cartao {
	QFrame *quadro;
	QLabel *nome, *parecido, *descricao, *marca;
	QPushButton *botao, *remover;
};

class Janela : public QWidget {
public:
	Janela(const QString &instalar_id);
	void procurar(const QString &texto)
	{
		busca_->setText(texto);
	}

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	void ler_catalogo();
	void montar();
	QWidget *montar_inicio();
	QWidget *montar_lista();
	void mostrar_lista(const QString &categoria, const QString &kit,
	    const QString &titulo);
	void filtrar();
	void atualizar_estado(std::function<void()> depois = nullptr);
	void pintar();
	void instalar(const QString &id);
	void remover(const QString &id);
	void instalar_kit(const QString &kit);
	QStringList programas_do_kit(const QString &kit) const;
	void tamanho_de(const QStringList &ids, std::function<void(qint64)> fim);
	void rodar(const QStringList &args, bool root, const QString &titulo,
	    std::function<void(bool)> fim);
	void ocupar(bool sim);

	QList<Programa> progs_;
	QMap<QString, int> por_id_;
	QList<Categoria> cats_;
	QList<Kit> kits_;
	QSet<QString> instalados_, kits_feitos_;
	bool repo_ok_ = true, ocupado_ = false;
	QString instalar_id_;

	QListWidget *lado_;
	QLineEdit *busca_;
	QStackedWidget *pilha_;
	QLabel *titulo_lista_, *vazio_;
	QWidget *lista_;
	QVBoxLayout *lista_v_;
	QMap<QString, Cartao> cartoes_;
	QMap<QString, QPushButton *> botoes_kit_;
	QMap<QString, QLabel *> marcas_kit_;
	QString filtro_cat_, filtro_kit_;

	QFrame *rodape_;
	QLabel *passo_;
	QProgressBar *barra_;
	QToolButton *ver_detalhes_;
	QPlainTextEdit *detalhes_;
};

Janela::Janela(const QString &instalar_id) : instalar_id_(instalar_id)
{
	setWindowTitle("Programas do LumeBSD");
	setWindowIcon(QIcon(ICONE));
	resize(1080, 760);
	ler_catalogo();
	montar();
	// Abrir e já digitar: a busca começa com o foco.
	busca_->setFocus();
	atualizar_estado([this] {
		if (instalar_id_.isEmpty())
			return;
		int i = por_id_.value(instalar_id_, -1);
		if (i < 0) {
			close();
			return;
		}
		const Programa &p = progs_[i];
		for (int r = 0; r < lado_->count(); r++)
			if (lado_->item(r)->data(Qt::UserRole).toString() == p.categoria)
				lado_->setCurrentRow(r);
		instalar(p.id);
	});
}

void
Janela::ler_catalogo()
{
	QFile f(qEnvironmentVariable("LUME_LOJA_CATALOGO", CATALOGO));
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QTextStream in(&f);
	while (!in.atEnd()) {
		QString l = in.readLine();
		if (l.isEmpty() || l.startsWith('#'))
			continue;
		if (l.startsWith("@categoria ")) {
			QStringList c = l.mid(11).split('|');
			if (c.size() >= 3)
				cats_ << Categoria{c[0], c[1], c[2]};
			continue;
		}
		if (l.startsWith("@kit ")) {
			QStringList c = l.mid(5).split('|');
			if (c.size() >= 4)
				kits_ << Kit{c[0], c[1], c[2],
				    c[3].split(' ', Qt::SkipEmptyParts)};
			continue;
		}
		QStringList c = l.split('|');
		if (c.size() != 9)
			continue;
		Programa p;
		p.id = c[0];
		p.categoria = c[1];
		p.nome = c[2];
		p.pacotes = c[3].split(' ', Qt::SkipEmptyParts);
		p.kits = c[4].split(' ', Qt::SkipEmptyParts);
		p.parecido = c[5];
		p.descricao = c[6];
		p.icone = c[7];
		p.web = c[3].startsWith("web:");
		p.abre = c[8] != "-";
		por_id_[p.id] = progs_.size();
		progs_ << p;
	}
}

QStringList
Janela::programas_do_kit(const QString &kit) const
{
	QStringList ks{kit};
	for (const Kit &k : kits_)
		if (k.id == kit)
			ks << k.inclui;
	QStringList r;
	for (const Programa &p : progs_)
		for (const QString &k : p.kits)
			if (ks.contains(k)) {
				r << p.id;
				break;
			}
	return r;
}

// A cara da loja: papel e tinta, serifa nos títulos, mono nos rótulos, fios
// finos em vez de sombras; não depende do tema do KDE (Fusion + esta folha).
static const char *ESTILO = R"QSS(
* { font-family: "IBM Plex Sans", "Noto Sans", sans-serif; color: #1c1b19; }
QWidget#raiz, QWidget#pagina, QScrollArea, QScrollArea > QWidget > QWidget { background: #efeadf; }
QLabel { background: transparent; }
QLabel#marca { font-family: "IBM Plex Serif", serif; font-size: 25px; }
QLabel#sub, QLabel#parecido, QLabel#italico {
	font-family: "IBM Plex Serif", serif; font-style: italic; color: #5d5950; }
QLabel#sub { font-size: 15px; }
QLabel#italico { font-size: 15px; }
QLabel#capitulo { font-family: "IBM Plex Serif", serif; font-size: 30px; }
QLabel#secao { font-family: "IBM Plex Serif", serif; font-size: 26px; }
QLabel#numero { font-family: "IBM Plex Serif", serif; font-size: 44px; color: #1c1b19; }
QLabel#titulo { font-family: "IBM Plex Serif", serif; font-size: 18px; }
QLabel#kit { font-family: "IBM Plex Serif", serif; font-size: 22px; }
QLabel#texto { font-family: "IBM Plex Serif", serif; font-size: 14px; }
QLabel#rotulo, QLabel#lista {
	font-family: "IBM Plex Mono", monospace; font-size: 11px; color: #5d5950; }
QLabel#vazio { font-family: "IBM Plex Serif", serif; font-style: italic; font-size: 15px; color: #5d5950; }
QFrame#fio { background: #1c1b19; border: none; min-height: 1px; max-height: 1px; }
QFrame#fioV { background: #1c1b19; border: none; min-width: 1px; max-width: 1px; }
QFrame#cartao, QFrame#capa { background: #f6f2e9; border: 1px solid #1c1b19; }
QFrame#cartao QLabel, QFrame#capa QLabel { background: transparent; }
QLineEdit#busca {
	background: transparent; border: none; border-bottom: 1px solid #1c1b19;
	padding: 6px 2px; font-family: "IBM Plex Serif", serif; font-size: 16px;
	selection-background-color: #1c1b19; selection-color: #efeadf; }
QListWidget#indice {
	background: #efeadf; border: none; outline: 0;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; }
QListWidget#indice::item { padding: 7px 12px; border: none; color: #1c1b19; }
QListWidget#indice::item:selected { background: #1c1b19; color: #efeadf; }
QListWidget#indice::item:hover:!selected { background: #e2dccd; }
QPushButton {
	background: transparent; border: 1px solid #1c1b19; padding: 6px 16px;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; min-height: 18px; }
QPushButton:hover { background: #e2dccd; }
QPushButton:pressed { background: #1c1b19; color: #efeadf; }
QPushButton#primario { background: #1c1b19; color: #efeadf; }
QPushButton#primario:hover { background: #3b3934; }
QPushButton:disabled, QPushButton#primario:disabled {
	background: transparent; color: #9b968b; border: 1px solid #bdb7a9; }
QPushButton#link, QToolButton#link {
	border: none; padding: 2px 0; background: transparent; text-decoration: underline;
	font-family: "IBM Plex Mono", monospace; font-size: 12px; }
QPushButton#link:hover, QToolButton#link:hover { background: transparent; color: #5d5950; }
QProgressBar { border: 1px solid #1c1b19; background: transparent; max-height: 6px; }
QProgressBar::chunk { background: #1c1b19; }
QPlainTextEdit { background: #f6f2e9; border: 1px solid #1c1b19;
	font-family: "IBM Plex Mono", monospace; font-size: 11px; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 2px; }
QScrollBar::handle:vertical { background: #8a857a; min-height: 36px; }
QScrollBar::handle:vertical:hover { background: #1c1b19; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QMessageBox, QDialog { background: #efeadf; }
QMessageBox QLabel { font-family: "IBM Plex Serif", serif; font-size: 14px; }
QToolTip { background: #1c1b19; color: #efeadf; border: none; padding: 4px 6px; }
)QSS";

static QFrame *
fio(bool vertical = false)
{
	auto *f = new QFrame;
	f->setObjectName(vertical ? "fioV" : "fio");
	return f;
}

static QLabel *
rotulo(const QString &texto, const char *nome)
{
	auto *l = new QLabel(texto);
	l->setObjectName(nome);
	return l;
}

// Ícones em tons de cinza, mesmo se o tema tiver cor.
static QPixmap
cinza(const QIcon &ic, int lado)
{
	QImage img = ic.pixmap(lado, lado).toImage().convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < img.height(); y++) {
		QRgb *l = reinterpret_cast<QRgb *>(img.scanLine(y));
		for (int x = 0; x < img.width(); x++) {
			int g = qGray(l[x]);
			l[x] = qRgba(g, g, g, qAlpha(l[x]));
		}
	}
	return QPixmap::fromImage(img);
}

static const char *ROMANOS[] = {"I", "II", "III", "IV", "V", "VI"};

void
Janela::montar()
{
	setObjectName("raiz");
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	// Cabeçalho: o nome como um título de livro, a busca como uma linha.
	auto *topo = new QHBoxLayout;
	topo->setContentsMargins(32, 22, 32, 18);
	topo->setSpacing(18);
	auto *nomes = new QVBoxLayout;
	nomes->setSpacing(0);
	nomes->addWidget(rotulo("Programas", "marca"));
	nomes->addWidget(rotulo("do LumeBSD, por área de estudo", "sub"));
	topo->addLayout(nomes);
	topo->addStretch(1);
	busca_ = new QLineEdit;
	busca_->setObjectName("busca");
	busca_->setPlaceholderText("Procurar: matlab, excel, autocad, spss…");
	busca_->setClearButtonEnabled(true);
	busca_->setMinimumWidth(360);
	topo->addWidget(busca_, 0, Qt::AlignBottom);
	auto *ajuda = new QPushButton("Ajuda");
	ajuda->setObjectName("link");
	ajuda->setCursor(Qt::PointingHandCursor);
	QObject::connect(ajuda, &QPushButton::clicked, this, [] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(AJUDA));
	});
	topo->addWidget(ajuda, 0, Qt::AlignBottom);
	v->addLayout(topo);
	v->addWidget(fio());

	auto *meio = new QHBoxLayout;
	meio->setSpacing(0);
	// O índice, como um sumário: números e nomes em mono.
	lado_ = new QListWidget;
	lado_->setObjectName("indice");
	lado_->setFixedWidth(270);
	lado_->setFocusPolicy(Qt::NoFocus);
	lado_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto add = [this](const QString &nome, const QString &dado) {
		auto *it = new QListWidgetItem(nome);
		it->setData(Qt::UserRole, dado);
		lado_->addItem(it);
	};
	add("00  Início: kits por área", "@inicio");
	add("··  Instalados", "@instalados");
	auto *sep = new QListWidgetItem;
	sep->setFlags(Qt::NoItemFlags);
	sep->setSizeHint(QSize(0, 12));
	lado_->addItem(sep);
	int n = 1;
	for (const Categoria &c : cats_)
		add(QString("%1  %2").arg(n++, 2, 10, QChar('0')).arg(c.nome), c.id);
	auto *colIndice = new QVBoxLayout;
	colIndice->setContentsMargins(20, 18, 0, 18);
	colIndice->addWidget(lado_);
	meio->addLayout(colIndice);
	meio->addSpacing(20);
	meio->addWidget(fio(true));

	pilha_ = new QStackedWidget;
	pilha_->addWidget(montar_inicio());
	pilha_->addWidget(montar_lista());
	meio->addWidget(pilha_, 1);
	v->addLayout(meio, 1);

	// Rodapé: o progresso de uma instalação, numa linha.
	rodape_ = new QFrame;
	rodape_->setObjectName("pagina");
	auto *r = new QVBoxLayout(rodape_);
	r->setContentsMargins(32, 0, 32, 14);
	r->setSpacing(10);
	r->addWidget(fio());
	auto *rl = new QHBoxLayout;
	passo_ = rotulo(QString(), "lista");
	barra_ = new QProgressBar;
	barra_->setTextVisible(false);
	ver_detalhes_ = new QToolButton;
	ver_detalhes_->setObjectName("link");
	ver_detalhes_->setText("detalhes");
	ver_detalhes_->setCheckable(true);
	rl->addWidget(passo_, 3);
	rl->addWidget(barra_, 2);
	rl->addWidget(ver_detalhes_);
	r->addLayout(rl);
	detalhes_ = new QPlainTextEdit;
	detalhes_->setReadOnly(true);
	detalhes_->setMaximumHeight(140);
	detalhes_->hide();
	r->addWidget(detalhes_);
	QObject::connect(ver_detalhes_, &QToolButton::toggled, this, [this](bool on) {
		detalhes_->setVisible(on);
	});
	rodape_->hide();
	v->addWidget(rodape_);

	QObject::connect(lado_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0)
			return;
		QString d = lado_->item(row)->data(Qt::UserRole).toString();
		if (d.isEmpty())
			return;
		busca_->blockSignals(true);
		busca_->clear();
		busca_->blockSignals(false);
		if (d == "@inicio") {
			pilha_->setCurrentIndex(0);
		} else if (d == "@instalados") {
			mostrar_lista("@instalados", QString(), "Instalados");
		} else {
			for (const Categoria &c : cats_)
				if (c.id == d)
					mostrar_lista(c.id, QString(), c.nome);
		}
	});
	QObject::connect(busca_, &QLineEdit::textChanged, this, [this](const QString &t) {
		if (t.trimmed().isEmpty()) {
			lado_->setCurrentRow(lado_->currentRow() < 0 ? 0 : lado_->currentRow());
			if (pilha_->currentIndex() == 1)
				filtrar();
			return;
		}
		lado_->blockSignals(true);
		lado_->clearSelection();
		lado_->setCurrentRow(-1);
		lado_->blockSignals(false);
		mostrar_lista(QString(), QString(), QString("Procurando “%1”").arg(t.trimmed()));
	});
	lado_->setCurrentRow(0);
}

QWidget *
Janela::montar_inicio()
{
	auto *rolar = new QScrollArea;
	rolar->setWidgetResizable(true);
	rolar->setFrameShape(QFrame::NoFrame);
	auto *w = new QWidget;
	w->setObjectName("pagina");
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(40, 28, 40, 28);
	v->setSpacing(18);

	v->addWidget(rotulo("Para que você vai usar o computador?", "capitulo"));
	auto *s = rotulo("Cada kit instala de uma vez os programas da sua área, em português. "
	    "Depois, procure acima pelo programa que você usava no Windows ou no Mac: "
	    "o LumeBSD mostra o que usar no lugar.", "italico");
	s->setWordWrap(true);
	v->addWidget(s);
	v->addSpacing(6);

	int n = 0;
	for (const Kit &k : kits_) {
		auto *q = new QFrame;
		q->setObjectName("capa");
		auto *h = new QHBoxLayout(q);
		h->setContentsMargins(22, 18, 22, 18);
		h->setSpacing(22);
		auto *num = rotulo(ROMANOS[qMin(n++, 5)], "numero");
		num->setFixedWidth(64);
		num->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
		h->addWidget(num);
		h->addWidget(fio(true));
		auto *tv = new QVBoxLayout;
		tv->setSpacing(6);
		tv->addWidget(rotulo(k.nome, "kit"));
		auto *d = rotulo(k.descricao, "texto");
		d->setWordWrap(true);
		tv->addWidget(d);
		QStringList nomes;
		for (const QString &id : programas_do_kit(k.id))
			nomes << progs_[por_id_[id]].nome;
		auto *lst = rotulo(nomes.join("  ·  ").toUpper(), "lista");
		lst->setWordWrap(true);
		tv->addSpacing(4);
		tv->addWidget(lst);
		h->addLayout(tv, 1);
		auto *bv = new QVBoxLayout;
		bv->setSpacing(8);
		auto *marca = rotulo("✓ INSTALADO", "rotulo");
		marca->hide();
		auto *b = new QPushButton("Instalar o kit");
		b->setObjectName("primario");
		b->setCursor(Qt::PointingHandCursor);
		auto *ver = new QPushButton("ver os programas");
		ver->setObjectName("link");
		ver->setCursor(Qt::PointingHandCursor);
		bv->addWidget(marca, 0, Qt::AlignRight);
		bv->addWidget(b);
		bv->addWidget(ver, 0, Qt::AlignRight);
		bv->addStretch(1);
		h->addLayout(bv);
		botoes_kit_[k.id] = b;
		marcas_kit_[k.id] = marca;
		QString kid = k.id, knome = k.nome;
		QObject::connect(b, &QPushButton::clicked, this, [this, kid] { instalar_kit(kid); });
		QObject::connect(ver, &QPushButton::clicked, this, [this, kid, knome] {
			lado_->blockSignals(true);
			lado_->setCurrentRow(-1);
			lado_->blockSignals(false);
			mostrar_lista(QString(), kid, "Kit " + knome);
		});
		v->addWidget(q);
	}

	v->addSpacing(4);
	auto *dica = rotulo("Programas do Windows? Muitos abrem com dois cliques no .exe (o LumeBSD "
	    "oferece o Wine na primeira vez). Office, Teams, Zoom? Estão em Na web, como apps no menu. "
	    "Do Mac? Procure pelo nome: a loja mostra o substituto.", "italico");
	dica->setWordWrap(true);
	v->addWidget(dica);
	v->addStretch(1);
	rolar->setWidget(w);
	return rolar;
}

QWidget *
Janela::montar_lista()
{
	auto *fora = new QWidget;
	fora->setObjectName("pagina");
	auto *fv = new QVBoxLayout(fora);
	fv->setContentsMargins(0, 0, 0, 0);
	fv->setSpacing(0);
	auto *cab = new QVBoxLayout;
	cab->setContentsMargins(40, 26, 40, 10);
	cab->setSpacing(10);
	titulo_lista_ = rotulo(QString(), "secao");
	cab->addWidget(titulo_lista_);
	cab->addWidget(fio());
	fv->addLayout(cab);

	auto *rolar = new QScrollArea;
	rolar->setWidgetResizable(true);
	rolar->setFrameShape(QFrame::NoFrame);
	lista_ = new QWidget;
	lista_->setObjectName("pagina");
	lista_v_ = new QVBoxLayout(lista_);
	lista_v_->setContentsMargins(40, 10, 40, 28);
	lista_v_->setSpacing(12);

	for (const Programa &p : progs_) {
		Cartao c;
		c.quadro = new QFrame;
		c.quadro->setObjectName("cartao");
		auto *h = new QHBoxLayout(c.quadro);
		h->setContentsMargins(18, 14, 18, 14);
		h->setSpacing(18);
		QString reserva;
		for (const Categoria &k : cats_)
			if (k.id == p.categoria)
				reserva = k.icone;
		auto *ic = new QLabel;
		ic->setPixmap(cinza(icone(p.icone, reserva), 40));
		ic->setAlignment(Qt::AlignTop);
		h->addWidget(ic);
		auto *tv = new QVBoxLayout;
		tv->setSpacing(3);
		auto *topo = new QHBoxLayout;
		topo->setSpacing(10);
		c.nome = rotulo(p.nome, "titulo");
		topo->addWidget(c.nome);
		if (p.web)
			topo->addWidget(rotulo("NA WEB", "rotulo"), 0, Qt::AlignBaseline);
		c.marca = rotulo("✓ INSTALADO", "rotulo");
		topo->addWidget(c.marca, 0, Qt::AlignBaseline);
		topo->addStretch(1);
		tv->addLayout(topo);
		c.parecido = rotulo(p.parecido.isEmpty() ? QString() : "no lugar de " + p.parecido, "parecido");
		c.parecido->setVisible(!p.parecido.isEmpty());
		tv->addWidget(c.parecido);
		c.descricao = rotulo(p.descricao, "texto");
		c.descricao->setWordWrap(true);
		tv->addSpacing(2);
		tv->addWidget(c.descricao);
		h->addLayout(tv, 1);
		auto *bv = new QVBoxLayout;
		bv->setSpacing(6);
		c.botao = new QPushButton;
		c.botao->setMinimumWidth(124);
		c.botao->setCursor(Qt::PointingHandCursor);
		c.remover = new QPushButton("remover");
		c.remover->setObjectName("link");
		c.remover->setCursor(Qt::PointingHandCursor);
		bv->addWidget(c.botao);
		bv->addWidget(c.remover, 0, Qt::AlignRight);
		bv->addStretch(1);
		h->addLayout(bv);
		QString id = p.id;
		QObject::connect(c.botao, &QPushButton::clicked, this, [this, id] {
			if (instalados_.contains(id))
				QProcess::startDetached(loja(), {"abrir", id});
			else
				instalar(id);
		});
		QObject::connect(c.remover, &QPushButton::clicked, this, [this, id] { remover(id); });
		lista_v_->addWidget(c.quadro);
		cartoes_[p.id] = c;
	}
	vazio_ = rotulo("Nada por aqui.", "vazio");
	vazio_->setWordWrap(true);
	lista_v_->addWidget(vazio_);
	lista_v_->addStretch(1);
	rolar->setWidget(lista_);
	fv->addWidget(rolar, 1);
	return fora;
}

void
Janela::mostrar_lista(const QString &categoria, const QString &kit,
    const QString &titulo)
{
	filtro_cat_ = categoria;
	filtro_kit_ = kit;
	titulo_lista_->setText(titulo);
	pilha_->setCurrentIndex(1);
	filtrar();
}

void
Janela::filtrar()
{
	QString q = simples(busca_->text().trimmed());
	QStringList do_kit = filtro_kit_.isEmpty() ? QStringList() : programas_do_kit(filtro_kit_);
	int n = 0;
	for (const Programa &p : progs_) {
		bool ver;
		if (!q.isEmpty()) {
			// Todas as palavras, em qualquer ordem: "microsoft excel" acha
			// o que diz "Excel" e "Microsoft Office".
			const QString onde = simples(p.nome + " " + p.parecido + " " + p.descricao + " " + p.id);
			ver = true;
			for (const QString &w : q.split(' ', Qt::SkipEmptyParts))
				ver = ver && onde.contains(w);
		}
		else if (!filtro_kit_.isEmpty())
			ver = do_kit.contains(p.id);
		else if (filtro_cat_ == "@instalados")
			ver = instalados_.contains(p.id);
		else
			ver = p.categoria == filtro_cat_;
		cartoes_[p.id].quadro->setVisible(ver);
		n += ver;
	}
	if (!q.isEmpty())
		vazio_->setText(QString("Nada encontrado para “%1”. Tente o nome do programa que você "
		    "usava, ou o que quer fazer: planilha, vídeo, circuito…").arg(busca_->text().trimmed()));
	else if (filtro_cat_ == "@instalados")
		vazio_->setText("Nenhum programa do catálogo instalado ainda. Comece por um kit, no Início.");
	else
		vazio_->setText("Nada por aqui.");
	vazio_->setVisible(n == 0);
}

void
Janela::pintar()
{
	for (const Programa &p : progs_) {
		Cartao &c = cartoes_[p.id];
		bool tem = instalados_.contains(p.id);
		c.marca->setVisible(tem);
		c.remover->setVisible(tem);
		c.botao->setText(tem ? (p.abre ? "Abrir" : "Instalado") : (p.web ? "Pôr no menu" : "Instalar"));
		// Instalar é a ação principal (tinta cheia); Abrir, contorno.
		c.botao->setObjectName(tem ? "" : "primario");
		c.botao->style()->unpolish(c.botao);
		c.botao->style()->polish(c.botao);
		c.botao->setEnabled(!ocupado_ && !(tem && !p.abre));
		c.remover->setEnabled(!ocupado_);
	}
	for (const Kit &k : kits_) {
		bool tudo = true;
		for (const QString &id : programas_do_kit(k.id))
			tudo &= instalados_.contains(id);
		marcas_kit_[k.id]->setVisible(tudo);
		botoes_kit_[k.id]->setText(tudo ? "Instalado" : "Instalar o kit");
		botoes_kit_[k.id]->setVisible(!tudo);
		botoes_kit_[k.id]->setEnabled(!tudo && !ocupado_);
	}
	if (pilha_->currentIndex() == 1)
		filtrar();
}

void
Janela::atualizar_estado(std::function<void()> depois)
{
	auto *p = new QProcess(this);
	QObject::connect(p, &QProcess::finished, this, [=](int, QProcess::ExitStatus) {
		instalados_.clear();
		kits_feitos_.clear();
		for (const QString &l : QString::fromUtf8(p->readAllStandardOutput()).split('\n')) {
			if (l.startsWith("@instalado "))
				instalados_ << l.mid(11).trimmed();
			else if (l.startsWith("@kit "))
				kits_feitos_ << l.mid(5).trimmed();
			else if (l.startsWith("@repo "))
				repo_ok_ = l.mid(6).trimmed() == "ok";
		}
		p->deleteLater();
		pintar();
		if (depois)
			depois();
	});
	p->start(loja(), {"--maquina", "estado"});
}

void
Janela::tamanho_de(const QStringList &ids, std::function<void(qint64)> fim)
{
	if (!repo_ok_) {
		fim(-1);
		return;
	}
	auto *p = new QProcess(this);
	QObject::connect(p, &QProcess::finished, this, [=](int, QProcess::ExitStatus) {
		qint64 t = -1;
		for (const QString &l : QString::fromUtf8(p->readAllStandardOutput()).split('\n'))
			if (l.startsWith("@tamanho "))
				t = l.mid(9).trimmed().toLongLong();
		p->deleteLater();
		fim(t);
	});
	p->start(loja(), QStringList{"--maquina", "tamanho"} + ids);
}

void
Janela::ocupar(bool sim)
{
	ocupado_ = sim;
	lado_->setEnabled(!sim);
	busca_->setEnabled(!sim);
	pintar();
	setCursor(sim ? Qt::BusyCursor : Qt::ArrowCursor);
}

void
Janela::rodar(const QStringList &args, bool root, const QString &titulo,
    std::function<void(bool)> fim)
{
	ocupar(true);
	rodape_->show();
	detalhes_->clear();
	passo_->setText(titulo);
	barra_->setRange(0, 0);
	auto *p = new QProcess(this);
	QStringList a;
	if (root)
		a << LOJA;
	a << "--maquina" << args;
	auto *buf = new QString;
	auto *erro = new QString;
	QObject::connect(p, &QProcess::readyReadStandardOutput, this, [=] {
		*buf += QString::fromUtf8(p->readAllStandardOutput());
		int i;
		while ((i = buf->indexOf('\n')) >= 0) {
			QString l = buf->left(i).trimmed();
			buf->remove(0, i + 1);
			if (l.startsWith("@progresso ")) {
				barra_->setRange(0, 100);
				barra_->setValue(l.mid(11).toInt());
			} else if (l.startsWith("@pacote ")) {
				QStringList c = l.mid(8).split(' ');
				passo_->setText(QString("%1 — %2 %3").arg(titulo, c.value(0), c.value(1)));
				detalhes_->appendPlainText(l.mid(8));
			} else if (l.startsWith("@passo ")) {
				QStringList c = l.mid(7).split(' ');
				passo_->setText(QString("%1 — %2").arg(titulo, c.mid(2).join(' ')));
			} else if (l.startsWith("@erro ")) {
				*erro = l.mid(6);
			} else if (!l.startsWith('@') && !l.isEmpty()) {
				detalhes_->appendPlainText(l);
			}
		}
	});
	QObject::connect(p, &QProcess::readyReadStandardError, this, [=] {
		detalhes_->appendPlainText(QString::fromUtf8(p->readAllStandardError()).trimmed());
	});
	auto acabar = [=](int code) {
		QString e = *erro;
		delete buf;
		delete erro;
		p->deleteLater();
		rodape_->hide();
		ocupar(false);
		// 126/127: a pessoa cancelou a senha do pkexec.
		bool cancelou = root && (code == 126 || code == 127) && e.isEmpty();
		if (code != 0 && !cancelou) {
			QMessageBox m(QMessageBox::Warning, "Programas do LumeBSD",
			    e.isEmpty() ? QString("Não deu certo (código %1).").arg(code)
			    : "Não deu certo: " + e, QMessageBox::Ok, this);
			if (!detalhes_->toPlainText().isEmpty())
				m.setDetailedText(detalhes_->toPlainText());
			m.exec();
		}
		atualizar_estado([=] { fim(code == 0); });
	};
	QObject::connect(p, &QProcess::finished, this,
	    [=](int code, QProcess::ExitStatus) { acabar(code); });
	QObject::connect(p, &QProcess::errorOccurred, this, [=](QProcess::ProcessError err) {
		if (err == QProcess::FailedToStart)
			acabar(-1);
	});
	p->start(root ? QString("pkexec") : loja(), a);
}

void
Janela::instalar(const QString &id)
{
	const Programa &p = progs_[por_id_[id]];
	auto sair = [this](bool) {
		if (!instalar_id_.isEmpty())
			close();
	};
	if (p.web) {
		rodar({"instalar", id}, false, "Pondo no menu: " + p.nome, sair);
		return;
	}
	passo_->setText("Calculando o tamanho…");
	ocupar(true);
	tamanho_de({id}, [=](qint64 t) {
		ocupar(false);
		QString txt = QString("<b>Instalar %1?</b><br><br>%2").arg(p.nome.toHtmlEscaped(),
		    p.descricao.toHtmlEscaped());
		if (t > 0)
			txt += QString("<br><br>Ocupa cerca de %1 no disco.").arg(tamanho(t));
		txt += "<br><br>Precisa de internet. Pede a sua senha.";
		if (!perguntar(this, "Instalar", txt, "Instalar")) {
			sair(false);
			return;
		}
		rodar({"instalar", id}, true, "Instalando " + p.nome, [=](bool ok) {
			if (ok && instalar_id_.isEmpty())
				cartoes_[id].botao->setFocus();
			sair(ok);
		});
	});
}

void
Janela::remover(const QString &id)
{
	const Programa &p = progs_[por_id_[id]];
	QString txt = p.web
	    ? QString("Tirar <b>%1</b> do menu?").arg(p.nome.toHtmlEscaped())
	    : QString("<b>Remover %1?</b><br><br>Os seus arquivos não são apagados; só o "
		"programa. O que outros programas usam fica.").arg(p.nome.toHtmlEscaped());
	if (!perguntar(this, "Remover", txt, "Remover"))
		return;
	rodar({"remover", id}, !p.web, "Removendo " + p.nome, [](bool) {});
}

void
Janela::instalar_kit(const QString &kit)
{
	QString nome;
	for (const Kit &k : kits_)
		if (k.id == kit)
			nome = k.nome;
	QStringList faltam, nomes;
	for (const QString &id : programas_do_kit(kit))
		if (!instalados_.contains(id)) {
			faltam << id;
			nomes << progs_[por_id_[id]].nome;
		}
	if (faltam.isEmpty())
		return;
	passo_->setText("Calculando o tamanho…");
	ocupar(true);
	tamanho_de(faltam, [=](qint64 t) {
		ocupar(false);
		QString txt = QString("<b>Instalar o kit %1?</b><br><br>Vai instalar: %2.")
		    .arg(nome.toHtmlEscaped(), nomes.join(", ").toHtmlEscaped());
		if (t > 0)
			txt += QString("<br><br>Ocupa cerca de %1 no disco; o download leva "
			    "alguns minutos.").arg(tamanho(t));
		txt += "<br><br>Precisa de internet. Pede a sua senha.";
		if (!perguntar(this, "Instalar o kit", txt, "Instalar"))
			return;
		rodar({"kit", kit}, true, "Instalando o kit " + nome, [](bool) {});
	});
}

void
Janela::closeEvent(QCloseEvent *e)
{
	if (ocupado_ && !rodape_->isHidden()) {
		QMessageBox::information(this, "Programas do LumeBSD",
		    "Espere a instalação terminar para fechar.");
		e->ignore();
		return;
	}
	e->accept();
}

int
main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QTranslator qt;
	if (qt.load(QLocale(), "qtbase", "_",
	    QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qt);
	app.setApplicationName("lume-programas");
	app.setApplicationDisplayName("Programas do LumeBSD");
	app.setDesktopFileName("lume-programas");
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	    [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });

	// A loja tem a própria cara (papel e tinta), seja qual for o tema.
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette pal;
	const QColor papel(0xef, 0xea, 0xdf), tinta(0x1c, 0x1b, 0x19), cartao(0xf6, 0xf2, 0xe9);
	pal.setColor(QPalette::Window, papel);
	pal.setColor(QPalette::Base, cartao);
	pal.setColor(QPalette::AlternateBase, papel);
	pal.setColor(QPalette::Button, papel);
	for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipBase})
		pal.setColor(r, tinta);
	pal.setColor(QPalette::Highlight, tinta);
	pal.setColor(QPalette::HighlightedText, papel);
	pal.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x85, 0x7a));
	app.setPalette(pal);
	app.setStyleSheet(ESTILO);

	QString instalar, procurar;
	QStringList a = app.arguments();
	int i = a.indexOf("--instalar");
	if (i >= 0 && i + 1 < a.size())
		instalar = a[i + 1];
	i = a.indexOf("--procurar");
	if (i >= 0 && i + 1 < a.size())
		procurar = a[i + 1];

	Janela j(instalar);
	j.show();
	if (!procurar.isEmpty())
		j.procurar(procurar);
	return app.exec();
}

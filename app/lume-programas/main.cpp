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
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSessionManager>
#include <QSet>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

static const char *LOJA = "/usr/sbin/lume-loja";
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
	resize(980, 680);
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

void
Janela::montar()
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	// Topo: nome e busca.
	auto *topo = new QHBoxLayout;
	topo->setContentsMargins(20, 14, 20, 14);
	auto *ic = new QLabel;
	ic->setPixmap(QIcon(ICONE).pixmap(40, 40));
	topo->addWidget(ic);
	auto *nome = new QLabel("Programas do LumeBSD");
	QFont f = nome->font();
	f.setPointSizeF(f.pointSizeF() * 1.45);
	nome->setFont(f);
	topo->addWidget(nome);
	topo->addStretch(1);
	busca_ = new QLineEdit;
	busca_->setPlaceholderText("Procurar (ex.: matlab, excel, word, autocad)");
	busca_->setClearButtonEnabled(true);
	busca_->setMinimumWidth(340);
	topo->addWidget(busca_);
	auto *ajuda = new QPushButton(QIcon::fromTheme("help-contents"), "Ajuda");
	ajuda->setFlat(true);
	QObject::connect(ajuda, &QPushButton::clicked, this, [] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(AJUDA));
	});
	topo->addWidget(ajuda);
	v->addLayout(topo);

	auto *linha = new QFrame;
	linha->setFrameShape(QFrame::HLine);
	linha->setFrameShadow(QFrame::Sunken);
	v->addWidget(linha);

	auto *meio = new QHBoxLayout;
	meio->setSpacing(0);
	lado_ = new QListWidget;
	lado_->setFrameShape(QFrame::NoFrame);
	lado_->setIconSize(QSize(22, 22));
	lado_->setFixedWidth(250);
	lado_->setSpacing(1);
	auto add = [this](const QString &nome, const QString &ic, const QString &dado) {
		auto *it = new QListWidgetItem(QIcon::fromTheme(ic), nome);
		it->setData(Qt::UserRole, dado);
		it->setSizeHint(QSize(0, 34));
		lado_->addItem(it);
	};
	add("Início: kits por área", "go-home", "@inicio");
	add("Instalados", "emblem-checked", "@instalados");
	auto *sep = new QListWidgetItem;
	sep->setFlags(Qt::NoItemFlags);
	sep->setSizeHint(QSize(0, 10));
	lado_->addItem(sep);
	for (const Categoria &c : cats_)
		add(c.nome, c.icone, c.id);
	meio->addWidget(lado_);

	auto *vl = new QFrame;
	vl->setFrameShape(QFrame::VLine);
	vl->setFrameShadow(QFrame::Sunken);
	meio->addWidget(vl);

	pilha_ = new QStackedWidget;
	pilha_->addWidget(montar_inicio());
	pilha_->addWidget(montar_lista());
	meio->addWidget(pilha_, 1);
	v->addLayout(meio, 1);

	// Rodapé: o progresso de uma instalação.
	rodape_ = new QFrame;
	rodape_->setFrameShape(QFrame::StyledPanel);
	auto *r = new QVBoxLayout(rodape_);
	r->setContentsMargins(20, 10, 20, 10);
	auto *rl = new QHBoxLayout;
	passo_ = new QLabel;
	barra_ = new QProgressBar;
	barra_->setTextVisible(false);
	barra_->setMaximumHeight(14);
	ver_detalhes_ = new QToolButton;
	ver_detalhes_->setText("Detalhes");
	ver_detalhes_->setCheckable(true);
	ver_detalhes_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	ver_detalhes_->setArrowType(Qt::RightArrow);
	rl->addWidget(passo_, 2);
	rl->addWidget(barra_, 3);
	rl->addWidget(ver_detalhes_);
	r->addLayout(rl);
	detalhes_ = new QPlainTextEdit;
	detalhes_->setReadOnly(true);
	detalhes_->setMaximumHeight(140);
	detalhes_->hide();
	r->addWidget(detalhes_);
	QObject::connect(ver_detalhes_, &QToolButton::toggled, this, [this](bool on) {
		detalhes_->setVisible(on);
		ver_detalhes_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
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
		mostrar_lista(QString(), QString(), QString("Resultados para “%1”").arg(t.trimmed()));
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
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(28, 22, 28, 22);
	v->setSpacing(16);

	auto *t = new QLabel("Para que você vai usar o computador?");
	QFont f = t->font();
	f.setPointSizeF(f.pointSizeF() * 1.3);
	f.setBold(true);
	t->setFont(f);
	v->addWidget(t);
	auto *s = new QLabel("Cada kit instala de uma vez os programas da sua área, em português. "
	    "Depois, é só procurar acima o programa que você usava no Windows ou no Mac "
	    "(MATLAB, Excel, AutoCAD...) para ver o que usar no lugar.");
	s->setWordWrap(true);
	v->addWidget(s);

	for (const Kit &k : kits_) {
		auto *q = new QFrame;
		q->setFrameShape(QFrame::StyledPanel);
		auto *h = new QHBoxLayout(q);
		h->setContentsMargins(18, 14, 18, 14);
		auto *ic = new QLabel;
		QString nomeic = k.id == "exatas" ? "applications-science"
		    : k.id == "sociais" ? "office-chart-line" : "applications-office";
		ic->setPixmap(QIcon::fromTheme(nomeic).pixmap(48, 48));
		ic->setAlignment(Qt::AlignTop);
		h->addWidget(ic);
		auto *tv = new QVBoxLayout;
		auto *n = new QLabel(k.nome);
		QFont fn = n->font();
		fn.setPointSizeF(fn.pointSizeF() * 1.15);
		fn.setBold(true);
		n->setFont(fn);
		auto *d = new QLabel(k.descricao);
		d->setWordWrap(true);
		QStringList nomes;
		for (const QString &id : programas_do_kit(k.id))
			nomes << progs_[por_id_[id]].nome;
		auto *lst = new QLabel(nomes.join(" · "));
		lst->setWordWrap(true);
		lst->setStyleSheet("color: palette(placeholder-text);");
		tv->addWidget(n);
		tv->addWidget(d);
		tv->addWidget(lst);
		h->addLayout(tv, 1);
		auto *bv = new QVBoxLayout;
		auto *marca = new QLabel("✓ Instalado");
		marca->hide();
		auto *b = new QPushButton("Instalar o kit");
		auto *ver = new QPushButton("Ver os programas");
		ver->setFlat(true);
		bv->addWidget(marca, 0, Qt::AlignRight);
		bv->addWidget(b);
		bv->addWidget(ver);
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

	auto *dica = new QLabel("<b>Programas do Windows?</b> Muitos rodam com o Wine (em "
	    "<i>Sistema e compatibilidade</i>): depois de instalado, dois cliques no "
	    ".exe. <b>Office, Teams, Zoom?</b> Estão em <i>Na web</i>, como apps no menu.");
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
	auto *fv = new QVBoxLayout(fora);
	fv->setContentsMargins(0, 0, 0, 0);
	titulo_lista_ = new QLabel;
	QFont f = titulo_lista_->font();
	f.setPointSizeF(f.pointSizeF() * 1.3);
	f.setBold(true);
	titulo_lista_->setFont(f);
	titulo_lista_->setContentsMargins(28, 18, 28, 4);
	fv->addWidget(titulo_lista_);

	auto *rolar = new QScrollArea;
	rolar->setWidgetResizable(true);
	rolar->setFrameShape(QFrame::NoFrame);
	lista_ = new QWidget;
	lista_v_ = new QVBoxLayout(lista_);
	lista_v_->setContentsMargins(28, 8, 28, 22);
	lista_v_->setSpacing(10);

	for (const Programa &p : progs_) {
		Cartao c;
		c.quadro = new QFrame;
		c.quadro->setFrameShape(QFrame::StyledPanel);
		auto *h = new QHBoxLayout(c.quadro);
		h->setContentsMargins(14, 10, 14, 10);
		h->setSpacing(14);
		QString reserva;
		for (const Categoria &k : cats_)
			if (k.id == p.categoria)
				reserva = k.icone;
		auto *ic = new QLabel;
		ic->setPixmap(icone(p.icone, reserva).pixmap(44, 44));
		ic->setAlignment(Qt::AlignTop);
		h->addWidget(ic);
		auto *tv = new QVBoxLayout;
		tv->setSpacing(2);
		auto *topo = new QHBoxLayout;
		c.nome = new QLabel(QString("<b>%1</b>%2").arg(p.nome.toHtmlEscaped(),
		    p.web ? "  <span style='color: palette(placeholder-text)'>· na web</span>" : ""));
		c.marca = new QLabel("✓ instalado");
		c.marca->setStyleSheet("color: palette(placeholder-text);");
		topo->addWidget(c.nome);
		topo->addWidget(c.marca);
		topo->addStretch(1);
		tv->addLayout(topo);
		c.parecido = new QLabel(p.parecido.isEmpty() ? QString()
		    : "No lugar de: " + p.parecido);
		c.parecido->setStyleSheet("color: palette(placeholder-text);");
		c.parecido->setVisible(!p.parecido.isEmpty());
		tv->addWidget(c.parecido);
		c.descricao = new QLabel(p.descricao);
		c.descricao->setWordWrap(true);
		tv->addWidget(c.descricao);
		h->addLayout(tv, 1);
		auto *bv = new QVBoxLayout;
		c.botao = new QPushButton;
		c.botao->setMinimumWidth(110);
		c.remover = new QPushButton("Remover");
		c.remover->setFlat(true);
		bv->addWidget(c.botao);
		bv->addWidget(c.remover);
		bv->addStretch(1);
		h->addLayout(bv);
		QString id = p.id;
		QObject::connect(c.botao, &QPushButton::clicked, this, [this, id] {
			if (instalados_.contains(id))
				QProcess::startDetached(LOJA, {"abrir", id});
			else
				instalar(id);
		});
		QObject::connect(c.remover, &QPushButton::clicked, this, [this, id] { remover(id); });
		lista_v_->addWidget(c.quadro);
		cartoes_[p.id] = c;
	}
	vazio_ = new QLabel("Nada por aqui.");
	vazio_->setWordWrap(true);
	vazio_->setStyleSheet("color: palette(placeholder-text);");
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
		if (!q.isEmpty())
			ver = simples(p.nome + " " + p.parecido + " " + p.descricao + " " + p.id).contains(q);
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
		    "usava, ou o que quer fazer (planilha, vídeo, circuito...).").arg(busca_->text().trimmed()));
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
		c.botao->setEnabled(!ocupado_ && !(tem && !p.abre));
		c.remover->setEnabled(!ocupado_);
	}
	for (const Kit &k : kits_) {
		bool tudo = true;
		for (const QString &id : programas_do_kit(k.id))
			tudo &= instalados_.contains(id);
		marcas_kit_[k.id]->setVisible(tudo);
		botoes_kit_[k.id]->setText(tudo ? "Instalado" : "Instalar o kit");
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
	p->start(LOJA, {"--maquina", "estado"});
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
	p->start(LOJA, QStringList{"--maquina", "tamanho"} + ids);
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
	p->start(root ? "pkexec" : LOJA, a);
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

	QString instalar;
	QStringList a = app.arguments();
	int i = a.indexOf("--instalar");
	if (i >= 0 && i + 1 < a.size())
		instalar = a[i + 1];

	Janela j(instalar);
	j.show();
	return app.exec();
}

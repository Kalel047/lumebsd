// SPDX-License-Identifier: BSD-2-Clause
//
// lume-atualizacoes - "Atualizações do LumeBSD": o app que mantém o sistema
// em dia, para quem não quer decorar comandos.
//
//   lume-atualizacoes            a janela: procura, mostra as novidades,
//                                instala, pede para reiniciar, volta
//   lume-atualizacoes --bandeja  na sessão (autostart): procura de tempos em
//                                tempos e só aparece na bandeja quando há
//                                uma versão nova ou uma atualização falhou
//
// Todo o trabalho é do lume-atualizar(8), chamado com --maquina (linhas
// "@chave valor"). O que precisa de root passa pelo pkexec (polkit: a ação
// org.lumebsd.atualizar pede a senha de um administrador, o grupo wheel).
//
// Sem moc: só sinais do próprio Qt ligados a lambdas, para compilar com o
// clang de fora do FreeBSD contra os cabeçalhos do pacote qt6-base.

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLibraryInfo>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSessionManager>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <memory>

static const char *ATUALIZAR = "/usr/sbin/lume-atualizar";
static const char *ICONE = "/usr/share/lume/app/lume-atualizacoes.svg";

// "@chave a=1 b=dois tres" -> {a: "1", b: "dois tres"}; o texto depois da
// chave fica em "".
struct Linha {
	QString chave;
	QMap<QString, QString> v;
};

static Linha
ler(const QString &s)
{
	Linha l;
	QString resto = s.mid(1);
	int esp = resto.indexOf(' ');
	l.chave = esp < 0 ? resto : resto.left(esp);
	resto = esp < 0 ? QString() : resto.mid(esp + 1);
	l.v[""] = resto;
	static const QRegularExpression par("(?:^| )([a-z_]+)=");
	auto it = par.globalMatch(resto);
	QList<QRegularExpressionMatch> ms;
	while (it.hasNext())
		ms << it.next();
	for (int i = 0; i < ms.size(); i++) {
		int ini = ms[i].capturedEnd(0);
		int fim = i + 1 < ms.size() ? ms[i + 1].capturedStart(0) : resto.size();
		l.v[ms[i].captured(1)] = resto.mid(ini, fim - ini);
	}
	return l;
}

static QString
tamanho(qint64 b)
{
	if (b >= 1024LL * 1024 * 1024)
		return QString::number(b / 1073741824.0, 'f', 1).replace('.', ',') + " GB";
	return QString::number(b / 1048576) + " MB";
}

// Pergunta com botões que dizem o que fazem ("Reiniciar agora", "Cancelar").
static bool
perguntar(QWidget *pai, const QString &titulo, const QString &texto,
    const QString &sim)
{
	QMessageBox m(QMessageBox::Question, titulo, texto, QMessageBox::NoButton, pai);
	QPushButton *s = m.addButton(sim, QMessageBox::AcceptRole);
	m.addButton("Cancelar", QMessageBox::RejectRole);
	m.setDefaultButton(s);
	m.exec();
	return m.clickedButton() == s;
}

static QString
nome_tipo(const QString &t)
{
	if (t == "correcao")
		return "Correção";
	if (t == "principal")
		return "Versão principal";
	return "Versão nova";
}

class Janela : public QWidget {
public:
	explicit Janela(bool bandeja);

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	enum Estado { Procurando, EmDia, Nova, Instalando, Pronto, EmTeste,
		Falhou, Erro, AoVivo };

	void montar();
	void mostrar(Estado e);
	void estado();
	void procurar();
	void instalar();
	void voltar();
	void descartar();
	void reiniciar();
	void notas(const QString &url);
	void avisar_bandeja();
	QProcess *rodar(const QStringList &args, bool root,
	    std::function<void(const Linha &)> cada,
	    std::function<void(int)> fim);

	bool bandeja_;
	QSystemTrayIcon *tray_ = nullptr;

	QLabel *titulo_, *sub_, *icone_, *manchete_, *texto_, *passo_;
	QProgressBar *barra_;
	QTextBrowser *notas_;
	QPlainTextEdit *detalhes_;
	QToolButton *ver_detalhes_;
	QPushButton *principal_, *secundario_;
	QPushButton *botao_voltar_;
	QFrame *caixa_;

	// O que se sabe do sistema e da versão nova.
	QString versao_, base_, ambiente_, anterior_, pendente_, reiniciar_be_;
	QString nova_, nova_base_, tipo_, troca_, notas_url_, falhou_be_,
	    falhou_motivo_, erro_;
	qint64 nova_tamanho_ = 0;
	bool ocupado_ = false;
	Estado est_ = Procurando;
};

Janela::Janela(bool bandeja) : bandeja_(bandeja)
{
	setWindowTitle("Atualizações do LumeBSD");
	setWindowIcon(QIcon::fromTheme("system-software-update", QIcon(ICONE)));
	resize(640, 560);
	montar();
	estado();
	if (bandeja_) {
		// Na entrada da sessão, espera a rede subir.
		mostrar(Procurando);
		QTimer::singleShot(90 * 1000, this, [this] { procurar(); });
	} else {
		procurar();
	}
	if (bandeja_) {
		// Procura de novo a cada 6 horas.
		auto *t = new QTimer(this);
		QObject::connect(t, &QTimer::timeout, this, [this] {
			if (!ocupado_)
				procurar();
		});
		t->start(6 * 3600 * 1000);
	}
}

void
Janela::montar()
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(28, 24, 28, 20);
	v->setSpacing(14);

	auto *topo = new QHBoxLayout;
	icone_ = new QLabel;
	icone_->setPixmap(QIcon(ICONE).pixmap(64, 64));
	topo->addWidget(icone_);
	auto *nomes = new QVBoxLayout;
	titulo_ = new QLabel("LumeBSD");
	QFont f = titulo_->font();
	f.setPointSizeF(f.pointSizeF() * 1.7);
	titulo_->setFont(f);
	sub_ = new QLabel;
	sub_->setStyleSheet("color: palette(placeholder-text);");
	nomes->addWidget(titulo_);
	nomes->addWidget(sub_);
	topo->addLayout(nomes, 1);
	v->addLayout(topo);

	caixa_ = new QFrame;
	caixa_->setFrameShape(QFrame::StyledPanel);
	auto *c = new QVBoxLayout(caixa_);
	c->setContentsMargins(18, 16, 18, 16);
	manchete_ = new QLabel;
	QFont fm = manchete_->font();
	fm.setPointSizeF(fm.pointSizeF() * 1.25);
	fm.setBold(true);
	manchete_->setFont(fm);
	manchete_->setWordWrap(true);
	texto_ = new QLabel;
	texto_->setWordWrap(true);
	texto_->setTextFormat(Qt::RichText);
	passo_ = new QLabel;
	barra_ = new QProgressBar;
	barra_->setTextVisible(false);
	notas_ = new QTextBrowser;
	notas_->setOpenExternalLinks(true);
	notas_->setMinimumHeight(140);
	c->addWidget(manchete_);
	c->addWidget(texto_);
	c->addWidget(passo_);
	c->addWidget(barra_);
	c->addWidget(notas_, 100);
	// Sem as novidades, o espaço sobra embaixo, não entre as linhas.
	c->addStretch(1);
	v->addWidget(caixa_, 1);

	ver_detalhes_ = new QToolButton;
	ver_detalhes_->setText("Detalhes");
	ver_detalhes_->setCheckable(true);
	ver_detalhes_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	ver_detalhes_->setArrowType(Qt::RightArrow);
	detalhes_ = new QPlainTextEdit;
	detalhes_->setReadOnly(true);
	detalhes_->setMaximumHeight(130);
	detalhes_->hide();
	QObject::connect(ver_detalhes_, &QToolButton::toggled, this, [this](bool on) {
		detalhes_->setVisible(on);
		ver_detalhes_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
	});
	v->addWidget(ver_detalhes_, 0, Qt::AlignLeft);
	v->addWidget(detalhes_);

	auto *baixo = new QHBoxLayout;
	botao_voltar_ = new QPushButton;
	botao_voltar_->setFlat(true);
	QObject::connect(botao_voltar_, &QPushButton::clicked, this, [this] { voltar(); });
	baixo->addWidget(botao_voltar_);
	baixo->addStretch(1);
	secundario_ = new QPushButton;
	principal_ = new QPushButton;
	principal_->setDefault(true);
	principal_->setAutoDefault(true);
	baixo->addWidget(secundario_);
	baixo->addWidget(principal_);
	v->addLayout(baixo);

	QObject::connect(principal_, &QPushButton::clicked, this, [this] {
		switch (est_) {
		case EmDia: case Erro: procurar(); break;
		case Nova: instalar(); break;
		case Pronto: reiniciar(); break;
		case Falhou: procurar(); break;
		default: break;
		}
	});
	QObject::connect(secundario_, &QPushButton::clicked, this, [this] {
		switch (est_) {
		case Falhou: descartar(); break;
		default: close(); break;
		}
	});
}

void
Janela::mostrar(Estado e)
{
	est_ = e;
	titulo_->setText(QString("LumeBSD %1 <i>“Farol”</i>").arg(versao_));
	QString s = QString("base FreeBSD %1").arg(base_);
	if (!ambiente_.isEmpty() && ambiente_ != "-")
		s += QString("  ·  ambiente de boot %1").arg(ambiente_);
	sub_->setText(s);

	passo_->hide();
	barra_->hide();
	notas_->hide();
	principal_->show();
	secundario_->show();
	principal_->setEnabled(true);
	secundario_->setText("Fechar");

	switch (e) {
	case Procurando:
		manchete_->setText("Procurando atualizações…");
		texto_->setText("");
		barra_->setRange(0, 0);
		barra_->show();
		principal_->hide();
		break;
	case EmDia:
		manchete_->setText("✓  Seu sistema está em dia.");
		texto_->setText(QString("Você tem a versão mais nova do LumeBSD. "
		    "Procurado às %1.").arg(QDateTime::currentDateTime().toString("HH:mm")));
		principal_->setText("Procurar de novo");
		break;
	case Nova: {
		manchete_->setText(QString("%1: LumeBSD %2").arg(nome_tipo(tipo_), nova_));
		QString t = "A versão nova é instalada à parte; a de agora fica "
		    "guardada. Se algo der errado no primeiro boot, o computador "
		    "volta sozinho para ela.";
		if (troca_ == "drivers")
			t += QString("<br><br><b>Troca a base para o FreeBSD %1:</b> os "
			    "drivers de vídeo e Wi-Fi são baixados de novo para ela.")
			    .arg(nova_base_);
		else if (troca_ == "programas")
			t += QString("<br><br><b>Troca a base para o FreeBSD %1:</b> todos "
			    "os programas são baixados de novo (alguns GB; leva um bom "
			    "tempo). Deixe o computador na tomada.").arg(nova_base_);
		if (nova_tamanho_ > 0)
			t += QString("<br><br>Download: %1.").arg(tamanho(nova_tamanho_));
		texto_->setText(t);
		if (!notas_url_.isEmpty() && notas_url_ != "-") {
			notas_->show();
			notas(notas_url_);
		}
		principal_->setText("Instalar");
		secundario_->setText("Agora não");
		break;
	}
	case Instalando:
		manchete_->setText(QString("Instalando o LumeBSD %1…").arg(nova_));
		texto_->setText("Pode continuar usando o computador.");
		passo_->show();
		barra_->show();
		principal_->hide();
		secundario_->hide();
		break;
	case Pronto:
		manchete_->setText("✓  Pronto. Falta reiniciar.");
		texto_->setText(QString("O LumeBSD %1 está instalado e entra no próximo "
		    "boot. Se ele não subir direito, o computador volta sozinho "
		    "para a versão de agora; se subir, a atualização é confirmada "
		    "no fim do boot.%2").arg(nova_,
		    troca_ != "nenhuma" && !troca_.isEmpty()
		    ? "<br><br>No primeiro boot os programas baixados são "
		      "instalados: leva alguns minutos." : ""));
		principal_->setText("Reiniciar agora");
		secundario_->setText("Depois");
		break;
	case EmTeste:
		manchete_->setText(QString("A versão %1 está em teste.").arg(pendente_));
		texto_->setText("O sistema está sendo conferido e a atualização é "
		    "confirmada sozinha em instantes. Se algo estiver errado, "
		    "reinicie: o computador volta para a versão anterior.");
		principal_->hide();
		break;
	case Falhou:
		manchete_->setText("A atualização não deu certo.");
		texto_->setText(QString("O computador voltou sozinho para a versão "
		    "%1, e nada foi perdido.<br><br>Motivo: %2<br><br>Você pode "
		    "tentar de novo mais tarde ou apagar a tentativa (ela ocupa "
		    "espaço no disco).").arg(versao_, falhou_motivo_.toHtmlEscaped()));
		principal_->setText("Procurar de novo");
		secundario_->setText("Apagar a tentativa");
		break;
	case Erro:
		manchete_->setText("Não deu certo.");
		texto_->setText(erro_.toHtmlEscaped());
		principal_->setText("Tentar de novo");
		break;
	case AoVivo:
		manchete_->setText("Este é o pendrive ao vivo.");
		texto_->setText("Atualizações são para o LumeBSD instalado. Para "
		    "instalar, abra o Konsole e rode <b>lume instalar</b>.");
		principal_->hide();
		break;
	}
	// O teclado vai para a ação principal (Espaço ou Enter aciona).
	if (principal_->isVisibleTo(this))
		principal_->setFocus();
	bool pode_voltar = !anterior_.isEmpty() && anterior_ != "-" &&
	    (e == EmDia || e == Nova || e == Erro);
	botao_voltar_->setVisible(pode_voltar);
	botao_voltar_->setText(QString("Voltar para a versão anterior (%1)…").arg(anterior_));
}

QProcess *
Janela::rodar(const QStringList &args, bool root,
    std::function<void(const Linha &)> cada, std::function<void(int)> fim)
{
	auto *p = new QProcess(this);
	QStringList a;
	QString prog;
	if (root) {
		prog = "pkexec";
		a << ATUALIZAR;
	} else {
		prog = ATUALIZAR;
	}
	a << "--maquina" << args;
	auto *buf = new QString;
	QObject::connect(p, &QProcess::readyReadStandardOutput, this, [=] {
		*buf += QString::fromUtf8(p->readAllStandardOutput());
		int i;
		while ((i = buf->indexOf('\n')) >= 0) {
			QString l = buf->left(i).trimmed();
			buf->remove(0, i + 1);
			if (l.startsWith('@'))
				cada(ler(l));
			else if (!l.isEmpty())
				detalhes_->appendPlainText(l);
		}
	});
	QObject::connect(p, &QProcess::readyReadStandardError, this, [=] {
		detalhes_->appendPlainText(QString::fromUtf8(p->readAllStandardError()).trimmed());
	});
	QObject::connect(p, &QProcess::finished, this, [=](int code, QProcess::ExitStatus) {
		delete buf;
		p->deleteLater();
		fim(code);
	});
	QObject::connect(p, &QProcess::errorOccurred, this, [=](QProcess::ProcessError err) {
		if (err == QProcess::FailedToStart) {
			delete buf;
			p->deleteLater();
			fim(-1);
		}
	});
	p->start(prog, a);
	return p;
}

void
Janela::estado()
{
	// Síncrono e rápido: versão, base, ambientes, o que está em teste.
	QProcess p;
	p.start(ATUALIZAR, {"--maquina", "estado"});
	p.waitForFinished(15000);
	falhou_be_.clear();
	reiniciar_be_.clear();
	for (const QString &s : QString::fromUtf8(p.readAllStandardOutput()).split('\n')) {
		if (!s.startsWith('@'))
			continue;
		Linha l = ler(s);
		if (l.chave == "estado") {
			versao_ = l.v["versao"];
			base_ = l.v["base"];
			ambiente_ = l.v["ambiente"];
			anterior_ = l.v["anterior"];
			pendente_ = l.v["pendente"];
			if (l.v["ao_vivo"] == "sim")
				est_ = AoVivo;
		} else if (l.chave == "reiniciar") {
			reiniciar_be_ = l.v["ambiente"];
		} else if (l.chave == "falhou") {
			falhou_be_ = l.v["ambiente"];
			falhou_motivo_ = l.v["motivo"];
		}
	}
}

void
Janela::procurar()
{
	estado();
	if (est_ == AoVivo) {
		mostrar(AoVivo);
		return;
	}
	if (!pendente_.isEmpty() && pendente_ != "-") {
		mostrar(EmTeste);
		return;
	}
	if (!reiniciar_be_.isEmpty()) {
		// Já instalada (talvez por outra janela ou pelo terminal).
		nova_ = reiniciar_be_.startsWith("lume-") ? reiniciar_be_.mid(5) : reiniciar_be_;
		mostrar(Pronto);
		return;
	}
	if (!falhou_be_.isEmpty()) {
		mostrar(Falhou);
		avisar_bandeja();
		return;
	}
	ocupado_ = true;
	mostrar(Procurando);
	nova_.clear();
	erro_.clear();
	rodar({"verificar"}, false, [this](const Linha &l) {
		if (l.chave == "nova") {
			nova_ = l.v["versao"];
			nova_base_ = l.v["base"];
			tipo_ = l.v["tipo"];
			troca_ = l.v["troca"];
			nova_tamanho_ = l.v["tamanho"].toLongLong();
			notas_url_ = l.v["notas"];
		} else if (l.chave == "erro") {
			erro_ = l.v[""];
		}
	}, [this](int) {
		ocupado_ = false;
		if (!nova_.isEmpty()) {
			mostrar(Nova);
			avisar_bandeja();
		} else if (!erro_.isEmpty()) {
			mostrar(Erro);
		} else {
			mostrar(EmDia);
			if (tray_)
				tray_->hide();
		}
	});
}

void
Janela::notas(const QString &url)
{
	notas_->setPlainText("Carregando as novidades…");
	auto *p = new QProcess(this);
	QObject::connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) {
		QString md = QString::fromUtf8(p->readAllStandardOutput());
		if (code == 0 && !md.isEmpty())
			notas_->setMarkdown(md);
		else
			notas_->hide();
		p->deleteLater();
	});
	p->start("fetch", {"-q", "-o", "-", url});
}

void
Janela::instalar()
{
	ocupado_ = true;
	mostrar(Instalando);
	passo_->setText("Pedindo permissão…");
	barra_->setRange(0, 0);
	erro_.clear();
	auto ok = std::make_shared<bool>(false);
	rodar({"aplicar"}, true, [this, ok](const Linha &l) {
		if (l.chave == "passo") {
			QStringList p = l.v[""].split(' ');
			if (p.size() >= 3)
				passo_->setText(QString("Passo %1 de %2: %3").arg(p[0], p[1],
				    p.mid(2).join(' ')));
			barra_->setRange(0, 0);
		} else if (l.chave == "progresso") {
			barra_->setRange(0, 100);
			barra_->setValue(l.v[""].toInt());
		} else if (l.chave == "pronto") {
			*ok = true;
		} else if (l.chave == "erro") {
			erro_ = l.v[""];
		}
		if (l.chave == "msg" || l.chave == "aviso" || l.chave == "ok" ||
		    l.chave == "erro" || l.chave == "config")
			detalhes_->appendPlainText(l.v[""]);
	}, [this, ok](int code) {
		ocupado_ = false;
		if (*ok && code == 0) {
			mostrar(Pronto);
		} else if (code == 126 || code == 127) {
			// pkexec: a senha não foi dada (126) ou não autorizado (127).
			procurar();
		} else {
			if (erro_.isEmpty())
				erro_ = "A atualização parou no meio. Nada mudou no sistema "
				    "em uso. Veja os detalhes.";
			mostrar(Erro);
			ver_detalhes_->setChecked(true);
		}
	});
}

void
Janela::voltar()
{
	if (!perguntar(this, "Voltar para a versão anterior",
	    QString("O sistema volta a ser o %1 no próximo boot. Suas pastas e "
	    "arquivos pessoais não mudam.").arg(anterior_), "Voltar"))
		return;
	ocupado_ = true;
	rodar({"voltar"}, true, [this](const Linha &l) {
		if (l.chave == "erro")
			erro_ = l.v[""];
	}, [this](int code) {
		ocupado_ = false;
		if (code == 0) {
			nova_ = anterior_;
			troca_.clear();
			mostrar(Pronto);
			manchete_->setText("✓  Pronto. Falta reiniciar.");
			texto_->setText(QString("No próximo boot o sistema volta a ser o %1.")
			    .arg(anterior_));
		} else if (code != 126 && code != 127) {
			mostrar(Erro);
		}
	});
}

void
Janela::descartar()
{
	rodar({"descartar"}, true, [](const Linha &) {}, [this](int) { procurar(); });
}

void
Janela::reiniciar()
{
	// No FreeBSD a sessão do KDE não reinicia o computador sozinha; o
	// lume-atualizar reinicia, com a permissão que a instalação já pediu.
	if (!perguntar(this, "Reiniciar",
	    "Salve o que estiver aberto: os programas são fechados e o "
	    "computador reinicia agora.", "Reiniciar agora"))
		return;
	rodar({"reiniciar"}, true, [](const Linha &) {}, [](int) {});
}

void
Janela::avisar_bandeja()
{
	if (!bandeja_ || isVisible())
		return;
	if (!tray_) {
		tray_ = new QSystemTrayIcon(QIcon(ICONE), this);
		tray_->setToolTip("Atualizações do LumeBSD");
		auto *menu = new QMenu(this);
		menu->addAction("Abrir", this, [this] { show(); raise(); activateWindow(); });
		menu->addAction("Lembrar mais tarde", this, [this] { tray_->hide(); });
		tray_->setContextMenu(menu);
		QObject::connect(tray_, &QSystemTrayIcon::activated, this,
		    [this](QSystemTrayIcon::ActivationReason) { show(); raise(); activateWindow(); });
		QObject::connect(tray_, &QSystemTrayIcon::messageClicked, this,
		    [this] { show(); raise(); activateWindow(); });
	}
	tray_->show();
	if (est_ == Falhou)
		tray_->showMessage("A atualização não deu certo",
		    "O computador voltou sozinho para a versão anterior. Clique para ver.",
		    QIcon(ICONE), 15000);
	else
		tray_->showMessage(QString("LumeBSD %1 disponível").arg(nova_),
		    QString("%1. Clique para ver as novidades e instalar.").arg(nome_tipo(tipo_)),
		    QIcon(ICONE), 15000);
}

void
Janela::closeEvent(QCloseEvent *e)
{
	if (ocupado_ && est_ == Instalando) {
		QMessageBox::information(this, "Instalando",
		    "Espere a instalação terminar; o sistema em uso não está sendo "
		    "mexido, mas a versão nova ainda está sendo preparada.");
		e->ignore();
		return;
	}
	if (bandeja_) {
		// Continua na sessão, procurando de tempos em tempos.
		hide();
		e->ignore();
		return;
	}
	e->accept();
}

int
main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	// Os textos do próprio Qt (OK, Cancelar...) no idioma do sistema.
	QTranslator qt;
	if (qt.load(QLocale(), "qtbase", "_",
	    QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qt);
	app.setApplicationName("lume-atualizacoes");
	app.setApplicationDisplayName("Atualizações do LumeBSD");
	app.setDesktopFileName("lume-atualizacoes");
	bool bandeja = app.arguments().contains("--bandeja");
	// Fora da sessão salva do KDE: o aviso já entra pelo autostart, e uma
	// janela de atualização restaurada no login estaria velha.
	QObject::connect(&app, &QGuiApplication::saveStateRequest, &app,
	    [](QSessionManager &m) { m.setRestartHint(QSessionManager::RestartNever); });
	app.setQuitOnLastWindowClosed(!bandeja);

	Janela j(bandeja);
	if (!bandeja)
		j.show();
	return app.exec();
}

// SPDX-License-Identifier: BSD-2-Clause
//
// lume-atributos - os atributos dos arquivos, na tela, como no Tracker do
// BeOS: ver e editar as etiquetas, a nota, o comentário e os atributos
// próprios de um arquivo, e procurar por eles, com a consulta salva num
// arquivo (.consulta) que abre de novo com dois cliques.
//
//   lume-atributos --editar ARQUIVO...   a janela de atributos (o botão
//                                        direito do Dolphin)
//   lume-atributos [--pasta PASTA]       a janela de procura
//   lume-atributos ARQUIVO.consulta      abre a consulta salva e procura
//
// Os atributos ficam no próprio arquivo (extattr, espaço "user"), com os
// nomes do KDE para os conhecidos; a procura é a do lume-atributo(1), ao
// vivo: a lista se atualiza sozinha quando um arquivo muda.
//
// Sem moc: só sinais do próprio Qt ligados a lambdas, para compilar com o
// clang de fora do FreeBSD contra os cabeçalhos do pacote qt6-base.

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <sys/types.h>
#include <sys/extattr.h>
#include <errno.h>

#include <cstdio>
#include <cstring>

namespace {

const int NS = EXTATTR_NAMESPACE_USER;
const char *const PROCURAR = "/usr/bin/lume-atributo";

// Os nomes conhecidos (os do KDE, que o Dolphin também mostra).
const char *const ETIQUETAS = "xdg.tags";
const char *const COMENTARIO = "xdg.comment";
const char *const NOTA = "baloo.rating";
const char *const ORIGEM = "xdg.origin.url";

bool conhecido(const QString &n)
{
    return n == ETIQUETAS || n == COMENTARIO || n == NOTA || n == ORIGEM;
}

// lume.autor aparece como "autor"; um nome com ponto fica como está.
QString mostrado(const QString &real)
{
    return real.startsWith("lume.") ? real.mid(5) : real;
}

QString gravado(const QString &nome)
{
    return nome.contains('.') ? nome : "lume." + nome;
}

bool nomeValido(const QString &n)
{
    if (n.isEmpty() || n.size() > 200)
        return false;
    for (QChar c : n)
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != '.' && c != '_' && c != '-')
            return false;
    return true;
}

QMap<QString, QByteArray> lerTodos(const QString &arq)
{
    QMap<QString, QByteArray> r;
    const QByteArray c = QFile::encodeName(arq);
    char lista[8192];
    ssize_t n = extattr_list_file(c.constData(), NS, lista, sizeof(lista));
    for (ssize_t i = 0; i < n;) {
        size_t t = (unsigned char)lista[i];
        if (i + 1 + (ssize_t)t > n)
            break;
        QByteArray nome(lista + i + 1, (int)t);
        i += 1 + t;
        ssize_t tam = extattr_get_file(c.constData(), NS, nome.constData(), nullptr, 0);
        if (tam < 0)
            continue;
        QByteArray v(tam, 0);
        tam = extattr_get_file(c.constData(), NS, nome.constData(), v.data(), v.size());
        if (tam >= 0)
            r.insert(QString::fromUtf8(nome), v.left(tam));
    }
    return r;
}

bool gravar(const QString &arq, const QString &nome, const QByteArray &valor)
{
    const QByteArray c = QFile::encodeName(arq), n = nome.toUtf8();
    if (valor.isEmpty()) {
        int r = extattr_delete_file(c.constData(), NS, n.constData());
        return r == 0 || errno == ENOATTR;
    }
    return extattr_set_file(c.constData(), NS, n.constData(), valor.constData(),
                            valor.size()) >= 0;
}

QString estrelas(const QByteArray &nota)
{
    if (nota.isEmpty())
        return {};
    int v = qBound(0, nota.toInt(), 10);
    QString s;
    for (int i = 0; i < 5; i++)
        s += v >= 2 * (i + 1) ? QStringLiteral("★") : QStringLiteral("☆");
    if (v % 2)
        s += QStringLiteral(" ½");
    return s;
}

// --- A janela de atributos ------------------------------------------------

class Atributos : public QDialog
{
public:
    explicit Atributos(const QStringList &arquivos, QWidget *pai = nullptr)
        : QDialog(pai), m_arqs(arquivos)
    {
        setWindowIcon(QIcon::fromTheme("tag"));
        const QString titulo = arquivos.size() == 1
            ? QFileInfo(arquivos[0]).fileName()
            : tr("%n arquivos", nullptr, arquivos.size());
        setWindowTitle(tr("Atributos de %1").arg(titulo));

        // Os valores: os de um arquivo, ou o que for igual em todos.
        QList<QMap<QString, QByteArray>> todos;
        for (const QString &a : arquivos)
            todos << lerTodos(a);
        QSet<QString> nomes;
        for (const auto &m : todos)
            for (auto it = m.begin(); it != m.end(); ++it)
                nomes << it.key();
        for (const QString &n : nomes) {
            bool igual = true;
            for (const auto &m : todos)
                if (!m.contains(n) || m[n] != todos[0][n])
                    igual = false;
            if (igual)
                m_inicial.insert(n, todos[0][n]);
            else
                m_varios << n;
        }

        auto *topo = new QVBoxLayout(this);
        auto *cab = new QLabel("<b>" + titulo.toHtmlEscaped() + "</b>");
        topo->addWidget(cab);

        auto *form = new QFormLayout;
        m_etiquetas = new QLineEdit(QString::fromUtf8(m_inicial.value(ETIQUETAS)));
        m_etiquetas->setPlaceholderText(m_varios.contains(ETIQUETAS)
            ? tr("(vários)") : tr("trabalho, família, viagem"));
        form->addRow(tr("Etiquetas:"), m_etiquetas);

        auto *linhaNota = new QHBoxLayout;
        m_nota = m_inicial.value(NOTA).toInt();
        for (int i = 0; i < 5; i++) {
            auto *b = new QToolButton;
            b->setAutoRaise(true);
            b->setToolTip(tr("%n estrela(s)", nullptr, i + 1));
            connect(b, &QToolButton::clicked, this, [this, i] {
                // Na mesma estrela de novo: tira a nota.
                m_nota = m_nota == 2 * (i + 1) ? 0 : 2 * (i + 1);
                m_notaMudou = true;
                pintarNota();
            });
            m_botoes << b;
            linhaNota->addWidget(b);
        }
        linhaNota->addStretch();
        pintarNota();
        form->addRow(tr("Nota:"), linhaNota);

        m_comentario = new QPlainTextEdit(QString::fromUtf8(m_inicial.value(COMENTARIO)));
        m_comentario->setPlaceholderText(m_varios.contains(COMENTARIO) ? tr("(vários)") : QString());
        m_comentario->setFixedHeight(fontMetrics().lineSpacing() * 4 + 12);
        form->addRow(tr("Comentário:"), m_comentario);

        m_origem = new QLineEdit(QString::fromUtf8(m_inicial.value(ORIGEM)));
        m_origem->setPlaceholderText(m_varios.contains(ORIGEM) ? tr("(vários)") : tr("de onde veio"));
        form->addRow(tr("Origem:"), m_origem);
        topo->addLayout(form);

        // Os atributos próprios (autor, cliente, projeto...).
        topo->addWidget(new QLabel(tr("Outros atributos:")));
        m_tabela = new QTableWidget(0, 2);
        m_tabela->setHorizontalHeaderLabels({tr("Nome"), tr("Valor")});
        m_tabela->horizontalHeader()->setStretchLastSection(true);
        m_tabela->verticalHeader()->hide();
        for (auto it = m_inicial.begin(); it != m_inicial.end(); ++it)
            if (!conhecido(it.key()))
                linha(mostrado(it.key()), QString::fromUtf8(it.value()));
        for (const QString &n : m_varios)
            if (!conhecido(n))
                linha(mostrado(n), QString(), true);
        topo->addWidget(m_tabela, 1);

        auto *botoesTabela = new QHBoxLayout;
        auto *mais = new QPushButton(QIcon::fromTheme("list-add"), tr("Novo atributo"));
        auto *menos = new QPushButton(QIcon::fromTheme("list-remove"), tr("Tirar"));
        connect(mais, &QPushButton::clicked, this, [this] {
            linha(QString(), QString());
            m_tabela->setCurrentCell(m_tabela->rowCount() - 1, 0);
            m_tabela->editItem(m_tabela->item(m_tabela->rowCount() - 1, 0));
        });
        connect(menos, &QPushButton::clicked, this, [this] {
            int r = m_tabela->currentRow();
            if (r >= 0)
                m_tabela->removeRow(r);
        });
        botoesTabela->addWidget(mais);
        botoesTabela->addWidget(menos);
        botoesTabela->addStretch();
        topo->addLayout(botoesTabela);

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, [this] {
            if (salvar())
                accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        topo->addWidget(bb);
        resize(460, 520);
    }

private:
    void linha(const QString &nome, const QString &valor, bool varios = false)
    {
        int r = m_tabela->rowCount();
        m_tabela->insertRow(r);
        m_tabela->setItem(r, 0, new QTableWidgetItem(nome));
        auto *v = new QTableWidgetItem(valor);
        if (varios)
            v->setToolTip(tr("Valores diferentes nos arquivos; fica como está se não mudar."));
        m_tabela->setItem(r, 1, v);
        if (varios)
            m_tabela->item(r, 0)->setData(Qt::UserRole, true);
    }

    void pintarNota()
    {
        for (int i = 0; i < m_botoes.size(); i++)
            m_botoes[i]->setText(m_nota >= 2 * (i + 1) ? QStringLiteral("★") : QStringLiteral("☆"));
    }

    bool salvar()
    {
        // O que mudou; num arquivo só, tudo; em vários, só o que a pessoa mexeu.
        QMap<QString, QByteArray> novo;
        auto campo = [&](const char *nome, const QString &texto) {
            QByteArray v = texto.trimmed().toUtf8();
            if (v != m_inicial.value(nome) && !(v.isEmpty() && m_varios.contains(nome)))
                novo.insert(nome, v);
        };
        // As etiquetas sem espaço sobrando: "a, b" vira "a,b".
        QStringList ets;
        for (const QString &e : m_etiquetas->text().split(',', Qt::SkipEmptyParts))
            if (!e.trimmed().isEmpty())
                ets << e.trimmed();
        campo(ETIQUETAS, ets.join(','));
        campo(COMENTARIO, m_comentario->toPlainText());
        campo(ORIGEM, m_origem->text());
        if (m_notaMudou)
            novo.insert(NOTA, m_nota ? QByteArray::number(m_nota) : QByteArray());

        QSet<QString> naTabela;
        for (int r = 0; r < m_tabela->rowCount(); r++) {
            QString n = m_tabela->item(r, 0) ? m_tabela->item(r, 0)->text().trimmed() : QString();
            QString v = m_tabela->item(r, 1) ? m_tabela->item(r, 1)->text() : QString();
            if (n.isEmpty())
                continue;
            if (!nomeValido(n)) {
                QMessageBox::warning(this, windowTitle(),
                    tr("Nome inválido: %1\nUse letras sem acento, números, ponto, _ e -.").arg(n));
                return false;
            }
            QString real = gravado(n);
            if (conhecido(real)) {
                QMessageBox::warning(this, windowTitle(),
                    tr("%1 já tem um campo próprio, acima.").arg(n));
                return false;
            }
            naTabela << real;
            bool varios = m_tabela->item(r, 0)->data(Qt::UserRole).toBool();
            if (varios && v.isEmpty())
                continue;
            if (v.toUtf8() != m_inicial.value(real) || varios)
                novo.insert(real, v.toUtf8());
        }
        // Os próprios que saíram da tabela são apagados.
        QSet<QString> antes(m_varios);
        for (auto it = m_inicial.begin(); it != m_inicial.end(); ++it)
            antes << it.key();
        for (const QString &n : antes)
            if (!conhecido(n) && !naTabela.contains(n))
                novo.insert(n, QByteArray());

        QStringList falhas;
        for (const QString &a : m_arqs)
            for (auto it = novo.begin(); it != novo.end(); ++it)
                if (!gravar(a, it.key(), it.value()))
                    falhas << QString("%1 (%2): %3").arg(QFileInfo(a).fileName(),
                        mostrado(it.key()), QString::fromLocal8Bit(strerror(errno)));
        if (!falhas.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                tr("Não deu para gravar:\n%1").arg(falhas.mid(0, 8).join('\n')));
            return false;
        }
        return true;
    }

    QStringList m_arqs;
    QMap<QString, QByteArray> m_inicial;
    QSet<QString> m_varios;
    QLineEdit *m_etiquetas, *m_origem;
    QPlainTextEdit *m_comentario;
    QList<QToolButton *> m_botoes;
    int m_nota = 0;
    bool m_notaMudou = false;
    QTableWidget *m_tabela;
};

// --- A janela de procura --------------------------------------------------

class Procura : public QWidget
{
public:
    explicit Procura(const QString &pasta)
    {
        setWindowIcon(QIcon::fromTheme("tag"));
        setWindowTitle(tr("Procurar por atributos"));
        auto *topo = new QVBoxLayout(this);

        auto *form = new QFormLayout;
        auto *linhaPasta = new QHBoxLayout;
        m_pasta = new QLineEdit(pasta.isEmpty() ? QDir::homePath() : pasta);
        auto *escolher = new QPushButton(QIcon::fromTheme("folder-open"), tr("Escolher…"));
        connect(escolher, &QPushButton::clicked, this, [this] {
            QString d = QFileDialog::getExistingDirectory(this, tr("Procurar em"), m_pasta->text());
            if (!d.isEmpty())
                m_pasta->setText(d);
        });
        linhaPasta->addWidget(m_pasta, 1);
        linhaPasta->addWidget(escolher);
        form->addRow(tr("Em:"), linhaPasta);
        m_cond = new QLineEdit;
        m_cond->setPlaceholderText(tr("etiquetas=trabalho  nota>=4  cliente~acme"));
        m_cond->setToolTip(tr("Todas as condições valem juntas.\n"
                              "nome=valor  igual (nas etiquetas: tem a etiqueta)\n"
                              "nome~texto  contém\n"
                              "nome>=n  nome<=n  nome>n  nome<n  números (a nota em estrelas)\n"
                              "nome  só ter o atributo"));
        connect(m_cond, &QLineEdit::returnPressed, this, [this] { procurar(); });
        form->addRow(tr("Condições:"), m_cond);
        topo->addLayout(form);

        auto *botoes = new QHBoxLayout;
        auto *ir = new QPushButton(QIcon::fromTheme("edit-find"), tr("Procurar"));
        ir->setDefault(true);
        connect(ir, &QPushButton::clicked, this, [this] { procurar(); });
        auto *salvarB = new QPushButton(QIcon::fromTheme("document-save"), tr("Salvar consulta…"));
        connect(salvarB, &QPushButton::clicked, this, [this] { salvar(); });
        m_estado = new QLabel;
        botoes->addWidget(ir);
        botoes->addWidget(salvarB);
        botoes->addStretch();
        botoes->addWidget(m_estado);
        topo->addLayout(botoes);

        m_lista = new QTreeWidget;
        m_lista->setHeaderLabels({tr("Nome"), tr("Etiquetas"), tr("Nota"), tr("Comentário"), tr("Pasta")});
        m_lista->setRootIsDecorated(false);
        m_lista->setSortingEnabled(true);
        m_lista->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_lista->setContextMenuPolicy(Qt::CustomContextMenu);
        m_lista->header()->resizeSection(0, 220);
        m_lista->header()->resizeSection(1, 140);
        m_lista->header()->resizeSection(2, 80);
        m_lista->header()->resizeSection(3, 180);
        connect(m_lista, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *it) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(0, Qt::UserRole).toString()));
        });
        connect(m_lista, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &p) {
            QStringList sel = selecionados();
            if (sel.isEmpty())
                return;
            QMenu m;
            m.addAction(QIcon::fromTheme("document-open"), tr("Abrir"), [sel] {
                for (const QString &a : sel)
                    QDesktopServices::openUrl(QUrl::fromLocalFile(a));
            });
            m.addAction(QIcon::fromTheme("folder-open"), tr("Abrir a pasta"), [sel] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(sel[0]).absolutePath()));
            });
            m.addAction(QIcon::fromTheme("tag"), tr("Atributos…"), [this, sel] {
                Atributos d(sel, this);
                d.exec();  // a lista ao vivo já se atualiza
            });
            m.exec(m_lista->viewport()->mapToGlobal(p));
        });
        topo->addWidget(m_lista, 1);
        resize(820, 520);

        // Ao vivo (lume-atributo procurar -v): uma linha por mudança.
        connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] { ler(); });
        connect(&m_proc, &QProcess::finished, this, [this](int codigo, QProcess::ExitStatus st) {
            if (codigo == 2) {
                m_estado->setText(tr("Condição inválida: %1")
                    .arg(QString::fromLocal8Bit(m_proc.readAllStandardError()).trimmed()
                         .section(": ", 1)));
            } else if (st == QProcess::CrashExit || codigo != 0) {
                m_vivo = false;
                contar();
            }
        });
    }

    // Uma consulta salva: "pasta=" e "condicoes=", uma por linha.
    bool abrir(const QString &arq)
    {
        QFile f(arq);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        while (!f.atEnd()) {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if (l.startsWith("pasta="))
                m_pasta->setText(expandir(l.mid(6)));
            else if (l.startsWith("condicoes="))
                m_cond->setText(l.mid(10));
        }
        setWindowTitle(tr("%1 — Procurar por atributos").arg(QFileInfo(arq).completeBaseName()));
        m_arquivo = arq;
        return true;
    }

    void procurar()
    {
        const QStringList conds = QProcess::splitCommand(m_cond->text());
        if (conds.isEmpty()) {
            m_estado->setText(tr("Escreva uma condição."));
            return;
        }
        if (m_proc.state() != QProcess::NotRunning)
            m_proc.kill(), m_proc.waitForFinished(1000);
        m_lista->clear();
        m_itens.clear();
        m_resto.clear();
        m_pronto = false;
        m_vivo = true;
        m_estado->setText(tr("Procurando…"));
        m_proc.start(PROCURAR, QStringList{"procurar", "-v", "-d", expandir(m_pasta->text())} + conds);
    }

private:
    static QString expandir(QString p)
    {
        if (p == "~" || p.startsWith("~/"))
            p = QDir::homePath() + p.mid(1);
        return p;
    }

    QStringList selecionados() const
    {
        QStringList r;
        for (QTreeWidgetItem *it : m_lista->selectedItems())
            r << it->data(0, Qt::UserRole).toString();
        return r;
    }

    // As linhas do lume-atributo: "+ arq" entrou, "~ arq" mudou, "- arq"
    // saiu, "." fim da primeira passada, "*" recomeço.
    void ler()
    {
        m_resto += m_proc.readAllStandardOutput();
        int fim;
        bool mexeu = false;
        m_lista->setSortingEnabled(false);
        while ((fim = m_resto.indexOf('\n')) >= 0) {
            const QByteArray l = m_resto.left(fim);
            m_resto.remove(0, fim + 1);
            if (l == ".") {
                m_pronto = true;
            } else if (l.startsWith("*")) {
                m_lista->clear();
                m_itens.clear();
            } else if (l.size() > 2 && (l[0] == '+' || l[0] == '~')) {
                item(QFile::decodeName(l.mid(2)));
            } else if (l.size() > 2 && l[0] == '-') {
                delete m_itens.take(QFile::decodeName(l.mid(2)));
            }
            mexeu = true;
        }
        m_lista->setSortingEnabled(true);
        if (mexeu)
            contar();
    }

    void item(const QString &arq)
    {
        QTreeWidgetItem *it = m_itens.value(arq);
        if (!it) {
            it = new QTreeWidgetItem(m_lista);
            m_itens.insert(arq, it);
        }
        const auto at = lerTodos(arq);
        QFileInfo fi(arq);
        it->setText(0, fi.fileName());
        it->setIcon(0, QIcon::fromTheme(fi.isDir() ? "folder" : "text-x-generic"));
        it->setText(1, QString::fromUtf8(at.value(ETIQUETAS)).replace(',', ", "));
        it->setText(2, estrelas(at.value(NOTA)));
        it->setText(3, QString::fromUtf8(at.value(COMENTARIO)).simplified());
        const QString dir = fi.absolutePath(), casa = QDir::homePath();
        it->setText(4, dir == casa ? "~" : dir.startsWith(casa + "/") ? "~" + dir.mid(casa.size()) : dir);
        it->setData(0, Qt::UserRole, arq);
        it->setToolTip(0, arq);
    }

    void contar()
    {
        const int n = m_itens.size();
        if (!m_pronto) {
            m_estado->setText(tr("Procurando… %n arquivo(s)", nullptr, n));
            return;
        }
        QString s = n == 0 ? tr("Nada encontrado.") : tr("%n arquivo(s)", nullptr, n);
        if (m_vivo)
            s += tr(" · ao vivo");
        m_estado->setText(s);
        m_estado->setToolTip(m_vivo ? tr("A lista se atualiza sozinha quando um arquivo "
                                         "ganha, perde ou muda um atributo.") : QString());
    }

    void salvar()
    {
        QString base = m_arquivo.isEmpty()
            ? QDir::homePath() + "/" + tr("Consultas") + "/" + tr("Nova consulta") + ".consulta"
            : m_arquivo;
        QDir().mkpath(QFileInfo(base).absolutePath());
        QString arq = QFileDialog::getSaveFileName(this, tr("Salvar consulta"), base,
                                                   tr("Consultas (*.consulta)"));
        if (arq.isEmpty())
            return;
        if (!arq.endsWith(".consulta"))
            arq += ".consulta";
        QFile f(arq);
        QString pasta = m_pasta->text();
        if (pasta.startsWith(QDir::homePath()))
            pasta = "~" + pasta.mid(QDir::homePath().size());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) ||
            f.write(QString("# Consulta do LumeBSD (lume-atributos)\npasta=%1\ncondicoes=%2\n")
                        .arg(pasta, m_cond->text()).toUtf8()) < 0) {
            QMessageBox::warning(this, windowTitle(), tr("Não deu para salvar %1.").arg(arq));
            return;
        }
        m_arquivo = arq;
        setWindowTitle(tr("%1 — Procurar por atributos").arg(QFileInfo(arq).completeBaseName()));
    }

    QLineEdit *m_pasta, *m_cond;
    QLabel *m_estado;
    QTreeWidget *m_lista;
    QProcess m_proc;
    QString m_arquivo;
    QHash<QString, QTreeWidgetItem *> m_itens;
    QByteArray m_resto;
    bool m_pronto = false, m_vivo = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("lume-atributos");
    app.setDesktopFileName("lume-atributos");
    QTranslator qt;
    if (qt.load(QLocale(), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qt);

    QStringList args = app.arguments().mid(1);
    if (!args.isEmpty() && args[0] == "--editar") {
        args.removeFirst();
        if (args.isEmpty()) {
            fprintf(stderr, "uso: lume-atributos --editar ARQUIVO...\n");
            return 2;
        }
        for (QString &a : args)
            if (a.startsWith("file://"))
                a = QUrl(a).toLocalFile();
        Atributos d(args);
        return d.exec() == QDialog::Accepted ? 0 : 1;
    }
    QString pasta, consulta;
    for (int i = 0; i < args.size(); i++) {
        QString a = args[i].startsWith("file://") ? QUrl(args[i]).toLocalFile() : args[i];
        if (a == "--pasta" && i + 1 < args.size())
            pasta = args[++i].startsWith("file://") ? QUrl(args[i]).toLocalFile() : args[i];
        else if (a == "--procurar")
            continue;
        else if (a.endsWith(".consulta"))
            consulta = a;
        else {
            fprintf(stderr, "uso: lume-atributos [--editar ARQUIVO... | --pasta PASTA | ARQUIVO.consulta]\n");
            return 2;
        }
    }
    Procura p(pasta);
    if (!consulta.isEmpty() && !p.abrir(consulta)) {
        QMessageBox::warning(nullptr, p.windowTitle(), QObject::tr("Não deu para abrir %1.").arg(consulta));
        return 1;
    }
    p.show();
    if (!consulta.isEmpty())
        p.procurar();
    return app.exec();
}

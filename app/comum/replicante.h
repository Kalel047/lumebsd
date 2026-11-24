// SPDX-License-Identifier: BSD-2-Clause
//
// LumeBSD, os Replicants (como no BeOS): um pedaço vivo de um app na área
// de trabalho. O replicante é o próprio app, rodando o seu código num modo
// pequeno (lume-numeros --replicante ID): uma folha de papel sem borda,
// presa ao fundo, em todas as áreas de trabalho e fora da barra de tarefas.
//
// A mesa (~/.config/lume/mesa) guarda os replicantes: o app, os dados (o
// que ele mostra) e onde fica. O lume-mesa, que entra com a sessão, roda um
// processo por replicante e os traz de volta se caírem; o replicante grava
// onde está quando se arrasta ou muda de tamanho, e "Tirar da mesa" o apaga.

#pragma once

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLockFile>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#if __has_include(<QtGui/qguiapplication_platform.h>)
#include <QtGui/qguiapplication_platform.h>
#endif

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

// --- a mesa ----------------------------------------------------------------

struct Replicante {
	QString id;       // r1, r2...
	QString app;      // lume-numeros
	QStringList dados; // o que mostra (cada app sabe ler os seus)
	QRect geo;        // onde fica (vazio: o replicante escolhe)
};

// (LUME_MESA troca o arquivo: os testes usam uma mesa só deles.)
inline QString arquivoMesa()
{
	const QString e = qEnvironmentVariable("LUME_MESA");
	if (!e.isEmpty())
		return e;
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/lume/mesa";
}

// Só apps do LumeBSD (o lume-mesa roda o que está na mesa).
inline bool appDaMesa(const QString &app)
{
	static const QRegularExpression re("^lume-[a-z]+$");
	return re.match(app).hasMatch();
}

inline std::vector<Replicante> lerMesa()
{
	std::vector<Replicante> v;
	QSettings s(arquivoMesa(), QSettings::IniFormat);
	QStringList grupos = s.childGroups();
	std::sort(grupos.begin(), grupos.end(), [](const QString &a, const QString &b) {
		return a.mid(1).toInt() < b.mid(1).toInt();
	});
	for (const QString &g : grupos) {
		if (!g.startsWith('r'))
			continue;
		s.beginGroup(g);
		Replicante r;
		r.id = g;
		r.app = s.value("app").toString();
		r.dados = s.value("dados").toStringList();
		r.geo = s.value("lugar").toRect();
		s.endGroup();
		if (appDaMesa(r.app))
			v.push_back(r);
	}
	return v;
}

inline bool acharNaMesa(const QString &id, Replicante &r)
{
	for (const Replicante &x : lerMesa())
		if (x.id == id) {
			r = x;
			return true;
		}
	return false;
}

// Mexer na mesa: um processo de cada vez (o lume-mesa e os replicantes).
template <typename F> inline void mexerNaMesa(F f)
{
	QDir().mkpath(QFileInfo(arquivoMesa()).absolutePath());
	QLockFile trava(arquivoMesa() + ".trava");
	trava.tryLock(3000);
	QSettings s(arquivoMesa(), QSettings::IniFormat);
	f(s);
	s.sync();
}

inline QString porNaMesa(const QString &app, const QStringList &dados, const QRect &geo = QRect())
{
	QString id;
	mexerNaMesa([&](QSettings &s) {
		int n = 1;
		while (s.childGroups().contains(QString("r%1").arg(n)))
			n++;
		id = QString("r%1").arg(n);
		s.beginGroup(id);
		s.setValue("app", app);
		s.setValue("dados", dados);
		if (!geo.isEmpty())
			s.setValue("lugar", geo);
		s.endGroup();
	});
	return id;
}

inline void tirarDaMesa(const QString &id)
{
	mexerNaMesa([&](QSettings &s) { s.remove(id); });
}

inline void guardarLugar(const QString &id, const QRect &geo)
{
	mexerNaMesa([&](QSettings &s) {
		if (s.childGroups().contains(id))
			s.setValue(id + "/lugar", geo);
	});
}

// Põe na mesa e acorda o lume-mesa (se já está rodando, este sai na hora e o
// que roda vê a mesa mudar).
inline QString porNaMesaEAbrir(const QString &app, const QStringList &dados, const QRect &geo = QRect())
{
	const QString id = porNaMesa(app, dados, geo);
	QProcess::startDetached("lume-mesa", {});
	return id;
}

// --- as dicas da janela para o gerenciador de janelas ----------------------
//
// No X11, o KWin lê o _NET_WM_STATE (abaixo das outras, fora da barra de
// tarefas e do paginador) e o _NET_WM_DESKTOP (todas as áreas de trabalho).
// O xcb é aberto na hora (dlopen): sem os cabeçalhos para compilar e sem
// nada a fazer no Wayland.

namespace mesa_xcb {
struct Cookie { unsigned int seq; };
struct RespAtomo { uint8_t tipo, pad; uint16_t seq; uint32_t tam; uint32_t atomo; };
struct IterTela { void *tela; int resto; int indice; };
struct Msg {
	uint8_t tipo = 33; // XCB_CLIENT_MESSAGE
	uint8_t formato = 32;
	uint16_t seq = 0;
	uint32_t janela = 0;
	uint32_t que = 0;
	uint32_t d[5] = {0, 0, 0, 0, 0};
};
} // namespace mesa_xcb

inline void dicasDaMesa(QWidget *w)
{
#if __has_include(<QtGui/qguiapplication_platform.h>) && QT_CONFIG(xcb)
	auto *x = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
	if (!x || !x->connection())
		return;
	using namespace mesa_xcb;
	void *lib = dlopen("libxcb.so.1", RTLD_LAZY | RTLD_LOCAL);
	if (!lib)
		lib = dlopen("libxcb.so", RTLD_LAZY | RTLD_LOCAL);
	if (!lib)
		return;
	auto internar = reinterpret_cast<Cookie (*)(void *, uint8_t, uint16_t, const char *)>(dlsym(lib, "xcb_intern_atom"));
	auto resposta = reinterpret_cast<RespAtomo *(*)(void *, Cookie, void **)>(dlsym(lib, "xcb_intern_atom_reply"));
	auto mandar = reinterpret_cast<Cookie (*)(void *, uint8_t, uint32_t, uint32_t, const char *)>(dlsym(lib, "xcb_send_event"));
	auto setup = reinterpret_cast<const void *(*)(void *)>(dlsym(lib, "xcb_get_setup"));
	auto telas = reinterpret_cast<IterTela (*)(const void *)>(dlsym(lib, "xcb_setup_roots_iterator"));
	auto descarregar = reinterpret_cast<int (*)(void *)>(dlsym(lib, "xcb_flush"));
	if (!internar || !resposta || !mandar || !setup || !telas || !descarregar)
		return;
	void *c = x->connection();
	auto atomo = [&](const char *nome) -> uint32_t {
		RespAtomo *r = resposta(c, internar(c, 0, uint16_t(strlen(nome)), nome), nullptr);
		const uint32_t a = r ? r->atomo : 0;
		free(r);
		return a;
	};
	const IterTela it = telas(setup(c));
	if (!it.tela)
		return;
	const uint32_t raiz = *static_cast<const uint32_t *>(it.tela);
	const uint32_t janela = uint32_t(w->winId());
	auto msg = [&](uint32_t que, std::initializer_list<uint32_t> d) {
		Msg m;
		m.janela = janela;
		m.que = que;
		int i = 0;
		for (uint32_t v : d)
			m.d[i++] = v;
		// SUBSTRUCTURE_NOTIFY | SUBSTRUCTURE_REDIRECT
		mandar(c, 0, raiz, (1u << 19) | (1u << 20), reinterpret_cast<const char *>(&m));
	};
	const uint32_t estado = atomo("_NET_WM_STATE");
	msg(estado, {1, atomo("_NET_WM_STATE_SKIP_TASKBAR"), atomo("_NET_WM_STATE_SKIP_PAGER"), 1});
	msg(estado, {1, atomo("_NET_WM_STATE_BELOW"), 0, 1});
	msg(atomo("_NET_WM_DESKTOP"), {0xFFFFFFFFu, 1});
	descarregar(c);
#else
	Q_UNUSED(w);
#endif
}

// --- a janela do replicante ------------------------------------------------
//
// O app desenha o que mostra (desenhar) no papel; a janela cuida do resto:
// arrastar (por qualquer lugar), mudar de tamanho (pela alça do canto), o
// menu (Abrir no app, Atualizar, Tirar da mesa) e guardar o lugar.

class JanelaReplicante : public QWidget
{
public:
	std::function<void(QPainter &, const QRectF &)> desenhar;
	std::function<void()> atualizar;
	std::function<void()> abrir;
	QString textoAbrir = "Abrir"; // "Abrir nos Números"
	QSize tamanhoPadrao{420, 280};

	explicit JanelaReplicante(const Replicante &r) : rep_(r)
	{
		setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnBottomHint);
		// Aparece sem tomar o foco (senão o KWin o marca como "pedindo atenção").
		setAttribute(Qt::WA_ShowWithoutActivating);
		setWindowTitle("Replicante");
		setMouseTracking(true);
		setAttribute(Qt::WA_Hover);
		setMinimumSize(160, 100);
		lugar_.setSingleShot(true);
		lugar_.setInterval(600);
		QObject::connect(&lugar_, &QTimer::timeout, this, [this] {
			if (!naMesa_)
				return;
			guardarLugar(rep_.id, geometry());
		});
	}

	const Replicante &replicante() const { return rep_; }

	// Põe no lugar guardado (ou no alto à direita da tela) e mostra.
	void mostrar()
	{
		QRect g = rep_.geo;
		const QScreen *tela = QGuiApplication::primaryScreen();
		const QRect livre = tela ? tela->availableGeometry() : QRect(0, 0, 1366, 768);
		const bool novo = g.isEmpty();
		if (novo)
			g = lugarLivre(livre);
		if (!livre.isEmpty() && !livre.intersects(g))
			g.moveTopLeft(livre.topLeft() + QPoint(32, 32));
		setGeometry(g);
		show();
		dicasDaMesa(this);
		naMesa_ = true;
		// O lugar escolhido já fica na mesa (o próximo replicante novo não o pega).
		if (novo)
			guardarLugar(rep_.id, g);
	}

	// O primeiro lugar sem outro replicante: em colunas da direita para a
	// esquerda, de cima para baixo (o lado esquerdo da área de trabalho
	// costuma ter os ícones).
	QRect lugarLivre(const QRect &livre) const
	{
		const int m = 24;
		QList<QRect> outros;
		for (const Replicante &x : lerMesa())
			if (x.id != rep_.id && !x.geo.isEmpty())
				outros << x.geo.adjusted(-m / 2, -m / 2, m / 2, m / 2);
		QRect g(QPoint(0, 0), tamanhoPadrao);
		for (int x = livre.right() - m - g.width(); x >= livre.left() + m; x -= 16)
			for (int y = livre.top() + m; y + g.height() <= livre.bottom() - m; y += 16) {
				g.moveTopLeft(QPoint(x, y));
				if (std::none_of(outros.begin(), outros.end(), [&](const QRect &o) { return o.intersects(g); }))
					return g;
			}
		// A área cheia: um pouco abaixo e à esquerda, como uma pilha.
		const int n = rep_.id.mid(1).toInt();
		g.moveTopRight(livre.topRight() + QPoint(-m - 24 * ((n - 1) % 6), m + 24 * ((n - 1) % 6)));
		return g;
	}

	// Sem tirar da mesa (o "Tirar" e o fim da sessão são diferentes).
	void tirar()
	{
		naMesa_ = false;
		tirarDaMesa(rep_.id);
		qApp->quit();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = rect();
		if (desenhar)
			desenhar(p, r);
		else
			p.fillRect(r, QColor(0xff, 0xfd, 0xf8));
		if (perto_) {
			// A alça (a mãozinha do BeOS): o canto que muda o tamanho.
			const QRectF a = alca();
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0x1c, 0x1b, 0x19));
			p.drawRect(a);
			p.setPen(QPen(QColor(0xff, 0xfd, 0xf8), 1.2));
			for (int i = 1; i <= 3; i++)
				p.drawLine(QPointF(a.right() - 3, a.top() + 3 * i + 1), QPointF(a.left() + 3 * i + 1, a.bottom() - 3));
		}
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() != Qt::LeftButton || !windowHandle())
			return;
		if (alca().contains(e->position()))
			windowHandle()->startSystemResize(Qt::BottomEdge | Qt::RightEdge);
		else
			windowHandle()->startSystemMove();
	}

	void mouseDoubleClickEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton && abrir)
			abrir();
	}

	void mouseMoveEvent(QMouseEvent *e) override
	{
		setCursor(alca().contains(e->position()) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
	}

	void enterEvent(QEnterEvent *) override
	{
		perto_ = true;
		update();
	}

	void leaveEvent(QEvent *) override
	{
		perto_ = false;
		update();
	}

	void moveEvent(QMoveEvent *) override { lugar_.start(); }
	void resizeEvent(QResizeEvent *) override { lugar_.start(); }

	void contextMenuEvent(QContextMenuEvent *e) override
	{
		QMenu m(this);
		if (abrir)
			m.addAction(textoAbrir, this, [this] { abrir(); });
		if (atualizar)
			m.addAction("Atualizar", this, [this] {
				atualizar();
				update();
			});
		m.addSeparator();
		m.addAction("Tirar da mesa", this, [this] { tirar(); });
		m.exec(e->globalPos());
	}

private:
	QRectF alca() const { return QRectF(width() - 16, height() - 16, 16, 16); }

	Replicante rep_;
	QTimer lugar_;
	bool perto_ = false;
	bool naMesa_ = false;
};


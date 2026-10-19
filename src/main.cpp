#include "ProcessModel.h"
#include "AppIdentity.h"
#include "SystemMonitor.h"
#include "ServicesModel.h"
#include "UsersModel.h"
#include "StartupModel.h"
#include "ColumnPreferences.h"

#include <QApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QObject>
#include <QPalette>
#include <QFontInfo>
#include <QColor>
#include <QVariantMap>
#include <algorithm>
#include <QElapsedTimer>
#include <QTimer>
#include <QQuickWindow>
#include <QDebug>

int main(int argc, char *argv[])
{
    QElapsedTimer startupClock;
    startupClock.start();
    const bool traceStartup = qEnvironmentVariableIsSet("TAREFAS11_STARTUP_TRACE");
    auto mark = [&startupClock, traceStartup](const char *name) {
        if (traceStartup)
            qInfo().noquote() << "[Tarefas11 startup]" << name << ':'
                              << startupClock.elapsed() << "ms";
    };
    QApplication app(argc, argv);
    mark("QApplication pronta");
    QCoreApplication::setApplicationName(QStringLiteral("Tarefas11"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tarefas11"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("utilities-system-monitor")));

    // Não forçar Basic ou Fusion. QApplication permite ao Plasma escolher
    // org.kde.desktop quando Breeze e Plasma Integration estão instalados.
    // QT_QUICK_CONTROLS_STYLE permanece sob controle do usuário.

    SystemMonitor monitor;
    monitor.primeSnapshot();
    mark("prévia rápida concluída");
    AppIdentityResolver iconResolver;
    ProcessModel processes(&iconResolver);
    ServicesModel servicesModel;
    UsersModel usersModel;
    // A aba XDG do KDE só precisa ser indexada na primeira visita.
    // Testes que constroem StartupModel diretamente preservam o carregamento normal.
    StartupModel startupModel(nullptr, false);
    ColumnPreferences columnPrefs;
    mark("modelos preparados; índice .desktop em andamento");
    // A primeira leitura de hardware pode atualizar o QML antes da varredura
    // custosa de PIDs/ícones. Esta pequena janela permite apresentar os dados
    // de CPU/RAM/disco, sem atrasar perceptivelmente a lista de processos.
    bool firstProcessSample = true;
    QObject::connect(&monitor, &SystemMonitor::sampled, &processes,
                     [&processes, &firstProcessSample](quint64 ticks) {
        if (firstProcessSample) {
            firstProcessSample = false;
            QTimer::singleShot(20, &processes,
                               [&processes, ticks]() { processes.refresh(ticks); });
        } else {
            processes.refresh(ticks);
        }
    });

    QQmlApplicationEngine engine;
    auto applyPlatformAppearance = [&engine]() {
        const QPalette palette = QGuiApplication::palette();
        const auto tint = [](const QColor &a, const QColor &b, double ratio) {
            return QColor::fromRgbF(a.redF()*(1-ratio)+b.redF()*ratio,
                                    a.greenF()*(1-ratio)+b.greenF()*ratio,
                                    a.blueF()*(1-ratio)+b.blueF()*ratio);
        };
        const QColor window = palette.color(QPalette::Window);
        const QColor base = palette.color(QPalette::Base);
        const QColor accent = palette.color(QPalette::Highlight);
        const QVariantMap theme {
            {QStringLiteral("window"), QVariant::fromValue(window)},
            {QStringLiteral("base"), QVariant::fromValue(base)},
            {QStringLiteral("alternate"), QVariant::fromValue(palette.color(QPalette::AlternateBase))},
            {QStringLiteral("text"), QVariant::fromValue(palette.color(QPalette::WindowText))},
            {QStringLiteral("muted"), QVariant::fromValue(palette.color(QPalette::PlaceholderText))},
            {QStringLiteral("border"), QVariant::fromValue(palette.color(QPalette::Mid))},
            {QStringLiteral("selection"), QVariant::fromValue(tint(base, accent, .20))},
            {QStringLiteral("highlight"), QVariant::fromValue(accent)}
        };
        const QFont font = QGuiApplication::font();
        engine.rootContext()->setContextProperty(QStringLiteral("platformTheme"), theme);
        engine.rootContext()->setContextProperty(QStringLiteral("systemDarkMode"), window.lightness() < 128);
        engine.rootContext()->setContextProperty(QStringLiteral("systemFontFamily"), font.family());
        engine.rootContext()->setContextProperty(QStringLiteral("systemFontSize"),
                                                 std::max(10, QFontInfo(font).pixelSize()));
    };
    applyPlatformAppearance();
    QObject::connect(&app, &QGuiApplication::paletteChanged, &engine,
                     [&applyPlatformAppearance](const QPalette &) { applyPlatformAppearance(); });
    QObject::connect(&app, &QGuiApplication::fontChanged, &engine,
                     [&applyPlatformAppearance](const QFont &) { applyPlatformAppearance(); });
    engine.addImageProvider(QStringLiteral("appicons"), new AppIconProvider(&iconResolver));
    engine.addImageProvider(QStringLiteral("startupicons"), new StartupIconProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("monitor"), &monitor);
    engine.rootContext()->setContextProperty(QStringLiteral("processes"), &processes);
    engine.rootContext()->setContextProperty(QStringLiteral("servicesModel"), &servicesModel);
    engine.rootContext()->setContextProperty(QStringLiteral("usersModel"), &usersModel);
    engine.rootContext()->setContextProperty(QStringLiteral("startupModel"), &startupModel);
    engine.rootContext()->setContextProperty(QStringLiteral("columnPrefs"), &columnPrefs);
    engine.loadFromModule(QStringLiteral("Tarefas11"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return 1;
    mark("QML construído");

    QObject::connect(&monitor, &SystemMonitor::sampled, &app,
                     [&mark](quint64) { mark("primeira amostragem completa"); },
                     Qt::SingleShotConnection);

    // A prévia leve é calculada antes do QML. A coleta integral (incluindo
    // processos) só começa após o primeiro frame; sem pausa artificial extra.
    bool monitoringStarted = false;
    auto startMonitoring = [&]() {
        if (monitoringStarted) return;
        monitoringStarted = true;
        mark("iniciando telemetria");
        monitor.start();
    };
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &app,
                         [&app, &mark, &startMonitoring]() {
            mark("primeiro frame apresentado");
            startMonitoring();
        }, Qt::SingleShotConnection);
    }
    // Fallback se o backend gráfico não emitir frameSwapped (ex.: offscreen).
    QTimer::singleShot(250, &app, startMonitoring);
    return app.exec();
}

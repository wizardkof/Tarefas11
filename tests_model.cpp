#include "StartupModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) qFatal("Failed to create fixture");
}
void check(bool condition, const char *message)
{
    if (!condition) qFatal("StartupModel regression: %s", message);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    const QString user = temp.path() + QStringLiteral("/user");
    const QString system = temp.path() + QStringLiteral("/system");
    qputenv("XDG_CONFIG_HOME", user.toUtf8());
    qputenv("XDG_CONFIG_DIRS", system.toUtf8());
    const QString systemDesktop = system + QStringLiteral("/autostart/example.desktop");
    writeFile(systemDesktop, "[Desktop Entry]\nType=Application\nName=Exemplo\nExec=/usr/bin/true\nIcon=utilities-terminal\n");
    writeFile(user + QStringLiteral("/plasma-workspace/env/test.sh"), "export TEST=true\n");
    // Arquivos XDG que não se aplicam a esta sessão KDE ficam ocultos.
    writeFile(system + QStringLiteral("/autostart/gnome-only.desktop"),
              "[Desktop Entry]\nType=Application\nName=GNOME Only\nExec=/usr/bin/true\nOnlyShowIn=GNOME;\n");
    // Construção leve para a aba ainda não aberta: nenhuma varredura XDG no startup.
    StartupModel lazyModel(nullptr, false);
    check(lazyModel.totalCount() == 0, "deferred autostart is initially empty");
    lazyModel.refresh();
    check(lazyModel.totalCount() == 2, "deferred autostart loads on first visit");
    StartupModel model;
    check(model.totalCount() == 2, "XDG/KDE-only and pre-startup scripts discovered");
    check(model.enabledCount() == 1, "application enabled initially");
    check(model.scriptsCount() == 1, "script discovered");
    check(model.setEnabled(QStringLiteral("desktop:example.desktop"), false), "disable system entry");
    check(model.enabledCount() == 0, "disable affected model");
    check(QFile::exists(user + QStringLiteral("/autostart/example.desktop")), "user override created");
    const QByteArray systemAfter = [] (const QString &path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }(systemDesktop);
    check(!systemAfter.contains("Hidden=true"), "global entry untouched");
    check(model.setEnabled(QStringLiteral("desktop:example.desktop"), true), "enable system entry");
    check(model.enabledCount() == 1, "re-enabled");
    check(model.addApplication(QStringLiteral("My App"), QStringLiteral("/usr/bin/true --verbose")), "add command");
    check(model.totalCount() == 3, "added startup entry");
    check(model.removeEntry(QStringLiteral("desktop:tarefas11-my-app.desktop")), "remove user entry");
    check(model.totalCount() == 2, "removed startup entry");
    check(!model.removeEntry(QStringLiteral("desktop:example.desktop")), "system override protected");
    QTextStream(stdout) << "Tarefas11 StartupModel: 16 checks PASS\n";
    return 0;
}

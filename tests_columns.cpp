#include "ColumnPreferences.h"
#include <QCoreApplication>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDebug>
#include <cmath>

static bool check(bool test, const char *message) {
    if (!test) qCritical() << "FAIL" << message;
    return test;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Tarefas11TestColumns"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tarefas11TestSuite"));
    QTemporaryDir dir;
    if (!check(dir.isValid(), "temporary test directory")) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    ColumnPreferences prefs;
    const auto original = prefs.columns(QStringLiteral("Processos"));
    if (!check(original.size() >= 6, "default visible columns")) return 1;
    bool ramImmediatelyBeforeSwap = false;
    for (int i = 1; i < original.size(); ++i) {
        if (original[i - 1].toMap().value("key") == QStringLiteral("mem") &&
            original[i].toMap().value("key") == QStringLiteral("swap"))
            ramImmediatelyBeforeSwap = true;
    }
    if (!check(ramImmediatelyBeforeSwap, "SWAP visivel ao lado da RAM por padrao")) return 1;
    bool detailsSwap = false;
    const auto detailCols = prefs.columns(QStringLiteral("Detalhes"));
    for (const auto &column : detailCols) {
        if (column.toMap().value("key") == QStringLiteral("swap")) detailsSwap = true;
    }
    if (!check(detailsSwap, "SWAP visivel em Detalhes")) return 1;
    prefs.setVisible(QStringLiteral("Processos"), QStringLiteral("net"), false);
    prefs.setVisible(QStringLiteral("Detalhes"), QStringLiteral("diskBusy"), true);
    prefs.resizeColumn(QStringLiteral("Processos"), QStringLiteral("name"), 15, 1120);
    const auto updated = prefs.columns(QStringLiteral("Processos"));
    for (const auto &c : updated) if (!check(c.toMap().value("key").toString() != QStringLiteral("net"), "net hidden")) return 1;
    prefs.setVisible(QStringLiteral("Processos"), QStringLiteral("name"), false);
    bool foundName = false;
    for (const auto &c : prefs.columns(QStringLiteral("Processos"))) if (c.toMap().value("key") == QStringLiteral("name")) foundName = true;
    if (!check(foundName, "name required")) return 1;
    ColumnPreferences restored;
    if (!check(restored.columns(QStringLiteral("Processos")).size() == updated.size(), "persisted visibility")) return 1;
    prefs.resetColumns(QStringLiteral("Processos"));
    if (!check(prefs.columns(QStringLiteral("Processos")).size() == original.size(), "reset defaults")) return 1;
    // Simula preferências salvas na v0.4.7, que não tinha a coluna SWAP.
    QSettings legacy;
    QJsonObject oldColumns;
    oldColumns.insert(QStringLiteral("mem"), QJsonObject{{QStringLiteral("weight"), 0.13}, {QStringLiteral("hidden"), false}});
    oldColumns.insert(QStringLiteral("name"), QJsonObject{{QStringLiteral("weight"), 0.46}, {QStringLiteral("hidden"), false}});
    legacy.setValue(QStringLiteral("colunas/Processos"), QJsonDocument(oldColumns).toJson(QJsonDocument::Compact));
    legacy.sync();
    ColumnPreferences migrated;
    bool swapMigrated = false;
    const auto migratedColumns = migrated.columns(QStringLiteral("Processos"));
    for (const auto &column : migratedColumns)
        if (column.toMap().value("key") == QStringLiteral("swap")) swapMigrated = true;
    if (!check(swapMigrated, "coluna SWAP default para perfis antigos")) return 1;
    return 0;
}

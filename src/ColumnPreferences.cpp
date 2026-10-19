#include "ColumnPreferences.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

ColumnPreferences::ColumnPreferences(QObject *parent) : QObject(parent) {
    load(QStringLiteral("Processos"));
    load(QStringLiteral("Detalhes"));
}

QList<ColumnPreferences::Definition> ColumnPreferences::definitions(const QString &page) {
    if (page == QStringLiteral("Detalhes")) return {
        {"name", "Nome", 0.28, true}, {"pid", "PID", 0.08, true}, {"ppid", "PID pai", 0.09, true},
        {"user", "Usuário", 0.14, true}, {"state", "Status", 0.10, true},
        {"cpu", "CPU", 0.075, true}, {"mem", "RAM", 0.12, true},
        {"swap", "SWAP", 0.10, true},
        {"virtual", "Virtual", 0.12, true}, {"threads", "Threads", 0.075, true},
        {"nice", "Nice", 0.055, true}, {"disk", "E/S disco", 0.10, false},
        {"diskBusy", "E/S %", 0.09, false}, {"net", "Rede TCP", 0.1, false},
        {"read", "Leitura", 0.1, false}, {"write", "Gravação", 0.1, false},
        {"platform", "Origem", 0.15, false}
    };
    return {
        {"name", "Nome", 0.46, true}, {"state", "Status", 0.14, true},
        {"cpu", "CPU", 0.10, true}, {"mem", "RAM", 0.13, true},
        {"swap", "SWAP", 0.10, true},
        {"disk", "Disco", 0.09, true}, {"net", "Rede TCP", 0.08, true},
        {"pid", "PID", 0.075, false}, {"ppid", "PID pai", 0.09, false},
        {"diskBusy", "E/S %", 0.10, true}, {"threads", "Threads", 0.09, false},
        {"read", "Leitura", 0.1, false}, {"write", "Gravação", 0.1, false},
        {"user", "Usuário", 0.13, false}, {"platform", "Origem", 0.15, false},
        {"virtual", "Virtual", 0.1, false}, {"nice", "Nice", 0.07, false}
    };
}

QString ColumnPreferences::settingsKey(const QString &page) const { return QStringLiteral("colunas/%1").arg(page); }

void ColumnPreferences::load(const QString &page) {
    QSettings settings;
    const auto defs = definitions(page);
    for (const auto &d : defs) {
        m_widths[page][d.key] = d.weight;
        if (!d.defaultVisible) m_hidden[page].insert(d.key);
    }
    const QByteArray json = settings.value(settingsKey(page)).toByteArray();
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
    const auto obj = doc.object();
    for (const auto &d : defs) {
        if (obj.value(d.key).isObject()) {
            const auto entry = obj.value(d.key).toObject();
            const double width = entry.value(QStringLiteral("weight")).toDouble(d.weight);
            if (std::isfinite(width) && width > 0.003 && width < 3) m_widths[page][d.key] = width;
            if (entry.value(QStringLiteral("hidden")).toBool(!d.defaultVisible)) m_hidden[page].insert(d.key);
            else m_hidden[page].remove(d.key);
        }
    }
    m_hidden[page].remove(QStringLiteral("name"));
}

void ColumnPreferences::save(const QString &page) {
    QJsonObject object;
    for (const auto &d : definitions(page)) {
        QJsonObject o;
        o.insert(QStringLiteral("weight"), m_widths.value(page).value(d.key, d.weight));
        o.insert(QStringLiteral("hidden"), m_hidden.value(page).contains(d.key));
        object.insert(d.key, o);
    }
    QSettings settings;
    settings.setValue(settingsKey(page), QJsonDocument(object).toJson(QJsonDocument::Compact));
    ++m_revision;
    emit changed();
}

QVariantList ColumnPreferences::columns(const QString &page) const {
    const auto definitionsList = definitions(page);
    double total = 0;
    for (const auto &d : definitionsList)
        if (!m_hidden.value(page).contains(d.key)) total += m_widths.value(page).value(d.key, d.weight);
    QVariantList result;
    for (const auto &d : definitionsList) {
        if (m_hidden.value(page).contains(d.key)) continue;
        result.append(QVariantMap{{"key", d.key}, {"title", d.title},
                                  {"ratio", m_widths.value(page).value(d.key, d.weight) / std::max(0.001, total)}});
    }
    return result;
}

QVariantList ColumnPreferences::availableColumns(const QString &page) const {
    QVariantList result;
    for (const auto &d : definitions(page))
        result.append(QVariantMap{{"key", d.key}, {"title", d.title},
                                  {"enabled", !m_hidden.value(page).contains(d.key)},
                                  {"mandatory", d.key == QStringLiteral("name")}});
    return result;
}

void ColumnPreferences::setVisible(const QString &page, const QString &key, bool visible) {
    if (key == QStringLiteral("name")) return;
    bool valid = false;
    for (const auto &d : definitions(page)) if (d.key == key) { valid = true; break; }
    if (!valid) return;
    if (visible) m_hidden[page].remove(key); else m_hidden[page].insert(key);
    save(page);
}

void ColumnPreferences::resizeColumn(const QString &page, const QString &key, double delta, double viewWidth) {
    if (!std::isfinite(delta) || viewWidth <= 100) return;
    auto active = columns(page);
    int target = -1;
    for (int i = 0; i < active.size() - 1; ++i)
        if (active[i].toMap().value("key").toString() == key) { target = i; break; }
    if (target < 0) return;
    const QString neighbor = active[target + 1].toMap().value("key").toString();
    const double fraction = delta / viewWidth;
    const double a = active[target].toMap().value("ratio").toDouble();
    const double b = active[target + 1].toMap().value("ratio").toDouble();
    const double minA = key == QStringLiteral("name") ? 120.0 / viewWidth : 55.0 / viewWidth;
    const double minB = neighbor == QStringLiteral("name") ? 120.0 / viewWidth : 55.0 / viewWidth;
    const double bounded = std::clamp(fraction, minA - a, b - minB);
    if (std::abs(bounded) < 0.00001) return;
    double sum = 0;
    for (const auto &c : active) sum += m_widths[page].value(c.toMap().value("key").toString());
    m_widths[page][key] += bounded * sum;
    m_widths[page][neighbor] -= bounded * sum;
    save(page);
}

void ColumnPreferences::resetColumns(const QString &page) {
    m_widths.remove(page);
    m_hidden.remove(page);
    for (const auto &d : definitions(page)) {
        m_widths[page][d.key] = d.weight;
        if (!d.defaultVisible) m_hidden[page].insert(d.key);
    }
    save(page);
}

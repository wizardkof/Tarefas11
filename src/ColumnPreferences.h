#pragma once
#include <QObject>
#include <QVariantList>
#include <QMap>
#include <QSet>

// Configuração persistente das colunas, separada por tela, sem dependências de KDE.
class ColumnPreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit ColumnPreferences(QObject *parent = nullptr);
    int revision() const { return m_revision; }
    Q_INVOKABLE QVariantList columns(const QString &page) const;
    Q_INVOKABLE QVariantList availableColumns(const QString &page) const;
    Q_INVOKABLE void setVisible(const QString &page, const QString &key, bool visible);
    Q_INVOKABLE void resizeColumn(const QString &page, const QString &key, double deltaPixels, double tableWidth);
    Q_INVOKABLE void resetColumns(const QString &page);
signals:
    void changed();
private:
    struct Definition { QString key, title; double weight; bool defaultVisible; };
    static QList<Definition> definitions(const QString &page);
    QString settingsKey(const QString &page) const;
    void load(const QString &page);
    void save(const QString &page);
    QMap<QString, double> &widths(const QString &page) { return m_widths[page]; }
    QMap<QString, QMap<QString, double>> m_widths;
    QMap<QString, QSet<QString>> m_hidden;
    int m_revision = 0;
};

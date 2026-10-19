#include "DrmEngineSampler.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <QObject>
#include <unistd.h>
#include <algorithm>

namespace {
QByteArray bytes(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(20000) : QByteArray{};
}
QString linked(const QString &file) {
    char buf[256];
    const auto raw = QFile::encodeName(file);
    const auto n = ::readlink(raw.constData(), buf, sizeof(buf)-1);
    if (n <= 0) return {};
    buf[n] = 0;
    return QString::fromLocal8Bit(buf);
}
QString engineClass(const QString &engine) {
    const QString name = engine.toLower();
    if (name.contains(QStringLiteral("enc")) || name.contains(QStringLiteral("venc"))) return QStringLiteral("encode");
    if (name.contains(QStringLiteral("dec")) || name.contains(QStringLiteral("vcn")) || name.contains(QStringLiteral("video"))) return QStringLiteral("decode");
    if (name.contains(QStringLiteral("copy")) || name.contains(QStringLiteral("dma")) || name.contains(QStringLiteral("blt"))) return QStringLiteral("copy");
    if (name.contains(QStringLiteral("render")) || name.contains(QStringLiteral("gfx")) || name.contains(QStringLiteral("compute")) || name == QStringLiteral("rcs")) return QStringLiteral("render");
    return {};
}
}

DrmEngineSampler::DrmEngineSampler() {
    QObject::connect(&m_watcher, &QFutureWatcher<Result>::finished, &m_watcher, [this]() {
        const auto output = m_watcher.result();
        if (!output.valid) return;
        m_previous = output.total;
        m_engines = output.engines;
        m_baseline = true;
    });
}

DrmEngineSampler::Engine DrmEngineSampler::device(const QString &pci) const {
    return m_engines.value(pci.toLower());
}

DrmEngineSampler::Result DrmEngineSampler::scan(const Totals &previous, double seconds, bool baseline) {
    Result out;
    out.valid = true;
    // PCI + DRM client id deduplicam file descriptors duplicados em vários processos.
    QHash<QString, DeviceTotals> clients;
    for (const QString &pid : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        pid.toInt(&isPid);
        if (!isPid) continue;
        const QString path = QStringLiteral("/proc/") + pid + QStringLiteral("/fd/");
        for (const QString &fd : QDir(path).entryList(QDir::System | QDir::AllEntries | QDir::NoDotAndDotDot)) {
            if (!linked(path + fd).startsWith(QStringLiteral("/dev/dri/"))) continue;
            const auto contents = QString::fromUtf8(bytes(QStringLiteral("/proc/") + pid + QStringLiteral("/fdinfo/") + fd));
            QString pci, client;
            DeviceTotals counters;
            for (const QString &line : contents.split('\n')) {
                const auto colon = line.indexOf(':');
                if (colon < 0) continue;
                const QString field = line.left(colon).trimmed();
                const QString value = line.mid(colon+1).trimmed();
                if (field == QStringLiteral("drm-pdev")) pci = value.toLower();
                else if (field == QStringLiteral("drm-client-id")) client = value;
                else if (field.startsWith(QStringLiteral("drm-engine-"))) {
                    const QString klass = engineClass(field.mid(11));
                    if (klass.isEmpty()) continue;
                    bool ok = false;
                    const quint64 nanos = value.section(' ',0,0).toULongLong(&ok);
                    if (ok) counters[klass] += nanos;
                }
            }
            if (pci.isEmpty() || client.isEmpty() || counters.isEmpty()) continue;
            auto &tot = clients[pci + ':' + client];
            for (auto it=counters.cbegin(); it!=counters.cend();++it)
                tot[it.key()] = std::max(tot.value(it.key()), it.value());
        }
    }
    for (auto it=clients.cbegin(); it!=clients.cend();++it) {
        const QString key = it.key().section(':',0,2); // PCI domain:bus:device.function
        auto &dest = out.total[key];
        for (auto c=it.value().cbegin(); c!=it.value().cend();++c) dest[c.key()] += c.value();
    }
    if (!baseline || seconds <= 0) return out;
    for (auto it=out.total.cbegin(); it!=out.total.cend();++it) {
        Engine e;
        const auto previousCounters = previous.value(it.key());
        auto calc = [&](const QString &name) -> double {
            if (!it.value().contains(name) || !previousCounters.contains(name)) return -1;
            const quint64 now=it.value().value(name), before=previousCounters.value(name);
            return now>=before ? std::clamp(100.0 * double(now-before) / (seconds*1e9), 0.0, 100.0) : -1;
        };
        e.render=calc(QStringLiteral("render"));
        e.copy=calc(QStringLiteral("copy"));
        e.encode=calc(QStringLiteral("encode"));
        e.decode=calc(QStringLiteral("decode"));
        out.engines.insert(it.key(),e);
    }
    return out;
}

void DrmEngineSampler::update() {
    if (m_watcher.isRunning() || (m_clock.isValid() && m_clock.elapsed() < 4000)) return;
    const double sec = m_clock.isValid() ? std::max(0.001,m_clock.restart()/1000.0) : 0;
    if (!m_clock.isValid()) m_clock.start();
    const auto previous=m_previous;
    const bool baseline=m_baseline;
    m_watcher.setFuture(QtConcurrent::run([previous,sec,baseline]() {
        return scan(previous,sec,baseline);
    }));
}

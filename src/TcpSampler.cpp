#include "TcpSampler.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>
#include <QSet>
#include <cstring>
#include <cstddef>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
quint64 socketInode(const QString &path) {
    QByteArray raw = QFile::encodeName(path);
    char buffer[128];
    const auto n = ::readlink(raw.constData(), buffer, sizeof(buffer)-1);
    if (n <= 0) return 0;
    buffer[n] = 0;
    if (::strncmp(buffer, "socket:[", 8) != 0) return 0;
    const QByteArray value(buffer+8, n-8);
    const int closing = value.indexOf(']');
    return closing > 0 ? value.left(closing).toULongLong() : 0;
}
}

TcpSampler::TcpSampler() {
    QObject::connect(&m_watcher, &QFutureWatcher<Result>::finished, &m_watcher, [this]() {
        const Result result = m_watcher.result();
        if (!result.succeeded) return;
        m_prevSockets = result.sockets;
        m_pidRate = result.rates;
        m_baseline = true;
    });
}

bool TcpSampler::dumpFamily(int family, QHash<quint64, Bytes> *out) {
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (fd < 0) return false;
    struct timeval timeout {0, 140000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    struct {
        nlmsghdr hdr;
        inet_diag_req_v2 req;
    } request{};
    request.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(inet_diag_req_v2));
    request.hdr.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.hdr.nlmsg_seq = 77;
    request.req.sdiag_family = static_cast<quint8>(family);
    request.req.sdiag_protocol = IPPROTO_TCP;
    request.req.idiag_states = 0xffffffffU;
    request.req.idiag_ext = static_cast<quint8>(1U << (INET_DIAG_INFO - 1));
    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    const bool sent = ::sendto(fd, &request, request.hdr.nlmsg_len, 0,
                               reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) >= 0;
    if (!sent) { ::close(fd); return false; }
    bool success = false;
    char buffer[65536];
    for (int loops = 0; loops < 150; ++loops) {
        const ssize_t length = ::recv(fd, buffer, sizeof(buffer), 0);
        if (length <= 0) break;
        int remaining = static_cast<int>(length);
        for (auto *message = reinterpret_cast<nlmsghdr*>(buffer);
             NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
            if (message->nlmsg_type == NLMSG_DONE) { success = true; ::close(fd); return success; }
            if (message->nlmsg_type == NLMSG_ERROR) { ::close(fd); return false; }
            if (message->nlmsg_type != SOCK_DIAG_BY_FAMILY || message->nlmsg_len < NLMSG_LENGTH(sizeof(inet_diag_msg))) continue;
            const auto *socketInfo = reinterpret_cast<const inet_diag_msg*>(NLMSG_DATA(message));
            if (socketInfo->idiag_inode == 0) continue;
            const int attrBytes = static_cast<int>(message->nlmsg_len - NLMSG_LENGTH(sizeof(inet_diag_msg)));
            auto *attr = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(const_cast<inet_diag_msg*>(socketInfo)) + NLMSG_ALIGN(sizeof(inet_diag_msg)));
            int attrRemaining = attrBytes;
            for (; RTA_OK(attr, attrRemaining); attr = RTA_NEXT(attr, attrRemaining)) {
                if (attr->rta_type != INET_DIAG_INFO || RTA_PAYLOAD(attr) < static_cast<int>(offsetof(tcp_info, tcpi_bytes_received) + sizeof(quint64))) continue;
                const auto *tcp = reinterpret_cast<const tcp_info*>(RTA_DATA(attr));
                out->insert(socketInfo->idiag_inode, {tcp->tcpi_bytes_received, tcp->tcpi_bytes_acked});
            }
        }
    }
    ::close(fd);
    return success;
}

TcpSampler::Result TcpSampler::scan(const QVector<int> &pids,
                                        const QHash<quint64, Bytes> &previous,
                                        double seconds, bool baseline) {
    Result result;
    const bool a = dumpFamily(AF_INET, &result.sockets);
    const bool b = dumpFamily(AF_INET6, &result.sockets);
    if (!a && !b) return result;
    result.succeeded = true;
    QHash<quint64, Rate> socketRates;
    for (auto it = result.sockets.cbegin(); it != result.sockets.cend(); ++it) {
        if (!baseline || !previous.contains(it.key()) || seconds <= 0) continue;
        const auto old = previous.value(it.key());
        if (it.value().rx < old.rx || it.value().tx < old.tx) continue;
        socketRates.insert(it.key(), {(it.value().rx - old.rx) / (1048576.0 * seconds),
            (it.value().tx - old.tx) / (1048576.0 * seconds), true});
    }
    for (int pid : pids) {
        const QString fdPath = QStringLiteral("/proc/%1/fd").arg(pid);
        const QStringList fds = QDir(fdPath).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
        QSet<quint64> seen;
        Rate sum;
        for (const QString &fd : fds) {
            const quint64 inode = socketInode(fdPath + '/' + fd);
            if (!inode || seen.contains(inode) || !socketRates.contains(inode)) continue;
            seen.insert(inode);
            const auto measured = socketRates.value(inode);
            sum.rxMiBs += measured.rxMiBs;
            sum.txMiBs += measured.txMiBs;
            sum.valid = true;
        }
        if (sum.valid) result.rates.insert(pid, sum);
    }
    return result;
}

void TcpSampler::update(const QVector<int> &pids) {
    if (m_watcher.isRunning() || (m_sinceLast.isValid() && m_sinceLast.elapsed() < 3500)) return;
    const double seconds = m_sinceLast.isValid() ? std::max(0.001, m_sinceLast.restart() / 1000.0) : 0;
    if (!m_sinceLast.isValid()) m_sinceLast.start();
    const auto previous = m_prevSockets;
    const bool baseline = m_baseline;
    m_watcher.setFuture(QtConcurrent::run([pids, previous, seconds, baseline]() {
        return scan(pids, previous, seconds, baseline);
    }));
}

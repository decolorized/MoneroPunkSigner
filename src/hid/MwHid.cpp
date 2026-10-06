// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// Реализация HID-транспорта для протокола mwlink.

#include "MwHid.h"

#include <QDebug>
#include <QElapsedTimer>

#include <hidapi/hidapi.h>

#include <cstring>
#include <utility>

namespace MwLink {

namespace {

constexpr int kHidFirstHdr = 3;   // "?##"

// kFrameStart определён в MwLink.h (дубликат отсюда удалён).

QString deviceDescription(quint16 vid, quint16 pid)
{
    return QStringLiteral("hid %1:%2")
        .arg(vid, 4, 16, QChar('0'))
        .arg(pid, 4, 16, QChar('0'));
}

QString hidErrorString(hid_device *dev)
{
    const wchar_t *w = hid_error(dev);   // dev == nullptr → глобальная ошибка
    if (!w)
        return QStringLiteral("unknown error");
    return QString::fromWCharArray(w);
}

} // namespace

// ---------------------------------------------------------------------------

Hid::~Hid()
{
    close();
}

Hid::Hid(Hid &&other) noexcept
    : m_dev(other.m_dev)
    , m_vid(other.m_vid)
    , m_pid(other.m_pid)
    , m_rxBuf(std::move(other.m_rxBuf))
    , m_rxExpected(other.m_rxExpected)
    , m_cancel(other.m_cancel)
    , m_verbose(other.m_verbose)
    , m_lastError(std::move(other.m_lastError))
{
    other.m_dev = nullptr;
    other.m_vid = 0;
    other.m_pid = 0;
    other.m_rxExpected = -1;
}

Hid &Hid::operator=(Hid &&other) noexcept
{
    if (this == &other)
        return *this;

    close();

    m_dev        = other.m_dev;
    m_vid        = other.m_vid;
    m_pid        = other.m_pid;
    m_rxBuf      = std::move(other.m_rxBuf);
    m_rxExpected = other.m_rxExpected;
    m_cancel     = other.m_cancel;
    m_verbose    = other.m_verbose;
    m_lastError  = std::move(other.m_lastError);

    other.m_dev = nullptr;
    other.m_vid = 0;
    other.m_pid = 0;
    other.m_rxExpected = -1;
    return *this;
}

// ---------------------------------------------------------------------------

bool Hid::open(quint16 vid, quint16 pid)
{
    close();
    m_lastError.clear();

    if (hid_init() != 0) {
        m_lastError = QStringLiteral("hid_init failed");
        return false;
    }

    hid_device_info *list = hid_enumerate(vid, pid);

    QByteArray path;
    bool seen = false;

    // Проход 1: интерфейс с usage_page 0xFF00 (как mwlink.py find_hid_path).
    for (hid_device_info *it = list; it; it = it->next) {
        seen = true;
        if (it->usage_page == kHidUsagePage && it->path) {
            path = it->path;
            break;
        }
    }
    // Проход 2: бэкенды без поддержки usage_page (libusb) отдают 0.
    if (path.isEmpty()) {
        for (hid_device_info *it = list; it; it = it->next) {
            if (it->usage_page == 0 && it->path) {
                path = it->path;
                break;
            }
        }
    }
    hid_free_enumeration(list);

    if (path.isEmpty()) {
        m_lastError = seen
            ? QStringLiteral("device %1:%2 found, but it has no vendor HID interface (usage page 0x%3)")
                  .arg(vid, 4, 16, QChar('0')).arg(pid, 4, 16, QChar('0'))
                  .arg(kHidUsagePage, 4, 16, QChar('0'))
            : QStringLiteral("device %1:%2 not found")
                  .arg(vid, 4, 16, QChar('0')).arg(pid, 4, 16, QChar('0'));
        return false;
    }

    m_dev = hid_open_path(path.constData());
    if (!m_dev) {
        m_lastError = QStringLiteral("cannot open HID device: %1 (check permissions / udev rules)")
            .arg(hidErrorString(nullptr));
        return false;
    }

    hid_set_nonblocking(m_dev, 1);

    m_vid = vid;
    m_pid = pid;

    drain();   // выбросить хвосты прошлых сеансов

    if (m_verbose)
        qDebug() << "[MwHid] opened" << deviceDescription(vid, pid);
    return true;
}

void Hid::close()
{
    if (m_dev) {
        hid_close(m_dev);
        m_dev = nullptr;
        if (m_verbose)
            qDebug() << "[MwHid] closed" << deviceDescription(m_vid, m_pid);
    }
    m_vid = 0;
    m_pid = 0;
    m_rxBuf.clear();
    m_rxExpected = -1;
    // hid_exit() намеренно не вызываем: hidapi может использоваться
    // другими частями Feather (Ledger/Trezor).
}

QString Hid::describe() const
{
    if (!m_dev)
        return QStringLiteral("hid (not open)");
    return deviceDescription(m_vid, m_pid);
}

void Hid::drain()
{
    m_rxBuf.clear();
    m_rxExpected = -1;
    if (!m_dev)
        return;

    unsigned char buf[64];
    for (int i = 0; i < 256; ++i) {
        if (hid_read_timeout(m_dev, buf, sizeof(buf), 0) <= 0)
            break;
    }
}

// ---------------------------------------------------------------------------

bool Hid::writeReport(const QByteArray &data63)
{
    if (!m_dev)
        return false;

    QByteArray report(1 + kHidData, '\0');
    report[0] = char(kHidReportId);

    const int n = qMin<int>(data63.size(), kHidData);
    if (n > 0)
        std::memcpy(report.data() + 1, data63.constData(), size_t(n));

    const int written = hid_write(m_dev,
                                  reinterpret_cast<const unsigned char *>(report.constData()),
                                  size_t(report.size()));
    if (written < 0) {
        m_lastError = QStringLiteral("hid_write failed: %1").arg(hidErrorString(m_dev));
        if (m_verbose)
            qDebug() << "[MwHid] write error:" << m_lastError;
        return false;
    }
    return true;
}

RecvStatus Hid::readReport(QByteArray *out, int timeoutMs)
{
    if (!m_dev)
        return RecvStatus::Error;

    unsigned char buf[64];
    const int n = hid_read_timeout(m_dev, buf, sizeof(buf), timeoutMs);

    if (n < 0) {
        m_lastError = QStringLiteral("hid_read failed: %1").arg(hidErrorString(m_dev));
        if (m_verbose)
            qDebug() << "[MwHid] read error:" << m_lastError;
        return RecvStatus::Error;
    }
    if (n == 0)
        return RecvStatus::Timeout;

    QByteArray rep(reinterpret_cast<const char *>(buf), n);
    if (rep.size() == kHidData + 1 && quint8(rep[0]) == kHidReportId)
        rep.remove(0, 1);

    *out = rep;
    return RecvStatus::Ok;
}

// ---------------------------------------------------------------------------

bool Hid::send(const QByteArray &msg)
{
    if (!m_dev) {
        m_lastError = QStringLiteral("HID device not open");
        return false;
    }
    if (m_verbose)
        logTx(msg);

    {
        QByteArray first;
        first.reserve(kHidData);
        // Первый report: "?##" + 60 байт сообщения. Само сообщение уже начинается
        // с magic "MW", поэтому на шине получается "?##MW...". Нельзя добавлять
        // весь kFrameStart (5 байт): "MW" задвоится, а хвост сообщения обрежется.
        first.append(kFrameStart, kHidFirstHdr);
        first.append(msg.left(kHidFirstData));
        if (first.size() < kHidData)
            first.append(kHidData - first.size(), '\0');
        if (!writeReport(first))
            return false;
    }

    int off = kHidFirstData;
    while (off < msg.size()) {
        QByteArray part;
        part.reserve(kHidData);
        part.append('?');
        part.append(msg.mid(off, kHidContData));
        if (part.size() < kHidData)
            part.append(kHidData - part.size(), '\0');
        if (!writeReport(part))
            return false;
        off += kHidContData;
    }
    return true;
}

// ---------------------------------------------------------------------------

RecvStatus Hid::recv(QByteArray *out, int timeoutMs)
{
    if (!m_dev) {
        m_lastError = QStringLiteral("HID device not open");
        return RecvStatus::Error;
    }
    m_lastError.clear();

    QElapsedTimer clock;
    clock.start();

    while (clock.elapsed() < timeoutMs) {

        if (m_cancel && m_cancel->load())
            return RecvStatus::Cancelled;

        const int left = timeoutMs - int(clock.elapsed());
        if (left <= 0)
            break;

        QByteArray rep;
        const RecvStatus st = readReport(&rep, qMin(left, 250));
        if (st == RecvStatus::Error)
            return st;
        if (st == RecvStatus::Timeout)
            continue;

        const bool isFrameStart =
            rep.size() >= int(sizeof(kFrameStart)) &&
            std::memcmp(rep.constData(), kFrameStart, sizeof(kFrameStart)) == 0;

        const bool isContinuation =
            !isFrameStart && rep.size() >= 1 && rep[0] == '?' && !m_rxBuf.isEmpty();

        if (isFrameStart) {
            m_rxBuf = rep.mid(3);          // "MW" + ...
            m_rxExpected = -1;
        } else if (isContinuation) {
            m_rxBuf.append(rep.mid(1));
        } else {
            if (m_verbose)
                qDebug() << "[MwHid] ignoring stray report of" << rep.size() << "bytes";
            continue;
        }

        if (m_rxExpected < 0 && m_rxBuf.size() >= kHdrLen) {
            const quint32 plen = quint32(quint8(m_rxBuf[4]))
                               | (quint32(quint8(m_rxBuf[5])) << 8)
                               | (quint32(quint8(m_rxBuf[6])) << 16)
                               | (quint32(quint8(m_rxBuf[7])) << 24);
            const quint32 total = quint32(kHdrLen) + plen + quint32(kCrcLen);

            if (plen > kMaxPayload || total > kMaxMsg) {
                if (m_verbose)
                    qDebug() << "[MwHid] dropping oversized frame, plen =" << plen;
                m_rxBuf.clear();
                m_rxExpected = -1;
                continue;
            }
            m_rxExpected = int(total);
        }

        if (m_rxExpected > 0 && m_rxBuf.size() >= m_rxExpected) {
            *out = m_rxBuf.left(m_rxExpected);
            // Хвост — нулевой паддинг последнего report'а. Сбрасываем целиком,
            // чтобы «заблудившийся» report с '?' не приклеился к мёртвому буферу.
            m_rxBuf.clear();
            m_rxExpected = -1;

            if (m_verbose)
                logRx(*out);
            return RecvStatus::Ok;
        }
    }

    return RecvStatus::Timeout;
}

// ---------------------------------------------------------------------------

void Hid::logTx(const QByteArray &msg)
{
    const QByteArray hex = msg.toHex(' ');
    qDebug().noquote() << QStringLiteral("[MwHid] TX %1 bytes:").arg(msg.size())
                       << hex.left(200) + (hex.size() > 200 ? "…" : "");
}

void Hid::logRx(const QByteArray &msg)
{
    const QByteArray hex = msg.toHex(' ');
    qDebug().noquote() << QStringLiteral("[MwHid] RX %1 bytes:").arg(msg.size())
                       << hex.left(200) + (hex.size() > 200 ? "…" : "");
}

} // namespace MwLink

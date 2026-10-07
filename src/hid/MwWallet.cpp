// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// Реализация клиента протокола mwlink.

#include "MwWallet.h"

#include <QDebug>
#include <QElapsedTimer>

namespace MwLink {

namespace {

inline quint32 readU32LE(const QByteArray &buf, int off)
{
    return  quint32(quint8(buf[off]))
          | (quint32(quint8(buf[off + 1])) << 8)
          | (quint32(quint8(buf[off + 2])) << 16)
          | (quint32(quint8(buf[off + 3])) << 24);
}

inline quint16 readU16LE(const QByteArray &buf, int off)
{
    return  quint16(quint8(buf[off]))
          | (quint16(quint8(buf[off + 1])) << 8);
}

inline QString cstr(const QByteArray &buf, int off, int len)
{
    QByteArray s = buf.mid(off, len);
    int z = s.indexOf('\0');
    if (z >= 0) s = s.left(z);
    return QString::fromUtf8(s);
}

constexpr int kInfoLen   = 128;
constexpr int kStatusLen = 48;

} // namespace

Wallet::Wallet(QObject *parent)
    : QObject(parent)
{
}

Wallet::~Wallet()
{
    close();
}

// ---------------------------------------------------------------------------

bool Wallet::open(quint16 vid, quint16 pid)
{
    m_lastError.clear();
    m_deviceError.clear();
    QString best;   // самая информативная ошибка

    auto tryOpen = [&](quint16 p) {
        if (m_hid.open(vid, p))
            return true;
        const QString e = m_hid.lastError();
        if (best.isEmpty() || !e.contains(QStringLiteral("not found")))
            best = e;
        return false;
    };

    if (pid != 0 && tryOpen(pid))
        return true;

    for (int i = 0; i < kNumPids; ++i) {
        if (kUsbPids[i] == pid)
            continue;
        if (tryOpen(kUsbPids[i]))
            return true;
    }

    m_lastError = best.isEmpty()
        ? QStringLiteral("HID device %1 not found").arg(vid, 4, 16, QChar('0'))
        : best;
    return false;
}

void Wallet::close()
{
    m_hid.close();
    m_lastError.clear();
    m_deviceError.clear();
}

// ---------------------------------------------------------------------------

bool Wallet::call(quint8 cmd, quint8 arg, const QByteArray &payload,
                  quint8 *outCmd, quint8 *outArg, QByteArray *outPayload,
                  int timeoutMs)
{
    m_lastError.clear();

    if (!m_hid.isOpen()) {
        m_lastError = QStringLiteral("HID device is not open");
        return false;
    }

    if (m_verbose)
        qDebug() << "[MwWallet] TX cmd=" << cmd << "arg=" << arg
                 << "payload=" << payload.size() << "bytes";

    const QByteArray frame = buildMsg(cmd, arg, payload);
    if (frame.isEmpty()) {
        m_lastError = QStringLiteral("payload too large (%1 > %2)")
            .arg(payload.size()).arg(kMaxPayload);
        return false;
    }

    // Запоздавший ответ на предыдущую команду не должен сойти за ответ на эту,
    // но LOG-события (причина отказа и т.п.) терять нельзя.
    flushPending();

    if (!m_hid.send(frame)) {
        m_lastError = m_hid.lastError();
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("HID write failed");
        return false;
    }

    QByteArray resp;
    switch (recvResponse(&resp, timeoutMs)) {
    case RecvStatus::Ok:
        break;
    case RecvStatus::Timeout:
        m_lastError = QStringLiteral("timeout waiting for response");
        return false;
    case RecvStatus::Cancelled:
        m_lastError = QStringLiteral("Cancelled");
        return false;
    case RecvStatus::Error:
    default:
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("HID read failed");
        return false;
    }

    quint8 rcmd = 0, rarg = 0;
    QByteArray rpay;
    if (!parseMsg(resp, &rcmd, &rarg, &rpay)) {
        m_lastError = QStringLiteral("malformed response frame");
        return false;
    }

    if (m_verbose)
        qDebug() << "[MwWallet] RX cmd=" << rcmd << "arg=" << rarg
                 << "payload=" << rpay.size() << "bytes";

    if (rcmd == RspError) {
        const QString text = QString::fromUtf8(rpay);
        m_lastError = text.isEmpty()
            ? QString::fromLatin1(errName(rarg))
            : QStringLiteral("%1: %2").arg(QString::fromLatin1(errName(rarg)), text);
        emit errorReceived(rarg, text);
        return false;
    }

    if (outCmd)     *outCmd = rcmd;
    if (outArg)     *outArg = rarg;
    if (outPayload) *outPayload = rpay;
    return true;
}

// ---------------------------------------------------------------------------

RecvStatus Wallet::recvResponse(QByteArray *out, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();

    while (clock.elapsed() < timeoutMs) {
        const int left = timeoutMs - int(clock.elapsed());
        if (left <= 0)
            break;

        QByteArray frame;
        const RecvStatus st = m_hid.recv(&frame, left);
        if (st == RecvStatus::Error) {
            m_lastError = m_hid.lastError();
            return st;
        }
        if (st != RecvStatus::Ok)
            return st;   // Timeout / Cancelled

        quint8 cmd = 0, arg = 0;
        QByteArray payload;
        if (!parseMsg(frame, &cmd, &arg, &payload)) {
            if (m_verbose)
                qDebug() << "[MwWallet] dropping malformed frame";
            continue;
        }

        if (handleEvent(cmd, arg, payload))
            continue;

        *out = frame;
        return RecvStatus::Ok;
    }
    return RecvStatus::Timeout;
}

bool Wallet::handleEvent(quint8 cmd, quint8 arg, const QByteArray &payload)
{
    if (cmd == EvtLog) {
        const QString text = QString::fromUtf8(payload);
        if (arg == LogError)
            m_deviceError = text;
        emit logReceived(arg, text);
        if (m_verbose)
            qDebug() << "[MwWallet] device log [" << logLevelChar(arg) << "]:" << text;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

bool Wallet::ping(int *rttMs)
{
    m_lastError.clear();
    m_deviceError.clear();

    QElapsedTimer clock;
    clock.start();

    quint8 cmd = 0;
    QByteArray echo;
    if (!call(CmdPing, 0, QByteArrayLiteral("ping"), &cmd, nullptr, &echo, 5000))
        return false;

    if (cmd != RspPong) {
        m_lastError = QStringLiteral("bad PONG (cmd=%1)").arg(cmd);
        return false;
    }
    if (rttMs)
        *rttMs = int(clock.elapsed());
    return true;
}

bool Wallet::parseInfo(const QByteArray &p, Info *info)
{
    if (p.size() < kInfoLen) {
        m_lastError = QStringLiteral("INFO too short (%1 < %2)")
            .arg(p.size()).arg(kInfoLen);
        return false;
    }

    info->proto       = quint8(p[0]);
    info->caps        = quint8(p[1]);
    info->state       = quint8(p[2]);
    info->network     = quint8(p[3]);
    info->maxPayload  = readU32LE(p, 4);
    info->wallets     = quint8(p[8]);
    info->unlocked    = (p[9] != 0);
    info->passphrase  = (p[10] != 0);
    info->kinds       = quint8(p[11]);

    info->fw          = cstr(p, 12, 16);
    info->board       = cstr(p, 28, 32);
    info->wallet      = cstr(p, 60, 32);

    info->resetReason = quint8(p[92]);
    info->crashOp     = quint8(p[93]);
    info->crashStage  = quint8(p[94]);
    info->crashFlags  = quint8(p[95]);
    info->crashStack  = readU16LE(p, 96);

    if (info->proto < kProtoVersion) {
        m_lastError = QStringLiteral("device speaks protocol %1; this client needs %2")
            .arg(info->proto).arg(kProtoVersion);
        return false;
    }
    return true;
}

bool Wallet::info(Info *info)
{
    m_lastError.clear();
    m_deviceError.clear();

    if (!info) {
        m_lastError = QStringLiteral("info: null output");
        return false;
    }

    quint8 cmd = 0;
    QByteArray payload;
    if (!call(CmdInfo, 0, {}, &cmd, nullptr, &payload, 5000))
        return false;

    if (cmd != RspInfo) {
        m_lastError = QStringLiteral("bad INFO response (cmd=%1)").arg(cmd);
        return false;
    }
    return parseInfo(payload, info);
}

bool Wallet::parseStatus(const QByteArray &p, Status *status)
{
    if (p.size() < kStatusLen) {
        m_lastError = QStringLiteral("STATUS too short (%1 < %2)")
            .arg(p.size()).arg(kStatusLen);
        return false;
    }

    for (int i = 0; i < N_Kinds; ++i)
        status->inbox[i]  = readU32LE(p, i * 4);
    for (int i = 0; i < N_Kinds; ++i)
        status->outbox[i] = readU32LE(p, 20 + i * 4);

    status->state    = quint8(p[40]);
    status->request  = quint8(p[41]);
    status->accept   = quint8(p[42]);
    status->reserved = quint8(p[43]);
    status->seq      = readU32LE(p, 44);
    return true;
}

bool Wallet::status(Status *status)
{
    m_lastError.clear();
    m_deviceError.clear();

    if (!status) {
        m_lastError = QStringLiteral("status: null output");
        return false;
    }

    quint8 cmd = 0;
    QByteArray payload;
    if (!call(CmdStatus, 0, {}, &cmd, nullptr, &payload, 5000))
        return false;

    if (cmd != RspStatus) {
        m_lastError = QStringLiteral("bad STATUS response (cmd=%1)").arg(cmd);
        return false;
    }
    return parseStatus(payload, status);
}

bool Wallet::put(Kind kind, const QByteArray &data, Kind *storedAs, int timeoutMs)
{
    m_lastError.clear();
    m_deviceError.clear();
    if (quint32(data.size()) > kMaxPayload) {
        m_lastError = QStringLiteral("file too large (%1 > %2)")
            .arg(data.size()).arg(kMaxPayload);
        return false;
    }

    quint8 cmd = 0, arg = 0;
    if (!call(CmdPut, quint8(kind), data, &cmd, &arg, nullptr, timeoutMs))
        return false;

    if (cmd != RspAck) {
        m_lastError = QStringLiteral("bad PUT response (cmd=%1)").arg(cmd);
        return false;
    }
    if (storedAs)
        *storedAs = Kind(arg);
    return true;
}

bool Wallet::get(Kind kind, QByteArray *out, int timeoutMs)
{
    m_lastError.clear();
    m_deviceError.clear();

    if (!out) {
        m_lastError = QStringLiteral("get: null output");
        return false;
    }

    quint8 cmd = 0, arg = 0;
    QByteArray payload;
    if (!call(CmdGet, quint8(kind), {}, &cmd, &arg, &payload, timeoutMs))
        return false;

    if (cmd != RspFile) {
        m_lastError = QStringLiteral("bad FILE response (cmd=%1)").arg(cmd);
        return false;
    }
    if (Kind(arg) != kind) {
        m_lastError = QStringLiteral("kind mismatch: asked %1, got %2")
            .arg(int(kind)).arg(int(arg));
        return false;
    }

    *out = payload;
    return true;
}

void Wallet::flushPending()
{
    QByteArray frame;
    while (m_hid.recv(&frame, 1) == RecvStatus::Ok) {
        quint8 cmd = 0, arg = 0;
        QByteArray payload;
        if (parseMsg(frame, &cmd, &arg, &payload))
            handleEvent(cmd, arg, payload);
    }
}

RecvStatus Wallet::pumpEvents(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QByteArray frame;
        const RecvStatus st = m_hid.recv(&frame, ms - int(clock.elapsed()));
        if (st == RecvStatus::Timeout)
            break;
        if (st != RecvStatus::Ok)
            return st;

        quint8 cmd = 0, arg = 0;
        QByteArray payload;
        if (parseMsg(frame, &cmd, &arg, &payload))
            handleEvent(cmd, arg, payload);
    }
    return RecvStatus::Timeout;
}

bool Wallet::waitForResult(Kind resultKind, Kind inputKind, QByteArray *out,
                           int timeoutMs, int pollMs)
{
    m_lastError.clear();
    m_deviceError.clear();
    if (!out) {
        m_lastError = QStringLiteral("waitForResult: null output");
        return false;
    }

    QElapsedTimer clock;
    clock.start();
    int idlePolls = 0;

    while (clock.elapsed() < timeoutMs) {
        Status st;
        if (!status(&st))
            return false;   // lastError: Cancelled / timeout / ошибка транспорта

        if (st.hasOutbox(resultKind))
            return get(resultKind, out, 60000);

        if (st.state == StateLocked || st.state == StateMenu) {
            m_lastError = QStringLiteral(
                "The device was locked or the wallet was closed before the result was ready");
            return false;
        }

        // Файл забран устройством (inbox пуст), оно снова свободно, а результата нет.
        const bool consumed = inputKind < N_Kinds && !st.hasInbox(inputKind);
        if (st.state == StateWallet && consumed) {
            if (++idlePolls >= 2) {
                m_lastError = m_deviceError.isEmpty()
                    ? QStringLiteral("Declined or refused on the device")
                    : QStringLiteral("Declined or refused on the device: %1").arg(m_deviceError);
                return false;
            }
        } else {
            idlePolls = 0;
        }

        switch (pumpEvents(pollMs)) {
        case RecvStatus::Cancelled:
            m_lastError = QStringLiteral("Cancelled");
            return false;
        case RecvStatus::Error:
            if (m_lastError.isEmpty())
                m_lastError = m_hid.lastError();
            return false;
        default:
            break;
        }
    }

    m_lastError = QStringLiteral("Timed out waiting for confirmation on the device");
    return false;
}

bool Wallet::clear(Kind kind)
{
    m_lastError.clear();
    m_deviceError.clear();

    quint8 cmd = 0;
    if (!call(CmdClear, quint8(kind), {}, &cmd, nullptr, nullptr, 5000))
        return false;

    if (cmd != RspAck) {
        m_lastError = QStringLiteral("bad CLEAR response (cmd=%1)").arg(cmd);
        return false;
    }
    return true;
}

bool Wallet::request(Req what)
{
    m_lastError.clear();
    m_deviceError.clear();

    quint8 cmd = 0;
    if (!call(CmdReq, quint8(what), {}, &cmd, nullptr, nullptr, 5000))
        return false;

    if (cmd != RspAck) {
        m_lastError = QStringLiteral("bad REQ response (cmd=%1)").arg(cmd);
        return false;
    }
    return true;
}

bool Wallet::exchangeFile(Kind kindIn, const QByteArray &data,
                          Kind kindOut, QByteArray *out,
                          int putTimeoutMs, int getTimeoutMs)
{
    Kind stored = KindAll;
    if (!put(kindIn, data, &stored, putTimeoutMs))
        return false;

    if (m_verbose && kindIn != KindAuto && stored != kindIn)
        qDebug() << "[MwWallet] device stored input as"
                 << kindName(stored) << "(asked" << kindName(kindIn) << ")";

    // GET не блокируется: ждём готовности результата опросом STATUS.
    return waitForResult(kindOut, stored, out, getTimeoutMs);
}

} // namespace MwLink

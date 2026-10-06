// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// Реализация протокола mwlink (совместимо с mwlink.py).
// Только HID — никакого serial.

#include "MwLink.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>

#include <zlib.h>

#include <cstring>

namespace MwLink {

// ===========================================================================
// Внутренние хелперы
// ===========================================================================

namespace {

// Собрать little-endian u32 в QByteArray (4 байта).
inline void appendU32LE(QByteArray &out, quint32 v)
{
    out.append(char( v        & 0xFF));
    out.append(char((v >> 8)  & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

// Прочитать little-endian u32 из буфера по смещению.
// Предполагается, что buf содержит минимум off+4 байта.
inline quint32 readU32LE(const QByteArray &buf, int off)
{
    return  quint32(quint8(buf[off]))
          | (quint32(quint8(buf[off + 1])) << 8)
          | (quint32(quint8(buf[off + 2])) << 16)
          | (quint32(quint8(buf[off + 3])) << 24);
}

// C-строка из фиксированного поля QByteArray (до первого '\0').
inline QString cstr(const QByteArray &buf, int off, int len)
{
    QByteArray s = buf.mid(off, len);
    int z = s.indexOf('\0');
    if (z >= 0) s = s.left(z);
    return QString::fromUtf8(s);
}

// CRC32 (zlib) от QByteArray.
inline quint32 crc32Of(const QByteArray &data)
{
    return quint32(crc32(0,
                         reinterpret_cast<const Bytef*>(data.constData()),
                         uInt(data.size())));
}

} // namespace

// ===========================================================================
// buildMsg
// ===========================================================================

QByteArray buildMsg(quint8 cmd, quint8 arg, const QByteArray &payload)
{
    if (quint32(payload.size()) > kMaxPayload)
        return {};

    QByteArray body;
    body.reserve(kHdrLen + payload.size());

    // Magic
    body.append(kMagic[0]);
    body.append(kMagic[1]);

    // cmd / arg
    body.append(char(cmd));
    body.append(char(arg));

    // len (u32 LE)
    appendU32LE(body, quint32(payload.size()));

    // payload
    body.append(payload);

    // CRC32 по всему body (без поля CRC)
    quint32 crc = crc32Of(body);

    QByteArray out = body;
    appendU32LE(out, crc);
    return out;
}

// ===========================================================================
// msgTotalLen
// ===========================================================================

int msgTotalLen(const QByteArray &head)
{
    if (head.size() < kHdrLen)
        return -1;
    if (head[0] != kMagic[0] || head[1] != kMagic[1])
        return -1;

    quint32 plen = readU32LE(head, 4);
    if (plen > kMaxPayload)
        return -1;

    quint32 total = quint32(kHdrLen) + plen + quint32(kCrcLen);
    if (total > kMaxMsg)
        return -1;

    return int(total);
}

// ===========================================================================
// parseMsg
// ===========================================================================

bool parseMsg(const QByteArray &frame,
              quint8 *cmd,
              quint8 *arg,
              QByteArray *payload)
{
    if (frame.size() < kHdrLen + kCrcLen)
        return false;
    if (frame[0] != kMagic[0] || frame[1] != kMagic[1])
        return false;

    quint32 plen = readU32LE(frame, 4);
    if (plen > kMaxPayload)
        return false;

    const int expected = kHdrLen + int(plen) + kCrcLen;
    if (frame.size() != expected)
        return false;

    // CRC32 по всему до поля CRC
    QByteArray body = frame.left(kHdrLen + int(plen));
    quint32 want = readU32LE(frame, kHdrLen + int(plen));
    quint32 got  = crc32Of(body);
    if (want != got)
        return false;

    if (cmd)     *cmd = quint8(frame[2]);
    if (arg)     *arg = quint8(frame[3]);
    if (payload) *payload = frame.mid(kHdrLen, int(plen));
    return true;
}

// ===========================================================================
// detectKind
// ===========================================================================

Kind detectKind(const QByteArray &data)
{
    // UTF-8 BOM иногда присутствует — срезаем.
    QByteArray d = data;
    if (d.startsWith("\xEF\xBB\xBF"))
        d.remove(0, 3);

    // Magic-строки — те же, что в mwlink.py MAGICS.
    static const struct {
        const char *magic;
        Kind        kind;
    } table[] = {
        { "Monero output export",    KOutputs    },
        { "Monero unsigned tx set",  KUnsignedTx },
        { "Monero key image export", KKeyImages  },
        { "Monero signed tx set",    KSignedTx   },
    };

    for (const auto &m : table) {
        const int len = int(std::strlen(m.magic));
        if (d.size() >= len && std::memcmp(d.constData(), m.magic, len) == 0)
            return m.kind;
    }
    return KindAll; // не распознано
}

// ===========================================================================
// resultName
// ===========================================================================
//
// Повторяет логику mwlink.py result_name():
//
//   <prefix><ts>_outputs            -> <prefix><now>_keyImages
//   <prefix><ts>_unsigned_monero_tx -> <prefix><now>_signed_monero_tx
//   <любое>                         -> <base>_<now>_<kind>
//
// now == 0 означает "использовать текущее время".

QString resultName(const QString &inputName, Kind outKind, qint64 now)
{
    if (now == 0)
        now = QDateTime::currentSecsSinceEpoch();

    const QString ts  = QString::number(now);
    const QString base = QFileInfo(inputName).fileName();

    // Регулярки как в Python: 9..11 цифр.
    static const QRegularExpression reTsOutputs(
        QStringLiteral("^(?P<pre>.*?)(?P<ts>\\d{9,11})_outputs(?P<ext>\\.[A-Za-z0-9]{1,8})?$"));

    static const QRegularExpression reOutputs(
        QStringLiteral("^(?P<pre>.*?)outputs(?P<ext>\\.[A-Za-z0-9]{1,8})?$"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression reTsUnsigned(
        QStringLiteral("^(?P<pre>.*?)(?P<ts>\\d{9,11})_unsigned_monero_tx(?P<ext>\\.[A-Za-z0-9]{1,8})?$"));

    static const QRegularExpression reUnsignedAny(
        QStringLiteral("\\d{9,11}"));

    switch (outKind) {

    case KKeyImages: {
        // borya-view2_1789637203_outputs -> borya-view2_<now>_keyImages
        auto m = reTsOutputs.match(base);
        if (m.hasMatch()) {
            return m.captured("pre")
                 + ts
                 + QStringLiteral("_keyImages")
                 + m.captured("ext");
        }

        // borya-view2_outputs -> borya-view2_<now>_keyImages
        auto m2 = reOutputs.match(base);
        if (m2.hasMatch() && !base.isEmpty()) {
            return m2.captured("pre")
                 + ts
                 + QStringLiteral("_keyImages")
                 + m2.captured("ext");
        }

        // Fallback
        return QStringLiteral("%1_%2_keyImages")
            .arg(base.isEmpty() ? QStringLiteral("wallet") : base, ts);
    }

    case KSignedTx: {
        // <prefix><ts>_unsigned_monero_tx -> <prefix><now>_signed_monero_tx
        auto m = reTsUnsigned.match(base);
        if (m.hasMatch()) {
            return m.captured("pre")
                 + ts
                 + QStringLiteral("_signed_monero_tx")
                 + m.captured("ext");
        }

        // Если в имени есть "unsigned" и 9..11-значный ts — заменить.
        if (base.contains(QStringLiteral("unsigned"))) {
            QString name = base;
            const auto tm = reUnsignedAny.match(name);   // только первый ts
            if (tm.hasMatch())
                name.replace(tm.capturedStart(), tm.capturedLength(), ts);
            name.replace(QStringLiteral("unsigned"),
                         QStringLiteral("signed"));
            return name;
        }

        // Fallback
        return QStringLiteral("%1_%2_signed_monero_tx")
            .arg(base.isEmpty() ? QStringLiteral("tx") : base, ts);
    }

    case KExport: {
        // К имени добавим "export" и .json
        QString safe = base;
        safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")),
                     QStringLiteral("_"));
        if (safe.isEmpty()) safe = QStringLiteral("wallet");
        return QStringLiteral("%1_%2_export.json").arg(safe, ts);
    }

    case KOutputs:
    case KUnsignedTx:
    default:
        // Общий случай: <base>_<now>_<kind>
        return QStringLiteral("%1_%2_%3")
            .arg(base.isEmpty() ? QStringLiteral("file") : base,
                 ts,
                 QString::fromLatin1(kindName(outKind)));
    }
}

// ===========================================================================
// describeReset
// ===========================================================================

QString describeReset(const Info &info)
{
    QString text = QStringLiteral("last reset: %1")
        .arg(QString::fromLatin1(resetReasonName(info.resetReason)));

    // crash_flags: бит 0 — есть crash_op/crash_stage,
    //              бит 1 — есть crash_stack.
    constexpr quint8 kFlagValid = 0x01;
    constexpr quint8 kFlagStack = 0x02;

    if (info.crashFlags & kFlagValid) {
        // Имена операций/стадий — из mwlink.py CRASH_OPS / CRASH_STAGES.
        static const char *ops[] = {
            "?", "signing", "key images", "opening a wallet", "wallet export"
        };
        static const char *stages[] = {
            "?", "loading", "review", "keys", "CLSAG",
            "Bulletproofs+", "sealing", "output"
        };

        const char *op    = (info.crashOp    < 5) ? ops[info.crashOp]       : "op?";
        const char *stage = (info.crashStage < 8) ? stages[info.crashStage] : "stage?";

        text += QStringLiteral(" during %1 / %2")
            .arg(QString::fromLatin1(op),
                 QString::fromLatin1(stage));
    }

    if (info.crashFlags & kFlagStack) {
        text += QStringLiteral(", crypto stack min free %1 B")
            .arg(info.crashStack);
    }

    return text;
}

} // namespace MwLink
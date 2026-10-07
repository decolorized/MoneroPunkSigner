// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// Протокол 3 (mwlink): обмен файлами Monero через HID.
// Совместим с mwlink.py (host-side courier) и прошивкой ColdPunk (link.h).
//
// Кадр сообщения (20 байт заголовка):
//   magic:8 = 4D 57 50 4B C7 3A 5E 91 ("MWPK" + 4 случайных байта)
//   version:1 = 3 | cmd:1 | arg:1 | reserved:1 = 0 | len:4 (LE)
//   hdr_crc32:4 (LE) — CRC32 байтов [0, 16)
//   payload:len | crc32:4 (LE) — CRC32 всего, что до него
//
// Транспорт — только HID. Формат HID report'ов:
//   Первый report:  [report_id=0x01] [ '?' '#' '#' + первые 60 байт сообщения ]
//   Продолжение:    [report_id=0x01] [ '?' + следующие 62 байта сообщения ]
// Начало кадра — report "?##", за которым целый заголовок с верными magic,
// версией и hdr_crc32 (весь заголовок помещается в первый report). Данные
// продолжения, случайно начинающиеся с "##" или даже с magic, кадр не рвут:
// вероятность совпадения CRC заголовка ~2^-32 поверх 64 бит magic.
// В протоколе 2 маркером было только "?##MW" — это и было источником коллизий.

#ifndef FEATHER_MWLINK_H
#define FEATHER_MWLINK_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <cstdint>

namespace MwLink {

// ===========================================================================
// Идентификация USB-устройства (как в mwlink.py)
// ===========================================================================

constexpr quint16 kUsbVid    = 0x303A;
constexpr quint16 kUsbPids[] = { 0x4024, 0x4025, 0x1001 };
constexpr int     kNumPids   = int(sizeof(kUsbPids) / sizeof(kUsbPids[0]));

// Интерфейс устройства: vendor-defined HID, usage_page 0xFF00.
// Именно так mwlink.py (find_hid_path) находит нужный интерфейс.
constexpr quint16 kHidUsagePage = 0xFF00;

// ===========================================================================
// HID framing (точно как в mwlink.py)
// ===========================================================================

constexpr quint8 kHidReportId  = 0x01;
constexpr int    kHidData      = 63;                // байт данных в report
constexpr int    kHidFirstData = kHidData - 3;      // 60: первый report (после "?##")
constexpr int    kHidContData  = kHidData - 1;      // 62: продолжения (после "?")

// Префикс первого report'а кадра.
constexpr char kHidStart[3] = { '?', '#', '#' };

// ===========================================================================
// Формат сообщения (COBS не нужен — HID сам разбивает на report'ы)
// ===========================================================================

constexpr quint8  kProtoVersion = 3;
constexpr int     kMagicLen     = 8;
constexpr unsigned char kMagic[kMagicLen] = { 0x4D, 0x57, 0x50, 0x4B, 0xC7, 0x3A, 0x5E, 0x91 };
constexpr int     kHdrCrcOff    = 16;               // magic(8) ver cmd arg res len(4)
constexpr int     kHdrLen       = 20;               // + hdr_crc32(4)
constexpr int     kCrcLen       = 4;
constexpr quint32 kMaxPayload   = 256 * 1024;       // 256 KiB, как в mwlink.py
constexpr quint32 kMaxMsg       = kHdrLen + kMaxPayload + kCrcLen;

// ===========================================================================
// Команды (host → device)
// ===========================================================================

enum Cmd : quint8 {
    CmdPing   = 1,
    CmdInfo   = 2,
    CmdPut    = 3,   // положить файл
    CmdGet    = 4,   // забрать результат
    CmdStatus = 5,   // статус (inbox/outbox)
    CmdClear  = 6,   // очистить inbox/outbox
    CmdReq    = 7,   // запрос (адрес, view key, ...)
};

// ===========================================================================
// Ответы (device → host)
// ===========================================================================

enum Rsp : quint8 {
    RspPong   = 0x81,
    RspInfo   = 0x82,
    RspAck    = 0x83,
    RspFile   = 0x84,
    RspStatus = 0x85,
    RspError  = 0xFF,
    EvtLog    = 0x90,   // асинхронный лог от устройства
};

// ===========================================================================
// Типы файлов (kind), как в mwlink.py KINDS
// ===========================================================================

enum Kind : quint8 {
    KOutputs    = 0,   // "Monero output export"
    KKeyImages  = 1,   // "Monero key image export"
    KUnsignedTx = 2,   // "Monero unsigned tx set"
    KSignedTx   = 3,   // "Monero signed tx set"
    KExport     = 4,   // wallet_export (JSON: view key, address)
    N_Kinds     = 5,

    KindAll  = 0xFF,   // для CMD_CLEAR: очистить всё
    KindAuto = 0xFE,   // для CMD_PUT: устройство само классифицирует
};

// Имена kind'ов для логов и UI.
inline const char* kindName(Kind k)
{
    switch (k) {
        case KOutputs:    return "outputs";
        case KKeyImages:  return "keyimages";
        case KUnsignedTx: return "unsigned_tx";
        case KSignedTx:   return "signed_tx";
        case KExport:     return "wallet_export";
        case KindAll:     return "all";
        case KindAuto:    return "auto";
        default:          return "?";
    }
}

// ===========================================================================
// Состояния устройства (INFO.state / STATUS.state)
// ===========================================================================

enum State : quint8 {
    StateLocked = 0,
    StateMenu   = 1,
    StateWallet = 2,
    StateBusy   = 3,
};

inline const char* stateName(State s)
{
    switch (s) {
        case StateLocked: return "locked";
        case StateMenu:   return "menu";
        case StateWallet: return "wallet open";
        case StateBusy:   return "busy";
        default:          return "?";
    }
}

// ===========================================================================
// Запросы (CMD_REQ)
// ===========================================================================

enum Req : quint8 {
    ReqAddress  = 1,
    ReqViewOnly = 2,
};

// ===========================================================================
// Коды ошибок (RSP_ERROR.arg)
// ===========================================================================

enum Err : quint8 {
    ErrBadCommand  = 1,
    ErrBadKind     = 2,
    ErrTooBig      = 3,
    ErrNotReady    = 4,
    ErrNoMemory    = 5,
    ErrCrc         = 6,
    ErrBadLength   = 7,
    ErrBadMagic    = 8,
    ErrUnknown     = 9,
    ErrBusy        = 10,
    ErrNotAccepted = 11,
};

inline const char* errName(quint8 code)
{
    switch (code) {
        case ErrBadCommand:  return "bad command";
        case ErrBadKind:     return "bad file kind";
        case ErrTooBig:      return "too big";
        case ErrNotReady:    return "not ready";
        case ErrNoMemory:    return "no memory";
        case ErrCrc:         return "crc mismatch";
        case ErrBadLength:   return "bad length";
        case ErrBadMagic:    return "bad magic";
        case ErrUnknown:     return "unrecognised file";
        case ErrBusy:        return "busy";
        case ErrNotAccepted: return "not accepted";
        default:             return "error";
    }
}

// Уровни лога (EvtLog.arg), как в mwlink.py LOG_LEVELS
enum LogLevel : quint8 {
    LogProgress = 0,   // 'P'
    LogError    = 1,   // 'E'
    LogInfo     = 2,   // 'I'
    LogDebug    = 3,   // 'D'
};

inline char logLevelChar(quint8 level)
{
    switch (level) {
        case LogProgress: return 'P';
        case LogError:    return 'E';
        case LogInfo:     return 'I';
        case LogDebug:    return 'D';
        default:          return '?';
    }
}

// ===========================================================================
// Флаги возможностей (INFO.caps)
// ===========================================================================

enum Caps : quint8 {
    CapSerial   = 0x01,
    CapHid      = 0x02,
    CapMsc      = 0x04,
    CapAutoKind = 0x08,
    CapReq      = 0x10,
};

// ===========================================================================
// Причины последней перезагрузки (INFO.reset_reason)
// ===========================================================================

enum ResetReason : quint8 {
    ResetUnknown    = 0,
    ResetPowerOn    = 1,
    ResetPin        = 2,
    ResetSoftware   = 3,
    ResetCrash      = 4,
    ResetIntWdt     = 5,
    ResetTaskWdt    = 6,
    ResetWatchdog   = 7,
    ResetDeepSleep  = 8,
    ResetBrownout   = 9,
    ResetUsbJtag    = 10,
    ResetOther      = 11,
};

inline const char* resetReasonName(quint8 code)
{
    switch (code) {
        case ResetUnknown:   return "unknown";
        case ResetPowerOn:   return "power-on";
        case ResetPin:       return "reset pin";
        case ResetSoftware:  return "software restart";
        case ResetCrash:     return "crash (panic)";
        case ResetIntWdt:    return "interrupt watchdog";
        case ResetTaskWdt:   return "task watchdog";
        case ResetWatchdog:  return "watchdog";
        case ResetDeepSleep: return "deep sleep wake";
        case ResetBrownout:  return "brownout";
        case ResetUsbJtag:   return "USB/JTAG reset";
        case ResetOther:     return "other";
        default:             return "unknown";
    }
}

// Сетевые режимы (INFO.network)
enum Network : quint8 {
    NetMainnet  = 0,
    NetTestnet  = 1,
    NetStagenet = 2,
};

inline const char* networkName(quint8 code)
{
    switch (code) {
        case NetMainnet:  return "mainnet";
        case NetTestnet:  return "testnet";
        case NetStagenet: return "stagenet";
        default:          return "?";
    }
}

// ===========================================================================
// INFO — ответ на CMD_INFO (128 байт, как в mwlink.py)
// ===========================================================================
//
// Раскладка (из mwlink.py, Wallet.info()):
//   [0]      proto
//   [1]      caps
//   [2]      state
//   [3]      network
//   [4..7]   max_payload (u32 LE)
//   [8]      wallets
//   [9]      unlocked (bool)
//   [10]     passphrase (bool)
//   [11]     kinds (битовая маска доступных kind'ов)
//   [12..27] fw       (C-строка, 16 байт)
//   [28..59] board    (C-строка, 32 байта)
//   [60..91] wallet   (C-строка, 32 байта)
//   [92]     reset_reason
//   [93]     crash_op
//   [94]     crash_stage
//   [95]     crash_flags
//   [96..97] crash_stack (u16 LE)

struct Info {
    quint8  proto       = 0;
    quint8  caps        = 0;
    quint8  state       = 0;
    quint8  network     = 0;
    quint32 maxPayload  = 0;
    quint8  wallets     = 0;
    bool    unlocked    = false;
    bool    passphrase  = false;
    quint8  kinds       = 0;

    QString fw;
    QString board;
    QString wallet;

    quint8  resetReason = 0;
    quint8  crashOp     = 0;
    quint8  crashStage  = 0;
    quint8  crashFlags  = 0;
    quint16 crashStack  = 0;

    bool isUnlocked() const { return unlocked; }
    bool isWalletOpen() const {
        return state == StateWallet || state == StateBusy;
    }
    bool hasCap(quint8 cap) const { return (caps & cap) != 0; }
};

// ===========================================================================
// STATUS — ответ на CMD_STATUS (48 байт)
// ===========================================================================
//
//   [0..19]   inbox[5]  (u32 LE) — что устройство приняло, но ещё не обработало
//   [20..39]  outbox[5] (u32 LE) — что устройство положило, но хост ещё не забрал
//   [40]      state
//   [41]      request
//   [42]      accept (битовая маска: какие kind'ы устройство готово принять)
//   [43]      reserved
//   [44..47]  seq (u32 LE)

struct Status {
    quint32 inbox[N_Kinds]  = {};
    quint32 outbox[N_Kinds] = {};
    quint8  state   = 0;
    quint8  request = 0;
    quint8  accept  = 0;
    quint8  reserved = 0;
    quint32 seq     = 0;

    bool hasInbox(Kind k)  const { return k < N_Kinds && inbox[k]  > 0; }
    bool hasOutbox(Kind k) const { return k < N_Kinds && outbox[k] > 0; }
    bool accepts(Kind k)   const { return (accept & (1 << k)) != 0; }
};

// ===========================================================================
// Сборка / разбор сообщения
// ===========================================================================

// Собрать кадр: заголовок (magic, версия, cmd, arg, len, hdr_crc32) +
// payload + crc32(LE).
// Возвращает пустой QByteArray, если payload > kMaxPayload.
QByteArray buildMsg(quint8 cmd, quint8 arg, const QByteArray &payload);

// Разобрать кадр. Проверяет magic, длину и CRC32.
// При успехе заполняет cmd/arg/payload (payload может быть пустым).
// Возвращает false при любой ошибке формата.
bool parseMsg(const QByteArray &frame,
              quint8 *cmd,
              quint8 *arg,
              QByteArray *payload);

// Только длина полного сообщения по первым kHdrLen байтам.
// Возвращает -1, если заголовок ещё не полный или некорректен (magic,
// версия, CRC заголовка, длина).
int msgTotalLen(const QByteArray &head);

// Заголовок протокола 3 в buf[off .. off + kHdrLen): magic, версия и CRC
// заголовка сходятся. При успехе отдаёт длину payload (ещё не проверенную).
bool headerValid(const QByteArray &buf, int off, quint32 *plen);

// ===========================================================================
// Определение типа файла по magic (для CMD_PUT с KindAuto)
// ===========================================================================

// Возвращает один из KOutputs / KUnsignedTx / KKeyImages / KSignedTx,
// либо KindAll, если magic не распознан.
Kind detectKind(const QByteArray &data);

// Имена magic-строк (для логов/ошибок).
inline const char* kindMagic(Kind k)
{
    switch (k) {
        case KOutputs:    return "Monero output export";
        case KKeyImages:  return "Monero key image export";
        case KUnsignedTx: return "Monero unsigned tx set";
        case KSignedTx:   return "Monero signed tx set";
        default:          return "";
    }
}

// ===========================================================================
// Утилиты
// ===========================================================================

// Человекочитаемое имя результата, как в mwlink.py (result_name):
//   borya-view2_1789637203_outputs  -> borya-view2_<now>_keyImages
//   1789839233_unsigned_monero_tx    -> 1789839961_signed_monero_tx
QString resultName(const QString &inputName, Kind outKind, qint64 now = 0);

// Причина последнего reset'а в виде строки (для UI).
QString describeReset(const Info &info);

} // namespace MwLink

#endif // FEATHER_MWLINK_H
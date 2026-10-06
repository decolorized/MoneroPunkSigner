// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// HID-транспорт для протокола mwlink (совместим с mwlink.py HidTransport).
//
//   Первый report (64 байта): [report_id=0x01] ['?' '#' '#' + 60 байт сообщения]   (сообщение начинается с "MW" → "?##MW...")
//   Продолжение  (64 байта): [report_id=0x01] ['?' ... 62 байта сообщения]

#ifndef FEATHER_MWHID_H
#define FEATHER_MWHID_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <atomic>

#include "MwLink.h"

struct hid_device_;
typedef struct hid_device_ hid_device;

namespace MwLink {

// Результат приёма. Раньше таймаут и ошибка различались через lastError().
enum class RecvStatus {
    Ok,
    Timeout,
    Error,       // см. lastError()
    Cancelled,   // выставлен флаг отмены
};

class Hid
{
public:
    Hid() = default;
    ~Hid();

    Hid(const Hid &) = delete;
    Hid &operator=(const Hid &) = delete;
    Hid(Hid &&other) noexcept;
    Hid &operator=(Hid &&other) noexcept;

    // Открывает устройство vid/pid с usage_page 0xFF00. Если бэкенд hidapi
    // usage_page не сообщает (libusb → 0), берётся первый интерфейс с usage_page == 0.
    // После открытия вычитывает устаревшие report'ы (drain).
    bool open(quint16 vid, quint16 pid);
    void close();
    bool isOpen() const { return m_dev != nullptr; }
    QString describe() const;

    bool send(const QByteArray &msg);

    // Принять одно сообщение целиком. Проверяет флаг отмены каждые ~250 мс.
    RecvStatus recv(QByteArray *out, int timeoutMs);

    // Выбросить всё, что уже лежит во входной очереди, и сбросить буфер сборки.
    void drain();

    // Флаг отмены (не владеем указателем). nullptr — отмена отключена.
    void setCancelFlag(const std::atomic_bool *flag) { m_cancel = flag; }

    void setVerbose(bool on) { m_verbose = on; }
    bool isVerbose() const { return m_verbose; }
    QString lastError() const { return m_lastError; }

private:
    bool writeReport(const QByteArray &data63);
    RecvStatus readReport(QByteArray *out, int timeoutMs);

    void logTx(const QByteArray &msg);
    void logRx(const QByteArray &msg);

    hid_device *m_dev = nullptr;
    quint16     m_vid = 0;
    quint16     m_pid = 0;

    QByteArray  m_rxBuf;
    int         m_rxExpected = -1;

    const std::atomic_bool *m_cancel = nullptr;

    bool        m_verbose   = false;
    QString     m_lastError;
};

} // namespace MwLink

#endif // FEATHER_MWHID_H

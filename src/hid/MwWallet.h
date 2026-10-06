// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// Клиент протокола mwlink. Feather — мастер, устройство — слейв.
//
// Все команды синхронные и блокирующие — вызывать ТОЛЬКО из фонового
// потока (см. HidOperation). Сигналы эмитятся из этого же потока.
//
// connect()/disconnect() переименованы в open()/close(): прежние имена
// скрывали QObject::connect/disconnect.

#ifndef FEATHER_MWWALLET_H
#define FEATHER_MWWALLET_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>

#include "MwLink.h"
#include "MwHid.h"

namespace MwLink {

class Wallet : public QObject
{
    Q_OBJECT

public:
    explicit Wallet(QObject *parent = nullptr);
    ~Wallet() override;

    Wallet(const Wallet &) = delete;
    Wallet &operator=(const Wallet &) = delete;

    // Открывает HID-устройство. pid == 0 — перебор kUsbPids.
    bool open(quint16 vid = kUsbVid, quint16 pid = 0);
    void close();
    bool isOpen() const { return m_hid.isOpen(); }
    QString describe() const { return m_hid.describe(); }

    QString lastError() const { return m_lastError; }

    // Флаг отмены: блокирующие get()/put() прервутся (≈250 мс) и вернут
    // false с lastError() == "Cancelled". nullptr — отключить.
    void setCancelFlag(const std::atomic_bool *flag) { m_hid.setCancelFlag(flag); }

    void setVerbose(bool on) { m_verbose = on; m_hid.setVerbose(on); }
    bool isVerbose() const { return m_verbose; }

    bool ping(int *rttMs = nullptr);
    bool info(Info *info);
    bool status(Status *status);

    bool put(Kind kind, const QByteArray &data, Kind *storedAs = nullptr,
             int timeoutMs = 60000);
    // GET отвечает СРАЗУ: файл из outbox либо ERROR "not ready" (outbox пуст).
    // Устройство НЕ ждёт подтверждения пользователя внутри GET — для ожидания
    // результата используйте waitForResult().
    bool get(Kind kind, QByteArray *out, int timeoutMs = 60000);

    // Ждёт результат так же, как mwlink.py (Exchange.step): опрашивает STATUS
    // каждые pollMs, и когда outbox[resultKind] > 0 — делает GET.
    // Прерывается с понятной ошибкой, если:
    //   • устройство заблокировано / кошелёк закрыт (state locked/menu);
    //   • файл обработан (inbox[inputKind] пуст, state == wallet), а результата
    //     нет два опроса подряд — пользователь отказался или файл отклонён
    //     (в текст добавляется последняя error-строка из лога устройства);
    //   • истёк timeoutMs; выставлен флаг отмены.
    // Регулярные запросы также держат живым «host active» таймер устройства
    // (оно шлёт LOG-события только тому, кто спрашивал за последние 30 с).
    bool waitForResult(Kind resultKind, Kind inputKind, QByteArray *out,
                       int timeoutMs = 600000, int pollMs = 500);

    // Последняя error-строка (уровень LogError), присланная устройством.
    QString deviceError() const { return m_deviceError; }
    bool clear(Kind kind = KindAll);
    bool request(Req what);

    // put + waitForResult (kindIn может быть KindAuto).
    bool exchangeFile(Kind kindIn, const QByteArray &data,
                      Kind kindOut, QByteArray *out,
                      int putTimeoutMs = 60000, int getTimeoutMs = 300000);

signals:
    void logReceived(quint8 level, const QString &text);
    void errorReceived(quint8 code, const QString &text);

private:
    bool call(quint8 cmd, quint8 arg, const QByteArray &payload,
              quint8 *outCmd, quint8 *outArg, QByteArray *outPayload,
              int timeoutMs);

    // Принять один ответ, пропуская EvtLog.
    RecvStatus recvResponse(QByteArray *out, int timeoutMs);

    bool handleEvent(quint8 cmd, quint8 arg, const QByteArray &payload);

    // Прочитать накопившиеся кадры: события уходят в logReceived, устаревшие
    // ответы отбрасываются (в отличие от Hid::drain() логи не теряются).
    void flushPending();
    // Читать и обрабатывать события до ms миллисекунд.
    RecvStatus pumpEvents(int ms);
    bool parseInfo(const QByteArray &p, Info *info);
    bool parseStatus(const QByteArray &p, Status *status);

    Hid     m_hid;
    QString m_lastError;
    bool    m_verbose = false;
    QString m_deviceError;
};

} // namespace MwLink

#endif // FEATHER_MWWALLET_H

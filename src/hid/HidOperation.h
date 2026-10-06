// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// HidOperation — сценарий обмена с HID-устройством в фоновом потоке.
//
// ПРАВИЛА:
//   • Шаги (addStep) выполняются в ФОНОВОМ потоке. Внутри нельзя трогать
//     UI, Wallet/PendingTransaction Feather и поля мастера. Шаг только
//     обменивается с устройством и кладёт байты в буфер (например,
//     std::shared_ptr<QByteArray>, захваченный лямбдой).
//   • Вся работа с кошельком — в слоте finished() (UI-поток).
//   • addCleanup() — шаги, которые выполняются ВСЕГДА (успех, ошибка,
//     отмена), пока устройство открыто. Нужны для очистки inbox/outbox.
//   • Деструктор отменяет операцию и ЖДЁТ фоновый поток (отмена
//     срабатывает за ≈250 мс, плюс cleanup), поэтому удаление страницы
//     или закрытие мастера во время операции безопасно.
//
// Пример:
//
//   auto ki = std::make_shared<QByteArray>();
//   m_op = new HidOperation(this);
//   m_op->setPayloadSize(blob.size());
//   m_op->addStep("Sending…", [blob](MwLink::Wallet &d){ MwLink::Kind k;
//                                  return d.put(MwLink::KOutputs, blob, &k); });
//   m_op->addStep("Waiting…", [ki](MwLink::Wallet &d){
//                                  // GET не ждёт: опрос STATUS + GET делает waitForResult
//                                  return d.waitForResult(MwLink::KKeyImages, MwLink::KOutputs,
//                                                         ki.get(), 600000); });
//   m_op->addCleanup("Clearing…", [](MwLink::Wallet &d){ return d.clear(MwLink::KKeyImages); });
//
// Протокол прошивки: CLEAR чистит только OUTBOX (inbox host очистить не может),
// поэтому перед PUT очищаем outbox результата (иначе можно забрать старый
// результат), а в cleanup — outbox результата после операции.
//   connect(m_op, &HidOperation::finished, this, [=](bool ok, const QString &err){ ... });
//   m_op->start();

#ifndef FEATHER_HIDOPERATION_H
#define FEATHER_HIDOPERATION_H

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

#include "MwLink.h"
#include "MwWallet.h"

namespace MwLink {

struct HidStep
{
    QString description;
    std::function<bool(MwLink::Wallet &)> run;
    bool critical = true;

    HidStep() = default;
    HidStep(QString d, std::function<bool(MwLink::Wallet &)> f, bool c = true)
        : description(std::move(d))
        , run(std::move(f))
        , critical(c)
    {}
};

class HidOperation : public QObject
{
    Q_OBJECT

public:
    explicit HidOperation(QObject *parent = nullptr);
    ~HidOperation() override;   // cancel() + ожидание фонового потока

    HidOperation(const HidOperation &) = delete;
    HidOperation &operator=(const HidOperation &) = delete;

    // --- Построение сценария (только до start()) ---------------------------

    void addStep(const QString &description,
                 std::function<bool(MwLink::Wallet &)> fn,
                 bool critical = true);

    void addStep(std::function<bool(MwLink::Wallet &)> fn,
                 bool critical = true);

    // Шаг очистки: выполняется всегда, ошибки игнорируются, отмена не
    // действует (флаг отмены на время cleanup отключается).
    void addCleanup(const QString &description,
                    std::function<bool(MwLink::Wallet &)> fn);

    void clearSteps();
    int stepCount() const { return m_steps.size(); }

    // Размер данных, которые будут отправлены. Если превышает лимит
    // устройства (INFO.max_payload) — операция завершится ошибкой ещё
    // до отправки.
    void setPayloadSize(quint32 bytes) { m_payloadSize = bytes; }

    // --- Запуск / отмена ---------------------------------------------------

    //   vid / pid — идентификация USB-устройства (pid == 0 — перебор kUsbPids).
    // Возвращает false, если операция уже запущена.
    bool start(quint16 vid = kUsbVid, quint16 pid = 0);

    // Прервать операцию: блокирующий put/get завершится за ≈250 мс.
    void cancel();

    bool isRunning() const { return m_running.load(); }
    bool isCancelled() const { return m_cancelled.load(); }

    // --- Диагностика / результат -------------------------------------------

    void setVerbose(bool on) { m_verbose = on; }
    bool isVerbose() const { return m_verbose; }

    // Ниже — читать только после сигнала finished() (UI-поток).
    QString lastError() const { return m_lastError; }
    QString deviceBoard() const { return m_deviceBoard; }   // INFO.board
    int stepsCompleted() const { return m_stepsCompleted.load(); }

    void setResultData(const QByteArray &data) { m_resultData = data; }
    QByteArray resultData() const { return m_resultData; }

signals:
    void stepStarted(int index, const QString &description);
    void stepFinished(int index, bool ok);
    void logReceived(quint8 level, const QString &text);
    void errorReceived(quint8 code, const QString &text);

    // ok == true, если все критические шаги прошли и операция не отменена.
    // error — причина (в т.ч. текст отказа устройства), пусто при успехе.
    void finished(bool ok, const QString &error);

private slots:
    void onDeviceLog(quint8 level, const QString &text);
    void onDeviceError(quint8 code, const QString &text);

private:
    void run();
    bool prepareDevice(MwLink::Wallet &dev);
    bool runStepSafe(const HidStep &step, MwLink::Wallet &dev);

    QList<HidStep> m_steps;
    QList<HidStep> m_cleanup;

    std::atomic_bool m_running{false};
    std::atomic_bool m_cancelled{false};
    std::atomic_int  m_stepsCompleted{0};

    QFuture<void> m_future;

    quint16 m_vid = 0;
    quint16 m_pid = 0;
    quint32 m_payloadSize = 0;

    bool    m_verbose = false;
    QString m_lastError;      // пишется фоном, читается после finished()
    QString m_deviceBoard;

    QByteArray m_resultData;
};

} // namespace MwLink

#endif // FEATHER_HIDOPERATION_H

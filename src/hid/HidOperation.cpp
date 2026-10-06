// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Feather Wallet Project
//
// Реализация HidOperation: фоновый сценарий обмена с HID-устройством.

#include "HidOperation.h"

#include <QDebug>
#include <QtConcurrent/QtConcurrent>

#include <exception>

namespace MwLink {

HidOperation::HidOperation(QObject *parent)
    : QObject(parent)
{
}

HidOperation::~HidOperation()
{
    // Фоновый поток держит this, поэтому обязательно дожидаемся его.
    // Отмена срабатывает внутри Hid::recv примерно за 250 мс.
    m_cancelled.store(true);
    m_future.waitForFinished();
}

// ---------------------------------------------------------------------------

void HidOperation::addStep(const QString &description,
                           std::function<bool(MwLink::Wallet &)> fn,
                           bool critical)
{
    Q_ASSERT_X(!m_running.load(), "HidOperation::addStep",
               "cannot add steps after start()");
    if (m_running.load()) {
        qWarning() << "[HidOperation] addStep ignored: already running";
        return;
    }
    m_steps.append(HidStep(description, std::move(fn), critical));
}

void HidOperation::addStep(std::function<bool(MwLink::Wallet &)> fn, bool critical)
{
    addStep(QStringLiteral("Step %1").arg(m_steps.size() + 1), std::move(fn), critical);
}

void HidOperation::addCleanup(const QString &description,
                              std::function<bool(MwLink::Wallet &)> fn)
{
    Q_ASSERT_X(!m_running.load(), "HidOperation::addCleanup",
               "cannot add steps after start()");
    if (m_running.load()) {
        qWarning() << "[HidOperation] addCleanup ignored: already running";
        return;
    }
    m_cleanup.append(HidStep(description, std::move(fn), false));
}

void HidOperation::clearSteps()
{
    Q_ASSERT_X(!m_running.load(), "HidOperation::clearSteps",
               "cannot clear steps while running");
    if (m_running.load()) {
        qWarning() << "[HidOperation] clearSteps ignored: already running";
        return;
    }
    m_steps.clear();
    m_cleanup.clear();
}

// ---------------------------------------------------------------------------

bool HidOperation::start(quint16 vid, quint16 pid)
{
    if (m_running.load()) {
        qWarning() << "[HidOperation] start ignored: already running";
        return false;
    }

    // Предыдущий запуск (если был) уже завершён; дожидаемся хвоста потока.
    m_future.waitForFinished();

    m_vid = vid;
    m_pid = pid;

    m_cancelled.store(false);
    m_stepsCompleted.store(0);
    m_lastError.clear();
    m_deviceBoard.clear();
    m_resultData.clear();
    m_running.store(true);

    if (m_verbose)
        qDebug() << "[HidOperation] starting with" << m_steps.size() << "steps";

    m_future = QtConcurrent::run([this]() { run(); });
    return true;
}

void HidOperation::cancel()
{
    if (!m_running.load())
        return;
    m_cancelled.store(true);
    if (m_verbose)
        qDebug() << "[HidOperation] cancel requested";
}

// ---------------------------------------------------------------------------

bool HidOperation::runStepSafe(const HidStep &step, MwLink::Wallet &dev)
{
    bool ok = false;
    try {
        ok = step.run(dev);
    } catch (const std::exception &e) {
        ok = false;
        m_lastError = QStringLiteral("exception: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        ok = false;
        m_lastError = QStringLiteral("unknown exception");
    }
    return ok;
}

void HidOperation::run()
{
    QString error;
    bool ok = false;

    {
        MwLink::Wallet dev;
        dev.setCancelFlag(&m_cancelled);

        if (!prepareDevice(dev)) {
            error = m_lastError.isEmpty()
                ? QStringLiteral("Failed to prepare device")
                : m_lastError;
            dev.close();
            m_lastError = error;
            m_running.store(false);
            emit finished(false, error);
            return;
        }

        const int total = m_steps.size();
        bool aborted = false;

        for (int i = 0; i < total; ++i) {

            if (m_cancelled.load()) {
                error = QStringLiteral("Cancelled by user");
                aborted = true;
                break;
            }

            const HidStep &step = m_steps[i];
            emit stepStarted(i, step.description);

            m_lastError.clear();
            const bool stepOk = runStepSafe(step, dev);

            // Причину отказа берём у устройства/транспорта (напр. «not accepted»).
            if (!stepOk && m_lastError.isEmpty())
                m_lastError = dev.lastError();

            emit stepFinished(i, stepOk);

            if (stepOk) {
                ++m_stepsCompleted;
                continue;
            }

            if (step.critical) {
                if (m_cancelled.load())
                    error = QStringLiteral("Cancelled by user");
                else
                    error = m_lastError.isEmpty()
                        ? QStringLiteral("Step failed: %1").arg(step.description)
                        : m_lastError;
                aborted = true;
                break;
            }

            if (m_verbose)
                qWarning() << "[HidOperation] non-critical step" << i
                           << "failed:" << m_lastError;
        }

        // --- Cleanup: выполняется всегда, пока устройство открыто ---
        if (dev.isOpen() && !m_cleanup.isEmpty()) {
            dev.setCancelFlag(nullptr);   // отмена не должна мешать очистке
            for (int i = 0; i < m_cleanup.size(); ++i) {
                emit stepStarted(total + i, m_cleanup[i].description);
                runStepSafe(m_cleanup[i], dev);
            }
        }

        dev.close();
        ok = !aborted;
    }

    m_lastError = error;
    m_running.store(false);

    if (m_verbose)
        qDebug() << "[HidOperation] finished ok=" << ok
                 << "steps=" << m_stepsCompleted.load() << "/" << m_steps.size()
                 << "error=" << error;

    emit finished(ok, error);
}

// ---------------------------------------------------------------------------

bool HidOperation::prepareDevice(MwLink::Wallet &dev)
{
    if (!dev.open(m_vid, m_pid)) {
        m_lastError = dev.lastError().isEmpty()
            ? QStringLiteral("HID device not found")
            : dev.lastError();
        return false;
    }

    // Wallet живёт в этом же (фоновом) потоке — DirectConnection безопасен.
    // Дальше сигналы HidOperation доставляются получателям из UI-потока очередью.
    connect(&dev, &MwLink::Wallet::logReceived,
            this, &HidOperation::onDeviceLog, Qt::DirectConnection);
    connect(&dev, &MwLink::Wallet::errorReceived,
            this, &HidOperation::onDeviceError, Qt::DirectConnection);

    MwLink::Info info;
    if (!dev.info(&info)) {
        m_lastError = dev.lastError().isEmpty()
            ? QStringLiteral("No INFO response from device")
            : dev.lastError();
        return false;
    }

    m_deviceBoard = info.board;

    if (m_verbose)
        qDebug() << "[HidOperation] device: fw=" << info.fw << "board=" << info.board
                 << "wallet=" << info.wallet
                 << "state=" << MwLink::stateName(MwLink::State(info.state))
                 << "unlocked=" << info.unlocked
                 << "maxPayload=" << info.maxPayload;

    if (!info.isUnlocked()) {
        m_lastError = QStringLiteral("Device is locked");
        return false;
    }
    if (!info.isWalletOpen()) {
        m_lastError = QStringLiteral("Wallet is not open on the device");
        return false;
    }
    if (info.state == MwLink::StateBusy) {
        m_lastError = QStringLiteral(
            "The device is busy with a previous file or request: "
            "finish or cancel it on the device");
        return false;
    }

    // Лимит размера: min(клиентский, лимит устройства из INFO).
    quint32 limit = kMaxPayload;
    if (info.maxPayload > 0 && info.maxPayload < limit)
        limit = info.maxPayload;
    if (m_payloadSize > limit) {
        m_lastError = QStringLiteral(
            "Data is too large for the device (%1 KiB, limit %2 KiB). "
            "Use QR codes or file transfer instead.")
            .arg((m_payloadSize + 1023) / 1024).arg(limit / 1024);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------

void HidOperation::onDeviceLog(quint8 level, const QString &text)
{
    emit logReceived(level, text);
}

void HidOperation::onDeviceError(quint8 code, const QString &text)
{
    emit errorReceived(code, text);
}

} // namespace MwLink

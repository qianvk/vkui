// SPDX-License-Identifier: MIT

#include <vkui/core/VkDiagnostics.h>

#include <QUuid>

#include <mutex>
#include <utility>

namespace vkui {
namespace {

std::mutex diagnosticMutex;
DiagnosticSink diagnosticSink;

} // namespace

DiagnosticSource DiagnosticSource::current(
    const std::source_location location)
{
    return {
        QString::fromUtf8(location.file_name()),
        QString::fromUtf8(location.function_name()),
        static_cast<int>(location.line()),
    };
}

void setDiagnosticSink(DiagnosticSink sink)
{
    const std::scoped_lock lock(diagnosticMutex);
    diagnosticSink = std::move(sink);
}

void clearDiagnosticSink()
{
    setDiagnosticSink({});
}

void writeDiagnostic(
    const DiagnosticLevel level,
    QString category,
    QString event,
    QString message,
    QJsonObject fields,
    DiagnosticSource source)
{
    DiagnosticSink sink;
    {
        const std::scoped_lock lock(diagnosticMutex);
        sink = diagnosticSink;
    }
    if (sink) {
        sink(DiagnosticRecord{
            level,
            std::move(category),
            std::move(event),
            std::move(message),
            std::move(fields),
            std::move(source),
        });
    }
}

DiagnosticSpan::DiagnosticSpan(
    QString category,
    QString event,
    QJsonObject fields,
    DiagnosticSource source)
    : m_category(std::move(category))
    , m_event(std::move(event))
    , m_correlationId(
          QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_fields(std::move(fields))
    , m_source(std::move(source))
    , m_started(std::chrono::steady_clock::now())
{
    m_fields.insert(QStringLiteral("correlation_id"), m_correlationId);
    writeDiagnostic(
        DiagnosticLevel::Debug,
        m_category,
        m_event + QStringLiteral(".begin"),
        {},
        m_fields,
        m_source);
}

DiagnosticSpan::~DiagnosticSpan()
{
    const qint64 duration =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_started)
            .count();
    QJsonObject fields = m_fields;
    fields.insert(QStringLiteral("duration_us"), duration);
    fields.insert(
        QStringLiteral("outcome"),
        m_failure.isEmpty()
            ? QStringLiteral("ok")
            : QStringLiteral("error"));
    if (!m_failure.isEmpty()) {
        fields.insert(QStringLiteral("reason"), m_failure);
    }
    writeDiagnostic(
        m_failure.isEmpty()
            ? DiagnosticLevel::Debug
            : DiagnosticLevel::Error,
        m_category,
        m_event + QStringLiteral(".end"),
        m_failure,
        std::move(fields),
        m_source);
}

void DiagnosticSpan::fail(QString reason)
{
    m_failure = std::move(reason);
}

void DiagnosticSpan::annotate(QString key, QJsonValue value)
{
    m_fields.insert(std::move(key), std::move(value));
}

} // namespace vkui

// SPDX-License-Identifier: MIT

#pragma once

#include <vkui/VkUiGlobal.h>

#include <QJsonObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <source_location>

namespace vkui {

enum class DiagnosticLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

struct DiagnosticSource final
{
    QString file;
    QString function;
    int line = 0;

    [[nodiscard]] static DiagnosticSource current(
        const std::source_location location =
            std::source_location::current());
};

struct DiagnosticRecord final
{
    DiagnosticLevel level = DiagnosticLevel::Info;
    QString category;
    QString event;
    QString message;
    QJsonObject fields;
    DiagnosticSource source;
};

using DiagnosticSink = std::function<void(DiagnosticRecord)>;

/** Installs one process-wide, thread-safe diagnostics adapter. */
VKUI_CORE_EXPORT void setDiagnosticSink(DiagnosticSink sink);
VKUI_CORE_EXPORT void clearDiagnosticSink();
VKUI_CORE_EXPORT void writeDiagnostic(
    DiagnosticLevel level,
    QString category,
    QString event,
    QString message = {},
    QJsonObject fields = {},
    DiagnosticSource source = DiagnosticSource::current());

/** Emits correlated begin/end records without coupling VkUI to a logger. */
class VKUI_CORE_EXPORT DiagnosticSpan final
{
public:
    DiagnosticSpan(
        QString category,
        QString event,
        QJsonObject fields = {},
        DiagnosticSource source = DiagnosticSource::current());
    ~DiagnosticSpan();

    DiagnosticSpan(const DiagnosticSpan &) = delete;
    DiagnosticSpan &operator=(const DiagnosticSpan &) = delete;

    void fail(QString reason = {});
    void annotate(QString key, QJsonValue value);

private:
    QString m_category;
    QString m_event;
    QString m_correlationId;
    QString m_failure;
    QJsonObject m_fields;
    DiagnosticSource m_source;
    std::chrono::steady_clock::time_point m_started;
};

} // namespace vkui

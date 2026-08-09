#include "VkCoreInternal.h"

namespace vkui::vk {

DispatchResult VkCore::submitCommandLine(
    const WindowId window,
    const CommandLineKind kind,
    std::u16string text)
{
    DispatchResult result;
    result.disposition = InputDisposition::Consumed;
    if (!m_impl->enabled
        || !m_impl->views.contains(window)) {
        return result;
    }
    struct InsertOneNormalCompletion final
    {
        Implementation *implementation = nullptr;
        DispatchResult *dispatch = nullptr;
        WindowId window = 0;

        ~InsertOneNormalCompletion()
        {
            if (implementation != nullptr
                && dispatch != nullptr
                && implementation->insertOneNormalPending) {
                implementation->finishInsertOneNormal(
                    *dispatch, window);
            }
        }
    } insertOneNormalCompletion{
        m_impl.get(), &result, window};
    std::size_t commandLineCount = 1;
    bool commandLineCountWasExplicit = false;
    if (m_impl->pendingCommandLine
        && m_impl->pendingCommandLine->window == window
        && m_impl->pendingCommandLine->kind == kind) {
        const auto found = m_impl->views.find(window);
        if (found != m_impl->views.end()
            && found->second.buffer
                == m_impl->pendingCommandLine->buffer) {
            commandLineCount =
                m_impl->pendingCommandLine->count;
            commandLineCountWasExplicit =
                m_impl->pendingCommandLine
                    ->countWasExplicit;
        }
    }
    m_impl->pendingCommandLine.reset();
    if (kind == CommandLineKind::SearchForward
        || kind == CommandLineKind::SearchBackward) {
        if (text.empty()) {
            text = m_impl->lastSearchPattern;
        }
        m_impl->searchBuffer(
            result,
            window,
            text,
            kind == CommandLineKind::SearchForward,
            commandLineCount,
            true);
        return result;
    }

    while (!text.empty()
           && QChar(text.front()).isSpace()) {
        text.erase(text.begin());
    }
    while (!text.empty()
           && QChar(text.back()).isSpace()) {
        text.pop_back();
    }

    if (!text.empty() && text.front() == u':') {
        text.erase(text.begin());
    }
    QString commandLine =
        QString::fromStdU16String(text).trimmed();
    if (commandLine.isEmpty()) {
        return result;
    }

    std::optional<CommandLineRange> parsedRange;
    std::optional<std::size_t> parsedSingleAddress;
    std::optional<std::size_t> parsedCount =
        commandLineCountWasExplicit
        ? std::optional<std::size_t>(commandLineCount)
        : std::nullopt;
    const auto foundWindow = m_impl->views.find(window);
    std::size_t currentLine = 1;
    if (foundWindow != m_impl->views.end()
        && foundWindow->second.buffer != 0) {
        const auto cursor = foundWindow->second.cursors.find(
            foundWindow->second.buffer);
        if (cursor != foundWindow->second.cursors.end()) {
            currentLine = cursor->second.line + 1;
        }
    }
    std::size_t lastLine = currentLine;
    if (foundWindow != m_impl->views.end()) {
        const auto foundBuffer = m_impl->buffers.find(
            foundWindow->second.buffer);
        if (foundBuffer != m_impl->buffers.end()) {
            lastLine = foundBuffer->second.lineStarts.size();
        }
    }
    const QRegularExpression rangePrefix(
        QStringLiteral(
            R"(^(%|(?:\.|\$|\d+)(?:\s*,\s*(?:\.|\$|\d+))?)\s*)"));
    const QRegularExpressionMatch rangeMatch =
        rangePrefix.match(commandLine);
    if (rangeMatch.hasMatch()) {
        const QString notation =
            rangeMatch.captured(1).remove(QLatin1Char(' '));
        const auto address = [currentLine, lastLine](
            const QString &value) -> std::optional<std::size_t> {
            if (value == QStringLiteral(".")) {
                return currentLine;
            }
            if (value == QStringLiteral("$")) {
                return lastLine;
            }
            bool ok = false;
            const qulonglong parsed = value.toULongLong(&ok);
            return ok
                ? std::optional<std::size_t>(
                      static_cast<std::size_t>(parsed))
                : std::nullopt;
        };
        if (notation == QStringLiteral("%")) {
            parsedRange = CommandLineRange{1, lastLine};
        } else {
            const qsizetype comma = notation.indexOf(
                QLatin1Char(','));
            if (comma < 0) {
                parsedSingleAddress = address(notation);
            } else {
                const auto first = address(notation.left(comma));
                const auto last = address(notation.mid(comma + 1));
                if (first && last) {
                    parsedRange = CommandLineRange{*first, *last};
                }
            }
        }
        commandLine.remove(0, rangeMatch.capturedLength());
        commandLine = commandLine.trimmed();
        if (commandLine.isEmpty()) {
            Event error;
            error.type = EventType::InputError;
            error.view = window;
            error.message = "missing Ex command after range";
            result.events.push_back(std::move(error));
            return result;
        }
    }
    const qsizetype separator = commandLine.indexOf(
        QRegularExpression(QStringLiteral("\\s+")));
    QString name = separator < 0
        ? commandLine
        : commandLine.left(separator);
    const QString rawArguments = separator < 0
        ? QString{}
        : commandLine.mid(separator).trimmed();
    const bool bang = name.endsWith(QLatin1Char('!'));
    if (bang) {
        name.chop(1);
    }
    const QString normalizedName = name.toLower();

    const auto parseArguments = [](const QString &raw) {
        std::vector<std::string> arguments;
        QString current;
        bool escaped = false;
        bool started = false;
        for (const QChar character : raw) {
            if (escaped) {
                current.append(character);
                escaped = false;
                started = true;
                continue;
            }
            if (character == QLatin1Char('\\')) {
                escaped = true;
                started = true;
                continue;
            }
            if (character.isSpace()) {
                if (started) {
                    arguments.push_back(current.toStdString());
                    current.clear();
                    started = false;
                }
                continue;
            }
            current.append(character);
            started = true;
        }
        // Vim's <f-args> treats a final backslash as a literal character.
        if (escaped) {
            current.append(QLatin1Char('\\'));
        }
        if (started) {
            arguments.push_back(current.toStdString());
        }
        return arguments;
    };
    const std::vector<std::string> parsedArguments =
        parseArguments(rawArguments);

    const auto emitCommand =
        [this,
         &result,
         window,
         &rawArguments,
         &parsedArguments,
         &parsedRange,
         &parsedSingleAddress,
         &parsedCount,
         bang](
            std::string id) {
            Event event;
            event.type = EventType::CommandRequested;
            event.view = window;
            const auto found = m_impl->views.find(window);
            if (found != m_impl->views.end()) {
                event.buffer = found->second.buffer;
            }
            event.mode = m_impl->effectiveMode();
            event.commandId = std::move(id);
            event.commandArguments = parsedArguments;
            event.count = parsedCount.value_or(1);
            event.countWasExplicit = parsedCount.has_value();
            event.bang = bang;
            event.lineRange = parsedRange
                ? parsedRange
                : parsedSingleAddress
                ? std::optional<CommandLineRange>(
                      CommandLineRange{
                          *parsedSingleAddress,
                          *parsedSingleAddress})
                : std::nullopt;
            event.rawArguments = rawArguments.toStdString();
            result.events.push_back(std::move(event));
        };

    if (normalizedName == QStringLiteral("set")) {
        const QString option = rawArguments.trimmed();
        const qsizetype equals = option.indexOf(
            QLatin1Char('='));
        const QString optionName =
            (equals < 0 ? option : option.left(equals))
                .trimmed()
                .toLower();
        const QString optionValue = equals < 0
            ? QString{}
            : option.mid(equals + 1).trimmed().toLower();
        if (optionName == QStringLiteral("virtualedit")
            || optionName == QStringLiteral("ve")) {
            if (equals < 0) {
                Event error;
                error.type = EventType::InputError;
                error.view = window;
                error.message =
                    "virtualedit requires = or =block";
                result.events.push_back(std::move(error));
            } else if (optionValue.isEmpty()) {
                setVirtualEditMode(VirtualEditMode::None);
            } else if (
                optionValue == QStringLiteral("block")) {
                setVirtualEditMode(VirtualEditMode::Block);
            } else {
                Event error;
                error.type = EventType::InputError;
                error.view = window;
                error.message =
                    "unsupported virtualedit value";
                result.events.push_back(std::move(error));
            }
        } else {
            Event error;
            error.type = EventType::InputError;
            error.view = window;
            error.message = "unsupported option";
            result.events.push_back(std::move(error));
        }
    } else if (normalizedName == QStringLiteral("w")
        || normalizedName == QStringLiteral("write")) {
        emitCommand("vkery.editor.save");
    } else if (normalizedName == QStringLiteral("q")
               || normalizedName == QStringLiteral("quit")) {
        emitCommand("vkery.window.quit");
    } else if (normalizedName == QStringLiteral("qa")
               || normalizedName == QStringLiteral("qall")) {
        emitCommand("vkery.application.close");
    } else if (normalizedName == QStringLiteral("wq")
               || normalizedName == QStringLiteral("x")
               || normalizedName == QStringLiteral("xit")) {
        emitCommand("vkery.editor.write-quit");
    } else if (normalizedName == QStringLiteral("new")) {
        emitCommand("vkery.editor.new-split");
    } else if (normalizedName == QStringLiteral("enew")) {
        emitCommand("vkery.editor.new");
    } else if (normalizedName == QStringLiteral("bd")
               || normalizedName == QStringLiteral("bdelete")) {
        emitCommand("vkery.editor.close");
    } else if (normalizedName == QStringLiteral("close")) {
        emitCommand("vkery.window.close");
    } else if (normalizedName == QStringLiteral("bn")
               || normalizedName == QStringLiteral("bnext")) {
        const auto found = m_impl->views.find(window);
        if (found != m_impl->views.end()) {
            m_impl->activateRelativeBuffer(
                result, found->second, window, 1);
        }
    } else if (normalizedName == QStringLiteral("bp")
               || normalizedName == QStringLiteral("bprevious")) {
        const auto found = m_impl->views.find(window);
        if (found != m_impl->views.end()) {
            m_impl->activateRelativeBuffer(
                result, found->second, window, -1);
        }
    } else if (normalizedName == QStringLiteral("sp")
               || normalizedName == QStringLiteral("split")) {
        emitCommand("vkery.window.split-horizontal");
    } else if (normalizedName == QStringLiteral("vs")
               || normalizedName == QStringLiteral("vsplit")) {
        emitCommand("vkery.window.split-vertical");
    } else {
        CommandInvocation context;
        context.mode = m_impl->effectiveMode();
        context.window = window;
        const auto found = m_impl->views.find(window);
        if (found != m_impl->views.end()) {
            context.buffer = found->second.buffer;
        }
        if (parsedSingleAddress) {
            const auto definition =
                m_impl->userCommands.definition(
                    name.toStdString(),
                    context.buffer == 0
                        ? std::nullopt
                        : std::optional<BufferId>(
                              context.buffer));
            if (definition
                && definition->acceptsCount
                && !definition->acceptsRange) {
                parsedCount = parsedSingleAddress;
            } else {
                parsedRange = CommandLineRange{
                    *parsedSingleAddress,
                    *parsedSingleAddress};
            }
        }
        std::string resolutionError;
        auto resolved = m_impl->userCommands.resolve(
            UserCommandRequest{
                name.toStdString(),
                parsedArguments,
                rawArguments.toStdString(),
                bang,
                parsedRange,
                parsedCount,
                std::move(context)},
            &resolutionError);
        if (!resolved) {
            Event event;
            event.type = EventType::InputError;
            event.view = window;
            event.message = std::move(resolutionError);
            result.events.push_back(std::move(event));
            return result;
        }
        Event event;
        event.type = EventType::CommandRequested;
        event.view = resolved->invocation.window;
        event.inputTarget = resolved->invocation.inputTarget;
        event.buffer = resolved->invocation.buffer;
        event.mode = resolved->invocation.mode;
        event.commandId = std::move(resolved->invocation.id);
        event.commandArguments =
            std::move(resolved->invocation.arguments);
        event.count = resolved->invocation.count;
        event.countWasExplicit =
            resolved->invocation.countWasExplicit;
        event.bang = resolved->invocation.bang;
        event.lineRange = resolved->invocation.lineRange;
        event.rawArguments =
            std::move(resolved->invocation.rawArguments);
        result.events.push_back(std::move(event));
    }
    return result;
}

std::optional<PromptSessionId> VkCore::beginPromptInput(
    const WindowId window,
    const InputTargetId inputTarget,
    std::u16string initialText)
{
    if (!m_impl->enabled
        || inputTarget == 0
        || !m_impl->views.contains(window)
        || m_impl->promptInput) {
        return std::nullopt;
    }
    cancelPendingInput();
    if (m_impl->nextPromptSession == 0) {
        ++m_impl->nextPromptSession;
    }
    const PromptSessionId session =
        m_impl->nextPromptSession++;
    m_impl->promptInput = Implementation::PromptInput{
        session,
        window,
        inputTarget,
        std::move(initialText)};
    return session;
}

bool VkCore::updatePromptInput(
    const PromptSessionId session,
    std::u16string text)
{
    if (!m_impl->promptInput
        || m_impl->promptInput->id != session) {
        return false;
    }
    m_impl->promptInput->text = std::move(text);
    return true;
}

DispatchResult VkCore::cancelPromptInput(
    const PromptSessionId session)
{
    DispatchResult result;
    if (!m_impl->promptInput
        || m_impl->promptInput->id != session) {
        return result;
    }
    result.disposition = InputDisposition::Consumed;
    m_impl->finishPrompt(
        result, EventType::PromptCancelled);
    return result;
}

std::optional<PromptSessionId>
VkCore::activePromptInput() const noexcept
{
    return m_impl->promptInput
        ? std::optional<PromptSessionId>(
              m_impl->promptInput->id)
        : std::nullopt;
}

} // namespace vkui::vk

#include "VkCoreInternal.h"

#include <unicode/uregex.h>
#include <unicode/utext.h>
#include <unicode/utf16.h>

namespace vkui::vk {

namespace {

constexpr int32_t SnapshotRegexChunkLength = 4096;

struct SnapshotTextContext final {
    const vkui::buffer::BufferStorage* storage = nullptr;
    std::int64_t length = 0;
    bool failed = false;
};

[[nodiscard]] UText* openSnapshotText(UText* text, SnapshotTextContext* context,
                                      UErrorCode* status);

[[nodiscard]] UText* U_CALLCONV snapshotTextClone(UText* destination, const UText* source,
                                                  const UBool deep, UErrorCode* status) {
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    if (deep) {
        *status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }
    auto* context =
        const_cast<SnapshotTextContext*>(static_cast<const SnapshotTextContext*>(source->context));
    destination = openSnapshotText(destination, context, status);
    if (U_SUCCESS(*status) && destination != nullptr) {
        const std::int64_t index = utext_getNativeIndex(const_cast<UText*>(source));
        utext_setNativeIndex(destination, index);
    }
    return destination;
}

[[nodiscard]] std::int64_t U_CALLCONV snapshotTextLength(UText* text) {
    return text->a;
}

[[nodiscard]] UBool U_CALLCONV snapshotTextAccess(UText* text, std::int64_t index,
                                                  const UBool forward) {
    auto* context =
        const_cast<SnapshotTextContext*>(static_cast<const SnapshotTextContext*>(text->context));
    if (context == nullptr || context->storage == nullptr) {
        return false;
    }
    const bool outOfBounds = index < 0 || index > context->length;
    index = std::clamp<std::int64_t>(index, 0, context->length);
    const auto useCurrentChunk = [&] {
        if (forward) {
            return (index >= text->chunkNativeStart && index < text->chunkNativeLimit) ||
                   (index == context->length && text->chunkNativeLimit == context->length);
        }
        return (index > text->chunkNativeStart && index <= text->chunkNativeLimit) ||
               (index == 0 && text->chunkNativeStart == 0);
    };
    if (!useCurrentChunk()) {
        std::int64_t needed = index;
        if (!forward && needed > 0) {
            --needed;
        } else if (forward && needed == context->length && needed > 0) {
            --needed;
        }
        std::int64_t start = needed - needed % SnapshotRegexChunkLength;
        const std::int64_t remaining = context->length - start;
        const std::int64_t limitWithoutBoundaryAdjustment =
            start + std::min<std::int64_t>(remaining, SnapshotRegexChunkLength);
        std::int64_t limit = limitWithoutBoundaryAdjustment;

        if (start > 0) {
            const auto atStart = context->storage->codeUnitAt(static_cast<std::size_t>(start));
            const auto beforeStart =
                context->storage->codeUnitAt(static_cast<std::size_t>(start - 1));
            if (atStart && beforeStart && U16_IS_TRAIL(*atStart) && U16_IS_LEAD(*beforeStart)) {
                --start;
            }
        }
        if (limit < context->length && limit > start) {
            const auto beforeLimit =
                context->storage->codeUnitAt(static_cast<std::size_t>(limit - 1));
            const auto atLimit = context->storage->codeUnitAt(static_cast<std::size_t>(limit));
            if (beforeLimit && atLimit && U16_IS_LEAD(*beforeLimit) && U16_IS_TRAIL(*atLimit)) {
                ++limit;
            }
        }
        const auto chunk = context->storage->readExact(static_cast<std::size_t>(start),
                                                       static_cast<std::size_t>(limit - start));
        if (!chunk || chunk->size() > static_cast<std::size_t>(SnapshotRegexChunkLength + 2)) {
            context->failed = true;
            text->chunkLength = 0;
            text->chunkNativeStart = index;
            text->chunkNativeLimit = index;
            text->chunkOffset = 0;
            text->nativeIndexingLimit = 0;
            return false;
        }
        auto* destination = static_cast<UChar*>(text->pExtra);
        std::transform(chunk->cbegin(), chunk->cend(), destination,
                       [](const char16_t codeUnit) { return static_cast<UChar>(codeUnit); });
        text->chunkContents = destination;
        text->chunkNativeStart = start;
        text->chunkNativeLimit = limit;
        text->chunkLength = static_cast<int32_t>(chunk->size());
        text->nativeIndexingLimit = text->chunkLength;
    }

    text->chunkOffset = static_cast<int32_t>(index - text->chunkNativeStart);
    if (text->chunkOffset > 0 && text->chunkOffset < text->chunkLength &&
        U16_IS_TRAIL(text->chunkContents[text->chunkOffset]) &&
        U16_IS_LEAD(text->chunkContents[text->chunkOffset - 1])) {
        --text->chunkOffset;
    }
    return !outOfBounds &&
           (forward ? text->chunkOffset < text->chunkLength : text->chunkOffset > 0);
}

[[nodiscard]] int32_t U_CALLCONV snapshotTextExtract(UText* text, std::int64_t start,
                                                     std::int64_t limit, UChar* destination,
                                                     const int32_t destinationCapacity,
                                                     UErrorCode* status) {
    if (U_FAILURE(*status)) {
        return 0;
    }
    auto* context =
        const_cast<SnapshotTextContext*>(static_cast<const SnapshotTextContext*>(text->context));
    if (context == nullptr || context->storage == nullptr || destinationCapacity < 0 ||
        (destination == nullptr && destinationCapacity > 0) || start > limit) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    start = std::clamp<std::int64_t>(start, 0, context->length);
    limit = std::clamp<std::int64_t>(limit, 0, context->length);
    if (start < context->length && start > 0) {
        const auto current = context->storage->codeUnitAt(static_cast<std::size_t>(start));
        const auto previous = context->storage->codeUnitAt(static_cast<std::size_t>(start - 1));
        if (current && previous && U16_IS_TRAIL(*current) && U16_IS_LEAD(*previous)) {
            --start;
        }
    }
    if (limit < context->length && limit > 0) {
        const auto current = context->storage->codeUnitAt(static_cast<std::size_t>(limit));
        const auto previous = context->storage->codeUnitAt(static_cast<std::size_t>(limit - 1));
        if (current && previous && U16_IS_TRAIL(*current) && U16_IS_LEAD(*previous)) {
            --limit;
        }
    }
    const std::int64_t requested = limit - start;
    if (requested > std::numeric_limits<int32_t>::max()) {
        *status = U_INDEX_OUTOFBOUNDS_ERROR;
        return 0;
    }
    const std::size_t copyLength =
        static_cast<std::size_t>(std::min<std::int64_t>(requested, destinationCapacity));
    std::size_t copied = 0;
    while (copied < copyLength) {
        const std::size_t amount =
            std::min<std::size_t>(SnapshotRegexChunkLength, copyLength - copied);
        const auto chunk =
            context->storage->readExact(static_cast<std::size_t>(start) + copied, amount);
        if (!chunk) {
            context->failed = true;
            *status = U_INTERNAL_PROGRAM_ERROR;
            return 0;
        }
        std::transform(chunk->cbegin(), chunk->cend(), destination + copied,
                       [](const char16_t codeUnit) { return static_cast<UChar>(codeUnit); });
        copied += chunk->size();
    }
    if (destination != nullptr && copied < static_cast<std::size_t>(destinationCapacity)) {
        destination[copied] = 0;
    }
    (void)snapshotTextAccess(text, limit, true);
    if (requested > destinationCapacity) {
        *status = U_BUFFER_OVERFLOW_ERROR;
    }
    return static_cast<int32_t>(requested);
}

const UTextFuncs SnapshotTextFunctions{sizeof(UTextFuncs),
                                       0,
                                       0,
                                       0,
                                       snapshotTextClone,
                                       snapshotTextLength,
                                       snapshotTextAccess,
                                       snapshotTextExtract,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr};

[[nodiscard]] UText* openSnapshotText(UText* text, SnapshotTextContext* context,
                                      UErrorCode* status) {
    if (U_FAILURE(*status) || context == nullptr || context->storage == nullptr) {
        if (U_SUCCESS(*status)) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        return text;
    }
    text = utext_setup(text, static_cast<int32_t>((SnapshotRegexChunkLength + 2) * sizeof(UChar)),
                       status);
    if (U_FAILURE(*status) || text == nullptr) {
        return text;
    }
    text->pFuncs = &SnapshotTextFunctions;
    text->context = context;
    text->a = context->length;
    text->chunkContents = static_cast<UChar*>(text->pExtra);
    text->chunkNativeStart = -1;
    text->chunkOffset = 1;
    text->chunkNativeLimit = 0;
    text->chunkLength = 0;
    text->nativeIndexingLimit = text->chunkOffset;
    return text;
}

struct RegexCloser final {
    void operator()(URegularExpression* expression) const noexcept {
        uregex_close(expression);
    }
};

struct TextCloser final {
    void operator()(UText* text) const noexcept {
        (void)utext_close(text);
    }
};

} // namespace

void VkCore::Implementation::requestCommandLine(
    DispatchResult &result,
    const WindowId windowId,
    const CommandLineKind kind,
    const std::size_t count,
    const bool countWasExplicit)
{
    Event requested;
    requested.type = EventType::CommandLineRequested;
    requested.view = windowId;
    const auto found = views.find(windowId);
    if (found != views.end()) {
        requested.buffer = found->second.buffer;
    }
    requested.commandLineKind = kind;
    requested.count = count;
    requested.countWasExplicit = countWasExplicit;
    pendingCommandLine = PendingCommandLine{
        windowId,
        requested.buffer,
        kind,
        count,
        countWasExplicit};
    result.events.push_back(std::move(requested));
}

void VkCore::Implementation::searchBuffer(
    DispatchResult &result,
    const WindowId windowId,
    const std::u16string &pattern,
    const bool forward,
    const std::size_t rawCount,
    const bool rememberPattern)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || pattern.empty()) {
        return;
    }
    // Use one engine for both owned and external text so regex syntax and
    // Unicode behavior cannot diverge with the storage representation.
    if (foundBuffer->second.storage.isExternalSession() || foundBuffer->second.storage.isOwned()) {
        const auto reportError = [&](std::string message) {
            Event error;
            error.type = EventType::InputError;
            error.view = windowId;
            error.buffer = view.buffer;
            error.message = std::move(message);
            result.events.push_back(std::move(error));
        };
        const auto descriptor = foundBuffer->second.storage.describe();
        if (!descriptor ||
            descriptor->size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()) ||
            pattern.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
            reportError("external snapshot is unavailable for search");
            return;
        }

        SnapshotTextContext context{&foundBuffer->second.storage,
                                    static_cast<std::int64_t>(descriptor->size), false};
        UErrorCode status = U_ZERO_ERROR;
        UText subjectText = UTEXT_INITIALIZER;
        UText* opened = openSnapshotText(&subjectText, &context, &status);
        if (U_FAILURE(status) || opened == nullptr) {
            reportError(u_errorName(status));
            return;
        }
        std::unique_ptr<UText, TextCloser> inputOwner(opened);
        UParseError parseError{};
        std::unique_ptr<URegularExpression, RegexCloser> expression(
            uregex_open(reinterpret_cast<const UChar*>(pattern.data()),
                        static_cast<int32_t>(pattern.size()), UREGEX_UWORD, &parseError, &status));
        if (U_FAILURE(status) || !expression) {
            reportError(u_errorName(status));
            return;
        }
        uregex_setUText(expression.get(), opened, &status);
        if (U_FAILURE(status)) {
            reportError(u_errorName(status));
            return;
        }
        if (rememberPattern) {
            lastSearchPattern = pattern;
            lastSearchForward = forward;
        }

        const Cursor originCursor =
            clampCursor(foundBuffer->second, view.cursors[view.buffer], false);
        const std::size_t originOffset = offset(foundBuffer->second, originCursor);
        const std::size_t forwardBegin = std::min(descriptor->size, originOffset + 1);
        const std::size_t count = std::max<std::size_t>(1, rawCount);
        std::size_t totalMatches = 0;
        std::size_t matchesBeforeForwardBegin = 0;
        std::size_t matchesBeforeOrigin = 0;
        UBool matched = uregex_find64(expression.get(), 0, &status);
        while (matched && U_SUCCESS(status)) {
            const std::int64_t position = uregex_start64(expression.get(), 0, &status);
            if (U_FAILURE(status) || position < 0) {
                break;
            }
            const std::size_t matchOffset = static_cast<std::size_t>(position);
            matchesBeforeForwardBegin += matchOffset < forwardBegin ? 1 : 0;
            matchesBeforeOrigin += matchOffset < originOffset ? 1 : 0;
            ++totalMatches;
            matched = uregex_findNext(expression.get(), &status);
        }
        if (U_FAILURE(status) || context.failed) {
            reportError(context.failed ? "external snapshot range became unavailable during search"
                                       : std::string(u_errorName(status)));
            return;
        }

        std::optional<std::size_t> destination;
        if (totalMatches != 0) {
            const std::size_t targetOrdinal =
                forward
                    ? (matchesBeforeForwardBegin % totalMatches + (count - 1) % totalMatches) %
                          totalMatches
                    : (matchesBeforeOrigin % totalMatches + totalMatches - count % totalMatches) %
                          totalMatches;
            status = U_ZERO_ERROR;
            matched = uregex_find64(expression.get(), 0, &status);
            std::size_t ordinal = 0;
            while (matched && U_SUCCESS(status)) {
                const std::int64_t position = uregex_start64(expression.get(), 0, &status);
                if (U_FAILURE(status) || position < 0) {
                    break;
                }
                if (ordinal++ == targetOrdinal) {
                    destination = static_cast<std::size_t>(position);
                    break;
                }
                matched = uregex_findNext(expression.get(), &status);
            }
        }
        if (U_FAILURE(status) || context.failed) {
            reportError(context.failed ? "external snapshot range became unavailable during search"
                                       : std::string(u_errorName(status)));
            return;
        }
        if (!destination) {
            reportError("pattern not found");
            return;
        }
        const Cursor destinationCursor = cursorAtOffset(foundBuffer->second, *destination);
        (void)moveToLocation(result, windowId, Location{view.buffer, *destination}, false,
                             destinationCursor.line != originCursor.line);
        return;
    }
    if (!searchExpressionCached
        || cachedSearchPattern != pattern) {
        cachedSearchPattern = pattern;
        cachedSearchExpression = QRegularExpression(
            QString::fromStdU16String(pattern),
            QRegularExpression::UseUnicodePropertiesOption);
        searchExpressionCached = true;
    }
    const QRegularExpression &expression =
        cachedSearchExpression;
    if (!expression.isValid()) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = expression.errorString()
                            .toStdString();
        result.events.push_back(std::move(error));
        return;
    }
    if (rememberPattern) {
        lastSearchPattern = pattern;
        lastSearchForward = forward;
    }
    const Cursor originCursor = clampCursor(
        foundBuffer->second,
        view.cursors[view.buffer],
        false);
    const std::size_t originOffset = offset(
        foundBuffer->second, originCursor);
    const std::size_t count =
        std::max<std::size_t>(1, rawCount);
    const std::u16string& bufferText = foundBuffer->second.storage.ownedText()->text();
    const QStringView subject{
        reinterpret_cast<const QChar *>(
            bufferText.data()),
        static_cast<qsizetype>(bufferText.size())};
    const std::size_t forwardBegin = std::min(
        bufferText.size(), originOffset + 1);
    std::size_t totalMatches = 0;
    std::size_t matchesBeforeForwardBegin = 0;
    std::size_t matchesBeforeOrigin = 0;
    auto matches = expression.globalMatchView(subject);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match =
            matches.next();
        if (!match.hasMatch()
            || match.capturedStart() < 0) {
            continue;
        }
        const std::size_t position =
            static_cast<std::size_t>(
                match.capturedStart());
        matchesBeforeForwardBegin +=
            position < forwardBegin ? 1 : 0;
        matchesBeforeOrigin +=
            position < originOffset ? 1 : 0;
        ++totalMatches;
    }
    std::optional<std::size_t> destination;
    if (totalMatches != 0) {
        const std::size_t targetOrdinal = forward
            ? (matchesBeforeForwardBegin % totalMatches
               + (count - 1) % totalMatches)
                % totalMatches
            : (matchesBeforeOrigin % totalMatches
               + totalMatches
               - count % totalMatches)
                % totalMatches;
        auto targetMatches =
            expression.globalMatchView(subject);
        std::size_t ordinal = 0;
        while (targetMatches.hasNext()) {
            const QRegularExpressionMatch match =
                targetMatches.next();
            if (!match.hasMatch()
                || match.capturedStart() < 0) {
                continue;
            }
            if (ordinal++ == targetOrdinal) {
                destination =
                    static_cast<std::size_t>(
                        match.capturedStart());
                break;
            }
        }
    }
    if (!destination) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = "pattern not found";
        result.events.push_back(std::move(error));
        return;
    }
    const Cursor destinationCursor = cursorAtOffset(
        foundBuffer->second, *destination);
    (void)moveToLocation(
        result,
        windowId,
        Location{view.buffer, *destination},
        false,
        destinationCursor.line
            != originCursor.line);
}

void VkCore::Implementation::searchWordUnderCursor(
    DispatchResult &result,
    const WindowId windowId,
    const bool forward,
    const bool exact,
    const std::size_t count)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    const auto foundBuffer = buffers.find(
        foundView->second.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    Buffer &buffer = foundBuffer->second;
    Cursor cursor = clampCursor(
        buffer,
        foundView->second.cursors.at(
            foundView->second.buffer),
        false);

    // Vim's star-family commands use the next word when the cursor is
    // currently on whitespace. Blank lines can be returned as word-motion
    // stops, so keep advancing until a searchable run is found.
    while (wordClass(
               buffer,
               cursor.line,
               cursor.column,
               false) == WordClass::White) {
        const auto next = nextWordStart(
            buffer, cursor, false);
        if (!next || *next == cursor) {
            Event error;
            error.type = EventType::InputError;
            error.view = windowId;
            error.buffer = foundView->second.buffer;
            error.message = "no word under cursor";
            result.events.push_back(std::move(error));
            return;
        }
        cursor = *next;
    }

    const WordClass runClass = wordClass(
        buffer, cursor.line, cursor.column, false);
    std::size_t start = cursor.column;
    while (start > 0) {
        const std::size_t previous =
            previousColumnAllowEnd(
                buffer, cursor.line, start);
        if (wordClass(
                buffer,
                cursor.line,
                previous,
                false) != runClass) {
            break;
        }
        start = previous;
    }
    std::size_t end = nextColumnAllowEnd(
        buffer, cursor.line, cursor.column);
    const std::size_t length = lineLength(
        buffer, cursor.line);
    while (end < length
           && wordClass(
                  buffer,
                  cursor.line,
                  end,
                  false) == runClass) {
        end = nextColumnAllowEnd(
            buffer, cursor.line, end);
    }

    const std::size_t absoluteStart =
        buffer.lineStarts[cursor.line] + start;
    const std::u16string patternText = buffer.text().substr(absoluteStart, end - start);
    QString pattern = QRegularExpression::escape(
        QString::fromUtf16(patternText.data(), static_cast<qsizetype>(patternText.size())));
    if (exact && runClass == WordClass::Keyword) {
        // This boundary mirrors wordClass(): Unicode letters, numbers,
        // combining marks, and underscore are keyword characters. Qt's
        // generic \b is ASCII-centric under some locale combinations and
        // does not express the same contract.
        static const QString KeywordBoundary =
            QStringLiteral("[\\p{L}\\p{N}\\p{M}_]");
        pattern.prepend(
            QStringLiteral("(?<!")
            + KeywordBoundary
            + QLatin1Char(')'));
        pattern.append(
            QStringLiteral("(?!")
            + KeywordBoundary
            + QLatin1Char(')'));
    }
    searchBuffer(
        result,
        windowId,
        pattern.toStdU16String(),
        forward,
        count,
        true);
}

[[nodiscard]] unsigned VkCore::Implementation::digitValue(
    const char16_t value) noexcept
{
    if (value >= u'0' && value <= u'9') {
        return static_cast<unsigned>(value - u'0');
    }
    if (value >= u'a' && value <= u'f') {
        return 10U
            + static_cast<unsigned>(value - u'a');
    }
    if (value >= u'A' && value <= u'F') {
        return 10U
            + static_cast<unsigned>(value - u'A');
    }
    return 16U;
}

[[nodiscard]] bool VkCore::Implementation::digitForBase(
    const char16_t value,
    const unsigned base) noexcept
{
    return digitValue(value) < base;
}

[[nodiscard]] std::optional<VkCore::Implementation::NumberToken> VkCore::Implementation::numberAtOrAfter(
    const Buffer &buffer,
    const Cursor cursor) const
{
    const std::size_t line = std::min(
        cursor.line, buffer.lineStarts.size() - 1);
    const std::size_t lineStart = buffer.lineStarts[line];
    const std::size_t length = lineLength(buffer, line);
    const std::size_t requested = std::min(
        cursor.column, length);
    const std::u16string text = buffer.text().substr(lineStart, length);

    std::size_t at = 0;
    while (at < length) {
        const std::size_t tokenStart = at;
        bool negative = false;
        if (text[at] == u'-'
            && at + 1 < length
            && text[at + 1] >= u'0'
            && text[at + 1] <= u'9') {
            negative = true;
            ++at;
        }
        if (text[at] < u'0' || text[at] > u'9') {
            at = tokenStart + 1;
            continue;
        }

        unsigned base = 10;
        bool uppercasePrefix = false;
        bool uppercaseDigits = false;
        std::size_t digitsStart = at;
        if (text[at] == u'0' && at + 2 < length
            && (text[at + 1] == u'x'
                || text[at + 1] == u'X')
            && digitForBase(text[at + 2], 16)) {
            base = 16;
            uppercasePrefix = text[at + 1] == u'X';
            digitsStart = at + 2;
        } else if (
            text[at] == u'0' && at + 2 < length
            && (text[at + 1] == u'b'
                || text[at + 1] == u'B')
            && digitForBase(text[at + 2], 2)) {
            base = 2;
            uppercasePrefix = text[at + 1] == u'B';
            digitsStart = at + 2;
        }

        std::size_t end = digitsStart;
        std::uint64_t value = 0;
        while (end < length
               && digitForBase(text[end], base)) {
            if (text[end] >= u'A'
                && text[end] <= u'F') {
                uppercaseDigits = true;
            }
            const std::uint64_t digit =
                digitValue(text[end]);
            constexpr std::uint64_t Maximum =
                std::numeric_limits<std::uint64_t>::max();
            value = value > (Maximum - digit) / base
                ? Maximum
                : value * base + digit;
            ++end;
        }

        if (end > requested) {
            return NumberToken{
                tokenStart,
                end,
                digitsStart,
                value,
                base,
                negative,
                uppercasePrefix,
                uppercaseDigits};
        }
        at = std::max(end, tokenStart + 1);
    }
    return std::nullopt;
}

[[nodiscard]] std::u16string VkCore::Implementation::formatUnsigned(
    std::uint64_t value,
    const unsigned base,
    const bool uppercase,
    const std::size_t minimumWidth)
{
    std::u16string reversed;
    do {
        const unsigned digit =
            static_cast<unsigned>(value % base);
        reversed.push_back(
            digit < 10
            ? static_cast<char16_t>(u'0' + digit)
            : static_cast<char16_t>(
                  (uppercase ? u'A' : u'a')
                  + digit - 10));
        value /= base;
    } while (value != 0);
    while (reversed.size() < minimumWidth) {
        reversed.push_back(u'0');
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

[[nodiscard]] bool VkCore::Implementation::changeNumber(
    DispatchResult &result,
    const WindowId windowId,
    const std::size_t count,
    const bool subtract,
    const bool remember)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer, view.cursors[view.buffer], false);
    const auto token = numberAtOrAfter(buffer, cursor);
    if (!token) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = "number not found";
        result.events.push_back(std::move(error));
        return false;
    }

    std::uint64_t magnitude = token->value;
    bool negative = token->negative;
    const std::uint64_t delta = count;
    const bool growsMagnitude = subtract == negative;
    if (growsMagnitude) {
        constexpr std::uint64_t Maximum =
            std::numeric_limits<std::uint64_t>::max();
        magnitude = magnitude > Maximum - delta
            ? Maximum
            : magnitude + delta;
    } else if (magnitude >= delta) {
        magnitude -= delta;
    } else {
        magnitude = delta - magnitude;
        negative = !negative;
    }
    if (magnitude == 0) {
        negative = false;
    }

    const std::size_t digitWidth =
        token->end - token->digitsStart;
    std::u16string replacement;
    if (negative) {
        replacement.push_back(u'-');
    }
    if (token->base == 16) {
        replacement += token->uppercasePrefix
            ? u"0X"
            : u"0x";
    } else if (token->base == 2) {
        replacement += token->uppercasePrefix
            ? u"0B"
            : u"0b";
    }
    replacement += formatUnsigned(
        magnitude,
        token->base,
        token->uppercaseDigits,
        digitWidth);

    const std::size_t absoluteStart =
        buffer.lineStarts[cursor.line] + token->start;
    const std::size_t absoluteEnd =
        buffer.lineStarts[cursor.line] + token->end;
    const Cursor target{
        cursor.line,
        token->start + replacement.size() - 1};
    if (!mutateBuffer(
            &result,
            view.buffer,
            absoluteStart,
            absoluteEnd,
            std::move(replacement),
            std::pair<WindowId, Cursor>{windowId, target})) {
        return false;
    }
    keywordCompletion.reset();
    if (remember) {
        RepeatChange repeat;
        repeat.command = subtract
            ? Command::SubtractNumber
            : Command::AddNumber;
        repeat.count = count;
        rememberChange(std::move(repeat));
    }
    return true;
}

void VkCore::Implementation::showBufferStatus(
    DispatchResult &result,
    const WindowId windowId)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    const auto foundBuffer = buffers.find(
        foundView->second.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        foundView->second.cursors.at(
            foundView->second.buffer),
        false);
    const std::size_t lineCount =
        std::max<std::size_t>(1, buffer.lineStarts.size());
    const std::size_t percent = std::min<std::size_t>(
        100, ((cursor.line + 1) * 100) / lineCount);
    Event status;
    status.type = EventType::StatusMessage;
    status.view = windowId;
    status.buffer = foundView->second.buffer;
    status.cursor = cursor;
    status.hasCursor = true;
    status.message = "\"" + buffer.path
        + "\" line " + std::to_string(cursor.line + 1)
        + " of " + std::to_string(lineCount)
        + " --" + std::to_string(percent)
        + "%-- col " + std::to_string(cursor.column + 1);
    result.events.push_back(std::move(status));
}

void VkCore::Implementation::showDetailedBufferStatus(
    DispatchResult &result,
    const WindowId windowId)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    const auto foundBuffer = buffers.find(
        foundView->second.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer,
        foundView->second.cursors.at(
            foundView->second.buffer),
        false);
    const BufferTextView& text = buffer.text();
    const std::size_t cursorOffset = offset(buffer, cursor);
    const std::size_t cursorEnd = text.empty()
        ? 0
        : cursorCharacterEnd(buffer, cursor);

    const auto scalarCount = [](const auto& value, const std::size_t rawLimit) {
        const std::size_t limit = std::min(rawLimit, value.size());
        std::size_t count = 0;
        for (std::size_t at = 0; at < limit;) {
            ++count;
            at += QChar::isHighSurrogate(value[at]) && at + 1 < limit &&
                          QChar::isLowSurrogate(value[at + 1])
                      ? 2
                      : 1;
        }
        return count;
    };
    const auto utf8Size = [](const auto& value, const std::size_t rawLimit) {
        const std::size_t limit = std::min(rawLimit, value.size());
        std::size_t bytes = 0;
        for (std::size_t at = 0; at < limit; ++at) {
            char32_t scalar = value[at];
            if (QChar::isHighSurrogate(value[at]) && at + 1 < limit &&
                QChar::isLowSurrogate(value[at + 1])) {
                const char16_t high = value[at];
                const char16_t low = value[at + 1];
                ++at;
                scalar = QChar::surrogateToUcs4(high, low);
            }
            bytes += scalar <= 0x7fU ? 1 : scalar <= 0x7ffU ? 2 : scalar <= 0xffffU ? 3 : 4;
        }
        return bytes;
    };

    std::size_t wordTotal = 0;
    std::size_t wordOrdinal = 0;
    bool insideWord = false;
    for (std::size_t at = 0; at < text.size();
         at = nextScalarOffset(text, at)) {
        const bool word = wordClassAtOffset(
            buffer, at, true) != WordClass::White;
        if (word && !insideWord) {
            ++wordTotal;
            if (at <= cursorOffset) {
                wordOrdinal = wordTotal;
            }
        }
        insideWord = word;
    }
    if (wordTotal != 0 && wordOrdinal == 0) {
        wordOrdinal = 1;
    }

    const std::size_t lineLengthUtf16 =
        lineLength(buffer, cursor.line);

    Event status;
    status.type = EventType::StatusMessage;
    status.view = windowId;
    status.buffer = foundView->second.buffer;
    status.cursor = cursor;
    status.hasCursor = true;
    status.message = "Col " + std::to_string(cursor.column + 1) + " of " +
                     std::to_string(lineLengthUtf16) + "; Line " + std::to_string(cursor.line + 1) +
                     " of " + std::to_string(buffer.lineStarts.size()) + "; Word " +
                     std::to_string(wordOrdinal) + " of " + std::to_string(wordTotal) + "; Char " +
                     std::to_string(scalarCount(text, cursorEnd)) + " of " +
                     std::to_string(scalarCount(text, text.size())) + "; Byte " +
                     std::to_string(utf8Size(text, cursorEnd)) + " of " +
                     std::to_string(utf8Size(text, text.size()));
    result.events.push_back(std::move(status));
}

[[nodiscard]] bool VkCore::Implementation::keywordStartsWith(
    const std::u16string_view word,
    const std::u16string_view candidatePrefix) const noexcept
{
    return word.size() >= candidatePrefix.size()
        && std::equal(
            candidatePrefix.cbegin(),
            candidatePrefix.cend(),
            word.cbegin());
}

[[nodiscard]] std::optional<KeywordCompletionState>
VkCore::Implementation::beginKeywordCompletion(
    const WindowId windowId,
    const Buffer &buffer,
    const View &view) const
{
    const Cursor cursor = clampCursor(
        buffer, view.cursors.at(view.buffer), true);
    const std::size_t insertionOffset = offset(buffer, cursor);
    std::size_t prefixColumn = cursor.column;
    while (prefixColumn > 0) {
        const std::size_t previous = previousColumnAllowEnd(
            buffer, cursor.line, prefixColumn);
        if (wordClass(
                buffer,
                cursor.line,
                previous,
                false) != WordClass::Keyword) {
            break;
        }
        prefixColumn = previous;
    }
    const std::size_t prefixStart =
        buffer.lineStarts[cursor.line] + prefixColumn;
    const std::u16string completionPrefix =
        buffer.text().substr(
            prefixStart,
            insertionOffset - prefixStart);

    struct Candidate final
    {
        std::u16string word;
    };
    std::vector<Candidate> after;
    std::vector<Candidate> before;
    constexpr std::size_t MaximumCandidates = 65'536;
    for (std::size_t line = 0;
         line < buffer.lineStarts.size()
         && after.size() + before.size()
                < MaximumCandidates;
         ++line) {
        const std::size_t length = lineLength(buffer, line);
        std::size_t column = 0;
        while (column < length
               && after.size() + before.size()
                    < MaximumCandidates) {
            if (wordClass(
                    buffer, line, column, false)
                != WordClass::Keyword) {
                column = nextColumnAllowEnd(
                    buffer, line, column);
                continue;
            }
            const std::size_t startColumn = column;
            do {
                column = nextColumnAllowEnd(
                    buffer, line, column);
            } while (
                column < length
                && wordClass(
                       buffer, line, column, false)
                    == WordClass::Keyword);
            const std::size_t start =
                buffer.lineStarts[line] + startColumn;
            const std::size_t end =
                buffer.lineStarts[line] + column;
            // Do not offer the word currently being edited as a match.
            if (!completionPrefix.empty()
                && start <= prefixStart
                && insertionOffset <= end) {
                continue;
            }
            const std::u16string word = buffer.text().substr(start, end - start);
            if (!keywordStartsWith(word, completionPrefix)) {
                continue;
            }
            Candidate candidate{std::u16string(word)};
            (start >= insertionOffset ? after : before)
                .push_back(std::move(candidate));
        }
    }

    KeywordCompletionState state;
    state.window = windowId;
    state.buffer = view.buffer;
    state.insertionOffset = insertionOffset;
    state.prefix = completionPrefix;
    state.options.push_back(completionPrefix);
    state.revision = buffer.revision();
    std::unordered_set<std::u16string> unique;
    const auto append = [&state, &unique](
                            const std::vector<Candidate> &source) {
        for (const Candidate &candidate : source) {
            if (unique.insert(candidate.word).second) {
                state.options.push_back(candidate.word);
            }
        }
    };
    append(after);
    append(before);
    return state;
}

[[nodiscard]] bool VkCore::Implementation::completeKeyword(
    DispatchResult &result,
    const WindowId windowId,
    const bool forward)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    Buffer &buffer = foundBuffer->second;
    const std::size_t cursorOffset = offset(
        buffer,
        clampCursor(
            buffer, view.cursors[view.buffer], true));
    const bool valid = keywordCompletion
        && keywordCompletion->window == windowId
        && keywordCompletion->buffer == view.buffer
        && keywordCompletion->revision
            == buffer.revision()
        && cursorOffset
            == keywordCompletion->insertionOffset
                + keywordCompletion->replacementLength;
    if (!valid) {
        keywordCompletion = beginKeywordCompletion(
            windowId, buffer, view);
    }
    if (!keywordCompletion
        || keywordCompletion->options.size() <= 1) {
        keywordCompletion.reset();
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = "keyword completion: pattern not found";
        result.events.push_back(std::move(error));
        return false;
    }

    KeywordCompletionState &state = *keywordCompletion;
    if (forward) {
        state.selected =
            (state.selected + 1) % state.options.size();
    } else {
        state.selected = state.selected == 0
            ? state.options.size() - 1
            : state.selected - 1;
    }
    const std::u16string &word =
        state.options[state.selected];
    const std::u16string suffix = word.substr(
        std::min(state.prefix.size(), word.size()));
    recordExternalInsertEdit(
        windowId,
        state.insertionOffset,
        state.replacementLength,
        suffix);
    if (!mutateBuffer(
            &result,
            view.buffer,
            state.insertionOffset,
            state.insertionOffset
                + state.replacementLength,
            suffix)) {
        keywordCompletion.reset();
        return false;
    }
    // mutateBuffer may rehash the buffer table only through callers that
    // add buffers, never here; reacquire nevertheless to keep this state
    // update independent of container implementation details.
    foundBuffer = buffers.find(view.buffer);
    state.replacementLength = suffix.size();
    state.revision = foundBuffer->second.revision();
    return true;
}

[[nodiscard]] bool VkCore::Implementation::deleteInsertPrefix(
    DispatchResult &result,
    const WindowId windowId,
    const bool wholeLine)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer, view.cursors[view.buffer], true);
    std::size_t startColumn = cursor.column;
    if (wholeLine) {
        startColumn = 0;
    } else {
        while (startColumn > 0) {
            const std::size_t previous =
                previousColumnAllowEnd(
                    buffer, cursor.line, startColumn);
            if (wordClass(
                    buffer,
                    cursor.line,
                    previous,
                    false) != WordClass::White) {
                break;
            }
            startColumn = previous;
        }
        if (startColumn > 0) {
            const std::size_t previous =
                previousColumnAllowEnd(
                    buffer, cursor.line, startColumn);
            const WordClass target = wordClass(
                buffer, cursor.line, previous, false);
            startColumn = previous;
            while (startColumn > 0) {
                const std::size_t before =
                    previousColumnAllowEnd(
                        buffer,
                        cursor.line,
                        startColumn);
                if (wordClass(
                        buffer,
                        cursor.line,
                        before,
                        false) != target) {
                    break;
                }
                startColumn = before;
            }
        }
    }
    if (startColumn == cursor.column) {
        return false;
    }
    const std::size_t lineStart =
        buffer.lineStarts[cursor.line];
    const std::size_t start = lineStart + startColumn;
    const std::size_t end = lineStart + cursor.column;
    recordExternalInsertEdit(
        windowId, start, end - start, {});
    return mutateBuffer(
        &result,
        view.buffer,
        start,
        end,
        {},
        std::pair<WindowId, Cursor>{
            windowId,
            Cursor{cursor.line, startColumn}});
}

[[nodiscard]] bool VkCore::Implementation::insertRegisterValue(
    DispatchResult &result,
    const WindowId windowId,
    const char32_t name)
{
    const RegisterValue value = registerValue(name);
    if (value.text.empty()) {
        return false;
    }
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return false;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()
        || rejectReadOnly(
            result, windowId, foundBuffer->second)) {
        return false;
    }
    const Buffer &buffer = foundBuffer->second;
    const Cursor cursor = clampCursor(
        buffer, view.cursors[view.buffer], true);
    const std::size_t insertion = offset(buffer, cursor);
    Cursor target = cursor;
    for (const char16_t unit : value.text) {
        if (unit == u'\n') {
            ++target.line;
            target.column = 0;
        } else {
            ++target.column;
        }
    }
    recordExternalInsertEdit(
        windowId, insertion, 0, value.text);
    return mutateBuffer(
        &result,
        view.buffer,
        insertion,
        insertion,
        value.text,
        std::pair<WindowId, Cursor>{
            windowId, target});
}

[[nodiscard]] bool VkCore::Implementation::startMacroRecording(
    const char32_t name)
{
    if (!((name >= U'a' && name <= U'z')
          || (name >= U'A' && name <= U'Z'))) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(
            name >= U'A' && name <= U'Z'
                ? name - U'A'
                : name - U'a');
    if (name >= U'a' && name <= U'z') {
        namedRegisters[index] = RegisterValue{};
    } else if (namedRegisters[index].macroKeys.empty()
               && !namedRegisters[index].text.empty()) {
        namedRegisters[index].macroKeys =
            macroKeysFromText(
                namedRegisters[index].text);
    }
    recordingMacro = index;
    return true;
}

void VkCore::Implementation::executeMacro(
    DispatchResult &result,
    const WindowId windowId,
    const InputTargetId inputTarget,
    const std::size_t registerIndex,
    const std::size_t rawCount)
{
    static_cast<void>(inputTarget);
    if (registerIndex >= namedRegisters.size()) {
        return;
    }
    RegisterValue &source = namedRegisters[registerIndex];
    if (source.macroKeys.empty()
        && !source.text.empty()) {
        source.macroKeys = macroKeysFromText(source.text);
    }
    if (source.macroKeys.empty()) {
        return;
    }
    constexpr std::size_t MaximumMacroSteps =
        1'000'000;
    const detail::KeySequence recorded = source.macroKeys;
    const std::size_t count =
        std::max<std::size_t>(1, rawCount);
    const bool overflow = !recorded.empty()
        && count
            > std::numeric_limits<std::size_t>::max()
                / recorded.size();
    const std::size_t steps = overflow
        ? std::numeric_limits<std::size_t>::max()
        : recorded.size() * count;
    if (!macroPlaybackActive) {
        macroPlaybackActive = true;
        macroStepsRemaining = MaximumMacroSteps;
    }
    if (steps > macroStepsRemaining) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.message = "macro step limit reached";
        result.events.push_back(std::move(error));
        input.clear();
        macroPlaybackActive = false;
        macroStepsRemaining = 0;
        return;
    }
    macroStepsRemaining -= steps;
    lastPlayedMacro = registerIndex;
    // Macro keys enter the same resolver as physical typeahead. This is
    // what makes mappings, nested macros and host transaction barriers
    // compose correctly instead of running an unsafe parallel command
    // interpreter.
    input.prependMacro(recorded, count);
}

void VkCore::Implementation::abortMacroCommand() noexcept
{
    macroCommandDisposition =
        MacroCommandDisposition::Abort;
}

} // namespace vkui::vk

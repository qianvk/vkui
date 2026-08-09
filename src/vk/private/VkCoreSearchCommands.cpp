#include "VkCoreInternal.h"

namespace vkui::vk {

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
    const std::u16string &bufferText =
        foundBuffer->second.text();
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
    QString pattern = QRegularExpression::escape(
        QStringView{
            reinterpret_cast<const QChar *>(
                buffer.text().data()
                + absoluteStart),
            static_cast<qsizetype>(end - start)}
            .toString());
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
    const std::u16string_view text{
        buffer.text().data() + lineStart,
        length};

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
    const std::u16string &text = buffer.text();
    const std::size_t cursorOffset = offset(buffer, cursor);
    const std::size_t cursorEnd = text.empty()
        ? 0
        : cursorCharacterEnd(buffer, cursor);

    const auto scalarCount = [](const std::u16string_view value) {
        std::size_t count = 0;
        for (std::size_t at = 0; at < value.size();) {
            ++count;
            at += QChar::isHighSurrogate(value[at])
                    && at + 1 < value.size()
                    && QChar::isLowSurrogate(value[at + 1])
                ? 2
                : 1;
        }
        return count;
    };
    const auto utf8Size = [](const std::u16string_view value) {
        return static_cast<std::size_t>(
            QString::fromUtf16(
                reinterpret_cast<const char16_t *>(value.data()),
                static_cast<qsizetype>(value.size()))
                .toUtf8()
                .size());
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
    const std::u16string_view beforeCursor{
        text.data(), std::min(cursorEnd, text.size())};
    Event status;
    status.type = EventType::StatusMessage;
    status.view = windowId;
    status.buffer = foundView->second.buffer;
    status.cursor = cursor;
    status.hasCursor = true;
    status.message = "Col "
        + std::to_string(cursor.column + 1)
        + " of " + std::to_string(lineLengthUtf16)
        + "; Line " + std::to_string(cursor.line + 1)
        + " of " + std::to_string(buffer.lineStarts.size())
        + "; Word " + std::to_string(wordOrdinal)
        + " of " + std::to_string(wordTotal)
        + "; Char " + std::to_string(
              scalarCount(beforeCursor))
        + " of " + std::to_string(
              scalarCount(text))
        + "; Byte " + std::to_string(
              utf8Size(beforeCursor))
        + " of " + std::to_string(utf8Size(text));
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
            const std::u16string_view word{
                buffer.text().data() + start,
                end - start};
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

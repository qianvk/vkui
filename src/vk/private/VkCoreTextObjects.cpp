#include "VkCoreInternal.h"

namespace vkui::vk {

[[nodiscard]] std::optional<VkCore::Implementation::TextRange>
VkCore::Implementation::textObjectRange(
    const Buffer &buffer,
    Cursor cursor,
    const char32_t object,
    const bool around,
    const std::size_t rawCount) const
{
    const BufferTextView& text = buffer.text();
    if (text.empty()) {
        return std::nullopt;
    }
    cursor = clampCursor(buffer, cursor, false);
    const std::size_t count =
        std::max<std::size_t>(1, rawCount);
    std::size_t current = std::min(
        offset(buffer, cursor), text.size() - 1);
    const auto scalarAtOffset = [&text](
        const std::size_t at) -> char32_t {
        if (at >= text.size()) {
            return U'\0';
        }
        const char16_t first = text[at];
        if (QChar::isHighSurrogate(first)
            && at + 1 < text.size()
            && QChar::isLowSurrogate(text[at + 1])) {
            return QChar::surrogateToUcs4(
                first, text[at + 1]);
        }
        return static_cast<char32_t>(first);
    };
    const auto classAt = [&scalarAtOffset](
        const std::size_t at,
        const bool bigWord) {
        const char32_t scalar = scalarAtOffset(at);
        if (scalar == U'\0'
            || u_isUWhiteSpace(
                static_cast<UChar32>(scalar))) {
            return WordClass::White;
        }
        if (bigWord) {
            return WordClass::Keyword;
        }
        const auto category = static_cast<UCharCategory>(
            u_charType(static_cast<UChar32>(scalar)));
        const bool mark =
            category == U_NON_SPACING_MARK
            || category == U_COMBINING_SPACING_MARK
            || category == U_ENCLOSING_MARK;
        return scalar == U'_'
                || u_isalnum(
                    static_cast<UChar32>(scalar))
                || mark
            ? WordClass::Keyword
            : WordClass::Punctuation;
    };
    const auto isSpace = [&classAt](const std::size_t at) {
        return classAt(at, false) == WordClass::White;
    };

    if (object == U'w' || object == U'W') {
        const bool bigWord = object == U'W';
        if (isSpace(current)) {
            std::size_t start = current;
            const std::size_t lineStart =
                buffer.lineStarts[cursor.line];
            while (start > lineStart) {
                const std::size_t previous =
                    previousScalarOffset(text, start);
                if (!isSpace(previous)) {
                    break;
                }
                start = previous;
            }
            std::size_t end = current;
            while (end < text.size() && isSpace(end)) {
                end = nextScalarOffset(text, end);
            }

            // `aw` on trailing whitespace has no following word to pair
            // with and therefore fails in Neovim. `iw` still selects the
            // whitespace run itself.
            if (around && end >= text.size()) {
                return std::nullopt;
            }

            // On whitespace, Vim treats the whitespace run as the first
            // inner-word object.  Around-word instead pairs that run with
            // the following word.  Additional counts consume subsequent
            // words and their intervening whitespace.
            std::size_t words = around
                ? count
                : count - 1;
            for (std::size_t iteration = 0;
                 iteration < words && end < text.size();
                 ++iteration) {
                if (iteration != 0) {
                    while (end < text.size()
                           && isSpace(end)) {
                        end = nextScalarOffset(text, end);
                    }
                }
                if (end >= text.size()) {
                    break;
                }
                const std::size_t representative = end;
                const WordClass representativeClass =
                    classAt(representative, bigWord);
                end = nextScalarOffset(text, end);
                while (end < text.size()
                       && !isSpace(end)
                       && classAt(end, bigWord)
                           == representativeClass) {
                    end = nextScalarOffset(text, end);
                }
            }
            return TextRange{start, end, false};
        }
        const auto sameClass =
            [bigWord, &classAt](
                const std::size_t lhs,
                const std::size_t rhs) {
                const WordClass left =
                    classAt(lhs, bigWord);
                const WordClass right =
                    classAt(rhs, bigWord);
                if (left == WordClass::White
                    || right == WordClass::White) {
                    return false;
                }
                return left == right;
            };
        std::size_t start = current;
        const std::size_t lineStart =
            buffer.lineStarts[cursor.line];
        while (start > lineStart) {
            const std::size_t previous =
                previousScalarOffset(text, start);
            if (!sameClass(previous, current)) {
                break;
            }
            start = previous;
        }
        std::size_t end = nextScalarOffset(text, current);
        while (end < text.size()
               && sameClass(end, current)) {
            end = nextScalarOffset(text, end);
        }
        for (std::size_t iteration = 1;
             iteration < count;
             ++iteration) {
            while (end < text.size()
                   && isSpace(end)) {
                end = nextScalarOffset(text, end);
            }
            if (end >= text.size()) {
                break;
            }
            const std::size_t representative = end;
            end = nextScalarOffset(text, end);
            while (end < text.size()
                   && sameClass(
                       end, representative)) {
                end = nextScalarOffset(text, end);
            }
        }
        if (around) {
            const std::size_t beforeTrailing = end;
            while (end < text.size()
                   && isSpace(end)) {
                end = nextScalarOffset(text, end);
            }
            if (end == beforeTrailing) {
                while (start > lineStart) {
                    const std::size_t previous =
                        previousScalarOffset(text, start);
                    if (!isSpace(previous)) {
                        break;
                    }
                    start = previous;
                }
            }
        }
        return TextRange{start, end, false};
    }

    if (object == U'\'' || object == U'\"'
        || object == U'`') {
        const std::size_t lineStart =
            buffer.lineStarts[cursor.line];
        const std::size_t lineEnd =
            lineStart + lineLength(buffer, cursor.line);
        const char16_t delimiter =
            static_cast<char16_t>(object);
        std::vector<std::pair<std::size_t, std::size_t>>
            pairs;
        std::optional<std::size_t> opening;
        bool escaped = false;
        for (std::size_t at = lineStart;
             at < lineEnd;
             ++at) {
            const char16_t value = text[at];
            if (value == u'\\') {
                escaped = !escaped;
                continue;
            }
            if (value == delimiter && !escaped) {
                if (!opening) {
                    opening = at;
                } else {
                    pairs.emplace_back(*opening, at);
                    opening.reset();
                }
            }
            escaped = false;
        }
        if (pairs.empty()) {
            return std::nullopt;
        }

        const auto horizontalSpace = [&text](
            const std::size_t at) {
            return at < text.size()
                && (text[at] == u' '
                    || text[at] == u'\t');
        };
        const auto aroundPair = [&horizontalSpace,
                                 lineStart,
                                 lineEnd](
            const std::size_t left,
            const std::size_t right) {
            std::size_t start = left;
            std::size_t end = right + 1;
            const std::size_t beforeTrailing = end;
            while (end < lineEnd
                   && horizontalSpace(end)) {
                ++end;
            }
            if (end == beforeTrailing) {
                while (start > lineStart
                       && horizontalSpace(start - 1)) {
                    --start;
                }
            }
            return TextRange{start, end, false};
        };

        const auto containing = std::ranges::find_if(
            pairs,
            [current](const auto &pair) {
                return pair.first <= current
                    && current <= pair.second;
            });
        if (containing != pairs.end()) {
            if (around) {
                return aroundPair(
                    containing->first,
                    containing->second);
            }
            // current_quote() uses the first count to remove the inner
            // content. A larger count expands to the quote delimiters;
            // it does not walk into the following quoted string.
            return count == 1
                ? std::optional<TextRange>(TextRange{
                      containing->first + 1,
                      containing->second,
                      false})
                : std::optional<TextRange>(TextRange{
                      containing->first,
                      containing->second + 1,
                      false});
        }

        const auto next = std::ranges::find_if(
            pairs,
            [current](const auto &pair) {
                return pair.first > current;
            });
        if (next != pairs.end()
            && next != pairs.begin()) {
            const auto previous = std::prev(next);
            bool onlyWhitespace =
                previous->second < current;
            for (std::size_t at = previous->second + 1;
                 onlyWhitespace && at < next->first;
                 ++at) {
                onlyWhitespace = horizontalSpace(at);
            }
            if (onlyWhitespace) {
                if (!around && count == 1) {
                    return TextRange{
                        previous->second + 1,
                        next->first,
                        false};
                }
                // Around quotes (and counted inner quotes) merge two
                // adjacent quoted strings by removing the two inner
                // delimiters together with their separating whitespace.
                return TextRange{
                    previous->second,
                    next->first + 1,
                    false};
            }
        }

        // Before a quoted string, current_quote() searches forward on
        // the same line. This is relied upon by ci" from line prefixes.
        if (next != pairs.end()) {
            if (around) {
                return aroundPair(next->first, next->second);
            }
            return count == 1
                ? std::optional<TextRange>(TextRange{
                      next->first + 1,
                      next->second,
                      false})
                : std::optional<TextRange>(TextRange{
                      next->first,
                      next->second + 1,
                      false});
        }
        return std::nullopt;
    }

    if (object == U't') {
        struct OpenTag final
        {
            std::u16string name;
            std::size_t start = 0;
            std::size_t contentStart = 0;
        };
        struct TagRange final
        {
            std::size_t outerStart = 0;
            std::size_t innerStart = 0;
            std::size_t innerEnd = 0;
            std::size_t outerEnd = 0;
        };
        std::vector<OpenTag> stack;
        std::vector<TagRange> enclosing;
        const auto sameTagName = [](
            const std::u16string &lhs,
            const std::u16string &rhs) {
            if (lhs.size() != rhs.size()) {
                return false;
            }
            return std::ranges::equal(
                lhs,
                rhs,
                [](const char16_t left,
                   const char16_t right) {
                    return u_foldCase(
                               static_cast<UChar32>(left),
                               U_FOLD_CASE_DEFAULT)
                        == u_foldCase(
                               static_cast<UChar32>(right),
                               U_FOLD_CASE_DEFAULT);
                });
        };
        for (std::size_t start = text.find(u'<');
             start != std::u16string::npos;
             start = text.find(u'<', start + 1)) {
            char16_t quote = u'\0';
            std::size_t end = start + 1;
            for (; end < text.size(); ++end) {
                const char16_t value = text[end];
                if (quote != u'\0') {
                    if (value == quote) {
                        quote = u'\0';
                    }
                } else if (value == u'\''
                           || value == u'\"') {
                    quote = value;
                } else if (value == u'>') {
                    break;
                }
            }
            if (end >= text.size()) {
                break;
            }
            std::size_t nameStart = start + 1;
            while (nameStart < end
                   && QChar(text[nameStart]).isSpace()) {
                ++nameStart;
            }
            if (nameStart >= end
                || text[nameStart] == u'!'
                || text[nameStart] == u'?') {
                start = end;
                continue;
            }
            const bool closingTag =
                text[nameStart] == u'/';
            if (closingTag) {
                ++nameStart;
            }
            const std::size_t nameEnd = [&] {
                std::size_t at = nameStart;
                while (at < end
                       && !QChar(text[at]).isSpace()
                       && text[at] != u'/'
                       && text[at] != u'>') {
                    ++at;
                }
                return at;
            }();
            if (nameEnd == nameStart) {
                start = end;
                continue;
            }
            const std::u16string name = text.substr(
                nameStart, nameEnd - nameStart);
            std::size_t last = end;
            while (last > start
                   && QChar(text[last - 1]).isSpace()) {
                --last;
            }
            const bool selfClosing =
                !closingTag && last > start
                && text[last - 1] == u'/';
            if (!closingTag && !selfClosing) {
                stack.push_back(OpenTag{
                    name, start, end + 1});
            } else if (closingTag) {
                const auto matching = std::find_if(
                    stack.rbegin(),
                    stack.rend(),
                    [&name, &sameTagName](
                        const OpenTag &entry) {
                        return sameTagName(
                            entry.name, name);
                    });
                if (matching != stack.rend()) {
                    const OpenTag openingTag = *matching;
                    stack.erase(
                        std::next(matching).base(),
                        stack.end());
                    if (openingTag.start <= current
                        && current <= end) {
                        enclosing.push_back(TagRange{
                            openingTag.start,
                            openingTag.contentStart,
                            start,
                            end + 1});
                    }
                }
            }
            start = end;
        }
        if (enclosing.empty()) {
            return std::nullopt;
        }
        std::ranges::sort(
            enclosing,
            [](const TagRange &lhs,
               const TagRange &rhs) {
                return lhs.outerEnd - lhs.outerStart
                    < rhs.outerEnd - rhs.outerStart;
            });
        if (count > enclosing.size()) {
            return std::nullopt;
        }
        const TagRange &chosen = enclosing[count - 1];
        return TextRange{
            around
                ? chosen.outerStart
                : chosen.innerStart,
            around
                ? chosen.outerEnd
                : chosen.innerEnd,
            false};
    }

    char16_t opening = u'\0';
    char16_t closing = u'\0';
    switch (object) {
    case U'(':
    case U')':
    case U'b':
        opening = u'(';
        closing = u')';
        break;
    case U'[':
    case U']':
        opening = u'[';
        closing = u']';
        break;
    case U'{':
    case U'}':
    case U'B':
        opening = u'{';
        closing = u'}';
        break;
    case U'<':
    case U'>':
        opening = u'<';
        closing = u'>';
        break;
    default:
        break;
    }
    if (opening != u'\0') {
        std::vector<std::size_t> stack;
        std::vector<std::pair<std::size_t, std::size_t>>
            enclosing;
        std::vector<std::pair<std::size_t, std::size_t>>
            allPairs;
        for (std::size_t index = 0;
             index < text.size();
             ++index) {
            if (text[index] == opening) {
                stack.push_back(index);
            } else if (text[index] == closing
                       && !stack.empty()) {
                const std::size_t left = stack.back();
                stack.pop_back();
                allPairs.emplace_back(left, index);
                if (left <= current && current <= index) {
                    enclosing.emplace_back(left, index);
                }
            }
        }
        std::ranges::sort(
            enclosing,
            [](const auto &lhs, const auto &rhs) {
                return lhs.second - lhs.first
                    < rhs.second - rhs.first;
            });
        std::optional<std::pair<std::size_t, std::size_t>>
            chosen;
        if (count <= enclosing.size()) {
            chosen = enclosing[count - 1];
        } else if (enclosing.empty()) {
            // current_block() searches forward when the cursor is before
            // a block instead of requiring the cursor to be enclosed by
            // it. Prefer the nearest opening delimiter.
            const auto next = std::ranges::min_element(
                allPairs,
                [current](const auto &lhs,
                          const auto &rhs) {
                    const std::size_t leftDistance =
                        lhs.first >= current
                        ? lhs.first - current
                        : std::numeric_limits<
                              std::size_t>::max();
                    const std::size_t rightDistance =
                        rhs.first >= current
                        ? rhs.first - current
                        : std::numeric_limits<
                              std::size_t>::max();
                    return leftDistance < rightDistance;
                });
            if (next != allPairs.end()
                && next->first >= current) {
                chosen = *next;
            }
        }
        if (!chosen) {
            return std::nullopt;
        }
        const auto [left, right] = *chosen;
        std::size_t innerStart = left + 1;
        if (!around
            && innerStart < right
            && text[innerStart] == u'\n') {
            const Cursor closingCursor =
                cursorAtOffset(buffer, right);
            const std::size_t closingLineStart =
                buffer.lineStarts[closingCursor.line];
            const bool closingAfterIndent = std::ranges::all_of(
                text.cbegin() + static_cast<std::ptrdiff_t>(closingLineStart),
                text.cbegin() + static_cast<std::ptrdiff_t>(right),
                [](const char16_t value) { return value == u' ' || value == u'\t'; });
            if (closingAfterIndent) {
                // For a block whose closing delimiter is the first
                // non-blank character of its line, Neovim keeps the line
                // break after the opening delimiter (`di{` -> "{\n}").
                ++innerStart;
            }
        }
        return TextRange{
            around ? left : innerStart,
            right + (around ? 1U : 0U),
            false};
    }

    if (object == U'p') {
        const auto isBlankLine =
            [this, &buffer](const std::size_t line) {
                const std::size_t length =
                    lineLength(buffer, line);
                const std::size_t start =
                    buffer.lineStarts[line];
                for (std::size_t column = 0;
                     column < length;
                     ++column) {
                    if (!QChar(
                             buffer.text()[
                                 start + column])
                             .isSpace()) {
                        return false;
                    }
                }
                return true;
            };
        const std::size_t lineCount =
            buffer.lineStarts.size();
        const bool startsBlank =
            isBlankLine(cursor.line);
        std::size_t first = cursor.line;
        std::size_t after = cursor.line;

        if (startsBlank) {
            while (first > 0
                   && isBlankLine(first - 1)) {
                --first;
            }
            after = cursor.line + 1;
            while (after < lineCount
                   && isBlankLine(after)) {
                ++after;
            }
            if (!around && count == 1) {
                return TextRange{
                    buffer.lineStarts[first],
                    after < lineCount
                        ? buffer.lineStarts[after]
                        : text.size(),
                    true};
            }

            if (after < lineCount) {
                // On separating blank lines, `ap` pairs the whitespace
                // run with the following paragraph. Counts continue with
                // later paragraph/blank-run units.
                std::size_t paragraphs = 0;
                const std::size_t wanted = around
                    ? count
                    : count - 1;
                while (after < lineCount
                       && paragraphs < wanted) {
                    while (after < lineCount
                           && !isBlankLine(after)) {
                        ++after;
                    }
                    ++paragraphs;
                    if (paragraphs < wanted) {
                        while (after < lineCount
                               && isBlankLine(after)) {
                            ++after;
                        }
                    }
                }
            } else if (around) {
                // No following paragraph: mirror current_par()'s leading
                // whitespace fallback and include the previous paragraph.
                while (first > 0
                       && isBlankLine(first - 1)) {
                    --first;
                }
                while (first > 0
                       && !isBlankLine(first - 1)) {
                    --first;
                }
            }
        } else {
            while (first > 0
                   && !isBlankLine(first - 1)) {
                --first;
            }
            after = cursor.line;
            std::size_t paragraphs = 0;
            while (after < lineCount
                   && paragraphs < count) {
                while (after < lineCount
                       && !isBlankLine(after)) {
                    ++after;
                }
                ++paragraphs;
                if (paragraphs < count) {
                    while (after < lineCount
                           && isBlankLine(after)) {
                        ++after;
                    }
                }
            }
            if (around) {
                const std::size_t beforeTrailing = after;
                while (after < lineCount
                       && isBlankLine(after)) {
                    ++after;
                }
                if (after == beforeTrailing) {
                    while (first > 0
                           && isBlankLine(first - 1)) {
                        --first;
                    }
                }
            }
        }
        std::size_t rangeStart =
            buffer.lineStarts[first];
        const std::size_t rangeEnd =
            after < lineCount
            ? buffer.lineStarts[after]
            : text.size();
        if (rangeEnd == text.size()
            && rangeStart > 0) {
            // A linewise object ending at EOF consumes the separator
            // before its first line so the preceding line is not left
            // with a synthetic trailing newline.
            --rangeStart;
        }
        return TextRange{
            rangeStart, rangeEnd, true};
    }

    if (object == U's') {
        struct SentenceSpan final
        {
            std::size_t start = 0;
            std::size_t end = 0;
        };
        const auto closingPunctuation = [&text](
            const std::size_t at) {
            return at < text.size()
                && (text[at] == u')'
                    || text[at] == u']'
                    || text[at] == u'"'
                    || text[at] == u'\'');
        };
        const auto sentenceEnd = [&text,
                                  &isSpace,
                                  &closingPunctuation](
            const std::size_t at)
            -> std::optional<std::size_t> {
            if (at >= text.size()
                || (text[at] != u'.'
                    && text[at] != u'!'
                    && text[at] != u'?')) {
                return std::nullopt;
            }
            std::size_t after = at + 1;
            while (closingPunctuation(after)) {
                ++after;
            }
            // A dot embedded in an identifier or decimal is not a
            // sentence terminator. Vim requires whitespace, EOL/EOF, or
            // closing punctuation followed by one of those boundaries.
            return after == text.size()
                    || isSpace(after)
                ? std::optional<std::size_t>(after)
                : std::nullopt;
        };

        std::vector<SentenceSpan> sentences;
        std::size_t scan = 0;
        while (scan < text.size()) {
            while (scan < text.size()
                   && isSpace(scan)) {
                scan = nextScalarOffset(text, scan);
            }
            if (scan >= text.size()) {
                break;
            }
            const std::size_t start = scan;
            std::size_t lastNonSpace = scan;
            std::optional<std::size_t> end;
            while (scan < text.size()) {
                if (!isSpace(scan)) {
                    lastNonSpace =
                        nextScalarOffset(text, scan);
                }
                if (const auto boundary = sentenceEnd(scan)) {
                    end = *boundary;
                    scan = *boundary;
                    break;
                }
                scan = nextScalarOffset(text, scan);
            }
            sentences.push_back(SentenceSpan{
                start,
                end.value_or(lastNonSpace)});
        }
        if (sentences.empty()) {
            return std::nullopt;
        }

        const auto containing = std::ranges::find_if(
            sentences,
            [current](const SentenceSpan &sentence) {
                return sentence.start <= current
                    && current < sentence.end;
            });
        if (containing != sentences.end()) {
            const std::size_t index =
                static_cast<std::size_t>(
                    std::distance(
                        sentences.begin(), containing));
            const std::size_t last = std::min(
                sentences.size() - 1,
                index + count - 1);
            std::size_t start = containing->start;
            std::size_t end = sentences[last].end;
            if (around) {
                const std::size_t beforeTrailing = end;
                while (end < text.size()
                       && isSpace(end)) {
                    end = nextScalarOffset(text, end);
                }
                if (end == beforeTrailing) {
                    const std::size_t lowerBound = index == 0
                        ? 0
                        : sentences[index - 1].end;
                    while (start > lowerBound) {
                        const std::size_t previous =
                            previousScalarOffset(text, start);
                        if (!isSpace(previous)) {
                            break;
                        }
                        start = previous;
                    }
                }
            }
            return TextRange{start, end, false};
        }

        const auto next = std::ranges::find_if(
            sentences,
            [current](const SentenceSpan &sentence) {
                return sentence.start > current;
            });
        const std::size_t nextIndex =
            next == sentences.end()
            ? sentences.size()
            : static_cast<std::size_t>(
                  std::distance(sentences.begin(), next));
        const std::size_t gapStart = nextIndex == 0
            ? 0
            : sentences[nextIndex - 1].end;
        const std::size_t gapEnd = nextIndex < sentences.size()
            ? sentences[nextIndex].start
            : text.size();
        if (current < gapStart || current >= gapEnd) {
            return std::nullopt;
        }
        if (!around && count == 1) {
            return gapStart < gapEnd
                ? std::optional<TextRange>(TextRange{
                      gapStart, gapEnd, false})
                : std::nullopt;
        }
        if (nextIndex < sentences.size()) {
            const std::size_t sentenceCount = around
                ? count
                : count - 1;
            const std::size_t last = std::min(
                sentences.size() - 1,
                nextIndex
                    + std::max<std::size_t>(
                          1, sentenceCount) - 1);
            return TextRange{
                gapStart,
                sentences[last].end,
                false};
        }
        // Trailing whitespace has no following sentence; around-sentence
        // falls back to the preceding sentence, while inner-sentence
        // still addresses the whitespace run itself.
        return around && !sentences.empty()
            ? std::optional<TextRange>(TextRange{
                  sentences.back().start,
                  gapEnd,
                  false})
            : std::optional<TextRange>(TextRange{
                  gapStart, gapEnd, false});
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Cursor>
VkCore::Implementation::characterSearchTarget(
    const Buffer &buffer,
    Cursor cursor,
    const CharacterSearch &search,
    const std::size_t count,
    const bool repeated) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    const std::size_t length =
        lineLength(buffer, cursor.line);
    std::size_t column = cursor.column;
    // Neovim's searchc() deliberately rejects the first candidate when
    // repeating a single t/T motion. This makes ';' and ',' move instead
    // of getting stuck next to the character found by the prior motion.
    bool skipFirstCandidate =
        repeated && search.till && count == 1;
    for (std::size_t occurrence = 0;
         occurrence < count;
         ++occurrence) {
        while (true) {
            if (search.forward) {
                const std::size_t next =
                    nextColumnAllowEnd(
                        buffer, cursor.line, column);
                if (next >= length) {
                    return std::nullopt;
                }
                column = next;
            } else {
                const std::size_t previous =
                    previousColumnAllowEnd(
                        buffer, cursor.line, column);
                if (previous == column) {
                    return std::nullopt;
                }
                column = previous;
            }

            const bool matches =
                scalarAt(
                    buffer, cursor.line, column)
                == search.target;
            if (matches && !skipFirstCandidate) {
                break;
            }
            skipFirstCandidate = false;
        }
    }

    if (search.till) {
        column = search.forward
            ? previousColumnAllowEnd(
                  buffer, cursor.line, column)
            : nextColumnAllowEnd(
                  buffer, cursor.line, column);
    }
    return Cursor{
        cursor.line,
        safeColumn(
            buffer, cursor.line, column, false)};
}

[[nodiscard]] std::size_t
VkCore::Implementation::nextScalarOffset(
    const std::u16string &text,
    const std::size_t offset) noexcept
{
    if (offset >= text.size()) {
        return text.size();
    }
    if (QChar::isHighSurrogate(text[offset])
        && offset + 1 < text.size()
        && QChar::isLowSurrogate(text[offset + 1])) {
        return offset + 2;
    }
    return offset + 1;
}

[[nodiscard]] std::size_t
VkCore::Implementation::nextScalarOffset(const BufferTextView& text,
                                         const std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return text.size();
    }
    return QChar::isHighSurrogate(text[offset]) && offset + 1 < text.size() &&
                   QChar::isLowSurrogate(text[offset + 1])
               ? offset + 2
               : offset + 1;
}

[[nodiscard]] std::size_t
VkCore::Implementation::previousScalarOffset(const std::u16string& text,
                                             std::size_t offset) noexcept {
    if (offset == 0) {
        return 0;
    }
    --offset;
    if (offset > 0 && QChar::isLowSurrogate(text[offset]) &&
        QChar::isHighSurrogate(text[offset - 1])) {
        --offset;
    }
    return offset;
}

[[nodiscard]] std::size_t
VkCore::Implementation::previousScalarOffset(const BufferTextView& text,
                                             std::size_t offset) noexcept {
    if (offset == 0) {
        return 0;
    }
    --offset;
    if (offset > 0
        && QChar::isLowSurrogate(text[offset])
        && QChar::isHighSurrogate(text[offset - 1])) {
        --offset;
    }
    return offset;
}

[[nodiscard]] std::optional<Cursor>
VkCore::Implementation::matchingPairTarget(
    const Buffer &buffer,
    Cursor cursor) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    const std::size_t length =
        lineLength(buffer, cursor.line);
    std::size_t column = cursor.column;
    char16_t opening = u'\0';
    char16_t closing = u'\0';
    bool forward = true;
    while (column < length) {
        const char16_t value =
            buffer.text()[
                buffer.lineStarts[cursor.line]
                + column];
        switch (value) {
        case u'(':
            opening = u'(';
            closing = u')';
            forward = true;
            break;
        case u'[':
            opening = u'[';
            closing = u']';
            forward = true;
            break;
        case u'{':
            opening = u'{';
            closing = u'}';
            forward = true;
            break;
        case u')':
            opening = u'(';
            closing = u')';
            forward = false;
            break;
        case u']':
            opening = u'[';
            closing = u']';
            forward = false;
            break;
        case u'}':
            opening = u'{';
            closing = u'}';
            forward = false;
            break;
        default:
            break;
        }
        if (opening != u'\0') {
            break;
        }
        const std::size_t next =
            nextColumnAllowEnd(
                buffer, cursor.line, column);
        if (next == column) {
            return std::nullopt;
        }
        column = next;
    }
    if (opening == u'\0') {
        return std::nullopt;
    }

    const BufferTextView& text = buffer.text();
    const std::size_t initial =
        buffer.lineStarts[cursor.line] + column;
    std::size_t depth = 1;
    if (forward) {
        for (std::size_t position =
                 nextScalarOffset(text, initial);
             position < text.size();
             position =
                 nextScalarOffset(text, position)) {
            const char16_t value = text[position];
            if (value == opening) {
                ++depth;
            } else if (
                value == closing
                && --depth == 0) {
                return cursorAtOffset(
                    buffer, position);
            }
        }
        return std::nullopt;
    }

    std::size_t position = initial;
    while (position > 0) {
        position =
            previousScalarOffset(text, position);
        const char16_t value = text[position];
        if (value == closing) {
            ++depth;
        } else if (
            value == opening
            && --depth == 0) {
            return cursorAtOffset(
                buffer, position);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Cursor>
VkCore::Implementation::nextWordStart(
    const Buffer &buffer,
    Cursor cursor,
    const bool bigWord) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    std::size_t line = cursor.line;
    std::size_t column = cursor.column;
    std::size_t length = lineLength(buffer, line);

    if (length == 0) {
        ++line;
        column = 0;
    } else if (
        wordClass(buffer, line, column, bigWord)
            != WordClass::White) {
        const WordClass initialClass =
            wordClass(buffer, line, column, bigWord);
        do {
            column = nextColumnAllowEnd(
                buffer, line, column);
        } while (
            column < length
            && wordClass(
                   buffer,
                   line,
                   column,
                   bigWord) == initialClass);
    }

    while (line < buffer.lineStarts.size()) {
        length = lineLength(buffer, line);
        if (length == 0) {
            return Cursor{line, 0};
        }
        while (column < length
               && wordClass(
                      buffer,
                      line,
                      column,
                      bigWord) == WordClass::White) {
            column = nextColumnAllowEnd(
                buffer, line, column);
        }
        if (column < length) {
            return Cursor{line, column};
        }
        ++line;
        column = 0;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Cursor>
VkCore::Implementation::previousWordStart(
    const Buffer &buffer,
    Cursor cursor,
    const bool bigWord) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    std::size_t line = cursor.line;
    std::size_t boundary = cursor.column;
    const std::size_t currentLength =
        lineLength(buffer, line);

    const WordClass initialClass =
        wordClass(
            buffer,
            line,
            cursor.column,
            bigWord);
    if (currentLength != 0
        && initialClass != WordClass::White) {
        std::size_t start = cursor.column;
        while (start > 0) {
            const std::size_t previous =
                previousColumnAllowEnd(
                    buffer, line, start);
            if (wordClass(
                    buffer,
                    line,
                    previous,
                    bigWord) != initialClass) {
                break;
            }
            start = previous;
        }
        if (start < cursor.column) {
            return Cursor{line, start};
        }
        boundary = start;
    }

    while (true) {
        while (boundary > 0) {
            const std::size_t previous =
                previousColumnAllowEnd(
                    buffer, line, boundary);
            boundary = previous;
            const WordClass previousClass =
                wordClass(
                    buffer,
                    line,
                    previous,
                    bigWord);
            if (previousClass == WordClass::White) {
                continue;
            }
            std::size_t start = previous;
            while (start > 0) {
                const std::size_t before =
                    previousColumnAllowEnd(
                        buffer, line, start);
                if (wordClass(
                        buffer,
                        line,
                        before,
                        bigWord) != previousClass) {
                    break;
                }
                start = before;
            }
            return Cursor{line, start};
        }
        if (line == 0) {
            return std::nullopt;
        }
        --line;
        boundary = lineLength(buffer, line);
        if (boundary == 0) {
            return Cursor{line, 0};
        }
    }
}

[[nodiscard]] std::optional<Cursor>
VkCore::Implementation::nextWordEnd(
    const Buffer &buffer,
    Cursor cursor,
    const bool bigWord) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    std::size_t line = cursor.line;
    std::size_t column = cursor.column;
    std::size_t length = lineLength(buffer, line);

    if (length != 0
        && wordClass(buffer, line, column, bigWord)
            != WordClass::White) {
        const WordClass initialClass =
            wordClass(buffer, line, column, bigWord);
        std::size_t end = column;
        std::size_t next = nextColumnAllowEnd(
            buffer, line, end);
        while (next < length
               && wordClass(
                      buffer,
                      line,
                      next,
                      bigWord) == initialClass) {
            end = next;
            next = nextColumnAllowEnd(
                buffer, line, end);
        }
        if (end != column) {
            return Cursor{line, end};
        }
        column = next;
    } else if (length == 0) {
        ++line;
        column = 0;
    }

    while (line < buffer.lineStarts.size()) {
        length = lineLength(buffer, line);
        while (column < length
               && wordClass(
                      buffer,
                      line,
                      column,
                      bigWord) == WordClass::White) {
            column = nextColumnAllowEnd(
                buffer, line, column);
        }
        if (column < length) {
            const WordClass foundClass =
                wordClass(
                    buffer,
                    line,
                    column,
                    bigWord);
            std::size_t end = column;
            std::size_t next =
                nextColumnAllowEnd(
                    buffer, line, end);
            while (next < length
                   && wordClass(
                          buffer,
                          line,
                          next,
                          bigWord) == foundClass) {
                end = next;
                next = nextColumnAllowEnd(
                    buffer, line, end);
            }
            return Cursor{line, end};
        }
        ++line;
        column = 0;
    }
    return std::nullopt;
}

[[nodiscard]] Cursor VkCore::Implementation::movedWordCursor(
    const Buffer &buffer,
    Cursor cursor,
    const Command command,
    const std::size_t count,
    const bool bigWord) const noexcept
{
    for (std::size_t iteration = 0;
         iteration < count;
         ++iteration) {
        const std::optional<Cursor> next =
            command == Command::BigWordForward
                || command == Command::WordForward
            ? nextWordStart(buffer, cursor, bigWord)
            : command == Command::BigWordBackward
                || command == Command::WordBackward
            ? previousWordStart(buffer, cursor, bigWord)
            : nextWordEnd(buffer, cursor, bigWord);
        if (!next) {
            cursor = (command
                          == Command::BigWordBackward
                      || command
                          == Command::WordBackward)
                ? Cursor{}
                : lastBufferCursor(buffer);
            break;
        }
        cursor = *next;
    }
    return cursor;
}

[[nodiscard]] VkCore::Implementation::WordClass VkCore::Implementation::wordClassAtOffset(
    const Buffer &buffer,
    const std::size_t offset,
    const bool bigWord) const noexcept
{
    if (offset >= buffer.text().size()) {
        return WordClass::White;
    }
    const BufferTextView& text = buffer.text();
    const char16_t first = text[offset];
    const char32_t scalar = QChar::isHighSurrogate(first)
            && offset + 1 < text.size()
            && QChar::isLowSurrogate(text[offset + 1])
        ? QChar::surrogateToUcs4(first, text[offset + 1])
        : static_cast<char32_t>(first);
    if (u_isUWhiteSpace(static_cast<UChar32>(scalar))) {
        return WordClass::White;
    }
    if (bigWord) {
        return WordClass::Keyword;
    }
    const auto category = static_cast<UCharCategory>(
        u_charType(static_cast<UChar32>(scalar)));
    const bool mark = category == U_NON_SPACING_MARK
        || category == U_COMBINING_SPACING_MARK
        || category == U_ENCLOSING_MARK;
    return scalar == U'_'
            || u_isalnum(static_cast<UChar32>(scalar))
            || mark
        ? WordClass::Keyword
        : WordClass::Punctuation;
}

[[nodiscard]] std::optional<Cursor> VkCore::Implementation::previousWordEnd(
    const Buffer &buffer,
    Cursor cursor,
    const bool bigWord) const noexcept
{
    const BufferTextView& text = buffer.text();
    if (text.empty()) {
        return std::nullopt;
    }
    cursor = clampCursor(buffer, cursor, false);
    std::size_t scan = std::min(
        offset(buffer, cursor), text.size() - 1);
    const WordClass current = wordClassAtOffset(
        buffer, scan, bigWord);
    if (current != WordClass::White) {
        while (scan > 0) {
            const std::size_t previous =
                previousScalarOffset(text, scan);
            if (wordClassAtOffset(
                    buffer, previous, bigWord)
                != current) {
                break;
            }
            scan = previous;
        }
        if (scan == 0) {
            return std::nullopt;
        }
        scan = previousScalarOffset(text, scan);
    }
    while (wordClassAtOffset(buffer, scan, bigWord)
           == WordClass::White) {
        if (scan == 0) {
            return std::nullopt;
        }
        scan = previousScalarOffset(text, scan);
    }
    return cursorAtOffset(buffer, scan);
}

[[nodiscard]] Cursor VkCore::Implementation::movedWordEndBackward(
    const Buffer &buffer,
    Cursor cursor,
    const std::size_t count,
    const bool bigWord) const noexcept
{
    for (std::size_t step = 0; step < count; ++step) {
        const auto previous = previousWordEnd(
            buffer, cursor, bigWord);
        if (!previous) {
            return Cursor{};
        }
        cursor = *previous;
    }
    return cursor;
}

[[nodiscard]] std::vector<std::size_t> VkCore::Implementation::sentenceStarts(
    const Buffer &buffer) const
{
    const BufferTextView& text = buffer.text();
    std::vector<std::size_t> starts{0};
    starts.reserve(buffer.lineStarts.size() * 2);

    // Empty lines are explicit sentence boundaries in Vim, and the first
    // non-empty line following them is a separate boundary as well.
    for (std::size_t line = 0;
         line < buffer.lineStarts.size();
         ++line) {
        if (lineLength(buffer, line) == 0) {
            starts.push_back(buffer.lineStarts[line]);
        } else if (line > 0
                   && lineLength(buffer, line - 1) == 0) {
            starts.push_back(buffer.lineStarts[line]);
        }
    }

    const auto isCloser = [](const char16_t value) {
        return value == u')' || value == u']'
            || value == u'\'' || value == u'"';
    };
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] != u'.' && text[at] != u'!'
            && text[at] != u'?') {
            continue;
        }
        std::size_t after = at + 1;
        while (after < text.size() && isCloser(text[after])) {
            ++after;
        }
        if (after < text.size()
            && !QChar(text[after]).isSpace()) {
            continue;
        }
        while (after < text.size()
               && QChar(text[after]).isSpace()) {
            ++after;
        }
        if (after < text.size()) {
            starts.push_back(after);
        }
    }
    std::ranges::sort(starts);
    starts.erase(
        std::unique(starts.begin(), starts.end()),
        starts.end());
    return starts;
}

[[nodiscard]] Cursor VkCore::Implementation::movedSentenceCursor(
    const Buffer &buffer,
    Cursor cursor,
    const std::size_t count,
    const bool forward) const
{
    const std::vector<std::size_t> starts =
        sentenceStarts(buffer);
    std::size_t current = offset(
        buffer, clampCursor(buffer, cursor, false));
    for (std::size_t step = 0; step < count; ++step) {
        if (forward) {
            const auto next = std::upper_bound(
                starts.cbegin(), starts.cend(), current);
            if (next == starts.cend()) {
                return lastBufferCursor(buffer);
            }
            current = *next;
        } else {
            auto previous = std::lower_bound(
                starts.cbegin(), starts.cend(), current);
            if (previous == starts.cbegin()) {
                return Cursor{};
            }
            --previous;
            current = *previous;
        }
    }
    return cursorAtOffset(buffer, current);
}

[[nodiscard]] Cursor VkCore::Implementation::movedParagraphCursor(
    const Buffer &buffer,
    Cursor cursor,
    const std::size_t count,
    const bool forward) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    std::size_t line = cursor.line;
    for (std::size_t step = 0; step < count; ++step) {
        if (forward) {
            if (lineLength(buffer, line) == 0) {
                while (line + 1 < buffer.lineStarts.size()
                       && lineLength(buffer, line) == 0) {
                    ++line;
                }
            }
            while (line + 1 < buffer.lineStarts.size()
                   && lineLength(buffer, line) != 0) {
                ++line;
            }
            if (line + 1 == buffer.lineStarts.size()
                && lineLength(buffer, line) != 0) {
                return lastBufferCursor(buffer);
            }
        } else if (lineLength(buffer, line) == 0) {
            while (line > 0
                   && lineLength(buffer, line) == 0) {
                --line;
            }
            while (line > 0
                   && lineLength(buffer, line - 1) != 0) {
                --line;
            }
            if (line > 0
                && lineLength(buffer, line - 1) == 0) {
                --line;
            }
        } else if (line > 0
                   && lineLength(buffer, line - 1) == 0) {
            --line;
        } else {
            while (line > 0
                   && lineLength(buffer, line - 1) != 0) {
                --line;
            }
        }
    }
    return Cursor{line, 0};
}

[[nodiscard]] bool VkCore::Implementation::isSectionBoundary(
    const Buffer &buffer,
    const std::size_t line) const noexcept
{
    const std::size_t length = lineLength(buffer, line);
    if (length == 0) {
        return false;
    }
    const std::size_t start = buffer.lineStarts[line];
    const BufferTextView& text = buffer.text();
    if (text[start] == u'\f') {
        return true;
    }
    if (text[start] != u'.' || length < 2) {
        return false;
    }

    // Neovim's default 'sections' value is "SHNHH HUnhsh": pairs of
    // nroff macro characters following a dot in column zero.
    constexpr std::array<std::u16string_view, 6> macros{
        u"SH", u"NH", u"H ", u"HU", u"nh", u"sh"};
    return std::ranges::any_of(
        macros,
        [&text, start, length](
            const std::u16string_view macro) {
            if (text[start + 1] != macro[0]) {
                return false;
            }
            if (macro[1] == u' ') {
                return length == 2
                    || text[start + 2] == u' '
                    || text[start + 2] == u'\t';
            }
            return length >= 3
                && text[start + 2] == macro[1];
        });
}

[[nodiscard]] bool VkCore::Implementation::isSectionMotionTarget(
    const Buffer &buffer,
    const std::size_t line,
    const bool closingBrace) const noexcept
{
    if (isSectionBoundary(buffer, line)) {
        return true;
    }
    const std::size_t length = lineLength(buffer, line);
    return length != 0
        && buffer.text()[buffer.lineStarts[line]]
            == (closingBrace ? u'}' : u'{');
}

[[nodiscard]] Cursor VkCore::Implementation::movedSectionCursor(
    const Buffer &buffer,
    Cursor cursor,
    const std::size_t count,
    const bool forward,
    const bool closingBrace,
    const bool operatorForwardStart) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    std::size_t line = cursor.line;
    const std::size_t lastLine =
        buffer.lineStarts.size() - 1;
    for (std::size_t step = 0; step < count; ++step) {
        std::optional<std::size_t> target;
        if (forward) {
            for (std::size_t candidate = line + 1;
                 candidate <= lastLine;
                 ++candidate) {
                const std::size_t length =
                    lineLength(buffer, candidate);
                const bool closesFunction =
                    operatorForwardStart
                    && length != 0
                    && buffer.text()[
                           buffer.lineStarts[candidate]]
                        == u'}';
                if (closesFunction) {
                    // With an operator, ]] stops below a column-zero },
                    // so d]] includes that closing line. At EOF there is
                    // no line below and the brace itself remains.
                    target = std::min(
                        candidate + 1, lastLine);
                    break;
                }
                if (isSectionMotionTarget(
                        buffer,
                        candidate,
                        closingBrace)) {
                    target = candidate;
                    break;
                }
            }
            line = target.value_or(lastLine);
        } else {
            std::size_t candidate = line;
            while (candidate > 0) {
                --candidate;
                if (isSectionMotionTarget(
                        buffer,
                        candidate,
                        closingBrace)) {
                    target = candidate;
                    break;
                }
            }
            line = target.value_or(0);
        }
    }
    return Cursor{line, 0};
}

[[nodiscard]] std::optional<std::size_t>
VkCore::Implementation::wordForwardOperatorEnd(
    const Buffer &buffer,
    Cursor cursor,
    const std::size_t count,
    const bool bigWord) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    for (std::size_t iteration = 0;
         iteration < count;
        ++iteration) {
        const std::optional<Cursor> next =
            nextWordStart(buffer, cursor, bigWord);
        if (iteration + 1 == count) {
            if (!next) {
                return buffer.text().size();
            }
            if (wordClass(
                    buffer,
                    cursor.line,
                    cursor.column,
                    bigWord) == WordClass::White
                || next->line == cursor.line) {
                return std::nullopt;
            }

            const std::size_t length =
                lineLength(buffer, cursor.line);
            const WordClass initialClass =
                wordClass(
                    buffer,
                    cursor.line,
                    cursor.column,
                    bigWord);
            std::size_t column = cursor.column;
            while (column < length
                   && wordClass(
                          buffer,
                          cursor.line,
                          column,
                          bigWord) == initialClass) {
                column = nextColumnAllowEnd(
                    buffer, cursor.line, column);
            }
            while (column < length
                   && wordClass(
                          buffer,
                          cursor.line,
                          column,
                          bigWord) == WordClass::White) {
                column = nextColumnAllowEnd(
                    buffer, cursor.line, column);
            }
            if (column == length) {
                // Neovim's operator w/W exception stops before the line
                // break when the last word traversed ends this line.
                return buffer.lineStarts[cursor.line]
                    + length;
            }
            return std::nullopt;
        }
        if (!next) {
            return buffer.text().size();
        }
        cursor = *next;
    }
    return std::nullopt;
}

} // namespace vkui::vk

#include "VkCoreInternal.h"

namespace vkui::vk {

[[nodiscard]] bool VkCore::Implementation::promptMatches(
    const WindowId window,
    const InputTargetId inputTarget) const noexcept
{
    return promptInput
        && promptInput->window == window
        && promptInput->inputTarget == inputTarget;
}

void VkCore::Implementation::finishPrompt(
    DispatchResult &result,
    const EventType type)
{
    if (!promptInput) {
        return;
    }
    Event event;
    event.type = type;
    event.view = promptInput->window;
    event.inputTarget = promptInput->inputTarget;
    event.promptSession = promptInput->id;
    event.promptText = std::move(promptInput->text);
    promptInput.reset();
    result.events.push_back(std::move(event));
}

VkCore::Implementation::Implementation()
{
    input.setLeader(
        detail::KeySequence{
            detail::characterKey(U' ')});
}

[[nodiscard]] Mode VkCore::Implementation::effectiveMode() const noexcept
{
    if (baseMode == Mode::Insert
        || baseMode == Mode::Replace
        || baseMode == Mode::Visual) {
        return baseMode;
    }
    if (pendingOperator) {
        return Mode::OperatorPending;
    }
    return Mode::Normal;
}

[[nodiscard]] std::size_t VkCore::Implementation::lineLength(
    const Buffer &buffer,
    const std::size_t line) const
{
    if (line >= buffer.lineStarts.size()) {
        return 0;
    }
    const std::size_t start = buffer.lineStarts[line];
    const std::size_t rawEnd =
        line + 1 < buffer.lineStarts.size()
        ? buffer.lineStarts[line + 1]
        : buffer.text().size();
    return rawEnd > start
               && buffer.text()[rawEnd - 1] == u'\n'
        ? rawEnd - start - 1
        : rawEnd - start;
}

[[nodiscard]] std::size_t VkCore::Implementation::graphemeCellWidth(
    const std::u16string_view grapheme) noexcept
{
    bool hasBase = false;
    bool emojiPresentation = false;
    bool hasEmoji = false;
    bool hasEmojiVariationSelector = false;
    std::size_t width = 1;
    for (std::size_t index = 0;
         index < grapheme.size();
         ++index) {
        char32_t scalar = grapheme[index];
        if (QChar::isHighSurrogate(grapheme[index])
            && index + 1 < grapheme.size()
            && QChar::isLowSurrogate(
                grapheme[index + 1])) {
            const char16_t high = grapheme[index];
            const char16_t low = grapheme[index + 1];
            ++index;
            scalar = QChar::surrogateToUcs4(
                high, low);
        }
        if (scalar == 0xfe0fU) {
            hasEmojiVariationSelector = true;
            continue;
        }
        const auto codePoint =
            static_cast<UChar32>(scalar);
        const auto category =
            static_cast<UCharCategory>(
                u_charType(codePoint));
        if (category != U_NON_SPACING_MARK
            && category != U_ENCLOSING_MARK
            && category != U_COMBINING_SPACING_MARK
            && scalar != 0x200dU) {
            hasBase = true;
        }
        const int eastAsianWidth =
            u_getIntPropertyValue(
                codePoint,
                UCHAR_EAST_ASIAN_WIDTH);
        if (eastAsianWidth == U_EA_WIDE
            || eastAsianWidth == U_EA_FULLWIDTH) {
            width = 2;
        }
        if (u_hasBinaryProperty(
                codePoint,
                UCHAR_EMOJI_PRESENTATION)) {
            emojiPresentation = true;
        }
        hasEmoji = hasEmoji
            || u_hasBinaryProperty(
                codePoint, UCHAR_EMOJI);
    }
    // A standalone combining sequence still needs a cursor cell. For a
    // normal grapheme, marks and joiners inherit the base cell width.
    if (!hasBase) {
        return 1;
    }
    return emojiPresentation
            || (hasEmoji
                && hasEmojiVariationSelector)
        ? 2
        : width;
}

[[nodiscard]] const DisplayLine &VkCore::Implementation::displayLine(
    const Buffer &buffer,
    const std::size_t rawLine) const
{
    const std::size_t line = std::min(
        rawLine, buffer.lineStarts.size() - 1);
    if (const auto cached = buffer.displayLines.find(line);
        cached != buffer.displayLines.cend()) {
        return cached->second;
    }

    DisplayLine layout;
    const std::size_t length = lineLength(buffer, line);
    layout.bufferLength = length;
    const std::size_t start = buffer.lineStarts[line];
    if (length == 0) {
        return buffer.displayLines
            .try_emplace(line, std::move(layout))
            .first->second;
    }

    const auto lineBegin =
        buffer.text().cbegin()
        + static_cast<std::ptrdiff_t>(start);
    const auto lineEnd = lineBegin
        + static_cast<std::ptrdiff_t>(length);
    const bool asciiOnly = std::ranges::all_of(
        lineBegin,
        lineEnd,
        [](const char16_t character) {
            return character < char16_t{0x80};
        });
    if (asciiOnly) {
        std::size_t bufferColumn = 0;
        std::size_t displayColumn = 0;
        while (bufferColumn < length) {
            const auto tab = std::find(
                lineBegin
                    + static_cast<std::ptrdiff_t>(
                        bufferColumn),
                lineEnd,
                u'\t');
            const std::size_t tabColumn =
                static_cast<std::size_t>(
                    std::distance(lineBegin, tab));
            displayColumn += tabColumn - bufferColumn;
            bufferColumn = tabColumn;
            if (tab == lineEnd) {
                break;
            }
            const std::size_t cellWidth =
                buffer.tabStop
                - (displayColumn % buffer.tabStop);
            layout.specialCells.push_back(
                DisplayCell{
                    bufferColumn,
                    bufferColumn + 1,
                    displayColumn,
                    displayColumn + cellWidth,
                    true});
            ++bufferColumn;
            displayColumn += cellWidth;
        }
        layout.width = displayColumn;
        return buffer.displayLines
            .try_emplace(line, std::move(layout))
            .first->second;
    }

    const QString text = QString::fromRawData(
        reinterpret_cast<const QChar *>(
            buffer.text().data() + start),
        static_cast<qsizetype>(length));
    QTextBoundaryFinder boundaries(
        QTextBoundaryFinder::Grapheme, text);
    std::size_t bufferColumn = 0;
    std::size_t displayColumn = 0;
    while (bufferColumn < length) {
        boundaries.setPosition(
            static_cast<qsizetype>(bufferColumn));
        qsizetype rawEnd = boundaries.toNextBoundary();
        if (rawEnd <= static_cast<qsizetype>(bufferColumn)
            || rawEnd > static_cast<qsizetype>(length)) {
            rawEnd = static_cast<qsizetype>(
                std::min(
                    length,
                    bufferColumn
                        + (QChar::isHighSurrogate(
                               buffer.text()[
                                   start + bufferColumn])
                           && bufferColumn + 1 < length
                           && QChar::isLowSurrogate(
                               buffer.text()[
                                   start + bufferColumn + 1])
                               ? 2
                               : 1)));
        }
        const std::size_t bufferEnd =
            static_cast<std::size_t>(rawEnd);
        const bool tab = bufferEnd == bufferColumn + 1
            && buffer.text()[start + bufferColumn]
                == u'\t';
        const std::size_t cellWidth = tab
            ? buffer.tabStop
                - (displayColumn % buffer.tabStop)
            : graphemeCellWidth(
                  std::u16string_view(
                      buffer.text().data()
                          + start + bufferColumn,
                      bufferEnd - bufferColumn));
        if (tab
            || bufferEnd - bufferColumn != 1
            || cellWidth != 1) {
            layout.specialCells.push_back(
                DisplayCell{
                    bufferColumn,
                    bufferEnd,
                    displayColumn,
                    displayColumn + cellWidth,
                    tab});
        }
        bufferColumn = bufferEnd;
        displayColumn += cellWidth;
    }
    layout.width = displayColumn;
    return buffer.displayLines
        .try_emplace(line, std::move(layout))
        .first->second;
}

[[nodiscard]] std::size_t VkCore::Implementation::displayColumnForBufferColumn(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t rawColumn) const
{
    if (rawColumn == 0) {
        return 0;
    }
    const DisplayLine &layout = displayLine(buffer, line);
    const std::size_t column = std::min(
        rawColumn, layout.bufferLength);
    if (column >= layout.bufferLength) {
        return layout.width;
    }
    const auto after = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.bufferEnd <= value;
        });
    if (after != layout.specialCells.cend()
        && after->bufferStart <= column) {
        return after->displayStart;
    }
    if (after == layout.specialCells.cbegin()) {
        return column;
    }
    const DisplayCell &previous = *std::prev(after);
    return previous.displayEnd
        + (column - previous.bufferEnd);
}

[[nodiscard]] DisplayPosition VkCore::Implementation::displayPositionForColumn(
    const Buffer &buffer,
    const std::size_t rawLine,
    const std::size_t displayColumn) const
{
    const std::size_t line = std::min(
        rawLine, buffer.lineStarts.size() - 1);
    const DisplayLine &layout = displayLine(buffer, line);
    const auto after = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        displayColumn,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.displayEnd <= value;
        });
    if (after != layout.specialCells.cend()
        && after->displayStart <= displayColumn) {
        return DisplayPosition{
            Cursor{line, after->bufferStart},
            displayColumn};
    }
    const std::size_t bufferColumn =
        after == layout.specialCells.cbegin()
        ? displayColumn
        : std::prev(after)->bufferEnd
            + (displayColumn
               - std::prev(after)->displayEnd);
    return DisplayPosition{
        Cursor{
            line,
            std::min(bufferColumn, layout.bufferLength)},
        displayColumn};
}

[[nodiscard]] DisplayBoundary VkCore::Implementation::displayBoundary(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t column) const
{
    const DisplayLine &layout = displayLine(buffer, line);
    if (column >= layout.width) {
        const std::size_t length = layout.bufferLength;
        return DisplayBoundary{
            column,
            length,
            length,
            layout.width,
            std::max(layout.width, column)};
    }
    const auto after = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.displayEnd <= value;
        });
    if (after != layout.specialCells.cend()
        && after->displayStart <= column) {
        if (column == after->displayStart) {
            return DisplayBoundary{
                column,
                after->bufferStart,
                after->bufferStart,
                column,
                column};
        }
        return DisplayBoundary{
            column,
            after->bufferStart,
            after->bufferEnd,
            after->displayStart,
            after->displayEnd};
    }
    const std::size_t bufferColumn =
        after == layout.specialCells.cbegin()
        ? column
        : std::prev(after)->bufferEnd
            + (column - std::prev(after)->displayEnd);
    return DisplayBoundary{
        column,
        std::min(bufferColumn, layout.bufferLength),
        std::min(bufferColumn, layout.bufferLength),
        column,
        column};
}

[[nodiscard]] std::pair<std::size_t, std::size_t>
VkCore::Implementation::displaySpan(
    const Buffer &buffer,
    const DisplayPosition &position,
    const bool blockVirtual) const
{
    const DisplayLine &layout = displayLine(
        buffer, position.buffer.line);
    const auto found = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        position.column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.displayEnd <= value;
        });
    if (found == layout.specialCells.cend()
        || found->displayStart > position.column) {
        return {position.column, position.column + 1};
    }
    if (blockVirtual && found->tab) {
        return {position.column, position.column + 1};
    }
    return {found->displayStart, found->displayEnd};
}

[[nodiscard]] DisplayPosition VkCore::Implementation::currentDisplayPosition(
    const View &view,
    const Buffer &buffer) const
{
    const auto cursor = view.cursors.find(view.buffer);
    const Cursor position = cursor == view.cursors.cend()
        ? Cursor{}
        : cursor->second;
    const auto display =
        view.displayColumns.find(view.buffer);
    return DisplayPosition{
        position,
        display == view.displayColumns.cend()
            ? displayColumnForBufferColumn(
                  buffer,
                  position.line,
                  position.column)
            : display->second};
}

void VkCore::Implementation::setDisplayPosition(
    View &view,
    const DisplayPosition position) const
{
    view.cursors[view.buffer] = position.buffer;
    view.displayColumns[view.buffer] = position.column;
}

[[nodiscard]] bool VkCore::Implementation::ownsVisualBlock(
    const WindowId window,
    const BufferId buffer) const noexcept
{
    return baseMode == Mode::Visual
        && visualSelection
        && visualSelection->kind == VisualKind::Block
        && visualSelection->window == window
        && visualSelection->buffer == buffer;
}

[[nodiscard]] std::optional<VisualBlockRange>
VkCore::Implementation::currentVisualBlockRange(
    const WindowId window,
    const View &view,
    const Buffer &buffer) const
{
    if (!ownsVisualBlock(window, view.buffer)) {
        return std::nullopt;
    }
    const DisplayPosition anchor{
        visualSelection->anchor,
        visualSelection->anchorDisplayColumn};
    const DisplayPosition cursor =
        currentDisplayPosition(view, buffer);
    const auto anchorSpan = displaySpan(
        buffer, anchor, true);
    const auto cursorSpan = displaySpan(
        buffer, cursor, true);
    return VisualBlockRange{
        std::min(anchor.buffer.line, cursor.buffer.line),
        std::max(anchor.buffer.line, cursor.buffer.line),
        std::min(anchorSpan.first, cursorSpan.first),
        std::max(anchorSpan.second, cursorSpan.second)};
}

[[nodiscard]] VisualBlockRow VkCore::Implementation::visualBlockRow(
    const Buffer &buffer,
    const std::size_t line,
    const VisualBlockRange &range) const
{
    const DisplayLine &layout = displayLine(buffer, line);
    const DisplayBoundary left = displayBoundary(
        buffer, line, range.firstColumn);
    const DisplayBoundary right = displayBoundary(
        buffer, line, range.lastColumnExclusive);
    const std::size_t selectedStart =
        range.firstColumn >= layout.width
        ? layout.bufferLength
        : left.bufferColumn;
    const std::size_t selectedEnd =
        range.lastColumnExclusive >= layout.width
        ? layout.bufferLength
        : right.cellBufferEnd != right.bufferColumn
        ? right.cellBufferEnd
        : right.bufferColumn;
    return VisualBlockRow{
        line,
        left,
        right,
        selectedStart,
        std::max(selectedStart, selectedEnd)};
}

[[nodiscard]] std::size_t VkCore::Implementation::visualColumnLeft(
    const DisplayLine &layout,
    std::size_t column,
    std::size_t count) const noexcept
{
    while (count != 0 && column != 0) {
        if (column > layout.width) {
            const std::size_t distance =
                column - layout.width;
            const std::size_t step = std::min(
                count, distance);
            column -= step;
            count -= step;
            continue;
        }

        const auto after = std::lower_bound(
            layout.specialCells.cbegin(),
            layout.specialCells.cend(),
            column,
            [](const DisplayCell &cell,
               const std::size_t value) {
                return cell.displayEnd < value;
            });
        if (after != layout.specialCells.cend()
            && after->displayStart < column
            && after->displayEnd >= column) {
            if (after->tab) {
                const std::size_t step = std::min(
                    count,
                    column - after->displayStart);
                column -= step;
                count -= step;
            } else {
                column = after->displayStart;
                --count;
            }
            continue;
        }

        const auto next = std::lower_bound(
            layout.specialCells.cbegin(),
            layout.specialCells.cend(),
            column,
            [](const DisplayCell &cell,
               const std::size_t value) {
                return cell.displayEnd <= value;
            });
        const std::size_t runStart =
            next == layout.specialCells.cbegin()
            ? 0
            : std::prev(next)->displayEnd;
        if (column > runStart) {
            const std::size_t step = std::min(
                count, column - runStart);
            column -= step;
            count -= step;
            continue;
        }
        if (next == layout.specialCells.cbegin()) {
            return 0;
        }
        const DisplayCell &previous = *std::prev(next);
        column = previous.tab
            ? previous.displayEnd - 1
            : previous.displayStart;
        --count;
    }
    return column;
}

[[nodiscard]] std::size_t VkCore::Implementation::visualColumnRight(
    const DisplayLine &layout,
    std::size_t column,
    std::size_t count) const noexcept
{
    while (count != 0) {
        if (column >= layout.width) {
            if (virtualEdit != VirtualEditMode::Block) {
                return layout.width;
            }
            const std::size_t available =
                std::numeric_limits<std::size_t>::max()
                - column;
            return column + std::min(count, available);
        }
        const auto current = std::lower_bound(
            layout.specialCells.cbegin(),
            layout.specialCells.cend(),
            column,
            [](const DisplayCell &cell,
               const std::size_t value) {
                return cell.displayEnd <= value;
            });
        if (current != layout.specialCells.cend()
            && current->displayStart <= column) {
            if (current->tab) {
                const std::size_t step = std::min(
                    count,
                    current->displayEnd - column);
                column += step;
                count -= step;
            } else {
                column = current->displayEnd;
                --count;
            }
            continue;
        }
        const std::size_t runEnd =
            current == layout.specialCells.cend()
            ? layout.width
            : current->displayStart;
        const std::size_t step = std::min(
            count, runEnd - column);
        column += step;
        count -= step;
    }
    return column;
}

[[nodiscard]] DisplayPosition VkCore::Implementation::movedVisualBlockPosition(
    const Buffer &buffer,
    const DisplayPosition origin,
    const Command command,
    const std::size_t count) const
{
    DisplayPosition destination = origin;
    if (command == Command::MoveUp) {
        destination.buffer.line =
            destination.buffer.line > count
            ? destination.buffer.line - count
            : 0;
    } else if (command == Command::MoveDown) {
        destination.buffer.line = std::min(
            buffer.lineStarts.size() - 1,
            destination.buffer.line + count);
    } else {
        const DisplayLine &layout = displayLine(
            buffer, destination.buffer.line);
        destination.column =
            command == Command::MoveLeft
            ? visualColumnLeft(
                  layout, destination.column, count)
            : visualColumnRight(
                  layout, destination.column, count);
    }
    if ((command == Command::MoveUp
         || command == Command::MoveDown)
        && virtualEdit != VirtualEditMode::Block) {
        destination.column = std::min(
            destination.column,
            displayLine(
                buffer,
                destination.buffer.line)
                .width);
    }
    return displayPositionForColumn(
        buffer,
        destination.buffer.line,
        destination.column);
}

[[nodiscard]] Cursor VkCore::Implementation::normalCursorForDisplayColumn(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t displayColumn) const
{
    DisplayPosition position = displayPositionForColumn(
        buffer, line, displayColumn);
    const DisplayLine &layout = displayLine(buffer, line);
    if (position.buffer.column == lineLength(buffer, line)
        && layout.bufferLength != 0) {
        position.buffer.column =
            previousColumnAllowEnd(
                buffer, line, layout.bufferLength);
    }
    return position.buffer;
}

[[nodiscard]] std::size_t VkCore::Implementation::normalColumnLimit(
    const Buffer &buffer,
    const std::size_t line) const
{
    const DisplayLine &layout = displayLine(buffer, line);
    if (layout.bufferLength == 0) {
        return 0;
    }
    return !layout.specialCells.empty()
            && layout.specialCells.back().bufferEnd
                == layout.bufferLength
        ? layout.specialCells.back().bufferStart
        : layout.bufferLength - 1;
}

[[nodiscard]] std::size_t VkCore::Implementation::safeColumn(
    const Buffer &buffer,
    const std::size_t line,
    std::size_t column,
    const bool allowEnd) const
{
    const std::size_t length = lineLength(buffer, line);
    if (column == 0 || length == 0) {
        return 0;
    }
    column = std::min(
        column,
        allowEnd
            ? length
            : normalColumnLimit(buffer, line));
    if (column == length) {
        return column;
    }
    const std::size_t lineStart = buffer.lineStarts[line];
    if (buffer.text()[lineStart + column]
        < char16_t{0x80}) {
        return column;
    }
    const DisplayLine &layout = displayLine(buffer, line);
    const auto found = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.bufferEnd <= value;
        });
    if (found != layout.specialCells.cend()
        && found->bufferStart <= column) {
        column = found->bufferStart;
    }
    return column;
}

[[nodiscard]] Cursor VkCore::Implementation::clampCursor(
    const Buffer &buffer,
    Cursor cursor,
    const bool allowEnd) const
{
    cursor.line = std::min(
        cursor.line, buffer.lineStarts.size() - 1);
    cursor.column = safeColumn(
        buffer, cursor.line, cursor.column, allowEnd);
    return cursor;
}

[[nodiscard]] std::size_t VkCore::Implementation::previousColumn(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t rawColumn) const
{
    std::size_t column = safeColumn(
        buffer, line, rawColumn, false);
    if (column == 0) {
        return 0;
    }
    const DisplayLine &layout = displayLine(buffer, line);
    const auto found = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.bufferEnd < value;
        });
    return found != layout.specialCells.cend()
            && found->bufferEnd == column
        ? found->bufferStart
        : column - 1;
}

[[nodiscard]] std::size_t VkCore::Implementation::nextColumn(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t rawColumn) const
{
    const std::size_t limit =
        normalColumnLimit(buffer, line);
    std::size_t column = safeColumn(
        buffer, line, rawColumn, false);
    if (column >= limit) {
        return limit;
    }
    return std::min(
        limit,
        nextColumnAllowEnd(
            buffer, line, column));
}

[[nodiscard]] std::size_t VkCore::Implementation::nextColumnAllowEnd(
    const Buffer &buffer,
    const std::size_t line,
    std::size_t column) const
{
    const std::size_t length = lineLength(buffer, line);
    column = safeColumn(
        buffer, line, column, true);
    if (column >= length) {
        return length;
    }
    const DisplayLine &layout = displayLine(buffer, line);
    const auto found = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.bufferStart < value;
        });
    return found != layout.specialCells.cend()
            && found->bufferStart == column
        ? found->bufferEnd
        : column + 1;
}

[[nodiscard]] std::size_t VkCore::Implementation::previousColumnAllowEnd(
    const Buffer &buffer,
    const std::size_t line,
    std::size_t column) const
{
    const std::size_t length = lineLength(buffer, line);
    column = std::min(column, length);
    if (column == 0) {
        return 0;
    }
    const DisplayLine &layout = displayLine(buffer, line);
    const auto found = std::lower_bound(
        layout.specialCells.cbegin(),
        layout.specialCells.cend(),
        column,
        [](const DisplayCell &cell,
           const std::size_t value) {
            return cell.bufferEnd < value;
        });
    if (found != layout.specialCells.cend()
        && found->bufferEnd == column) {
        return found->bufferStart;
    }
    return column - 1;
}

[[nodiscard]] std::size_t VkCore::Implementation::advanceWithinLine(
    const Buffer &buffer,
    const std::size_t line,
    std::size_t column,
    const std::size_t count) const
{
    for (std::size_t index = 0;
         index < count;
         ++index) {
        const std::size_t next =
            nextColumnAllowEnd(
                buffer, line, column);
        if (next == column) {
            break;
        }
        column = next;
    }
    return column;
}

[[nodiscard]] std::size_t VkCore::Implementation::retreatWithinLine(
    const Buffer &buffer,
    const std::size_t line,
    std::size_t column,
    const std::size_t count) const
{
    for (std::size_t index = 0;
         index < count;
         ++index) {
        const std::size_t previous =
            previousColumnAllowEnd(
                buffer, line, column);
        if (previous == column) {
            break;
        }
        column = previous;
    }
    return column;
}

[[nodiscard]] std::size_t VkCore::Implementation::offset(
    const Buffer &buffer,
    Cursor cursor) const noexcept
{
    cursor.line = std::min(
        cursor.line, buffer.lineStarts.size() - 1);
    cursor.column = std::min(
        cursor.column, lineLength(buffer, cursor.line));
    return buffer.lineStarts[cursor.line] + cursor.column;
}

[[nodiscard]] Cursor VkCore::Implementation::cursorAtOffset(
    const Buffer &buffer,
    const std::size_t rawOffset,
    const bool allowEnd) const
{
    const std::size_t bounded =
        std::min(rawOffset, buffer.text().size());
    const auto upper = std::upper_bound(
        buffer.lineStarts.cbegin(),
        buffer.lineStarts.cend(),
        bounded);
    const std::size_t line =
        upper == buffer.lineStarts.cbegin()
        ? 0
        : static_cast<std::size_t>(
              std::distance(
                  buffer.lineStarts.cbegin(),
                  upper) - 1);
    return clampCursor(
        buffer,
        Cursor{
            line,
            bounded - buffer.lineStarts[line]},
        allowEnd);
}

[[nodiscard]] std::size_t VkCore::Implementation::cursorCharacterEnd(
    const Buffer &buffer,
    Cursor cursor) const noexcept
{
    cursor = clampCursor(buffer, cursor, false);
    const std::size_t length =
        lineLength(buffer, cursor.line);
    if (length == 0) {
        return offset(buffer, cursor);
    }
    return buffer.lineStarts[cursor.line]
        + nextColumnAllowEnd(
            buffer, cursor.line, cursor.column);
}

[[nodiscard]] bool VkCore::Implementation::bigWordCharacter(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t column) const noexcept
{
    const std::size_t length = lineLength(buffer, line);
    if (column >= length) {
        return false;
    }
    return !QChar(
                buffer.text()[
                    buffer.lineStarts[line] + column])
                .isSpace();
}

[[nodiscard]] VkCore::Implementation::WordClass VkCore::Implementation::wordClass(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t column,
    const bool bigWord) const noexcept
{
    if (column >= lineLength(buffer, line)) {
        return WordClass::White;
    }
    const char32_t scalar = scalarAt(
        buffer, line, column);
    if (u_isUWhiteSpace(
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
            || u_isalnum(static_cast<UChar32>(scalar))
            || mark
        ? WordClass::Keyword
        : WordClass::Punctuation;
}

[[nodiscard]] std::size_t VkCore::Implementation::firstNonBlankColumn(
    const Buffer &buffer,
    const std::size_t line,
    const bool allowEnd) const noexcept
{
    const std::size_t length = lineLength(buffer, line);
    const std::size_t start = buffer.lineStarts[line];
    std::size_t column = 0;
    while (column < length) {
        const char16_t value =
            buffer.text()[start + column];
        if (value != u' ' && value != u'\t') {
            break;
        }
        column = nextColumnAllowEnd(
            buffer, line, column);
    }
    // On an all-blank line Neovim leaves the Normal cursor on the last
    // blank rather than creating a virtual end-of-line position.
    return safeColumn(buffer, line, column, allowEnd);
}

[[nodiscard]] std::size_t VkCore::Implementation::lastNonBlankColumn(
    const Buffer &buffer,
    const std::size_t line) const noexcept
{
    const std::size_t start = buffer.lineStarts[line];
    std::size_t column = lineLength(buffer, line);
    while (column > 0) {
        const std::size_t previous =
            previousColumnAllowEnd(
                buffer, line, column);
        const char16_t value =
            buffer.text()[start + previous];
        if (value != u' ' && value != u'\t') {
            return previous;
        }
        column = previous;
    }
    return 0;
}

[[nodiscard]] Cursor VkCore::Implementation::lastBufferCursor(
    const Buffer &buffer) const noexcept
{
    const std::size_t line =
        buffer.lineStarts.size() - 1;
    return Cursor{
        line, normalColumnLimit(buffer, line)};
}

[[nodiscard]] char32_t VkCore::Implementation::scalarAt(
    const Buffer &buffer,
    const std::size_t line,
    const std::size_t column) const noexcept
{
    const std::size_t absolute =
        buffer.lineStarts[line] + column;
    const char16_t first =
        buffer.text()[absolute];
    if (QChar::isHighSurrogate(first)
        && absolute + 1
            < buffer.text().size()
        && QChar::isLowSurrogate(
            buffer.text()[absolute + 1])) {
        return QChar::surrogateToUcs4(
            first,
            buffer.text()[absolute + 1]);
    }
    return static_cast<char32_t>(first);
}

} // namespace vkui::vk

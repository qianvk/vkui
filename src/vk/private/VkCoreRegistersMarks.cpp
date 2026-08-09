#include "VkCoreInternal.h"

namespace vkui::vk {

[[nodiscard]] RegisterValue VkCore::Implementation::registerValue(
    const char32_t name) const
{
    if (name == U'_') {
        return {};
    }
    if ((name >= U'a' && name <= U'z')
        || (name >= U'A' && name <= U'Z')) {
        const char32_t normalized =
            name >= U'A' && name <= U'Z'
            ? name - U'A' + U'a'
            : name;
        return namedRegisters[
            static_cast<std::size_t>(
                normalized - U'a')];
    }
    if (name == U'-') {
        return smallDeleteRegister;
    }
    if (name == U'_') {
        // The black-hole register is write-only. Reading it for put,
        // CTRL-R insertion, or macro execution produces no payload.
        return {};
    }
    if (name >= U'0' && name <= U'9') {
        return numberedRegisters[
            static_cast<std::size_t>(name - U'0')];
    }
    return unnamedRegister;
}

void VkCore::Implementation::writeExplicitRegister(
    const char32_t name,
    const RegisterValue &value)
{
    if (name >= U'a' && name <= U'z') {
        namedRegisters[
            static_cast<std::size_t>(name - U'a')]
            = value;
        return;
    }
    if (name >= U'A' && name <= U'Z') {
        RegisterValue &destination =
            namedRegisters[
                static_cast<std::size_t>(
                    name - U'A')];
        if (destination.macroKeys.empty()
            && !destination.text.empty()) {
            // The destination may predate lossless macro recording (for
            // example it may have been supplied by a yank). Rebuild only
            // when there is no exact key sequence to preserve.
            destination.macroKeys = macroKeysFromText(
                destination.text);
        }
        detail::KeySequence appended = value.macroKeys;
        if (appended.empty() && !value.text.empty()) {
            appended = macroKeysFromText(value.text);
        }

        const bool destinationHasValue =
            !destination.text.empty()
            || !destination.macroKeys.empty();
        if (!destinationHasValue) {
            destination = value;
            if (destination.macroKeys.empty()
                && !destination.text.empty()) {
                destination.macroKeys =
                    std::move(appended);
            }
            return;
        }

        const bool destinationWasLinewise =
            destination.linewise;
        const bool destinationWasBlockwise =
            destination.blockwise;
        const bool becomesLinewise =
            destination.linewise || value.linewise;
        if (!destinationWasLinewise && value.linewise
            && (destination.text.empty()
                || destination.text.back() != u'\n')) {
            destination.text.push_back(u'\n');
            destination.macroKeys.push_back(
                detail::specialKey(SpecialKey::Enter));
        }
        if (!becomesLinewise
            && destinationWasBlockwise
            && !destination.text.empty()
            && !value.text.empty()
            && destination.text.back() != u'\n') {
            // Appending to an existing block adds rows. Appending a block
            // to an existing character register remains characterwise
            // and therefore concatenates directly.
            destination.text.push_back(u'\n');
            destination.macroKeys.push_back(
                detail::specialKey(SpecialKey::Enter));
        }
        destination.text += value.text;
        destination.macroKeys.insert(
            destination.macroKeys.end(),
            appended.begin(), appended.end());
        if (destinationWasLinewise && !value.linewise
            && (destination.text.empty()
                || destination.text.back() != u'\n')) {
            destination.text.push_back(u'\n');
            destination.macroKeys.push_back(
                detail::specialKey(SpecialKey::Enter));
        }
        destination.linewise = becomesLinewise;
        destination.blockwise = !becomesLinewise
            && destinationWasBlockwise;
        destination.blockDisplayWidth =
            destination.blockwise
            ? std::max(
                  destination.blockDisplayWidth,
                  value.blockwise
                      ? value.blockDisplayWidth
                      : std::size_t{0})
            : 0;
        return;
    }
    if (name >= U'0' && name <= U'9') {
        numberedRegisters[
            static_cast<std::size_t>(name - U'0')]
            = value;
        return;
    }
    if (name == U'-') {
        smallDeleteRegister = value;
    }
}

void VkCore::Implementation::writeYankRegister(
    std::u16string text,
    const bool linewise,
    const bool blockwise,
    const std::size_t blockDisplayWidth)
{
    RegisterValue value{
        std::move(text),
        linewise,
        blockwise,
        blockwise ? blockDisplayWidth : 0,
        {}};
    value.macroKeys = macroKeysFromText(value.text);
    const char32_t target = selectedRegister;
    selectedRegister = U'"';
    if (target == U'_') {
        return;
    }
    if (target != U'"') {
        writeExplicitRegister(target, value);
    }
    unnamedRegister = value;
    if (target == U'"') {
        numberedRegisters[0] = std::move(value);
    }
}

void VkCore::Implementation::writeDeleteRegister(
    std::u16string text,
    const bool linewise,
    const bool blockwise,
    const std::size_t blockDisplayWidth)
{
    RegisterValue value{
        std::move(text),
        linewise,
        blockwise,
        blockwise ? blockDisplayWidth : 0,
        {}};
    value.macroKeys = macroKeysFromText(value.text);
    const char32_t target = selectedRegister;
    selectedRegister = U'"';
    if (target == U'_') {
        return;
    }
    if (target != U'"') {
        writeExplicitRegister(target, value);
    }
    unnamedRegister = value;
    const bool small =
        !linewise
        && value.text.find(u'\n')
            == std::u16string::npos;
    if (small) {
        // Explicit named-register small deletes update the named and
        // unnamed registers, but not Vim's small-delete register.
        if (target == U'"') {
            smallDeleteRegister = std::move(value);
        }
    } else {
        // Linewise and multi-line deletes always rotate 1..9, including
        // when an explicit named register was selected.
        for (std::size_t index = 9;
             index > 1;
             --index) {
            numberedRegisters[index] =
                std::move(
                    numberedRegisters[index - 1]);
        }
        numberedRegisters[1] = std::move(value);
    }
}

void VkCore::Implementation::writeOperationRegister(
    const OperatorKind operation,
    std::u16string text,
    const bool linewise,
    const bool blockwise,
    const std::size_t blockDisplayWidth)
{
    if (operation == OperatorKind::Yank) {
        writeYankRegister(
            std::move(text),
            linewise,
            blockwise,
            blockDisplayWidth);
        return;
    }
    writeDeleteRegister(
        std::move(text),
        linewise,
        blockwise,
        blockDisplayWidth);
}

[[nodiscard]] std::optional<VkCore::Implementation::Location> VkCore::Implementation::markLocation(
    const View &view,
    const char32_t name) const
{
    const BufferId currentBuffer = view.buffer;
    if (name >= U'a' && name <= U'z') {
        const auto found = buffers.find(currentBuffer);
        if (found == buffers.end()) {
            return std::nullopt;
        }
        const auto &mark = found->second.localMarks[
            static_cast<std::size_t>(name - U'a')];
        return mark
            ? std::optional<Location>(
                  Location{currentBuffer, *mark})
            : std::nullopt;
    }
    if (name >= U'A' && name <= U'Z') {
        return globalMarks[
            static_cast<std::size_t>(name - U'A')];
    }
    if (name == U'\'' || name == U'`') {
        return view.previousContext;
    }
    const auto found = buffers.find(currentBuffer);
    if (found == buffers.end()) {
        return std::nullopt;
    }
    const auto local = [currentBuffer](
                           const auto &mark)
        -> std::optional<Location> {
        return mark
            ? std::optional<Location>(
                  Location{currentBuffer, *mark})
            : std::nullopt;
    };
    switch (name) {
    case U'.':
        return local(found->second.lastChangeMark);
    case U'^':
        return local(found->second.lastInsertExitMark);
    case U'[':
        return local(
            found->second.lastOperationStartMark);
    case U']':
        return local(
            found->second.lastOperationEndMark);
    case U'<':
        return local(found->second.lastVisualStartMark);
    case U'>':
        return local(found->second.lastVisualEndMark);
    case U'\"':
        return local(found->second.lastCursorMark);
    default:
        break;
    }
    return std::nullopt;
}

void VkCore::Implementation::setMark(
    DispatchResult &result,
    const WindowId windowId,
    const char32_t name)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    const BufferId bufferId =
        foundView->second.buffer;
    const auto foundBuffer = buffers.find(bufferId);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Cursor cursor = clampCursor(
        foundBuffer->second,
        foundView->second.cursors[bufferId],
        false);
    const Location location{
        bufferId,
        offset(foundBuffer->second, cursor)};
    if (name >= U'a' && name <= U'z') {
        foundBuffer->second.localMarks[
            static_cast<std::size_t>(name - U'a')]
            = location.offset;
        return;
    }
    if (name >= U'A' && name <= U'Z') {
        globalMarks[
            static_cast<std::size_t>(name - U'A')]
            = location;
        return;
    }
    Event error;
    error.type = EventType::InputError;
    error.view = windowId;
    error.buffer = bufferId;
    error.message = "invalid mark name";
    result.events.push_back(std::move(error));
}

[[nodiscard]] bool VkCore::Implementation::sameJumpLine(
    const Location first,
    const Location second) const
{
    if (first.buffer != second.buffer) {
        return false;
    }
    const auto found = buffers.find(first.buffer);
    if (found == buffers.end()) {
        return first.offset == second.offset;
    }
    return cursorAtOffset(
               found->second, first.offset)
               .line
        == cursorAtOffset(
               found->second, second.offset)
               .line;
}

void VkCore::Implementation::appendUniqueJumpLine(
    std::vector<Location> &jumpList,
    const Location location) const
{
    // Neovim keeps at most one jump for a buffer line. A newer column on
    // that line replaces the old entry and moves it to the tail.
    std::erase_if(
        jumpList,
        [this, location](const Location candidate) {
            return sameJumpLine(candidate, location);
        });
    jumpList.push_back(location);
    constexpr std::size_t MaximumJumpEntries = 100;
    if (jumpList.size() > MaximumJumpEntries) {
        const std::size_t excess =
            jumpList.size() - MaximumJumpEntries;
        jumpList.erase(
            jumpList.begin(),
            jumpList.begin()
                + static_cast<std::ptrdiff_t>(excess));
    }
}

void VkCore::Implementation::recordJump(
    View &view,
    const Location origin,
    const Location destination)
{
    Q_UNUSED(destination);
    view.previousContext = origin;
    // Neovim's jumplist stores positions departed from. The current live
    // cursor is represented by an index one past the tail until CTRL-O
    // first needs a forward-return entry; storing every destination here
    // makes CTRL-I return to a stale jump target after an ordinary local
    // motion.
    // The default 'jumpoptions' does not contain "stack". Therefore a
    // new jump made while browsing older entries preserves the forward
    // branch; it removes the same buffer+line entry and appends the
    // current origin at the tail instead of truncating newer entries.
    appendUniqueJumpLine(view.jumpList, origin);
    // One-past-end means the window is at its live cursor, matching
    // getjumplist()'s tail state in Neovim.
    view.jumpIndex = view.jumpList.size();
}

[[nodiscard]] bool VkCore::Implementation::moveToLocation(
    DispatchResult &result,
    const WindowId windowId,
    const Location destination,
    const bool linewise,
    const bool record,
    const std::optional<std::size_t>
        jumpIndexAfterCommit)
{
    const auto foundView = views.find(windowId);
    const auto foundBuffer = buffers.find(
        destination.buffer);
    if (foundView == views.end()
        || foundView->second.buffer == 0
        || foundBuffer == buffers.end()) {
        return false;
    }
    View &view = foundView->second;
    const auto currentBuffer = buffers.find(view.buffer);
    if (currentBuffer == buffers.end()) {
        return false;
    }
    const Location origin{
        view.buffer,
        offset(
            currentBuffer->second,
            view.cursors[view.buffer])};
    Cursor target = cursorAtOffset(
        foundBuffer->second,
        destination.offset);
    if (linewise) {
        target.column = firstNonBlankColumn(
            foundBuffer->second, target.line);
    }
    const Location effectiveDestination{
        destination.buffer,
        offset(foundBuffer->second, target)};
    if (destination.buffer != view.buffer) {
        pendingLocationMove = PendingLocationMove{
            windowId,
            origin,
            effectiveDestination,
            record,
            jumpIndexAfterCommit,
            std::nullopt};
        Event requested;
        requested.type =
            EventType::BufferActivationRequested;
        requested.view = windowId;
        requested.buffer = destination.buffer;
        requested.cursor = target;
        requested.hasCursor = true;
        result.events.push_back(std::move(requested));
        return true;
    }
    if (record) {
        recordJump(
            view, origin, effectiveDestination);
    }
    if (jumpIndexAfterCommit) {
        view.jumpIndex = *jumpIndexAfterCommit;
    }
    view.cursors[view.buffer] = target;
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
    return true;
}

void VkCore::Implementation::jumpToMark(
    DispatchResult &result,
    const WindowId windowId,
    const char32_t name,
    const bool linewise)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()) {
        return;
    }
    const auto destination = markLocation(
        foundView->second, name);
    if (destination
        && moveToLocation(
            result,
            windowId,
            *destination,
            linewise,
            true)) {
        return;
    }
    Event error;
    error.type = EventType::InputError;
    error.view = windowId;
    error.buffer = foundView->second.buffer;
    error.message = "mark is not set";
    result.events.push_back(std::move(error));
}

void VkCore::Implementation::moveInJumpList(
    DispatchResult &result,
    const WindowId windowId,
    const bool forward,
    const std::size_t count)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto currentBuffer = buffers.find(view.buffer);
    if (currentBuffer == buffers.end()) {
        return;
    }
    const std::vector<Location> originalList = view.jumpList;
    const std::size_t originalIndex = view.jumpIndex;
    std::vector<Location> candidateList = view.jumpList;
    std::size_t candidateIndex = std::min(
        view.jumpIndex, candidateList.size());

    if (!forward
        && candidateIndex >= candidateList.size()) {
        const Location current{
            view.buffer,
            offset(
                currentBuffer->second,
                view.cursors[view.buffer])};
        appendUniqueJumpLine(candidateList, current);
        candidateIndex = candidateList.empty()
            ? 0
            : candidateList.size() - 1;
    }

    const std::size_t available = forward
        ? candidateIndex < candidateList.size()
            ? candidateList.size() - candidateIndex - 1
            : 0
        : candidateIndex;
    if (candidateList.empty() || count > available) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = forward
            ? "jumplist has fewer newer entries than requested"
            : "jumplist has fewer older entries than requested";
        result.events.push_back(std::move(error));
        return;
    }
    const std::size_t targetIndex = forward
        ? candidateIndex + count
        : candidateIndex - count;
    const Location target = candidateList[targetIndex];
    view.jumpList = candidateList;
    const bool crossBuffer =
        target.buffer != view.buffer;
    const bool moving = moveToLocation(
        result,
        windowId,
        target,
        false,
        false,
        targetIndex);
    if (!moving) {
        view.jumpList = originalList;
        view.jumpIndex = originalIndex;
        return;
    }
    if (crossBuffer && pendingLocationMove) {
        pendingLocationMove->jumpListAfterCommit =
            candidateList;
        view.jumpList = originalList;
        view.jumpIndex = originalIndex;
    }
}

void VkCore::Implementation::moveInChangeList(
    DispatchResult &result,
    const WindowId windowId,
    const bool forward,
    const std::size_t count)
{
    const auto foundView = views.find(windowId);
    if (foundView == views.end()
        || foundView->second.kind != ViewKind::Editor
        || foundView->second.buffer == 0) {
        return;
    }
    View &view = foundView->second;
    const auto foundBuffer = buffers.find(view.buffer);
    if (foundBuffer == buffers.end()) {
        return;
    }
    const Buffer &buffer = foundBuffer->second;
    const std::size_t length = buffer.changeList.size();
    if (length == 0) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = "change list is empty";
        result.events.push_back(std::move(error));
        return;
    }

    // This is the clamp-and-boundary behavior of Neovim's
    // get_changelist(). The live position is one past the tail; both g;
    // and g, first select the newest entry from that position.
    const std::size_t current = std::min(
        view.changeIndex, length);
    std::size_t target = current;
    bool atBoundary = false;
    if (forward) {
        if (current >= length) {
            target = length - 1;
        } else if (count > length - 1 - current) {
            atBoundary = current == length - 1;
            target = length - 1;
        } else {
            target = current + count;
        }
    } else if (count > current) {
        atBoundary = current == 0;
        target = 0;
    } else {
        target = current - count;
    }
    if (atBoundary) {
        Event error;
        error.type = EventType::InputError;
        error.view = windowId;
        error.buffer = view.buffer;
        error.message = forward
            ? "at end of change list"
            : "at start of change list";
        result.events.push_back(std::move(error));
        return;
    }

    view.changeIndex = target;
    view.cursors[view.buffer] = cursorAtOffset(
        buffer, buffer.changeList[target]);
    view.displayColumns.erase(view.buffer);
    view.preferredColumn.reset();
    emitCursor(result, windowId, view);
}

} // namespace vkui::vk

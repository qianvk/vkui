#include <vkui/vk/VkCore.h>

#include <QKeyEvent>
#include <QTest>

#include <algorithm>
#include <array>
#include <string_view>

using namespace vkui::vk;

namespace {

[[nodiscard]] Qt::KeyboardModifier controlModifier()
{
#ifdef Q_OS_MACOS
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

[[nodiscard]] DispatchResult press(
    VkCore &core,
    const WindowId window,
    const int key,
    const Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    const QString &text = {})
{
    const QKeyEvent event{
        QEvent::KeyPress,
        key,
        modifiers,
        text};
    return core.dispatch(window, window, event);
}

[[nodiscard]] DispatchResult pressCharacter(
    VkCore &core,
    const WindowId window,
    const QChar character)
{
    int key = 0;
    if (character.isLetter()) {
        key = Qt::Key_A
            + character.toUpper().unicode()
            - QChar(u'A').unicode();
    } else if (character.isDigit()) {
        key = Qt::Key_0
            + character.unicode()
            - QChar(u'0').unicode();
    } else if (character == QChar(u' ')) {
        key = Qt::Key_Space;
    } else if (character == QChar(u'.')) {
        key = Qt::Key_Period;
    } else if (character == QChar(u'"')) {
        key = Qt::Key_QuoteDbl;
    } else if (character == QChar(u'-')) {
        key = Qt::Key_Minus;
    } else if (character == QChar(u'~')) {
        key = Qt::Key_AsciiTilde;
    } else if (character == QChar(u'\'')) {
        key = Qt::Key_Apostrophe;
    } else if (character == QChar(u'`')) {
        key = Qt::Key_QuoteLeft;
    } else if (character == QChar(u'/')) {
        key = Qt::Key_Slash;
    } else if (character == QChar(u'?')) {
        key = Qt::Key_Question;
    } else if (character == QChar(u'@')) {
        key = Qt::Key_At;
    } else if (character == QChar(u'*')) {
        key = Qt::Key_Asterisk;
    } else if (character == QChar(u'#')) {
        key = Qt::Key_NumberSign;
    } else if (character == QChar(u'^')) {
        key = Qt::Key_AsciiCircum;
    } else if (character == QChar(u'(')) {
        key = Qt::Key_ParenLeft;
    } else if (character == QChar(u')')) {
        key = Qt::Key_ParenRight;
    } else if (character == QChar(u'[')) {
        key = Qt::Key_BracketLeft;
    } else if (character == QChar(u']')) {
        key = Qt::Key_BracketRight;
    } else if (character == QChar(u'{')) {
        key = Qt::Key_BraceLeft;
    } else if (character == QChar(u'}')) {
        key = Qt::Key_BraceRight;
    } else if (character == QChar(u'<')) {
        key = Qt::Key_Less;
    } else if (character == QChar(u'>')) {
        key = Qt::Key_Greater;
    }
    return press(
        core,
        window,
        key,
        character.isUpper()
            ? Qt::ShiftModifier
            : Qt::NoModifier,
        QString(character));
}

void command(
    VkCore &core,
    const WindowId window,
    const QString &keys)
{
    for (const QChar key : keys) {
        (void)pressCharacter(core, window, key);
    }
}

[[nodiscard]] std::size_t textOffset(
    const std::u16string &text,
    const Cursor cursor)
{
    std::size_t offset = 0;
    for (std::size_t line = 0;
         line < cursor.line;
         ++line) {
        const auto newline =
            text.find(u'\n', offset);
        if (newline == std::u16string::npos) {
            return text.size();
        }
        offset = newline + 1;
    }
    return std::min(
        text.size(), offset + cursor.column);
}

void insertText(
    VkCore &core,
    const WindowId window,
    const std::u16string &text)
{
    const auto buffer = core.activeBuffer(window);
    const auto state = core.window(window);
    QVERIFY(buffer.has_value());
    QVERIFY(state.has_value());
    const std::size_t offset =
        textOffset(buffer->text, state->cursor);
    Cursor cursor = state->cursor;
    for (const char16_t value : text) {
        if (value == u'\n') {
            ++cursor.line;
            cursor.column = 0;
        } else {
            ++cursor.column;
        }
    }
    QVERIFY(core.applyExternalEdit(
        window,
        offset,
        0,
        text,
        cursor));
}

void escape(VkCore &core, const WindowId window)
{
    (void)press(core, window, Qt::Key_Escape);
}

[[nodiscard]] std::u16string bufferText(
    const VkCore &core,
    const BufferId buffer)
{
    const auto snapshot = core.buffer(buffer);
    return snapshot
        ? snapshot->text
        : std::u16string{};
}

} // namespace

class VkEditingBehaviorTests final : public QObject
{
    Q_OBJECT

private slots:
    void characterChangesAreUnicodeSafeAndRepeatable();
    void putUsesCharacterLineAndNumberedRegisters();
    void insertionLineAndJoinCommandsShareUndoTransactions();
    void visualSelectionsAreRealInclusiveRanges();
    void viewportCommandsRemainWindowLocal();
    void replacementAtEndOfLineIsAtomic();
    void insertRepeatCountsAndJoinRulesMatchNeovim();
    void visualStateIsWindowAndBufferLocal();
    void visualSnapshotIsWindowAndBufferLocal();
    void bigWordAndLineMotionsMatchNeovim();
    void newMotionsShareVisualAndOperatorPaths();
    void sectionMotionsCountsOperatorsAndHintsMatchNeovim();
    void visualBlockTextObjectsAndNamedRegisters();
    void visualBlockUsesDisplayCellsAndVirtualColumns();
    void visualBlockModeSpecificCommandsMatchNeovim();
    void registersAndDotRepeatMatchNeovim();
    void textObjectsAndSearchWhitespaceMatchNeovim();
    void textObjectStateMachinesMatchNeovim();
    void marksSearchJumpListAndMacros();
    void changeListAndLastInsertPositionMatchNeovim();
    void alternateBufferIsWindowLocalAndTransactional();
    void macroFailureDispositionMatchesNeovim();
    void jumpListTracksEditsAndJumpMotions();
    void jumpListReturnsToTheLiveCursorLikeNeovim();
    void jumpListCountsTabDedupAndBranchingMatchNeovim();
    void splitWindowsCloneButThenOwnTheirJumpLists();
    void countedSearchInsertAndTextObjectsMatchNeovim();
    void wordUnderCursorSearchMatchesNeovim();
    void specialMarksAndCrossBufferJumpsAreTransactional();
    void macroRegistersAndVisualDotShareAuthoritativeState();
    void exRangesReachUserCommandsWithoutLosingContext();
    void writeQuitIsOneSemanticTransaction();
    void transientPromptKeepsVkAtTheInputBoundary();
    void numberAddSubtractUsesNativeUndoAndDotRecipes();
    void insertKeywordCompletionCyclesAuthoritativeBuffer();
    void controlGPublishesStructuredBufferStatus();
    void backwardEndsSentencesParagraphsAndTransformOperators();
    void insertControlWordLineAndRegisterCommandsAreCoreOwned();
    void replaceModeRestoresUndoSegmentsAndDotRecipes();
    void insertControlOConsumesOneCompleteNormalCommand();
    void visualShiftAndCaseTransformsPreserveSelectionGeometry();
};

void VkEditingBehaviorTests::
    characterChangesAreUnicodeSafeAndRepeatable()
{
    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/unicode.txt",
            u"A😀BC",
            {0, 1});

        command(core, window, QStringLiteral("x"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"ABC"));
        command(core, window, QStringLiteral("u"));
        QVERIFY(core.setViewCursor(window, {0, 3}));
        command(core, window, QStringLiteral("X"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"ABC"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/repeat.txt",
            u"abcdef",
            {0, 0});
        command(core, window, QStringLiteral("2x"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"cdef"));
        command(core, window, QStringLiteral("l."));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"cf"));

        QVERIFY(core.replaceBufferText(
            buffer, u"abcd", {0, 0}));
        command(core, window, QStringLiteral("2rZ"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"ZZcd"));
        command(core, window, QStringLiteral("l."));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"ZZZZ"));

        QVERIFY(core.replaceBufferText(
            buffer, u"aB😀", {0, 0}));
        command(core, window, QStringLiteral("2~"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"Ab😀"));
    }
}

void VkEditingBehaviorTests::
    putUsesCharacterLineAndNumberedRegisters()
{
    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/register-lines.txt",
            u"one\ntwo\nthree",
            {0, 0});
        command(core, window, QStringLiteral("yyjdd"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\nthree"));

        command(core, window, QStringLiteral("\"0P"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\none\nthree"));
        command(core, window, QStringLiteral("u\"1P"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\ntwo\nthree"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/register-small.txt",
            u"abc",
            {0, 0});
        command(core, window, QStringLiteral("x"));
        QVERIFY(core.setViewCursor(window, {0, 1}));
        command(core, window, QStringLiteral("\"-P"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"bac"));

        command(core, window, QStringLiteral("u"));
        QVERIFY(core.setViewCursor(window, {0, 0}));
        command(core, window, QStringLiteral("p"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"bac"));

        command(core, window, QStringLiteral("\"_p"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"bac"));
    }
}

void VkEditingBehaviorTests::
    insertionLineAndJoinCommandsShareUndoTransactions()
{
    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/open.txt",
            u"one\ntwo",
            {0, 0});
        command(core, window, QStringLiteral("o"));
        QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
        insertText(core, window, u"new");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\nnew\ntwo"));

        QVERIFY(core.setViewCursor(window, {2, 0}));
        command(core, window, QStringLiteral("."));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\nnew\ntwo\nnew"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\nnew\ntwo"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/substitute.txt",
            u"abcd",
            {0, 1});
        command(core, window, QStringLiteral("2s"));
        insertText(core, window, u"XY");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"aXYd"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcd"));

        QVERIFY(core.replaceBufferText(
            buffer,
            u"one\ntwo\nthree",
            {0, 0}));
        command(core, window, QStringLiteral("2S"));
        insertText(core, window, u"new");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"new\nthree"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/line-changes.txt",
            u"abc\ndef",
            {0, 1});
        command(core, window, QStringLiteral("D"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"a\ndef"));
        command(core, window, QStringLiteral("uC"));
        insertText(core, window, u"X");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"aX\ndef"));

        QVERIFY(core.replaceBufferText(
            buffer,
            u"one\n   two\nthree",
            {0, 0}));
        command(core, window, QStringLiteral("J"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one two\nthree"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\n   two\nthree"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/open-above.txt",
            u"one\ntwo",
            {1, 0});
        command(core, window, QStringLiteral("O"));
        insertText(core, window, u"above");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\nabove\ntwo"));
    }
}

void VkEditingBehaviorTests::
    visualSelectionsAreRealInclusiveRanges()
{
    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/visual-char.txt",
            u"abcde",
            {0, 0});
        command(core, window, QStringLiteral("v2ld"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"de"));
        QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
        command(core, window, QStringLiteral("u"));

        QVERIFY(core.setViewCursor(window, {0, 1}));
        command(core, window, QStringLiteral("vlc"));
        QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
        insertText(core, window, u"XY");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"aXYde"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/visual-line.txt",
            u"one\ntwo\nthree",
            {0, 0});
        command(core, window, QStringLiteral("Vjd"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"three"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\ntwo\nthree"));
        QVERIFY(core.setViewCursor(window, {0, 0}));
        command(core, window, QStringLiteral("Vjy"));
        QVERIFY(core.setViewCursor(window, {2, 0}));
        command(core, window, QStringLiteral("P"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"one\ntwo\none\ntwo\nthree"));
    }
}

void VkEditingBehaviorTests::
    viewportCommandsRemainWindowLocal()
{
    VkCore core;
    const WindowId first =
        core.registerView(ViewKind::Editor);
    const WindowId second =
        core.registerView(ViewKind::Editor);
    std::u16string text;
    for (int line = 0; line < 30; ++line) {
        if (!text.empty()) {
            text.push_back(u'\n');
        }
        text += u"x";
    }
    const BufferId buffer = core.synchronizeBuffer(
        first,
        "/vault/viewport.txt",
        text,
        {0, 0});
    QCOMPARE(
        core.synchronizeBuffer(
            second,
            "/vault/viewport.txt",
            text,
            {20, 0}),
        buffer);
    QVERIFY(core.setWindowViewport(
        first, 0, 0, 6));
    QVERIFY(core.setWindowViewport(
        second, 18, 0, 6));

    (void)press(
        core,
        first,
        Qt::Key_F,
        controlModifier(),
        QStringLiteral("f"));
    QCOMPARE(core.window(first)->topline, std::size_t{6});
    QCOMPARE(core.window(first)->cursor, (Cursor{6, 0}));
    QCOMPARE(core.window(second)->topline, std::size_t{18});

    (void)press(
        core,
        first,
        Qt::Key_B,
        controlModifier(),
        QStringLiteral("b"));
    QCOMPARE(core.window(first)->topline, std::size_t{0});
    QCOMPARE(core.window(first)->cursor, (Cursor{0, 0}));

    QVERIFY(core.setWindowViewport(
        first, 2, 0, 6));
    QVERIFY(core.setViewCursor(first, {4, 0}));
    command(core, first, QStringLiteral("2"));
    (void)press(
        core, first, Qt::Key_PageDown);
    QCOMPARE(core.window(first)->topline, std::size_t{14});
    QCOMPARE(core.window(first)->cursor, (Cursor{16, 0}));
    QCOMPARE(core.window(second)->topline, std::size_t{18});

    command(core, first, QStringLiteral("2"));
    (void)press(
        core, first, Qt::Key_PageUp);
    QCOMPARE(core.window(first)->topline, std::size_t{2});
    QCOMPARE(core.window(first)->cursor, (Cursor{4, 0}));
    QVERIFY(core.setWindowViewport(
        first, 0, 0, 6));
    QVERIFY(core.setViewCursor(first, {0, 0}));

    command(core, first, QStringLiteral("2"));
    (void)press(
        core,
        first,
        Qt::Key_E,
        controlModifier(),
        QStringLiteral("e"));
    QCOMPARE(core.window(first)->topline, std::size_t{2});
    QCOMPARE(core.window(first)->cursor, (Cursor{2, 0}));
    (void)press(
        core,
        first,
        Qt::Key_Y,
        controlModifier(),
        QStringLiteral("y"));
    QCOMPARE(core.window(first)->topline, std::size_t{1});

    QVERIFY(core.setViewCursor(first, {10, 0}));
    command(core, first, QStringLiteral("zt"));
    QCOMPARE(core.window(first)->topline, std::size_t{10});
    command(core, first, QStringLiteral("zz"));
    QCOMPARE(core.window(first)->topline, std::size_t{7});
    command(core, first, QStringLiteral("zb"));
    QCOMPARE(core.window(first)->topline, std::size_t{5});
    QCOMPARE(core.window(second)->topline, std::size_t{18});
}

void VkEditingBehaviorTests::
    replacementAtEndOfLineIsAtomic()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/replace-eol.txt",
        u"abc",
        {0, 1});

    command(core, window, QStringLiteral("3r"));
    const DispatchResult rejected =
        pressCharacter(core, window, QChar(u'X'));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abc"));
    QVERIFY(std::ranges::any_of(
        rejected.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));

    command(core, window, QStringLiteral("2rX"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"aXX"));

    QVERIFY(core.replaceBufferText(
        buffer, u"A😀B", {0, 1}));
    command(core, window, QStringLiteral("3rZ"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"A😀B"));
    command(core, window, QStringLiteral("2rZ"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"AZZ"));

    QVERIFY(core.replaceBufferText(
        buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("2r"));
    (void)press(core, window, Qt::Key_Return);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"\nc"));
}

void VkEditingBehaviorTests::
    insertRepeatCountsAndJoinRulesMatchNeovim()
{
    const auto verifyInsertRepeat =
        [](const QString &insertCommand,
           const std::u16string &initial,
           const Cursor cursor,
           const Cursor repeatCursor,
           const std::u16string &expected) {
            VkCore core;
            const WindowId window =
                core.registerView(ViewKind::Editor);
            const BufferId buffer =
                core.synchronizeBuffer(
                    window,
                    "/vault/insert-repeat.txt",
                    initial,
                    cursor);
            command(core, window, insertCommand);
            insertText(core, window, u"X");
            escape(core, window);
            QVERIFY(core.setViewCursor(
                window, repeatCursor));
            command(core, window, QStringLiteral("2."));
            QCOMPARE(
                bufferText(core, buffer),
                expected);
        };

    verifyInsertRepeat(
        QStringLiteral("i"),
        u"abc",
        {0, 1},
        {0, 2},
        u"aXXXbc");
    verifyInsertRepeat(
        QStringLiteral("a"),
        u"abc",
        {0, 0},
        {0, 2},
        u"aXbXXc");
    verifyInsertRepeat(
        QStringLiteral("I"),
        u"  abc",
        {0, 4},
        {0, 3},
        u"  XXXabc");
    verifyInsertRepeat(
        QStringLiteral("A"),
        u"abc",
        {0, 0},
        {0, 3},
        u"abcXXX");

    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/join.txt",
        u"Hello.\nworld",
        {0, 0});

    QVERIFY(core.replaceBufferText(
        buffer, u"a", {0, 0}));
    command(core, window, QStringLiteral("3o"));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"a\nX\nX\nX"));
    QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"a\nX\nX\nX\nX\nX\nX"));
    QCOMPARE(core.window(window)->cursor, (Cursor{6, 0}));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"a\nX\nX\nX"));

    QVERIFY(core.replaceBufferText(
        buffer, u"a", {0, 0}));
    command(core, window, QStringLiteral("3O"));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X\nX\nX\na"));
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 0}));

    QVERIFY(core.replaceBufferText(
        buffer, u"Hello.\nworld", {0, 0}));
    command(core, window, QStringLiteral("J"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"Hello. world"));

    QVERIFY(core.replaceBufferText(
        buffer, u"Hello \n world", {0, 0}));
    command(core, window, QStringLiteral("J"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"Hello world"));

    QVERIFY(core.replaceBufferText(
        buffer, u"call(\n  )", {0, 0}));
    command(core, window, QStringLiteral("J"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"call()"));

    QVERIFY(core.replaceBufferText(
        buffer, u"a\t\n b", {0, 0}));
    command(core, window, QStringLiteral("J"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"a\tb"));

    QVERIFY(core.replaceBufferText(
        buffer, u"a\n\nb", {0, 0}));
    command(core, window, QStringLiteral("3J"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"a b"));
}

void VkEditingBehaviorTests::
    visualStateIsWindowAndBufferLocal()
{
    {
        VkCore core;
        const WindowId first =
            core.registerView(ViewKind::Editor);
        const WindowId second =
            core.registerView(ViewKind::Editor);
        const BufferId firstBuffer =
            core.synchronizeBuffer(
                first,
                "/vault/first.txt",
                u"first",
                {0, 0});
        const BufferId secondBuffer =
            core.synchronizeBuffer(
                second,
                "/vault/second.txt",
                u"second",
                {0, 0});

        command(core, first, QStringLiteral("vl"));
        QCOMPARE(
            core.mode(),
            std::optional<Mode>(Mode::Visual));
        command(core, second, QStringLiteral("d"));
        QCOMPARE(
            core.mode(),
            std::optional<Mode>(Mode::OperatorPending));
        // The context boundary cancels the stale Visual selection before the
        // physical key is interpreted. Therefore the same key begins a fresh
        // Normal-mode operator in the newly active window.
        escape(core, second);
        QCOMPARE(
            bufferText(core, firstBuffer),
            std::u16string(u"first"));
        QCOMPARE(
            bufferText(core, secondBuffer),
            std::u16string(u"second"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId original =
            core.synchronizeBuffer(
                window,
                "/vault/original.txt",
                u"original",
                {0, 0});
        command(core, window, QStringLiteral("vl"));
        const BufferId replacement =
            core.synchronizeBuffer(
                window,
                "/vault/replacement.txt",
                u"replacement",
                {0, 0});
        command(core, window, QStringLiteral("d"));
        QCOMPARE(
            core.mode(),
            std::optional<Mode>(Mode::Normal));
        QCOMPARE(
            bufferText(core, original),
            std::u16string(u"original"));
        QCOMPARE(
            bufferText(core, replacement),
            std::u16string(u"replacement"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerView(ViewKind::Editor);
        const BufferId buffer =
            core.synchronizeBuffer(
                window,
                "/vault/visual-line-repeat.txt",
                u"a\nb\nc\nd\ne",
                {0, 0});
        command(core, window, QStringLiteral("Vjd."));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"e"));
    }
}

void VkEditingBehaviorTests::
    visualSnapshotIsWindowAndBufferLocal()
{
    VkCore core;
    const WindowId first =
        core.registerView(ViewKind::Editor);
    const WindowId second =
        core.registerView(ViewKind::Editor);
    const WindowId sharedBufferWindow =
        core.registerView(ViewKind::Editor);
    const BufferId firstBuffer =
        core.synchronizeBuffer(
            first,
            "/vault/first-snapshot.txt",
            u"one\ntwo\nthree",
            {1, 1});
    QVERIFY(firstBuffer != 0);
    QVERIFY(core.synchronizeBuffer(
        second,
        "/vault/second-snapshot.txt",
        u"unrelated",
        {0, 0}) != 0);
    QCOMPARE(
        core.synchronizeBuffer(
            sharedBufferWindow,
            "/vault/first-snapshot.txt",
            u"one\ntwo\nthree",
            {0, 0}),
        firstBuffer);

    command(core, first, QStringLiteral("V"));
    const auto firstVisual = core.window(first);
    const auto unrelated = core.window(second);
    const auto sharedBuffer =
        core.window(sharedBufferWindow);
    QVERIFY(firstVisual.has_value());
    QVERIFY(unrelated.has_value());
    QVERIFY(sharedBuffer.has_value());
    QVERIFY(firstVisual->visualAnchor.has_value());
    QCOMPARE(firstVisual->visualAnchor->line, std::size_t{1});
    QCOMPARE(firstVisual->visualAnchor->column, std::size_t{1});
    QVERIFY(firstVisual->visualLinewise);
    QVERIFY(!unrelated->visualAnchor.has_value());
    QVERIFY(!unrelated->visualLinewise);
    QVERIFY(!sharedBuffer->visualAnchor.has_value());
    QVERIFY(!sharedBuffer->visualLinewise);

    // A window switch invalidates the old presentation snapshot rather than
    // retargeting its anchor to whichever buffer becomes active.
    QVERIFY(core.setActiveView(second));
    const auto inactive = core.window(first);
    QVERIFY(inactive.has_value());
    QVERIFY(!inactive->visualAnchor.has_value());
    QVERIFY(!inactive->visualLinewise);

    QVERIFY(core.setActiveView(first));
    command(core, first, QStringLiteral("v"));
    const auto characterwise = core.window(first);
    QVERIFY(characterwise.has_value());
    QVERIFY(characterwise->visualAnchor.has_value());
    QVERIFY(!characterwise->visualLinewise);

    // Replacing the buffer displayed by the same window must also remove the
    // stale anchor from its public projection.
    QVERIFY(core.synchronizeBuffer(
        first,
        "/vault/replacement-snapshot.txt",
        u"replacement",
        {0, 0}) != 0);
    const auto replaced = core.window(first);
    QVERIFY(replaced.has_value());
    QVERIFY(!replaced->visualAnchor.has_value());
    QVERIFY(!replaced->visualLinewise);
}

void VkEditingBehaviorTests::
    bigWordAndLineMotionsMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/motions.txt",
        u"one,two 😀three\n  four five",
        {0, 0});

    // Lowercase word motions use Vim's three classes (keyword,
    // non-keyword punctuation, and whitespace). Uppercase WORD motions only
    // distinguish whitespace. Keeping this matrix here prevents punctuation
    // from silently collapsing back into WORD behavior.
    QVERIFY(core.replaceBufferText(
        buffer,
        u"foo.bar baz",
        {0, 0}));
    command(core, window, QStringLiteral("w"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 3}));
    command(core, window, QStringLiteral("w"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 4}));
    command(core, window, QStringLiteral("w"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 8}));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("2e"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 3}));
    QVERIFY(core.setViewCursor(window, {0, 5}));
    command(core, window, QStringLiteral("2b"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 3}));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("W"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 8}));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("E"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 6}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"one,two 😀three\n  four five",
        {0, 0}));

    command(core, window, QStringLiteral("W"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 8}));
    command(core, window, QStringLiteral("E"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 14}));
    command(core, window, QStringLiteral("B"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 8}));

    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("2W"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{1, 2}));
    command(core, window, QStringLiteral("2B"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 0}));
    command(core, window, QStringLiteral("2E"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 14}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"aa bb\n\ncc",
        {0, 0}));
    command(core, window, QStringLiteral("2W"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{1, 0}));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("3W"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{2, 0}));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("3E"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{2, 1}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"  😀x  \n\tlast  \n    middle\n  fourth\n fifth",
        {0, 6}));
    command(core, window, QStringLiteral("9^"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 2}));
    command(core, window, QStringLiteral("2g_"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{1, 4}));

    QVERIFY(core.setViewCursor(window, {4, 2}));
    command(core, window, QStringLiteral("3gg"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{2, 4}));
    command(core, window, QStringLiteral("gg"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 2}));

    QVERIFY(core.setViewCursor(window, {0, 2}));
    command(core, window, QStringLiteral("2+"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{2, 4}));
    QVERIFY(core.setViewCursor(window, {4, 2}));
    command(core, window, QStringLiteral("2-"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{2, 4}));

    QVERIFY(core.setViewCursor(window, {0, 2}));
    command(core, window, QStringLiteral("2"));
    (void)press(core, window, Qt::Key_Return);
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{2, 4}));
}

void VkEditingBehaviorTests::
    newMotionsShareVisualAndOperatorPaths()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/operator-motions.txt",
        u"foo bar",
        {0, 0});

    command(core, window, QStringLiteral("dW"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"bar"));
    QVERIFY(core.replaceBufferText(
        buffer, u"foo bar", {0, 0}));
    command(core, window, QStringLiteral("dE"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u" bar"));
    QVERIFY(core.replaceBufferText(
        buffer, u"foo bar", {0, 5}));
    command(core, window, QStringLiteral("dB"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"foo ar"));

    QVERIFY(core.replaceBufferText(
        buffer, u"😀word next", {0, 0}));
    command(core, window, QStringLiteral("dE"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u" next"));

    QVERIFY(core.replaceBufferText(
        buffer, u"foo bar", {0, 0}));
    command(core, window, QStringLiteral("cW"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X bar"));

    QVERIFY(core.replaceBufferText(
        buffer, u"foo.bar baz", {0, 0}));
    command(core, window, QStringLiteral("cw"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X.bar baz"));

    QVERIFY(core.replaceBufferText(
        buffer, u"  alpha", {0, 4}));
    command(core, window, QStringLiteral("d^"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"  pha"));
    QVERIFY(core.replaceBufferText(
        buffer, u"  alpha  \n next", {0, 2}));
    command(core, window, QStringLiteral("dg_"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"    \n next"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"one\ntwo\nthree\nfour",
        {0, 0}));
    command(core, window, QStringLiteral("d2+"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"four"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"one\ntwo\nthree\nfour",
        {2, 0}));
    command(core, window, QStringLiteral("d-"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one\nfour"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"one\ntwo\nthree\nfour",
        {0, 0}));
    command(core, window, QStringLiteral("d"));
    (void)press(core, window, Qt::Key_Return);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"three\nfour"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"one\ntwo\nthree\nfour",
        {3, 0}));
    command(core, window, QStringLiteral("2dgg"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"aa bb\n  cc  \n\tdd\n  ee",
        {0, 0}));
    const auto verifyVisual =
        [&core, window](
            const Cursor start,
            const QString &motion,
            const Cursor expected) {
            QVERIFY(core.setViewCursor(window, start));
            command(
                core,
                window,
                QStringLiteral("v") + motion);
            const auto state = core.window(window);
            QVERIFY(state.has_value());
            QCOMPARE(
                core.mode(),
                std::optional<Mode>(Mode::Visual));
            QCOMPARE(state->cursor, expected);
            QVERIFY(state->visualAnchor.has_value());
            QCOMPARE(*state->visualAnchor, start);
            escape(core, window);
        };

    verifyVisual(
        {0, 0}, QStringLiteral("W"), {0, 3});
    verifyVisual(
        {0, 4}, QStringLiteral("B"), {0, 3});
    verifyVisual(
        {0, 0}, QStringLiteral("E"), {0, 1});
    verifyVisual(
        {1, 4}, QStringLiteral("9^"), {1, 2});
    verifyVisual(
        {0, 0}, QStringLiteral("2g_"), {1, 3});
    verifyVisual(
        {3, 3}, QStringLiteral("3gg"), {2, 1});
    verifyVisual(
        {0, 0}, QStringLiteral("2+"), {2, 1});
    verifyVisual(
        {3, 2}, QStringLiteral("-"), {2, 1});

    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("v2"));
    (void)press(core, window, Qt::Key_Return);
    const auto enterVisual = core.window(window);
    QVERIFY(enterVisual.has_value());
    QCOMPARE(
        core.mode(),
        std::optional<Mode>(Mode::Visual));
    QCOMPARE(enterVisual->cursor, (Cursor{2, 1}));
    QVERIFY(enterVisual->visualAnchor.has_value());
    QCOMPARE(*enterVisual->visualAnchor, (Cursor{0, 0}));
    escape(core, window);
}

void VkEditingBehaviorTests::
    sectionMotionsCountsOperatorsAndHintsMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerView(ViewKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/sections.txt",
        u"top\n.SH alpha\nbody\n}\nmiddle\n.NH beta\n{\ntail\n}",
        {2, 0});
    QVERIFY(buffer != 0);

    const auto hasNativeHint = [](
                                   const InputHintSnapshot &snapshot,
                                   const std::u32string_view key,
                                   const std::string_view description) {
        return std::ranges::any_of(
            snapshot.candidates,
            [key, description](
                const InputHintCandidate &candidate) {
                return candidate.keyNotation == key
                    && candidate.description == description
                    && candidate.origin
                        == InputHintOrigin::NativeGrammar;
            });
    };

    // The same bracket descriptors drive native hints and execution.
    auto result =
        pressCharacter(core, window, QChar(u'['));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration.has_value());
    const auto previousSectionHints = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(previousSectionHints.has_value());
    QCOMPARE(
        previousSectionHints->prefixNotation,
        std::u32string(U"["));
    QVERIFY(hasNativeHint(
        *previousSectionHints,
        U"[",
        "Previous section start"));
    QVERIFY(hasNativeHint(
        *previousSectionHints,
        U"]",
        "Previous section end"));
    result = pressCharacter(core, window, QChar(u'['));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{1, 0}));

    const auto verifyMotion =
        [&core, window](
            const Cursor start,
            const QString &keys,
            const Cursor expected) {
            QVERIFY(core.setViewCursor(window, start));
            command(core, window, keys);
            const auto state = core.window(window);
            QVERIFY(state.has_value());
            QCOMPARE(state->cursor, expected);
        };

    // Default 'sections' macros and column-zero braces are both targets.
    verifyMotion(
        {2, 0}, QStringLiteral("[["), {1, 0});
    verifyMotion(
        {2, 0}, QStringLiteral("]]"), {5, 0});
    verifyMotion(
        {2, 0}, QStringLiteral("[]"), {1, 0});
    verifyMotion(
        {2, 0}, QStringLiteral("]["), {3, 0});

    verifyMotion(
        {0, 0}, QStringLiteral("2]]"), {5, 0});
    verifyMotion(
        {8, 0}, QStringLiteral("2[["), {5, 0});
    verifyMotion(
        {0, 0}, QStringLiteral("2]["), {3, 0});
    verifyMotion(
        {8, 0}, QStringLiteral("2[]"), {3, 0});

    // Neovim clamps failed outward searches without moving past an edge.
    verifyMotion(
        {0, 0}, QStringLiteral("[["), {0, 0});
    verifyMotion(
        {0, 0}, QStringLiteral("[]"), {0, 0});
    verifyMotion(
        {8, 0}, QStringLiteral("]]"), {8, 0});
    verifyMotion(
        {8, 0}, QStringLiteral("]["), {8, 0});

    QVERIFY(core.setViewCursor(window, {2, 0}));
    command(core, window, QStringLiteral("v]]"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Visual));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{5, 0}));
    QCOMPARE(
        core.window(window)->visualAnchor,
        std::optional<Cursor>(Cursor{2, 0}));
    escape(core, window);

    // With an operator, ]] stops below a column-zero closing brace so d]]
    // consumes that brace line. The operator prefix exposes the same native
    // section descriptors in the hint snapshot.
    QVERIFY(core.replaceBufferText(
        buffer,
        u"start\nbody\n}\nafter\n{\nnext",
        {0, 0}));
    result = pressCharacter(core, window, QChar(u'd'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    result = pressCharacter(core, window, QChar(u']'));
    QCOMPARE(result.disposition, InputDisposition::Pending);
    QVERIFY(result.inputHintGeneration.has_value());
    const auto operatorSectionHints = core.pendingInputHint(
        *result.inputHintGeneration);
    QVERIFY(operatorSectionHints.has_value());
    QCOMPARE(
        operatorSectionHints->prefixNotation,
        std::u32string(U"d]"));
    QVERIFY(hasNativeHint(
        *operatorSectionHints,
        U"]",
        "Next section start"));
    QVERIFY(hasNativeHint(
        *operatorSectionHints,
        U"[",
        "Next section end"));
    (void)pressCharacter(core, window, QChar(u']'));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"after\n{\nnext"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"start\nbody\n}\nafter\n{\nnext",
        {0, 0}));
    command(core, window, QStringLiteral("2d]]"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"{\nnext"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"start\nbody\n}\nafter\n{\nnext",
        {0, 0}));
    command(core, window, QStringLiteral("d2]]"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"{\nnext"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"start\nbody\n}\nafter\n{\nnext",
        {0, 0}));
    command(core, window, QStringLiteral("d]["));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"}\nafter\n{\nnext"));

    // At EOF there is no line below the closing brace; d]] leaves the brace
    // itself, and an operator issued on the final line is a no-op.
    QVERIFY(core.replaceBufferText(
        buffer,
        u"start\nbody\n}",
        {0, 0}));
    command(core, window, QStringLiteral("d]]"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"}"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
    command(core, window, QStringLiteral("d]]"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"}"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
}

void VkEditingBehaviorTests::
    visualBlockTextObjectsAndNamedRegisters()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/advanced-editing.txt",
        u"abcd\nefgh\nijkl",
        {0, 1});
    QVERIFY(buffer != 0);

    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jl"));
    const auto block = core.window(window);
    QVERIFY(block.has_value());
    QVERIFY(block->visualAnchor.has_value());
    QVERIFY(block->visualBlockwise);
    command(core, window, QStringLiteral("d"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ad\neh\nijkl"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abcd\nefgh\nijkl"));

    QVERIFY(core.setViewCursor(window, {0, 1}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jlc"));
    insertText(core, window, u"Z");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"aZd\neZh\nijkl"));
    QVERIFY(core.setViewCursor(window, {2, 1}));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"aZd\neZh\niZl"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"aZd\neZh\nijkl"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abcd\nefgh\nijkl"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"one two",
        {0, 0}));
    command(core, window, QStringLiteral("\"adw"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"two"));
    command(core, window, QStringLiteral("\"aP"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one two"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"say \"hello world\" now",
        {0, 7}));
    command(core, window, QStringLiteral("di\""));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"say \"\" now"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"before (one two) after",
        {0, 9}));
    command(core, window, QStringLiteral("da("));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"before  after"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"<section><b>inside</b></section>",
        {0, 13}));
    command(core, window, QStringLiteral("dit"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"<section><b></b></section>"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"left <value> right",
        {0, 8}));
    command(core, window, QStringLiteral("da<"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"left  right"));
}

void VkEditingBehaviorTests::
    visualBlockUsesDisplayCellsAndVirtualColumns()
{
    {
        VkCore core;
        QCOMPARE(
            core.virtualEditMode(),
            VirtualEditMode::None);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-virtualedit-option.txt",
            u"abcdef\na",
            {0, 3});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("llj"));
        auto state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->cursor, (Cursor{1, 1}));
        QCOMPARE(state->displayCursor.column, std::size_t{1});

        (void)core.submitCommandLine(
            window,
            CommandLineKind::Ex,
            u"set ve=block");
        QCOMPARE(
            core.virtualEditMode(),
            VirtualEditMode::Block);
        command(core, window, QStringLiteral("kj"));
        state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->displayCursor.column, std::size_t{5});

        (void)core.submitCommandLine(
            window,
            CommandLineKind::Ex,
            u"set virtualedit=");
        QCOMPARE(
            core.virtualEditMode(),
            VirtualEditMode::None);
        state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->cursor, (Cursor{1, 1}));
        QCOMPARE(state->displayCursor.column, std::size_t{1});
    }

    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-escape-normalizes-coladd.txt",
            u"abcdef\na",
            {0, 3});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("llj"));
        QCOMPARE(
            core.window(window)->displayCursor.column,
            std::size_t{5});
        escape(core, window);
        const auto normal = core.window(window);
        QVERIFY(normal.has_value());
        QCOMPARE(normal->cursor, (Cursor{1, 0}));
        QCOMPARE(normal->displayCursor.column, std::size_t{0});
    }

    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-tabs.txt",
            u"a\tb\n0123456789",
            {0, 0});
        QVERIFY(buffer != 0);

        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("lllj"));
        const auto state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->cursor, (Cursor{1, 3}));
        QCOMPARE(state->displayCursor.column, std::size_t{3});
        QVERIFY(state->visualBlock.has_value());
        QCOMPARE(
            *state->visualBlock,
            (VisualBlockRange{0, 1, 0, 4}));
        const auto rows = core.visualBlockRows(window, 0, 1);
        QCOMPARE(rows.size(), std::size_t{2});
        QCOMPARE(rows[0].left.displayColumn, std::size_t{0});
        QCOMPARE(rows[0].right.displayColumn, std::size_t{4});
        QCOMPARE(rows[0].right.bufferColumn, std::size_t{1});
        QCOMPARE(rows[0].right.cellBufferEnd, std::size_t{2});
        command(core, window, QStringLiteral("d"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"    b\n456789"));
    }

    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-short-lines.txt",
            u"abcdef\na\nabcd",
            {0, 3});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("llj"));
        const auto virtualState = core.window(window);
        QVERIFY(virtualState.has_value());
        QCOMPARE(virtualState->cursor, (Cursor{1, 1}));
        QCOMPARE(
            virtualState->displayCursor.column,
            std::size_t{5});
        command(core, window, QStringLiteral("c"));
        insertText(core, window, u"X");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcX\na  X\nabcd"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcdef\na\nabcd"));
        QVERIFY(core.setViewCursor(window, {2, 1}));
        command(core, window, QStringLiteral("."));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcdef\na\naX"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-unicode.txt",
            u"a你b\naXb\na\u0301b\n😀z",
            {0, 0});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("ljd"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u" b\nb\na\u0301b\n😀z"));

        QVERIFY(core.setViewCursor(window, {2, 0}));
        command(core, window, QStringLiteral("l"));
        const auto combining = core.window(window);
        QVERIFY(combining.has_value());
        QCOMPARE(combining->cursor, (Cursor{2, 2}));
        QCOMPARE(
            combining->displayCursor.column,
            std::size_t{1});

        QVERIFY(core.setViewCursor(window, {3, 0}));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        const auto emoji = core.window(window);
        QVERIFY(emoji.has_value());
        QVERIFY(emoji->visualBlock.has_value());
        QCOMPARE(
            emoji->visualBlock->lastColumnExclusive,
            std::size_t{2});
        escape(core, window);
    }

    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-tabstop.txt",
            u"a\tb",
            {0, 0});
        QVERIFY(buffer != 0);
        QVERIFY(core.setBufferTabStop(buffer, 4));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("lll"));
        const auto state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->tabStop, std::size_t{4});
        QCOMPARE(state->displayCursor.column, std::size_t{3});
        QVERIFY(state->visualBlock.has_value());
        QCOMPARE(
            state->visualBlock->lastColumnExclusive,
            std::size_t{4});
        escape(core, window);
    }
}

void VkEditingBehaviorTests::
    visualBlockModeSpecificCommandsMatchNeovim()
{
    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-insert.txt",
            u"abcdef\na\nabcd",
            {0, 3});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("jjI"));
        insertText(core, window, u"X");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcXdef\na\nabcXd"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcdef\na\nabcd"));
    }

    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-append.txt",
            u"abcdef\na",
            {0, 3});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("jA"));
        insertText(core, window, u"X");
        escape(core, window);
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcdXef\na   X"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcdef\na"));
        QVERIFY(core.setViewCursor(window, {0, 0}));
        command(core, window, QStringLiteral("."));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"aXbcdef\naX"));
        command(core, window, QStringLiteral("u"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcdef\na"));
    }

    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-corners.txt",
            u"abcdef\nabcdef\nabcdef",
            {0, 1});
        QVERIFY(buffer != 0);
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("jjll"));
        command(core, window, QStringLiteral("o"));
        auto state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->cursor, (Cursor{0, 1}));
        QCOMPARE(
            state->visualDisplayAnchor,
            std::optional<DisplayPosition>(
                DisplayPosition{Cursor{2, 3}, 3}));
        command(core, window, QStringLiteral("O"));
        state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->cursor, (Cursor{0, 3}));
        QCOMPARE(
            state->visualDisplayAnchor,
            std::optional<DisplayPosition>(
                DisplayPosition{Cursor{2, 1}, 1}));
        escape(core, window);
    }

    {
        VkCore core;
        core.setVirtualEditMode(VirtualEditMode::Block);
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/block-put-replace-toggle.txt",
            u"XY\nUV\nabcdef\nabcdef",
            {0, 0});
        QVERIFY(buffer != 0);
        command(core, window, QStringLiteral("\"a"));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("ljy"));
        QVERIFY(core.setViewCursor(window, {2, 1}));
        command(core, window, QStringLiteral("\"a"));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("ljp"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"XY\nUV\naXYdef\naUVdef"));

        QVERIFY(core.replaceBufferText(
            buffer,
            u"XY\nUV\nabcdef\nabcdef",
            {2, 1}));
        command(core, window, QStringLiteral("\"a"));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("ljP"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"XY\nUV\naXYdef\naUVdef"));

        QVERIFY(core.replaceBufferText(
            buffer,
            u"abcdef\na",
            {0, 3}));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("ljrX"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"abcXXf\na  XX"));

        QVERIFY(core.replaceBufferText(
            buffer,
            u"abCD\nxyZZ",
            {0, 1}));
        (void)press(
            core,
            window,
            Qt::Key_V,
            controlModifier(),
            QStringLiteral("v"));
        command(core, window, QStringLiteral("lj~"));
        QCOMPARE(
            bufferText(core, buffer),
            std::u16string(u"aBcD\nxYzZ"));
    }
}

void VkEditingBehaviorTests::
    registersAndDotRepeatMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/register-semantics.txt",
        u"abc",
        {0, 0});
    QVERIFY(buffer != 0);

    // The black-hole register is empty when read; it must never alias the
    // unnamed register populated by the preceding delete.
    command(core, window, QStringLiteral("x\"_p"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"bc"));

    // The explicit register is part of the redo recipe.  Dot must update the
    // same register, exactly like Neovim's prep_redo(regname, ...).
    QVERIFY(core.replaceBufferText(
        buffer, u"abcd", {0, 0}));
    command(core, window, QStringLiteral("\"ax."));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"cd"));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"cbd"));

    // Appending characterwise text to a linewise named register preserves a
    // line boundary and the linewise register type.
    QVERIFY(core.replaceBufferText(
        buffer, u"abc\ndef", {0, 0}));
    command(core, window, QStringLiteral("\"ayy\"Ayl"));
    QVERIFY(core.replaceBufferText(buffer, u"X", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X\nabc\na\n"));

    // The inverse composition also introduces the missing line boundary.
    QVERIFY(core.replaceBufferText(
        buffer, u"abc\ndef", {0, 0}));
    command(core, window, QStringLiteral("\"ayl\"Ayy"));
    QVERIFY(core.replaceBufferText(buffer, u"X", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X\na\nabc\n"));

    // An explicit named target does not suppress numbered-register rotation
    // for a linewise delete. Small explicit deletes, however, leave `-`
    // untouched, and explicit yanks leave register 0 untouched.
    QVERIFY(core.replaceBufferText(
        buffer, u"one\ntwo\nthree", {0, 0}));
    command(core, window, QStringLiteral("\"add\"1P"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one\ntwo\nthree"));

    QVERIFY(core.replaceBufferText(
        buffer, u"ABC", {0, 0}));
    command(core, window, QStringLiteral("x\"ax\"-P"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"AC"));

    QVERIFY(core.replaceBufferText(
        buffer, u"ABC", {0, 0}));
    command(core, window, QStringLiteral("yll\"ayl\"0P"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"AABC"));

    // D/C spanning multiple lines rotate the numbered delete registers but
    // retain characterwise type. A put therefore reconstructs the exact
    // deleted suffix instead of inserting it as whole lines.
    QVERIFY(core.replaceBufferText(
        buffer, u"abc\ndef\nghi", {0, 1}));
    command(core, window, QStringLiteral("2Dp"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abc\ndef\nghi"));

    // Uppercase append preserves the destination register's type. Two block
    // yanks append rows vertically and retain the original screen-cell width.
    QVERIFY(core.replaceBufferText(
        buffer, u"ab\ncd\nXY\nZW", {0, 0}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jl\"ay"));
    QVERIFY(core.setViewCursor(window, {2, 0}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jl\"Ay"));
    QVERIFY(core.replaceBufferText(
        buffer, u".\n.\n.\n.", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u".ab\n.cd\n.XY\n.ZW"));

    QVERIFY(core.replaceBufferText(
        buffer, u"ab\ncd\nXY", {0, 0}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jl\"ay"));
    QVERIFY(core.setViewCursor(window, {2, 0}));
    command(core, window, QStringLiteral("\"Ayl"));
    QVERIFY(core.replaceBufferText(
        buffer, u".\n.\n.", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u".ab\n.cd\n.X"));

    QVERIFY(core.replaceBufferText(
        buffer, u"QZ\nab\ncd", {0, 0}));
    command(core, window, QStringLiteral("\"ayl"));
    QVERIFY(core.setViewCursor(window, {1, 0}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jl\"Ay"));
    QVERIFY(core.replaceBufferText(buffer, u".", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u".Qab\ncd"));

    // Dot stores the chosen register identity, not a stale copy of its old
    // payload. Replacing register a in another window must affect the replay.
    QVERIFY(core.replaceBufferText(buffer, u"A", {0, 0}));
    command(core, window, QStringLiteral("\"ay$"));
    QVERIFY(core.replaceBufferText(buffer, u"X", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"XA"));
    const WindowId other =
        core.registerWindow(WindowKind::Editor);
    QVERIFY(core.synchronizeBuffer(
        other,
        "/vault/register-replacement.txt",
        u"B",
        {0, 0}) != 0);
    command(core, other, QStringLiteral("\"ay$"));
    QVERIFY(core.setActiveView(window));
    QVERIFY(core.setViewCursor(window, {0, 1}));
    command(core, window, QStringLiteral("."));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"XAB"));
}

void VkEditingBehaviorTests::
    textObjectsAndSearchWhitespaceMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/text-object-boundaries.txt",
        u"one   two",
        {0, 3});
    QVERIFY(buffer != 0);

    // On whitespace, `iw` selects the whitespace run rather than silently
    // advancing to the next word.
    command(core, window, QStringLiteral("diw"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"onetwo"));

    QVERIFY(core.replaceBufferText(
        buffer, u"one   two three", {0, 3}));
    command(core, window, QStringLiteral("d2iw"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one three"));

    // Quote objects search the next pair on the current line when the cursor
    // is before it, matching Neovim's current_quote() behavior.
    QVERIFY(core.replaceBufferText(
        buffer, u"abc \"def\" xyz", {0, 0}));
    command(core, window, QStringLiteral("di\""));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abc \"\" xyz"));

    // Leading and trailing spaces are data in a search pattern, not command
    // line decoration.
    QVERIFY(core.replaceBufferText(
        buffer, u"x foo y\nfoo", {0, 0}));
    const DispatchResult searched = core.submitCommandLine(
        window,
        CommandLineKind::SearchForward,
        u" foo ");
    QVERIFY(std::ranges::none_of(
        searched.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 1}));
}

void VkEditingBehaviorTests::
    textObjectStateMachinesMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/text-object-state-machines.txt",
        u"one  ",
        {0, 3});
    QVERIFY(buffer != 0);

    // current_word(): inner trailing whitespace is addressable, but around
    // whitespace fails when there is no following word to pair it with.
    command(core, window, QStringLiteral("diw"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"one"));
    QVERIFY(core.replaceBufferText(buffer, u"one  ", {0, 3}));
    command(core, window, QStringLiteral("daw"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"one  "));

    // current_block(): a cursor before a block searches forward. Multiline
    // inner blocks retain the structural line break between their braces.
    QVERIFY(core.replaceBufferText(
        buffer, u"before (one) after", {0, 0}));
    command(core, window, QStringLiteral("di("));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"before () after"));
    QVERIFY(core.replaceBufferText(
        buffer, u"{\n  one\n}", {1, 2}));
    command(core, window, QStringLiteral("di{"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"{\n}"));

    // current_quote(): count expands the selected pair, while whitespace
    // between adjacent pairs is itself the inner object. Around removes the
    // two inner delimiters and joins the quoted contents.
    QVERIFY(core.replaceBufferText(
        buffer, u"\"one\" \"two\"", {0, 2}));
    command(core, window, QStringLiteral("d2i\""));
    QCOMPARE(bufferText(core, buffer), std::u16string(u" \"two\""));
    QVERIFY(core.replaceBufferText(
        buffer, u"\"one\" \"two\"", {0, 5}));
    command(core, window, QStringLiteral("di\""));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"\"one\"\"two\""));
    QVERIFY(core.replaceBufferText(
        buffer, u"\"one\" \"two\"", {0, 5}));
    command(core, window, QStringLiteral("da\""));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"\"onetwo\""));
    QVERIFY(core.replaceBufferText(
        buffer, u"\"one\" \"two\"", {0, 4}));
    command(core, window, QStringLiteral("d2i\""));
    QCOMPARE(bufferText(core, buffer), std::u16string(u" \"two\""));

    // current_par(): blank runs are independent inner objects; around pairs
    // them with the following paragraph, with a leading fallback at EOF.
    QVERIFY(core.replaceBufferText(
        buffer, u"one\n\n\ntwo", {1, 0}));
    command(core, window, QStringLiteral("dip"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"one\ntwo"));
    QVERIFY(core.replaceBufferText(
        buffer, u"one\n\n\ntwo", {1, 0}));
    command(core, window, QStringLiteral("dap"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"one"));
    QVERIFY(core.replaceBufferText(
        buffer, u"one\n\ntwo", {2, 0}));
    command(core, window, QStringLiteral("dap"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"one"));

    // findsent/current_sent(): decimal dots do not terminate a sentence;
    // whitespace between sentences is an inner object, and around uses the
    // following sentence or the preceding fallback at EOF.
    QVERIFY(core.replaceBufferText(
        buffer, u"value 3.14 here. Next.", {0, 6}));
    command(core, window, QStringLiteral("dis"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u" Next."));
    QVERIFY(core.replaceBufferText(
        buffer, u"One.  Two.", {0, 5}));
    command(core, window, QStringLiteral("dis"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"One.Two."));
    QVERIFY(core.replaceBufferText(
        buffer, u"One.  Two.", {0, 5}));
    command(core, window, QStringLiteral("das"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"One."));
    QVERIFY(core.replaceBufferText(
        buffer, u"One.  Two.", {0, 7}));
    command(core, window, QStringLiteral("das"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"One."));

    // HTML tag names are ASCII-case-insensitive just like Neovim's tag text
    // object, while the original spelling remains untouched.
    QVERIFY(core.replaceBufferText(
        buffer, u"<DIV>x</div>", {0, 5}));
    command(core, window, QStringLiteral("dit"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"<DIV></div>"));
}

void VkEditingBehaviorTests::
    marksSearchJumpListAndMacros()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/navigation.txt",
        u"zero needle\none\ntwo needle\nthree",
        {0, 2});
    QVERIFY(buffer != 0);

    command(core, window, QStringLiteral("maG`a"));
    auto state = core.window(window);
    QVERIFY(state.has_value());
    QCOMPARE(state->cursor, (Cursor{0, 2}));
    command(core, window, QStringLiteral("G'a"));
    state = core.window(window);
    QVERIFY(state.has_value());
    QCOMPARE(state->cursor, (Cursor{0, 0}));

    DispatchResult search = core.submitCommandLine(
        window,
        CommandLineKind::SearchForward,
        u"needle");
    QVERIFY(std::none_of(
        search.events.cbegin(),
        search.events.cend(),
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
    state = core.window(window);
    QVERIFY(state.has_value());
    QCOMPARE(state->cursor.line, std::size_t{0});
    QCOMPARE(state->cursor.column, std::size_t{5});
    command(core, window, QStringLiteral("n"));
    state = core.window(window);
    QVERIFY(state.has_value());
    QCOMPARE(state->cursor, (Cursor{2, 4}));
    (void)press(
        core,
        window,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    state = core.window(window);
    QVERIFY(state.has_value());
    QCOMPARE(state->cursor, (Cursor{0, 5}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"abcdef",
        {0, 0}));
    command(core, window, QStringLiteral("qaxlq"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"bcdef"));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("@a"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"cdef"));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"cef"));

    VkCore barrierCore;
    const WindowId barrierWindow =
        barrierCore.registerWindow(WindowKind::Editor);
    const BufferId barrierBuffer =
        barrierCore.synchronizeBuffer(
            barrierWindow,
            "/vault/macro-barrier.txt",
            u"abc",
            {0, 0});
    QVERIFY(barrierBuffer != 0);
    MappingDefinition mapped;
    mapped.lhs = U"z";
    mapped.target = HostActionTarget{
        HostAction::FocusPanelRight};
    std::string mappingError;
    QVERIFY2(
        barrierCore.addMapping(mapped, &mappingError) != 0,
        mappingError.c_str());
    command(barrierCore, barrierWindow, QStringLiteral("qazxq"));
    QVERIFY(barrierCore.replaceBufferText(
        barrierBuffer, u"abc", {0, 0}));
    (void)pressCharacter(
        barrierCore, barrierWindow, QChar(u'@'));
    const DispatchResult paused = pressCharacter(
        barrierCore, barrierWindow, QChar(u'a'));
    QVERIFY(paused.hostBarrierGeneration.has_value());
    QVERIFY(std::ranges::any_of(
        paused.events,
        [](const Event &event) {
            return event.type == EventType::HostAction
                && event.hostAction
                    == HostAction::FocusPanelRight;
        }));
    QCOMPARE(
        bufferText(barrierCore, barrierBuffer),
        std::u16string(u"abc"));
    const DispatchResult resumed =
        barrierCore.acknowledgeHostBarrier(
            *paused.hostBarrierGeneration,
            true,
            barrierWindow,
            barrierWindow);
    QVERIFY(!resumed.hostBarrierGeneration.has_value());
    QCOMPARE(
        bufferText(barrierCore, barrierBuffer),
        std::u16string(u"bc"));
}

void VkEditingBehaviorTests::
    macroFailureDispositionMatchesNeovim()
{
    const auto verify = [](
        const std::u16string &macro,
        const std::u16string &initial,
        const Cursor expectedCursor,
        const std::u16string &expectedText,
        const bool readOnly = false) {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/macro-disposition.txt",
            macro,
            {0, 0});
        QVERIFY(buffer != 0);
        command(core, window, QStringLiteral("\"ay$"));
        QVERIFY(core.replaceBufferText(
            buffer, initial, {0, 0}));
        QVERIFY(core.setBufferReadOnly(buffer, readOnly));
        command(core, window, QStringLiteral("@a"));
        QCOMPARE(bufferText(core, buffer), expectedText);
        const auto state = core.window(window);
        QVERIFY(state.has_value());
        QCOMPARE(state->cursor, expectedCursor);
    };

    // searchc()/current_block() failures abort the remaining macro keys.
    verify(u"fxl", u"abc", {0, 0}, u"abc");
    verify(u";l", u"abc", {0, 0}, u"abc");
    verify(u"dfxl", u"abc", {0, 0}, u"abc");
    verify(u"di(l", u"abc", {0, 0}, u"abc");

    // These failures are explicitly non-aborting in Neovim: a missing pair
    // for %, failed n, an unset mark, and a rejected read-only edit all leave
    // the following `l` executable.
    verify(u"%l", u"abc", {0, 1}, u"abc");
    verify(u"nl", u"abc", {0, 1}, u"abc");
    verify(u"`zl", u"abc", {0, 1}, u"abc");
    verify(u"xl", u"abc", {0, 1}, u"abc", true);
}

void VkEditingBehaviorTests::
    jumpListTracksEditsAndJumpMotions()
{
    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/same-line-search-jump.txt",
            u"a x x",
            {0, 0});
        QVERIFY(buffer != 0);
        (void)core.submitCommandLine(
            window,
            CommandLineKind::SearchForward,
            u"x");
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));
        (void)press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        // Neovim only records a search jump when it crosses a line.
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/jump-edit.txt",
            u"a\nc",
            {0, 0});
        QVERIFY(buffer != 0);
        (void)core.submitCommandLine(
            window,
            CommandLineKind::SearchForward,
            u"c");
        (void)press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
        command(core, window, QStringLiteral("i"));
        insertText(core, window, u"X");
        escape(core, window);
        (void)press(
            core,
            window,
            Qt::Key_I,
            controlModifier(),
            QStringLiteral("i"));
        QCOMPARE(bufferText(core, buffer), std::u16string(u"Xa\nc"));
        QCOMPARE(core.window(window)->cursor, (Cursor{1, 0}));
    }

    const auto verifyJumpBack = [](
        const std::u16string &text,
        const Cursor start,
        const QString &motion,
        const Cursor destination) {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/jump-motion.txt",
            text,
            start);
        QVERIFY(buffer != 0);
        command(core, window, motion);
        QCOMPARE(core.window(window)->cursor, destination);
        (void)press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        QCOMPARE(core.window(window)->cursor, start);
    };
    verifyJumpBack(
        u"a\nb\nc", {0, 0}, QStringLiteral("G"), {2, 0});
    verifyJumpBack(
        u"a\nb\nc", {2, 0}, QStringLiteral("gg"), {0, 0});

    // Same-line jumps are not separate jumplist entries in Neovim, even
    // when their columns differ.
    VkCore sameLineCore;
    const WindowId sameLineWindow =
        sameLineCore.registerWindow(WindowKind::Editor);
    QVERIFY(sameLineCore.synchronizeBuffer(
                sameLineWindow,
                "/vault/same-line-pair.txt",
                u"(x)",
                {0, 0})
            != 0);
    command(sameLineCore, sameLineWindow, QStringLiteral("%"));
    QCOMPARE(
        sameLineCore.window(sameLineWindow)->cursor,
        (Cursor{0, 2}));
    const DispatchResult noOlderPair = press(
        sameLineCore,
        sameLineWindow,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    QCOMPARE(
        sameLineCore.window(sameLineWindow)->cursor,
        (Cursor{0, 2}));
    QVERIFY(std::ranges::any_of(
        noOlderPair.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
}

void VkEditingBehaviorTests::
    jumpListReturnsToTheLiveCursorLikeNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/live-jumplist-cursor.txt",
        u"zero\none\ntwo\nthree",
        {0, 0});
    QVERIFY(buffer != 0);

    command(core, window, QStringLiteral("Gk"));
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 0}));

    (void)press(
        core,
        window,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));

    (void)press(
        core,
        window,
        Qt::Key_I,
        controlModifier(),
        QStringLiteral("i"));
    // CTRL-I returns to the live position at which CTRL-O was invoked, not
    // the stale destination of the earlier G jump.
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 0}));

    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("}"));
    const Cursor paragraphDestination =
        core.window(window)->cursor;
    QVERIFY(paragraphDestination != (Cursor{0, 0}));
    (void)press(
        core,
        window,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
}

void VkEditingBehaviorTests::
    jumpListCountsTabDedupAndBranchingMatchNeovim()
{
    const auto inputError = [](
        const DispatchResult &result,
        const std::string_view direction) {
        return std::ranges::any_of(
            result.events,
            [direction](const Event &event) {
                return event.type == EventType::InputError
                    && event.message.find(direction)
                        != std::string::npos;
            });
    };

    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/jumplist-count-tab.txt",
            u"zero\none\ntwo\nthree",
            {0, 0});
        QVERIFY(buffer != 0);
        command(core, window, QStringLiteral("G"));
        QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));

        // Counts are atomic. Neovim does not clamp a too-large CTRL-O or
        // CTRL-I to the oldest/newest entry.
        (void)pressCharacter(core, window, QChar(u'2'));
        const DispatchResult tooOld = press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
        QVERIFY(inputError(tooOld, "older"));

        (void)press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
        (void)pressCharacter(core, window, QChar(u'2'));
        const DispatchResult tooNew = press(
            core,
            window,
            Qt::Key_I,
            controlModifier(),
            QStringLiteral("i"));
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
        QVERIFY(inputError(tooNew, "newer"));

        // Unmodified Tab is CTRL-I's Normal-mode alias.
        (void)press(core, window, Qt::Key_Tab);
        QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
        const DispatchResult newest =
            press(core, window, Qt::Key_Tab);
        QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
        QVERIFY(inputError(newest, "newer"));

        // The alias is deliberately not active in Visual or Insert mode.
        command(core, window, QStringLiteral("v"));
        (void)press(core, window, Qt::Key_Tab);
        QCOMPARE(core.mode(), std::optional<Mode>(Mode::Visual));
        QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
        escape(core, window);
        command(core, window, QStringLiteral("i"));
        (void)press(core, window, Qt::Key_Tab);
        QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
        QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/jumplist-line-dedup.txt",
            u"abcdef\none\ntwo",
            {2, 0});
        QVERIFY(buffer != 0);
        command(core, window, QStringLiteral("ma"));

        QVERIFY(core.setViewCursor(window, {0, 1}));
        command(core, window, QStringLiteral("`a"));
        QVERIFY(core.setViewCursor(window, {0, 4}));
        command(core, window, QStringLiteral("`a"));

        (void)press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        // Duplicate identity is buffer+line, not the exact column. Only the
        // newest position for line zero remains reachable.
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 4}));
        const DispatchResult oldest = press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 4}));
        QVERIFY(inputError(oldest, "older"));
    }

    {
        VkCore core;
        const WindowId window =
            core.registerWindow(WindowKind::Editor);
        const BufferId buffer = core.synchronizeBuffer(
            window,
            "/vault/jumplist-default-non-stack.txt",
            u"zero\none\ntwo\nthree\nfour\nfive",
            {0, 0});
        QVERIFY(buffer != 0);

        // Build origins on lines 0, 4 and 1, with the live cursor on 5.
        command(core, window, QStringLiteral("GkggjG"));
        QCOMPARE(core.window(window)->cursor, (Cursor{5, 0}));
        (void)pressCharacter(core, window, QChar(u'2'));
        (void)press(
            core,
            window,
            Qt::Key_O,
            controlModifier(),
            QStringLiteral("o"));
        QCOMPARE(core.window(window)->cursor, (Cursor{4, 0}));

        // Default 'jumpoptions' is non-stack. A jump from the middle keeps
        // the former forward entries (lines 1 and 5) in history.
        command(core, window, QStringLiteral("gg"));
        QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
        for (const Cursor expected : {
                 Cursor{4, 0}, Cursor{5, 0}, Cursor{1, 0}}) {
            const DispatchResult moved = press(
                core,
                window,
                Qt::Key_O,
                controlModifier(),
                QStringLiteral("o"));
            QVERIFY(!inputError(moved, "older"));
            QCOMPARE(core.window(window)->cursor, expected);
        }
    }
}

void VkEditingBehaviorTests::
    splitWindowsCloneButThenOwnTheirJumpLists()
{
    VkCore core;
    const WindowId source =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        source,
        "/vault/split-jumplist.txt",
        u"zero\none\ntwo",
        {0, 0});
    QVERIFY(buffer != 0);
    command(core, source, QStringLiteral("G"));

    const WindowId split =
        core.registerWindow(WindowKind::Editor);
    QVERIFY(core.attachBuffer(split, buffer, {2, 0}));
    QVERIFY(core.cloneWindowNavigationState(source, split));
    (void)press(
        core,
        split,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    QCOMPARE(core.window(split)->cursor, (Cursor{0, 0}));

    // Browsing the cloned list is window-local and cannot move the source.
    QCOMPARE(core.window(source)->cursor, (Cursor{2, 0}));
    (void)press(
        core,
        split,
        Qt::Key_I,
        controlModifier(),
        QStringLiteral("i"));
    QCOMPARE(core.window(split)->cursor, (Cursor{2, 0}));
    QCOMPARE(core.window(source)->cursor, (Cursor{2, 0}));
}

void VkEditingBehaviorTests::
    wordUnderCursorSearchMatchesNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/star-search.txt",
        u"cat scatter cat",
        {0, 0});
    QVERIFY(buffer != 0);

    command(core, window, QStringLiteral("*"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 12}));
    command(core, window, QStringLiteral("#"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));

    QVERIFY(core.replaceBufferText(
        buffer, u"cat scatter cat", {0, 0}));
    command(core, window, QStringLiteral("g*"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 5}));

    QVERIFY(core.replaceBufferText(
        buffer, u"猫 猫咪 猫", {0, 0}));
    command(core, window, QStringLiteral("*"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 5}));
    QVERIFY(core.replaceBufferText(
        buffer, u"猫 猫咪 猫", {0, 0}));
    command(core, window, QStringLiteral("g*"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));

    QVERIFY(core.replaceBufferText(
        buffer, u"one one one one", {0, 0}));
    command(core, window, QStringLiteral("2*"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 8}));
}

void VkEditingBehaviorTests::
    specialMarksAndCrossBufferJumpsAreTransactional()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/special-marks.txt",
        u"  alpha\nbeta\ngamma",
        {0, 3});
    QVERIFY(buffer != 0);

    // The previous-context mark is exact with backtick and line-normalized
    // with apostrophe; each jump updates it so the command can toggle back.
    command(core, window, QStringLiteral("G``"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 3}));
    command(core, window, QStringLiteral("G''"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));

    QVERIFY(core.replaceBufferText(
        buffer, u"abcd\nef", {0, 1}));
    command(core, window, QStringLiteral("i"));
    insertText(core, window, u"X");
    escape(core, window);
    command(core, window, QStringLiteral("G`."));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 1}));
    command(core, window, QStringLiteral("G`^"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));

    QVERIFY(core.replaceBufferText(
        buffer, u"abcd", {0, 0}));
    command(core, window, QStringLiteral("y2l"));
    QVERIFY(core.setViewCursor(window, {0, 3}));
    command(core, window, QStringLiteral("`["));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
    QVERIFY(core.setViewCursor(window, {0, 3}));
    command(core, window, QStringLiteral("`]"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 1}));

    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("vll"));
    escape(core, window);
    QVERIFY(core.setViewCursor(window, {0, 3}));
    command(core, window, QStringLiteral("`<"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
    QVERIFY(core.setViewCursor(window, {0, 3}));
    command(core, window, QStringLiteral("`>"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));

    QVERIFY(core.setViewCursor(window, {0, 2}));
    const BufferId other = core.synchronizeBuffer(
        window,
        "/vault/other-special-mark.txt",
        u"other",
        {0, 0});
    QVERIFY(other != 0);
    QVERIFY(core.attachBuffer(window, buffer, {0, 0}));
    command(core, window, QStringLiteral("`\""));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 2}));

    VkCore transactionCore;
    const WindowId originWindow =
        transactionCore.registerWindow(WindowKind::Editor);
    const WindowId markWindow =
        transactionCore.registerWindow(WindowKind::Editor);
    const BufferId originBuffer =
        transactionCore.synchronizeBuffer(
            originWindow,
            "/vault/jump-origin.txt",
            u"origin",
            {0, 0});
    const BufferId markBuffer =
        transactionCore.synchronizeBuffer(
            markWindow,
            "/vault/jump-target.txt",
            u"target\nline",
            {1, 0});
    QVERIFY(originBuffer != 0);
    QVERIFY(markBuffer != 0);
    QVERIFY(transactionCore.setActiveWindow(markWindow));
    command(transactionCore, markWindow, QStringLiteral("mA"));
    QVERIFY(transactionCore.setActiveWindow(originWindow));

    (void)pressCharacter(
        transactionCore, originWindow, QChar(u'`'));
    DispatchResult requested = pressCharacter(
        transactionCore, originWindow, QChar(u'A'));
    QVERIFY(requested.hostBarrierGeneration.has_value());
    const auto activation = std::ranges::find_if(
        requested.events,
        [markBuffer](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == markBuffer;
        });
    QVERIFY(activation != requested.events.cend());
    (void)transactionCore.acknowledgeHostBarrier(
        *requested.hostBarrierGeneration,
        false,
        originWindow,
        originWindow);
    const DispatchResult noForwardJump = press(
        transactionCore,
        originWindow,
        Qt::Key_I,
        controlModifier(),
        QStringLiteral("i"));
    QVERIFY(std::ranges::none_of(
        noForwardJump.events,
        [](const Event &event) {
            return event.type
                == EventType::BufferActivationRequested;
        }));

    (void)pressCharacter(
        transactionCore, originWindow, QChar(u'`'));
    requested = pressCharacter(
        transactionCore, originWindow, QChar(u'A'));
    QVERIFY(requested.hostBarrierGeneration.has_value());
    const auto committedActivation = std::ranges::find_if(
        requested.events,
        [](const Event &event) {
            return event.type
                == EventType::BufferActivationRequested;
        });
    QVERIFY(committedActivation != requested.events.cend());
    QVERIFY(transactionCore.attachBuffer(
        originWindow,
        markBuffer,
        committedActivation->cursor));
    (void)transactionCore.acknowledgeHostBarrier(
        *requested.hostBarrierGeneration,
        true,
        originWindow,
        originWindow);
    QCOMPARE(
        transactionCore.window(originWindow)->buffer,
        markBuffer);
    QCOMPARE(
        transactionCore.window(originWindow)->cursor,
        (Cursor{1, 0}));

    const DispatchResult jumpBack = press(
        transactionCore,
        originWindow,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    QVERIFY(jumpBack.hostBarrierGeneration.has_value());
    QVERIFY(std::ranges::any_of(
        jumpBack.events,
        [originBuffer](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == originBuffer;
        }));
    (void)transactionCore.acknowledgeHostBarrier(
        *jumpBack.hostBarrierGeneration,
        false,
        originWindow,
        originWindow);
}

void VkEditingBehaviorTests::
    changeListAndLastInsertPositionMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/change-list.txt",
        u"one\ntwo\nthree\nfour",
        {0, 0});
    QVERIFY(buffer != 0);

    // Change positions belong to the buffer. Traversal is window-local and
    // starts one past the newest entry after every live edit.
    command(core, window, QStringLiteral("x"));
    QVERIFY(core.setViewCursor(window, {2, 0}));
    command(core, window, QStringLiteral("x"));
    QVERIFY(core.setViewCursor(window, {3, 0}));
    command(core, window, QStringLiteral("x"));

    command(core, window, QStringLiteral("g;"));
    QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
    command(core, window, QStringLiteral("g;"));
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 0}));
    command(core, window, QStringLiteral("99g;"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));
    const DispatchResult atStart =
        pressCharacter(core, window, QChar(u'g'));
    Q_UNUSED(atStart);
    const DispatchResult rejectedBack =
        pressCharacter(core, window, QChar(u';'));
    QVERIFY(std::ranges::any_of(
        rejectedBack.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));

    command(core, window, QStringLiteral("g,"));
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 0}));
    command(core, window, QStringLiteral("99g,"));
    QCOMPARE(core.window(window)->cursor, (Cursor{3, 0}));
    const DispatchResult gotoPrefix =
        pressCharacter(core, window, QChar(u'g'));
    Q_UNUSED(gotoPrefix);
    const DispatchResult rejectedForward =
        pressCharacter(core, window, QChar(u','));
    QVERIFY(std::ranges::any_of(
        rejectedForward.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));

    // From the one-past-tail live position, both directions select the most
    // recent change, matching get_changelist() rather than treating g, as a
    // no-op.
    QVERIFY(core.setViewCursor(window, {1, 0}));
    command(core, window, QStringLiteral("x"));
    command(core, window, QStringLiteral("g,"));
    QCOMPARE(core.window(window)->cursor, (Cursor{1, 0}));

    // Undo/redo repair stored offsets but do not manufacture or discard
    // changelist entries.
    command(core, window, QStringLiteral("u"));
    command(core, window, QStringLiteral("g;"));
    QCOMPARE(core.window(window)->cursor.line, std::size_t{3});
    (void)press(
        core,
        window,
        Qt::Key_R,
        controlModifier(),
        QStringLiteral("r"));
    command(core, window, QStringLiteral("g,"));
    QCOMPARE(core.window(window)->cursor.line, std::size_t{1});

    QVERIFY(core.replaceBufferText(buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("A"));
    insertText(core, window, u"XY");
    escape(core, window);
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 4}));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("gi"));
    QCOMPARE(core.mode(), Mode::Insert);
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 5}));
    insertText(core, window, u"Z");
    escape(core, window);
    QCOMPARE(bufferText(core, buffer), std::u16string(u"abcXYZ"));

    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral("3gi"));
    insertText(core, window, u"Q");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abcXYZQQQ"));

    VkCore fresh;
    const WindowId freshWindow =
        fresh.registerWindow(WindowKind::Editor);
    const BufferId freshBuffer = fresh.synchronizeBuffer(
        freshWindow,
        "/vault/gi-without-mark.txt",
        u"abc",
        {0, 2});
    command(fresh, freshWindow, QStringLiteral("gi"));
    QCOMPARE(fresh.window(freshWindow)->cursor, (Cursor{0, 0}));
    insertText(fresh, freshWindow, u"X");
    escape(fresh, freshWindow);
    QCOMPARE(
        bufferText(fresh, freshBuffer),
        std::u16string(u"Xabc"));

    VkCore coalesced;
    const WindowId coalescedWindow =
        coalesced.registerWindow(WindowKind::Editor);
    QVERIFY(coalesced.synchronizeBuffer(
        coalescedWindow,
        "/vault/coalesced-changes.txt",
        std::u16string(100, u'x'),
        {0, 0}) != 0);
    command(coalesced, coalescedWindow, QStringLiteral("x"));
    QVERIFY(coalesced.setViewCursor(coalescedWindow, {0, 10}));
    command(coalesced, coalescedWindow, QStringLiteral("x"));
    command(coalesced, coalescedWindow, QStringLiteral("g;"));
    QCOMPARE(
        coalesced.window(coalescedWindow)->cursor,
        (Cursor{0, 10}));
    (void)pressCharacter(
        coalesced, coalescedWindow, QChar(u'g'));
    const DispatchResult coalescedStart = pressCharacter(
        coalesced, coalescedWindow, QChar(u';'));
    QVERIFY(std::ranges::any_of(
        coalescedStart.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));

    VkCore bounded;
    const WindowId boundedWindow =
        bounded.registerWindow(WindowKind::Editor);
    std::u16string lines;
    for (std::size_t line = 0; line < 105; ++line) {
        if (!lines.empty()) {
            lines.push_back(u'\n');
        }
        lines.push_back(u'x');
    }
    QVERIFY(bounded.synchronizeBuffer(
        boundedWindow,
        "/vault/bounded-changes.txt",
        std::move(lines),
        {0, 0}) != 0);
    for (std::size_t line = 0; line < 105; ++line) {
        QVERIFY(bounded.setViewCursor(
            boundedWindow, {line, 0}));
        command(bounded, boundedWindow, QStringLiteral("x"));
    }
    command(bounded, boundedWindow, QStringLiteral("999g;"));
    QCOMPARE(
        bounded.window(boundedWindow)->cursor.line,
        std::size_t{5});
}

void VkEditingBehaviorTests::
    alternateBufferIsWindowLocalAndTransactional()
{
    VkCore core;
    const WindowId firstWindow =
        core.registerWindow(WindowKind::Editor);
    const WindowId secondWindow =
        core.registerWindow(WindowKind::Editor);
    const BufferId first = core.synchronizeBuffer(
        firstWindow, "/vault/first.txt", u"first", {0, 0});
    const BufferId second = core.synchronizeBuffer(
        secondWindow, "/vault/second.txt", u"second", {0, 0});
    const BufferId third = core.synchronizeBuffer(
        secondWindow, "/vault/third.txt", u"third", {0, 0});
    QVERIFY(first != 0);
    QVERIFY(second != 0);
    QVERIFY(third != 0);

    QVERIFY(core.attachBuffer(firstWindow, second, {0, 0}));
    QVERIFY(core.setActiveWindow(firstWindow));
    DispatchResult requested = press(
        core,
        firstWindow,
        Qt::Key_6,
        controlModifier(),
        QStringLiteral("^"));
    QVERIFY(std::ranges::any_of(
        requested.events,
        [first](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == first;
        }));
    QCOMPARE(core.window(firstWindow)->buffer, second);

    // Rejecting the host transaction leaves both the current and alternate
    // identities untouched.
    requested = press(
        core,
        firstWindow,
        Qt::Key_6,
        controlModifier() | Qt::ShiftModifier,
        QStringLiteral("^"));
    const auto activation = std::ranges::find_if(
        requested.events,
        [first](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == first;
        });
    QVERIFY(activation != requested.events.cend());
    QVERIFY(core.attachBuffer(firstWindow, first, {0, 0}));
    QCOMPARE(core.window(firstWindow)->buffer, first);

    requested = press(
        core,
        firstWindow,
        Qt::Key_6,
        controlModifier(),
        QStringLiteral("^"));
    QVERIFY(std::ranges::any_of(
        requested.events,
        [second](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == second;
        }));

    // A count addresses the stable Core buffer id, exactly as [count]CTRL-^
    // addresses a Neovim buffer number.
    command(
        core,
        firstWindow,
        QString::number(static_cast<qulonglong>(third)));
    requested = press(
        core,
        firstWindow,
        Qt::Key_6,
        controlModifier(),
        QStringLiteral("^"));
    QVERIFY(std::ranges::any_of(
        requested.events,
        [third](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == third;
        }));

    // The second window has its own alternate (second), independent of the
    // first window's first<->second history.
    QVERIFY(core.setActiveWindow(secondWindow));
    requested = press(
        core,
        secondWindow,
        Qt::Key_6,
        controlModifier(),
        QStringLiteral("^"));
    QVERIFY(std::ranges::any_of(
        requested.events,
        [second](const Event &event) {
            return event.type
                    == EventType::BufferActivationRequested
                && event.buffer == second;
        }));

    QVERIFY(core.removeBuffer(second));
    const DispatchResult removedAlternate = press(
        core,
        secondWindow,
        Qt::Key_6,
        controlModifier(),
        QStringLiteral("^"));
    QVERIFY(std::ranges::none_of(
        removedAlternate.events,
        [](const Event &event) {
            return event.type
                == EventType::BufferActivationRequested;
        }));
    QVERIFY(std::ranges::any_of(
        removedAlternate.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
}

void VkEditingBehaviorTests::
    countedSearchInsertAndTextObjectsMatchNeovim()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/count-boundaries.txt",
        u"abc",
        {0, 0});
    QVERIFY(buffer != 0);

    command(core, window, QStringLiteral("2i"));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(bufferText(core, buffer), std::u16string(u"XXabc"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"abc"));

    QVERIFY(core.replaceBufferText(
        buffer, u"(a(b)c)d", {0, 3}));
    command(core, window, QStringLiteral("d2i("));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"()d"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"zero needle one needle two needle",
        {0, 0}));
    const DispatchResult prefix = pressCharacter(
        core, window, QChar(u'2'));
    QCOMPARE(prefix.disposition, InputDisposition::Consumed);
    const DispatchResult requested = pressCharacter(
        core, window, QChar(u'/'));
    const auto request = std::ranges::find_if(
        requested.events,
        [](const Event &event) {
            return event.type == EventType::CommandLineRequested;
        });
    QVERIFY(request != requested.events.cend());
    QCOMPARE(request->count, std::size_t{2});
    QVERIFY(request->countWasExplicit);
    const DispatchResult searched = core.submitCommandLine(
        window,
        CommandLineKind::SearchForward,
        u"needle");
    QVERIFY(std::ranges::none_of(
        searched.events,
        [](const Event &event) {
            return event.type == EventType::InputError;
        }));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 16}));
}

void VkEditingBehaviorTests::
    macroRegistersAndVisualDotShareAuthoritativeState()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/register-dot.txt",
        u"abc",
        {0, 0});
    QVERIFY(buffer != 0);

    // A recorded macro is immediately the text payload of the same named
    // register, and a later yank into that register is executable by @.
    command(core, window, QStringLiteral("qaxq"));
    QVERIFY(core.replaceBufferText(buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"axbc"));
    command(core, window, QStringLiteral("qAlq"));
    QVERIFY(core.replaceBufferText(buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("@a"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"bc"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 1}));
    QVERIFY(core.replaceBufferText(buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("\"ap"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"axlbc"));
    QVERIFY(core.replaceBufferText(buffer, u"xabc", {0, 0}));
    command(core, window, QStringLiteral("\"ayl"));
    QVERIFY(core.replaceBufferText(buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("@a"));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"bc"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 0}));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"one two three",
        {0, 0}));
    command(core, window, QStringLiteral("ciw"));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X two three"));
    QVERIFY(core.setViewCursor(window, {0, 2}));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"X X three"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"a\nb\nc\nd\ne\nf\ng",
        {0, 0}));
    command(core, window, QStringLiteral("V2jd2."));
    QCOMPARE(bufferText(core, buffer), std::u16string(u"g"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"abcd\nefgh\nijkl\nmnop",
        {0, 0}));
    command(core, window, QStringLiteral("vjld"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"gh\nijkl\nmnop"));
    command(core, window, QStringLiteral("2."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"kl\nmnop"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"gh\nijkl\nmnop"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"abcd\nefgh\nijkl\nmnop",
        {0, 0}));
    command(core, window, QStringLiteral("vjlc"));
    insertText(core, window, u"X");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"Xgh\nijkl\nmnop"));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"Xkl\nmnop"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"abcdefgh\nijklmnop\nqrstuvwx",
        {0, 0}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("jld2."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"efgh\nmnop\nqrstuvwx"));

    command(core, window, QStringLiteral("qbxlq"));
    QVERIFY(core.replaceBufferText(
        buffer,
        u"efgh\nmnop\nqrstuvwx",
        {0, 0}));
    // Replaying dot or a macro against a read-only buffer neither mutates
    // text nor lets a rejected write abort the remaining macro typeahead.
    QVERIFY(core.setBufferReadOnly(buffer, true));
    const DispatchResult rejected = pressCharacter(
        core, window, QChar(u'.'));
    QVERIFY(std::ranges::any_of(
        rejected.events,
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("read-only")
                    != std::string::npos;
        }));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"efgh\nmnop\nqrstuvwx"));
    (void)pressCharacter(core, window, QChar(u'@'));
    const DispatchResult macroResult = pressCharacter(
        core, window, QChar(u'b'));
    QVERIFY(std::ranges::any_of(
        macroResult.events,
        [](const Event &event) {
            return event.type == EventType::InputError
                && event.message.find("read-only")
                    != std::string::npos;
        }));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"efgh\nmnop\nqrstuvwx"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 1}));
}

void VkEditingBehaviorTests::
    exRangesReachUserCommandsWithoutLosingContext()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/ex-ranges.txt",
        u"one\ntwo\nthree\nfour",
        {1, 0});
    QVERIFY(buffer != 0);

    UserCommandDefinition ranged{
        "RangeDo", "plugin.range.run", "Run on lines"};
    ranged.acceptsRange = true;
    UserCommandDefinition counted{
        "CountDo", "plugin.count.run", "Run count"};
    counted.acceptsCount = true;
    std::string error;
    auto rangedLease = core.userCommands().registerCommand(
        "plugin.range", std::move(ranged), &error);
    QVERIFY2(rangedLease, error.c_str());
    auto countedLease = core.userCommands().registerCommand(
        "plugin.count", std::move(counted), &error);
    QVERIFY2(countedLease, error.c_str());

    const DispatchResult rangeResult = core.submitCommandLine(
        window, CommandLineKind::Ex, u":1,3RangeDo");
    const auto rangeEvent = std::ranges::find_if(
        rangeResult.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(rangeEvent != rangeResult.events.cend());
    QCOMPARE(
        rangeEvent->lineRange,
        std::optional<CommandLineRange>({1, 3}));

    const DispatchResult percentResult = core.submitCommandLine(
        window, CommandLineKind::Ex, u":%RangeDo");
    const auto percentEvent = std::ranges::find_if(
        percentResult.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(percentEvent != percentResult.events.cend());
    QCOMPARE(
        percentEvent->lineRange,
        std::optional<CommandLineRange>({1, 4}));

    const DispatchResult countResult = core.submitCommandLine(
        window, CommandLineKind::Ex, u":3CountDo");
    const auto countEvent = std::ranges::find_if(
        countResult.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(countEvent != countResult.events.cend());
    QCOMPARE(countEvent->count, std::size_t{3});
    QVERIFY(countEvent->countWasExplicit);
}

void VkEditingBehaviorTests::
    writeQuitIsOneSemanticTransaction()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/write-quit.txt",
        u"changed",
        {0, 0});
    QVERIFY(buffer != 0);

    for (const std::u16string &commandLine : {
             std::u16string(u":wq"),
             std::u16string(u":x"),
             std::u16string(u":xit")}) {
        const DispatchResult result = core.submitCommandLine(
            window, CommandLineKind::Ex, commandLine);
        const auto commands = std::ranges::count_if(
            result.events,
            [](const Event &event) {
                return event.type == EventType::CommandRequested;
            });
        QCOMPARE(commands, std::ptrdiff_t{1});
        const auto requested = std::ranges::find_if(
            result.events,
            [](const Event &event) {
                return event.type == EventType::CommandRequested;
            });
        QVERIFY(requested != result.events.cend());
        QCOMPARE(
            requested->commandId,
            std::string("vkery.editor.write-quit"));
        QCOMPARE(requested->view, window);
        QCOMPARE(requested->buffer, buffer);
    }

    const std::array dispatchCases{
        std::pair{u":q", "vkery.window.quit"},
        std::pair{u":quit!", "vkery.window.quit"},
        std::pair{u":qa", "vkery.application.close"},
        std::pair{u":qall!", "vkery.application.close"},
        std::pair{u":new", "vkery.editor.new-split"},
        std::pair{u":enew", "vkery.editor.new"},
        std::pair{u":sp", "vkery.window.split-horizontal"},
        std::pair{u":vs", "vkery.window.split-vertical"},
    };
    for (const auto &[commandLine, expected] : dispatchCases) {
        const DispatchResult dispatched = core.submitCommandLine(
            window,
            CommandLineKind::Ex,
            std::u16string(commandLine));
        const auto requested = std::ranges::find_if(
            dispatched.events,
            [](const Event &event) {
                return event.type == EventType::CommandRequested;
            });
        QVERIFY(requested != dispatched.events.cend());
        QCOMPARE(requested->commandId, std::string(expected));
        QCOMPARE(requested->view, window);
        QCOMPARE(requested->buffer, buffer);
        QCOMPARE(
            requested->bang,
            std::u16string_view(commandLine).ends_with(u'!'));
    }

    const DispatchResult splitFile = core.submitCommandLine(
        window,
        CommandLineKind::Ex,
        u":sp Chapter\\ One.txt");
    const auto splitRequest = std::ranges::find_if(
        splitFile.events,
        [](const Event &event) {
            return event.type == EventType::CommandRequested;
        });
    QVERIFY(splitRequest != splitFile.events.cend());
    QCOMPARE(
        splitRequest->commandArguments,
        std::vector<std::string>({"Chapter One.txt"}));
    QCOMPARE(
        splitRequest->rawArguments,
        std::string("Chapter\\ One.txt"));
}

void VkEditingBehaviorTests::
    transientPromptKeepsVkAtTheInputBoundary()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Navigation);
    const InputTargetId target = window;

    const auto first = core.beginPromptInput(
        window, target, u"draft");
    QVERIFY(first.has_value());
    (void)core.transitionInputContext(
        window, target, false);

    QKeyEvent literal{
        QEvent::KeyPress,
        Qt::Key_X,
        Qt::NoModifier,
        QStringLiteral("x")};
    QVERIFY(!core.shouldCapture(window, literal));
    QCOMPARE(
        core.dispatch(window, target, literal).disposition,
        InputDisposition::PassThrough);
    QVERIFY(core.updatePromptInput(*first, u"draft-x"));

    const DispatchResult submitted = press(
        core, window, Qt::Key_Return);
    QCOMPARE(
        submitted.disposition,
        InputDisposition::Consumed);
    const auto submit = std::ranges::find_if(
        submitted.events,
        [](const Event &event) {
            return event.type
                == EventType::PromptSubmitted;
        });
    QVERIFY(submit != submitted.events.cend());
    QCOMPARE(submit->promptSession, *first);
    QCOMPARE(submit->promptText, std::u16string(u"draft-x"));
    QVERIFY(!core.activePromptInput().has_value());

    const auto second = core.beginPromptInput(
        window, target, u"rename");
    QVERIFY(second.has_value());
    const DispatchResult cancelled = press(
        core, window, Qt::Key_Escape);
    QVERIFY(std::ranges::any_of(
        cancelled.events,
        [](const Event &event) {
            return event.type
                == EventType::PromptCancelled;
        }));
}

void VkEditingBehaviorTests::
    numberAddSubtractUsesNativeUndoAndDotRecipes()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/numbers.txt",
        u"1 5\n007 0xff 0b101 -9",
        {0, 0});
    QVERIFY(buffer != 0);

    command(core, window, QStringLiteral("3"));
    (void)press(
        core,
        window,
        Qt::Key_A,
        controlModifier(),
        QStringLiteral("a"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"4 5\n007 0xff 0b101 -9"));

    QVERIFY(core.setViewCursor(window, {0, 2}));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"4 8\n007 0xff 0b101 -9"));

    // Each command is one Core undo transaction; dot keeps the original
    // count unless the dot command supplies an explicit replacement count.
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"4 5\n007 0xff 0b101 -9"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"1 5\n007 0xff 0b101 -9"));

    QVERIFY(core.setViewCursor(window, {1, 0}));
    command(core, window, QStringLiteral("2"));
    (void)press(
        core,
        window,
        Qt::Key_A,
        controlModifier(),
        QStringLiteral("a"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"1 5\n009 0xff 0b101 -9"));

    QVERIFY(core.setViewCursor(window, {1, 4}));
    (void)press(
        core,
        window,
        Qt::Key_A,
        controlModifier(),
        QStringLiteral("a"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"1 5\n009 0x100 0b101 -9"));

    QVERIFY(core.setViewCursor(window, {1, 10}));
    (void)press(
        core,
        window,
        Qt::Key_X,
        controlModifier(),
        QStringLiteral("x"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"1 5\n009 0x100 0b100 -9"));

    QVERIFY(core.setViewCursor(window, {1, 16}));
    command(core, window, QStringLiteral("2"));
    (void)press(
        core,
        window,
        Qt::Key_A,
        controlModifier(),
        QStringLiteral("a"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"1 5\n009 0x100 0b100 -7"));

    VkCore casingCore;
    const WindowId casingWindow =
        casingCore.registerWindow(WindowKind::Editor);
    const BufferId casingBuffer = casingCore.synchronizeBuffer(
        casingWindow,
        "/vault/number-case.txt",
        u"0x0A 0X0a",
        {0, 2});
    (void)press(
        casingCore,
        casingWindow,
        Qt::Key_A,
        controlModifier(),
        QStringLiteral("a"));
    QVERIFY(casingCore.setViewCursor(casingWindow, {0, 7}));
    (void)press(
        casingCore,
        casingWindow,
        Qt::Key_A,
        controlModifier(),
        QStringLiteral("a"));
    QCOMPARE(
        bufferText(casingCore, casingBuffer),
        std::u16string(u"0x0B 0X0b"));
}

void VkEditingBehaviorTests::
    insertKeywordCompletionCyclesAuthoritativeBuffer()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/completion.txt",
        u"alpha alps\nal alpine\nal",
        {2, 1});
    QVERIFY(buffer != 0);

    command(core, window, QStringLiteral("a"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
    QKeyEvent next{
        QEvent::KeyPress,
        Qt::Key_N,
        controlModifier(),
        QStringLiteral("n")};
    QVERIFY(core.shouldCapture(window, next));
    auto result = core.dispatch(window, window, next);
    QCOMPARE(result.disposition, InputDisposition::Consumed);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha alps\nal alpine\nalpha"));

    result = core.dispatch(window, window, next);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha alps\nal alpine\nalps"));
    QKeyEvent previous{
        QEvent::KeyPress,
        Qt::Key_P,
        controlModifier(),
        QStringLiteral("p")};
    result = core.dispatch(window, window, previous);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha alps\nal alpine\nalpha"));

    escape(core, window);
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha alps\nal alpine\nal"));

    // Completion text is part of the Insert recipe. Dot repeats only the
    // suffix at another identical prefix, without duplicating the prefix.
    command(core, window, QStringLiteral("2gg"));
    QVERIFY(core.setViewCursor(window, {1, 1}));
    command(core, window, QStringLiteral("."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha alps\nalpha alpine\nal"));

    VkCore unicodeCore;
    const WindowId unicodeWindow =
        unicodeCore.registerWindow(WindowKind::Editor);
    const BufferId unicodeBuffer = unicodeCore.synchronizeBuffer(
        unicodeWindow,
        "/vault/unicode-completion.txt",
        u"\u82f9\u679c\u6811 \u82f9\u679c\u6c41\n\u82f9\u679c",
        {1, 1});
    command(unicodeCore, unicodeWindow, QStringLiteral("a"));
    (void)press(
        unicodeCore,
        unicodeWindow,
        Qt::Key_P,
        controlModifier(),
        QStringLiteral("p"));
    QCOMPARE(
        bufferText(unicodeCore, unicodeBuffer),
        std::u16string(
            u"\u82f9\u679c\u6811 \u82f9\u679c\u6c41\n\u82f9\u679c\u6c41"));
}

void VkEditingBehaviorTests::
    controlGPublishesStructuredBufferStatus()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/status.txt",
        u"zero\none\ntwo\nthree",
        {1, 2});
    const DispatchResult result = press(
        core,
        window,
        Qt::Key_G,
        controlModifier(),
        QStringLiteral("g"));
    const auto status = std::ranges::find_if(
        result.events,
        [](const Event &event) {
            return event.type == EventType::StatusMessage;
        });
    QVERIFY(status != result.events.cend());
    QCOMPARE(status->buffer, buffer);
    QCOMPARE(status->cursor, (Cursor{1, 2}));
    QCOMPARE(
        status->message,
        std::string(
            "\"/vault/status.txt\" line 2 of 4 --50%-- col 3"));
}

void VkEditingBehaviorTests::
    backwardEndsSentencesParagraphsAndTransformOperators()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/remaining-motions.txt",
        u"aa bb.cc dd\n\nSecond.  Third!\ncontinued\n\nLast",
        {0, 6});

    command(core, window, QStringLiteral("ge"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 5}));
    command(core, window, QStringLiteral("gE"));
    QCOMPARE(core.window(window)->cursor, (Cursor{0, 1}));

    QVERIFY(core.setViewCursor(window, {2, 8}));
    command(core, window, QStringLiteral(")"));
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 9}));
    command(core, window, QStringLiteral("2)"));
    QCOMPARE(core.window(window)->cursor, (Cursor{4, 0}));
    command(core, window, QStringLiteral("2("));
    QCOMPARE(core.window(window)->cursor, (Cursor{2, 9}));

    QVERIFY(core.setViewCursor(window, {0, 3}));
    command(core, window, QStringLiteral("}"));
    QCOMPARE(core.window(window)->cursor, (Cursor{1, 0}));
    command(core, window, QStringLiteral("}"));
    QCOMPARE(core.window(window)->cursor, (Cursor{4, 0}));
    command(core, window, QStringLiteral("{"));
    QCOMPARE(core.window(window)->cursor, (Cursor{1, 0}));

    QVERIFY(core.replaceBufferText(
        buffer, u"one two three", {0, 5}));
    command(core, window, QStringLiteral("dge"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ono three"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one two three"));

    QVERIFY(core.replaceBufferText(
        buffer, u"lower UPPER", {0, 0}));
    command(core, window, QStringLiteral("guw"));
    command(core, window, QStringLiteral("w."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"lower upper"));

    QVERIFY(core.replaceBufferText(
        buffer, u"AbC Stra\u00dfe\n  Mixed Case\nlast", {0, 0}));
    (void)pressCharacter(core, window, QChar(u'g'));
    const DispatchResult lowerOperator =
        pressCharacter(core, window, QChar(u'u'));
    QVERIFY(lowerOperator.inputHintGeneration.has_value());
    const auto lowerHints = core.pendingInputHint(
        *lowerOperator.inputHintGeneration);
    QVERIFY(lowerHints.has_value());
    QCOMPARE(lowerHints->prefixNotation, std::u32string(U"gu"));
    QVERIFY(std::ranges::any_of(
        lowerHints->candidates,
        [](const InputHintCandidate &candidate) {
            return candidate.keyNotation == U"u"
                && candidate.sequenceNotation == U"guu";
        }));
    (void)pressCharacter(core, window, QChar(u'w'));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abc Stra\u00dfe\n  Mixed Case\nlast"));
    command(core, window, QStringLiteral("wguw"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abc stra\u00dfe\n  Mixed Case\nlast"));
    command(core, window, QStringLiteral("gUU"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ABC STRASSE\n  Mixed Case\nlast"));
    command(core, window, QStringLiteral("jguu"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ABC STRASSE\n  mixed case\nlast"));

    QVERIFY(core.setBufferTabStop(buffer, 2));
    QVERIFY(core.setViewCursor(window, {0, 0}));
    command(core, window, QStringLiteral(">>"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"  ABC STRASSE\n  mixed case\nlast"));
    command(core, window, QStringLiteral("j."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"  ABC STRASSE\n    mixed case\nlast"));
    command(core, window, QStringLiteral("<j"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"  ABC STRASSE\n  mixed case\nlast"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"  ABC STRASSE\n    mixed case\nlast"));

    const DispatchResult detail = press(
        core,
        window,
        Qt::Key_G,
        controlModifier(),
        QStringLiteral("g"));
    // Ctrl-G alone remains the concise status; g<C-g> is asserted below.
    QVERIFY(std::ranges::any_of(
        detail.events,
        [](const Event &event) {
            return event.type == EventType::StatusMessage;
        }));
    command(core, window, QStringLiteral("g"));
    const DispatchResult detailed = press(
        core,
        window,
        Qt::Key_G,
        controlModifier(),
        QStringLiteral("g"));
    const auto status = std::ranges::find_if(
        detailed.events,
        [](const Event &event) {
            return event.type == EventType::StatusMessage;
        });
    QVERIFY(status != detailed.events.cend());
    QVERIFY(status->message.find("Word ") != std::string::npos);
    QVERIFY(status->message.find("Char ") != std::string::npos);
    QVERIFY(status->message.find("Byte ") != std::string::npos);
}

void VkEditingBehaviorTests::
    insertControlWordLineAndRegisterCommandsAreCoreOwned()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/insert-control.txt",
        u"alpha beta\ntail",
        {0, 0});

    command(core, window, QStringLiteral("\"ayiw"));
    QVERIFY(core.setViewCursor(window, {1, 3}));
    command(core, window, QStringLiteral("A"));
    insertText(core, window, u" ");
    const auto controlR = press(
        core,
        window,
        Qt::Key_R,
        controlModifier(),
        QStringLiteral("r"));
    QCOMPARE(controlR.disposition, InputDisposition::Pending);
    QVERIFY(controlR.inputHintGeneration.has_value());
    const auto registerHints = core.pendingInputHint(
        *controlR.inputHintGeneration);
    QVERIFY(registerHints.has_value());
    QCOMPARE(
        registerHints->prefixNotation,
        std::u32string(U"<C-r>"));
    const auto registerInsert =
        pressCharacter(core, window, QChar(u'a'));
    QCOMPARE(
        registerInsert.disposition,
        InputDisposition::Consumed);
    QVERIFY(std::ranges::any_of(
        registerInsert.events,
        [](const Event &event) {
            return event.type == EventType::BufferEdited;
        }));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha beta\ntail alpha"));

    (void)press(
        core,
        window,
        Qt::Key_W,
        controlModifier(),
        QStringLiteral("w"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha beta\ntail "));
    insertText(core, window, u" extra");
    (void)press(
        core,
        window,
        Qt::Key_U,
        controlModifier(),
        QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha beta\n"));

    escape(core, window);
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
    const auto undo = pressCharacter(core, window, QChar(u'u'));
    QVERIFY(std::ranges::any_of(
        undo.events,
        [](const Event &event) {
            return event.type == EventType::BufferEdited;
        }));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"alpha beta\ntail"));
}

void VkEditingBehaviorTests::
    replaceModeRestoresUndoSegmentsAndDotRecipes()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/replace-mode.txt",
        u"abcdefghij\nABCDEFGHIJ",
        {0, 1});

    command(core, window, QStringLiteral("3Rxy"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Replace));
    escape(core, window);
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"axyxyxyhij\nABCDEFGHIJ"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 6}));

    QVERIFY(core.setViewCursor(window, {1, 0}));
    command(core, window, QStringLiteral("2."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"axyxyxyhij\nxyxyEFGHIJ"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"axyxyxyhij\nABCDEFGHIJ"));

    QVERIFY(core.replaceBufferText(
        buffer, u"abcdef", {0, 2}));
    command(core, window, QStringLiteral("RXY"));
    (void)press(core, window, Qt::Key_Backspace);
    command(core, window, QStringLiteral("z"));
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abXzef"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abcdef"));

    QVERIFY(core.setViewCursor(window, {0, 2}));
    command(core, window, QStringLiteral("RXY"));
    (void)press(core, window, Qt::Key_Left);
    command(core, window, QStringLiteral("Z"));
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abXZef"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abXYef"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abcdef"));

    QVERIFY(core.replaceBufferText(
        buffer, u"abcDEFghi", {0, 3}));
    command(core, window, QStringLiteral("R12"));
    (void)press(core, window, Qt::Key_Return);
    command(core, window, QStringLiteral("xy"));
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"abc12\nxyhi"));
}

void VkEditingBehaviorTests::
    insertControlOConsumesOneCompleteNormalCommand()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/insert-control-o.txt",
        u"one two three four five",
        {0, 0});

    command(core, window, QStringLiteral("i"));
    insertText(core, window, u"X");
    const DispatchResult controlO = press(
        core,
        window,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    QCOMPARE(controlO.disposition, InputDisposition::Pending);
    QVERIFY(controlO.inputHintGeneration.has_value());
    const auto hint = core.pendingInputHint(
        *controlO.inputHintGeneration);
    QVERIFY(hint.has_value());
    QCOMPARE(hint->prefixNotation, std::u32string(U"<C-o>"));
    command(core, window, QStringLiteral("2d2w"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
    insertText(core, window, u"Q");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"XQfive"));

    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"Xfive"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"Xone two three four five"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"one two three four five"));

    QVERIFY(core.replaceBufferText(buffer, u"abc", {0, 0}));
    command(core, window, QStringLiteral("i"));
    insertText(core, window, u"X");
    (void)press(
        core,
        window,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    command(core, window, QStringLiteral("d"));
    escape(core, window);
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
    insertText(core, window, u"Q");
    escape(core, window);
    QCOMPARE(bufferText(core, buffer), std::u16string(u"XQabc"));

    QVERIFY(core.replaceBufferText(buffer, u"abcdef", {0, 0}));
    command(core, window, QStringLiteral("i"));
    insertText(core, window, u"X");
    (void)press(
        core,
        window,
        Qt::Key_O,
        controlModifier(),
        QStringLiteral("o"));
    command(core, window, QStringLiteral("vllU"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Insert));
    insertText(core, window, u"Q");
    escape(core, window);
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"XQABCdef"));
}

void VkEditingBehaviorTests::
    visualShiftAndCaseTransformsPreserveSelectionGeometry()
{
    VkCore core;
    const WindowId window =
        core.registerWindow(WindowKind::Editor);
    const BufferId buffer = core.synchronizeBuffer(
        window,
        "/vault/visual-transforms.txt",
        u"abc def ghi",
        {0, 0});

    command(core, window, QStringLiteral("v2lU"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ABC def ghi"));
    QCOMPARE(core.mode(), std::optional<Mode>(Mode::Normal));
    command(core, window, QStringLiteral("w."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ABC DEF ghi"));
    command(core, window, QStringLiteral("u"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"ABC def ghi"));

    QVERIFY(core.replaceBufferText(
        buffer, u"abc\ndef\nghi", {0, 0}));
    QVERIFY(core.setBufferTabStop(buffer, 2));
    command(core, window, QStringLiteral("Vj3>"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"      abc\n      def\nghi"));
    command(core, window, QStringLiteral("2j."));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(u"      abc\n      def\n      ghi"));

    QVERIFY(core.replaceBufferText(
        buffer,
        u"abcdef\nghijkl\nmnopqr",
        {0, 2}));
    (void)press(
        core,
        window,
        Qt::Key_V,
        controlModifier(),
        QStringLiteral("v"));
    command(core, window, QStringLiteral("2jl>"));
    QCOMPARE(
        bufferText(core, buffer),
        std::u16string(
            u"ab  cdef\ngh  ijkl\nmn  opqr"));
    QCOMPARE(
        core.window(window)->cursor,
        (Cursor{0, 2}));
}

QTEST_GUILESS_MAIN(VkEditingBehaviorTests)

#include "tst_interaction_editing.moc"

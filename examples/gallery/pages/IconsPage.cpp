// SPDX-License-Identifier: MIT

#include "IconsPage.h"
#include "VkChosenSymbols_p.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VTextStyle.h>

namespace {

QString normalizedSearchText(QString text) {
    text.replace(u'-', u' ');
    text.replace(u'_', u' ');
    return text.simplified().toCaseFolded();
}

QString iconsPageText(const char* sourceText) {
    return QCoreApplication::translate("IconsPage", sourceText);
}

class ChosenIconsView final : public QWidget {
  public:
    explicit ChosenIconsView(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);

        auto* explanation = new QLabel(
            iconsPageText(
                QT_TRANSLATE_NOOP(
                    "IconsPage",
                    "Standalone 24×24 SVGs promoted from the persisted icon-chosen selection.")),
            this);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);

        auto* search = new QLineEdit(this);
        search->setObjectName(QStringLiteral("chosenIconSearch"));
        search->setClearButtonEnabled(true);
        search->setPlaceholderText(
            iconsPageText(QT_TRANSLATE_NOOP("IconsPage", "Search chosen icons by name")));
        layout->addWidget(search);

        gridHost_ = new QWidget(this);
        grid_ = new QGridLayout(gridHost_);
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setHorizontalSpacing(10);
        grid_->setVerticalSpacing(10);
        for (int column = 0; column < kColumns; ++column) {
            grid_->setColumnStretch(column, 1);
        }
        layout->addWidget(gridHost_);

        empty_ = new QLabel(
            iconsPageText(QT_TRANSLATE_NOOP("IconsPage", "No chosen icons match the search.")),
            gridHost_);
        empty_->setAlignment(Qt::AlignCenter);

        cards_.reserve(gallery::detail::kVkChosenSymbols.size());
        for (const gallery::detail::VkChosenSymbolDescriptor& descriptor :
             gallery::detail::kVkChosenSymbols) {
            auto* button = new QToolButton(gridHost_);
            const QString sourceName = QString::fromLatin1(descriptor.sourceName);
            const QString label = iconsPageText(descriptor.label);
            button->setObjectName(QStringLiteral("chosenIconCard"));
            button->setProperty("sourceName", sourceName);
            button->setIcon(vkui::icon(descriptor.symbol));
            button->setIconSize(QSize(28, 28));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setText(QStringLiteral("%1\n%2").arg(label, sourceName));
            button->setToolTip(sourceName);
            button->setAccessibleName(QStringLiteral("%1, %2").arg(label, sourceName));
            button->setMinimumSize(132, 80);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            const QString searchable =
                normalizedSearchText(QStringLiteral("%1 %2").arg(label, sourceName));
            cards_.append({button, searchable});
        }

        connect(search, &QLineEdit::textChanged, this,
                [this](const QString& text) { applyFilter(text); });
        applyFilter({});
    }

  private:
    struct Card final {
        QToolButton* button = nullptr;
        QString searchable;
    };

    void applyFilter(const QString& text) {
        while (QLayoutItem* item = grid_->takeAt(0)) {
            delete item;
        }

        const QStringList terms =
            normalizedSearchText(text).split(u' ', Qt::SkipEmptyParts);
        int visibleCount = 0;
        for (const Card& card : std::as_const(cards_)) {
            const bool matches = std::all_of(terms.cbegin(), terms.cend(), [&card](const auto& term) {
                return card.searchable.contains(term);
            });
            card.button->setVisible(matches);
            if (!matches) {
                continue;
            }
            grid_->addWidget(card.button, visibleCount / kColumns, visibleCount % kColumns);
            ++visibleCount;
        }

        empty_->setVisible(visibleCount == 0);
        if (visibleCount == 0) {
            grid_->addWidget(empty_, 0, 0, 1, kColumns);
        }
    }

    static constexpr int kColumns = 4;
    QWidget* gridHost_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QLabel* empty_ = nullptr;
    QList<Card> cards_;
};

} // namespace

IconsPage::IconsPage(QWidget* parent) : QWidget(parent) {
    auto* canvas = this;
    auto* layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Theme-aware SVG Icons"), canvas);
    vkui::setTextStyle(*title, vkui::VTextStyle::Title);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("Each original SVG contains semantic primary and optional secondary channels. Colors "
           "are "
           "resolved at render time and cached by role, state, size, DPR, and theme generation."),
        canvas);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* symbolsGroup = new QGroupBox(tr("Symbol set"), canvas);
    auto* grid = new QGridLayout(symbolsGroup);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    struct SymbolEntry {
        vkui::VkSymbol symbol;
        const char* name;
    };
    const QList<SymbolEntry> symbols{
        {vkui::VkSymbol::ChevronLeft, QT_TR_NOOP("Chevron left")},
        {vkui::VkSymbol::ChevronRight, QT_TR_NOOP("Chevron right")},
        {vkui::VkSymbol::ChevronUp, QT_TR_NOOP("Chevron up")},
        {vkui::VkSymbol::ChevronDown, QT_TR_NOOP("Chevron down")},
        {vkui::VkSymbol::Plus, QT_TR_NOOP("Plus")},
        {vkui::VkSymbol::Minus, QT_TR_NOOP("Minus")},
        {vkui::VkSymbol::Close, QT_TR_NOOP("Close")},
        {vkui::VkSymbol::Checkmark, QT_TR_NOOP("Checkmark")},
        {vkui::VkSymbol::Information, QT_TR_NOOP("Information")},
        {vkui::VkSymbol::Warning, QT_TR_NOOP("Warning")},
        {vkui::VkSymbol::Settings, QT_TR_NOOP("Settings")},
        {vkui::VkSymbol::Search, QT_TR_NOOP("Search")},
        {vkui::VkSymbol::Folder, QT_TR_NOOP("Folder")},
        {vkui::VkSymbol::Document, QT_TR_NOOP("Document")},
        {vkui::VkSymbol::Share, QT_TR_NOOP("Share")},
        {vkui::VkSymbol::More, QT_TR_NOOP("More")},
        {vkui::VkSymbol::ToggleOff, QT_TR_NOOP("Toggle off")},
        {vkui::VkSymbol::ToggleOn, QT_TR_NOOP("Toggle on")},
        {vkui::VkSymbol::Power, QT_TR_NOOP("Power")},
        {vkui::VkSymbol::Sidebar, QT_TR_NOOP("Sidebar")},
        {vkui::VkSymbol::Grid, QT_TR_NOOP("Grid")},
        {vkui::VkSymbol::List, QT_TR_NOOP("List")},
        {vkui::VkSymbol::Edit, QT_TR_NOOP("Edit")},
        {vkui::VkSymbol::Trash, QT_TR_NOOP("Trash")},
        {vkui::VkSymbol::Download, QT_TR_NOOP("Download")},
        {vkui::VkSymbol::Install, QT_TR_NOOP("Install")},
        {vkui::VkSymbol::Upload, QT_TR_NOOP("Upload")},
        {vkui::VkSymbol::Lock, QT_TR_NOOP("Lock")},
        {vkui::VkSymbol::Eye, QT_TR_NOOP("Eye")},
        {vkui::VkSymbol::Save, QT_TR_NOOP("Save")},
        {vkui::VkSymbol::Reset, QT_TR_NOOP("Reset")},
        {vkui::VkSymbol::Duplicate, QT_TR_NOOP("Duplicate")},
        {vkui::VkSymbol::Templates, QT_TR_NOOP("Templates")},
        {vkui::VkSymbol::Image, QT_TR_NOOP("Image")},
        {vkui::VkSymbol::Background, QT_TR_NOOP("Background")},
        {vkui::VkSymbol::CanvasBackground, QT_TR_NOOP("Canvas background")},
        {vkui::VkSymbol::PhotoLibrary, QT_TR_NOOP("Photo library")},
        {vkui::VkSymbol::Focus, QT_TR_NOOP("Focus")},
        {vkui::VkSymbol::FocusTarget, QT_TR_NOOP("Focus target")},
        {vkui::VkSymbol::Rename, QT_TR_NOOP("Rename")},
        {vkui::VkSymbol::Projects, QT_TR_NOOP("Projects")},
        {vkui::VkSymbol::Remove, QT_TR_NOOP("Remove")},
        {vkui::VkSymbol::Reveal, QT_TR_NOOP("Reveal")},
        {vkui::VkSymbol::Clear, QT_TR_NOOP("Clear")},
        {vkui::VkSymbol::DefaultTemplate, QT_TR_NOOP("Default template")},
        {vkui::VkSymbol::InsertAbove, QT_TR_NOOP("Insert above")},
        {vkui::VkSymbol::InsertBelow, QT_TR_NOOP("Insert below")},
    };
    for (int index = 0; index < symbols.size(); ++index) {
        const SymbolEntry& entry = symbols.at(index);
        auto* button = new QToolButton(symbolsGroup);
        const QString name = tr(entry.name);
        button->setIcon(vkui::icon(entry.symbol));
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setText(name);
        button->setAccessibleName(name);
        button->setMinimumSize(104, 70);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        grid->addWidget(button, index / 4, index % 4);
    }
    layout->addWidget(symbolsGroup);

    auto* chosenGroup = new QGroupBox(tr("Chosen SVG symbols"), canvas);
    auto* chosenLayout = new QVBoxLayout(chosenGroup);
    chosenLayout->addWidget(new ChosenIconsView(chosenGroup));
    layout->addWidget(chosenGroup);

    auto* rolesGroup = new QGroupBox(tr("Semantic roles"), canvas);
    auto* roles = new QHBoxLayout(rolesGroup);
    struct RoleEntry {
        vkui::VkIconRole role;
        const char* name;
    };
    const QList<RoleEntry> roleEntries{
        {vkui::VkIconRole::Primary, QT_TR_NOOP("Primary")},
        {vkui::VkIconRole::Secondary, QT_TR_NOOP("Secondary")},
        {vkui::VkIconRole::Disabled, QT_TR_NOOP("Disabled")},
        {vkui::VkIconRole::Accent, QT_TR_NOOP("Accent")},
        {vkui::VkIconRole::Destructive, QT_TR_NOOP("Destructive")},
    };
    for (const RoleEntry& entry : roleEntries) {
        auto* button = new QToolButton(rolesGroup);
        const QString name = tr(entry.name);
        button->setIcon(vkui::icon(vkui::VkSymbol::Information, entry.role));
        button->setIconSize(QSize(24, 24));
        button->setText(name);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setAccessibleName(name);
        if (entry.role == vkui::VkIconRole::Disabled) {
            button->setEnabled(false);
        }
        roles->addWidget(button);
    }
    roles->addStretch();
    layout->addWidget(rolesGroup);

    auto* fileGroup = new QGroupBox(tr("File symbols"), canvas);
    auto* fileLayout = new QVBoxLayout(fileGroup);

    auto* fileTypes = new QHBoxLayout;
    struct FileIconEntry {
        vkui::VkSymbol symbol;
        vkui::VkIconRole role;
        const char* name;
    };
    const QList<FileIconEntry> fileEntries{
        {vkui::VkSymbol::FileFolderClosed, vkui::VkIconRole::Accent, QT_TR_NOOP("Folder")},
        {vkui::VkSymbol::FileCode, vkui::VkIconRole::Secondary, QT_TR_NOOP("Source file")},
        {vkui::VkSymbol::FilePdf, vkui::VkIconRole::Destructive, QT_TR_NOOP("PDF file")},
        {vkui::VkSymbol::FileImage, vkui::VkIconRole::Accent, QT_TR_NOOP("Image file")},
        {vkui::VkSymbol::FileArchive, vkui::VkIconRole::Secondary, QT_TR_NOOP("Archive")},
        {vkui::VkSymbol::FileBook, vkui::VkIconRole::Accent, QT_TR_NOOP("Book")},
    };
    for (const FileIconEntry& entry : fileEntries) {
        auto* button = new QToolButton(fileGroup);
        button->setIcon(vkui::icon(entry.symbol, entry.role));
        button->setIconSize(QSize(24, 24));
        button->setText(tr(entry.name));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setMinimumHeight(64);
        fileTypes->addWidget(button);
    }
    fileTypes->addStretch();
    fileLayout->addLayout(fileTypes);

    auto* colorExplanation = new QLabel(
        tr("File SVGs use the same semantic theme roles and explicit colors as every symbol."),
        fileGroup);
    colorExplanation->setWordWrap(true);
    fileLayout->addWidget(colorExplanation);
    auto* colorRow = new QHBoxLayout;
    auto* folderPreview = new QToolButton(fileGroup);
    folderPreview->setIconSize(QSize(28, 28));
    folderPreview->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    folderPreview->setText(tr("Custom folder"));
    folderPreview->setMinimumHeight(42);
    auto* chooseColor = new QPushButton(tr("Choose color…"), fileGroup);
    auto* colorValue = new QLabel(fileGroup);
    const QColor initialColor = vkui::VkThemeManager::instance()->theme().colors().accent;
    folderPreview->setProperty("folderColor", initialColor);
    auto applyFolderColor = [folderPreview, colorValue](const QColor& color) {
        folderPreview->setProperty("folderColor", color);
        folderPreview->setIcon(vkui::icon(vkui::VkSymbol::FileFolderClosed, color));
        colorValue->setText(color.name(QColor::HexArgb));
    };
    applyFolderColor(initialColor);
    auto openColorDialog = [this, folderPreview, applyFolderColor] {
        const QColor selected =
            QColorDialog::getColor(folderPreview->property("folderColor").value<QColor>(), this,
                                   tr("Choose folder color"), QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) {
            applyFolderColor(selected);
        }
    };
    connect(chooseColor, &QPushButton::clicked, this, openColorDialog);
    connect(folderPreview, &QToolButton::clicked, this, openColorDialog);
    colorRow->addWidget(folderPreview);
    colorRow->addWidget(chooseColor);
    colorRow->addWidget(colorValue);
    colorRow->addStretch();
    fileLayout->addLayout(colorRow);
    layout->addWidget(fileGroup);
    layout->addStretch();
}

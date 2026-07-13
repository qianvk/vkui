// SPDX-License-Identifier: MIT

#include "IconsPage.h"

#include "../GalleryFonts.h"

#include <QColorDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

IconsPage::IconsPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* canvas = new QWidget(scrollArea);
    auto* layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Theme-aware SVG Icons"), canvas);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
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

    auto* fontGroup = new QGroupBox(tr("Fira Code and Nerd Font"), canvas);
    auto* fontLayout = new QVBoxLayout(fontGroup);
    auto* codeSample = new QLabel(
        QStringLiteral("auto ready = file != nullptr && count >= 2;  // -> => != >="), fontGroup);
    codeSample->setFont(gallery::codeFont(codeSample->font().pointSizeF() + 1.0));
    codeSample->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fontLayout->addWidget(codeSample);

    auto* fileTypes = new QHBoxLayout;
    struct FontIconEntry {
        char32_t codePoint;
        vkui::VkIconRole role;
        const char* name;
    };
    const QList<FontIconEntry> fileEntries{
        {0xF07B, vkui::VkIconRole::Accent, QT_TR_NOOP("Folder")},
        {0xF1C9, vkui::VkIconRole::Secondary, QT_TR_NOOP("Source file")},
        {0xF1C1, vkui::VkIconRole::Destructive, QT_TR_NOOP("PDF file")},
        {0xF1C5, vkui::VkIconRole::Accent, QT_TR_NOOP("Image file")},
        {0xF1C6, vkui::VkIconRole::Secondary, QT_TR_NOOP("Archive")},
    };
    for (const FontIconEntry& entry : fileEntries) {
        auto* button = new QToolButton(fontGroup);
        button->setIcon(gallery::nerdIcon(entry.codePoint, entry.role));
        button->setIconSize(QSize(24, 24));
        button->setText(tr(entry.name));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setMinimumHeight(64);
        fileTypes->addWidget(button);
    }
    fileTypes->addStretch();
    fontLayout->addLayout(fileTypes);

    auto* colorExplanation = new QLabel(
        tr("Nerd Font glyphs can use a semantic theme role or an explicit QColor."), fontGroup);
    colorExplanation->setWordWrap(true);
    fontLayout->addWidget(colorExplanation);
    auto* colorRow = new QHBoxLayout;
    auto* folderPreview = new QToolButton(fontGroup);
    folderPreview->setIconSize(QSize(28, 28));
    folderPreview->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    folderPreview->setText(tr("Custom folder"));
    folderPreview->setMinimumHeight(42);
    auto* chooseColor = new QPushButton(tr("Choose color…"), fontGroup);
    auto* colorValue = new QLabel(fontGroup);
    const QColor initialColor = vkui::VkThemeManager::instance()->theme().colors().accent;
    folderPreview->setProperty("folderColor", initialColor);
    auto applyFolderColor = [folderPreview, colorValue](const QColor& color) {
        folderPreview->setProperty("folderColor", color);
        folderPreview->setIcon(gallery::nerdIcon(0xF07B, color));
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
    fontLayout->addLayout(colorRow);
    layout->addWidget(fontGroup);
    layout->addStretch();

    scrollArea->setWidget(canvas);
    outer->addWidget(scrollArea);
    QTimer::singleShot(0, scrollArea, [scrollArea] {
        scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->minimum());
    });
}

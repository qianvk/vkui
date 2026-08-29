// SPDX-License-Identifier: MIT

#pragma once

#include <QTranslator>
#include <QWidget>
#include <optional>
#include <vkui/widgets/panels/VPanelManager.h>
#include <vkui/window/VWindowAgent.h>

class QLabel;
class QStandardItemModel;
class QWidget;

class GalleryContentView;

namespace vkui {
class VCombobox;
class VTreeView;
class VSplitter;
} // namespace vkui

class GalleryWindow final : public QWidget {
    Q_OBJECT

  public:
    explicit GalleryWindow(QWidget* parent = nullptr);

  signals:
    void preferencesRequested();

  private:
    enum class Language {
        System,
        English,
        SimplifiedChinese,
    };

    void rebuildCentralWidget();
    void registerWindowChrome(QWidget* navigationTitleBar, QWidget* contentTitleBar,
                              const QList<QWidget*>& interactiveWidgets);
    void scheduleLanguageChange(Language language);
    void applyLanguage(Language language);
    void updateWindowTitle();

    QTranslator translator_;
    Language language_ = Language::System;
    std::optional<Language> pendingLanguage_;
    bool languageChangeScheduled_ = false;
    int currentPage_ = 0;
    int navigationWidth_ = 224;
    QWidget* central_ = nullptr;
    vkui::VSplitter* splitter_ = nullptr;
    vkui::VTreeView* navigation_ = nullptr;
    GalleryContentView* pages_ = nullptr;
    QStandardItemModel* navigationModel_ = nullptr;
    vkui::VCombobox* appearanceBox_ = nullptr;
    vkui::VCombobox* languageBox_ = nullptr;
    // Window-scoped panel state survives application-level view reconstruction.
    vkui::VPanelManager panelManager_;
    // Declared last so native teardown precedes QWidget base destruction.
    vkui::VWindowAgent windowAgent_;
};

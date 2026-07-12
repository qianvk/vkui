// SPDX-License-Identifier: MIT

#pragma once

#include <QMainWindow>
#include <QTranslator>

class QComboBox;
class QLabel;
class QListView;
class QStackedWidget;
class QStringListModel;

class GalleryWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit GalleryWindow(QWidget* parent = nullptr);

  private:
    enum class Language {
        System,
        English,
        SimplifiedChinese,
    };

    void rebuildCentralWidget();
    void applyLanguage(Language language);
    void updateWindowTitle();

    QTranslator translator_;
    Language language_ = Language::System;
    int currentPage_ = 0;
    QListView* navigation_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QStringListModel* navigationModel_ = nullptr;
    QComboBox* appearanceBox_ = nullptr;
    QComboBox* languageBox_ = nullptr;
};

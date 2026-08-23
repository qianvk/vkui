// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

class GalleryPreferencesWindow;
class GalleryWindow;

/** Owns application-scoped windows and enforces one live Preferences instance. */
class GalleryApplicationController final : public QObject {
  public:
    GalleryApplicationController();
    ~GalleryApplicationController() override;

    void showMainWindow();

  private:
    void showPreferencesWindow();

    std::unique_ptr<GalleryWindow> galleryWindow_;
    QPointer<GalleryPreferencesWindow> preferencesWindow_;
};

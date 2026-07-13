// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>
#include <QtGui/QIcon>
#include <vkui/VkUiGlobal.h>

namespace vkui {

enum class VkSymbol {
    ChevronLeft,
    ChevronRight,
    ChevronUp,
    ChevronDown,
    Plus,
    Minus,
    Close,
    Checkmark,
    Information,
    Warning,
    Settings,
    Search,
    Folder,
    Document,
    Share,
    More,
    ToggleOff,
    ToggleOn,
    Power,
    Sidebar,
    Grid,
    List,
    Edit,
    Trash,
    Download,
    Upload,
    Lock,
    Eye,
    Save,
    Reset,
    Duplicate,
    Templates,
    Image,
    Background,
    CanvasBackground,
    PhotoLibrary,
    Focus,
    FocusTarget,
    Rename,
    Projects,
    Remove,
    Reveal,
    Clear,
    DefaultTemplate,
};

enum class VkIconRole {
    Primary,
    Secondary,
    Disabled,
    Accent,
    Destructive,
};

/** Creates a scalable icon whose semantic colors follow the current theme. */
[[nodiscard]] VKUI_CORE_EXPORT QIcon icon(VkSymbol symbol, VkIconRole role = VkIconRole::Primary);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkSymbol)
Q_DECLARE_METATYPE(vkui::VkIconRole)

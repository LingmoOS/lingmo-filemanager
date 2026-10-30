// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>

#include "desktop/widgets/widgetconfig.h"
#include "ui/view.h"

namespace Lingmo::ui {
class Button;
class Label;
class Painter;
}

namespace fm {

// The widget gallery: the desktop dimmed, a panel of the widget kinds the
// desktop knows, and Done. Picking a card adds one; the gallery stays open so
// several can be added, as Edit Widgets does on macOS.
class WidgetGallery : public ui::View
{
public:
    WidgetGallery();
    ~WidgetGallery() override;

    std::function<void(WidgetKind)> onAdd;
    std::function<void()> onDone;

    void retranslate();

    bool acceptsPointer() const override { return false; }
    bool acceptsFocus() const override { return true; }

    void paint(ui::Painter& painter) override;
    void layout() override;

protected:
    bool keyPress(const ui::KeyEvent& event) override;

private:
    class PanelView;
    class PreviewCard;

    PanelView* m_panelView = nullptr;
    ui::Label* m_title = nullptr;
    ui::Label* m_subtitle = nullptr;
    ui::Button* m_done = nullptr;
    std::vector<PreviewCard*> m_cards;
};

} // namespace fm

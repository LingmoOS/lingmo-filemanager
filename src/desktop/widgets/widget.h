// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "desktop/widgets/widgetconfig.h"
#include "ui/view.h"

namespace Lingmo::ui {
class Painter;
}

namespace fm {

class WidgetLayer;

// A card on the desktop: the rounded, blurred body, the hover and editing
// chrome, moving it by hand, and the widget's own content. The layer owns the
// card and places it; the card only reports what the pointer does.
class DesktopWidget : public ui::View
{
public:
    DesktopWidget(WidgetLayer& layer, WidgetKind kind, WidgetSize size);
    ~DesktopWidget() override = default;

    WidgetKind kind() const { return m_kind; }
    WidgetSize size() const { return m_size; }
    void setSize(WidgetSize size);
    WidgetPlacement placement() const;

    bool editing() const { return m_editing; }
    void setEditing(bool editing);

    // The card's outline in window coordinates, for the window's backdrop blur.
    ui::RectF blurRect() const;

    // Text that came from a translation; called after the language changes.
    void retranslate();

    bool acceptsPointer() const override { return true; }
    ui::CursorShape cursorShape() const override;
    void paint(ui::Painter& painter) override;
    void layout() override;

    // The widget's content is clickable: a press without a move calls this.
    std::function<void()> onClicked;

protected:
    // Places and draws what the widget itself shows, inside the card.
    virtual void layoutContent() = 0;
    virtual void paintContent(ui::Painter&) {}
    virtual void retranslateContent() {}

    // The card minus its padding, in the card's coordinates.
    ui::RectF contentRect() const;
    // False when the compositor has no backdrop blur: the card is then opaque.
    bool frosted() const;

    void pointerEnter() override;
    void pointerLeave() override;
    void pointerPress(const ui::PointerEvent& event) override;
    void pointerMove(const ui::PointerEvent& event) override;
    void pointerRelease(const ui::PointerEvent& event) override;
    void hoverChanged() override;

private:
    WidgetLayer& m_layer;
    WidgetKind m_kind;
    WidgetSize m_size;
    bool m_editing = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_dragging = false;
    ui::PointF m_pressWindow;
    ui::PointF m_dragOrigin;
};

// The widget of a kind, as the desktop's settings name it.
std::unique_ptr<DesktopWidget> createDesktopWidget(WidgetLayer& layer, WidgetKind kind, WidgetSize size);

} // namespace fm

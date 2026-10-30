// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/widget.h"

#include <cmath>

#include "base/event_loop.h"
#include "desktop/widgets/calendarwidget.h"
#include "desktop/widgets/clockwidget.h"
#include "desktop/widgets/systemwidget.h"
#include "desktop/widgets/widgetlayer.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace fm {

namespace {

constexpr double kRemoveInset = 8;
constexpr double kRemoveSize = 18;
constexpr double kDragDistance = 4;

ui::PointF removeCentre(const ui::RectF& card)
{
    return {card.width - kRemoveInset - kRemoveSize / 2, kRemoveInset + kRemoveSize / 2};
}

} // namespace

DesktopWidget::DesktopWidget(WidgetLayer& layer, WidgetKind kind, WidgetSize size)
    : m_layer(layer)
    , m_kind(kind)
    , m_size(size)
{
    setTracksHover(true);
    setCursorShape(ui::CursorShape::Default);
}

void DesktopWidget::setSize(WidgetSize size)
{
    m_size = size;
    requestLayout();
    requestPaint();
}

WidgetPlacement DesktopWidget::placement() const
{
    const ui::RectF card = geometry();
    return {m_kind, m_size, card.x, card.y};
}

void DesktopWidget::setEditing(bool editing)
{
    if (m_editing == editing)
        return;
    m_editing = editing;
    requestPaint();
}

ui::RectF DesktopWidget::blurRect() const
{
    const ui::RectF card = localRect();
    const ui::PointF origin = mapToWindow(card.topLeft());
    return {origin.x, origin.y, card.width, card.height};
}

void DesktopWidget::retranslate()
{
    retranslateContent();
    requestLayout();
    requestPaint();
}

ui::RectF DesktopWidget::contentRect() const
{
    const ui::RectF card = localRect();
    const double padding = 18;
    return {padding, 16, card.width - padding * 2, card.height - 32};
}

bool DesktopWidget::frosted() const
{
    const ui::Window* window = this->window();
    return window && window->backgroundGlassAvailable();
}

ui::CursorShape DesktopWidget::cursorShape() const
{
    if (m_dragging)
        return ui::CursorShape::Grabbing;
    return m_hovered ? ui::CursorShape::Grab : ui::CursorShape::Default;
}

void DesktopWidget::layout()
{
    ui::View::layout();
    layoutContent();
}

void DesktopWidget::paint(ui::Painter& painter)
{
    const ui::Theme& theme = ui::Theme::current();
    const ui::RectF card = localRect();
    // The compositor blurs the card's silhouette behind the window; the fill is
    // a light tint over it. Without the blur the card is its own background.
    ui::Color fill;
    if (frosted()) {
        fill = theme.darkMode ? ui::Color {255, 255, 255, 26} : ui::Color {255, 255, 255, 150};
    } else {
        fill = theme.darkMode ? ui::Color {34, 34, 39, 238} : ui::Color {248, 248, 250, 238};
    }
    if (m_hovered && !m_dragging)
        fill = fill.lighter(theme.darkMode ? 1.15 : 1.02);

    painter.drawShadow(card, kWidgetRadius, 14,
                       ui::Color {0, 0, 0, uint8_t(theme.darkMode ? 90 : 45)}, {0, 3});
    const ui::Color border = m_editing ? theme.highlight : theme.text.withAlpha(m_hovered ? 0.18 : 0.09);
    painter.drawRoundedRect(card, {kWidgetRadius, 0, ui::Corners::Continuous}, fill,
                            m_editing ? 1.5 : 1.0, border);

    paintContent(painter);

    if (m_editing) {
        const ui::PointF centre = removeCentre(card);
        const ui::RectF button {centre.x - kRemoveSize / 2, centre.y - kRemoveSize / 2, kRemoveSize,
                                kRemoveSize};
        painter.fillEllipse(button, ui::Color {0, 0, 0, 145});
        painter.drawLine({centre.x - 3, centre.y - 3}, {centre.x + 3, centre.y + 3},
                         ui::Color {255, 255, 255, 235}, 1.5);
        painter.drawLine({centre.x + 3, centre.y - 3}, {centre.x - 3, centre.y + 3},
                         ui::Color {255, 255, 255, 235}, 1.5);
    }
}

void DesktopWidget::pointerEnter()
{
    m_hovered = true;
    requestPaint();
}

void DesktopWidget::pointerLeave()
{
    m_hovered = false;
    requestPaint();
}

void DesktopWidget::hoverChanged()
{
    m_hovered = isHovered();
    requestPaint();
}

void DesktopWidget::pointerPress(const ui::PointerEvent& event)
{
    if (event.button == ui::MouseButton::Right) {
        m_layer.showWidgetMenu(this, mapToWindow(event.position));
        return;
    }
    if (event.button != ui::MouseButton::Left)
        return;
    if (m_editing) {
        const ui::PointF centre = removeCentre(localRect());
        const ui::RectF button {centre.x - kRemoveSize / 2, centre.y - kRemoveSize / 2, kRemoveSize,
                                kRemoveSize};
        if (button.contains(event.position)) {
            // The card is this event's receiver: remove it once the event is
            // over, not under the pointer's own dispatch.
            m_layer.removeWidgetDeferred(this);
            return;
        }
    }
    m_pressed = true;
    m_dragging = false;
    m_pressWindow = mapToWindow(event.position);
    m_dragOrigin = geometry().topLeft();
}

void DesktopWidget::pointerMove(const ui::PointerEvent& event)
{
    if (!m_pressed)
        return;
    const ui::PointF current = mapToWindow(event.position);
    const ui::PointF delta = current - m_pressWindow;
    if (!m_dragging && (std::abs(delta.x) > kDragDistance || std::abs(delta.y) > kDragDistance))
        m_dragging = true;
    if (!m_dragging)
        return;
    m_layer.moveWidget(this, {m_dragOrigin.x + delta.x, m_dragOrigin.y + delta.y}, false);
}

void DesktopWidget::pointerRelease(const ui::PointerEvent& event)
{
    if (!m_pressed)
        return;
    const bool dragged = m_dragging;
    m_pressed = false;
    m_dragging = false;
    if (dragged) {
        m_layer.moveWidget(this, geometry().topLeft(), true);
    } else if (onClicked) {
        onClicked();
    }
    (void)event;
    requestPaint();
}

std::unique_ptr<DesktopWidget> createDesktopWidget(WidgetLayer& layer, WidgetKind kind, WidgetSize size)
{
    switch (kind) {
    case WidgetKind::Clock:
        return std::make_unique<ClockWidget>(layer, size);
    case WidgetKind::Calendar:
        return std::make_unique<CalendarWidget>(layer, size);
    case WidgetKind::System:
        return std::make_unique<SystemWidget>(layer, size);
    }
    return nullptr;
}

} // namespace fm

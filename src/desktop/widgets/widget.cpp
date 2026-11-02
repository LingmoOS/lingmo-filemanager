// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/widget.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "base/event_loop.h"
#include "desktop/widgets/calendarwidget.h"
#include "desktop/widgets/batterywidget.h"
#include "desktop/widgets/clockwidget.h"
#include "desktop/widgets/nowplayingwidget.h"
#include "desktop/widgets/noteswidget.h"
#include "desktop/widgets/weatherwidget.h"
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
constexpr double kContentPaddingX = 18;
constexpr double kContentPaddingY = 16;

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
    if (m_size == size || m_resizing)
        return;

    // No window: fall back to instant change.
    if (!window()) {
        m_size = size;
        requestLayout();
        requestPaint();
        return;
    }

    m_resizing = true;
    const ui::RectF current = geometry();
    const ui::SizeF target = widgetSizeSpec(size);

    m_sizeAnimation = std::make_unique<ui::Animation>(window());
    m_sizeAnimation->setDuration(std::chrono::milliseconds(200));
    m_sizeAnimation->setEasing(ui::Animation::Easing::OutCubic);
    m_sizeAnimation->onUpdate = [this, current, target](double t) {
        const double w = current.width + (target.width - current.width) * t;
        const double h = current.height + (target.height - current.height) * t;
        setGeometry({current.x, current.y, w, h});
    };
    m_sizeAnimation->onFinished = [this, size] {
        m_size = size;
        m_resizing = false;
        m_sizeAnimation.reset();
        requestLayout();
        if (m_layer.onChanged)
            m_layer.onChanged();
    };
    m_sizeAnimation->start();
}

void DesktopWidget::setInstanceId(std::string id)
{
    if (m_id == id)
        return;
    m_id = std::move(id);
    instanceIdChanged();
}

WidgetPlacement DesktopWidget::placement() const
{
    const ui::RectF card = geometry();
    return {m_kind, m_size, card.x, card.y, m_id};
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
    return {kContentPaddingX, kContentPaddingY, card.width - kContentPaddingX * 2,
            card.height - kContentPaddingY * 2};
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
        const int alpha = theme.darkMode ? (m_hovered || m_dragging ? 40 : 28)
                                         : (m_hovered || m_dragging ? 170 : 142);
        fill = ui::Color {255, 255, 255, uint8_t(alpha)};
    } else {
        fill = theme.darkMode ? ui::Color {34, 34, 39, 242} : ui::Color {248, 248, 250, 244};
    }
    if (m_hovered && !m_dragging)
        fill = fill.lighter(theme.darkMode ? 1.15 : 1.02);

    // A card under the pointer, and more so while dragged, sits a little
    // further above the desktop than the resting ones.
    const double shadowBlur = m_dragging ? 28 : m_hovered ? 20 : 14;
    const int shadowAlpha = theme.darkMode ? (m_dragging ? 135 : m_hovered ? 110 : 90)
                                           : (m_dragging ? 85 : m_hovered ? 65 : 45);
    painter.drawShadow(card, kWidgetRadius, shadowBlur, ui::Color {0, 0, 0, uint8_t(shadowAlpha)},
                       {0, m_dragging ? 8.0 : 3.0});
    const ui::Color border = m_editing ? theme.highlight
            : (m_hovered || m_dragging) ? theme.text.withAlpha(0.22)
                                        : theme.text.withAlpha(0.10);
    painter.drawRoundedRect(card, {kWidgetRadius, 0, ui::Corners::Continuous}, fill,
                            m_editing ? 1.5 : 1.0, border);

    if (frosted() && !m_editing) {
        // A hairline where the light catches the top of the glass.
        painter.drawHorizontalLine(card.x + kWidgetRadius, card.y + 0.5,
                                   std::max(0.0, card.width - kWidgetRadius * 2),
                                   ui::Color {255, 255, 255, uint8_t(theme.darkMode ? 18 : 72)}, 1);
    }

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
    m_layer.raiseWidget(this);
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
    case WidgetKind::Battery:
        return std::make_unique<BatteryWidget>(layer, size);
    case WidgetKind::NowPlaying:
        return std::make_unique<NowPlayingWidget>(layer, size);
    case WidgetKind::Notes:
        return std::make_unique<NotesWidget>(layer, size);
    case WidgetKind::Weather:
        return std::make_unique<WeatherWidget>(layer, size);
    }
    return nullptr;
}

} // namespace fm

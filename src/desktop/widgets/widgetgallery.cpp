// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/widgetgallery.h"

#include <algorithm>
#include <cmath>

#include "base/i18n.h"
#include "ui/button.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

using Lingmo::i18n::tr;

namespace fm {

// Freedesktop icon name for each widget kind, used in the gallery preview.
static const char* widgetIconName(WidgetKind kind)
{
    switch (kind) {
    case WidgetKind::Clock:        return "org.gnome.clocks";
    case WidgetKind::Calendar:     return "org.gnome.Calendar";
    case WidgetKind::System:       return "lingmo-system-monitor";
    case WidgetKind::Battery:      return "battery";
    case WidgetKind::NowPlaying:   return "applications-multimedia";
    case WidgetKind::Notes:        return "org.gnome.Notes";
    case WidgetKind::Weather:      return "org.gnome.Weather";
    case WidgetKind::WorldClock:   return "gworldclock";
    case WidgetKind::Countdown:    return "ktimer";
    case WidgetKind::NetworkSpeed: return "mate-netspeed-applet";
    case WidgetKind::Todo:         return "org.gnome.Todo";
    case WidgetKind::Launcher:     return "gnome-panel-launcher";
    }
    return "applications-system";
}

// ── PreviewCard / PanelView class definitions ─────────────────────────────────

class WidgetGallery::PreviewCard final : public ui::View
{
public:
    explicit PreviewCard(WidgetKind kind)
        : m_kind(kind)
    {
        setCursorShape(ui::CursorShape::Pointer);

        m_icon = addChild<ui::ThemeIcon>(widgetIconName(kind), 48);
        m_icon->setThemeSize(48);

        m_title = addChild<ui::Label>();
        m_title->setTextAlignment(ui::Label::TextAlignment::Center);
        m_title->setFontSize(12);
        m_title->setWeight(600);
        retranslate();
    }

    bool acceptsPointer() const override { return true; }

    void retranslate()
    {
        switch (m_kind) {
        case WidgetKind::Clock:        m_title->setText(tr("Clock"));       break;
        case WidgetKind::Calendar:     m_title->setText(tr("Calendar"));    break;
        case WidgetKind::System:       m_title->setText(tr("System"));      break;
        case WidgetKind::Battery:      m_title->setText(tr("Battery"));     break;
        case WidgetKind::NowPlaying:   m_title->setText(tr("Now Playing")); break;
        case WidgetKind::Notes:        m_title->setText(tr("Notes"));       break;
        case WidgetKind::Weather:      m_title->setText(tr("Weather"));     break;
        case WidgetKind::WorldClock:   m_title->setText(tr("World Clock")); break;
        case WidgetKind::Countdown:    m_title->setText(tr("Countdown"));   break;
        case WidgetKind::NetworkSpeed: m_title->setText(tr("Network"));     break;
        case WidgetKind::Todo:         m_title->setText(tr("To-Do"));       break;
        case WidgetKind::Launcher:     m_title->setText(tr("Launcher"));    break;
        }
        m_title->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.82)));
    }

    std::function<void()> onClicked;

    void paint(ui::Painter& painter) override
    {
        const ui::Theme& theme = ui::Theme::current();
        const ui::RectF preview = previewRect();
        const ui::Color fill = m_hovered
            ? theme.highlight.withAlpha(theme.darkMode ? 0.28 : 0.16)
            : theme.text.withAlpha(theme.darkMode ? 0.08 : 0.05);
        painter.drawShadow(preview, theme.mediumRadius,
                           m_hovered ? 14 : 8,
                           ui::Color{0, 0, 0, uint8_t(theme.darkMode
                               ? (m_hovered ? 80 : 45)
                               : (m_hovered ? 45 : 25))},
                           {0, 2});
        painter.fillRoundedRect(preview, theme.mediumRadius, fill);
        if (m_hovered)
            painter.drawRoundedRect(preview, {theme.mediumRadius, 0, ui::Corners::Continuous},
                                    ui::Color{0, 0, 0, 0}, 1,
                                    theme.highlight.withAlpha(0.45));
    }

    void layout() override
    {
        const ui::RectF preview = previewRect();
        const double iconSize = m_icon->sizeHint().width;
        // Icon centred in the preview area.
        m_icon->setGeometry({preview.x + (preview.width - iconSize) / 2.0,
                             preview.y + (preview.height - iconSize) / 2.0,
                             iconSize, iconSize});
        // Title below the preview card.
        m_title->setGeometry({0, geometry().height - 24, geometry().width, 20});
    }

protected:
    void pointerEnter() override
    {
        m_hovered = true;
        m_title->setColor(ui::ColorRecipe(ui::Theme::current().highlight));
        requestPaint();
    }
    void pointerLeave() override
    {
        m_hovered = false;
        m_title->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.82)));
        requestPaint();
    }
    void pointerRelease(const ui::PointerEvent& event) override
    {
        if (localRect().contains(event.position) && onClicked)
            onClicked();
    }

private:
    ui::RectF previewRect() const
    {
        return {10, 10, geometry().width - 20, geometry().height - 44};
    }

    WidgetKind m_kind;
    ui::ThemeIcon* m_icon  = nullptr;
    ui::Label*     m_title = nullptr;
    bool           m_hovered = false;
};

// The panel: the only part of the gallery that takes presses. The dim around
// it is the gallery's paint, so the widgets being edited stay reachable
// beside it.
class WidgetGallery::PanelView final : public ui::View
{
public:
    PanelView() { setName("widget-gallery-panel"); }

    bool acceptsPointer() const override { return true; }

    void paint(ui::Painter& painter) override
    {
        const ui::Theme& theme = ui::Theme::current();
        const ui::RectF panel = localRect();
        painter.drawShadow(panel, theme.hugeRadius, 30,
                           ui::Color {0, 0, 0, uint8_t(theme.darkMode ? 120 : 70)}, {0, 10});
        const ui::Color fill = theme.darkMode ? ui::Color {32, 32, 37, 242}
                                              : ui::Color {249, 249, 251, 242};
        painter.drawRoundedRect(panel, {theme.hugeRadius, 0, ui::Corners::Continuous}, fill, 1,
                                theme.text.withAlpha(0.12));
        painter.drawHorizontalLine(panel.x + theme.hugeRadius, panel.y + 0.5,
                                   std::max(0.0, panel.width - theme.hugeRadius * 2),
                                   ui::Color {255, 255, 255,
                                              uint8_t(theme.darkMode ? 16 : 90)},
                                   1);
    }
};

WidgetGallery::WidgetGallery()
{
    setName("widget-gallery");
    setVisible(false);
    setClipsChildren(false);

    m_panelView = addChild<PanelView>();
    m_title = m_panelView->addChild<ui::Label>();
    m_title->setFontSize(20);
    m_title->setWeight(700);
    m_subtitle = m_panelView->addChild<ui::Label>();
    m_subtitle->setFontSize(12);
    m_subtitle->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.6)));

    for (WidgetKind kind : {WidgetKind::Clock, WidgetKind::Calendar, WidgetKind::System,
                            WidgetKind::Battery, WidgetKind::NowPlaying, WidgetKind::Notes,
                            WidgetKind::Weather, WidgetKind::WorldClock, WidgetKind::Countdown,
                            WidgetKind::NetworkSpeed, WidgetKind::Todo, WidgetKind::Launcher}) {
        PreviewCard* card = m_panelView->addChild<PreviewCard>(kind);
        card->onClicked = [this, kind] {
            if (onAdd)
                onAdd(kind);
        };
        m_cards.push_back(card);
    }

    m_done = m_panelView->addChild<ui::Button>(tr("Done"));
    m_done->setName("widget-gallery:done");
    m_done->onClicked = [this] {
        if (onDone)
            onDone();
    };
    retranslate();
}

WidgetGallery::~WidgetGallery() = default;

void WidgetGallery::retranslate()
{
    m_title->setText(tr("Widgets"));
    m_subtitle->setText(tr("Add widgets to your desktop"));
    m_subtitle->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.6)));
    m_done->setText(tr("Done"));
    for (PreviewCard* card : m_cards)
        card->retranslate();
}

void WidgetGallery::paint(ui::Painter& painter)
{
    painter.fillRect(localRect(), ui::Color {0, 0, 0, 96});
}

void WidgetGallery::layout()
{
    const ui::RectF area = localRect();
    const int count = int(m_cards.size());
    const int columns = std::max(1, count <= 5 ? count : 3);
    const int rows = std::max(1, (count + columns - 1) / columns);
    const double width = std::min(count <= 5 ? 920.0 : 680.0, std::max(320.0, area.width - 48));
    const double height = std::min(count <= 5 ? 360.0 : 500.0,
                                   std::max(240.0, area.height - 48));
    m_panelView->setGeometry({(area.width - width) / 2, (area.height - height) / 2, width, height});

    m_title->setGeometry({24, 18, width - 48, 28});
    m_subtitle->setGeometry({24, 46, width - 48, 20});

    const double margin = 24;
    const double gap = 16;
    // Between the cards and the Done row, so neither touches the other.
    const double cardHeight = std::max(0.0, (height - 76 - 58 - gap * (rows - 1)) / rows);
    const double cardWidth = (width - margin * 2 - gap * (columns - 1)) / columns;
    for (size_t i = 0; i < m_cards.size(); ++i) {
        const int column = int(i) % columns;
        const int row = int(i) / columns;
        m_cards[i]->setGeometry({margin + column * (cardWidth + gap),
                                 76 + row * (cardHeight + gap), cardWidth, cardHeight});
    }
    const ui::SizeF doneSize = m_done->sizeHint();
    m_done->setGeometry({width - margin - std::max(76.0, doneSize.width + 24), height - 44,
                         std::max(76.0, doneSize.width + 24), 30});
}

bool WidgetGallery::keyPress(const ui::KeyEvent& event)
{
    if (event.key == ui::key::Escape) {
        if (onDone)
            onDone();
        return true;
    }
    return false;
}

} // namespace fm

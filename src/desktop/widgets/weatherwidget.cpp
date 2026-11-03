// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/weatherwidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>

#include <curl/curl.h>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/json.h"
#include "base/locale.h"
#include "base/settings.h"
#include "base/thread_pool.h"
#include "core/config.h"
#include "ui/icon.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/path.h"
#include "ui/theme.h"

using Lingmo::base::json::Value;
using Lingmo::i18n::tr;

namespace fm {

namespace {

constexpr const char* kGeocoding = "https://geocoding-api.open-meteo.com/v1/search";
constexpr const char* kForecast = "https://api.open-meteo.com/v1/forecast";
constexpr auto kRefreshInterval = std::chrono::minutes(15);
constexpr auto kCacheExpiry = std::chrono::minutes(15);
constexpr size_t kMaxCacheEntries = 10;

// Weather cache: shared across all WeatherWidget instances
struct CachedWeather
{
    WeatherData data;
    std::chrono::steady_clock::time_point fetchTime;
};

static std::map<std::string, CachedWeather> s_weatherCache;
static std::mutex s_cacheMutex;

std::optional<WeatherData> getCachedWeather(const std::string& city)
{
    std::lock_guard lock(s_cacheMutex);
    auto it = s_weatherCache.find(city);
    if (it == s_weatherCache.end())
        return std::nullopt;

    const auto age = std::chrono::steady_clock::now() - it->second.fetchTime;
    if (age > kCacheExpiry) {
        s_weatherCache.erase(it);
        return std::nullopt;
    }
    return it->second.data;
}

void setCachedWeather(const std::string& city, const WeatherData& data)
{
    std::lock_guard lock(s_cacheMutex);
    // LRU eviction: remove oldest entry if cache is full
    if (s_weatherCache.size() >= kMaxCacheEntries) {
        auto oldest = std::min_element(s_weatherCache.begin(), s_weatherCache.end(),
            [](const auto& a, const auto& b) { return a.second.fetchTime < b.second.fetchTime; });
        s_weatherCache.erase(oldest);
    }
    s_weatherCache[city] = {data, std::chrono::steady_clock::now()};
}

struct HttpResponse
{
    bool ok = false;
    std::string body;
};

size_t appendBody(char* data, size_t size, size_t count, void* user)
{
    auto* body = static_cast<std::string*>(user);
    body->append(data, size * count);
    return size * count;
}

std::optional<std::string> urlEncode(CURL* curl, const std::string& value)
{
    char* escaped = curl_easy_escape(curl, value.c_str(), int(value.size()));
    if (!escaped)
        return std::nullopt;
    std::string result = escaped;
    curl_free(escaped);
    return result;
}

std::optional<HttpResponse> get(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return std::nullopt;
    HttpResponse response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "lingmo-filemanager/5.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300)
        return std::nullopt;
    response.ok = true;
    return response;
}

bool fetchWeather(const std::string& city, WeatherData& data)
{
    CURL* escape = curl_easy_init();
    if (!escape)
        return false;
    const std::optional<std::string> encoded = urlEncode(escape, city);
    curl_easy_cleanup(escape);
    if (!encoded)
        return false;

    const std::optional<HttpResponse> search = get(
            std::string(kGeocoding) + "?name=" + *encoded + "&count=1&format=json&language=en");
    if (!search || !search->ok)
        return false;
    const std::optional<Value> searchJson = Value::parse(search->body);
    if (!searchJson || !searchJson->isObject())
        return false;
    const Value& results = (*searchJson)["results"];
    if (!results.isArray() || results.size() == 0)
        return false;
    const Value& place = results[0];
    const double latitude = place["latitude"].toDouble();
    const double longitude = place["longitude"].toDouble();
    if (!std::isfinite(latitude) || !std::isfinite(longitude))
        return false;

    std::ostringstream url;
    url << kForecast << "?latitude=" << latitude << "&longitude=" << longitude
        << "&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto";
    const std::optional<HttpResponse> forecast = get(url.str());
    if (!forecast || !forecast->ok)
        return false;
    const std::optional<Value> json = Value::parse(forecast->body);
    if (!json || !json->isObject())
        return false;
    const Value& current = (*json)["current"];
    if (!current.isObject())
        return false;

    data.valid = true;
    data.city = place["name"].toString(city);
    data.temperature = current["temperature_2m"].toDouble();
    data.humidity = int(current["relative_humidity_2m"].toDouble(-1));
    data.code = int(current["weather_code"].toDouble(-1));
    return true;
}

std::string conditionText(int code)
{
    if (code == 0)
        return tr("Clear");
    if (code == 1)
        return tr("Mostly clear");
    if (code == 2)
        return tr("Partly cloudy");
    if (code == 3)
        return tr("Cloudy");
    if (code == 45 || code == 48)
        return tr("Fog");
    if (code >= 51 && code <= 57)
        return tr("Drizzle");
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
        return tr("Rain");
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
        return tr("Snow");
    if (code >= 95)
        return tr("Thunderstorm");
    return tr("Weather");
}

bool isRain(int code)
{
    return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
}

bool isSnow(int code)
{
    return (code >= 71 && code <= 77) || code == 85 || code == 86;
}

const char* weatherIconName(int code)
{
    if (code == 0)
        return "weather-clear";
    if (code <= 2)
        return "weather-few-clouds";
    if (code == 3)
        return "weather-overcast";
    if (code == 45 || code == 48)
        return "weather-fog";
    if (code >= 51 && code <= 55)
        return "weather-showers-scattered";
    if (code == 56 || code == 57)
        return "weather-freezing-rain";
    if (code >= 61 && code <= 65)
        return "weather-showers";
    if (code == 66 || code == 67)
        return "weather-freezing-rain";
    if (code >= 71 && code <= 75)
        return "weather-snow";
    if (code == 77)
        return "weather-snow-scattered";
    if (code >= 80 && code <= 82)
        return "weather-showers";
    if (code == 85 || code == 86)
        return "weather-snow";
    if (code >= 95 && code <= 99)
        return "weather-storm";
    return "weather-none-available";
}

} // namespace

WeatherWidget::WeatherWidget(WidgetLayer& layer, WidgetSize size)
    : DesktopWidget(layer, WidgetKind::Weather, size)
{
    setName("weather-widget");
    m_city = addChild<ui::Label>();
    m_city->setFontSize(11);
    m_city->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.58)));
    m_city->setElide(ui::Label::Elide::Right);
    m_temperature = addChild<ui::Label>();
    m_temperature->setWeight(300);
    m_condition = addChild<ui::Label>();
    m_condition->setFontSize(11);
    m_condition->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.68)));
    m_condition->setElide(ui::Label::Elide::Right);
    m_humidity = addChild<ui::Label>();
    m_humidity->setFontSize(10);
    m_humidity->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.5)));

    m_weatherIcon = addChild<ui::ThemeIcon>();
    m_weatherIcon->setVisible(false);

    m_timer.setInterval(kRefreshInterval);
    m_timer.setSingleShot(false);
    m_timer.onTimeout = [this] { refresh(); };
    m_timer.start();
    refresh();
}

WeatherWidget::~WeatherWidget()
{
    *m_alive = false;
    m_timer.stop();
}

void WeatherWidget::refresh()
{
    const base::Settings settings(settingsFile());
    const std::string city = settings.stringValue("Weather/City");
    if (city.empty()) {
        m_requestedCity.clear();
        m_data = {};
        updateLabels();
        return;
    }

    // Check cache first: avoid network request if fresh data exists
    if (city == m_requestedCity) {
        const auto cached = getCachedWeather(city);
        if (cached) {
            m_data = *cached;
            m_lastFetch = std::chrono::steady_clock::now();
            updateLabels();
            return;
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const bool stale = !m_data.valid || m_lastFetch.time_since_epoch().count() == 0
            || now - m_lastFetch >= kRefreshInterval;
    if (city != m_requestedCity || (stale && !m_fetching)) {
        m_requestedCity = city;
        if (!m_fetching)
            fetch(city);
        else
            updateLabels();
        return;
    }
    updateLabels();
}

void WeatherWidget::fetch(const std::string& city)
{
    base::EventLoop* loop = base::EventLoop::current();
    if (!loop)
        return;
    m_fetching = true;
    updateLabels();
    base::ThreadPool::global().post([this, alive = m_alive, loop, city] {
        WeatherData result;
        if (!fetchWeather(city, result))
            result.error = "unavailable";
        loop->post([this, alive, result, city] {
            if (!*alive)
                return;
            m_fetching = false;
            m_lastFetch = std::chrono::steady_clock::now();
            if (city != m_requestedCity) {
                // The city changed while this was in flight; the next refresh
                // fetches the new one.
                refresh();
                return;
            }
            if (result.valid)
                apply(result);
            else {
                m_data = {};
                m_data.error = result.error;
                updateLabels();
            }
        });
    });
}

void WeatherWidget::apply(const WeatherData& data)
{
    m_data = data;
    setCachedWeather(m_requestedCity, data);
    updateLabels();
}

void WeatherWidget::updateLabels()
{
    const bool valid = m_data.valid;
    m_city->setText(m_requestedCity.empty() ? tr("Weather") : m_requestedCity);
    m_temperature->setText(valid ? base::Locale::system().toString(std::lround(m_data.temperature), 0) + "°"
                                : "--");
    if (valid)
        m_condition->setText(conditionText(m_data.code));
    else if (m_requestedCity.empty())
        m_condition->setText(tr("Set a city in Settings"));
    else if (m_fetching)
        m_condition->setText(tr("Updating…"));
    else
        m_condition->setText(tr("Weather unavailable"));
    m_humidity->setText(valid && m_data.humidity >= 0
                                ? std::string(tr("Humidity")) + " "
                                        + base::Locale::system().toString(m_data.humidity, 0) + "%"
                                : std::string());

    // Load weather icon via ThemeIcon (self-drawing View)
    if (m_weatherIcon) {
        if (valid) {
            const double iconSize = size() == WidgetSize::Small ? 36
                                   : size() == WidgetSize::Large ? 64 : 48;
            m_weatherIcon->setIconName(weatherIconName(m_data.code));
            m_weatherIcon->setSize(iconSize);
            m_weatherIcon->setVisible(size() != WidgetSize::Small);
        } else {
            m_weatherIcon->setVisible(false);
        }
    }

    requestLayout();
    requestPaint();
}

void WeatherWidget::layoutContent()
{
    const ui::RectF content = contentRect();
    if (content.width <= 0 || content.height <= 0)
        return;
    const bool small = size() == WidgetSize::Small;
    const bool large = size() == WidgetSize::Large;
    const double cityHeight = std::max(14.0, m_city->sizeHint().height);
    const double conditionHeight = std::max(14.0, m_condition->sizeHint().height);
    const double humidityHeight = std::max(12.0, m_humidity->sizeHint().height);
    m_city->setGeometry({content.x, content.y, content.width, cityHeight});
    const double textWidth = small ? content.width : std::max(0.0, content.width - 100);
    m_temperature->setFontSize(small ? 34 : large ? 54 : 44);
    m_temperature->setGeometry({content.x, content.y + cityHeight + 2, textWidth,
                                std::max(0.0, content.height - cityHeight - conditionHeight - humidityHeight - 8)});
    m_condition->setGeometry({content.x, content.bottom() - conditionHeight - humidityHeight - 2,
                              textWidth, conditionHeight});
    m_humidity->setGeometry({content.x, content.bottom() - humidityHeight, textWidth, humidityHeight});

    // Position the weather icon at the top-right of the content area
    if (m_weatherIcon && m_weatherIcon->isVisible()) {
        const double iconSize = m_weatherIcon->sizeHint().width;
        m_weatherIcon->setGeometry({content.right() - iconSize - 4, content.y + 4, iconSize, iconSize});
    }
}

void WeatherWidget::paintContent(ui::Painter& painter)
{
    if (size() == WidgetSize::Small)
        return;

    // ThemeIcon draws itself; only draw the hand-drawn fallback when the
    // system icon is not showing (icon name not found in the theme).
    if (m_weatherIcon && m_weatherIcon->isVisible()) {
        const std::string iconPath = m_weatherIcon->path();
        if (!iconPath.empty())
            return;  // system icon found — ThemeIcon already drew it
    }

    // Fallback: hand-drawn weather graphics
    const ui::Theme& theme = ui::Theme::current();
    const ui::RectF content = contentRect();
    const double size = std::min(64.0, content.width * 0.24);
    const double cx = content.right() - size / 2 - 8;
    const double cy = content.y + size / 2 + 12;
    const ui::Color ink = theme.text.withAlpha(0.62);
    const ui::Color accent = theme.highlight;
    const int code = m_data.valid ? m_data.code : 0;

    if (m_data.valid && code <= 2) {
        const double radius = size * 0.22;
        painter.fillEllipse({cx - radius, cy - radius, radius * 2, radius * 2}, accent);
        for (int i = 0; i < 8; ++i) {
            const double angle = i * std::numbers::pi / 4;
            painter.drawLine({cx + std::cos(angle) * radius * 1.4, cy + std::sin(angle) * radius * 1.4},
                             {cx + std::cos(angle) * radius * 1.9, cy + std::sin(angle) * radius * 1.9},
                             ink, 1.2);
        }
        if (code == 2) {
            painter.fillRoundedRect({cx - size * 0.16, cy + size * 0.13, size * 0.42, size * 0.22},
                                    size * 0.11, theme.text.withAlpha(0.24));
        }
        return;
    }
    if (m_data.valid || !m_requestedCity.empty()) {
        painter.fillRoundedRect({cx - size * 0.26, cy + size * 0.05, size * 0.52, size * 0.22},
                                size * 0.11, theme.text.withAlpha(0.24));
        painter.fillRoundedRect({cx - size * 0.02, cy - size * 0.10, size * 0.30, size * 0.22},
                                size * 0.10, theme.text.withAlpha(0.24));
        if (m_data.valid && isRain(code)) {
            for (int i = 0; i < 3; ++i)
                painter.drawLine({cx - size * 0.18 + i * size * 0.16, cy + size * 0.32},
                                 {cx - size * 0.24 + i * size * 0.16, cy + size * 0.46}, accent, 1.5);
        } else if (m_data.valid && isSnow(code)) {
            for (int i = 0; i < 3; ++i)
                painter.fillEllipse({cx - size * 0.20 + i * size * 0.16, cy + size * 0.36, 2.5, 2.5},
                                    accent);
        }
    }
}

void WeatherWidget::retranslateContent()
{
    m_city->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.58)));
    m_condition->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.68)));
    m_humidity->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.5)));
    refresh();
}

} // namespace fm

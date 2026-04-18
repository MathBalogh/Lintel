#include "document.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace lintel {

UIValue operator""_px(unsigned long long v) { return UIValue::px(static_cast<float>(v)); }
UIValue operator""_pct(unsigned long long v) { return UIValue::pct(static_cast<float>(v) / 100.0f); }

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

Color::Color() noexcept
    : r(0.f), g(0.f), b(0.f), a(1.f) {}

Color::Color(float f)
    : r(f), g(f), b(f), a(1.f) {}

Color::Color(float red, float green, float blue, float alpha) noexcept
    : r(red), g(green), b(blue), a(alpha) {}

Color Color::rgb(float r, float g, float b, float a) noexcept {
    return Color(r, g, b, a);
}

Color Color::hsl(float h, float s, float l, float a) noexcept {
    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;

    s = std::clamp(s, 0.f, 1.f);
    l = std::clamp(l, 0.f, 1.f);

    const float c = (1.f - std::fabs(2.f * l - 1.f)) * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = l - c / 2.f;

    float r1 = 0.f, g1 = 0.f, b1 = 0.f;
    if (h < 60.f) { r1 = c; g1 = x; }
    else if (h < 120.f) { r1 = x; g1 = c; }
    else if (h < 180.f) { g1 = c; b1 = x; }
    else if (h < 240.f) { g1 = x; b1 = c; }
    else if (h < 300.f) { r1 = x;          b1 = c; }
    else { r1 = c;          b1 = x; }

    return Color(r1 + m, g1 + m, b1 + m, a);
}

Color Color::hsv(float h, float s, float v, float a) noexcept {
    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;

    s = std::clamp(s, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);

    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;

    float r1 = 0.f, g1 = 0.f, b1 = 0.f;
    if (h < 60.f) { r1 = c; g1 = x; }
    else if (h < 120.f) { r1 = x; g1 = c; }
    else if (h < 180.f) { g1 = c; b1 = x; }
    else if (h < 240.f) { g1 = x; b1 = c; }
    else if (h < 300.f) { r1 = x;          b1 = c; }
    else { r1 = c;          b1 = x; }

    return Color(r1 + m, g1 + m, b1 + m, a);
}

Color Color::hex(uint32_t rgba) {
    const float r = ((rgba >> 24) & 0xFFu) / 255.f;
    const float g = ((rgba >> 16) & 0xFFu) / 255.f;
    const float b = ((rgba >> 8) & 0xFFu) / 255.f;
    const float a = ((rgba) & 0xFFu) / 255.f;
    return Color(r, g, b, a);
}

void Color::clamp() noexcept {
    r = std::clamp(r, 0.f, 1.f);
    g = std::clamp(g, 0.f, 1.f);
    b = std::clamp(b, 0.f, 1.f);
    a = std::clamp(a, 0.f, 1.f);
}

Color Color::lighten(const Color& o, float f) noexcept {
    return Color(o.r + f, o.g + f, o.b + f, o.a);
}

// ---------------------------------------------------------------------------
// Rect
// ---------------------------------------------------------------------------

Rect::Rect(float w_, float h_) noexcept
    : x(0.f), y(0.f), w(w_), h(h_) {}

Rect::Rect(float x_, float y_, float w_, float h_) noexcept
    : x(x_), y(y_), w(w_), h(h_) {}

// ---------------------------------------------------------------------------
// Edges
// ---------------------------------------------------------------------------

Edges::Edges() noexcept
    : top(0.f), right(0.f), bottom(0.f), left(0.f) {}

Edges::Edges(float all) noexcept
    : top(all), right(all), bottom(all), left(all) {}

Edges::Edges(float x_axis, float y_axis) noexcept
    : top(y_axis), right(x_axis), bottom(y_axis), left(x_axis) {}

float Edges::horizontal() const { return left + right; }
float Edges::vertical()   const { return top + bottom; }

bool Properties::has_hot_float(unsigned int rel) const {
    return (hot_floats_mask_ >> rel) & 1u;
}
bool Properties::has_hot_color(unsigned int rel) const {
    return (hot_colors_mask_ >> rel) & 1u;
}

bool Properties::is_dirty() const { return layout_dirty_; }
bool Properties::is_clean() const { return !layout_dirty_; }
void Properties::make_dirty() { layout_dirty_ = true; }
void Properties::make_clean() { layout_dirty_ = false; }

bool Properties::set(Key key, Property value) {
    if (key.is_hot_float() && value.is_float()) {
        unsigned int rel = key.hot_float_index();
        hot_floats_[rel] = value.get_float();
        hot_floats_mask_ |= 1u << rel;
    }
    else if (key.is_hot_color() && value.is_color()) {
        unsigned int rel = key.hot_color_index();
        hot_colors_[rel] = value.get_color();
        hot_colors_mask_ |= 1u << rel;
    }
    else {
        cold_[key] = value;
    }

    return key.affects_layout();
}
bool Properties::clear(Key key) {
    if (key.is_hot_float()) {
        unsigned int rel = key.hot_float_index();
        hot_floats_mask_ &= ~(1u << rel);
    }
    else if (key.is_hot_color()) {
        unsigned int rel = key.hot_color_index();
        hot_colors_mask_ &= ~(1u << rel);
    }
    else {
        cold_.erase(key);
    }

    return key.affects_layout();
}

bool Properties::has(Key key) const noexcept {
    if (key.is_hot_float()) {
        return has_hot_float(key.hot_float_index());
    }
    else if (key.is_hot_color()) {
        return has_hot_color(key.hot_color_index());
    }
    else {
        return cold_.count(key) != 0;
    }
}

bool* Properties::get_bool(Key key) {
    if (auto it = cold_.find(key); it != cold_.end() && it->second.is_bool()) return &it->second.get_bool();
    return nullptr;
}
float* Properties::get_float(Key key) {
    if (key.is_hot_float()) {
        unsigned int rel = key.hot_float_index();
        if (has_hot_float(rel)) {
            return &hot_floats_[rel];
        }
    }
    else if (auto it = cold_.find(key); it != cold_.end() && it->second.is_float()) {
        return &it->second.get_float();
    }
    return nullptr;
}
unsigned int* Properties::get_enum(Key key) {
    if (auto it = cold_.find(key); it != cold_.end() && it->second.is_enum()) return &it->second.get_enum();
    return nullptr;
}
UIValue* Properties::get_ui(Key key) {
    if (auto it = cold_.find(key); it != cold_.end() && it->second.is_ui()) return &it->second.get_ui();
    return nullptr;
}
Color* Properties::get_color(Key key) {
    if (key.is_hot_color()) {
        unsigned int rel = key.hot_color_index();
        if (has_hot_color(rel)) {
            return &hot_colors_[rel];
        }
    }
    else if (auto it = cold_.find(key); it != cold_.end() && it->second.is_color()) {
        return &it->second.get_color();
    }
    return nullptr;
}
std::wstring* Properties::get_wstring(Key key) {
    if (auto it = cold_.find(key); it != cold_.end() && it->second.is_wstring()) return &it->second.get_wstring();
    return nullptr;
}

Property Properties::get(Key key) const {
    if (key.is_hot_float()) {
        unsigned int rel = key.hot_float_index();
        if (has_hot_float(rel)) {
            return Property(hot_floats_[rel]);
        }
    }
    else if (key.is_hot_color()) {
        unsigned int rel = key.hot_color_index();
        if (has_hot_color(rel)) {
            return Property(hot_colors_[rel]);
        }
    }
    else if (auto it = cold_.find(key); it != cold_.end()) {
        return it->second;
    }
    return Property();
}

// --- Constructors ---

Property::Property(): data_{ 0 }, type_(Type::Null) {}

Property::Property(bool p) {
    new (&data_) bool(p);
    type_ = Type::Bool;
}

Property::Property(float p) {
    new (&data_) float(p);
    type_ = Type::Float;
}

Property::Property(unsigned int p) {
    new (&data_) unsigned int(p);
    type_ = Type::Enum;
}

Property::Property(const UIValue& p) {
    new (&data_) UIValue(p);
    type_ = Type::UIValue;
}

Property::Property(const Color& p) {
    new (&data_) Color(p);
    type_ = Type::Color;
}

Property::Property(const std::wstring& str) {
    new (&data_) std::wstring(str);
    type_ = Type::WString;
}

// --- Destructor ---

Property::~Property() {
    destroy();
}

// --- Copy / Move ---

Property::Property(const Property& other) {
    copy_from(other);
}

Property& Property::operator=(const Property& other) {
    if (this != &other) {
        destroy();
        copy_from(other);
    }
    return *this;
}

Property::Property(Property&& other) noexcept {
    move_from(std::move(other));
}

Property& Property::operator=(Property&& other) noexcept {
    if (this != &other) {
        destroy();
        move_from(std::move(other));
    }
    return *this;
}

// --- Internal helpers ---

void Property::destroy() {
    switch (type_) {
        case Type::UIValue:
            ptr<UIValue>()->~UIValue();
            break;
        case Type::Color:
            ptr<Color>()->~Color();
            break;
        case Type::WString:
            ptr<std::wstring>()->~basic_string();
            break;
        default:
            break;
    }
    type_ = Type::Null;
}

void Property::copy_from(const Property& other) {
    type_ = Type::Null;

    switch (other.type_) {
        case Type::Bool:
            new (&data_) bool(*other.ptr<bool>());
            type_ = Type::Bool;
            break;
        case Type::Float:
            new (&data_) float(*other.ptr<float>());
            type_ = Type::Float;
            break;
        case Type::Enum:
            new (&data_) unsigned int(*other.ptr<unsigned int>());
            type_ = Type::Enum;
            break;
        case Type::UIValue:
            new (&data_) UIValue(*other.ptr<UIValue>());
            type_ = Type::UIValue;
            break;
        case Type::Color:
            new (&data_) Color(*other.ptr<Color>());
            type_ = Type::Color;
            break;
        case Type::WString:
            new (&data_) std::wstring(*other.ptr<std::wstring>());
            type_ = Type::WString;
            break;
        default:
            type_ = Type::Null;
            break;
    }
}

void Property::move_from(Property&& other) noexcept {
    type_ = Type::Null;

    switch (other.type_) {
        case Type::Bool:
            new (&data_) bool(*other.ptr<bool>());
            type_ = Type::Bool;
            break;
        case Type::Float:
            new (&data_) float(*other.ptr<float>());
            type_ = Type::Float;
            break;
        case Type::Enum:
            new (&data_) unsigned int(*other.ptr<unsigned int>());
            type_ = Type::Enum;
            break;
        case Type::UIValue:
            new (&data_) UIValue(std::move(*other.ptr<UIValue>()));
            type_ = Type::UIValue;
            break;
        case Type::Color:
            new (&data_) Color(std::move(*other.ptr<Color>()));
            type_ = Type::Color;
            break;
        case Type::WString:
            new (&data_) std::wstring(std::move(*other.ptr<std::wstring>()));
            type_ = Type::WString;
            break;
        default:
            type_ = Type::Null;
            break;
    }

    other.destroy();
}

// --- Type checks ---

Property::Type Property::type() const noexcept { return type_; }

bool Property::is_null() const noexcept { return type_ == Type::Null; }
bool Property::is_bool() const noexcept { return type_ == Type::Bool; }
bool Property::is_float() const noexcept { return type_ == Type::Float; }
bool Property::is_enum() const noexcept { return type_ == Type::Enum; }
bool Property::is_ui() const noexcept { return type_ == Type::UIValue; }
bool Property::is_color() const noexcept { return type_ == Type::Color; }
bool Property::is_wstring() const noexcept { return type_ == Type::WString; }

// --- Getters ---

bool& Property::get_bool() {
    assert(is_bool());
    return *ptr<bool>();
}
bool Property::get_bool() const {
    assert(is_bool());
    return *ptr<bool>();
}

float& Property::get_float() {
    assert(is_float());
    return *ptr<float>();
}
float Property::get_float() const {
    assert(is_float());
    return *ptr<float>();
}

unsigned int& Property::get_enum() {
    assert(is_enum());
    return *ptr<unsigned int>();
}
unsigned int Property::get_enum() const {
    assert(is_enum());
    return *ptr<unsigned int>();
}

Color& Property::get_color() {
    assert(is_color());
    return *ptr<Color>();
}
Color Property::get_color() const {
    assert(is_color());
    return *ptr<Color>();
}

UIValue& Property::get_ui() {
    assert(is_ui());
    return *ptr<UIValue>();
}
UIValue Property::get_ui() const {
    assert(is_ui());
    return *ptr<UIValue>();
}

std::wstring& Property::get_wstring() {
    assert(is_wstring());
    return *ptr<std::wstring>();
}
const std::wstring& Property::get_wstring() const {
    assert(is_wstring());
    return *ptr<std::wstring>();
}

// --- Operators ---

bool Property::operator==(const std::wstring& str) const {
    return is_wstring() && get_wstring() == str;
}

Property::operator bool() const noexcept {
    return is_bool() ? get_bool() : false;
}

Property::operator float() const noexcept {
    return is_float() ? get_float() : 0.f;
}

Property::operator Color() const noexcept {
    return is_color() ? get_color() : Color(0.f, 0.f, 0.f, 1.f);
}

Property::operator UIValue() const noexcept {
    return is_ui() ? get_ui() : UIValue();
}

} // namespace lintel

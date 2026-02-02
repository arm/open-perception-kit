#include <amp/PerceptionContext.h>
#include <cairo.h>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Osd {
class Layer;
using RawDetectionBox = amp::DetectionRect;

using Layers_t = std::deque<std::unique_ptr<Layer>>;
//------------------------------------------------
// object to wrap around cairo surface contains the actual drawing
class Layer {
  public:
    Layer(float width, float height)
        : surface(cairo_image_surface_create(
              CAIRO_FORMAT_ARGB32, static_cast<int>(width), static_cast<int>(height))),
          context(cairo_create(surface)){};
    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    ~Layer() {
        if (context) {
            cairo_destroy(context);
        }
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }
    float getWidth() const {
        return cairo_image_surface_get_width(surface);
    }
    float getHeight() const {
        return cairo_image_surface_get_height(surface);
    }
    cairo_surface_t *surface;
    cairo_t *context;
};
class Coordinate {
  public:
    Coordinate(float x, float y) : x(x), y(y) {}
    const float x;
    const float y;

    Coordinate operator+(const Coordinate &other) const {
        return Coordinate(x + other.x, y + other.y);
    }

    Coordinate operator-(const Coordinate &other) const {
        return Coordinate(x - other.x, y - other.y);
    }
};

class CoordinateRel : public Coordinate {
  public:
    CoordinateRel(float x, float y, const Layer &layer)
        : CoordinateRel{x, y, layer.getWidth(), layer.getHeight()} {}
    CoordinateRel(float x, float y, float width, float height)
        : Coordinate{x * width, y * height}, width(width), height(height) {}

  private:
    const float width;
    const float height;
};

class Color {
  public:
    constexpr Color(double r, double g, double b, double a) : r(r), g(g), b(b), a(a) {}

    // Parse #RRGGBBAA at compile time when given a string literal.
    constexpr explicit Color(std::string_view colorCode) : r(0.0), g(0.0), b(0.0), a(1.0) {
        const auto parseHex = [](char c) constexpr -> uint8_t {
            if (c >= '0' && c <= '9') {
                return static_cast<uint8_t>(c - '0');
            }
            if (c >= 'a' && c <= 'f') {
                return static_cast<uint8_t>(10 + (c - 'a'));
            }
            if (c >= 'A' && c <= 'F') {
                return static_cast<uint8_t>(10 + (c - 'A'));
            }
            throw std::invalid_argument("Invalid hex digit in color code");
        };

        const auto parseColor = [&](std::string_view code) constexpr -> uint32_t {
            if (code.size() != 9 || code.front() != '#') {
                throw std::invalid_argument("Color code must be #RRGGBBAA");
            }
            uint32_t value = 0;
            for (std::size_t i = 1; i < code.size(); ++i) {
                value = static_cast<uint32_t>((value << 4) | parseHex(code[i]));
            }
            return value;
        };

        const uint32_t color = parseColor(colorCode);
        r = static_cast<double>((color >> 24) & 0xff) / 255.0;
        g = static_cast<double>((color >> 16) & 0xff) / 255.0;
        b = static_cast<double>((color >> 8) & 0xff) / 255.0;
        a = static_cast<double>((color >> 0) & 0xff) / 255.0;
    }

    double r;
    double g;
    double b;
    double a;
};

// primitive drawing elements
class Point {
  public:
    static void draw(Layer &layer, const Coordinate &pos, const Color &color, float size) {
        cairo_set_source_rgba(layer.context, color.r, color.g, color.b, color.a);
        cairo_rectangle(layer.context, pos.x - size / 2, pos.y - size / 2, size, size);
        cairo_fill(layer.context);
    }
};
class Rectangle {
  public:
    static void draw(Layer &layer,
                     const Coordinate &pos,
                     float width,
                     float height,
                     const Color &color,
                     float thickness = 2.0f) {
        cairo_set_source_rgba(layer.context, color.r, color.g, color.b, color.a);
        cairo_set_line_width(layer.context, thickness);
        cairo_rectangle(layer.context, pos.x, pos.y, width, height);
        cairo_stroke(layer.context);
    }
};

class RectangleFilled {
  public:
    static void
    draw(Layer &layer, const Coordinate &pos, float width, float height, const Color &color) {
        cairo_set_source_rgba(layer.context, color.r, color.g, color.b, color.a);
        cairo_rectangle(layer.context, pos.x, pos.y, width, height);
        cairo_fill(layer.context);
    }
};
class Text {
  public:
    static void draw(Layer &layer,
                     const Coordinate &pos,
                     const std::string &text,
                     const Color &color,
                     const Color &bgColor,
                     const std::string &fontFamily,
                     float fontSize) {
        cairo_select_font_face(
            layer.context, fontFamily.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(layer.context, fontSize);
        cairo_text_extents_t text_ex;
        cairo_text_extents(layer.context, text.c_str(), &text_ex);
        RectangleFilled::draw(
            layer, Coordinate{pos.x, pos.y}, text_ex.width + 5, text_ex.height + 5, bgColor);
        cairo_set_source_rgba(layer.context, color.r, color.g, color.b, color.a);
        cairo_move_to(layer.context, pos.x, pos.y + text_ex.height);
        cairo_show_text(layer.context, text.c_str());
    }
};

class Circle {
  public:
    static void draw(Layer &layer,
                     const Coordinate &center,
                     float radius,
                     const Color &color,
                     float thickness = 2.0f) {
        cairo_set_source_rgba(layer.context, color.r, color.g, color.b, color.a);
        cairo_set_line_width(layer.context, thickness);
        cairo_arc(layer.context, center.x, center.y, radius, 0, std::numbers::pi * 2);
        cairo_stroke(layer.context);
    }
};

// composite elements
class ObjectBox {
  public:
    static constexpr auto fontsize = 16.0f;
    static void draw(Layer &layer,
                     const RawDetectionBox &objectBox,
                     const Color &color,
                     float thickness = 2.0f) {
        Text::draw(layer,
                   Coordinate{objectBox.x, objectBox.y},
                   objectBox.label,
                   Color{"#ffffffff"},
                   Color{"#000000ff"},
                   "monospace",
                   fontsize);
        Rectangle::draw(layer,
                        Coordinate{objectBox.x, objectBox.y},
                        objectBox.w,
                        objectBox.h,
                        color,
                        thickness);
    }
};

// object to wrap around cairo surface for the final output
class Canvas {
  public:
    Canvas(uint8_t *image_data, int width, int height) {
        surface = cairo_image_surface_create_for_data(
            image_data, CAIRO_FORMAT_ARGB32, width, height, width * 4);
        auto surface_status = cairo_surface_status(surface);
        if (surface_status != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surface);
            throw std::runtime_error("Failed to create surface in canvas");
        }
        cr = cairo_create(surface);
    }
    ~Canvas() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    void paint(const Layers_t &drawingLayers) {
        for (auto &layer : drawingLayers) {
            cairo_set_source_surface(cr, layer->surface, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_paint(cr);
        }
    }

  private:
    cairo_surface_t *surface;
    cairo_t *cr;
};
} // namespace Osd
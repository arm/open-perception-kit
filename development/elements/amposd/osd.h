/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "amp/Color.h"
#include "amp/Perception.h"
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

using Layers_t = std::deque<std::unique_ptr<Layer>>;
//------------------------------------------------
// object to wrap around cairo surface contains the actual drawing
class Layer {
  public:
    Layer(float width, float height)
        : surface(cairo_image_surface_create(
              CAIRO_FORMAT_ARGB32, static_cast<int>(width), static_cast<int>(height))),
          context(cairo_create(surface)) {};
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

// primitive drawing elements
class Point {
  public:
    static void draw(Layer &layer, const Coordinate &pos, const amp::Color &color, float size) {
        cairo_set_source_rgba(layer.context,
                              amp::Colors::getRedf(color),
                              amp::Colors::getGreenf(color),
                              amp::Colors::getBluef(color),
                              amp::Colors::getAlphaf(color));
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
                     const amp::Color &color,
                     float thickness = 2.0f) {
        cairo_set_source_rgba(layer.context,
                              amp::Colors::getRedf(color),
                              amp::Colors::getGreenf(color),
                              amp::Colors::getBluef(color),
                              amp::Colors::getAlphaf(color));
        cairo_set_line_width(layer.context, thickness);
        cairo_rectangle(layer.context, pos.x, pos.y, width, height);
        cairo_stroke(layer.context);
    }
};

class RectangleFilled {
  public:
    static void
    draw(Layer &layer, const Coordinate &pos, float width, float height, const amp::Color &color) {
        cairo_set_source_rgba(layer.context,
                              amp::Colors::getRedf(color),
                              amp::Colors::getGreenf(color),
                              amp::Colors::getBluef(color),
                              amp::Colors::getAlphaf(color));
        cairo_rectangle(layer.context, pos.x, pos.y, width, height);
        cairo_fill(layer.context);
    }
};

class Text {
  public:
    static void draw(Layer &layer,
                     const Coordinate &pos,
                     const std::string &text,
                     const amp::Color &color,
                     const amp::Color &bgColor,
                     const std::string &fontFamily,
                     float fontSize) {
        cairo_select_font_face(
            layer.context, fontFamily.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(layer.context, fontSize);
        cairo_text_extents_t text_ex;
        cairo_text_extents(layer.context, text.c_str(), &text_ex);
        RectangleFilled::draw(
            layer, Coordinate{pos.x, pos.y}, text_ex.width + 5, text_ex.height + 5, bgColor);
        cairo_set_source_rgba(layer.context,
                              amp::Colors::getRedf(color),
                              amp::Colors::getGreenf(color),
                              amp::Colors::getBluef(color),
                              amp::Colors::getAlphaf(color));
        cairo_move_to(layer.context, pos.x, pos.y + text_ex.height);
        cairo_show_text(layer.context, text.c_str());
    }
};

class Circle {
  public:
    static void draw(Layer &layer,
                     const Coordinate &center,
                     float radius,
                     const amp::Color &color,
                     float thickness = 2.0f) {
        cairo_set_source_rgba(layer.context,
                              amp::Colors::getRedf(color),
                              amp::Colors::getGreenf(color),
                              amp::Colors::getBluef(color),
                              amp::Colors::getAlphaf(color));
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
                     const amp::Perception::Rect &objectBox,
                     const amp::Color &color,
                     float thickness = 2.0f) {
        Text::draw(layer,
                   Coordinate{objectBox.x, objectBox.y},
                   objectBox.text,
                   amp::Colors::fromStringOrDefault("#ffffffff"),
                   amp::Colors::fromStringOrDefault("#000000ff"),
                   "monospace",
                   fontsize);
        Rectangle::draw(layer,
                        Coordinate{objectBox.x, objectBox.y},
                        objectBox.width,
                        objectBox.height,
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

class Arrow {
  public:
    // headLength/headWidth are in pixels. You can tune defaults.
    static void draw(Layer &layer,
                     const Coordinate &from,
                     const Coordinate &to,
                     const amp::Color &color,
                     float thickness = 2.0f,
                     float headLength = 12.0f,
                     float headWidth = 8.0f) {
        cairo_t *cr = layer.context;

        cairo_set_source_rgba(cr,
                              amp::Colors::getRedf(color),
                              amp::Colors::getGreenf(color),
                              amp::Colors::getBluef(color),
                              amp::Colors::getAlphaf(color));

        // Vector from->to
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float len = std::sqrt(dx * dx + dy * dy);

        // Degenerate case: no length -> draw a point
        if (len < 1e-6f) {
            cairo_set_line_width(cr, thickness);
            cairo_arc(cr, from.x, from.y, thickness * 0.5f, 0.0, std::numbers::pi * 2.0);
            cairo_fill(cr);
            return;
        }

        // Unit direction
        const float ux = dx / len;
        const float uy = dy / len;

        // Clamp head length so it doesn't exceed arrow length
        const float hl = std::min(headLength, len);

        // End of shaft (start of head), so head doesn't overshoot the endpoint
        const Coordinate shaftEnd{to.x - ux * hl, to.y - uy * hl};

        // Perpendicular unit
        const float px = -uy;
        const float py = ux;

        // Head triangle corners
        const float hw = headWidth * 0.5f;
        const Coordinate left{shaftEnd.x + px * hw, shaftEnd.y + py * hw};
        const Coordinate right{shaftEnd.x - px * hw, shaftEnd.y - py * hw};

        // Draw shaft
        cairo_set_line_width(cr, thickness);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, from.x, from.y);
        cairo_line_to(cr, shaftEnd.x, shaftEnd.y);
        cairo_stroke(cr);

        // Draw head (filled triangle)
        cairo_move_to(cr, to.x, to.y);
        cairo_line_to(cr, left.x, left.y);
        cairo_line_to(cr, right.x, right.y);
        cairo_close_path(cr);
        cairo_fill(cr);
    }
};

} // namespace Osd
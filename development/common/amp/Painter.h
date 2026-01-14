#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace amp {

class Painter {
  public:
    // buffer: pointer to preallocated image data (not owned) - Rgb888
    // width:  number of pixels per row
    // height: number of rows
    // strideBytes: number of bytes between starts of consecutive rows
    Painter(uint8_t *bufferRgb888, int width, int height, int strideBytes)
        : m_data(bufferRgb888), m_width(width), m_height(height), m_stride(strideBytes) {}

    // Simple validity check
    bool valid() const noexcept {
        return (m_data != nullptr && m_width > 0 && m_height > 0 && m_stride >= m_width * 3);
    }

    // Draw an *unfilled* rectangle with given color and thickness.
    // (x, y) = top-left corner in pixels.
    void drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, int thickness = 1) {
        if (!valid() || w <= 0 || h <= 0 || thickness <= 0)
            return;

        // We will draw up to `thickness` pixels inward from each edge.
        // Each "layer" is a 1-pixel-wide rectangle inset by `i` pixels.
        for (int i = 0; i < thickness; ++i) {
            int x0 = x + i;
            int y0 = y + i;
            int x1 = x + w - 1 - i;
            int y1 = y + h - 1 - i;

            if (x0 > x1 || y0 > y1) {
                // The rectangle collapsed (too thick for its size).
                break;
            }

            // Top edge
            drawHLine(x0, x1, y0, r, g, b);

            // Bottom edge (only if different from top)
            if (y1 != y0) {
                drawHLine(x0, x1, y1, r, g, b);
            }

            // Left edge
            drawVLine(y0, y1, x0, r, g, b);

            // Right edge (only if different from left)
            if (x1 != x0) {
                drawVLine(y0, y1, x1, r, g, b);
            }
        }
    }

    void drawSegmentMap8(const uint8_t *smPixels, size_t smWidth, size_t smHeight) {
        if (!smPixels || !m_data || smWidth == 0 || smHeight == 0 || m_width == 0 || m_height == 0)
            return;

        for (size_t y = 0; y < (size_t)m_height; ++y) {
            const size_t smY = (y * smHeight) / m_height; // 0 .. smHeight-1
            const size_t smRow = smY * smWidth;

            for (size_t x = 0; x < (size_t)m_width; ++x) {
                const size_t smX = (x * smWidth) / m_width; // 0 .. smWidth-1
                const uint8_t smPixel = smPixels[smRow + smX];

                // If it's a probability/heatmap, you'll probably want a threshold:
                // if (smPixel >= 128) { ... }
                if (smPixel != 0 && smPixel != 128) {
                    // printf("%d ", smPixel);
                    const size_t dstOffset = 3 * (y * m_width + x);
                    m_data[dstOffset + 0] = 0x00;
                    m_data[dstOffset + 1] = 0x00;
                    m_data[dstOffset + 2] = 0x00;
                }
            }
        }
    }

    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
        // Clip left/top
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }

        // Clip right/bottom
        if (x + w > m_width)
            w = m_width - x;
        if (y + h > m_height)
            h = m_height - y;

        // Fully outside?
        if (w <= 0 || h <= 0)
            return;

        for (int row = 0; row < h; ++row) {
            uint8_t *dst = m_data + (y + row) * m_stride + x * 3;

            for (int col = 0; col < w; ++col) {
                dst[0] = r;
                dst[1] = g;
                dst[2] = b;
                dst += 3;
            }
        }
    }

    // Safely set a pixel if inside bounds
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
            return;

        uint8_t *p = m_data + y * m_stride + x * 3;
        p[0] = r;
        p[1] = g;
        p[2] = b;
    }

    void drawCircle(
        int centerX, int centerY, int radius, uint8_t r, uint8_t g, uint8_t b, int thickness = 1) {
        if (!valid() || radius <= 0 || thickness <= 0)
            return;

        const int outerR = radius;
        const int innerR = std::max(0, radius - thickness + 1);

        const int outerR2 = outerR * outerR;
        const int innerR2 = innerR * innerR;

        // Brute-force over bounding box, rely on setPixel() for clipping.
        for (int dy = -outerR; dy <= outerR; ++dy) {
            for (int dx = -outerR; dx <= outerR; ++dx) {
                const int dist2 = dx * dx + dy * dy;
                if (dist2 <= outerR2 && dist2 >= innerR2) {
                    setPixel(centerX + dx, centerY + dy, r, g, b);
                }
            }
        }
    }

    // Draw a horizontal line y, from x0 to x1 inclusive, clipped
    void drawHLine(int x0, int x1, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (y < 0 || y >= m_height)
            return;

        if (x0 > x1)
            std::swap(x0, x1);

        // Clip to [0, m_width-1]
        x0 = std::max(x0, 0);
        x1 = std::min(x1, m_width - 1);
        if (x0 > x1)
            return;

        uint8_t *row = m_data + y * m_stride + x0 * 3;
        for (int x = x0; x <= x1; ++x) {
            row[0] = r;
            row[1] = g;
            row[2] = b;
            row += 3;
        }
    }

    // Draw a vertical line x, from y0 to y1 inclusive, clipped
    void drawVLine(int y0, int y1, int x, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || x >= m_width)
            return;

        if (y0 > y1)
            std::swap(y0, y1);

        // Clip to [0, m_height-1]
        y0 = std::max(y0, 0);
        y1 = std::min(y1, m_height - 1);
        if (y0 > y1)
            return;

        uint8_t *p = m_data + y0 * m_stride + x * 3;
        for (int y = y0; y <= y1; ++y) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p += m_stride;
        }
    }

    // Draw a "point" as a filled square centered at (x, y).
    // thickness = 0 → single pixel
    // thickness = 1 → 3x3 square, etc.
    void drawPoint(int x, int y, uint8_t r, uint8_t g, uint8_t b, int thickness = 0) {
        if (!valid() || thickness < 0)
            return;

        int radius = thickness; // half-size in pixels

        int x0 = x - radius;
        int x1 = x + radius;
        int y0 = y - radius;
        int y1 = y + radius;

        // Clip to image bounds
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, m_width - 1);
        y1 = std::min(y1, m_height - 1);

        if (x0 > x1 || y0 > y1)
            return;

        for (int row = y0; row <= y1; ++row) {
            uint8_t *p = m_data + row * m_stride + x0 * 3;
            for (int col = x0; col <= x1; ++col) {
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p += 3;
            }
        }
    }

    // Draw a line from (x0, y0) to (x1, y1) with given color and thickness.
    // thickness = 0 -> 1-pixel wide line
    void
    drawLine(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, int thickness = 0) {
        if (!valid() || thickness < 0)
            return;

        // Bresenham-style integer line drawing
        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);

        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;

        int err = dx - dy;

        while (true) {
            // Draw a "point" (square) at the current position.
            // drawPoint handles clipping and thickness.
            drawPoint(x0, y0, r, g, b, thickness);

            if (x0 == x1 && y0 == y1)
                break;

            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

  private:
    uint8_t *m_data;
    int m_width;
    int m_height;
    int m_stride; // bytes per row
};
} // namespace amp

namespace amp {

struct TextRenderer {
    static constexpr int GLYPH_W = 10;
    static constexpr int GLYPH_H = 16;
    static constexpr int GLYPH_SPACING = 0; // 0 = continuous background

    // Map supported chars -> glyph index
    static int charToIndex(char ch) {
        if (ch >= '0' && ch <= '9')
            return ch - '0'; // 0..9

        if (ch >= 'a' && ch <= 'z')
            return 10 + (ch - 'a'); // 10..35

        switch (ch) {
        case ' ':
            return 36;
        case ',':
            return 37;
        case '.':
            return 38;
        case ':':
            return 39;
        case '?':
            return 40;
        case '!':
            return 41;
        case '%':
            return 42;
        case '-':
            return 43;
        default:
            return -1;
        }
    }

    // 44 glyphs: 0–9, a–z, space, , . : ? ! % -
    // Each row is 10 bits (use bits 9..0 left->right).
    static constexpr uint16_t font[44][GLYPH_H] = {
        // '0' (0)
        {0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0100000001,
         0b0100001001,
         0b0100010001,
         0b0100100001,
         0b0101000001,
         0b0110000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0000000000},
        // '1' (1)
        {0b0000011000,
         0b0000111000,
         0b0001011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0000011000,
         0b0001111110,
         0b0000000000},
        // '2' (2)
        {0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0000000001,
         0b0000000001,
         0b0000000010,
         0b0000000100,
         0b0000001000,
         0b0000010000,
         0b0000100000,
         0b0001000000,
         0b0010000000,
         0b0100000000,
         0b0100000000,
         0b0111111111,
         0b0000000000},
        // '3' (3)
        {0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0000000001,
         0b0000000001,
         0b0000000010,
         0b0000111100,
         0b0000000010,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0000000000},
        // '4' (4)
        {0b0000001000,
         0b0000011000,
         0b0000101000,
         0b0001001000,
         0b0010001000,
         0b0100001000,
         0b1000001000,
         0b1111111111,
         0b0000001000,
         0b0000001000,
         0b0000001000,
         0b0000001000,
         0b0000001000,
         0b0000001000,
         0b0000001000,
         0b0000000000},
        // '5' (5)
        {0b0111111111,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0111111100,
         0b0000000010,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0000000000},
        // '6' (6)
        {0b0000111100,
         0b0001000000,
         0b0010000000,
         0b0100000000,
         0b0100000000,
         0b0111111100,
         0b0100000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0000000000,
         0b0000000000},
        // '7' (7)
        {0b0111111111,
         0b0000000001,
         0b0000000010,
         0b0000000010,
         0b0000000100,
         0b0000000100,
         0b0000001000,
         0b0000001000,
         0b0000010000,
         0b0000010000,
         0b0000100000,
         0b0000100000,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0000000000},
        // '8' (8)
        {0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0000000000,
         0b0000000000},
        // '9' (9)
        {0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0001111111,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0000000010,
         0b0000000100,
         0b0000011000,
         0b0000010000,
         0b0000000000},

        // 'a' (10)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0001111100,
         0b0010000010,
         0b0000000001,
         0b0000000001,
         0b0001111111,
         0b0010000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000011,
         0b0001111101,
         0b0000000000,
         0b0000000000},
        // 'b' (11)
        {0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0101111100,
         0b0110000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0110000010,
         0b0101111100,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'c' (12)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0001111110,
         0b0010000001,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0010000001,
         0b0001111110,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'd' (13)
        {0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0001111101,
         0b0010000011,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000011,
         0b0001111101,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'e' (14)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0100000001,
         0b0111111111,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0010000001,
         0b0001111110,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'f' (15)
        {0b0000111110,
         0b0001000001,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0111111100,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0001000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'g' (16)
        {0b0000000000,
         0b0000000000,
         0b0001111101,
         0b0010000011,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000011,
         0b0001111101,
         0b0000000001,
         0b0000000001,
         0b0100000010,
         0b0011111100,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'h' (17)
        {0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0101111100,
         0b0110000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'i' (18)
        {0b0000010000,
         0b0000010000,
         0b0000000000,
         0b0000110000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000111000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'j' (19)
        {0b0000000100,
         0b0000000100,
         0b0000000000,
         0b0000001100,
         0b0000000100,
         0b0000000100,
         0b0000000100,
         0b0000000100,
         0b0000000100,
         0b0000000100,
         0b0000000100,
         0b0100000100,
         0b0010001000,
         0b0001110000,
         0b0000000000,
         0b0000000000},
        // 'k' (20)
        {0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000100,
         0b0100001000,
         0b0100010000,
         0b0100100000,
         0b0111000000,
         0b0100100000,
         0b0100010000,
         0b0100001000,
         0b0100000100,
         0b0100000010,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'l' (21)
        {0b0001110000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000111000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'm' (22)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0111101110,
         0b0100010001,
         0b0100010001,
         0b0100010001,
         0b0100010001,
         0b0100010001,
         0b0100010001,
         0b0100010001,
         0b0100010001,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'n' (23)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0101111100,
         0b0110000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'o' (24)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0001111100,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'p' (25)
        {0b0000000000,
         0b0000000000,
         0b0101111100,
         0b0110000010,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0110000010,
         0b0101111100,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'q' (26)
        {0b0000000000,
         0b0000000000,
         0b0001111101,
         0b0010000011,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0010000011,
         0b0001111101,
         0b0000000001,
         0b0000000001,
         0b0000000001,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'r' (27)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0101111100,
         0b0110000010,
         0b0100000001,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0100000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 's' (28)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0001111110,
         0b0010000001,
         0b0010000000,
         0b0001000000,
         0b0000111100,
         0b0000000010,
         0b0000000001,
         0b0000000001,
         0b0100000001,
         0b0011111110,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 't' (29)
        {0b0000000000,
         0b0000100000,
         0b0000100000,
         0b0011111110,
         0b0000100000,
         0b0000100000,
         0b0000100000,
         0b0000100000,
         0b0000100000,
         0b0000100000,
         0b0000100001,
         0b0000100001,
         0b0000011110,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'u' (30)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000001,
         0b0100000011,
         0b0011111101,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'v' (31)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0010000010,
         0b0010000010,
         0b0001000100,
         0b0001000100,
         0b0000101000,
         0b0000101000,
         0b0000010000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'w' (32)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0100000001,
         0b0100000001,
         0b0100010001,
         0b0100010001,
         0b0100101001,
         0b0100101001,
         0b0010101010,
         0b0011000110,
         0b0010000010,
         0b0010000010,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'x' (33)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0100000001,
         0b0010000010,
         0b0001000100,
         0b0000101000,
         0b0000010000,
         0b0000101000,
         0b0001000100,
         0b0010000010,
         0b0100000001,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // 'y' (34)
        {0b0000000000,
         0b0000000000,
         0b0100000001,
         0b0100000001,
         0b0010000010,
         0b0010000010,
         0b0001000100,
         0b0000101000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000100000,
         0b0001000000,
         0b0010000000,
         0b0000000000,
         0b0000000000},
        // 'z' (35)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0111111111,
         0b0000000010,
         0b0000000100,
         0b0000001000,
         0b0000010000,
         0b0000100000,
         0b0001000000,
         0b0010000000,
         0b0111111111,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},

        // ' ' (36)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // ',' (37)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000110000,
         0b0000110000,
         0b0000010000,
         0b0000100000,
         0b0000000000},
        // '.' (38)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000110000,
         0b0000110000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // ':' (39)
        {0b0000000000,
         0b0000000000,
         0b0000110000,
         0b0000110000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000110000,
         0b0000110000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // '?' (40)
        {0b0001111100,
         0b0010000010,
         0b0100000001,
         0b0000000001,
         0b0000000010,
         0b0000000100,
         0b0000001000,
         0b0000010000,
         0b0000010000,
         0b0000000000,
         0b0000010000,
         0b0000010000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // '!' (41)
        {0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000010000,
         0b0000000000,
         0b0000010000,
         0b0000010000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // '%' (42) – crude diagonal + 2 dots
        {0b0100000001,
         0b0010000010,
         0b0010000010,
         0b0001000100,
         0b0001000100,
         0b0000101000,
         0b0000101000,
         0b0000010000,
         0b0000010000,
         0b0000000000,
         0b0100000001,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
        // '-' (43)
        {0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0001111100,
         0b0001111100,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000,
         0b0000000000},
    };

    static void drawChar(Painter &surf,
                         int x,
                         int y,
                         char ch,
                         uint8_t fr,
                         uint8_t fg,
                         uint8_t fb,
                         uint8_t br,
                         uint8_t bg,
                         uint8_t bb) {
        int idx = charToIndex(ch);
        if (idx < 0 || idx >= 44) {
            // Unsupported: just draw background block
            for (int r = 0; r < GLYPH_H; ++r)
                for (int c = 0; c < GLYPH_W; ++c)
                    surf.setPixel(x + c, y + r, br, bg, bb);
            return;
        }

        const uint16_t *glyph = font[idx];

        for (int row = 0; row < GLYPH_H; ++row) {
            uint16_t bits = glyph[row];
            for (int col = 0; col < GLYPH_W; ++col) {
                bool on = (bits & (1u << (GLYPH_W - 1 - col))) != 0;
                if (on)
                    surf.setPixel(x + col, y + row, fr, fg, fb);
                else
                    surf.setPixel(x + col, y + row, br, bg, bb);
            }
        }
    }

    static void drawText(Painter &surf,
                         int x,
                         int y,
                         const std::string &text,
                         uint8_t fr,
                         uint8_t fg,
                         uint8_t fb,
                         uint8_t br,
                         uint8_t bg,
                         uint8_t bb) {
        int cursorX = x;
        int cursorY = y;

        for (char ch : text) {
            if (ch == '\n') {
                cursorX = x;
                cursorY += GLYPH_H + 1; // line spacing
                continue;
            }

            drawChar(surf, cursorX, cursorY, ch, fr, fg, fb, br, bg, bb);
            cursorX += GLYPH_W + GLYPH_SPACING;
        }
    }
};

} // namespace amp

/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace opk {

/**
 * @brief Packed color value in ABGR byte order.
 */
using Color = uint32_t;

/**
 * @brief Floating-point RGBA color.
 */
struct Colorf {
    /** @brief Constructs opaque white. */
    Colorf() = default;
    /** @brief Constructs color from RGBA components. */
    Colorf(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
    /** @brief Constructs opaque color from RGB components. */
    Colorf(float r, float g, float b) : r(r), g(g), b(b) {}

    /// Red, green, blue, alpha channels.
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

/**
 * @brief Packs RGB bytes into a Color with alpha set to 255.
 */
constexpr Color colorFromRgbBytes(uint32_t r, uint32_t g, uint32_t b) {
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

/**
 * @brief Named colors and conversion helpers.
 */
class Colors {
  public:
    /// Named color constants usable by config and UI code.
    static constexpr const Color space = 0x00000000;
    static constexpr const Color transparentWhite = 0x00ffffff;

    static constexpr const Color pink = colorFromRgbBytes(255, 192, 203);
    static constexpr const Color lightPink = colorFromRgbBytes(255, 182, 193);
    static constexpr const Color hotPink = colorFromRgbBytes(255, 105, 180);
    static constexpr const Color deepPink = colorFromRgbBytes(255, 20, 147);
    static constexpr const Color paleVioletRed = colorFromRgbBytes(219, 112, 147);
    static constexpr const Color mediumVioletRed = colorFromRgbBytes(199, 21, 133);

    static constexpr const Color lightSalmon = colorFromRgbBytes(255, 160, 122);
    static constexpr const Color salmon = colorFromRgbBytes(250, 128, 114);
    static constexpr const Color darkSalmon = colorFromRgbBytes(233, 150, 122);
    static constexpr const Color lightCoral = colorFromRgbBytes(240, 128, 128);
    static constexpr const Color indianRed = colorFromRgbBytes(205, 92, 92);
    static constexpr const Color crimson = colorFromRgbBytes(220, 20, 60);
    static constexpr const Color fireBrick = colorFromRgbBytes(178, 34, 34);
    static constexpr const Color darkRed = colorFromRgbBytes(139, 0, 0);
    static constexpr const Color red = colorFromRgbBytes(255, 0, 0);

    static constexpr const Color orangeRed = colorFromRgbBytes(255, 69, 0);
    static constexpr const Color tomato = colorFromRgbBytes(255, 99, 71);
    static constexpr const Color coral = colorFromRgbBytes(255, 127, 80);
    static constexpr const Color darkOrange = colorFromRgbBytes(255, 140, 0);
    static constexpr const Color orange = colorFromRgbBytes(255, 165, 0);

    static constexpr const Color yellow = colorFromRgbBytes(255, 255, 0);
    static constexpr const Color lightYellow = colorFromRgbBytes(255, 255, 224);
    static constexpr const Color lemonChiffon = colorFromRgbBytes(255, 250, 205);
    static constexpr const Color lightGoldenrodYellow = colorFromRgbBytes(250, 250, 210);
    static constexpr const Color papayaWhip = colorFromRgbBytes(255, 239, 213);
    static constexpr const Color moccasin = colorFromRgbBytes(255, 228, 181);
    static constexpr const Color peachPuff = colorFromRgbBytes(255, 218, 185);
    static constexpr const Color paleGoldenrod = colorFromRgbBytes(238, 232, 170);
    static constexpr const Color khaki = colorFromRgbBytes(240, 230, 140);
    static constexpr const Color darkKhaki = colorFromRgbBytes(189, 183, 107);
    static constexpr const Color gold = colorFromRgbBytes(255, 215, 0);

    static constexpr const Color cornsilk = colorFromRgbBytes(255, 248, 220);
    static constexpr const Color blanchedAlmond = colorFromRgbBytes(255, 235, 205);
    static constexpr const Color bisque = colorFromRgbBytes(255, 228, 196);
    static constexpr const Color navajoWhite = colorFromRgbBytes(255, 222, 173);
    static constexpr const Color wheat = colorFromRgbBytes(245, 222, 179);
    static constexpr const Color burlyWood = colorFromRgbBytes(222, 184, 135);
    static constexpr const Color tan = colorFromRgbBytes(210, 180, 140);
    static constexpr const Color rosyBrown = colorFromRgbBytes(188, 143, 143);
    static constexpr const Color sandyBrown = colorFromRgbBytes(244, 164, 96);
    static constexpr const Color goldenrod = colorFromRgbBytes(218, 165, 32);
    static constexpr const Color darkGoldenrod = colorFromRgbBytes(184, 134, 11);
    static constexpr const Color peru = colorFromRgbBytes(205, 133, 63);
    static constexpr const Color chocolate = colorFromRgbBytes(210, 105, 30);
    static constexpr const Color saddleBrown = colorFromRgbBytes(139, 69, 19);
    static constexpr const Color sienna = colorFromRgbBytes(160, 82, 45);
    static constexpr const Color brown = colorFromRgbBytes(165, 42, 42);
    static constexpr const Color maroon = colorFromRgbBytes(128, 0, 0);

    static constexpr const Color darkOliveGreen = colorFromRgbBytes(85, 107, 47);
    static constexpr const Color olive = colorFromRgbBytes(128, 128, 0);
    static constexpr const Color oliveDrab = colorFromRgbBytes(107, 142, 35);
    static constexpr const Color yellowGreen = colorFromRgbBytes(154, 205, 50);
    static constexpr const Color limeGreen = colorFromRgbBytes(50, 205, 50);
    static constexpr const Color lime = colorFromRgbBytes(0, 255, 0);
    static constexpr const Color lawnGreen = colorFromRgbBytes(124, 252, 0);
    static constexpr const Color chartreuse = colorFromRgbBytes(127, 255, 0);
    static constexpr const Color greenYellow = colorFromRgbBytes(173, 255, 47);
    static constexpr const Color springGreen = colorFromRgbBytes(0, 255, 127);
    static constexpr const Color mediumSpringGreen = colorFromRgbBytes(0, 250, 154);
    static constexpr const Color lightGreen = colorFromRgbBytes(144, 238, 144);
    static constexpr const Color paleGreen = colorFromRgbBytes(152, 251, 152);
    static constexpr const Color darkSeaGreen = colorFromRgbBytes(143, 188, 143);
    static constexpr const Color mediumSeaGreen = colorFromRgbBytes(60, 179, 113);
    static constexpr const Color seaGreen = colorFromRgbBytes(46, 139, 87);
    static constexpr const Color forestGreen = colorFromRgbBytes(34, 139, 34);
    static constexpr const Color green = colorFromRgbBytes(0, 128, 0);
    static constexpr const Color darkGreen = colorFromRgbBytes(0, 100, 0);

    static constexpr const Color mediumAquamarine = colorFromRgbBytes(102, 205, 170);
    static constexpr const Color aqua = colorFromRgbBytes(0, 255, 255);
    static constexpr const Color cyan = colorFromRgbBytes(0, 255, 255);
    static constexpr const Color lightCyan = colorFromRgbBytes(224, 255, 255);
    static constexpr const Color paleTurquoise = colorFromRgbBytes(175, 238, 238);
    static constexpr const Color aquamarine = colorFromRgbBytes(127, 255, 212);
    static constexpr const Color turquoise = colorFromRgbBytes(64, 224, 208);
    static constexpr const Color mediumTurquoise = colorFromRgbBytes(72, 209, 204);
    static constexpr const Color darkTurquoise = colorFromRgbBytes(0, 206, 209);
    static constexpr const Color lightSeaGreen = colorFromRgbBytes(32, 178, 170);
    static constexpr const Color cadetBlue = colorFromRgbBytes(95, 158, 160);
    static constexpr const Color darkCyan = colorFromRgbBytes(0, 139, 139);
    static constexpr const Color teal = colorFromRgbBytes(0, 128, 128);

    static constexpr const Color lightSteelBlue = colorFromRgbBytes(176, 196, 222);
    static constexpr const Color powderBlue = colorFromRgbBytes(176, 224, 230);
    static constexpr const Color lightBlue = colorFromRgbBytes(173, 216, 230);
    static constexpr const Color skyBlue = colorFromRgbBytes(135, 206, 235);
    static constexpr const Color lightSkyBlue = colorFromRgbBytes(135, 206, 250);
    static constexpr const Color deepSkyBlue = colorFromRgbBytes(0, 191, 255);
    static constexpr const Color dodgerBlue = colorFromRgbBytes(30, 144, 255);
    static constexpr const Color cornflowerBlue = colorFromRgbBytes(100, 149, 237);
    static constexpr const Color steelBlue = colorFromRgbBytes(70, 130, 180);
    static constexpr const Color royalBlue = colorFromRgbBytes(65, 105, 225);
    static constexpr const Color blue = colorFromRgbBytes(0, 0, 255);
    static constexpr const Color mediumBlue = colorFromRgbBytes(0, 0, 205);
    static constexpr const Color darkBlue = colorFromRgbBytes(0, 0, 139);
    static constexpr const Color navy = colorFromRgbBytes(0, 0, 128);
    static constexpr const Color midnightBlue = colorFromRgbBytes(25, 25, 112);

    static constexpr const Color lavender = colorFromRgbBytes(230, 230, 250);
    static constexpr const Color thistle = colorFromRgbBytes(216, 191, 216);
    static constexpr const Color plum = colorFromRgbBytes(221, 160, 221);
    static constexpr const Color violet = colorFromRgbBytes(238, 130, 238);
    static constexpr const Color orchid = colorFromRgbBytes(218, 112, 214);
    static constexpr const Color fuchsia = colorFromRgbBytes(255, 0, 255);
    static constexpr const Color magenta = colorFromRgbBytes(255, 0, 255);
    static constexpr const Color mediumOrchid = colorFromRgbBytes(186, 85, 211);
    static constexpr const Color mediumPurple = colorFromRgbBytes(147, 112, 219);
    static constexpr const Color blueViolet = colorFromRgbBytes(138, 43, 226);
    static constexpr const Color darkViolet = colorFromRgbBytes(148, 0, 211);
    static constexpr const Color darkOrchid = colorFromRgbBytes(153, 50, 204);
    static constexpr const Color darkMagenta = colorFromRgbBytes(139, 0, 139);
    static constexpr const Color purple = colorFromRgbBytes(128, 0, 128);
    static constexpr const Color indigo = colorFromRgbBytes(75, 0, 130);
    static constexpr const Color darkSlateBlue = colorFromRgbBytes(72, 61, 139);
    static constexpr const Color rebeccaPurple = colorFromRgbBytes(102, 51, 153);
    static constexpr const Color slateBlue = colorFromRgbBytes(106, 90, 205);
    static constexpr const Color mediumSlateBlue = colorFromRgbBytes(123, 104, 238);

    static constexpr const Color white = colorFromRgbBytes(255, 255, 255);
    static constexpr const Color snow = colorFromRgbBytes(255, 250, 250);
    static constexpr const Color honeydew = colorFromRgbBytes(240, 255, 240);
    static constexpr const Color mintCream = colorFromRgbBytes(245, 255, 250);
    static constexpr const Color azure = colorFromRgbBytes(240, 255, 255);
    static constexpr const Color aliceBlue = colorFromRgbBytes(240, 248, 255);
    static constexpr const Color ghostWhite = colorFromRgbBytes(248, 248, 255);
    static constexpr const Color whiteSmoke = colorFromRgbBytes(245, 245, 245);
    static constexpr const Color seashell = colorFromRgbBytes(255, 245, 238);
    static constexpr const Color beige = colorFromRgbBytes(245, 245, 220);
    static constexpr const Color oldLace = colorFromRgbBytes(253, 245, 230);
    static constexpr const Color floralWhite = colorFromRgbBytes(255, 250, 240);
    static constexpr const Color ivory = colorFromRgbBytes(255, 255, 240);
    static constexpr const Color antiqueWhite = colorFromRgbBytes(250, 235, 215);
    static constexpr const Color linen = colorFromRgbBytes(250, 240, 230);
    static constexpr const Color lavenderBlush = colorFromRgbBytes(255, 240, 245);
    static constexpr const Color mistyRose = colorFromRgbBytes(255, 228, 225);

    static constexpr const Color gainsboro = colorFromRgbBytes(220, 220, 220);
    static constexpr const Color lightGray = colorFromRgbBytes(211, 211, 211);
    static constexpr const Color silver = colorFromRgbBytes(192, 192, 192);
    static constexpr const Color darkGray = colorFromRgbBytes(169, 169, 169);
    static constexpr const Color gray = colorFromRgbBytes(128, 128, 128);
    static constexpr const Color dimGray = colorFromRgbBytes(105, 105, 105);
    static constexpr const Color lightSlateGray = colorFromRgbBytes(119, 136, 153);
    static constexpr const Color slateGray = colorFromRgbBytes(112, 128, 144);
    static constexpr const Color darkSlateGray = colorFromRgbBytes(47, 79, 79);
    static constexpr const Color black = colorFromRgbBytes(0, 0, 0);

    /**
     * @brief Parses a named color or #RRGGBB/#RRGGBBAA value.
     * @param name Color name or hex string.
     * @param result Parsed color output.
     * @return true on success, false otherwise.
     */
    static inline bool fromString(const std::string &name, Color &result) {
        Color color;
        if (tryFromString(name, color)) {
            result = color;
            return true;
        }
        return false;
    }

    /**
     * @brief Parses a color and returns a fallback when parsing fails.
     * @param name Color name or hex string.
     * @param defaultColor Fallback color when parsing fails.
     * @return Parsed color or @p defaultColor.
     */
    static inline Color fromStringOrDefault(const std::string &name,
                                            Color defaultColor = 0xffffff) {
        Color color;
        if (tryFromString(name, color))
            return color;
        return defaultColor;
    }

    /** @brief Extracts red byte component. */
    static uint8_t getRed(Color color) {
        return ((unsigned char)(color >> 0));
    }
    /** @brief Extracts green byte component. */
    static uint8_t getGreen(Color color) {
        return ((unsigned char)(color >> 8));
    }
    /** @brief Extracts blue byte component. */
    static uint8_t getBlue(Color color) {
        return ((unsigned char)(color >> 16));
    }
    /** @brief Extracts alpha byte component. */
    static uint8_t getAlpha(Color color) {
        return ((unsigned char)(color >> 24));
    }
    /** @brief Extracts red component as [0,1] float. */
    static float getRedf(Color color) {
        return ((color) & 0xff) / 255.0f;
    }
    /** @brief Extracts green component as [0,1] float. */
    static float getGreenf(Color color) {
        return ((color >> 8) & 0xff) / 255.0f;
    }
    /** @brief Extracts blue component as [0,1] float. */
    static float getBluef(Color color) {
        return ((color >> 16) & 0xff) / 255.0f;
    }
    /** @brief Extracts alpha component as [0,1] float. */
    static float getAlphaf(Color color) {
        return (color >> 24) / 255.0f;
    }

  private:
    /**
     * @brief Internal parser for named and hex colors.
     */
    static inline bool tryFromString(const std::string &name, Color &result) {
        static const std::unordered_map<std::string, Color> table = {

            {"space", Colors::space},

            {"pink", Colors::pink},
            {"lightPink", Colors::lightPink},
            {"hotPink", Colors::hotPink},
            {"deepPink", Colors::deepPink},
            {"paleVioletRed", Colors::paleVioletRed},
            {"mediumVioletRed", Colors::mediumVioletRed},

            {"lightSalmon", Colors::lightSalmon},
            {"salmon", Colors::salmon},
            {"darkSalmon", Colors::darkSalmon},
            {"lightCoral", Colors::lightCoral},
            {"indianRed", Colors::indianRed},
            {"crimson", Colors::crimson},
            {"fireBrick", Colors::fireBrick},
            {"darkRed", Colors::darkRed},
            {"red", Colors::red},

            {"orangeRed", Colors::orangeRed},
            {"tomato", Colors::tomato},
            {"coral", Colors::coral},
            {"darkOrange", Colors::darkOrange},
            {"orange", Colors::orange},

            {"yellow", Colors::yellow},
            {"lightYellow", Colors::lightYellow},
            {"lemonChiffon", Colors::lemonChiffon},
            {"lightGoldenrodYellow", Colors::lightGoldenrodYellow},
            {"papayaWhip", Colors::papayaWhip},
            {"moccasin", Colors::moccasin},
            {"peachPuff", Colors::peachPuff},
            {"paleGoldenrod", Colors::paleGoldenrod},
            {"khaki", Colors::khaki},
            {"darkKhaki", Colors::darkKhaki},
            {"gold", Colors::gold},

            {"cornsilk", Colors::cornsilk},
            {"blanchedAlmond", Colors::blanchedAlmond},
            {"bisque", Colors::bisque},
            {"navajoWhite", Colors::navajoWhite},
            {"wheat", Colors::wheat},
            {"burlyWood", Colors::burlyWood},
            {"tan", Colors::tan},
            {"rosyBrown", Colors::rosyBrown},
            {"sandyBrown", Colors::sandyBrown},
            {"goldenrod", Colors::goldenrod},
            {"darkGoldenrod", Colors::darkGoldenrod},
            {"peru", Colors::peru},
            {"chocolate", Colors::chocolate},
            {"saddleBrown", Colors::saddleBrown},
            {"sienna", Colors::sienna},
            {"brown", Colors::brown},
            {"maroon", Colors::maroon},

            {"darkOliveGreen", Colors::darkOliveGreen},
            {"olive", Colors::olive},
            {"oliveDrab", Colors::oliveDrab},
            {"yellowGreen", Colors::yellowGreen},
            {"limeGreen", Colors::limeGreen},
            {"lime", Colors::lime},
            {"lawnGreen", Colors::lawnGreen},
            {"chartreuse", Colors::chartreuse},
            {"greenYellow", Colors::greenYellow},
            {"springGreen", Colors::springGreen},
            {"mediumSpringGreen", Colors::mediumSpringGreen},
            {"lightGreen", Colors::lightGreen},
            {"paleGreen", Colors::paleGreen},
            {"darkSeaGreen", Colors::darkSeaGreen},
            {"mediumSeaGreen", Colors::mediumSeaGreen},
            {"seaGreen", Colors::seaGreen},
            {"forestGreen", Colors::forestGreen},
            {"green", Colors::green},
            {"darkGreen", Colors::darkGreen},

            {"mediumAquamarine", Colors::mediumAquamarine},
            {"aqua", Colors::aqua},
            {"cyan", Colors::cyan},
            {"lightCyan", Colors::lightCyan},
            {"paleTurquoise", Colors::paleTurquoise},
            {"aquamarine", Colors::aquamarine},
            {"turquoise", Colors::turquoise},
            {"mediumTurquoise", Colors::mediumTurquoise},
            {"darkTurquoise", Colors::darkTurquoise},
            {"lightSeaGreen", Colors::lightSeaGreen},
            {"cadetBlue", Colors::cadetBlue},
            {"darkCyan", Colors::darkCyan},
            {"teal", Colors::teal},

            {"lightSteelBlue", Colors::lightSteelBlue},
            {"powderBlue", Colors::powderBlue},
            {"lightBlue", Colors::lightBlue},
            {"skyBlue", Colors::skyBlue},
            {"lightSkyBlue", Colors::lightSkyBlue},
            {"deepSkyBlue", Colors::deepSkyBlue},
            {"dodgerBlue", Colors::dodgerBlue},
            {"cornflowerBlue", Colors::cornflowerBlue},
            {"steelBlue", Colors::steelBlue},
            {"royalBlue", Colors::royalBlue},
            {"blue", Colors::blue},
            {"mediumBlue", Colors::mediumBlue},
            {"darkBlue", Colors::darkBlue},
            {"navy", Colors::navy},
            {"midnightBlue", Colors::midnightBlue},

            {"lavender", Colors::lavender},
            {"thistle", Colors::thistle},
            {"plum", Colors::plum},
            {"violet", Colors::violet},
            {"orchid", Colors::orchid},
            {"fuchsia", Colors::fuchsia},
            {"magenta", Colors::magenta},
            {"mediumOrchid", Colors::mediumOrchid},
            {"mediumPurple", Colors::mediumPurple},
            {"blueViolet", Colors::blueViolet},
            {"darkViolet", Colors::darkViolet},
            {"darkOrchid", Colors::darkOrchid},
            {"darkMagenta", Colors::darkMagenta},
            {"purple", Colors::purple},
            {"indigo", Colors::indigo},
            {"darkSlateBlue", Colors::darkSlateBlue},
            {"rebeccaPurple", Colors::rebeccaPurple},
            {"slateBlue", Colors::slateBlue},
            {"mediumSlateBlue", Colors::mediumSlateBlue},

            {"white", Colors::white},
            {"snow", Colors::snow},
            {"honeydew", Colors::honeydew},
            {"mintCream", Colors::mintCream},
            {"azure", Colors::azure},
            {"aliceBlue", Colors::aliceBlue},
            {"ghostWhite", Colors::ghostWhite},
            {"whiteSmoke", Colors::whiteSmoke},
            {"seashell", Colors::seashell},
            {"beige", Colors::beige},
            {"oldLace", Colors::oldLace},
            {"floralWhite", Colors::floralWhite},
            {"ivory", Colors::ivory},
            {"antiqueWhite", Colors::antiqueWhite},
            {"linen", Colors::linen},
            {"lavenderBlush", Colors::lavenderBlush},
            {"mistyRose", Colors::mistyRose},

            {"gainsboro", Colors::gainsboro},
            {"lightGray", Colors::lightGray},
            {"silver", Colors::silver},
            {"darkGray", Colors::darkGray},
            {"gray", Colors::gray},
            {"dimGray", Colors::dimGray},
            {"lightSlateGray", Colors::lightSlateGray},
            {"slateGray", Colors::slateGray},
            {"darkSlateGray", Colors::darkSlateGray},
            {"black", Colors::black}

        };

        if (auto it = table.find(name); it != table.end()) {
            result = it->second;
            return true;
        }

        if (name.empty() || name.front() != '#')
            return false;

        std::string hex = name.substr(1);
        std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        uint32_t rgba;
        if (false == parseHex(hex, rgba))
            return false;

        result = (Color)rgba;

        return true;
    }

    /**
     * @brief Parses lower-case hex color text (RRGGBB or RRGGBBAA).
     */
    static bool parseHex(const std::string &hex, uint32_t &result) {
        const size_t len = hex.size();
        if (len != 6 && len != 8)
            return false;

        uint32_t value = 0;

        for (char c : hex) {
            value <<= 4;

            if (c >= '0' && c <= '9')
                value |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<uint32_t>(10 + (c - 'a'));
            else
                return false;
        }

        if (len == 6) {
            // hex = RRGGBB
            uint32_t r = (value >> 16) & 0xff;
            uint32_t g = (value >> 8) & 0xff;
            uint32_t b = (value >> 0) & 0xff;

            result = (0xffu << 24) | (b << 16) | (g << 8) | r;
        } else {
            // hex = RRGGBBAA
            uint32_t r = (value >> 24) & 0xff;
            uint32_t g = (value >> 16) & 0xff;
            uint32_t b = (value >> 8) & 0xff;
            uint32_t a = (value >> 0) & 0xff;

            result = (a << 24) | (b << 16) | (g << 8) | r;
        }

        return true;
    }
};
} // namespace opk

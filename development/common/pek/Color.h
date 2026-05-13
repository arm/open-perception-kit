/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/String.h"
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>

namespace pek {

typedef uint32_t Color;

struct Colorf {
    Colorf() {}
    Colorf(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
    Colorf(float r, float g, float b) : r(r), g(g), b(b), a(1.0f) {}

    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

#define COLOR_FROM_RGB_BYTES(r, g, b)                                                              \
    (0xff000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r))

class Colors {
  public:
    static constexpr const Color space = 0x00000000;
    static constexpr const Color transparentWhite = 0x00ffffff;

    static constexpr const Color pink = COLOR_FROM_RGB_BYTES(255, 192, 203);
    static constexpr const Color lightPink = COLOR_FROM_RGB_BYTES(255, 182, 193);
    static constexpr const Color hotPink = COLOR_FROM_RGB_BYTES(255, 105, 180);
    static constexpr const Color deepPink = COLOR_FROM_RGB_BYTES(255, 20, 147);
    static constexpr const Color paleVioletRed = COLOR_FROM_RGB_BYTES(219, 112, 147);
    static constexpr const Color mediumVioletRed = COLOR_FROM_RGB_BYTES(199, 21, 133);

    static constexpr const Color lightSalmon = COLOR_FROM_RGB_BYTES(255, 160, 122);
    static constexpr const Color salmon = COLOR_FROM_RGB_BYTES(250, 128, 114);
    static constexpr const Color darkSalmon = COLOR_FROM_RGB_BYTES(233, 150, 122);
    static constexpr const Color lightCoral = COLOR_FROM_RGB_BYTES(240, 128, 128);
    static constexpr const Color indianRed = COLOR_FROM_RGB_BYTES(205, 92, 92);
    static constexpr const Color crimson = COLOR_FROM_RGB_BYTES(220, 20, 60);
    static constexpr const Color fireBrick = COLOR_FROM_RGB_BYTES(178, 34, 34);
    static constexpr const Color darkRed = COLOR_FROM_RGB_BYTES(139, 0, 0);
    static constexpr const Color red = COLOR_FROM_RGB_BYTES(255, 0, 0);

    static constexpr const Color orangeRed = COLOR_FROM_RGB_BYTES(255, 69, 0);
    static constexpr const Color tomato = COLOR_FROM_RGB_BYTES(255, 99, 71);
    static constexpr const Color coral = COLOR_FROM_RGB_BYTES(255, 127, 80);
    static constexpr const Color darkOrange = COLOR_FROM_RGB_BYTES(255, 140, 0);
    static constexpr const Color orange = COLOR_FROM_RGB_BYTES(255, 165, 0);

    static constexpr const Color yellow = COLOR_FROM_RGB_BYTES(255, 255, 0);
    static constexpr const Color lightYellow = COLOR_FROM_RGB_BYTES(255, 255, 224);
    static constexpr const Color lemonChiffon = COLOR_FROM_RGB_BYTES(255, 250, 205);
    static constexpr const Color lightGoldenrodYellow = COLOR_FROM_RGB_BYTES(250, 250, 210);
    static constexpr const Color papayaWhip = COLOR_FROM_RGB_BYTES(255, 239, 213);
    static constexpr const Color moccasin = COLOR_FROM_RGB_BYTES(255, 228, 181);
    static constexpr const Color peachPuff = COLOR_FROM_RGB_BYTES(255, 218, 185);
    static constexpr const Color paleGoldenrod = COLOR_FROM_RGB_BYTES(238, 232, 170);
    static constexpr const Color khaki = COLOR_FROM_RGB_BYTES(240, 230, 140);
    static constexpr const Color darkKhaki = COLOR_FROM_RGB_BYTES(189, 183, 107);
    static constexpr const Color gold = COLOR_FROM_RGB_BYTES(255, 215, 0);

    static constexpr const Color cornsilk = COLOR_FROM_RGB_BYTES(255, 248, 220);
    static constexpr const Color blanchedAlmond = COLOR_FROM_RGB_BYTES(255, 235, 205);
    static constexpr const Color bisque = COLOR_FROM_RGB_BYTES(255, 228, 196);
    static constexpr const Color navajoWhite = COLOR_FROM_RGB_BYTES(255, 222, 173);
    static constexpr const Color wheat = COLOR_FROM_RGB_BYTES(245, 222, 179);
    static constexpr const Color burlyWood = COLOR_FROM_RGB_BYTES(222, 184, 135);
    static constexpr const Color tan = COLOR_FROM_RGB_BYTES(210, 180, 140);
    static constexpr const Color rosyBrown = COLOR_FROM_RGB_BYTES(188, 143, 143);
    static constexpr const Color sandyBrown = COLOR_FROM_RGB_BYTES(244, 164, 96);
    static constexpr const Color goldenrod = COLOR_FROM_RGB_BYTES(218, 165, 32);
    static constexpr const Color darkGoldenrod = COLOR_FROM_RGB_BYTES(184, 134, 11);
    static constexpr const Color peru = COLOR_FROM_RGB_BYTES(205, 133, 63);
    static constexpr const Color chocolate = COLOR_FROM_RGB_BYTES(210, 105, 30);
    static constexpr const Color saddleBrown = COLOR_FROM_RGB_BYTES(139, 69, 19);
    static constexpr const Color sienna = COLOR_FROM_RGB_BYTES(160, 82, 45);
    static constexpr const Color brown = COLOR_FROM_RGB_BYTES(165, 42, 42);
    static constexpr const Color maroon = COLOR_FROM_RGB_BYTES(128, 0, 0);

    static constexpr const Color darkOliveGreen = COLOR_FROM_RGB_BYTES(85, 107, 47);
    static constexpr const Color olive = COLOR_FROM_RGB_BYTES(128, 128, 0);
    static constexpr const Color oliveDrab = COLOR_FROM_RGB_BYTES(107, 142, 35);
    static constexpr const Color yellowGreen = COLOR_FROM_RGB_BYTES(154, 205, 50);
    static constexpr const Color limeGreen = COLOR_FROM_RGB_BYTES(50, 205, 50);
    static constexpr const Color lime = COLOR_FROM_RGB_BYTES(0, 255, 0);
    static constexpr const Color lawnGreen = COLOR_FROM_RGB_BYTES(124, 252, 0);
    static constexpr const Color chartreuse = COLOR_FROM_RGB_BYTES(127, 255, 0);
    static constexpr const Color greenYellow = COLOR_FROM_RGB_BYTES(173, 255, 47);
    static constexpr const Color springGreen = COLOR_FROM_RGB_BYTES(0, 255, 127);
    static constexpr const Color mediumSpringGreen = COLOR_FROM_RGB_BYTES(0, 250, 154);
    static constexpr const Color lightGreen = COLOR_FROM_RGB_BYTES(144, 238, 144);
    static constexpr const Color paleGreen = COLOR_FROM_RGB_BYTES(152, 251, 152);
    static constexpr const Color darkSeaGreen = COLOR_FROM_RGB_BYTES(143, 188, 143);
    static constexpr const Color mediumSeaGreen = COLOR_FROM_RGB_BYTES(60, 179, 113);
    static constexpr const Color seaGreen = COLOR_FROM_RGB_BYTES(46, 139, 87);
    static constexpr const Color forestGreen = COLOR_FROM_RGB_BYTES(34, 139, 34);
    static constexpr const Color green = COLOR_FROM_RGB_BYTES(0, 128, 0);
    static constexpr const Color darkGreen = COLOR_FROM_RGB_BYTES(0, 100, 0);

    static constexpr const Color mediumAquamarine = COLOR_FROM_RGB_BYTES(102, 205, 170);
    static constexpr const Color aqua = COLOR_FROM_RGB_BYTES(0, 255, 255);
    static constexpr const Color cyan = COLOR_FROM_RGB_BYTES(0, 255, 255);
    static constexpr const Color lightCyan = COLOR_FROM_RGB_BYTES(224, 255, 255);
    static constexpr const Color paleTurquoise = COLOR_FROM_RGB_BYTES(175, 238, 238);
    static constexpr const Color aquamarine = COLOR_FROM_RGB_BYTES(127, 255, 212);
    static constexpr const Color turquoise = COLOR_FROM_RGB_BYTES(64, 224, 208);
    static constexpr const Color mediumTurquoise = COLOR_FROM_RGB_BYTES(72, 209, 204);
    static constexpr const Color darkTurquoise = COLOR_FROM_RGB_BYTES(0, 206, 209);
    static constexpr const Color lightSeaGreen = COLOR_FROM_RGB_BYTES(32, 178, 170);
    static constexpr const Color cadetBlue = COLOR_FROM_RGB_BYTES(95, 158, 160);
    static constexpr const Color darkCyan = COLOR_FROM_RGB_BYTES(0, 139, 139);
    static constexpr const Color teal = COLOR_FROM_RGB_BYTES(0, 128, 128);

    static constexpr const Color lightSteelBlue = COLOR_FROM_RGB_BYTES(176, 196, 222);
    static constexpr const Color powderBlue = COLOR_FROM_RGB_BYTES(176, 224, 230);
    static constexpr const Color lightBlue = COLOR_FROM_RGB_BYTES(173, 216, 230);
    static constexpr const Color skyBlue = COLOR_FROM_RGB_BYTES(135, 206, 235);
    static constexpr const Color lightSkyBlue = COLOR_FROM_RGB_BYTES(135, 206, 250);
    static constexpr const Color deepSkyBlue = COLOR_FROM_RGB_BYTES(0, 191, 255);
    static constexpr const Color dodgerBlue = COLOR_FROM_RGB_BYTES(30, 144, 255);
    static constexpr const Color cornflowerBlue = COLOR_FROM_RGB_BYTES(100, 149, 237);
    static constexpr const Color steelBlue = COLOR_FROM_RGB_BYTES(70, 130, 180);
    static constexpr const Color royalBlue = COLOR_FROM_RGB_BYTES(65, 105, 225);
    static constexpr const Color blue = COLOR_FROM_RGB_BYTES(0, 0, 255);
    static constexpr const Color mediumBlue = COLOR_FROM_RGB_BYTES(0, 0, 205);
    static constexpr const Color darkBlue = COLOR_FROM_RGB_BYTES(0, 0, 139);
    static constexpr const Color navy = COLOR_FROM_RGB_BYTES(0, 0, 128);
    static constexpr const Color midnightBlue = COLOR_FROM_RGB_BYTES(25, 25, 112);

    static constexpr const Color lavender = COLOR_FROM_RGB_BYTES(230, 230, 250);
    static constexpr const Color thistle = COLOR_FROM_RGB_BYTES(216, 191, 216);
    static constexpr const Color plum = COLOR_FROM_RGB_BYTES(221, 160, 221);
    static constexpr const Color violet = COLOR_FROM_RGB_BYTES(238, 130, 238);
    static constexpr const Color orchid = COLOR_FROM_RGB_BYTES(218, 112, 214);
    static constexpr const Color fuchsia = COLOR_FROM_RGB_BYTES(255, 0, 255);
    static constexpr const Color magenta = COLOR_FROM_RGB_BYTES(255, 0, 255);
    static constexpr const Color mediumOrchid = COLOR_FROM_RGB_BYTES(186, 85, 211);
    static constexpr const Color mediumPurple = COLOR_FROM_RGB_BYTES(147, 112, 219);
    static constexpr const Color blueViolet = COLOR_FROM_RGB_BYTES(138, 43, 226);
    static constexpr const Color darkViolet = COLOR_FROM_RGB_BYTES(148, 0, 211);
    static constexpr const Color darkOrchid = COLOR_FROM_RGB_BYTES(153, 50, 204);
    static constexpr const Color darkMagenta = COLOR_FROM_RGB_BYTES(139, 0, 139);
    static constexpr const Color purple = COLOR_FROM_RGB_BYTES(128, 0, 128);
    static constexpr const Color indigo = COLOR_FROM_RGB_BYTES(75, 0, 130);
    static constexpr const Color darkSlateBlue = COLOR_FROM_RGB_BYTES(72, 61, 139);
    static constexpr const Color rebeccaPurple = COLOR_FROM_RGB_BYTES(102, 51, 153);
    static constexpr const Color slateBlue = COLOR_FROM_RGB_BYTES(106, 90, 205);
    static constexpr const Color mediumSlateBlue = COLOR_FROM_RGB_BYTES(123, 104, 238);

    static constexpr const Color white = COLOR_FROM_RGB_BYTES(255, 255, 255);
    static constexpr const Color snow = COLOR_FROM_RGB_BYTES(255, 250, 250);
    static constexpr const Color honeydew = COLOR_FROM_RGB_BYTES(240, 255, 240);
    static constexpr const Color mintCream = COLOR_FROM_RGB_BYTES(245, 255, 250);
    static constexpr const Color azure = COLOR_FROM_RGB_BYTES(240, 255, 255);
    static constexpr const Color aliceBlue = COLOR_FROM_RGB_BYTES(240, 248, 255);
    static constexpr const Color ghostWhite = COLOR_FROM_RGB_BYTES(248, 248, 255);
    static constexpr const Color whiteSmoke = COLOR_FROM_RGB_BYTES(245, 245, 245);
    static constexpr const Color seashell = COLOR_FROM_RGB_BYTES(255, 245, 238);
    static constexpr const Color beige = COLOR_FROM_RGB_BYTES(245, 245, 220);
    static constexpr const Color oldLace = COLOR_FROM_RGB_BYTES(253, 245, 230);
    static constexpr const Color floralWhite = COLOR_FROM_RGB_BYTES(255, 250, 240);
    static constexpr const Color ivory = COLOR_FROM_RGB_BYTES(255, 255, 240);
    static constexpr const Color antiqueWhite = COLOR_FROM_RGB_BYTES(250, 235, 215);
    static constexpr const Color linen = COLOR_FROM_RGB_BYTES(250, 240, 230);
    static constexpr const Color lavenderBlush = COLOR_FROM_RGB_BYTES(255, 240, 245);
    static constexpr const Color mistyRose = COLOR_FROM_RGB_BYTES(255, 228, 225);

    static constexpr const Color gainsboro = COLOR_FROM_RGB_BYTES(220, 220, 220);
    static constexpr const Color lightGray = COLOR_FROM_RGB_BYTES(211, 211, 211);
    static constexpr const Color silver = COLOR_FROM_RGB_BYTES(192, 192, 192);
    static constexpr const Color darkGray = COLOR_FROM_RGB_BYTES(169, 169, 169);
    static constexpr const Color gray = COLOR_FROM_RGB_BYTES(128, 128, 128);
    static constexpr const Color dimGray = COLOR_FROM_RGB_BYTES(105, 105, 105);
    static constexpr const Color lightSlateGray = COLOR_FROM_RGB_BYTES(119, 136, 153);
    static constexpr const Color slateGray = COLOR_FROM_RGB_BYTES(112, 128, 144);
    static constexpr const Color darkSlateGray = COLOR_FROM_RGB_BYTES(47, 79, 79);
    static constexpr const Color black = COLOR_FROM_RGB_BYTES(0, 0, 0);

    static inline bool fromString(const std::string &name, Color &result) {
        Color color;
        if (tryFromString(name, color))
            return true;
        return false;
    }

    static inline Color fromStringOrDefault(const std::string &name,
                                            Color defaultColor = 0xffffff) {
        Color color;
        if (tryFromString(name, color))
            return color;
        return defaultColor;
    }

    static uint8_t getRed(Color color) {
        return ((unsigned char)(color >> 0));
    }
    static uint8_t getGreen(Color color) {
        return ((unsigned char)(color >> 8));
    }
    static uint8_t getBlue(Color color) {
        return ((unsigned char)(color >> 16));
    }
    static uint8_t getAlpha(Color color) {
        return ((unsigned char)(color >> 24));
    }
    static float getRedf(Color color) {
        return ((color) & 0xff) / 255.0f;
    }
    static float getGreenf(Color color) {
        return ((color >> 8) & 0xff) / 255.0f;
    }
    static float getBluef(Color color) {
        return ((color >> 16) & 0xff) / 255.0f;
    }
    static float getAlphaf(Color color) {
        return (color >> 24) / 255.0f;
    }

  private:
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

        if (!pek::utf8::beginsWith(name, "#"))
            return false;

        std::string hex = pek::utf8::removeLeft(pek::utf8::toLowerAscii(name), 1);

        uint32_t rgba;
        if (false == parseHex(hex, rgba))
            return false;

        result = (Color)rgba;

        return true;
    }

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
} // namespace pek
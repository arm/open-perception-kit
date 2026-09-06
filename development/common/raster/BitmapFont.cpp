/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "raster/BitmapFont.h"

namespace pek::raster {
namespace {

constexpr std::uint8_t row(std::uint8_t bits) noexcept {
    return static_cast<std::uint8_t>(bits << 2);
}

constexpr BitmapGlyph makeGlyph(std::uint8_t r0,
                                std::uint8_t r1,
                                std::uint8_t r2,
                                std::uint8_t r3,
                                std::uint8_t r4,
                                std::uint8_t r5,
                                std::uint8_t r6) noexcept {
    return {{0, 0, row(r0), row(r1), row(r2), row(r3), row(r4), row(r5), row(r6), 0, 0, 0}};
}

constexpr BitmapGlyph Blank = makeGlyph(0, 0, 0, 0, 0, 0, 0);
constexpr BitmapGlyph Question = makeGlyph(0b11110, 0b00001, 0b00001, 0b00110, 0b00100, 0, 0b00100);

constexpr BitmapGlyph Digit0 =
    makeGlyph(0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110);
constexpr BitmapGlyph Digit1 =
    makeGlyph(0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110);
constexpr BitmapGlyph Digit2 =
    makeGlyph(0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111);
constexpr BitmapGlyph Digit3 =
    makeGlyph(0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110);
constexpr BitmapGlyph Digit4 =
    makeGlyph(0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010);
constexpr BitmapGlyph Digit5 =
    makeGlyph(0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110);
constexpr BitmapGlyph Digit6 =
    makeGlyph(0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110);
constexpr BitmapGlyph Digit7 =
    makeGlyph(0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000);
constexpr BitmapGlyph Digit8 =
    makeGlyph(0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110);
constexpr BitmapGlyph Digit9 =
    makeGlyph(0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b11100);

constexpr BitmapGlyph LetterA =
    makeGlyph(0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001);
constexpr BitmapGlyph LetterB =
    makeGlyph(0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110);
constexpr BitmapGlyph LetterC =
    makeGlyph(0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110);
constexpr BitmapGlyph LetterD =
    makeGlyph(0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110);
constexpr BitmapGlyph LetterE =
    makeGlyph(0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111);
constexpr BitmapGlyph LetterF =
    makeGlyph(0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000);
constexpr BitmapGlyph LetterG =
    makeGlyph(0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110);
constexpr BitmapGlyph LetterH =
    makeGlyph(0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001);
constexpr BitmapGlyph LetterI =
    makeGlyph(0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110);
constexpr BitmapGlyph LetterJ =
    makeGlyph(0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010, 0b01100);
constexpr BitmapGlyph LetterK =
    makeGlyph(0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001);
constexpr BitmapGlyph LetterL =
    makeGlyph(0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111);
constexpr BitmapGlyph LetterM =
    makeGlyph(0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001);
constexpr BitmapGlyph LetterN =
    makeGlyph(0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001);
constexpr BitmapGlyph LetterO =
    makeGlyph(0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110);
constexpr BitmapGlyph LetterP =
    makeGlyph(0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000);
constexpr BitmapGlyph LetterQ =
    makeGlyph(0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101);
constexpr BitmapGlyph LetterR =
    makeGlyph(0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001);
constexpr BitmapGlyph LetterS =
    makeGlyph(0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110);
constexpr BitmapGlyph LetterT =
    makeGlyph(0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100);
constexpr BitmapGlyph LetterU =
    makeGlyph(0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110);
constexpr BitmapGlyph LetterV =
    makeGlyph(0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100);
constexpr BitmapGlyph LetterW =
    makeGlyph(0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010);
constexpr BitmapGlyph LetterX =
    makeGlyph(0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001);
constexpr BitmapGlyph LetterY =
    makeGlyph(0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100);
constexpr BitmapGlyph LetterZ =
    makeGlyph(0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111);

constexpr BitmapGlyph Percent =
    makeGlyph(0b11001, 0b11010, 0b00010, 0b00100, 0b01000, 0b01011, 0b10011);
constexpr BitmapGlyph Exclaim = makeGlyph(0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100);
constexpr BitmapGlyph Minus = makeGlyph(0, 0, 0, 0b11111, 0, 0, 0);
constexpr BitmapGlyph Plus = makeGlyph(0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0);
constexpr BitmapGlyph Comma = makeGlyph(0, 0, 0, 0, 0, 0b00100, 0b01000);
constexpr BitmapGlyph Dot = makeGlyph(0, 0, 0, 0, 0, 0, 0b00100);
constexpr BitmapGlyph Colon = makeGlyph(0, 0b00100, 0, 0, 0b00100, 0, 0);
constexpr BitmapGlyph Semicolon = makeGlyph(0, 0b00100, 0, 0, 0b00100, 0b00100, 0b01000);
constexpr BitmapGlyph LParen =
    makeGlyph(0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010);
constexpr BitmapGlyph RParen =
    makeGlyph(0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000);
constexpr BitmapGlyph LBracket =
    makeGlyph(0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110);
constexpr BitmapGlyph RBracket =
    makeGlyph(0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110);
constexpr BitmapGlyph LBrace =
    makeGlyph(0b00110, 0b01000, 0b01000, 0b10000, 0b01000, 0b01000, 0b00110);
constexpr BitmapGlyph RBrace =
    makeGlyph(0b01100, 0b00010, 0b00010, 0b00001, 0b00010, 0b00010, 0b01100);
constexpr BitmapGlyph Equal = makeGlyph(0, 0, 0b11111, 0, 0b11111, 0, 0);
constexpr BitmapGlyph Less =
    makeGlyph(0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010);
constexpr BitmapGlyph Greater =
    makeGlyph(0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100, 0b01000);
constexpr BitmapGlyph Slash =
    makeGlyph(0b00001, 0b00010, 0b00010, 0b00100, 0b01000, 0b01000, 0b10000);
constexpr BitmapGlyph Backslash =
    makeGlyph(0b10000, 0b01000, 0b01000, 0b00100, 0b00010, 0b00010, 0b00001);
constexpr BitmapGlyph Underscore = makeGlyph(0, 0, 0, 0, 0, 0, 0b11111);
constexpr BitmapGlyph Hash =
    makeGlyph(0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010);
constexpr BitmapGlyph Star = makeGlyph(0, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101, 0);
constexpr BitmapGlyph At = makeGlyph(0b01110, 0b10001, 0b10111, 0b10101, 0b10111, 0b10000, 0b01111);
constexpr BitmapGlyph Pipe =
    makeGlyph(0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100);

const BitmapGlyph &letterGlyph(char character) noexcept {
    switch (character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A')
                                                 : character) {
    case 'A':
        return LetterA;
    case 'B':
        return LetterB;
    case 'C':
        return LetterC;
    case 'D':
        return LetterD;
    case 'E':
        return LetterE;
    case 'F':
        return LetterF;
    case 'G':
        return LetterG;
    case 'H':
        return LetterH;
    case 'I':
        return LetterI;
    case 'J':
        return LetterJ;
    case 'K':
        return LetterK;
    case 'L':
        return LetterL;
    case 'M':
        return LetterM;
    case 'N':
        return LetterN;
    case 'O':
        return LetterO;
    case 'P':
        return LetterP;
    case 'Q':
        return LetterQ;
    case 'R':
        return LetterR;
    case 'S':
        return LetterS;
    case 'T':
        return LetterT;
    case 'U':
        return LetterU;
    case 'V':
        return LetterV;
    case 'W':
        return LetterW;
    case 'X':
        return LetterX;
    case 'Y':
        return LetterY;
    case 'Z':
        return LetterZ;
    default:
        return Question;
    }
}

} // namespace

bool BitmapFont::hasGlyph(char character) noexcept {
    if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z')) {
        return true;
    }

    switch (character) {
    case ' ':
    case '%':
    case '!':
    case '-':
    case '+':
    case ',':
    case '.':
    case '?':
    case ':':
    case ';':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '=':
    case '<':
    case '>':
    case '/':
    case '\\':
    case '_':
    case '#':
    case '*':
    case '@':
    case '|':
        return true;
    default:
        return false;
    }
}

const BitmapGlyph &BitmapFont::glyph(char character) noexcept {
    if (character >= '0' && character <= '9') {
        switch (character) {
        case '0':
            return Digit0;
        case '1':
            return Digit1;
        case '2':
            return Digit2;
        case '3':
            return Digit3;
        case '4':
            return Digit4;
        case '5':
            return Digit5;
        case '6':
            return Digit6;
        case '7':
            return Digit7;
        case '8':
            return Digit8;
        default:
            return Digit9;
        }
    }

    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
        return letterGlyph(character);
    }

    switch (character) {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
        return Blank;
    case '%':
        return Percent;
    case '!':
        return Exclaim;
    case '-':
        return Minus;
    case '+':
        return Plus;
    case ',':
        return Comma;
    case '.':
        return Dot;
    case '?':
        return Question;
    case ':':
        return Colon;
    case ';':
        return Semicolon;
    case '(':
        return LParen;
    case ')':
        return RParen;
    case '[':
        return LBracket;
    case ']':
        return RBracket;
    case '{':
        return LBrace;
    case '}':
        return RBrace;
    case '=':
        return Equal;
    case '<':
        return Less;
    case '>':
        return Greater;
    case '/':
        return Slash;
    case '\\':
        return Backslash;
    case '_':
        return Underscore;
    case '#':
        return Hash;
    case '*':
        return Star;
    case '@':
        return At;
    case '|':
        return Pipe;
    default:
        return Question;
    }
}

} // namespace pek::raster

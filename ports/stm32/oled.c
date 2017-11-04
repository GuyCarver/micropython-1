/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

//This controls the diyMall 9.6 OLED display.

#if MICROPY_PY_OLED

#include "py/mphal.h"
#include "py/mpconfig.h"
#include "py/nlr.h"
#include "py/misc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/qstr.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "bufhelper.h"
#include "i2c.h"
#include "oled.h"
#include "math.h"
#include "py/objarray.h"
#include "py/objtuple.h"

/// moduleref pyb
/// class diyMall 9.6 OLED display driver.
///
/// The oled class is used to control the diyMall OLED display.
/// The display is a 128x64 blue screen.
///
/// The display must be connected in either the X or Y I2C positions.
///
///     disp = pyb.oled('X')      # if oled is in the X position.
///     disp = pyb.oled('Y')      # if oled is in the Y position.
///
/// Then you may use:
///
///     disp.text((0, 0, 'Hello world!', 1)     # print text to the screen
///

static const mp_obj_t OLED_ADDRESS = MP_ROM_INT(0x3C);  // 011110+SA0+RW - 0x3C or 0x3D

#define SETCONTRAST								0x81
#define DISPLAYALLON_RESUME						0xA4
#define DISPLAYALLON							0xA5
#define NORMALDISPLAY							0xA6
#define INVERTDISPLAY							0xA7
#define DISPLAYOFF								0xAE
#define DISPLAYON								0xAF
#define SETDISPLAYOFFSET						0xD3
#define SETCOMPINS								0xDA
#define SETVCOMDETECT							0xDB
#define SETDISPLAYCLOCKDIV						0xD5
#define SETPRECHARGE							0xD9
#define SETMULTIPLEX							0xA8
#define SETLOWCOLUMN							0x00
#define SETHIGHCOLUMN							0x10
#define SETSTARTLINE							0x40
#define MEMORYMODE								0x20
#define COMSCANINC								0xC0
#define COMSCANDEC								0xC8
#define SEGREMAP								0xA0
#define CHARGEPUMP								0x8D
#define EXTRNALVCC								0x1
#define SWITCHAPVCC								0x2
#define ACTIVATE_SCROLL							0x2F
#define DEACTIVATE_SCROLL						0x2E
#define SET_VERTICAL_SCROLL_AREA				0xA3
#define RIGHT_HORIZONTAL_SCROLL					0x26
#define LEFT_HORIZONTAL_SCROLL					0x27
#define VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL	0x29
#define VERTICAL_AND_LEFT_HORIZONTAL_SCROLL		0x2A
#define OLED_W									128
#define OLED_H									64

//Font data used by _drawchar()
typedef struct _oled_font_data {
    uint width;
    uint height;
    uint start;
    uint end;
    const uint8_t *data;
} oled_font_data;


//Either ST7735 or LCD have created the font. If not use the include instead.
extern const uint8_t font_petme128_8x8[];
//#include "font_petme128_8x8.h"

STATIC oled_font_data DefaultFont = { 8, 8, 32, 127, font_petme128_8x8 };

typedef struct _pyb_oled_obj_t {
    mp_obj_base_t base;

    // hardware control for the LCD
    I2C_HandleTypeDef *i2c;
    mp_obj_array_t *barray;
    uint8_t buffer[(OLED_W * OLED_H / 8) + 1];	//+1 for the data write command at index 0.
    uint8_t rotate;    //rotation 0-3

} pyb_oled_obj_t;

extern int absint( int v );
int _clamp( int min, int max, int value );

STATIC void _writedata( mp_obj_t self_in, uint8_t *data, uint16_t size) {
    pyb_oled_obj_t *self = self_in;

    mp_obj_t argA[6];
    mp_load_method(self->i2c, MP_QSTR_send, argA);	//This sets argA 0 and 1
    if (argA[0]) {
//        argA[1] = self->i2c;
        argA[2] = self->barray;			//Point to the static byte array and set it with our data and size.
        self->barray->len = size;
        self->barray->items = data;
        argA[3] = OLED_ADDRESS;
        mp_call_method_n_kw(2, 0, argA);
    }

//This didn't work.  For some reason it always gave a not ready error (2).
//    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(self->i2c, i2c_addr, data, size, 2000);
//    if (status != HAL_OK) {
//        printf("I2C write status bad %d, %u.\n", status, (uint)self->i2c->ErrorCode);
//        i2c_reset_after_error(self->i2c);
//        mp_hal_raise(status);
//    }
}

STATIC void _writecommand( mp_obj_t self_in, uint8_t command ) {
    uint8_t dataA[2];
    dataA[0] = 0;
    dataA[1] = command;

    _writedata(self_in, dataA, sizeof(dataA));
}

STATIC void _seton( pyb_oled_obj_t *self, bool abTF ) {
    _writecommand(self, abTF ? DISPLAYON : DISPLAYOFF);
}

STATIC void _invert( pyb_oled_obj_t *self, bool abTF ) {
    _writecommand(self, abTF ? INVERTDISPLAY : NORMALDISPLAY);
}

STATIC void _display( pyb_oled_obj_t *self ) {
    _writecommand(self, SETLOWCOLUMN);
    _writecommand(self, SETHIGHCOLUMN);
    _writecommand(self, SETSTARTLINE);
    _writedata(self, self->buffer, sizeof(self->buffer));
}

STATIC void _fill( pyb_oled_obj_t *self, uint8_t data ) {
    memset(self->buffer, data, sizeof(self->buffer));
    self->buffer[0] = 0x40;	//Data write command at the beginning of the buffer.
}

STATIC void _clear( pyb_oled_obj_t *self ) {
    _fill(self, 0);
}

/// Draw a pixel at the given position with the given color.  Color
///  is stored in a 2 byte array.
STATIC void _pixel( pyb_oled_obj_t *self, int8_t x, int8_t y, bool aOn ) {
    if ((0 <= x) && (x < OLED_W) && (0 <= y) && (y < OLED_H)) {
        int8_t tx = x;
        switch (self->rotate) {
            case 1:
                x = OLED_W - y - 1;
                y = tx;
                break;
            case 2:
                x = OLED_W - x - 1;
                y = OLED_H - y - 1;
                break;
            case 3:
                x = y;
                y = OLED_H - tx - 1;
                break;
            default:
                break;
        }

        uint8_t bit = 1 << (y & 0x07);
        int index = x + ((y / 8) * OLED_W) + 1; //Add 1 to skip the data write command we've stored at index 0.
        if (aOn) {
            self->buffer[index] |= bit;
        }
        else {
            self->buffer[index] &= ~bit;
        }
    }
}

void _fillrect( pyb_oled_obj_t *self, int8_t x, int8_t y, int8_t w, int8_t h, bool aOn ) {
    int8_t ex = x + w;
    int8_t ey = y + h;

    for ( uint32_t yy = y; yy < ey; ++yy) {
        for ( uint32_t xx = x; xx < ex; ++xx) {
            _pixel(self, xx, yy, aOn);
        }
    }
}

void _line( pyb_oled_obj_t *self, int8_t px, int8_t py, int8_t ex, int8_t ey, bool aOn ) {
    int8_t dx = ex - px;
    int8_t dy = ey - py;

    if (dx == 0) {
        //Make sure we use the smallest y.
        _fillrect(self, px, dy >= 0 ? py : ey, 1, absint(dy) + 1, aOn);
    }
    else if (dy == 0) {
        _fillrect(self, dx >= 0 ? px : ex, py, absint(dx) + 1, 1, aOn);
    }
    else {
        int inx = dx > 0 ? 1 : -1;
        int iny = dy > 0 ? 1 : -1;

        dx = dx >= 0 ? dx : -dx;
        dy = dy >= 0 ? dy : -dy;
        if (dx >= dy) {
            dy <<= 1;
            int e = dy - dx;
            dx <<= 1;
            while (px != ex) {
                _pixel(self, px, py, aOn);
                if (e >= 0) {
                    py += iny;
                    e -= dx;
                }
                e += dy;
                px += inx;
            }
        }
        else {
            dx <<= 1;
            int e = dx - dy;
            dy <<= 1;
            while (py != ey) {
                _pixel(self, px, py, aOn);
                if (e >= 0) {
                    px += inx;
                    e -= dy;
                }
                e += dx;
                py += iny;
            }
        }
    }
}

/// Draw a character at the gien position using the 2 byte color array.  Pixels come from
///  the given font and are scaled by sx, sy.
STATIC void _drawchar( pyb_oled_obj_t *self, int x, int y, uint ci, bool aOn, oled_font_data *font, int sx, int sy ) {

    if ((font->start <= ci) && (ci <= font->end)) {
        ci = (ci - font->start) * font->width;

        const uint8_t *charA = font->data + ci;
        if ((sx <= 1) && (sy <= 1)) {
            for (uint i = 0; i < font->width; ++i) {
                uint8_t c = *charA++;
                int cy = y;
                for (uint j = 0; j < font->height; ++j) {
                    if (c & 0x01) {
                        _pixel(self, x, cy, aOn);
                    }
                    cy += 1;
                    c >>= 1;
                }
                x += 1;
            }
        } else {
            for (uint i = 0; i < font->width; ++i) {
                uint8_t c = *charA++;
                int cy = y;
                for (uint j = 0; j < font->height; ++j) {
                    if (c & 0x01) {
                        _fillrect(self, x, y, sx, sy, aOn);
                    }
                    cy += sy;
                    c >>= 1;
                }
                x += sx;
            }
        }
    }
}

//0 = off
//1 = left
//2 = right
//3 = diag left
//4 = diag right
STATIC void _scroll( pyb_oled_obj_t *self, uint8_t aDir, uint8_t aStart, uint8_t aEnd ) {
    switch (aDir) {
        case 0:
            _writecommand(self, DEACTIVATE_SCROLL);
            break;
        case 1:
        case 2:
            _writecommand(self, aDir == 1 ? LEFT_HORIZONTAL_SCROLL : RIGHT_HORIZONTAL_SCROLL);
            _writecommand(self, 0x00);
            _writecommand(self, aStart);
            _writecommand(self, 0x00);
            _writecommand(self, aEnd);
            _writecommand(self, 0x00);
            _writecommand(self, 0xFF);
            _writecommand(self, ACTIVATE_SCROLL);
            break;
        case 3:
        case 4:
            _writecommand(self, SET_VERTICAL_SCROLL_AREA);
            _writecommand(self, 0x00);
            _writecommand(self, OLED_H);
            _writecommand(self, aDir == 3 ? VERTICAL_AND_LEFT_HORIZONTAL_SCROLL : VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL);
            _writecommand(self, 0x00);
            _writecommand(self, aStart);
            _writecommand(self, 0x00);
            _writecommand(self, aEnd);
            _writecommand(self, 0x01);
            _writecommand(self, ACTIVATE_SCROLL);
            break;
        default:
            break;
    }
}

/// Hardware reset.
STATIC void _init( pyb_oled_obj_t *self, size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {

    mp_obj_t i2cA[2] = { args[0], mp_obj_new_int(0) }; //Location is passed in, I2C.MASTER is 0.
    //Create i2c object with the port # and master mode.  Assume defaults for all other values.
    self->i2c = pyb_i2c_type.make_new(NULL, 2, 0, i2cA);
//    self->i2c = args[0];

    self->rotate = 0;
    self->barray = mp_obj_new_bytearray(0, NULL);

    _clear(self);

    _writecommand(self, DISPLAYOFF);
    _writecommand(self, SETDISPLAYCLOCKDIV);
    _writecommand(self, 0x80);
    _writecommand(self, SETMULTIPLEX);
    _writecommand(self, 0x3F);
    _writecommand(self, SETDISPLAYOFFSET);
    _writecommand(self, 0x00);
    _writecommand(self, SETSTARTLINE);
    _writecommand(self, CHARGEPUMP);
    _writecommand(self, 0x14);
    _writecommand(self, MEMORYMODE);
    _writecommand(self, 0x00);
    _writecommand(self, SEGREMAP + 0x01);
    _writecommand(self, COMSCANDEC);
    _writecommand(self, SETCOMPINS);
    _writecommand(self, 0x12);
    _writecommand(self, SETCONTRAST);
    _writecommand(self, 0xCF);
    _writecommand(self, SETPRECHARGE);
    _writecommand(self, 0xF1);
    _writecommand(self, SETVCOMDETECT);
    _writecommand(self, 0x40);
    _writecommand(self, DISPLAYALLON_RESUME);
    _writecommand(self, NORMALDISPLAY);
    _writecommand(self, 0xB0);
    _writecommand(self, 0x10);
    _writecommand(self, 0x01);	//Set original position to 0,0.

    _seton(self, true);
    _display(self);
}

//Commands.

/// \method display()
///
/// update display.
STATIC mp_obj_t pyb_oled_display(mp_obj_t self_in) {
    pyb_oled_obj_t *self = self_in;
    _display(self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_oled_display_obj, pyb_oled_display);

/// \method fill(value = 0)
///
/// Fill screen with given value (default = 0).
///
STATIC mp_obj_t pyb_oled_fill( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    uint8_t value = (n_args > 1) ? mp_obj_get_int(args[1]) : 0;
    _fill(self, value);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_fill_obj, 1, 2, pyb_oled_fill);

//Scroll off, left, right, diagleft, diagright, 0, 7

/// \method scroll(dir, start = 0, end = 7)
///
/// Scroll given area in given direction.
///
STATIC mp_obj_t pyb_oled_scroll( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    uint8_t dir = mp_obj_get_int(args[1]);
    uint8_t start = (n_args > 2) ? mp_obj_get_int(args[2]) : 0;
    uint8_t end = (n_args > 3) ? mp_obj_get_int(args[3]) : 7;
    _scroll(self, dir, start, end);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_scroll_obj, 2, 4, pyb_oled_scroll);

/// \method clear()
///
/// Fill display with 0.
STATIC mp_obj_t pyb_oled_clear(mp_obj_t self_in) {
    _clear(self_in);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_oled_clear_obj, pyb_oled_clear);

/// \method on(value)
///
/// Turn the display on/off.  True or 1 turns it on, False or 0 turns it off.
STATIC mp_obj_t pyb_oled_on(mp_obj_t self_in, mp_obj_t value) {
    _seton(self_in, mp_obj_is_true(value));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_oled_on_obj, pyb_oled_on);

/// \method invert(value)
///
/// Set color inverted (black = white).  0 = normal else inverted.
STATIC mp_obj_t pyb_oled_invert(mp_obj_t self_in, mp_obj_t value) {
    _invert(self_in, mp_obj_is_true(value));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_oled_invert_obj, pyb_oled_invert);

/// \method rotation(value)
///
/// Set rotation of oled display.  Valid values are between 0 and 3.
STATIC mp_obj_t pyb_oled_rotation(mp_obj_t self_in, mp_obj_t rotation_in) {
    pyb_oled_obj_t *self = self_in;
    int rotate = mp_obj_get_int(rotation_in) & 0x03;
    self->rotate = rotate;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_oled_rotation_obj, pyb_oled_rotation);

/// \method size()
///
/// Return the size in (w, h) tuple.
STATIC mp_obj_t pyb_oled_size( mp_obj_t self_in ) {
    mp_obj_t sz[] = { mp_obj_new_int(OLED_W), mp_obj_new_int(OLED_H) };
    return mp_obj_new_tuple(2, sz);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_oled_size_obj, pyb_oled_size);

//Key strings for font dictionaries.
STATIC const mp_obj_t k_wobj = MP_ROM_QSTR(MP_QSTR_Width);
STATIC const mp_obj_t k_hobj = MP_ROM_QSTR(MP_QSTR_Height);
STATIC const mp_obj_t k_sobj = MP_ROM_QSTR(MP_QSTR_Start);
STATIC const mp_obj_t k_eobj = MP_ROM_QSTR(MP_QSTR_End);
STATIC const mp_obj_t k_dobj = MP_ROM_QSTR(MP_QSTR_Data);

/// \method text(pos, string, aOn, font, size=1)
///
/// Write the string `str` to the screen.
STATIC mp_obj_t pyb_oled_text(mp_uint_t n_args, const mp_obj_t *args) {
    pyb_oled_obj_t *self = args[0];

    mp_uint_t posLen;
    mp_obj_t *pos;
    mp_obj_tuple_get(args[1], &posLen, &pos);

    uint len;
    const char *data = mp_obj_str_get_data(args[2], &len);
    int x = mp_obj_get_int(pos[0]);
    int y = mp_obj_get_int(pos[1]);
    bool bon = mp_obj_is_true(args[3]);
    uint sx = 1;
    uint sy = 1;

    oled_font_data font;
    bool bfontSet = false;

    //If font is given then read the data from the font dict.
    if (n_args >= 5) {
        if (MP_OBJ_IS_TYPE(args[4], &mp_type_dict)) {
            mp_obj_dict_t *fontd = args[4];
            mp_obj_t arg = mp_obj_dict_get(fontd, k_wobj);
            if (arg != MP_OBJ_NULL) {
                font.width = mp_obj_get_int(arg);
                arg = mp_obj_dict_get(fontd, k_hobj);
                if (arg != MP_OBJ_NULL) {
                    font.height = mp_obj_get_int(arg);
                    arg = mp_obj_dict_get(fontd, k_sobj);
                    if (arg != MP_OBJ_NULL) {
                        font.start = mp_obj_get_int(arg);
                        arg = mp_obj_dict_get(fontd, k_eobj);
                        if (arg != MP_OBJ_NULL) {
                            font.end = mp_obj_get_int(arg);
                            arg = mp_obj_dict_get(fontd, k_dobj);
                            if (arg != MP_OBJ_NULL) {
                                mp_buffer_info_t bufinfo;
                                mp_get_buffer(arg, &bufinfo, MP_BUFFER_READ);
                                font.data = bufinfo.buf;
                                bfontSet = true;
                            }
                        }
                    }
                }
            }
        }

        //If size value given get data from either tuple or single integer.
        if (n_args >= 6) {
            if (MP_OBJ_IS_TYPE(args[5], &mp_type_tuple)) {
                mp_obj_t *fsize;
                mp_obj_tuple_get(args[5], &posLen, &fsize);
                sx = mp_obj_get_int(fsize[0]);
                sy = mp_obj_get_int(fsize[1]);
            } else if (MP_OBJ_IS_INT(args[5])) {
                sx = mp_obj_get_int(args[5]);
                sy = sx;
            }
        }
    }

    //If no font set use the default.
    if (!bfontSet) font = DefaultFont;

    int px = x;
    uint width = font.width * sx;
    uint height = font.height * sy + 1;    //Add 1 to keep lines separated by 1 line.
    for (uint i = 0; i < len; ++i) {
        _drawchar(self, px, y, *data++, bon, &font, sx, sy);
        px += width;
        if (px + width > OLED_W) {
            y += height;
            if (y > OLED_H) break;
            px = x;
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_text_obj, 4, 6, pyb_oled_text);

/// \method pixel(pos, onoff)
///
/// Set the pixel at (x, y) on/off.
///
STATIC mp_obj_t pyb_oled_pixel(mp_obj_t self_in, mp_obj_t pos_in, mp_obj_t aOn) {
    pyb_oled_obj_t *self = self_in;

    mp_uint_t posLen;
    mp_obj_t *pos;
    mp_obj_tuple_get(pos_in, &posLen, &pos);
    int px = mp_obj_get_int(pos[0]);
    int py = mp_obj_get_int(pos[1]);
    bool bon = mp_obj_is_true(aOn);

    _pixel(self, px, py, bon);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_oled_pixel_obj, pyb_oled_pixel);
// STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_pixel_obj, 3, 3, pyb_oled_pixel);

/// \method line(start, end, aOn)
///
/// Draws a line from start to end.
STATIC mp_obj_t pyb_oled_line( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    mp_uint_t posLen;
    mp_obj_t *pos;

    mp_obj_tuple_get(args[1], &posLen, &pos);
    int px = mp_obj_get_int(pos[0]);
    int py = mp_obj_get_int(pos[1]);
    mp_obj_tuple_get(args[2], &posLen, &pos);
    int ex = mp_obj_get_int(pos[0]);
    int ey = mp_obj_get_int(pos[1]);
    bool bon = mp_obj_is_true(args[3]);

    _line(self, px, py, ex, ey, bon);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_line_obj, 4, 4, pyb_oled_line);

/// \method rect(start, size, on/off)
///
/// Draw rectangle at start for size.
///
STATIC mp_obj_t pyb_oled_rect( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    mp_uint_t posLen;
    mp_obj_t *pos;
    mp_obj_t *size;
    mp_obj_tuple_get(args[1], &posLen, &pos);
    mp_obj_tuple_get(args[2], &posLen, &size);
    bool bon = mp_obj_is_true(args[3]);

    int px = mp_obj_get_int(pos[0]);
    int py = mp_obj_get_int(pos[1]);
    int ex = px + mp_obj_get_int(size[0]) - 1;
    int ey = px + mp_obj_get_int(size[1]) - 1;

    _line(self, px, py, px, ey, bon);
    _line(self, px, py, ex, py, bon);
    _line(self, px, ey, ex, ey, bon);
    _line(self, ex, py, ex, ey, bon);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_rect_obj, 4, 4, pyb_oled_rect);

/// \method fillrect(start, size, on/off)
///
/// Fill rectangle at start for size with colour.
///
STATIC mp_obj_t pyb_oled_fillrect( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    mp_uint_t posLen;
    mp_obj_t *pos;
    mp_uint_t sizeLen;
    mp_obj_t *size;
    mp_obj_tuple_get(args[1], &posLen, &pos);
    mp_obj_tuple_get(args[2], &sizeLen, &size);
    bool bon = mp_obj_is_true(args[3]);

    int px = mp_obj_get_int(pos[0]);
    int py = mp_obj_get_int(pos[1]);
    int sx = mp_obj_get_int(size[0]);
    int sy = mp_obj_get_int(size[1]);

    _fillrect(self, px, py, sx, sy, bon);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_fillrect_obj, 4, 4, pyb_oled_fillrect);

/// \method circle(start, radius, on/off)
///
/// Draw cricle of given radius.
///
STATIC mp_obj_t pyb_oled_circle( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    mp_uint_t posLen;
    mp_obj_t *pos;
    mp_obj_tuple_get(args[1], &posLen, &pos);
    int px = mp_obj_get_int(pos[0]);
    int py = mp_obj_get_int(pos[1]);
    int rad = mp_obj_get_int(args[2]);
    bool bon = mp_obj_is_true(args[3]);

    int xend = ((rad * 724) >> 10) + 1; //.7071 * 1024 = 724. >> 10 = / 1024
    float rsq = rad * rad;
    for (int x = 0; x < xend; ++x) {
        float fy = sqrtf(rsq - (float)(x * x));

        int y = (int)fy;
        int xp = px + x;
        int yp = py + y;
        int xn = px - x;
        int yn = py - y;
        int xyp = px + y;
        int yxp = py + x;
        int xyn = px - y;
        int yxn = py - x;

        _pixel(self, xp, yp, bon);
        _pixel(self, xp, yn, bon);
        _pixel(self, xn, yp, bon);
        _pixel(self, xn, yn, bon);
        _pixel(self, xyp, yxp, bon);
        _pixel(self, xyp, yxn, bon);
        _pixel(self, xyn, yxp, bon);
        _pixel(self, xyn, yxn, bon);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_circle_obj, 4, 4, pyb_oled_circle);

/// \method fillcircle(start, radius, on/off)
///
/// Draw filled cricle of given radius with colour.
///
STATIC mp_obj_t pyb_oled_fillcircle( mp_uint_t n_args, const mp_obj_t *args ) {
    pyb_oled_obj_t *self = args[0];
    mp_uint_t posLen;
    mp_obj_t *pos;
    mp_obj_tuple_get(args[1], &posLen, &pos);
    int px = mp_obj_get_int(pos[0]);
    int py = mp_obj_get_int(pos[1]);
    int rad = mp_obj_get_int(args[2]);
    bool bon = mp_obj_is_true(args[3]);

    float rsq = rad * rad;

    for (int x = 0; x < rad; ++x) {
        float fy = sqrtf(rsq - (float)(x * x));
        int y = (int)fy;
        int y0 = py - y;
        int x0 = _clamp(0, OLED_W, px + x);
        int x1 = _clamp(0, OLED_W, px - x);

        int ey = _clamp(0, OLED_H, y0 + y * 2);
        y0 = _clamp(0, OLED_H, y0);
        int len = absint(ey - y0) + 1;

        _line(self, x0, y0, 1, len, bon);
        _line(self, x1, y0, 1, len, bon);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_oled_fillcircle_obj, 4, 4, pyb_oled_fillcircle);

STATIC mp_obj_t pyb_oled_i2c( mp_obj_t self_in ) {
    pyb_oled_obj_t *self = self_in;
    return self->i2c;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_oled_i2c_obj, pyb_oled_i2c);

STATIC mp_obj_t pyb_oled_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    pyb_oled_obj_t *self = args[0];
    _init(self, n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pyb_oled_init_obj, 1, pyb_oled_init);

/// \method deinit()
/// Turn off the I2C bus.
STATIC mp_obj_t pyb_oled_deinit(mp_obj_t self_in) {
    pyb_oled_obj_t *self = self_in;
    free(self->barray);
//TODO: call deinit on pi2c?  Need to at least release it or something.
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_oled_deinit_obj, pyb_oled_deinit);

/// \classmethod \constructor(i2c_position)
///
/// Construct an oled display object in the given position.  `position` can be 'X', 0, 'Y' or 1, and
/// should match the i2c position where the OLED display is plugged in.
STATIC mp_obj_t pyb_oled_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    // create oled object
    pyb_oled_obj_t *oled = m_new_obj(pyb_oled_obj_t);
    oled->base.type = &pyb_oled_type;

     mp_map_t kw_args;
     mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    _init(oled, n_args, args, &kw_args);

    return oled;
}

STATIC const mp_rom_map_elem_t pyb_oled_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&pyb_oled_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pyb_oled_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_display), MP_ROM_PTR(&pyb_oled_display_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&pyb_oled_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&pyb_oled_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c), MP_ROM_PTR(&pyb_oled_i2c_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&pyb_oled_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_invertcolor), MP_ROM_PTR(&pyb_oled_invert_obj) },
    { MP_ROM_QSTR(MP_QSTR_rotation), MP_ROM_PTR(&pyb_oled_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&pyb_oled_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&pyb_oled_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&pyb_oled_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&pyb_oled_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&pyb_oled_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_fillrect), MP_ROM_PTR(&pyb_oled_fillrect_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&pyb_oled_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_fillcircle), MP_ROM_PTR(&pyb_oled_fillcircle_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll), MP_ROM_PTR(&pyb_oled_scroll_obj) },
    //Scroll options off, left, right, diagleft, diagright.
    { MP_ROM_QSTR(MP_QSTR_STOP), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_LEFT), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_DIAGLEFT), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_DIAGRIGHT), MP_ROM_INT(4) }
};

STATIC MP_DEFINE_CONST_DICT(pyb_oled_locals_dict, pyb_oled_locals_dict_table);

const mp_obj_type_t pyb_oled_type = {
    { &mp_type_type },
    .name = MP_QSTR_oled,
    .make_new = pyb_oled_make_new,
    .locals_dict = (mp_obj_dict_t*)&pyb_oled_locals_dict
};

#endif //MICROPY_PY_OLED

//----------------------------------------------------------------------
// Copyright (c) 2017, Guy Carver
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//
//     * The name of Guy Carver may not be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// FILE    mpu6050.c
// BY      Guy Carver
// DATE    09/30/2017 09:26 PM
//----------------------------------------------------------------------

//This file is for controlling the MPU6050 tripple axis accelerometer.

#if MICROPY_PY_MPU6050
This file is currently turned off.  Change the define for MICROPY_PY_MPU6050 in the makefile.

#include <stdio.h>
//#include <string.h>
#include "py/mpconfig.h"
#include "py/nlr.h"
#include "py/misc.h"
#include <stdint.h>

#include "py/qstr.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "pin.h"
#include "genhdr/pins.h"
#include "bufhelper.h"
#include "i2c.h"
#include "mphalport.h"
#include "mpu6050.h"
#include "math.h"
#include "py/objtuple.h"

#define ID_LOW 0x68  //#address pin low (GND), default for InvenSense evaluation board
#define ID_HIGH 0x69
#define RA_GYRO_CONFIG  0x1B
#define RA_ACCEL_CONFIG 0x1C
#define RA_ACCEL_XOUT_H 0x3B
#define RA_TEMP_OUT_H   0x41
#define RA_GYRO_XOUT_H  0x43
#define RA_PWR_MGMT_1   0x6B
#define RA_PWR_MGMT_2   0x6C

#define GCONFIG_FS_SEL_BIT     4
#define GCONFIG_FS_SEL_LENGTH  2
#define ACONFIG_FS_SEL_BIT     4
#define ACONFIG_FS_SEL_LENGTH  2
#define PWR1_SLEEP_BIT         6
#define PWR1_CLKSEL_BIT        2
#define PWR1_CLKSEL_LENGTH     3
#define CLOCK_PLL_XGYRO        0x01

STATIC float caccel( int16_t aVal ) {
    return (float)aVal;
}

STATIC float cgyro( int16_t aVal ) {
    return (float)aVal / 131.0f;
}

STATIC float ctemp( int16_t aVal ) {
    return (float)aVal / 154.0f + 24.87f;
}

typedef struct _pyb_mpu6050_obj_t {
    mp_obj_base_t base;

    I2C_HandleTypeDef *i2c;
    uint32_t id;
    mp_obj_t idobj;
    uint8_t data[14];
} pyb_mpu6050_obj_t;

typedef float (*convfn)( int16_t );

convfn cfns[] = { caccel, caccel, caccel, ctemp, cgyro, cgyro, cgyro };

STATIC void _writedata( mp_obj_t self_in, mp_obj_t address, mp_obj_t data, mp_obj_t size ) {
    pyb_mpu6050_obj_t *self = self_in;
    mp_obj_t argA[6];
    argA[0] = 0;
    mp_load_method(self->i2c, MP_QSTR_send, argA);  //This sets argA 0 and 1.
    if (argA[0]) {
//        argA[1] = self->i2c;
        argA[2] = self->barray;
        self->barray->len = size;
        self->barray->items = data;
        argA[3] = address;
        mp_call_method_n_kw(2, 0, argA);
    }
}

STATIC void _writecommand( mp_obj_t self_in, mp_obj_t address, mp_obj_t command ) {
    _writedata(self_in, address, command);
}

STATIC void _readdata( mp_obj_t self_in, uint16_t address, uint8_t *data, uint16_t size) {
    pyb_mpu6050_obj_t *self = self_in;

    //TODO: Use the MP_QSTR_recieve command.
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(self->i2c, address, data, size, 0);

    if (status != HAL_OK) {
        mp_hal_raise(status);
    }
}

STATIC void _writebits( mp_obj_t self_in, uint16_t address, uint8_t start, uint8_t len, uint8_t value ) {
    uint8_t data;
    _readdata(self_in, address, &data, 1);
    uint8_t mask = ((1 << len) - 1) << (start - len + 1);
    value = (value << (start - len + 1)) & mask; //shift data into correct position
    data = (data & (~mask)) | value;
    _writedata(self_in, mp_obj_new_int_from_uint(address), &data, 1);
}

//STATIC uint16_t _read16bit( mp_obj_t self_in, uint16_t address) {
//    uint8_t bytedata[2];
//    _readdata(self_in, address, bytedata, 2);
//    return (bytedata[0] << 8) | bytedata[1];
//}
//
//STATIC uint8_t _readbits( mp_obj_t self_in, uint8_t start, uint8_t len, uint16_t address ) {
//    uint8_t data;
//    _readdata(self_in, address, &data, 1);
//    return (data >> (start - len + 1)) & ((1 << len) - 1);
//}

/// \method setclocksource(value)
///
/// Set the clock source.
STATIC mp_obj_t setclocksource(mp_obj_t self_in, mp_obj_t value) {
    _writebits(self_in, RA_PWR_MGMT_1, PWR1_CLKSEL_BIT, PWR1_CLKSEL_LENGTH, mp_obj_get_int(value));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_mpu6050_setclocksource_obj, setclocksource);

/// \method setfullscalegrange(value)
///
/// set full scale gyro range.
STATIC mp_obj_t setfullscalegrange(mp_obj_t self_in, mp_obj_t value) {
    _writebits(self_in, RA_GYRO_CONFIG, GCONFIG_FS_SEL_BIT, GCONFIG_FS_SEL_LENGTH, mp_obj_get_int(value));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_mpu6050_setfullscalegrange_obj, setfullscalegrange);

/// \method setfullscalearange(value)
///
/// set full scale accel range.
STATIC mp_obj_t setfullscalearange(mp_obj_t self_in, mp_obj_t value) {
    _writebits(self_in, RA_ACCEL_CONFIG, ACONFIG_FS_SEL_BIT, ACONFIG_FS_SEL_LENGTH, mp_obj_get_int(value));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_mpu6050_setfullscalearange_obj, setfullscalearange);

/// \method setsleepenabled(value)
///
/// set sleep enabled
STATIC mp_obj_t setsleepenabled(mp_obj_t self_in, mp_obj_t value) {
    uint8_t v = mp_obj_get_int(value) ? 0 : 1;
    _writebits(self_in, RA_PWR_MGMT_1, PWR1_SLEEP_BIT, 1, v);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(pyb_mpu6050_setsleepenabled_obj, setsleepenabled);

STATIC void _init( pyb_mpu6050_obj_t *self, size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {

    self->idobj = args[0];
    self->id = ID_LOW;

    if (MP_OBJ_IS_INT(self->idobj)) {
        uint32_t val = mp_obj_get_int(id_obj);
        if (val != 0) self->id = ID_HIGH;
    }
    else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "id should be 0 or 1"));
    }

    mp_obj_t zero = mp_obj_new_int(0);
    mp_obj_t i2cA[2] = { args[1], zero }; //Location is passed in, I2C.MASTER is 0.
    //Create i2c object with the port # and master mode.  Assume defaults for all other values.
    self->i2c = pyb_i2c_type.make_new(NULL, 2, 0, i2cA);
//    self->i2c = args[0];

//    setclocksource(mpu, mp_obj_new_int(CLOCK_PLL_XGYRO));
//    printf("SetClockSource\n");

//    mp_obj_t zr = mp_obj_new_int(0);
//    setfullscalegrange(mpu, zr);
//    setfullscalearange(mpu, zr);
//    setsleepenabled(mpu, zr);

//    printf("Wake up\n");
    _writecommand(self, mp_obj_new_int(RA_PWR_MGMT_1), zero);
}

STATIC mp_obj_t pyb_mpu6050_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    pyb_mpu6050_obj_t *self = args[0];
    _init(self, n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pyb_mpu6050_init_obj, 1, pyb_mpu6050_init);

/// \method deinit()
/// Turn off the I2C bus.
STATIC mp_obj_t pyb_mpu6050_deinit(mp_obj_t self_in) {
    pyb_mpu6050_obj_t *self = self_in;
    free(self->barray);
//TODO: call deinit on pi2c?  Need to at least release it or something.
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_mpu6050_deinit_obj, pyb_mpu6050_deinit);

/// \classmethod \constructor(Address 0 or 1, I2C port 1 or 2)
///
/// Construct an mpu6050 object in the given position using address 0x68 or 0x69.
STATIC mp_obj_t pyb_mpu6050_make_new(const mp_obj_type_t *type_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    // create mpu object
    pyb_mpu6050_obj_t *mpu = m_new_obj(pyb_mpu6050_obj_t);

     mp_map_t kw_args;
     mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    _init(oled, n_args, args, &kw_args);

    return mpu;
}

STATIC const mp_rom_map_elem_t pyb_mpu6050_locals_dict_table[] = {
    //instance methods.
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&pyb_mpu6050_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pyb_mpu6050_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_setclocksource), MP_ROM_PTR(&pyb_mpu6050_setclocksource_obj) },
    { MP_ROM_QSTR(MP_QSTR_setfullscalegrange), MP_ROM_PTR(&pyb_mpu6050_setfullscalegrange_obj) },
    { MP_ROM_QSTR(MP_QSTR_setfullscalearange), MP_ROM_PTR(&pyb_mpu6050_setfullscalearange_obj) },
    { MP_ROM_QSTR(MP_QSTR_setsleepenabled), MP_ROM_PTR(&pyb_mpu6050_setsleepenabled_obj) }
};

STATIC MP_DEFINE_CONST_DICT(pyb_mpu6050_locals_dict, pyb_mpu6050_locals_dict_table);

const mp_obj_type_t pyb_mpu6050_type = {
    { &mp_type_type },
    .name = MP_QSTR_MPU6050,
    .make_new = pyb_mpu6050_make_new,
    .locals_dict = (mp_obj_dict_t)&pyb_mpu6050_locals_dict,
};

#endif //MICROPY_PY_MPU6050

/*
*/

#if MICROPY_PY_MPU6050

#include <std.h>
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
#include "mphal.h"
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
    uint8_t data[14];
} pyb_mpu6050_obj_t;

typedef float (*convfn)( int16_t );

convfn cfns[] = { caccel, caccel, caccel, ctemp, cgyro, cgyro, cgyro };

STATIC void _writedata( mp_obj_t self_in, uint16_t address, uint8_t *data, uint16_t size) {
    pyb_mpu6050_obj_t *self = self_in;

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(self->i2c, address, data, size, 200);
//    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(self->i2c, self->id, address, I2C_MEMADD_SIZE_8BIT, data, size, 200);

    if (status != HAL_OK) {
        printf("I2C write status bad %d, %d.\n", status, self->i2c->ErrorCode);
        mp_hal_raise(status);
    }
}

//STATIC void _write16bit( mp_obj_t self_in, uint16_t value, uint16_t address) {
//    uint8_t bytedata[2];
//    bytedata[0] = (uint8_t)(value >> 8);
//    bytedata[1] = (uint8_t)value;
//    _writedata(self_in, bytedata, 2, address);
//}

STATIC void _writecommand( mp_obj_t self_in, uint16_t address, uint8_t command ) {
    _writedata(self_in, address, &command, 1);
}

STATIC void _readdata( mp_obj_t self_in, uint16_t address, uint8_t *data, uint16_t size) {
    pyb_mpu6050_obj_t *self = self_in;

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
    _writedata(self_in, address, &data, 1);
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

/// \classmethod \constructor(I2C port "x" or "y", Address 0 or 1)
///
/// Construct an mpu6050 object in the given position using address 0x68 or 0x69.
STATIC mp_obj_t pyb_mpu6050_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    // get position
    const char *mpu_loc = mp_obj_str_get_str(args[0]);

    mp_obj_t id_obj = args[1];
    uint32_t id = ID_LOW;

    if (MP_OBJ_IS_INT(id_obj)) {
        uint32_t val = mp_obj_get_int(id_obj);
        if (val != 0) id = ID_HIGH;
    }
    else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "id should be 0 or 1"));
    }

    // create mpu object
    pyb_mpu6050_obj_t *mpu = m_new_obj(pyb_mpu6050_obj_t);

    mpu->id = id;
    printf("Device %u\n", (unsigned int)id);

    if ((mpu_loc[0] | 0x20) == 'x' && mpu_loc[1] == '\0') {
        mpu->i2c = &I2CHandle1;
    } else if ((mpu_loc[0] | 0x20) == 'y' && mpu_loc[1] == '\0') {
        mpu->i2c = &I2CHandle2;
    } else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "I2C bus '%s' not x or y", mpu_loc));
    }

    printf("starting I2C\n");

    I2C_InitTypeDef *init = &mpu->i2c->Init;
    init->AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    init->ClockSpeed      = 400000;
    init->DualAddressMode = I2C_DUALADDRESS_DISABLED;
    init->DutyCycle       = I2C_DUTYCYCLE_16_9;
    init->GeneralCallMode = I2C_GENERALCALL_DISABLED;
    init->NoStretchMode   = I2C_NOSTRETCH_DISABLED;
    init->OwnAddress1     = PYB_I2C_MASTER_ADDRESS;
    init->OwnAddress2     = 0xfe; // unused
    i2c_init(mpu->i2c);
    printf("I2C good\n");

    HAL_StatusTypeDef status;
    bool bok = false;

    for ( int i = 0; i < 10; ++i) {
        status = HAL_I2C_IsDeviceReady(mpu->i2c, id, 10, 200);
        printf("status: %d\n", status);
        if (status == HAL_OK) {
            bok = true;
            break;
        }
        HAL_Delay(30);
    }

    printf(bok ? "device ready: %d\n" : "device not ready: %d\n", mpu->i2c->State);

    _writecommand(mpu, RA_PWR_MGMT_1, 0);
    printf("Wake up\n");

//    setclocksource(mpu, mp_obj_new_int(CLOCK_PLL_XGYRO));
//    printf("SetClockSource\n");

//    mp_obj_t zr = mp_obj_new_int(0);
//    setfullscalegrange(mpu, zr);
//    setfullscalearange(mpu, zr);
//    setsleepenabled(mpu, zr);

    return mpu;
}

STATIC const mp_map_elem_t pyb_mpu6050_locals_dict_table[] = {
    //instance methods.
    { MP_OBJ_NEW_QSTR(MP_QSTR_setclocksource), (mp_obj_t)&pyb_mpu6050_setclocksource_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_setfullscalegrange), (mp_obj_t)&pyb_mpu6050_setfullscalegrange_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_setfullscalearange), (mp_obj_t)&pyb_mpu6050_setfullscalearange_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_setsleepenabled), (mp_obj_t)&pyb_mpu6050_setsleepenabled_obj }
};

STATIC MP_DEFINE_CONST_DICT(pyb_mpu6050_locals_dict, pyb_mpu6050_locals_dict_table);

const mp_obj_type_t pyb_mpu6050_type = {
    { &mp_type_type },
    .name = MP_QSTR_MPU6050,
    .make_new = pyb_mpu6050_make_new,
    .locals_dict = (mp_obj_t)&pyb_mpu6050_locals_dict,
};

#endif //MICROPY_PY_MPU6050

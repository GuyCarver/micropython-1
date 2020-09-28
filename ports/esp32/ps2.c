//----------------------------------------------------------------------
// Copyright (c) 2018, gcarver
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//	 * Redistributions of source code must retain the above copyright notice,
//	   this list of conditions and the following disclaimer.
//
//	 * Redistributions in binary form must reproduce the above copyright notice,
//	   this list of conditions and the following disclaimer in the documentation
//	   and/or other materials provided with the distribution.
//
//	 * The name of Guy Carver may not be used to endorse or promote products derived
//	   from this software without specific prior written permission.
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
//----------------------------------------------------------------------

#include <string.h>
#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "mphalport.h"
#include "modmachine.h"

//NOTE: A number of delays and other seemingly superfluous calls may not be necessary of may be too long.
// I didn't test the tweaking of these values too much as I was in a hurry.
// Optimally the delay times could be set on class variables and used rather than hard coding them.

//TODO: Implement button name using qstr lookup in constants array and qstr_str()?  Either that or just return the qstr.

const mp_obj_type_t machine_ps2_type;
mp_rom_map_elem_t getinputname( unsigned int aIndex );

//Bit 0 indicates button on/off state.  Bit 1 indicates change state between current and previous on/off value.
typedef enum {
	_UP,
	_DOWN,										//Button is down.
	_RELEASED,									//Indicate button was just released.
	_PRESSED,									//Indicate button was just pressed.
} ps2_button_states;

//_buttons array indexes
typedef enum {
	_SELECT,
	_L_HAT,
	_R_HAT,
	_START,
	_DPAD_U,
	_DPAD_R,
	_DPAD_D,
	_DPAD_L,
	_L_TRIGGER,
	_R_TRIGGER,
	_L_SHOULDER,
	_R_SHOULDER,
	_TRIANGLE,
	_CIRCLE,
	_CROSS,
	_SQUARE
} ps2_buttons;

//_joys array indexes. Note these need to be anded with 0xF as they start at 16 to start at the end of the buttons enums.
typedef enum {
	_RX = 0x10,
	_RY,
	_LX,
	_LY
} ps2_axis;

unsigned char cmd_qmode[] = {1,0x41,0,0,0};	   //Add the below bytes in to read analog (analog button mode needs to be set)
unsigned char cmd_qdata[] = {1,0x42,0,0,0,0,0,0,0}; //,0,0,0,0,0,0,0,0,0,0,0,0,0)
unsigned char cmd_enter_config[] = {1,0x43,0,1,0};
unsigned char cmd_exit_config[] = {1,0x43,0,0,0x5A,0x5A,0x5A,0x5A,0x5A};
unsigned char cmd_set_mode[] = {1,0x44,0,1,3,0,0,0,0}; //1 = analog stick mode, 3 = lock mode button.
//unsigned char cmd_ds2_native = {1,0x4F,0,0xFF,0xFF,03,00,00,00};
//unsigned char cmd_enable_analog = {1,0x4F,0,0xFF,0xFF,3,0,0,0}; //enable analog pressure input from button.
//unsigned char cmd_enable_rumble = {0x01,0x4D,0,0,1};
//unsigned char cmd_type_read= {1,0x45,0,0,0,0,0,0,0};

typedef struct _machine_ps2_obj_t {
	mp_obj_base_t base;

	mp_obj_t _cmd;								//Command pin.
	mp_obj_t _data;								//Data pin.
	mp_obj_t _clk;								//Clock pin to handle timing of send/receive.
	mp_obj_t _att;								//Attention pin used to tell controller communication will occur.

	mp_obj_t _callback;							//Callback function used when button changes.

	unsigned int _prevbuttons;					//Holds previous button state.
	int _joys[4];								//Joystick axis values +/- 255 with 0 as center.
	unsigned char _buttons[16];					//Button values.  See ps2_button_states.
	unsigned char _res[sizeof(cmd_qdata)];		//Buffer for reading data. (Must be 22 if qdata is larger and we are reading analog button data.

} machine_ps2_obj_t;

//Set value of given pin.
STATIC void _setpin( mp_obj_t aPin, int aValue ) {
	mp_obj_t value = aValue ? mp_const_true : mp_const_false;
	machine_pin_type.call(aPin, 1, 0, &value);
}

//Get value of given pin.
STATIC int _getpin( mp_obj_t Pin ) {
	return mp_obj_get_int(machine_pin_type.call(Pin, 0, 0, NULL));
}

//Send given data and receive into _res array.
STATIC void _sendrcv( machine_ps2_obj_t *self, const unsigned char *apData, unsigned int aLen ) {
	_setpin(self->_att, 0);						//Set self->_att to 0 to tell controller we are going to send.
	mp_hal_delay_us(1);

	//Loop through all of the characters and send them.
	for ( uint32_t i = 0; i < aLen; ++i) {
		unsigned char value = 0;
		unsigned char snd = apData[i];

		for ( uint32_t j = 0; j < 8; ++j) {
			//Set self->_cmd to high if snd & 1
			_setpin(self->_cmd, snd & 1);
			snd >>= 1;
			_setpin(self->_clk, 0);				//Set self->_clk low.
			mp_hal_delay_us(5);					//Delay must be at least 5 to work.
			value |= _getpin(self->_data) << j;
			_setpin(self->_clk, 1);				//set self->_clk high.
			mp_hal_delay_us(5);					//Delay must be at least 5 to work.
		}
		self->_res[i] = value;					//Store the read value into result buffer.
	}
	_setpin(self->_att, 1);						//Set self->_att to 1.
	mp_hal_delay_us(3);							//Delay just in case.
}

//Read data and process into _buttons and _joys arrays.
STATIC void _qdata( machine_ps2_obj_t *self ) {
	_sendrcv(self, cmd_qdata, sizeof(cmd_qdata));

	//Double buffer button input so we can check for state changes.
	unsigned int prev = self->_prevbuttons;
	unsigned int b = self->_res[3] | (self->_res[4] << 8);
	self->_prevbuttons = b;						//Set new prev buttons for next time.
	for ( uint32_t i = 0; i < 16; ++i) {
		unsigned char bv = !(b & 1);
		//If == then value changed because the prev check doesn't negate the bit like bv setting above.
		if (bv == (prev & 1)) {
			bv |= _RELEASED;					//Bit 1 set = changed state.  Bit 0 = up/down state.
		}
		self->_buttons[i] = bv;

		//If value not _UP and we have a callback function, then call it.
		if (bv && (self->_callback != mp_const_none)) {
			mp_call_function_2(self->_callback, MP_OBJ_NEW_SMALL_INT(i), MP_OBJ_NEW_SMALL_INT(bv));
		}
		b >>= 1;
		prev >>= 1;
	}

	int sgn = 1;
	//Loop through joystick input and change values 0-255 to +/- 255 with 0 in the middle.
	for ( uint32_t i = 5; i < 9; ++i) {
		self->_joys[i - 5] = ((self->_res[i] - 0x80) << 1) * sgn;
		sgn = -sgn;								//Every other input (y) needs to be reversed.
	}
}

//Initialize the joystick.
STATIC void _init( machine_ps2_obj_t *self ) {
	//Read data, but probably not necessary, maybe just the delay is, but without these
	// initialization may not succeed.
	_qdata(self);
	mp_hal_delay_us(100);
	_qdata(self);
	mp_hal_delay_us(100);

	_sendrcv(self, cmd_enter_config, sizeof(cmd_enter_config));
	mp_hal_delay_us(3);
	_sendrcv(self, cmd_set_mode, sizeof(cmd_set_mode));
	mp_hal_delay_us(3);
//Put these in to enable rumble and variable pressure buttons.
//	_sendrcv(self, cmd_enable_rumble, sizeof(cmd_enable_rumble));
//	mp_hall_delay_us(3);
//	_sendrcv(self, cmd_enable_analog, sizeof(cmd_enable_analog));
//	mp_hall_delay_us(3);
	_sendrcv(self, cmd_exit_config, sizeof(cmd_exit_config));
	mp_hal_delay_us(3);

	//Read data a few times to settle out state changes.  Don't know why more than
	// a couple are needed but 6 is the minimum to be safe.
	for ( uint32_t i = 0; i < 6; ++i) {
		_qdata(self);
		mp_hal_delay_us(100);
	}
}

//Get button value given an index.
STATIC mp_obj_t machine_ps2_button( mp_obj_t self_in, mp_obj_t arg ) {
	machine_ps2_obj_t *self = self_in;
	int index = mp_obj_get_int(arg);
	if ((index < 0) || (index >= 16)) {
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Button index '%d' out of range 0-15.", index));
	}

	return MP_OBJ_NEW_SMALL_INT(self->_buttons[index]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ps2_button_obj, machine_ps2_button);

//Get button name given an index.
STATIC mp_obj_t machine_ps2_inputname( mp_obj_t self_in, mp_obj_t arg ) {
	machine_ps2_obj_t *self = self_in;
	int index = mp_obj_get_int(arg);
	if ((index < 0) || (index > _LY)) {
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Input index '%d' out of range 0-19.", index));
	}

	mp_rom_map_elem_t elem = getinputname(index);
	return (mp_obj_t)(elem.key);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ps2_inputname_obj, machine_ps2_inputname);

//Get joystick value given an index.
STATIC mp_obj_t machine_ps2_joy( mp_obj_t self_in, mp_obj_t arg ) {
	machine_ps2_obj_t *self = self_in;
	//If the input values are _LX etc, they have an additional bit set to start them at index 16.  Strip that off.
	int index = mp_obj_get_int(arg) & 0x03;
	return MP_OBJ_NEW_SMALL_INT(self->_joys[index]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ps2_joy_obj, machine_ps2_joy);

//Set/Get callback function.  If setting, returns previous value.
STATIC mp_obj_t machine_ps2_callback( size_t n_args, const mp_obj_t *args ) {
	machine_ps2_obj_t *self = args[0];
	mp_obj_t rv = self->_callback;

	if (n_args > 1) {
		self->_callback = args[1];
	}

	return rv;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_ps2_callback_obj, 1, 2, machine_ps2_callback);

//Read data and update _buttons and _joys arrays.  Also call callback if set.
STATIC mp_obj_t machine_ps2_update( mp_obj_t self_in ) {
	machine_ps2_obj_t *self = self_in;
	_qdata(self);
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ps2_update_obj, machine_ps2_update);

//Print data for ps2 joystick.
STATIC void machine_ps2_print( const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind ) {
	machine_ps2_obj_t *self = self_in;
	//TODO: Change this to print _buttons and _joys.
	mp_print_str(print, "Buffer: ");
	for ( int i = 0; i < 9; ++i) {
		mp_printf(print, "%X, ", self->_res[i]);
	}

//	machine_pin_type.print(print, self->_cmd, kind);
//	machine_pin_type.print(print, self->_data, kind);
//	machine_pin_type.print(print, self->_clk, kind);
//	machine_pin_type.print(print, self->_att, kind);
}

//Create a new PS2 object given the pin #s and optional callback function.
STATIC mp_obj_t machine_ps2_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
	if (n_args < 4) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Not enough parameters cmd, data, clk, att, [callback]."));
	}
	machine_ps2_obj_t *ps2 = m_new_obj(machine_ps2_obj_t);
	ps2->base.type = &machine_ps2_type;
	memset(ps2->_buttons, 0, sizeof(ps2->_buttons));
	memset(ps2->_joys, 0, sizeof(ps2->_joys));
	ps2->_prevbuttons = 0xFFFF;

	mp_obj_t pinargs[3];

	mp_map_t *_map = mp_obj_dict_get_map(machine_pin_type.locals_dict);
	//Output pin value.
	mp_map_elem_t *_elem = mp_map_lookup(_map, MP_OBJ_NEW_QSTR(MP_QSTR_OUT), MP_MAP_LOOKUP);
	pinargs[1] = _elem->value;
	//All pins are PULL_UP type.
	_elem = mp_map_lookup(_map, MP_OBJ_NEW_QSTR(MP_QSTR_PULL_UP), MP_MAP_LOOKUP);
	pinargs[2] = _elem->value;

	//Set up output pins cmd, clk and att.
	pinargs[0] = args[0];
	ps2->_cmd = machine_pin_type.make_new(&machine_pin_type, 3, 0, pinargs);
	pinargs[0] = args[2];
	ps2->_clk = machine_pin_type.make_new(&machine_pin_type, 3, 0, pinargs);
	pinargs[0] = args[3];
	ps2->_att = machine_pin_type.make_new(&machine_pin_type, 3, 0, pinargs);

	//Input pin for data.
	_elem = mp_map_lookup(_map, MP_OBJ_NEW_QSTR(MP_QSTR_IN), MP_MAP_LOOKUP);
	pinargs[1] = _elem->value;
	pinargs[0] = args[1];
	ps2->_data = machine_pin_type.make_new(&machine_pin_type, 3, 0, pinargs);

	//Set these pins to on or 1st command will fail.
	_setpin(ps2->_att, 1);
	_setpin(ps2->_clk, 1);

	ps2->_callback = mp_const_none;

	_init(ps2);

	if (n_args > 4) {
		//Set up the callback.
		ps2->_callback = args[4];
	}

	return MP_OBJ_FROM_PTR(ps2);
}

STATIC const mp_rom_map_elem_t machine_ps2_locals_dict_table[] = {
	//Class constants.
	{ MP_ROM_QSTR(MP_QSTR_SELECT), MP_ROM_INT(_SELECT) },
	{ MP_ROM_QSTR(MP_QSTR_L_HAT), MP_ROM_INT(_L_HAT) },
	{ MP_ROM_QSTR(MP_QSTR_R_HAT), MP_ROM_INT(_R_HAT) },
	{ MP_ROM_QSTR(MP_QSTR_START), MP_ROM_INT(_START) },
	{ MP_ROM_QSTR(MP_QSTR_DPAD_U), MP_ROM_INT(_DPAD_U) },
	{ MP_ROM_QSTR(MP_QSTR_DPAD_R), MP_ROM_INT(_DPAD_R) },
	{ MP_ROM_QSTR(MP_QSTR_DPAD_D), MP_ROM_INT(_DPAD_D) },
	{ MP_ROM_QSTR(MP_QSTR_DPAD_L), MP_ROM_INT(_DPAD_L) },
	{ MP_ROM_QSTR(MP_QSTR_L_TRIGGER), MP_ROM_INT(_L_TRIGGER) },
	{ MP_ROM_QSTR(MP_QSTR_R_TRIGGER), MP_ROM_INT(_R_TRIGGER) },
	{ MP_ROM_QSTR(MP_QSTR_L_SHOULDER), MP_ROM_INT(_L_SHOULDER) },
	{ MP_ROM_QSTR(MP_QSTR_R_SHOULDER), MP_ROM_INT(_R_SHOULDER) },
	{ MP_ROM_QSTR(MP_QSTR_TRIANGLE), MP_ROM_INT(_TRIANGLE) },
	{ MP_ROM_QSTR(MP_QSTR_CIRCLE), MP_ROM_INT(_CIRCLE) },
	{ MP_ROM_QSTR(MP_QSTR_CROSS), MP_ROM_INT(_CROSS) },
	{ MP_ROM_QSTR(MP_QSTR_SQUARE), MP_ROM_INT(_SQUARE) },
	{ MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_INT(_RX) },
	{ MP_ROM_QSTR(MP_QSTR_RY), MP_ROM_INT(_RY) },
	{ MP_ROM_QSTR(MP_QSTR_LX), MP_ROM_INT(_LX) },
	{ MP_ROM_QSTR(MP_QSTR_LY), MP_ROM_INT(_LY) },
	{ MP_ROM_QSTR(MP_QSTR_UP), MP_ROM_INT(_UP) },
	{ MP_ROM_QSTR(MP_QSTR_DOWN), MP_ROM_INT(_DOWN) },
	{ MP_ROM_QSTR(MP_QSTR_RELEASED), MP_ROM_INT(_RELEASED) },
	{ MP_ROM_QSTR(MP_QSTR_PRESSED), MP_ROM_INT(_PRESSED) },
	//Instance methods.
	{ MP_ROM_QSTR(MP_QSTR_button), MP_ROM_PTR(&machine_ps2_button_obj) },
	{ MP_ROM_QSTR(MP_QSTR_inputname), MP_ROM_PTR(&machine_ps2_inputname_obj) },
	{ MP_ROM_QSTR(MP_QSTR_joy), MP_ROM_PTR(&machine_ps2_joy_obj) },
	{ MP_ROM_QSTR(MP_QSTR_callback), MP_ROM_PTR(&machine_ps2_callback_obj) },
	{ MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&machine_ps2_update_obj) },
};

STATIC MP_DEFINE_CONST_DICT(machine_ps2_locals_dict, machine_ps2_locals_dict_table);

mp_rom_map_elem_t getinputname( unsigned int aIndex ) {
	return machine_ps2_locals_dict_table[aIndex];
}

const mp_obj_type_t machine_ps2_type = {
	{ &mp_type_type },
	.name = MP_QSTR_PS2,
	.print = machine_ps2_print,
	.make_new = machine_ps2_make_new,
	.locals_dict = (mp_obj_dict_t*)&machine_ps2_locals_dict,
};

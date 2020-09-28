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

#include <windows.h>
#include <XInput.h>
#include <string.h>
#include "py/obj.h"
#include "py/nlr.h"

//This module emulates the PS2 controller using the Xbox 360 controller and XInput.

//NOTE: A number of delays and other seemingly superfluous calls may not be necessary of may be too long.
// I didn't test the tweaking of these values too much as I was in a hurry.
// Optimally the delay times could be set on class variables and used rather than hard coding them.

//TODO: Implement button name using qstr lookup in constants array and qstr_str()?  Either that or just return the qstr.

const mp_obj_type_t machine_ps2_type;
mp_rom_map_elem_t getinputname( unsigned int aIndex );

//Bit 0 indicates button on/off state.  Bit 1 indicates change state between current and previous on/off value.
typedef enum {
	_UP = 0,
	_DOWN = 1, 									//Button is down.
	_RELEASED = 2, 								//Indicate button was just released.
	_PRESSED = 3, 								//Indicate button was just pressed.
} ps2_button_states;

//_buttons array indexes
typedef enum {
	_SELECT = 0,
	_L_HAT = 1,
	_R_HAT = 2,
	_START = 3,
	_DPAD_U = 4,
	_DPAD_R = 5,
	_DPAD_D = 6,
	_DPAD_L = 7,
	_L_TRIGGER = 8,
	_R_TRIGGER = 9,
	_L_SHOULDER = 10,
	_R_SHOULDER = 11,
	_TRIANGLE   = 12,
	_CIRCLE = 13,
	_CROSS = 14,
	_SQUARE = 15
} ps2_buttons;

//_joys array indexes.
typedef enum {
	_RX = 0x10,
	_RY = 0x11,
	_LX = 0x12,
	_LY = 0x13
} ps2_axis;

#define _L_TRIGGER_X 0x0400
#define _R_TRIGGER_X 0x0800

unsigned int _remap[16] = {
	XINPUT_GAMEPAD_BACK,
	XINPUT_GAMEPAD_LEFT_THUMB,
	XINPUT_GAMEPAD_LEFT_THUMB,
	XINPUT_GAMEPAD_START,
	XINPUT_GAMEPAD_DPAD_UP,
	XINPUT_GAMEPAD_DPAD_RIGHT,
	XINPUT_GAMEPAD_DPAD_DOWN,
	XINPUT_GAMEPAD_DPAD_LEFT,
	_L_TRIGGER_X,
	_R_TRIGGER_X,
	XINPUT_GAMEPAD_LEFT_SHOULDER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER,
	XINPUT_GAMEPAD_Y,
	XINPUT_GAMEPAD_B,
	XINPUT_GAMEPAD_A,
	XINPUT_GAMEPAD_X
};

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

	XINPUT_STATE _controllerState;
	int _controllerNum;

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

STATIC void _qdata( machine_ps2_obj_t *self ) {

	memset(&(self->_controllerState), 0, sizeof(self->_controllerState));

	DWORD result = XInputGetState(self->_controllerNum, &(self->_controllerState));

	if (result == ERROR_SUCCESS) {
		if (self->_controllerState.Gamepad.bLeftTrigger > 10) {
			self->_controllerState.Gamepad.wButtons |= _L_TRIGGER_X;
		}
		if (self->_controllerState.Gamepad.bRightTrigger > 10) {
			self->_controllerState.Gamepad.wButtons |= _R_TRIGGER_X;
		}

		for ( uint32_t i = 0; i < 16; ++i) {
			unsigned int b = self->_controllerState.Gamepad.wButtons & _remap[i];
			unsigned char bv = b ? 1 : 0;
			if ((self->_prevbuttons & _remap[i]) != b) {
				bv |= 2;
			}
			self->_buttons[i] = bv;

			//If value not _UP and we have a callback function, then call it.
			if (bv && (self->_callback != mp_const_none)) {
				mp_call_function_2(self->_callback, MP_OBJ_NEW_SMALL_INT(i), MP_OBJ_NEW_SMALL_INT(bv));
			}
		}

		self->_prevbuttons = self->_controllerState.Gamepad.wButtons;

		int v = self->_controllerState.Gamepad.sThumbLX;
		self->_joys[_LX & 0x03] = v >> 8;
		v = self->_controllerState.Gamepad.sThumbLY;
		self->_joys[_LY & 0x03] = v >> 8;
		v = self->_controllerState.Gamepad.sThumbRX;
		self->_joys[_RX & 0x03] = v >> 8;
		v = self->_controllerState.Gamepad.sThumbRY;
		self->_joys[_RY & 0x03] = v >> 8;
	}
}

//Get button value given an index.
STATIC mp_obj_t machine_ps2_update( mp_obj_t self_in ) {
	machine_ps2_obj_t *self = self_in;
	_qdata(self);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ps2_update_obj, machine_ps2_update);

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


//Get joystick value given an index.
STATIC mp_obj_t machine_ps2_joy( mp_obj_t self_in, mp_obj_t arg ) {
	machine_ps2_obj_t *self = self_in;
	int index = mp_obj_get_int(arg) & 0x03;
	return MP_OBJ_NEW_SMALL_INT(self->_joys[index]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ps2_joy_obj, machine_ps2_joy);

//Get button/joy input name given an index.
STATIC mp_obj_t machine_ps2_inputname( mp_obj_t self_in, mp_obj_t arg ) {
	machine_ps2_obj_t *self = self_in;
	int index = mp_obj_get_int(arg);
	if ((index < 0) || (index > _LY)) {
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "input index '%d' out of range 0-19.", index));
	}

	mp_rom_map_elem_t elem = getinputname(index);
	return (mp_obj_t)(elem.key);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ps2_inputname_obj, machine_ps2_inputname);

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

STATIC void _init( machine_ps2_obj_t *self ) {
	//TODO: Do any setup.

	for ( uint32_t i = 0; i < 6; ++i) {
		_qdata(self);
	}
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
	ps2->_controllerNum = 0;

	ps2->_cmd = args[0];
	ps2->_data = args[1];
	ps2->_clk = args[2];
	ps2->_att = args[3];

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
	{ MP_ROM_QSTR(MP_QSTR_joy), MP_ROM_PTR(&machine_ps2_joy_obj) },
	{ MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&machine_ps2_update_obj) },
	{ MP_ROM_QSTR(MP_QSTR_inputname), MP_ROM_PTR(&machine_ps2_inputname_obj) },
	{ MP_ROM_QSTR(MP_QSTR_callback), MP_ROM_PTR(&machine_ps2_callback_obj) },
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


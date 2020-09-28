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
#include <mmsystem.h>
#include <dsound.h>
#include "py/obj.h"

const mp_obj_type_t guy_test_type;

typedef struct _guy_test_obj_t {
	mp_obj_base_t base;
	vstr_t _cmd;

	mp_obj_t _callback;							//Callback function.

	unsigned int _value;
} guy_test_obj_t;

STATIC mp_obj_t _sendCommand( mp_obj_t self_in, const char *cmd, mp_obj_t arg, const char *suffix ) {
	guy_test_obj_t *self = self_in;

	char retstring[256];

	size_t str_len;
	const char *file = mp_obj_str_get_data(arg, &str_len);

	vstr_reset(&self->_cmd);
	vstr_add_str(&self->_cmd, cmd);
	vstr_add_str(&self->_cmd, file);
	if (suffix) {
		vstr_add_str(&self->_cmd, suffix);
	}

	const char *cmdstr = vstr_str(&self->_cmd);
	mciSendString(cmdstr, retstring, sizeof(retstring), 0);
	return mp_obj_new_str(retstring, strlen(retstring) + 1);
}

STATIC mp_obj_t guy_test_command( mp_obj_t self_in, mp_obj_t arg ) {
	guy_test_obj_t *self = self_in;

	size_t str_len;
	const char *cmdstr = mp_obj_str_get_data(arg, &str_len);
	char retstring[256];
	mciSendString(cmdstr, retstring, sizeof(retstring), 0);
	return mp_obj_new_str(retstring, strlen(retstring) + 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_command_obj, guy_test_command);

STATIC mp_obj_t guy_test_load( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "open \"", arg, "\"");
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_load_obj, guy_test_load);

STATIC mp_obj_t guy_test_unload( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "close ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_unload_obj, guy_test_unload);

STATIC mp_obj_t guy_test_play( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "play ", arg, " from 0");
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_play_obj, guy_test_play);

STATIC mp_obj_t guy_test_stop( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "stop ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_stop_obj, guy_test_stop);

STATIC mp_obj_t guy_test_pause( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "pause ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_pause_obj, guy_test_pause);

STATIC mp_obj_t guy_test_resume( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "resume ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_test_resume_obj, guy_test_resume);


//Set/Get callback function.  If setting, returns previous value.
STATIC mp_obj_t guy_test_callback( size_t n_args, const mp_obj_t *args ) {
	guy_test_obj_t *self = args[0];
	mp_obj_t rv = self->_callback;

	if (n_args > 1) {
		self->_callback = args[1];
	}

	return rv;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(guy_test_callback_obj, 1, 2, guy_test_callback);

//Print data for test object.
STATIC void guy_test_print( const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind ) {
	guy_test_obj_t *self = self_in;
	mp_printf(print, "Value: %X", self->_value);
}

//Create a new test object.
STATIC mp_obj_t guy_test_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
	guy_test_obj_t *self = m_new_obj(guy_test_obj_t);
	self->base.type = &guy_test_type;
	self->_value = 0;

	vstr_init(&(self->_cmd), 128);

	self->_callback = mp_const_none;

	if (n_args > 1) {
		//Set up the callback.
		self->_callback = args[1];
	}

	return MP_OBJ_FROM_PTR(self);
}

STATIC const mp_rom_map_elem_t guy_test_locals_dict_table[] = {
	//Instance methods.
	{ MP_ROM_QSTR(MP_QSTR_command), MP_ROM_PTR(&guy_test_command_obj) },
	{ MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&guy_test_load_obj) },
	{ MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&guy_test_unload_obj) },
	{ MP_ROM_QSTR(MP_QSTR_play), MP_ROM_PTR(&guy_test_play_obj) },
	{ MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&guy_test_stop_obj) },
	{ MP_ROM_QSTR(MP_QSTR_pause), MP_ROM_PTR(&guy_test_pause_obj) },
	{ MP_ROM_QSTR(MP_QSTR_resume), MP_ROM_PTR(&guy_test_resume_obj) },
	{ MP_ROM_QSTR(MP_QSTR_callback), MP_ROM_PTR(&guy_test_callback_obj) },
};

STATIC MP_DEFINE_CONST_DICT(guy_test_locals_dict, guy_test_locals_dict_table);

const mp_obj_type_t guy_test_type = {
	{ &mp_type_type },
	.name = MP_QSTR_test,
	.print = guy_test_print,
	.make_new = guy_test_make_new,
	.locals_dict = (mp_obj_dict_t*)&guy_test_locals_dict,
};


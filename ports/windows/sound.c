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

const mp_obj_type_t guy_sound_type;

typedef struct _guy_sound_obj_t {
	mp_obj_base_t base;
	vstr_t _cmd;								//Buffer for constructing command.
	vstr_t _aliasstr;
	unsigned int _alias;						//Alias index.
} guy_sound_obj_t;

///
/// <summary> Send micSendString command. </summary>
/// <param name="cmd"> Command name with space IE: "play ". </param>
/// <param name="arg"> String (file name) to appent to command string. </param>
/// <param name="suffix"> String to append to cmd + arg. </param>
/// <returns> result string from mciSendString command. </returns>
///
STATIC mp_obj_t _sendCommand( mp_obj_t self_in, const char *cmd, mp_obj_t arg, const char *suffix ) {
	guy_sound_obj_t *self = self_in;

	size_t str_len;
	const char *file = mp_obj_str_get_data(arg, &str_len);

	vstr_reset(&self->_cmd);
	vstr_add_str(&self->_cmd, cmd);
	vstr_add_str(&self->_cmd, file);
	if (suffix) {
		vstr_add_str(&self->_cmd, suffix);
	}
	vstr_null_terminated_str(&self->_cmd);

	const char *cmdstr = vstr_str(&self->_cmd);
	OutputDebugString(cmdstr);
	OutputDebugString("\r\n");
	//NOTE: I used to get the result string but no matter what, it was always "\x00", so I took it out.
	mciSendString(cmdstr, NULL, 0, 0);
	return mp_const_none;
}

STATIC mp_obj_t guy_sound_command( mp_obj_t self_in, mp_obj_t arg ) {
	guy_sound_obj_t *self = self_in;

	size_t str_len;
	const char *cmdstr = mp_obj_str_get_data(arg, &str_len);
	mciSendString(cmdstr, NULL, 0, 0);
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_command_obj, guy_sound_command);

STATIC mp_obj_t guy_sound_load( mp_obj_t self_in, mp_obj_t arg ) {
	guy_sound_obj_t *self = self_in;

	vstr_reset(&self->_aliasstr);
	vstr_printf(&self->_aliasstr, "\" alias snd%d", self->_alias++);
	vstr_null_terminated_str(&self->_aliasstr);
	const char *suffixstr = vstr_str(&self->_aliasstr);
	_sendCommand(self, "open \"", arg, suffixstr);
	return mp_obj_new_str(&suffixstr[8], strlen(suffixstr) - 8);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_load_obj, guy_sound_load);

STATIC mp_obj_t guy_sound_unload( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "close ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_unload_obj, guy_sound_unload);

STATIC mp_obj_t guy_sound_play( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "play ", arg, " from 0");
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_play_obj, guy_sound_play);

STATIC mp_obj_t guy_sound_stop( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "stop ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_stop_obj, guy_sound_stop);

STATIC mp_obj_t guy_sound_pause( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "pause ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_pause_obj, guy_sound_pause);

STATIC mp_obj_t guy_sound_resume( mp_obj_t self_in, mp_obj_t arg ) {
	return _sendCommand(self_in, "resume ", arg, NULL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(guy_sound_resume_obj, guy_sound_resume);

//Create a new sound object.
STATIC mp_obj_t guy_sound_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
	guy_sound_obj_t *self = m_new_obj(guy_sound_obj_t);
	self->base.type = &guy_sound_type;
	self->_alias = 0;

	vstr_init(&(self->_aliasstr), 16);
	vstr_init(&(self->_cmd), 256);

	return MP_OBJ_FROM_PTR(self);
}

STATIC const mp_rom_map_elem_t guy_sound_locals_dict_table[] = {
	//Instance methods.
	{ MP_ROM_QSTR(MP_QSTR_command), MP_ROM_PTR(&guy_sound_command_obj) },
	{ MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&guy_sound_load_obj) },
	{ MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&guy_sound_unload_obj) },
	{ MP_ROM_QSTR(MP_QSTR_play), MP_ROM_PTR(&guy_sound_play_obj) },
	{ MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&guy_sound_stop_obj) },
	{ MP_ROM_QSTR(MP_QSTR_pause), MP_ROM_PTR(&guy_sound_pause_obj) },
	{ MP_ROM_QSTR(MP_QSTR_resume), MP_ROM_PTR(&guy_sound_resume_obj) },
};

STATIC MP_DEFINE_CONST_DICT(guy_sound_locals_dict, guy_sound_locals_dict_table);

const mp_obj_type_t guy_sound_type = {
	{ &mp_type_type },
	.name = MP_QSTR_sound,
	.make_new = guy_sound_make_new,
	.locals_dict = (mp_obj_dict_t*)&guy_sound_locals_dict,
};

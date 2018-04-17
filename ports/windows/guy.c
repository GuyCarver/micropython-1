//----------------------------------------------------------------------
// Copyright (c) 2018, gcarver
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
// FILE    guy.c
// BY      gcarver
// DATE    04/17/2018 10:56 AM
//----------------------------------------------------------------------

#include "py/obj.h"

//This file contains the guy module which is auto registered.  I add my custom types here.
//import guy

//Externs for the custom types I implemented in other files.
extern const mp_obj_type_t machine_ps2_type;

//Add the custom types to this list.
STATIC const mp_rom_map_elem_t mp_module_guy_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_guy) },
    { MP_ROM_QSTR(MP_QSTR_PS2), MP_ROM_PTR(&machine_ps2_type) }
};

STATIC MP_DEFINE_CONST_DICT(mp_module_guy_globals, mp_module_guy_globals_table);

const mp_obj_module_t mp_module_guy = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_guy_globals,
};

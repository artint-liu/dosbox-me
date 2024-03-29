/*
 *  Copyright (C) 2002-2010  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* $Id: joystick.cpp,v 1.21 2009-05-27 09:15:41 qbix79 Exp $ */

#include <string.h>
#include "dosbox.h"
#include "inout.h"
#include "setup.h"
#include "joystick.h"
#include "pic.h"
#include "support.h"

#define RANGE 64
#define TIMEOUT 10

#define OHMS 120000/2
#define JOY_S_CONSTANT 0.0000242
#define S_PER_OHM 0.000000011

struct JoyStick {
	bool enabled;
	float xpos,ypos;
	double xtick,ytick;
	Bitu xcount,ycount;
	bool button[2];
};

JoystickType joytype;
static JoyStick stick[2];

static Bit32u last_write = 0;
static bool write_active = false;
static bool swap34 = false;
bool button_wrapping_enabled = true;

#ifndef MINI_SDL
extern bool autofire; //sdl_mapper.cpp
#endif

static Bitu read_p201(Bitu port,Bitu iolen) {
	/* Reset Joystick to 0 after TIMEOUT ms */
	if(write_active && ((PIC_Ticks - last_write) > TIMEOUT)) {
		write_active = false;
		stick[0].xcount = 0;
		stick[1].xcount = 0;
		stick[0].ycount = 0;
		stick[1].ycount = 0;
//		LOG_MSG("reset by time %d %d",PIC_Ticks,last_write);
	}

	/**  Format of the byte to be returned:       
	**                        | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
	**                        +-------------------------------+
	**                          |   |   |   |   |   |   |   |
	**  Joystick B, Button 2 ---+   |   |   |   |   |   |   +--- Joystick A, X Axis
	**  Joystick B, Button 1 -------+   |   |   |   |   +------- Joystick A, Y Axis
	**  Joystick A, Button 2 -----------+   |   |   +----------- Joystick B, X Axis
	**  Joystick A, Button 1 ---------------+   +--------------- Joystick B, Y Axis
	**/
	Bit8u ret=0xff;
	if (stick[0].enabled) {
		if (stick[0].xcount) stick[0].xcount--; else ret&=~1;
		if (stick[0].ycount) stick[0].ycount--; else ret&=~2;
		if (stick[0].button[0]) ret&=~16;
		if (stick[0].button[1]) ret&=~32;
	}
	if (stick[1].enabled) {
		if (stick[1].xcount) stick[1].xcount--; else ret&=~4;
		if (stick[1].ycount) stick[1].ycount--; else ret&=~8;
		if (stick[1].button[0]) ret&=~64;
		if (stick[1].button[1]) ret&=~128;
	}
	return ret;
}

static Bitu read_p201_timed(Bitu port,Bitu iolen) {
	Bit8u ret=0xff;
	double currentTick = PIC_FullIndex();
	if( stick[0].enabled ){
		if( stick[0].xtick < currentTick ) ret &=~1;
		if( stick[0].ytick < currentTick ) ret &=~2;
	}
	if( stick[1].enabled ){
		if( stick[1].xtick < currentTick ) ret &=~4;
		if( stick[1].ytick < currentTick ) ret &=~8;
	}

	if (stick[0].enabled) {
		if (stick[0].button[0]) ret&=~16;
		if (stick[0].button[1]) ret&=~32;
	}
	if (stick[1].enabled) {
		if (stick[1].button[0]) ret&=~64;
		if (stick[1].button[1]) ret&=~128;
	}
	return ret;
}

static void write_p201(Bitu port,Bitu val,Bitu iolen) {
	/* Store writetime index */
	write_active = true;
	last_write = PIC_Ticks;
	if (stick[0].enabled) {
		stick[0].xcount=(Bitu)((stick[0].xpos*RANGE)+RANGE);
		stick[0].ycount=(Bitu)((stick[0].ypos*RANGE)+RANGE);
	}
	if (stick[1].enabled) {
		stick[1].xcount=(Bitu)(((swap34? stick[1].ypos : stick[1].xpos)*RANGE)+RANGE);
		stick[1].ycount=(Bitu)(((swap34? stick[1].xpos : stick[1].ypos)*RANGE)+RANGE);
	}

}
static void write_p201_timed(Bitu port,Bitu val,Bitu iolen) {
	// Store writetime index
	// Axes take time = 24.2 microseconds + ( 0.011 microsecons/ohm * resistance )
	// to reset to 0
	// Precalculate the time at which each axis hits 0 here
	double currentTick = PIC_FullIndex();
	if (stick[0].enabled) {
		stick[0].xtick = currentTick + 1000.0*( JOY_S_CONSTANT + S_PER_OHM *
	                         (double)(((stick[0].xpos+1.0)* OHMS)) );
		stick[0].ytick = currentTick + 1000.0*( JOY_S_CONSTANT + S_PER_OHM *
		                 (double)(((stick[0].ypos+1.0)* OHMS)) );
	}
	if (stick[1].enabled) {
		stick[1].xtick = currentTick + 1000.0*( JOY_S_CONSTANT + S_PER_OHM *
		                 (double)((swap34? stick[1].ypos : stick[1].xpos)+1.0) * OHMS);
		stick[1].ytick = currentTick + 1000.0*( JOY_S_CONSTANT + S_PER_OHM *
		                 (double)((swap34? stick[1].xpos : stick[1].ypos)+1.0) * OHMS);
	}
}

void JOYSTICK_Enable(Bitu which,bool enabled) {
	if (which<2) stick[which].enabled=enabled;
}

void JOYSTICK_Button(Bitu which,Bitu num,bool pressed) {
	if ((which<2) && (num<2)) stick[which].button[num]=pressed;
}

void JOYSTICK_Move_X(Bitu which,float x) {
	if (which<2) {
		stick[which].xpos=x;
	}
}

void JOYSTICK_Move_Y(Bitu which,float y) {
	if (which<2) {
		stick[which].ypos=y;
	}
}

bool JOYSTICK_IsEnabled(Bitu which) {
	if (which<2) return stick[which].enabled;
	return false;
}

bool JOYSTICK_GetButton(Bitu which, Bitu num) {
	if ((which<2) && (num<2)) return stick[which].button[num];
	return false;
}

float JOYSTICK_GetMove_X(Bitu which) {
	if (which<2) return stick[which].xpos;
	return 0.0f;
}

float JOYSTICK_GetMove_Y(Bitu which) {
	if (which<2) return stick[which].ypos;
	return 0.0f;
}

class JOYSTICK:public Module_base{
private:
	IO_ReadHandleObject ReadHandler;
	IO_WriteHandleObject WriteHandler;
public:
	JOYSTICK(Section* configuration):Module_base(configuration){
		Section_prop * section=static_cast<Section_prop *>(configuration);
		const char * type=section->Get_string("joysticktype");
		if (!strcasecmp(type,"none"))       joytype = JOY_NONE;
		else if (!strcasecmp(type,"false")) joytype = JOY_NONE;
		else if (!strcasecmp(type,"auto"))  joytype = JOY_AUTO;
		else if (!strcasecmp(type,"2axis")) joytype = JOY_2AXIS;
		else if (!strcasecmp(type,"4axis")) joytype = JOY_4AXIS;
		else if (!strcasecmp(type,"4axis_2")) joytype = JOY_4AXIS_2;
		else if (!strcasecmp(type,"fcs"))   joytype = JOY_FCS;
		else if (!strcasecmp(type,"ch"))    joytype = JOY_CH;
		else joytype = JOY_AUTO;

		bool timed = section->Get_bool("timed");
		if(timed) {
			ReadHandler.Install(0x201,read_p201_timed,IO_MB);
			WriteHandler.Install(0x201,write_p201_timed,IO_MB);
		} else {
			ReadHandler.Install(0x201,read_p201,IO_MB);
			WriteHandler.Install(0x201,write_p201,IO_MB);
		}
#ifndef MINI_SDL
		autofire = section->Get_bool("autofire");
#endif
		swap34 = section->Get_bool("swap34");
		button_wrapping_enabled = section->Get_bool("buttonwrap");
		stick[0].enabled = false;
		stick[1].enabled = false;
		stick[0].xtick = stick[0].ytick = stick[1].xtick =
		                 stick[1].ytick = PIC_FullIndex();
	}
};
static JOYSTICK* test;

void JOYSTICK_Destroy(Section* sec) {
	delete test;
}

void JOYSTICK_Init(Section* sec) {
	test = new JOYSTICK(sec);
	sec->AddDestroyFunction(&JOYSTICK_Destroy,true); 
}

/* Keyboard scancode mapping from SDL to Mac codes
 *
 * From Mini vMac's OSGLUSDL.c, which is Copyright (C) 2012 Paul
 * C. Pratt, Manuel Alfayate, and released under GPLv2.
 */

#ifndef KEYMAP_SDL_H
#define KEYMAP_SDL_H

#include "keymap.h"

static inline int SDLScan2MacKeyCode(SDL_Scancode i)
{
	int v = MKC_None;

	switch (i) {
        case SDL_SCANCODE_BACKSPACE: v = MKC_BackSpace; break;
        case SDL_SCANCODE_TAB: v = MKC_Tab; break;
        case SDL_SCANCODE_CLEAR: v = MKC_Clear; break;
        case SDL_SCANCODE_RETURN: v = MKC_Return; break;
        case SDL_SCANCODE_PAUSE: v = MKC_Pause; break;
        case SDL_SCANCODE_ESCAPE: v = MKC_Escape; break;
        case SDL_SCANCODE_SPACE: v = MKC_Space; break;
        case SDL_SCANCODE_APOSTROPHE: v = MKC_SingleQuote; break;
        case SDL_SCANCODE_COMMA: v = MKC_Comma; break;
        case SDL_SCANCODE_MINUS: v = MKC_Minus; break;
        case SDL_SCANCODE_PERIOD: v = MKC_Period; break;
        case SDL_SCANCODE_SLASH: v = MKC_Slash; break;
        case SDL_SCANCODE_0: v = MKC_0; break;
        case SDL_SCANCODE_1: v = MKC_1; break;
        case SDL_SCANCODE_2: v = MKC_2; break;
        case SDL_SCANCODE_3: v = MKC_3; break;
        case SDL_SCANCODE_4: v = MKC_4; break;
        case SDL_SCANCODE_5: v = MKC_5; break;
        case SDL_SCANCODE_6: v = MKC_6; break;
        case SDL_SCANCODE_7: v = MKC_7; break;
        case SDL_SCANCODE_8: v = MKC_8; break;
        case SDL_SCANCODE_9: v = MKC_9; break;
        case SDL_SCANCODE_SEMICOLON: v = MKC_SemiColon; break;
        case SDL_SCANCODE_EQUALS: v = MKC_Equal; break;

        case SDL_SCANCODE_LEFTBRACKET: v = MKC_LeftBracket; break;
        case SDL_SCANCODE_BACKSLASH: v = MKC_BackSlash; break;
        case SDL_SCANCODE_RIGHTBRACKET: v = MKC_RightBracket; break;
        case SDL_SCANCODE_GRAVE: v = MKC_Grave; break;

        case SDL_SCANCODE_A: v = MKC_A; break;
        case SDL_SCANCODE_B: v = MKC_B; break;
        case SDL_SCANCODE_C: v = MKC_C; break;
        case SDL_SCANCODE_D: v = MKC_D; break;
        case SDL_SCANCODE_E: v = MKC_E; break;
        case SDL_SCANCODE_F: v = MKC_F; break;
        case SDL_SCANCODE_G: v = MKC_G; break;
        case SDL_SCANCODE_H: v = MKC_H; break;
        case SDL_SCANCODE_I: v = MKC_I; break;
        case SDL_SCANCODE_J: v = MKC_J; break;
        case SDL_SCANCODE_K: v = MKC_K; break;
        case SDL_SCANCODE_L: v = MKC_L; break;
        case SDL_SCANCODE_M: v = MKC_M; break;
        case SDL_SCANCODE_N: v = MKC_N; break;
        case SDL_SCANCODE_O: v = MKC_O; break;
        case SDL_SCANCODE_P: v = MKC_P; break;
        case SDL_SCANCODE_Q: v = MKC_Q; break;
        case SDL_SCANCODE_R: v = MKC_R; break;
        case SDL_SCANCODE_S: v = MKC_S; break;
        case SDL_SCANCODE_T: v = MKC_T; break;
        case SDL_SCANCODE_U: v = MKC_U; break;
        case SDL_SCANCODE_V: v = MKC_V; break;
        case SDL_SCANCODE_W: v = MKC_W; break;
        case SDL_SCANCODE_X: v = MKC_X; break;
        case SDL_SCANCODE_Y: v = MKC_Y; break;
        case SDL_SCANCODE_Z: v = MKC_Z; break;

        case SDL_SCANCODE_KP_0: v = MKC_KP0; break;
        case SDL_SCANCODE_KP_1: v = MKC_KP1; break;
        case SDL_SCANCODE_KP_2: v = MKC_KP2; break;
        case SDL_SCANCODE_KP_3: v = MKC_KP3; break;
        case SDL_SCANCODE_KP_4: v = MKC_KP4; break;
        case SDL_SCANCODE_KP_5: v = MKC_KP5; break;
        case SDL_SCANCODE_KP_6: v = MKC_KP6; break;
        case SDL_SCANCODE_KP_7: v = MKC_KP7; break;
        case SDL_SCANCODE_KP_8: v = MKC_KP8; break;
        case SDL_SCANCODE_KP_9: v = MKC_KP9; break;

        case SDL_SCANCODE_KP_PERIOD: v = MKC_Decimal; break;
        case SDL_SCANCODE_KP_DIVIDE: v = MKC_KPDevide; break;
        case SDL_SCANCODE_KP_MULTIPLY: v = MKC_KPMultiply; break;
        case SDL_SCANCODE_KP_MINUS: v = MKC_KPSubtract; break;
        case SDL_SCANCODE_KP_PLUS: v = MKC_KPAdd; break;
        case SDL_SCANCODE_KP_ENTER: v = MKC_Enter; break;
        case SDL_SCANCODE_KP_EQUALS: v = MKC_KPEqual; break;

        case SDL_SCANCODE_UP: v = MKC_Up; break;
        case SDL_SCANCODE_DOWN: v = MKC_Down; break;
        case SDL_SCANCODE_RIGHT: v = MKC_Right; break;
        case SDL_SCANCODE_LEFT: v = MKC_Left; break;
        case SDL_SCANCODE_INSERT: v = MKC_Help; break;
        case SDL_SCANCODE_HOME: v = MKC_Home; break;
        case SDL_SCANCODE_END: v = MKC_End; break;
        case SDL_SCANCODE_PAGEUP: v = MKC_PageUp; break;
        case SDL_SCANCODE_PAGEDOWN: v = MKC_PageDown; break;
        /* FIXME, case SDL_SCANCODE_CAPSLOCK and MKC_formac_CapsLock */
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_LSHIFT: v = MKC_Shift; break;
        case SDL_SCANCODE_RCTRL:
        case SDL_SCANCODE_LCTRL: v = MKC_Control; break;
        case SDL_SCANCODE_RALT:
        case SDL_SCANCODE_LALT: v = MKC_Option; break;
        case SDL_SCANCODE_RGUI:
        case SDL_SCANCODE_LGUI: v = MKC_Command; break;

        case SDL_SCANCODE_KP_A: v = MKC_A; break;
        case SDL_SCANCODE_KP_B: v = MKC_B; break;
        case SDL_SCANCODE_KP_C: v = MKC_C; break;
        case SDL_SCANCODE_KP_D: v = MKC_D; break;
        case SDL_SCANCODE_KP_E: v = MKC_E; break;
        case SDL_SCANCODE_KP_F: v = MKC_F; break;

        case SDL_SCANCODE_KP_BACKSPACE: v = MKC_BackSpace; break;
        case SDL_SCANCODE_KP_CLEAR: v = MKC_Clear; break;
        case SDL_SCANCODE_KP_COMMA: v = MKC_Comma; break;
        case SDL_SCANCODE_KP_DECIMAL: v = MKC_Decimal; break;

        default:
                break;
	}

	return v;
}

#endif

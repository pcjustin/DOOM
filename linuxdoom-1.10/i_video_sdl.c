// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// SDL2 graphics implementation for DOOM
// Replaces X11 backend with cross-platform SDL2
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include <SDL.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "doomdef.h"

// SDL2 globals
static SDL_Window*   sdl_window = NULL;
static SDL_Renderer* sdl_renderer = NULL;
static SDL_Texture*  sdl_texture = NULL;

// Display dimensions (scaled by multiply factor)
static int window_width;
static int window_height;

// Blocky mode scaling factor (2x, 3x, 4x)
static int multiply = 2;

// 32-bit RGBA palette: maps 8-bit DOOM palette indices to display pixels
static uint32_t palette[256];

// Pixel buffer for texture upload (scaled framebuffer)
static uint32_t* pixels = NULL;

// Mouse state
static boolean mouse_grabbed = false;


//
// xlatekey - Translate SDL2 scancode to DOOM key
//
static int xlatekey(SDL_Scancode scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_LEFT:      return KEY_LEFTARROW;
        case SDL_SCANCODE_RIGHT:     return KEY_RIGHTARROW;
        case SDL_SCANCODE_DOWN:      return KEY_DOWNARROW;
        case SDL_SCANCODE_UP:        return KEY_UPARROW;
        case SDL_SCANCODE_ESCAPE:    return KEY_ESCAPE;
        case SDL_SCANCODE_RETURN:    return KEY_ENTER;
        case SDL_SCANCODE_TAB:       return KEY_TAB;
        case SDL_SCANCODE_F1:        return KEY_F1;
        case SDL_SCANCODE_F2:        return KEY_F2;
        case SDL_SCANCODE_F3:        return KEY_F3;
        case SDL_SCANCODE_F4:        return KEY_F4;
        case SDL_SCANCODE_F5:        return KEY_F5;
        case SDL_SCANCODE_F6:        return KEY_F6;
        case SDL_SCANCODE_F7:        return KEY_F7;
        case SDL_SCANCODE_F8:        return KEY_F8;
        case SDL_SCANCODE_F9:        return KEY_F9;
        case SDL_SCANCODE_F10:       return KEY_F10;
        case SDL_SCANCODE_F11:       return KEY_F11;
        case SDL_SCANCODE_F12:       return KEY_F12;
        case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
        case SDL_SCANCODE_DELETE:    return KEY_BACKSPACE;
        case SDL_SCANCODE_PAUSE:     return KEY_PAUSE;
        case SDL_SCANCODE_EQUALS:    return KEY_EQUALS;
        case SDL_SCANCODE_KP_EQUALS: return KEY_EQUALS;
        case SDL_SCANCODE_MINUS:     return KEY_MINUS;
        case SDL_SCANCODE_KP_MINUS:  return KEY_MINUS;
        case SDL_SCANCODE_LSHIFT:    return KEY_RSHIFT;
        case SDL_SCANCODE_RSHIFT:    return KEY_RSHIFT;
        case SDL_SCANCODE_LCTRL:     return KEY_RCTRL;
        case SDL_SCANCODE_RCTRL:     return KEY_RCTRL;
        case SDL_SCANCODE_LALT:      return KEY_RALT;
        case SDL_SCANCODE_RALT:      return KEY_RALT;
        default:
            // Handle ASCII printable characters
            if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
                return 'a' + (scancode - SDL_SCANCODE_A);
            if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
                return '1' + (scancode - SDL_SCANCODE_1);
            if (scancode == SDL_SCANCODE_0)
                return '0';
            if (scancode == SDL_SCANCODE_SPACE)
                return ' ';
            break;
    }
    return 0;
}


//
// I_ShutdownGraphics
//
void I_ShutdownGraphics(void)
{
    if (pixels)
    {
        free(pixels);
        pixels = NULL;
    }

    if (sdl_texture)
    {
        SDL_DestroyTexture(sdl_texture);
        sdl_texture = NULL;
    }

    if (sdl_renderer)
    {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = NULL;
    }

    if (sdl_window)
    {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }

    SDL_Quit();
}


//
// I_StartFrame
//
void I_StartFrame(void)
{
    // Nothing to do
}


//
// I_StartTic
//
// Poll for SDL events and post them to the game
//
void I_StartTic(void)
{
    SDL_Event sdl_event;
    event_t event;

    if (!sdl_window)
        return;

    while (SDL_PollEvent(&sdl_event))
    {
        switch (sdl_event.type)
        {
            case SDL_QUIT:
                I_Quit();
                break;

            case SDL_KEYDOWN:
                event.type = ev_keydown;
                event.data1 = xlatekey(sdl_event.key.keysym.scancode);
                if (event.data1)
                    D_PostEvent(&event);
                break;

            case SDL_KEYUP:
                event.type = ev_keyup;
                event.data1 = xlatekey(sdl_event.key.keysym.scancode);
                if (event.data1)
                    D_PostEvent(&event);
                break;

            case SDL_MOUSEBUTTONDOWN:
                event.type = ev_mouse;
                event.data1 = 0;
                if (sdl_event.button.button == SDL_BUTTON_LEFT)
                    event.data1 |= 1;
                if (sdl_event.button.button == SDL_BUTTON_MIDDLE)
                    event.data1 |= 2;
                if (sdl_event.button.button == SDL_BUTTON_RIGHT)
                    event.data1 |= 4;
                event.data2 = event.data3 = 0;
                D_PostEvent(&event);
                break;

            case SDL_MOUSEBUTTONUP:
                event.type = ev_mouse;
                event.data1 = 0;
                // Current button state (others still pressed)
                if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))
                    event.data1 |= 1;
                if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_MIDDLE))
                    event.data1 |= 2;
                if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT))
                    event.data1 |= 4;
                event.data2 = event.data3 = 0;
                D_PostEvent(&event);
                break;

            case SDL_MOUSEMOTION:
                if (mouse_grabbed)
                {
                    event.type = ev_mouse;
                    event.data1 = 0;
                    if (sdl_event.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT))
                        event.data1 |= 1;
                    if (sdl_event.motion.state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
                        event.data1 |= 2;
                    if (sdl_event.motion.state & SDL_BUTTON(SDL_BUTTON_RIGHT))
                        event.data1 |= 4;
                    // Scale relative motion by 4 for sensitivity (matches X11 behavior)
                    event.data2 = sdl_event.motion.xrel << 2;
                    event.data3 = -sdl_event.motion.yrel << 2;  // Invert Y
                    D_PostEvent(&event);
                }
                break;

            case SDL_WINDOWEVENT:
                if (sdl_event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                {
                    if (mouse_grabbed)
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                }
                else if (sdl_event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                {
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                }
                break;
        }
    }
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit(void)
{
    // Nothing to do
}


//
// I_FinishUpdate
//
// Converts the 8-bit indexed framebuffer to 32-bit RGBA and blits it.
// This is based on the X11 implementation in i_video.c lines 393-413.
//
void I_FinishUpdate(void)
{
    static int lasttic;
    int tics;
    int i;
    int x, y, ox, oy;
    byte* src;

    // Draw little dots on the bottom of the screen (devparm mode)
    if (devparm)
    {
        i = I_GetTime();
        tics = i - lasttic;
        lasttic = i;
        if (tics > 20) tics = 20;

        for (i = 0; i < tics*2; i += 2)
            screens[0][(SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
        for (; i < 20*2; i += 2)
            screens[0][(SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;
    }

    // Convert 8-bit indexed framebuffer to 32-bit RGBA, scaling by multiply.
    // This is the core rendering loop from i_video.c lines 393-413.
    src = screens[0];

    for (y = 0; y < SCREENHEIGHT; y++)
    {
        for (x = 0; x < SCREENWIDTH; x++)
        {
            uint32_t pixel = palette[src[y * SCREENWIDTH + x]];
            for (oy = 0; oy < multiply; oy++)
            {
                for (ox = 0; ox < multiply; ox++)
                {
                    pixels[(y * multiply + oy) * window_width + (x * multiply + ox)] = pixel;
                }
            }
        }
    }

    // Upload pixel buffer to GPU texture
    SDL_UpdateTexture(sdl_texture, NULL, pixels, window_width * sizeof(uint32_t));

    // Clear, copy texture to renderer, present
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
}


//
// I_ReadScreen
//
void I_ReadScreen(byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}


//
// I_SetPalette
//
// Builds a 32-bit RGBA lookup table from the DOOM 8-bit palette.
// This is based on i_video.c lines 470-484.
//
void I_SetPalette(byte* pal)
{
    int i;
    int r, g, b;

    for (i = 0; i < 256; i++)
    {
        r = gammatable[usegamma][*pal++];
        g = gammatable[usegamma][*pal++];
        b = gammatable[usegamma][*pal++];

        // SDL_PIXELFORMAT_ARGB8888 format (0xAARRGGBB on little-endian)
        palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}


//
// I_InitGraphics
//
void I_InitGraphics(void)
{
    static int firsttime = 1;

    if (!firsttime)
        return;
    firsttime = 0;

    signal(SIGINT, (void (*)(int)) I_Quit);

    // Check for scaling flags
    if (M_CheckParm("-2"))
        multiply = 2;
    if (M_CheckParm("-3"))
        multiply = 3;
    if (M_CheckParm("-4"))
        multiply = 4;

    window_width = SCREENWIDTH * multiply;
    window_height = SCREENHEIGHT * multiply;

    // Check if user wants to grab mouse
    mouse_grabbed = !!M_CheckParm("-grabmouse");

    // Initialize SDL2 video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        I_Error("SDL_Init failed: %s", SDL_GetError());

    // Create window
    sdl_window = SDL_CreateWindow(
        "DOOM",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_width,
        window_height,
        SDL_WINDOW_SHOWN
    );

    if (!sdl_window)
        I_Error("SDL_CreateWindow failed: %s", SDL_GetError());

    // Create renderer with VSync enabled
    sdl_renderer = SDL_CreateRenderer(
        sdl_window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!sdl_renderer)
        I_Error("SDL_CreateRenderer failed: %s", SDL_GetError());

    // Set scaling quality to nearest-neighbor (pixelated look)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // Create streaming texture for framebuffer
    // Use ARGB8888 for better compatibility on macOS
    sdl_texture = SDL_CreateTexture(
        sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        window_width,
        window_height
    );

    if (!sdl_texture)
        I_Error("SDL_CreateTexture failed: %s", SDL_GetError());

    // Allocate pixel buffer (for scaled 32-bit output)
    pixels = (uint32_t*)malloc(window_width * window_height * sizeof(uint32_t));
    if (!pixels)
        I_Error("Failed to allocate pixel buffer");

    // Note: screens[0] is already allocated by V_Init() - do not allocate it here!

    // Hide mouse cursor
    SDL_ShowCursor(SDL_DISABLE);

    // Grab mouse if requested
    if (mouse_grabbed)
        SDL_SetRelativeMouseMode(SDL_TRUE);

    fprintf(stderr, "I_InitGraphics: SDL2 %dx%d (scale %dx)\n",
            window_width, window_height, multiply);
}

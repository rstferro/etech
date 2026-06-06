#pragma once
/* =====================================================================
 * platform.h — Includes de sistema na ordem correta para Windows +
 *              Raylib + Raygui.
 *
 * Para habilitar a implementação do raygui em UM ÚNICO arquivo .c,
 * defina RAYGUI_IMPLEMENTATION *antes* de incluir este header:
 *
 *   #define RAYGUI_SUPPORT_ICONS 0
 *   #define RAYGUI_IMPLEMENTATION
 *   #include "platform.h"
 *
 * Todos os outros .c apenas fazem:
 *   #include "platform.h"
 * ===================================================================== */

/* 1. Raylib — deve vir antes de raygui.h e de windows.h */
#include "raylib.h"

/* 2. Raygui — depende dos tipos de raylib */
#ifndef RAYGUI_SUPPORT_ICONS
#  define RAYGUI_SUPPORT_ICONS 0
#endif
#include "raygui.h"

/* 3. Win32 — macros para evitar conflito com nomes do raylib/raygui */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  define Rectangle   Rectangle_W32API
#  define ShowCursor  ShowCursor_W32API
#  define DrawText    DrawText_W32API
#  define LoadImage   LoadImage_W32API
#  define CloseWindow CloseWindow_W32API
#  include <windows.h>
#  include <winspool.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <process.h>
#  undef Rectangle
#  undef ShowCursor
#  undef DrawText
#  undef LoadImage
#  undef CloseWindow
#else
#  error "Compile este arquivo apenas no Windows."
#endif

/* 4. Biblioteca padrão */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>

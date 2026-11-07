#pragma once

/*
 * Single source of truth for the graphics knobs shared by the game runner and
 * any launcher. The launcher writes these as environment variables before
 * CreateProcess; the runner reads them once at boot. Keep names and defaults
 * here so a release launcher can expose every one without guessing.
 */

#define PS3_ENV_VFS_ROOT        "PS3_VFS_ROOT"
#define PS3_ENV_TITLE           "PS3_TITLE"
#define PS3_ENV_VERBOSE         "PS3_VERBOSE"
#define PS3_ENV_INTERNAL        "PS3_INTERNAL"
#define PS3_ENV_OUTPUT          "PS3_OUTPUT"
#define PS3_ENV_FSR             "PS3_FSR"
#define PS3_ENV_FSR_SHARPNESS   "PS3_FSR_SHARPNESS"
#define PS3_ENV_FPS_CAP         "PS3_FPS_CAP"
#define PS3_ENV_TICK_HZ         "PS3_TICK_HZ"
#define PS3_ENV_WINDOW_MODE     "PS3_WINDOW_MODE"
#define PS3_ENV_ANISO           "PS3_ANISO"
#define PS3_ENV_LOD_BIAS        "PS3_LOD_BIAS"

#define PS3_DEFAULT_INTERNAL    "720"
#define PS3_DEFAULT_OUTPUT      "1080p"
#define PS3_DEFAULT_FSR         "auto"
#define PS3_DEFAULT_SHARPNESS   "0.6"
#define PS3_DEFAULT_FPS_CAP     "30"
#define PS3_DEFAULT_WINDOW_MODE "borderless"
#define PS3_DEFAULT_ANISO       "16"
#define PS3_DEFAULT_LOD_BIAS    "0"

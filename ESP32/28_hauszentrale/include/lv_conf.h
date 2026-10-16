/**
 * @file lv_conf.h
 * @brief LVGL configuration for Waveshare ESP32-S3-Touch-LCD-7
 *
 * S4.07: Project-specific lv_conf.h (not symlinked from project 19)
 * S4.06: LV_COLOR_DEPTH=16, LV_COLOR_16_SWAP=0 (little-endian RGB565)
 * S4.08: LV_DRAW_SW_ASYNC=1 (async software rendering)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* clang-format off */

/*Enable reports that configuration is not valid*/
#define LV_CONF_CHECK1 1

/*Ovaliate LVGL version*/
#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE 1
#endif

/*---------------------
 * Compiler Config
 *--------------------*/
/*Select valid syntax*/
#define LV_USE_COMPILER 1

/*-----------------
 * Runtime Configs
 *----------------*/

/*Size of type used to store {time, period} on Animations*/
#define LV_ANIM_TIME_MAX 20000

/*Declare the type of the required argument for a custom draw end callback*/
/* #define LV_DRAW_SW_ASM_CUSTOM 0 */

/*1: Use another libpng for MicroPython*/
#define LV_USE_LIBPNG 0

/*-----------------------
 * Display Configs
 *----------------------*/

#define LV_USE_USER_DATA 1

/*Default display refresh, mousepad refresh periods are in milliseconds*/
#define LV_REFR_PERIOD 10

/*Use lvgl definitions*/
#define LV_USE_INTERNAL 1

/*Use external file for font*/
#define LV_USE_EXTERNAL 0

/*Use micro font*/
#define LV_USE_FONT_SUBPX 0

/*Use a method to sort the labels in the stacked objects*/
#define LV_USE_PARSED 0

/*------------------
 * Input Devices
 *----------------*/

/*Input device read periods are in milliseconds*/
#define LV_INPUT_READ_PERIOD 10

/*-------------------
 * COLOR FORMATS
 *-----------------*/

/*Color depth: 1 (S1), 8 (S8), 16 (S16), 32 (S32)*/
#define LV_COLOR_DEPTH 16          /*S4.06: 16-bit RGB565*/

/*Swap the 2 bytes of RGB565 color. Useful if the display has an 8-bit interface*/
#define LV_COLOR_16_SWAP 0         /*Little-endian: R[5bits]/G[6bits]/B[5bits]*/

/*Enable Sub-pixel rendering*/
#define LV_USE_SUBPX 0

/*------------------
 * MONOSPACE FONTS
 *------------------*/

#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_MONTSERRAT_52 0

/*S21.01: Generate custom font set at 110/52/32/21/21 px with degree + level glyphs*/
/*Custom fonts loaded at runtime from flash, not embedded here*/

/*--------------------
 * DRAW PROCESS CONFIGS
 *-------------------*/

/*Enable layer with bottom type*/
#define LV_USE_LAYER_BOTTOM 1

/*Enable center auto rotation*/
#define LV_USE_LV_ASM 0

/*Enable async (aka thread) image decode*/
#define LV_DRAW_SW_ASYNC 1         /*S4.08: Async software rendering*/

/*Use the webp decoder*/
#define LV_USE_LIBWEBP 0

/*---------------------
 * LV Log Library
 *--------------------*/
/*Select method to print log after defined by "LV_LOG_PRINTF()"*/
#define LV_LOG_PRINTF 0

/*Get LV_LOG_LEVEL and LV_LOG_STACKTRACE_ON_ERROR macros*/
#define LV_USE_LOG_STACKTRACE 0

/*-----------------
 * LV ASSERT
 *----------------*/
/*Set 1 definf LV_LOG for assert faild*/
#define LV_USE_LOG 1

/*----------------------------
 * IMAGE DECODER CONFIG
 *---------------------------*/

/*Enable PNG image decoder*/
#define LV_USE_PNG 1               /*S4.07: PNG decoding enabled*/

/*Enable Canvas widget. It can be used to draw SVG image.*/
#define LV_USE_CANVAS 1

/*Enable FreeType font library. This can be used by the Font Builder.*/
#define LV_USE_FREETYPE 0

/*------------------
 * SVG CONFIG
 *-----------------*/

/*Enable SVG decoder handle*/
#define LV_USE_VECTOR_GRAPHIC 0

/*------------------
 * FILE SYSTEM
 *-----------------*/

/*Enable POSIX-style file system (for SPIFFS)*/
#define LV_USE_FS_POSIX 1          /*Mount point: "/"*/
#define LV_USE_FS_STDIO 1          /*Min cluster size: 8 sectors*/
#define LV_FS_STDIO_LETTER '/'   /*Use / for_stdio*/
#define LV_FS_POSIX_LETTER '/'   /*Use / for posix*/

/*Enable Windows-style file system*/
#define LV_USE_FS_WIN32 0

/*---------------------
 * FONT CONFIG
 *---------------------*/

/*Enable Unicode emoji short code getter*/
#define LV_USE_FONT_EMOJI 1
#define LV_FONT_EMOJI_UNICODE_RANGE 0xE000..0xF8FF

/*Enable Unicode high code (4.32 MB)*/
#define LV_USE_FONT_EMOJI_UNICODE 1

/*Enable Unicode high code (4.32 MB)*/
#define LV_USE_FONT_EMOJI_EXTENDED 1

/*------------------
 * STORAGE CONFIG
 *-----------------*/

/*Enable ext storage*/
#define LV_USE_EXT_STORAGE 0

/*------------------
 * DISPLAY CONFIG
 *-----------------*/

/*Set the number of synchronization buffers per DMA channel*/
#define LV_DMA_SYNC_NUM 1

/*Use a separate thread for the display refresh on ARM-RTOS*/
#define LV_USE_DISPLAY_TASK_SEPARATE 0

/*Default display refresh period*/
#define LV_DISPLAY_REFR_PERIOD 10

/*Enable double buffer render mode*/
#define LV_DISPLAY_DOUBLE_BUFFER 0

/*Use external memory for frame buffer (PSRAM)*/
#define LV_USE_EXTERNAL_MEM 1

/*Use frame buffer in PSRAM for RGB panel*/
#define LV_FB_IN_PSRAM 1

/*------------------
 * INPUT DEVICE CONFIG
 *-----------------*/

/*Enable input device reader thread*/
#define LV_USE_INPUT_READER 1
#define LV_INPUT_READER_PERIOD 10

/*Use user data for input device*/
#define LV_USE_INPUT_DEVICE_USER_DATA 1

/*------------------
 * THEME CONFIG
 *-----------------*/

/*Default theme: 0=none (use built-in), 1=light, 2=dark*/
#define LV_THEME_DEFAULT 0

/*Default font*/
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/*Enable scalable theme*/
#define LV_USE_THEME_SCALABLE 1

/*Enable transition animation for themes*/
#define LV_USE_THEME_TRANSITION 1

/*------------------
 * LAYOUT CONFIG
 *-----------------*/

/*Enable calculator layout (grid-like)*/
#define LV_USE_FLEX 1

/*Enable absolute layout*/
#define LV_USE_GRID 1

/*------------------
 * CONTAINER CONFIG
 *-----------------*/

/*Enable container widget*/
#define LV_USE_CONTAINER 1

/*------------------
 * ARC CONFIG
 *-----------------*/

/*Enable arc widget*/
#define LV_USE_ARC 1

/*------------------
 * BAR CONFIG
 *-----------------*/

/*Enable bar widget*/
#define LV_USE_BAR 1

/*------------------
 * BUTTON CONFIG
 *-----------------*/

/*Enable button widget*/
#define LV_USE_BUTTON 1

/*------------------
 * CHART CONFIG
 *-----------------*/

/*Enable chart widget (we use canvas instead, but keep for compatibility)*/
#define LV_USE_CHART 1

/*------------------
 * COLOR SWATCH CONFIG
 *-----------------*/

/*Enable color_swatch widget*/
#define LV_USE_COLOR_SWATCH 0

/*------------------
 * CALENDAR CONFIG
 *-----------------*/

/*Enable calendar widget*/
#define LV_USE_CALENDAR 0

/*------------------
 * CANVAS CONFIG
 *-----------------*/

/*Enable canvas widget*/
#define LV_USE_CANVAS 1

/*------------------
 * CHECKBOX CONFIG
 *-----------------*/

/*Enable checkbox widget*/
#define LV_USE_CHECKBOX 1

/*------------------
 * DATE SELECTOR CONFIG
 *-----------------*/

/*Enable date_selector widget*/
#define LV_USE_DATE_SELECTOR 0

/*------------------
 * DRAW CONFIG
 *-----------------*/

/*Enable software renderer*/
#define LV_USE_DRAW_SW 1

/*Enable ARM 2D renderer*/
#define LV_USE_DRAW_ARM2D 0

/*Enable NXP PXP renderer*/
#define LV_USE_DRAW_PXP 0

/*Enable NXP DMA2D renderer*/
#define LV_USE_DRAW_DMA2D 0

/*------------------
 * IMAGE BUTTON CONFIG
 *-----------------*/

/*Enable image_button widget*/
#define LV_USE_IMAGE_BUTTON 1

/*------------------
 * IMAGE CONFIG
 *-----------------*/

/*Enable image widget*/
#define LV_USE_IMAGE 1

/*------------------
 * LABEL CONFIG
 *-----------------*/

/*Enable label widget*/
#define LV_USE_LABEL 1

/*Use wide mode for label (long text support)*/
/* LV_LABEL_LONG_WRAP removed: LVGL 9.x uses lv_label_long_mode_t enum */
#define LV_LABEL_LONG_TXT_HINT 1

/*------------------
 * LINE CONFIG
 *-----------------*/

/*Enable line widget*/
#define LV_USE_LINE 1

/*------------------
 * LIST CONFIG
 *-----------------*/

/*Enable list widget*/
#define LV_USE_LIST 1

/*------------------
 * MENU CONFIG
 *-----------------*/

/*Enable menu widget*/
#define LV_USE_MENU 1

/*------------------
 * METER CONFIG
 *-----------------*/

/*Enable meter widget*/
#define LV_USE_METER 0

/*------------------
 * MESSAGE BOX CONFIG
 *-----------------*/

/*Enable message_box widget*/
#define LV_USE_MESSAGE_BOX 1

/*------------------
 * SPAN CONFIG
 *-----------------*/

/*Enable span widget*/
#define LV_USE_SPAN 1

/*------------------
 * PAGE CONFIG
 *-----------------*/

/*Enable page widget*/
#define LV_USE_PAGE 1

/*------------------
 * PHOTO BOX CONFIG
 *-----------------*/

/*Enable photo_box (image gallery) widget*/
#define LV_USE_PHOTO_BOX 0

/*------------------
 * ROTARY CONFIG
 *-----------------*/

/*Enable rotary encoder input*/
#define LV_USE_ROTARY 0

/*------------------
 * SLIDER CONFIG
 *-----------------*/

/*Enable slider widget*/
#define LV_USE_SLIDER 1

/*------------------
 * TABLE CONFIG
 *-----------------*/

/*Enable table widget*/
#define LV_USE_TABLE 1

/*------------------
 * TAB VIEW CONFIG
 *-----------------*/

/*Enable tab_view widget*/
#define LV_USE_TABVIEW 1

/*------------------
 * TOOLTIP CONFIG
 *-----------------*/

/*Enable tooltip*/
#define LV_USE_TOOLTIP 1

/*------------------
 * TREE VIEW CONFIG
 *-----------------*/

/*Enable tree_view widget*/
#define LV_USE_TREE_VIEW 1

/*------------------
 * ANIMATION CONFIG
 *-----------------*/

/*Enable animation*/
#define LV_USE_ANIMATION 1

/*------------------
 * EVENT CONFIG
 *-----------------*/

/*Enable event*/
#define LV_USE_EVENT 1

/*------------------
 * STYLES CONFIG
 *-----------------*/

/*Enable style*/
#define LV_USE_STYLE 1

/*------------------
 * STYLE GEN MACRO
 *-----------------*/

/*Enable style generator macro*/
#define LV_USE_STYLE_GEN_MACRO 1

/*------------------
 * EASING CONFIG
 *-----------------*/

/*Enable easing animations*/
#define LV_USE_EASING 1

/*------------------
 * LARGE COORDINATE
 *-----------------*/

/*Enable large coordinate (> 32767)*/
#define LV_USE_LARGE_COORD 1

/*------------------
 * CALL BACK
 *-----------------*/

/*Enable call back*/
#define LV_USE_CALLBACK 1

/*------------------
 * USER DATA
 *-----------------*/

/*Enable user data*/
#define LV_USE_USER_DATA 1

/*------------------
 * FLEX LAYOUT
 *-----------------*/

/*Enable flex layout*/
#define LV_USE_FLEX 1

/*------------------
 * GRID LAYOUT
 *-----------------*/

/*Enable grid layout*/
#define LV_USE_GRID 1

/*------------------
 * PART
 *-----------------*/

/*Enable part*/
#define LV_USE_PART 1

/*------------------
 * RENDER THREAD
 *-----------------*/

/*Use a separate thread for rendering on SMP systems*/
#define LV_USE_RENDER_THREAD 0

/*------------------
 * IDLE HOOK
 *-----------------*/

/*Use the idle hook*/
#define LV_USE_OS_IDLE_HOOK 0

/*------------------
 * Sockets
 *-----------------*/

/*Enable sockets*/
#define LV_USE_SOCKET 0

/*------------------
 * ADAPTER
 *-----------------*/

/*Enable adapter*/
#define LV_USE_ADAPTER 0

/*------------------
 * LOG
 *-----------------*/

/*Use the log library*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN    /*Warn level: errors and warnings*/
#define LV_LOG_PRINTF 0                 /*Use printf for log output*/

/*------------------
 * ASSERT
 *-----------------*/

/*Enable assert*/
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1

/*------------------
 * VERIFY
 *-----------------*/

/*Enable memory verify*/
#define LV_USE_MEM_VERIFY 1

/* clang-format off */

#endif /*LV_CONF_H*/
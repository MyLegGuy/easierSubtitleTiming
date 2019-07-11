// All times are in milliseconds

// Must end in a slash. Folder path appended to the start of the font from the result of GETFONTCOMMAND
#define DEFAULTFONTDIR "/usr/share/fonts/TTF/"
#define GETFONTCOMMAND "fc-match"

// Sentence detection settings
//
// Minimum amount of silence that ends a sentence
#define SENTENCE_SPACE_TIME 400
// Any sentence shorter than this is ignored
#define MIN_SENTENCE_TIME SENTENCE_SPACE_TIME
// 
#define DEFAULTVADMODE 3

// Control
#define MODKEY SDLK_LSHIFT
#define NORMALSEEK 1000
#define MEGASEEK 3000
#define REACTSEEK 400

// UI
#define BARHEIGHT 32
#define INDICATORWIDTH 18
#define FONTSIZE 20
#define TIMEPERPIXEL 25
#define INDENTPIXELS 32
#define CROSSOUTHDENOM 10 // cross out (font) height denominator.
#define EXPLICITCHOPW 3 //
#define TIMEFORMAT "%02d:%02d,%03d"
// "a" is in milliseconds. These argument are given to TIMEFORMAT
#define TIMEARGS(a) (int)(a/60000) ,(int)((a%60000)/1000),(int)(a%1000)

// Colors
#define BACKGROUNDCOLOR MAKERGBA(0,0,0,255)
#define MODBACKGROUNDCOLOR MAKERGBA(0,0,57,255)
#define MARKERCOLOR MAKERGBA(0,255,0,255)
#define PAUSEDMARKERCOLOR MAKERGBA(150,150,150,255);
#define COLORINACTIVESENTENCE MAKERGBA(255,255,255,255)
#define COLORCURRENTSENTENCE MAKERGBA(255,0,0,255)
#define COLORNOSENTENCE BACKGROUNDCOLOR
#define ACTIVESUBCOLOR FC_MakeColor(0,255,0,255)
#define CROSSOUTCOLOR MAKERGBA(255,0,0,255) // The line that goes though skipped subs
#define EXPLICITCHOPCOLOR MAKERGBA(195,110,0,255)
#define ACTIONHISTORYCOLOR FC_MakeColor(150,150,150,255)

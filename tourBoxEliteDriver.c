/*
  compile with:
  
  gcc -o tourBoxEliteDriver tourBoxEliteDriver.c -lusb-1.0
  
*/


/* How many application mappings are supported?
   Each mapping is toggled when switching to a different application
     and has a different mapping section in the settings file.
   If your settings file contains more mappings than this, the extra ones
     will be skipped with a warning message.
   Increasing this number increases the RAM used by the driver. */
#define MAX_NUM_APPS  64

/* How many key sequence steps can be emitted by a single TourBox button
     or 2-button combo?
   Note that the ">" sends in a sequence count as steps, and a quoted
     string implies a ">" send between each character in the string.
   If you define a key sequence longer than this in your settings file,
     it will be skipped with a warning message.
   Increasing this number increases the RAM used by the driver. */
#define MAX_KEY_SEQUENCE_STEPS  100

/* How many sleeps can occur in each key sequence?
   If your define a key sequence with more sleeps than this in your settings
     file, it will be skipped with a warning message.
   Increasing this number increases the RAM used by the driver. */
#define MAX_KEY_SEQUENCE_SLEEPS  10

/* How long can a quoted application name in the settings file be?
   Quoted names longer than this are truncated internally.
   Note that these "names" are meant to be unique patterns to match, and
     not entire window titles.
   Increasing this number increases the RAM used by the driver slightly. */
#define MAX_APPLICATION_NAME_LENGTH  80






/* for popen and pclose */
/* and for nanosleep */
#define _POSIX_C_SOURCE 199309L


#define inline __inline__
#include <stdint.h>

#include <libusb-1.0/libusb.h>

#include <linux/uinput.h>

/* for popen and pclose */
#include <stdio.h>

/* for nanosleep */
#include <time.h>


#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>


/* the VID and PID of a TourBox Elite */
#define TOURBOX_VID 0xC251
#define TOURBOX_PID 0x2005
/* Vendor-Specific interface number */
#define IFACE 1
/* found with lsusb -v */
#define EP_OUT 0x02
/* found with lsusb -v */
#define EP_IN  0x82 
#define USB_TIMEOUT 500


#define NUM_TOURBOX_CONTROLS 20
#define NUM_TOURBOX_PRESS_CONTROLS 14

/* each command arrives from TourBox as 8 bits
   lowest 6 bits encode which control was interacted with
*/

#define TALL          0x00
#define SIDE          0x01
#define TOP           0x02
#define SHORT         0x03
#define KNOB_TURN     0x04
#define SCROLL_TURN   0x09
#define SCROLL_PRESS  0x0A
#define DIAL_TURN     0x0F
#define UP            0x10
#define DOWN          0x11
#define LEFT          0x12
#define RIGHT         0x13
#define C1            0x22
#define C2            0x23
#define TOUR          0x2A
#define KNOB_PRESS    0x37
#define DIAL_PRESS    0x38

/* upper two bits encode press/release or turn direction for turn widgets */
#define PRESS         0x00
#define RELEASE       0x80
#define CCW_DOWN      0x00
#define CW_UP         0x40

#define SCROLL_TURN_DOWN   ( SCROLL_TURN  | CCW_DOWN )
#define SCROLL_TURN_UP     ( SCROLL_TURN  | CW_UP )
#define KNOB_TURN_CCW      ( KNOB_TURN    | CCW_DOWN )
#define KNOB_TURN_CW       ( KNOB_TURN    | CW_UP )
#define DIAL_TURN_CCW      ( DIAL_TURN    | CCW_DOWN )
#define DIAL_TURN_CW       ( DIAL_TURN    | CW_UP )


unsigned char tourBoxControlCodes[ NUM_TOURBOX_CONTROLS ] = {
    TALL,
    SIDE,
    TOP,
    SHORT,
    KNOB_TURN_CCW,
    KNOB_TURN_CW,
    SCROLL_TURN_DOWN,
    SCROLL_TURN_UP,
    SCROLL_PRESS,
    DIAL_TURN_CCW,
    DIAL_TURN_CW,
    UP,
    DOWN,
    LEFT,
    RIGHT,
    C1,
    C2,
    TOUR,
    KNOB_PRESS,
    DIAL_PRESS 
    };

unsigned char tourBoxPressControlCodes[ NUM_TOURBOX_PRESS_CONTROLS ] = {
    TALL,
    SIDE,
    TOP,
    SHORT,
    SCROLL_PRESS,
    UP,
    DOWN,
    LEFT,
    RIGHT,
    C1,
    C2,
    TOUR,
    KNOB_PRESS,
    DIAL_PRESS 
    };

const char *tourBoxControlNames[ NUM_TOURBOX_CONTROLS ] = {
    "TALL",
    "SIDE",
    "TOP",
    "SHORT",
    "KNOB_TURN_CCW",
    "KNOB_TURN_CW",
    "SCROLL_TURN_DOWN",
    "SCROLL_TURN_UP",
    "SCROLL_PRESS",
    "DIAL_TURN_CCW",
    "DIAL_TURN_CW",
    "UP",
    "DOWN",
    "LEFT",
    "RIGHT",
    "C1",
    "C2",
    "TOUR",
    "KNOB_PRESS",
    "DIAL_PRESS" 
    };


#define NUM_TOURBOX_TURN_WIDGETS  3

unsigned char tourBoxTurnWidgets[ NUM_TOURBOX_TURN_WIDGETS ] = {
    KNOB_TURN,
    SCROLL_TURN,
    DIAL_TURN
    };

const char *tourBoxTurnWidgetNames[ NUM_TOURBOX_TURN_WIDGETS ] = {
    "KNOB_TURN",
    "SCROLL_TURN",
    "DIAL_TURN"
    };



/* maps each combo of a primary TURN controls and a held-down modifier (press)
   control to a byte position in the 94-byte TourBox setup message.
   Note that only TURN primary controls are represented in the 94-byte message,
   since only they have haptics and rotation settings.
   
   The final spot in the second index is for non-combo turns of primary
   controls with no other button pressed.
*/
int tourBoxSetupMap[ NUM_TOURBOX_TURN_WIDGETS ]
                   [ NUM_TOURBOX_PRESS_CONTROLS + 1 ];

/* must call this once at startup */
void populateSetupMap( void );


/* equal test for strings */
/* returns 1 if two strings are equal, 0 if not */
char equal( const char *inStringA, const char *inStringB );

/* returns 1 if string inLookIn contains inLookFor */
char contains( const char *inLookIn, const char *inLookFor );

/* returns 1 if string inLookIn starts with inLookFor */
char startsWith( const char *inLookIn, const char *inLookFor );

/* returns pointer into string beyond space or tab characters */
char *skipWhitespace( char *inString );


/* converts string to unsigned base-10 number.  String must start
   with the number.
   Returns -1 on error.*/
int parseNumber( const char *inString );


/* returns index into tourBoxControlCodes
   returns -1 on no match */
int stringToControlIndex( const char *inString );

/* returns string representation of an index in tourBoxControlCodes */
const char *controlIndexToString( int inControlCodeIndex );


/* returns index into more limited tourBoxPressControlCodes
   returns -1 on no match
   returns NUM_TOURBOX_PRESS_CONTROLS (special extra index)
   if inString is NONE */
int stringToPressControlIndex( const char *inString );


/* returns index into tourBoxTurnWidgets
   returns -1 on no match */
int stringToTurnWidgetIndex( const char *inString );



int stringToControlIndex( const char *inString ) {
    int i;
    for( i=0; i<NUM_TOURBOX_CONTROLS; i++ ) {
        if( equal( inString, tourBoxControlNames[i] ) ) {
            return i;
            }
        }
    return -1;
    }



const char *controlIndexToString( int inControlCodeIndex ) {
    return tourBoxControlNames[ inControlCodeIndex ];
    }





int stringToPressControlIndex( const char *inString ) {
    int i;
    int code;
    int fullIndex;

    /* special case
       if code is NONE, then map to last index */
    if( equal( inString, "NONE" ) ) {
        return NUM_TOURBOX_PRESS_CONTROLS;
        }
    
    
    fullIndex = stringToControlIndex( inString );

    if( fullIndex == -1 ) {
        return -1;
        }

    code = tourBoxControlCodes[ fullIndex ];
    
    /* see if it matches a press control code */
    for( i=0; i<NUM_TOURBOX_PRESS_CONTROLS; i++ ) {
        if( tourBoxPressControlCodes[i] == code ) {
            return i;
            }
        }
    return -1;
    }



int stringToTurnWidgetIndex( const char *inString ) {
    int i;
    for( i=0; i<NUM_TOURBOX_TURN_WIDGETS; i++ ) {
        if( equal( inString, tourBoxTurnWidgetNames[i] ) ) {
            return i;
            }
        }
    return -1;
    }



void populateSetupMap( void ) {
    /* this block of code generated from gnumeric spread sheet
       byteMap.gnumeric */
    
    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "NONE" ) ] = 4;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "TALL" ) ] = 6;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "SHORT" ) ] = 8;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "TOP" ) ] = 10;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "SIDE" ) ] = 12;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "NONE" ) ] = 14;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "TALL" ) ] = 16;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "SHORT" ) ] = 18;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "TOP" ) ] = 20;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "SIDE" ) ] = 22;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "NONE" ) ] = 24;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "UP" ) ] = 26;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "DOWN" ) ] = 28;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "LEFT" ) ] = 30;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "RIGHT" ) ] = 32;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "KNOB_PRESS" ) ] = 34;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "SCROLL_PRESS" ) ] = 36;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "DIAL_PRESS" ) ] = 38;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "TOUR" ) ] = 40;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "UP" ) ] = 42;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "DOWN" ) ] = 44;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "LEFT" ) ] = 46;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "RIGHT" ) ] = 48;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "C1" ) ] = 50;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "KNOB_TURN" ) ]
        [ stringToPressControlIndex( "C2" ) ] = 52;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "SCROLL_PRESS" ) ] = 54;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "KNOB_PRESS" ) ] = 56;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "DIAL_PRESS" ) ] = 58;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "TOUR" ) ] = 60;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "C1" ) ] = 62;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "SCROLL_TURN" ) ]
        [ stringToPressControlIndex( "C2" ) ] = 64;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "DIAL_PRESS" ) ] = 66;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "KNOB_PRESS" ) ] = 68;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "SCROLL_PRESS" ) ] = 70;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "TOUR" ) ] = 72;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "UP" ) ] = 74;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "DOWN" ) ] = 76;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "LEFT" ) ] = 78;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "RIGHT" ) ] = 80;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "C1" ) ] = 82;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "C2" ) ] = 84;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "TALL" ) ] = 86;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "SHORT" ) ] = 88;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "TOP" ) ] = 90;

    tourBoxSetupMap[ stringToTurnWidgetIndex( "DIAL_TURN" ) ]
        [ stringToPressControlIndex( "SIDE" ) ] = 92;
    }






typedef struct ApplicationMapping {
        char name[ MAX_APPLICATION_NAME_LENGTH + 1 ];
        
        /*
          first index is the main control being manipulated
           
          second index is another control already held down
              as a modifier,
              or no other control held down (final spot)
        */
        int keyCodeSequenceLength[ NUM_TOURBOX_CONTROLS ]
                                 [ NUM_TOURBOX_PRESS_CONTROLS + 1 ];

        /* Up to 64 uinput KEY codes, separated by KEY_RESERVED to
             send batches of keys as a simultaneous combo.
           If SLEEP_TRIGGER is present, the next sleep in keySequenceSleepsMS
             is used.
        */
        unsigned short keyCodeSquence[ NUM_TOURBOX_CONTROLS ]
                                     [ NUM_TOURBOX_PRESS_CONTROLS + 1 ]
                                     [ MAX_KEY_SEQUENCE_STEPS ];

        /* Sleep times used by any SLEEP_TRIGGER that occurs in
           keyCodeSquence */
        int keySequenceSleepsMS[ NUM_TOURBOX_CONTROLS ]
                               [ NUM_TOURBOX_PRESS_CONTROLS + 1 ]
                               [ MAX_KEY_SEQUENCE_SLEEPS ];
        
        /* 0, 1, 2 for Off, Weak, Strong haptics */
        int hapticStrength[ NUM_TOURBOX_TURN_WIDGETS ]
                          [ NUM_TOURBOX_PRESS_CONTROLS + 1 ];
        
        /* 0, 1, 2 for Slow, Medium, Fast rotation */
        int rotationSpeed[ NUM_TOURBOX_TURN_WIDGETS ]
                         [ NUM_TOURBOX_PRESS_CONTROLS + 1 ];
        
    } ApplicationMapping;




ApplicationMapping appMappings[MAX_NUM_APPS];

int numAppMappings = 0;



/* takes any control in tourBoxControlCodes
   returns an index into tourBoxTurnWidgets, or -1 if control code
   is not a turn widget */
int controlToTurnWidgetIndex( int inTourboxControl );



int controlToTurnWidgetIndex( int inTourboxControl ) {
    int widget = -1;
    int i;
    
    switch( inTourboxControl ) {
        case KNOB_TURN_CW:
        case KNOB_TURN_CCW:
            widget = KNOB_TURN;
            break;
        case DIAL_TURN_CW:
        case DIAL_TURN_CCW:
            widget = DIAL_TURN;
            break;
        case SCROLL_TURN_UP:
        case SCROLL_TURN_DOWN:
            widget = SCROLL_TURN;
            break;
        }

    if( widget != -1 ) {
        for( i=0; i<NUM_TOURBOX_TURN_WIDGETS; i++ ) {
            if( tourBoxTurnWidgets[i] == widget ) {
                return i;
                }
            }
        }     
    
    return -1;
    }






/* maps a string like "KEY_A" to a uinput keycode like KEY_A */
/* returns -1 if there's no mapping */
int stringToKeyCode( char *inString );


/* maps key codes to their string name, or NULL on no match */
const char *keyCodeToString( int inKeyCode );



/* special key code that occurs in an ApplicationMapping to indicate
   a sleep in the sequence */
#define SLEEP_TRIGGER  ( KEY_MAX + 1 )


#define NUM_KEY_CODES 399

int keyCodes[NUM_KEY_CODES] = {
    KEY_ESC,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,
    KEY_MINUS,
    KEY_EQUAL,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_Q,
    KEY_W,
    KEY_E,
    KEY_R,
    KEY_T,
    KEY_Y,
    KEY_U,
    KEY_I,
    KEY_O,
    KEY_P,
    KEY_LEFTBRACE,
    KEY_RIGHTBRACE,
    KEY_ENTER,
    KEY_LEFTCTRL,
    KEY_A,
    KEY_S,
    KEY_D,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_GRAVE,
    KEY_LEFTSHIFT,
    KEY_BACKSLASH,
    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,
    KEY_N,
    KEY_M,
    KEY_COMMA,
    KEY_DOT,
    KEY_SLASH,
    KEY_RIGHTSHIFT,
    KEY_KPASTERISK,
    KEY_LEFTALT,
    KEY_SPACE,
    KEY_CAPSLOCK,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_NUMLOCK,
    KEY_SCROLLLOCK,
    KEY_KP7,
    KEY_KP8,
    KEY_KP9,
    KEY_KPMINUS,
    KEY_KP4,
    KEY_KP5,
    KEY_KP6,
    KEY_KPPLUS,
    KEY_KP1,
    KEY_KP2,
    KEY_KP3,
    KEY_KP0,
    KEY_KPDOT,
    KEY_ZENKAKUHANKAKU,
    KEY_102ND,
    KEY_F11,
    KEY_F12,
    KEY_RO,
    KEY_KATAKANA,
    KEY_HIRAGANA,
    KEY_HENKAN,
    KEY_KATAKANAHIRAGANA,
    KEY_MUHENKAN,
    KEY_KPJPCOMMA,
    KEY_KPENTER,
    KEY_RIGHTCTRL,
    KEY_KPSLASH,
    KEY_SYSRQ,
    KEY_RIGHTALT,
    KEY_LINEFEED,
    KEY_HOME,
    KEY_UP,
    KEY_PAGEUP,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_END,
    KEY_DOWN,
    KEY_PAGEDOWN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_MACRO,
    KEY_MUTE,
    KEY_VOLUMEDOWN,
    KEY_VOLUMEUP,
    KEY_POWER,
    KEY_KPEQUAL,
    KEY_KPPLUSMINUS,
    KEY_PAUSE,
    KEY_SCALE,
    KEY_KPCOMMA,
    KEY_HANGEUL,
    KEY_HANGUEL,
    KEY_HANJA,
    KEY_YEN,
    KEY_LEFTMETA,
    KEY_RIGHTMETA,
    KEY_COMPOSE,
    KEY_STOP,
    KEY_AGAIN,
    KEY_PROPS,
    KEY_UNDO,
    KEY_FRONT,
    KEY_COPY,
    KEY_OPEN,
    KEY_PASTE,
    KEY_FIND,
    KEY_CUT,
    KEY_HELP,
    KEY_MENU,
    KEY_CALC,
    KEY_SETUP,
    KEY_SLEEP,
    KEY_WAKEUP,
    KEY_FILE,
    KEY_SENDFILE,
    KEY_DELETEFILE,
    KEY_XFER,
    KEY_PROG1,
    KEY_PROG2,
    KEY_WWW,
    KEY_MSDOS,
    KEY_COFFEE,
    KEY_SCREENLOCK,
    KEY_DIRECTION,
    KEY_CYCLEWINDOWS,
    KEY_MAIL,
    KEY_BOOKMARKS,
    KEY_COMPUTER,
    KEY_BACK,
    KEY_FORWARD,
    KEY_CLOSECD,
    KEY_EJECTCD,
    KEY_EJECTCLOSECD,
    KEY_NEXTSONG,
    KEY_PLAYPAUSE,
    KEY_PREVIOUSSONG,
    KEY_STOPCD,
    KEY_RECORD,
    KEY_REWIND,
    KEY_PHONE,
    KEY_ISO,
    KEY_CONFIG,
    KEY_HOMEPAGE,
    KEY_REFRESH,
    KEY_EXIT,
    KEY_MOVE,
    KEY_EDIT,
    KEY_SCROLLUP,
    KEY_SCROLLDOWN,
    KEY_KPLEFTPAREN,
    KEY_KPRIGHTPAREN,
    KEY_NEW,
    KEY_REDO,
    KEY_F13,
    KEY_F14,
    KEY_F15,
    KEY_F16,
    KEY_F17,
    KEY_F18,
    KEY_F19,
    KEY_F20,
    KEY_F21,
    KEY_F22,
    KEY_F23,
    KEY_F24,
    KEY_PLAYCD,
    KEY_PAUSECD,
    KEY_PROG3,
    KEY_PROG4,
    KEY_DASHBOARD,
    KEY_SUSPEND,
    KEY_CLOSE,
    KEY_PLAY,
    KEY_FASTFORWARD,
    KEY_BASSBOOST,
    KEY_PRINT,
    KEY_HP,
    KEY_CAMERA,
    KEY_SOUND,
    KEY_QUESTION,
    KEY_EMAIL,
    KEY_CHAT,
    KEY_SEARCH,
    KEY_CONNECT,
    KEY_FINANCE,
    KEY_SPORT,
    KEY_SHOP,
    KEY_ALTERASE,
    KEY_CANCEL,
    KEY_BRIGHTNESSDOWN,
    KEY_BRIGHTNESSUP,
    KEY_MEDIA,
    KEY_SWITCHVIDEOMODE,
    KEY_KBDILLUMTOGGLE,
    KEY_KBDILLUMDOWN,
    KEY_KBDILLUMUP,
    KEY_SEND,
    KEY_REPLY,
    KEY_FORWARDMAIL,
    KEY_SAVE,
    KEY_DOCUMENTS,
    KEY_BATTERY,
    KEY_BLUETOOTH,
    KEY_WLAN,
    KEY_UWB,
    KEY_UNKNOWN,
    KEY_VIDEO_NEXT,
    KEY_VIDEO_PREV,
    KEY_BRIGHTNESS_CYCLE,
    KEY_BRIGHTNESS_ZERO,
    KEY_DISPLAY_OFF,
    KEY_WWAN,
    KEY_WIMAX,
    KEY_RFKILL,
    KEY_MICMUTE,
    KEY_OK,
    KEY_SELECT,
    KEY_GOTO,
    KEY_CLEAR,
    KEY_POWER2,
    KEY_OPTION,
    KEY_INFO,
    KEY_TIME,
    KEY_VENDOR,
    KEY_ARCHIVE,
    KEY_PROGRAM,
    KEY_CHANNEL,
    KEY_FAVORITES,
    KEY_EPG,
    KEY_PVR,
    KEY_MHP,
    KEY_LANGUAGE,
    KEY_TITLE,
    KEY_SUBTITLE,
    KEY_ANGLE,
    KEY_ZOOM,
    KEY_MODE,
    KEY_KEYBOARD,
    KEY_SCREEN,
    KEY_PC,
    KEY_TV,
    KEY_TV2,
    KEY_VCR,
    KEY_VCR2,
    KEY_SAT,
    KEY_SAT2,
    KEY_CD,
    KEY_TAPE,
    KEY_RADIO,
    KEY_TUNER,
    KEY_PLAYER,
    KEY_TEXT,
    KEY_DVD,
    KEY_AUX,
    KEY_MP3,
    KEY_AUDIO,
    KEY_VIDEO,
    KEY_DIRECTORY,
    KEY_LIST,
    KEY_MEMO,
    KEY_CALENDAR,
    KEY_RED,
    KEY_GREEN,
    KEY_YELLOW,
    KEY_BLUE,
    KEY_CHANNELUP,
    KEY_CHANNELDOWN,
    KEY_FIRST,
    KEY_LAST,
    KEY_AB,
    KEY_NEXT,
    KEY_RESTART,
    KEY_SLOW,
    KEY_SHUFFLE,
    KEY_BREAK,
    KEY_PREVIOUS,
    KEY_DIGITS,
    KEY_TEEN,
    KEY_TWEN,
    KEY_VIDEOPHONE,
    KEY_GAMES,
    KEY_ZOOMIN,
    KEY_ZOOMOUT,
    KEY_ZOOMRESET,
    KEY_WORDPROCESSOR,
    KEY_EDITOR,
    KEY_SPREADSHEET,
    KEY_GRAPHICSEDITOR,
    KEY_PRESENTATION,
    KEY_DATABASE,
    KEY_NEWS,
    KEY_VOICEMAIL,
    KEY_ADDRESSBOOK,
    KEY_MESSENGER,
    KEY_DISPLAYTOGGLE,
    KEY_SPELLCHECK,
    KEY_LOGOFF,
    KEY_DOLLAR,
    KEY_EURO,
    KEY_FRAMEBACK,
    KEY_FRAMEFORWARD,
    KEY_CONTEXT_MENU,
    KEY_MEDIA_REPEAT,
    KEY_10CHANNELSUP,
    KEY_10CHANNELSDOWN,
    KEY_IMAGES,
    KEY_DEL_EOL,
    KEY_DEL_EOS,
    KEY_INS_LINE,
    KEY_DEL_LINE,
    KEY_FN,
    KEY_FN_ESC,
    KEY_FN_F1,
    KEY_FN_F2,
    KEY_FN_F3,
    KEY_FN_F4,
    KEY_FN_F5,
    KEY_FN_F6,
    KEY_FN_F7,
    KEY_FN_F8,
    KEY_FN_F9,
    KEY_FN_F10,
    KEY_FN_F11,
    KEY_FN_F12,
    KEY_FN_1,
    KEY_FN_2,
    KEY_FN_D,
    KEY_FN_E,
    KEY_FN_F,
    KEY_FN_S,
    KEY_FN_B,
    KEY_BRL_DOT1,
    KEY_BRL_DOT2,
    KEY_BRL_DOT3,
    KEY_BRL_DOT4,
    KEY_BRL_DOT5,
    KEY_BRL_DOT6,
    KEY_BRL_DOT7,
    KEY_BRL_DOT8,
    KEY_BRL_DOT9,
    KEY_BRL_DOT10,
    KEY_NUMERIC_0,
    KEY_NUMERIC_1,
    KEY_NUMERIC_2,
    KEY_NUMERIC_3,
    KEY_NUMERIC_4,
    KEY_NUMERIC_5,
    KEY_NUMERIC_6,
    KEY_NUMERIC_7,
    KEY_NUMERIC_8,
    KEY_NUMERIC_9,
    KEY_NUMERIC_STAR,
    KEY_NUMERIC_POUND,
    KEY_CAMERA_FOCUS,
    KEY_WPS_BUTTON,
    KEY_TOUCHPAD_TOGGLE,
    KEY_TOUCHPAD_ON,
    KEY_TOUCHPAD_OFF,
    KEY_CAMERA_ZOOMIN,
    KEY_CAMERA_ZOOMOUT,
    KEY_CAMERA_UP,
    KEY_CAMERA_DOWN,
    KEY_CAMERA_LEFT,
    KEY_CAMERA_RIGHT,
    KEY_ATTENDANT_ON,
    KEY_ATTENDANT_OFF,
    KEY_ATTENDANT_TOGGLE,
    KEY_LIGHTS_TOGGLE,
    KEY_ALS_TOGGLE };


const char *keyCodeStrings[NUM_KEY_CODES] = {
    "KEY_ESC",
    "KEY_1",
    "KEY_2",
    "KEY_3",
    "KEY_4",
    "KEY_5",
    "KEY_6",
    "KEY_7",
    "KEY_8",
    "KEY_9",
    "KEY_0",
    "KEY_MINUS",
    "KEY_EQUAL",
    "KEY_BACKSPACE",
    "KEY_TAB",
    "KEY_Q",
    "KEY_W",
    "KEY_E",
    "KEY_R",
    "KEY_T",
    "KEY_Y",
    "KEY_U",
    "KEY_I",
    "KEY_O",
    "KEY_P",
    "KEY_LEFTBRACE",
    "KEY_RIGHTBRACE",
    "KEY_ENTER",
    "KEY_LEFTCTRL",
    "KEY_A",
    "KEY_S",
    "KEY_D",
    "KEY_F",
    "KEY_G",
    "KEY_H",
    "KEY_J",
    "KEY_K",
    "KEY_L",
    "KEY_SEMICOLON",
    "KEY_APOSTROPHE",
    "KEY_GRAVE",
    "KEY_LEFTSHIFT",
    "KEY_BACKSLASH",
    "KEY_Z",
    "KEY_X",
    "KEY_C",
    "KEY_V",
    "KEY_B",
    "KEY_N",
    "KEY_M",
    "KEY_COMMA",
    "KEY_DOT",
    "KEY_SLASH",
    "KEY_RIGHTSHIFT",
    "KEY_KPASTERISK",
    "KEY_LEFTALT",
    "KEY_SPACE",
    "KEY_CAPSLOCK",
    "KEY_F1",
    "KEY_F2",
    "KEY_F3",
    "KEY_F4",
    "KEY_F5",
    "KEY_F6",
    "KEY_F7",
    "KEY_F8",
    "KEY_F9",
    "KEY_F10",
    "KEY_NUMLOCK",
    "KEY_SCROLLLOCK",
    "KEY_KP7",
    "KEY_KP8",
    "KEY_KP9",
    "KEY_KPMINUS",
    "KEY_KP4",
    "KEY_KP5",
    "KEY_KP6",
    "KEY_KPPLUS",
    "KEY_KP1",
    "KEY_KP2",
    "KEY_KP3",
    "KEY_KP0",
    "KEY_KPDOT",
    "KEY_ZENKAKUHANKAKU",
    "KEY_102ND",
    "KEY_F11",
    "KEY_F12",
    "KEY_RO",
    "KEY_KATAKANA",
    "KEY_HIRAGANA",
    "KEY_HENKAN",
    "KEY_KATAKANAHIRAGANA",
    "KEY_MUHENKAN",
    "KEY_KPJPCOMMA",
    "KEY_KPENTER",
    "KEY_RIGHTCTRL",
    "KEY_KPSLASH",
    "KEY_SYSRQ",
    "KEY_RIGHTALT",
    "KEY_LINEFEED",
    "KEY_HOME",
    "KEY_UP",
    "KEY_PAGEUP",
    "KEY_LEFT",
    "KEY_RIGHT",
    "KEY_END",
    "KEY_DOWN",
    "KEY_PAGEDOWN",
    "KEY_INSERT",
    "KEY_DELETE",
    "KEY_MACRO",
    "KEY_MUTE",
    "KEY_VOLUMEDOWN",
    "KEY_VOLUMEUP",
    "KEY_POWER",
    "KEY_KPEQUAL",
    "KEY_KPPLUSMINUS",
    "KEY_PAUSE",
    "KEY_SCALE",
    "KEY_KPCOMMA",
    "KEY_HANGEUL",
    "KEY_HANGUEL",
    "KEY_HANJA",
    "KEY_YEN",
    "KEY_LEFTMETA",
    "KEY_RIGHTMETA",
    "KEY_COMPOSE",
    "KEY_STOP",
    "KEY_AGAIN",
    "KEY_PROPS",
    "KEY_UNDO",
    "KEY_FRONT",
    "KEY_COPY",
    "KEY_OPEN",
    "KEY_PASTE",
    "KEY_FIND",
    "KEY_CUT",
    "KEY_HELP",
    "KEY_MENU",
    "KEY_CALC",
    "KEY_SETUP",
    "KEY_SLEEP",
    "KEY_WAKEUP",
    "KEY_FILE",
    "KEY_SENDFILE",
    "KEY_DELETEFILE",
    "KEY_XFER",
    "KEY_PROG1",
    "KEY_PROG2",
    "KEY_WWW",
    "KEY_MSDOS",
    "KEY_COFFEE",
    "KEY_SCREENLOCK",
    "KEY_DIRECTION",
    "KEY_CYCLEWINDOWS",
    "KEY_MAIL",
    "KEY_BOOKMARKS",
    "KEY_COMPUTER",
    "KEY_BACK",
    "KEY_FORWARD",
    "KEY_CLOSECD",
    "KEY_EJECTCD",
    "KEY_EJECTCLOSECD",
    "KEY_NEXTSONG",
    "KEY_PLAYPAUSE",
    "KEY_PREVIOUSSONG",
    "KEY_STOPCD",
    "KEY_RECORD",
    "KEY_REWIND",
    "KEY_PHONE",
    "KEY_ISO",
    "KEY_CONFIG",
    "KEY_HOMEPAGE",
    "KEY_REFRESH",
    "KEY_EXIT",
    "KEY_MOVE",
    "KEY_EDIT",
    "KEY_SCROLLUP",
    "KEY_SCROLLDOWN",
    "KEY_KPLEFTPAREN",
    "KEY_KPRIGHTPAREN",
    "KEY_NEW",
    "KEY_REDO",
    "KEY_F13",
    "KEY_F14",
    "KEY_F15",
    "KEY_F16",
    "KEY_F17",
    "KEY_F18",
    "KEY_F19",
    "KEY_F20",
    "KEY_F21",
    "KEY_F22",
    "KEY_F23",
    "KEY_F24",
    "KEY_PLAYCD",
    "KEY_PAUSECD",
    "KEY_PROG3",
    "KEY_PROG4",
    "KEY_DASHBOARD",
    "KEY_SUSPEND",
    "KEY_CLOSE",
    "KEY_PLAY",
    "KEY_FASTFORWARD",
    "KEY_BASSBOOST",
    "KEY_PRINT",
    "KEY_HP",
    "KEY_CAMERA",
    "KEY_SOUND",
    "KEY_QUESTION",
    "KEY_EMAIL",
    "KEY_CHAT",
    "KEY_SEARCH",
    "KEY_CONNECT",
    "KEY_FINANCE",
    "KEY_SPORT",
    "KEY_SHOP",
    "KEY_ALTERASE",
    "KEY_CANCEL",
    "KEY_BRIGHTNESSDOWN",
    "KEY_BRIGHTNESSUP",
    "KEY_MEDIA",
    "KEY_SWITCHVIDEOMODE",
    "KEY_KBDILLUMTOGGLE",
    "KEY_KBDILLUMDOWN",
    "KEY_KBDILLUMUP",
    "KEY_SEND",
    "KEY_REPLY",
    "KEY_FORWARDMAIL",
    "KEY_SAVE",
    "KEY_DOCUMENTS",
    "KEY_BATTERY",
    "KEY_BLUETOOTH",
    "KEY_WLAN",
    "KEY_UWB",
    "KEY_UNKNOWN",
    "KEY_VIDEO_NEXT",
    "KEY_VIDEO_PREV",
    "KEY_BRIGHTNESS_CYCLE",
    "KEY_BRIGHTNESS_ZERO",
    "KEY_DISPLAY_OFF",
    "KEY_WWAN",
    "KEY_WIMAX",
    "KEY_RFKILL",
    "KEY_MICMUTE",
    "KEY_OK",
    "KEY_SELECT",
    "KEY_GOTO",
    "KEY_CLEAR",
    "KEY_POWER2",
    "KEY_OPTION",
    "KEY_INFO",
    "KEY_TIME",
    "KEY_VENDOR",
    "KEY_ARCHIVE",
    "KEY_PROGRAM",
    "KEY_CHANNEL",
    "KEY_FAVORITES",
    "KEY_EPG",
    "KEY_PVR",
    "KEY_MHP",
    "KEY_LANGUAGE",
    "KEY_TITLE",
    "KEY_SUBTITLE",
    "KEY_ANGLE",
    "KEY_ZOOM",
    "KEY_MODE",
    "KEY_KEYBOARD",
    "KEY_SCREEN",
    "KEY_PC",
    "KEY_TV",
    "KEY_TV2",
    "KEY_VCR",
    "KEY_VCR2",
    "KEY_SAT",
    "KEY_SAT2",
    "KEY_CD",
    "KEY_TAPE",
    "KEY_RADIO",
    "KEY_TUNER",
    "KEY_PLAYER",
    "KEY_TEXT",
    "KEY_DVD",
    "KEY_AUX",
    "KEY_MP3",
    "KEY_AUDIO",
    "KEY_VIDEO",
    "KEY_DIRECTORY",
    "KEY_LIST",
    "KEY_MEMO",
    "KEY_CALENDAR",
    "KEY_RED",
    "KEY_GREEN",
    "KEY_YELLOW",
    "KEY_BLUE",
    "KEY_CHANNELUP",
    "KEY_CHANNELDOWN",
    "KEY_FIRST",
    "KEY_LAST",
    "KEY_AB",
    "KEY_NEXT",
    "KEY_RESTART",
    "KEY_SLOW",
    "KEY_SHUFFLE",
    "KEY_BREAK",
    "KEY_PREVIOUS",
    "KEY_DIGITS",
    "KEY_TEEN",
    "KEY_TWEN",
    "KEY_VIDEOPHONE",
    "KEY_GAMES",
    "KEY_ZOOMIN",
    "KEY_ZOOMOUT",
    "KEY_ZOOMRESET",
    "KEY_WORDPROCESSOR",
    "KEY_EDITOR",
    "KEY_SPREADSHEET",
    "KEY_GRAPHICSEDITOR",
    "KEY_PRESENTATION",
    "KEY_DATABASE",
    "KEY_NEWS",
    "KEY_VOICEMAIL",
    "KEY_ADDRESSBOOK",
    "KEY_MESSENGER",
    "KEY_DISPLAYTOGGLE",
    "KEY_SPELLCHECK",
    "KEY_LOGOFF",
    "KEY_DOLLAR",
    "KEY_EURO",
    "KEY_FRAMEBACK",
    "KEY_FRAMEFORWARD",
    "KEY_CONTEXT_MENU",
    "KEY_MEDIA_REPEAT",
    "KEY_10CHANNELSUP",
    "KEY_10CHANNELSDOWN",
    "KEY_IMAGES",
    "KEY_DEL_EOL",
    "KEY_DEL_EOS",
    "KEY_INS_LINE",
    "KEY_DEL_LINE",
    "KEY_FN",
    "KEY_FN_ESC",
    "KEY_FN_F1",
    "KEY_FN_F2",
    "KEY_FN_F3",
    "KEY_FN_F4",
    "KEY_FN_F5",
    "KEY_FN_F6",
    "KEY_FN_F7",
    "KEY_FN_F8",
    "KEY_FN_F9",
    "KEY_FN_F10",
    "KEY_FN_F11",
    "KEY_FN_F12",
    "KEY_FN_1",
    "KEY_FN_2",
    "KEY_FN_D",
    "KEY_FN_E",
    "KEY_FN_F",
    "KEY_FN_S",
    "KEY_FN_B",
    "KEY_BRL_DOT1",
    "KEY_BRL_DOT2",
    "KEY_BRL_DOT3",
    "KEY_BRL_DOT4",
    "KEY_BRL_DOT5",
    "KEY_BRL_DOT6",
    "KEY_BRL_DOT7",
    "KEY_BRL_DOT8",
    "KEY_BRL_DOT9",
    "KEY_BRL_DOT10",
    "KEY_NUMERIC_0",
    "KEY_NUMERIC_1",
    "KEY_NUMERIC_2",
    "KEY_NUMERIC_3",
    "KEY_NUMERIC_4",
    "KEY_NUMERIC_5",
    "KEY_NUMERIC_6",
    "KEY_NUMERIC_7",
    "KEY_NUMERIC_8",
    "KEY_NUMERIC_9",
    "KEY_NUMERIC_STAR",
    "KEY_NUMERIC_POUND",
    "KEY_CAMERA_FOCUS",
    "KEY_WPS_BUTTON",
    "KEY_TOUCHPAD_TOGGLE",
    "KEY_TOUCHPAD_ON",
    "KEY_TOUCHPAD_OFF",
    "KEY_CAMERA_ZOOMIN",
    "KEY_CAMERA_ZOOMOUT",
    "KEY_CAMERA_UP",
    "KEY_CAMERA_DOWN",
    "KEY_CAMERA_LEFT",
    "KEY_CAMERA_RIGHT",
    "KEY_ATTENDANT_ON",
    "KEY_ATTENDANT_OFF",
    "KEY_ATTENDANT_TOGGLE",
    "KEY_LIGHTS_TOGGLE",
    "KEY_ALS_TOGGLE" };


/* use KEY_RESERVED to represent a SEND (send combo of keys) in our
   key sequences */
#define SEND_KEY_COMBO   KEY_RESERVED


/* modifiers to set haptics strength and rotation speed */
#define H0 0
#define H1 1
#define H2 2

#define R0 3
#define R1 4
#define R2 5


char equal( const char *inStringA, const char *inStringB ) {
    int i = 0;
    while( inStringA[i] != '\0' && inStringB[i] != '\0' ) {

        if( inStringA[i] != inStringB[i] ) {
            return 0;
            }
        i++;
        }

    /* both at end? */
    if( inStringA[i] != inStringB[i] ) {
        return 0;
        }
    
    return 1;
    }


char contains( const char *inLookIn, const char *inLookFor ) {
    int i = 0;
    int f = 0;
    while( inLookIn[i] != '\0' && inLookFor[f] != '\0' ) {

        if( inLookIn[i] != inLookFor[f] ) {
            /* mismatch, start back at start of inLookFor */
            f = 0;
            }
        else {
            /* match.  keep advancing in inLookFor */
            f++;
            }
        i++;
        }

    if( inLookFor[f] == '\0' ) {
        /* got to end of string we're looking for, meaning we matched it */
        return 1;
        }
    else {
        return 0;
        }
    }



char startsWith( const char *inLookIn, const char *inLookFor ) {
    int i = 0;
    int f = 0;
    
    while( inLookIn[i] != '\0' && inLookFor[f] != '\0' ) {

        if( inLookIn[i] != inLookFor[f] ) {
            /* mismatch, failed */
            return 0;
            }

        /* match.  keep advancing in both strings */
        f++;
        i++;
        }

    if( inLookFor[f] == '\0' ) {
        /* got to end of string we're looking for, meaning we matched it */
        return 1;
        }
    else {
        return 0;
        }
    }


char *skipWhitespace( char *inString ) {
    int i = 0;
    while( inString[i] == ' ' ||
           inString[i] == '\t' ) {
        i++;
        }
    return &( inString[i] );
    }



/* by sticking to 9 digits, we don't need to worry about overflow
   condition above 4 billion-and-something for 32-bit ints*/
#define MAX_PARSE_DIGITS  9

int parseNumber( const char *inString ) {
    int digits[ MAX_PARSE_DIGITS ];

    int numDigits = 0;
    int i;
    int parsedInt = 0;
    int digitPowerOfTen = 1;

    if( inString[ 0 ] < '0' ||
        inString[ 0 ] > '9' ) {
        /* starts with non-int */
        return -1;
        }
    
    while( numDigits < MAX_PARSE_DIGITS &&
           inString[ numDigits ] >= '0' &&
           inString[ numDigits ] <= '9' ) {
        digits[ numDigits ] = inString[ numDigits ] - '0';
        numDigits++;
        }

    if( numDigits >= MAX_PARSE_DIGITS &&
        inString[ numDigits ] >= '0' &&
        inString[ numDigits ] <= '9' ) {
        
        /* more digits left, too long */
        return -1;
        }

    for( i = numDigits - 1; i >= 0; i-- ) {
        parsedInt += digits[i] * digitPowerOfTen;
        digitPowerOfTen *= 10;
        }
    
    return parsedInt;
    }

    
           
    



int stringToKeyCode( char *inString ) {
    int i;

    /* special case, bare > maps to KEY_RESERVED */
    if( equal( inString, ">" ) ) {
        return KEY_RESERVED;
        }
    
    for( i=0; i<NUM_KEY_CODES; i++ ) {
        if( equal( inString, keyCodeStrings[i] ) ) {
            return keyCodes[i];
            }
        }
    return -1;
    }


const char *keyCodeToString( int inKeyCode ) {
    int i;

    /* special case */
    if( inKeyCode == KEY_RESERVED ) {
        return "KEY_RESERVED";
        }
    
    for( i=0; i<NUM_KEY_CODES; i++ ) {
        if( keyCodes[i] == inKeyCode ) {
            return keyCodeStrings[i];
            }
        }
    return NULL;
    }




/* from a source string, parse the next tourbox control code string,
   map it to an index in tourBoxControlCodes, or -1 on failure
   and return a pointer to the next advanced spot in the string (beyond
   the parsed control code string)
   if no valid tourBoxControlName is found, along with -1,
   the return position in inSourceString is not advanced */
char *getNextTourboxCodeIndexAndAdvance( char *inSourceString,
                                         int *outCodeIndex );






/* from source string, parse next tourbox modifier (H0, H1, R1, R2, etc.)
   and return pointer to next advanced spot in string (beyond parsed modifier).
   If no valid modifiere is found, outModifier is set to -1 and
   the return position in inSourceString is not advanced */
char *getNextTourboxModifierAndAdvance( char *inSourceString,
                                        int *outModifier );


/* from a source string, parse the next KEY_ code string,
   or '>', which maps to KEY_RESERVED, or -1 on failue
   and return a pointer to the next advanced spot in the string (beyond
   the parsed control code string)
   if no valid KEY_ code name (or > ) is found, along with -1,
   the return position in inSourceString is not advanced */
char *getNextKeyCodeAndAdvance( char *inSourceString, int *outKeyCode );



/* is a control code index pointing to a code that is a valid code
   in tourBoxPressControlCodes? */
char isPressCode( int inTourBoxControlCodeIndex );


/* maps an index from tourBoxControlCodes to an index
   in tourBoxPressControlCodes, or -1 if inTourBoxControlCodeIndex does
   not map to a press code */
int getPressCodeIndex( int inTourBoxControlCodeIndex );


/* maps and index from tourBoxPressControlCodes to an index
   in tourBoxControlCodes, or -1 on error */
int getControlCodeIndex( int inTourBoxPressControlCodeIndex );


/* reads next space/tab/>/end -delimited token from inSourceString
   returns pointer to spot after token in inSourceString
   fills inTokenBuffer with token, ending with \0
   Also parses tokens surrounded by " marks, even if they contain spaces
   and tabs.  Returned token will start with " and end with " (followed by \0)
   in that case.
   Token can be at most inBufferLength - 1 chars long.
   Longer tokens will be truncated
   but the returned pointer into inSourceString will still be beyond
   the end of the too-long token.
   Note that the '>' counts as the end of a token,
   since we might have token sequences jammed together with > between
   with no spaces.
   If our scan position STARTS with > (or whitespace followed by >),
   however, we simply return that single character as our token.
*/
char *getNextTokenAndAdvance( char *inSourceString,
                              char *inTokenBuffer,
                              unsigned int inBufferLength );


char *getNextTourboxCodeIndexAndAdvance( char *inSourceString,
                                         int *outCodeIndex ) {
    char *nextSpot;
    char token[20];
    
    nextSpot = getNextTokenAndAdvance( inSourceString,
                                       token,
                                       sizeof( token ) );

    *outCodeIndex = stringToControlIndex( token );

    if( *outCodeIndex == -1 ){
        /* rewind string position */
        return inSourceString;
        }
    
    return nextSpot;
    }


char *getNextTourboxModifierAndAdvance( char *inSourceString,
                                        int *outModifier ) {
    char *nextSpot;
    /* long enough to get character AFTER the 2-character pattern that
       we are looking for.  So we don't mistakenly parse H1A as H1
    */
    char token[4];
    
    nextSpot = getNextTokenAndAdvance( inSourceString,
                                       token,
                                       sizeof( token ) );
    *outModifier = -1;

    if( token[2] == '\0' ) {
        /* bad token if it's longer than 2 characters */
        
        if( token[0] == 'H' ) {
            switch( token[1] ) {
                case '0':
                    *outModifier = H0;
                    break;
                case '1':
                    *outModifier = H1;
                    break;
                case '2':
                    *outModifier = H2;
                    break;
                }
            }
        if( token[0] == 'R' ) {
            switch( token[1] ) {
                case '0':
                    *outModifier = R0;
                    break;
                case '1':
                    *outModifier = R1;
                    break;
                case '2':
                    *outModifier = R2;
                    break;
                }
            }
        }

    
    if( *outModifier == -1 ){
        /* rewind string position */
        return inSourceString;
        }
    
    return nextSpot;
    } 



char *getNextKeyCodeAndAdvance( char *inSourceString,
                                int *outKeyCode ) {
    char *nextSpot;
    char token[32];
    
    nextSpot = getNextTokenAndAdvance( inSourceString,
                                       token,
                                       sizeof( token ) );

    *outKeyCode = stringToKeyCode( token );

    if( *outKeyCode == -1 ){
        /* rewind string position */
        return inSourceString;
        }
    
    return nextSpot;
    }




char *getNextTokenAndAdvance( char *inSourceString,
                              char *inTokenBuffer,
                              unsigned int inBufferLength ) {
    unsigned int i = 0;
    unsigned int postSpaceIndex;

    unsigned int bufferPos = 0;
    char isQuote = 0;

    
    /* skip spaces or tabs */
    while( inSourceString[i] == ' ' ||
           inSourceString[i] == '\t' ) {
        i++;
        }

    
    if( inSourceString[i] == '>' ) {
        /* special case, our next token is > */
        inTokenBuffer[0] = '>';
        inTokenBuffer[1] = '\0';

        return &( inSourceString[ i + 1 ] );
        }

    
    if( inSourceString[i] == '"' ) {
        /* start of a quote */
        isQuote = 1;
        
        postSpaceIndex = i + 1;

        inTokenBuffer[0] = '"';
        bufferPos++;
        
        while( inSourceString[ postSpaceIndex ] != '\n' &&
               inSourceString[ postSpaceIndex ] != '\r' &&
               inSourceString[ postSpaceIndex ] != '\0' ) {

            if( bufferPos < inBufferLength - 1 ) {
                inTokenBuffer[ bufferPos ] =  inSourceString[ postSpaceIndex ];
                bufferPos++;
                }
            if( inSourceString[ postSpaceIndex ] == '"' ) {
                /* closing quote */
                postSpaceIndex ++;
                break;
                }

            postSpaceIndex ++;
            }
        }
    
    
    if( ! isQuote ) {
        postSpaceIndex = i;

        while( inSourceString[ postSpaceIndex ] != ' ' &&
               inSourceString[ postSpaceIndex ] != '\t' &&
               inSourceString[ postSpaceIndex ] != '>' &&
               inSourceString[ postSpaceIndex ] != '\n' &&
               inSourceString[ postSpaceIndex ] != '\r' &&
               inSourceString[ postSpaceIndex ] != '\0' ) {

            if( bufferPos < inBufferLength - 1 ) {
                inTokenBuffer[ bufferPos ] =  inSourceString[ postSpaceIndex ];
                bufferPos++;
                }
        
            postSpaceIndex++;
            }
        }

    
    /* terminate buffer
       even if we got to buffer limit before reading entire string
       we skipped the rest of the string above, so we're still
       in a good spot to parse the *next* token after that
       so in that case, our buffer will contain an invalid
       code name, since none are longer than 63 chars */

    inTokenBuffer[ bufferPos ] = '\0';

    return &( inSourceString[ postSpaceIndex ] );
    }



/* returns \0 if called on an empty string */
char getLastChar( char *inString );

    

char getLastChar( char *inString ) {
    int i;
    if( inString[0] == '\0' ) {
        return '\0';
        }
    
    while( inString[i] != '\0' ) {
        i++;
        }
    return inString[ i - 1 ];
    }



typedef struct KeyCodePair {
        int first;
        /* second can be -1 for a single-code (non-pair) */
        int second;
    } KeyCodePair;


/* gets a key code pair needed to type a given printable character
   returns -1, -1 as the pair if the inChar isn't printable */
KeyCodePair getKeyCodePair( char inChar );


KeyCodePair getKeyCodePair( char inChar ) {
    KeyCodePair pair = { -1, -1 };


    if( inChar >= 'a' && inChar <= 'z' ) {
        char codeString[6] = "KEY_X";
        /* convert to upper and put at end of KEY_ */
        inChar = (char)( inChar - 'a' );
        inChar = (char)( inChar + 'A' );
        codeString[4] = inChar;
        pair.first = stringToKeyCode( codeString );
        }
    else if( inChar >= 'A' && inChar <= 'Z' ) {
        char codeString[6] = "KEY_X";
        /* put directly at end of KEY_ */
        codeString[4] = inChar;
        /* send SHIFT 2-key combo */
        pair.first = KEY_LEFTSHIFT;
        pair.second = stringToKeyCode( codeString );
        }
    else if( inChar >= '0' && inChar <= '9' ) {
        char codeString[6] = "KEY_X";
        /* put directly at end of KEY_ */
        codeString[4] = inChar;
        pair.first = stringToKeyCode( codeString );
        }
    else {
        switch( inChar ) {
            case ' ':
                pair.first = KEY_SPACE;
                break;
            case '.':
                pair.first = KEY_DOT;
                break;
            case ',':
                pair.first = KEY_COMMA;
                break;
            case '/':
                pair.first = KEY_SLASH;
                break;
            case ';':
                pair.first = KEY_SEMICOLON;
                break;
            case '\'':
                pair.first = KEY_APOSTROPHE;
                break;
            case '-':
                pair.first = KEY_MINUS;
                break;
            case '=':
                pair.first = KEY_EQUAL;
                break;
            case '`':
                pair.first = KEY_GRAVE;
                break;
            case '[':
                pair.first = KEY_LEFTBRACE;
                break;
            case ']':
                pair.first = KEY_RIGHTBRACE;
                break;
            case '\\':
                pair.first = KEY_BACKSLASH;
                break;
            case '!':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_1;
                break;
            case '@':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_2;
                break;
            case '#':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_3;
                break;
            case '$':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_4;
                break;
            case '%':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_5;
                break;
            case '^':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_6;
                break;
            case '&':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_7;
                break;
            case '*':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_8;
                break;
            case '(':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_9;
                break;
            case ')':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_0;
                break;
            case '_':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_MINUS;
                break;
            case '+':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_EQUAL;
                break;
            case '{':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_LEFTBRACE;
                break;
            case '}':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_RIGHTBRACE;
                break;
            case '|':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_BACKSLASH;
                break;
            case ':':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_SEMICOLON;
                break;
            case '"':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_APOSTROPHE;
                break;
            case '<':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_COMMA;
                break;
            case '>':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_DOT;
                break;
            case '?':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_SLASH;
                break;
            case '~':
                pair.first = KEY_LEFTSHIFT;
                pair.second = KEY_GRAVE;
                break;
            }   
                
        }
    
    
    return pair;
    }


/* counts how many KEY_ codes, including KEY_RESERVED between each keystroke,
   are needed to type a given string of printable characters
   inString must start with a " and end with a "
   returns -1 if a non-printable character is encountered
*/
int countKeyCodeSequence( char *inString );


int countKeyCodeSequence( char *inString ) {
    int i = 1;
    int count = 0;

    while( inString[i] != '"' && inString[i] != '\0' ) {
        KeyCodePair p = getKeyCodePair( inString[i] );

        if( p.first == -1 ) {
            return -1;
            }
        /* for KEY_RESERVED plus the first key in the pair */
        count += 2;

        if( p.second != -1 ) {
            /* for the second key in the pair */
            count++;
            }
        
        i++;
        }

    return count;
    }






char isPressCode( int inTourBoxControlCodeIndex ) {
    int i = getPressCodeIndex( inTourBoxControlCodeIndex );

    if( i == -1 ) {
        return 0;
        }
    else {
        return 1;
        }
    }




int getPressCodeIndex( int inTourBoxControlCodeIndex ) {
    int i;
    int code = tourBoxControlCodes[ inTourBoxControlCodeIndex ];
    
    /* see if it matches a press control code */
    for( i=0; i<NUM_TOURBOX_PRESS_CONTROLS; i++ ) {
        if( tourBoxPressControlCodes[i] == code ) {
            return i;
            }
        }
    return -1;
    }



int getControlCodeIndex( int inTourBoxPressControlCodeIndex ) {
    int i;
    int code = tourBoxPressControlCodes[ inTourBoxPressControlCodeIndex ];
    
    /* see if it matches a control code */
    for( i=0; i<NUM_TOURBOX_CONTROLS; i++ ) {
        if( tourBoxControlCodes[i] == code ) {
            return i;
            }
        }
    return -1;
    }




/* Gets the name of the active window, filling inBuffer.
   If name too long for buffer, it is cut off.
   returns 1 on success, 0 on failure. */
char getActiveWindowName( char *inBuffer, int inBufferSize );


char getActiveWindowName( char *inBuffer, int inBufferSize ) {
    char retVal = 0;
    
    FILE *commandOutput = popen(
        "xprop -id $(xprop -root -f _NET_ACTIVE_WINDOW "
        "0x \" \\$0\\n\" _NET_ACTIVE_WINDOW | "
        "awk '{print $2}') WM_NAME | "
        "awk -F'=' '{print $2}' | sed 's/\"//g'", "r" );
    
    if( commandOutput != NULL ) {

        if( fgets( inBuffer, inBufferSize, commandOutput ) != NULL ) {
            retVal = 1;
            }
        pclose( commandOutput );
        }
    
    return retVal;
    }


/* returns pointer to mapping, or NULL if there's no match. */
ApplicationMapping *getMatchingMapping( const char *inWindowName );


ApplicationMapping *getMatchingMapping( const char *inWindowName ) {
    int i;
    for( i=0; i<numAppMappings; i++ ) {
        ApplicationMapping *m = &( appMappings[i] );

        if( contains( inWindowName, m->name ) ) {
            return m;
            }
        }
    return NULL;
    }



/* makes inMappig active and sends setup message for it.
   returns 1 on success, 0 on failure.*/
char makeMappingActive( ApplicationMapping *inMapping,
                        libusb_device_handle *inUSB );


/* sends default setup message, with haptics turned off for all controls
   returns 1 on success, 0 on failure.*/
char sendDefaultSetupMessage( libusb_device_handle *inUSB );



unsigned char tourBoxSetupMessage[] = {
    0xb5, 0x00, 0x5d, 0x04, 0x00, 0x05, 0x00, 0x06,
    0x00, 0x07, 0x00, 0x08, 0x00, 0x09, 0x00, 0x0b,
    0x00, 0x0c, 0x00, 0x0d, 0x00, 0x0e, 0x00, 0x0f,
    0x00, 0x26, 0x00, 0x27, 0x00, 0x28, 0x00, 0x29,
    0x00, 0x3b, 0x00, 0x3c, 0x00, 0x3d, 0x00, 0x3e,
    0x00, 0x3f, 0x00, 0x40, 0x00, 0x41, 0x00, 0x42,
    0x00, 0x43, 0x00, 0x44, 0x00, 0x45, 0x00, 0x46,
    0x00, 0x47, 0x00, 0x48, 0x00, 0x49, 0x00, 0x4a,
    0x00, 0x4b, 0x00, 0x4c, 0x00, 0x4d, 0x00, 0x4e,
    0x00, 0x4f, 0x00, 0x50, 0x00, 0x51, 0x00, 0x52,
    0x00, 0x53, 0x00, 0x54, 0x00, 0xa8, 0x00, 0xa9,
    0x00, 0xaa, 0x00, 0xab, 0x00, 0xfe };


char sendDefaultSetupMessage( libusb_device_handle *inUSB ) {
    int t;
    int p;

    int numSent;
    int usbResult;
    char success = 0;
    
    int setupIndex;
    for( t=0; t < NUM_TOURBOX_TURN_WIDGETS; t++ ) {
        /* 1 extra mapping (p <=) for turn widget with no modifier */
        for( p=0; p <= NUM_TOURBOX_PRESS_CONTROLS; p++ ) {

            setupIndex = tourBoxSetupMap[t][p];
            tourBoxSetupMessage[ setupIndex ] = 0;
            }
        }
    usbResult =
        libusb_bulk_transfer( inUSB,
                              EP_OUT,
                              tourBoxSetupMessage,
                              sizeof(tourBoxSetupMessage),
                              &numSent,
                              USB_TIMEOUT );
    if( usbResult == 0 &&
        numSent == sizeof( tourBoxSetupMessage ) ) {
        success = 1;
        }
    return success;
    }



char makeMappingActive( ApplicationMapping *inMapping,
                        libusb_device_handle *inUSB ) {
    int i;
    int t;
    int p;
    int h;
    int r;
    unsigned char hByte;
    unsigned char rByte;
    int numSent;
    int usbResult;
    char success = 0;
    
    int setupIndex;
    
    for( i=0; i<numAppMappings; i++ ) {
        ApplicationMapping *m = &( appMappings[i] );
        if( m == inMapping ) {
            /* send 94-byte setup message */

            for( t=0; t < NUM_TOURBOX_TURN_WIDGETS; t++ ) {
                /* 1 extra mapping (p <=) for turn widget with no modifier */
                for( p=0; p <= NUM_TOURBOX_PRESS_CONTROLS; p++ ) {

                    setupIndex = tourBoxSetupMap[t][p];

                    h = m->hapticStrength[t][p];
                    r = m->rotationSpeed[t][p];
                    switch( h ) {
                        case 0:
                            hByte = 0;
                            break;
                        case 1:
                            hByte = 4;
                            break;
                        case 2:
                            hByte = 8;
                            break;
                        }
                    switch( r ) {
                        case 0:
                            rByte = 2;
                            break;
                        case 1:
                            rByte = 1;
                            break;
                        case 2:
                            rByte = 0;
                            break;
                        }
                    tourBoxSetupMessage[ setupIndex ] = hByte | rByte;
                    }
                }
        
            usbResult =
                libusb_bulk_transfer( inUSB,
                                      EP_OUT,
                                      tourBoxSetupMessage,
                                      sizeof(tourBoxSetupMessage),
                                      &numSent,
                                      USB_TIMEOUT );
            if( usbResult == 0 &&
                numSent == sizeof( tourBoxSetupMessage ) ) {
                success = 1;
                }
            }
        }

    return success;
    }


/* emit a uinput event */
void uinputEmit( int inUinputFile, unsigned short inType,
                 unsigned short inCode, int inVal );



void uinputEmit( int inUinputFile, unsigned short inType,
                 unsigned short inCode, int inVal ) {
    struct input_event event;

    event.type = inType;
    event.code = inCode;
    event.value = inVal;
    /* timestamp values below are ignored */
    event.time.tv_sec = 0;
    event.time.tv_usec = 0;

    write( inUinputFile, &event, sizeof(event) );
    }



/* inHeldPressControlIndex is index into tourBoxPressControlCodes or -1
   if nothing is held.
   inControlIndex is index into tourBoxControlCodes */
void sendUinputSequence( int inHeldPressControlIndex,
                         int inControlIndex,
                         ApplicationMapping *inActiveMapping,
                         int inUinputFile );


/* sleeps for a number of milliseconds */
void msSleep( int inNumMilliseconds );


/* track which key presses we have sent as one combo
   at end of combo, we need to send key releases */
unsigned short sentPressComboBuffer[ MAX_KEY_SEQUENCE_STEPS ];


void sendUinputSequence( int inHeldPressControlIndex,
                         int inControlIndex,
                         ApplicationMapping *inActiveMapping,
                         int inUinputFile ) {
    int sequenceLength;
    unsigned short *sequence;
    int *sleepSequence;
    int i, p;
    int lastWasReport = 0;
    int sentPressComboLength = 0;
    int nextSleepIndex = 0;
    
    if( inHeldPressControlIndex == -1 ) {
        /* extra last element in list is for bare control with nothing
           else held down */
        inHeldPressControlIndex = NUM_TOURBOX_PRESS_CONTROLS;
        }
    
    /* get sequence from mapping */
    sequenceLength = inActiveMapping->
        keyCodeSequenceLength[ inControlIndex ][ inHeldPressControlIndex ];


    if( sequenceLength == 0 ) {
        /* emtpy sequence, send nothing */
        return;
        }
    
    
    sequence = inActiveMapping->
        keyCodeSquence[ inControlIndex ][ inHeldPressControlIndex ];
    
    sleepSequence = inActiveMapping->
        keySequenceSleepsMS[ inControlIndex ][ inHeldPressControlIndex ];
    
    /* send it */
    for( i=0; i<sequenceLength; i++ ) {
        if( sequence[i] == KEY_RESERVED ) {
            /* report the end of the press combo , to send them all */
            uinputEmit( inUinputFile, EV_SYN, SYN_REPORT, 0 );

            if( sentPressComboLength > 0 ) {
                /* now send releases for everything in our combo */
                for( p=0; p<sentPressComboLength; p++ ) {
                    uinputEmit( inUinputFile, EV_KEY,
                                sentPressComboBuffer[p], 0 );
                    }
                /* report the end of the release combo */
                uinputEmit( inUinputFile, EV_SYN, SYN_REPORT, 0 );
                }
            
            /* clear the buffer */
            sentPressComboLength = 0;
            
            lastWasReport = 1;
            }
        else if( sequence[i] == SLEEP_TRIGGER &&
                 nextSleepIndex < MAX_KEY_SEQUENCE_SLEEPS ) {
            
            msSleep( sleepSequence[ nextSleepIndex ] );
            nextSleepIndex++;
            }
        else {
            uinputEmit( inUinputFile, EV_KEY, sequence[i], 1 );
            sentPressComboBuffer[ sentPressComboLength ] = sequence[i];
            sentPressComboLength++;
            
            lastWasReport = 0;
            }
        }
    
    if( ! lastWasReport ) {
        /* final report to send the last key combo */
        uinputEmit( inUinputFile, EV_SYN, SYN_REPORT, 0 );

        if( sentPressComboLength > 0 ) {
            /* now send releases for everything in our combo */
            for( p=0; p<sentPressComboLength; p++ ) {
                uinputEmit( inUinputFile, EV_KEY, sentPressComboBuffer[p], 0 );
                }
            /* report the end of the release combo */
            uinputEmit( inUinputFile, EV_SYN, SYN_REPORT, 0 );
            }
        
        /* clear the buffer */
        sentPressComboLength = 0;
        }
    }


void msSleep( int inNumMilliseconds ) {
    struct timespec ts;
    ts.tv_sec = inNumMilliseconds / 1000;
    ts.tv_nsec = ( inNumMilliseconds % 1000 ) * 1000000;

    nanosleep( &ts, NULL );
    }

    



/* processes input byte from TourBox, applying inActiveMapping and generating
   key events to uinput
   If inActiveMapping is NULL, we send no uinput, but we still process
   inputs to track which buttons are held down. */
void handleTourBoxInput( unsigned char inByte,
                         ApplicationMapping *inActiveMapping,
                         int inUinputFile );


/* index into tourBoxPressControlCodes for what button is held
   If multiple buttons are held, the oldest one wins.
*/
int heldPressControlIndex = -1;


void handleTourBoxInput( unsigned char inByte,
                         ApplicationMapping *inActiveMapping,
                         int inUinputFile ) {
    unsigned char controlCode;
    unsigned char actionCode;

    int controlIndex = -1;
    int pressIndex = -1;
    int turnWidgetIndex = -1;
    int i;
    

    /* strip out first 6 bits to get control code */
    controlCode = inByte & 0x3F;
    /* last two bits */
    actionCode = inByte & 0xC0;

    /* first, search for match for our whole byte
       since turn controls map using the whole byte */
    for( i=0; i<NUM_TOURBOX_CONTROLS; i++ ) {
        if( tourBoxControlCodes[i] == inByte ) {
            controlIndex = i;
            break;
            }
        }
    if( controlIndex == -1 ) {
        /* no mapping for whole byte
           this is not a turn control
           check again using only the controlCode portion of the byte */
        for( i=0; i<NUM_TOURBOX_CONTROLS; i++ ) {
            if( tourBoxControlCodes[i] == controlCode ) {
                controlIndex = i;
                break;
                }
            }
        }

    if( controlIndex == -1 ) {
        printf( "Failed to extract known control code "
                "from TourBox input byte 0x%02X\n", inByte );
        return;
        }
    
    
    for( i=0; i<NUM_TOURBOX_PRESS_CONTROLS; i++ ) {
        if( tourBoxPressControlCodes[i] == controlCode ) {
            pressIndex = i;
            break;
            }
        }
    if( pressIndex == -1 ) {
        for( i=0; i<NUM_TOURBOX_TURN_WIDGETS; i++ ) {
            if( tourBoxTurnWidgets[i] == controlCode ) {
                turnWidgetIndex = i;
                break;
                }
            }
        }

    if( pressIndex != -1 ) {
        if( actionCode == PRESS ) {
            if( inActiveMapping != NULL ) {
                /* send event for this press */
                sendUinputSequence( heldPressControlIndex, controlIndex,
                                    inActiveMapping, inUinputFile );
                }
            
            if( heldPressControlIndex == -1 ) {
                heldPressControlIndex = pressIndex;
                }
            /* else something else already held, don't track new
               hold */
            }
        else if( actionCode == RELEASE ) {
            /* we never send events for releases */
            
            if( heldPressControlIndex == pressIndex ) {
                /* a release of what we have marked as held */
                heldPressControlIndex = -1;
                }
            }
        }
    else if( turnWidgetIndex != -1 ) {
        if( inActiveMapping != NULL ) {
            /* send event for this turn */
            sendUinputSequence( heldPressControlIndex, controlIndex,
                                inActiveMapping, inUinputFile );
            }
        }
    }





char inputLoopContinue = 1;


void SigIntHandler( int sig );


void SigIntHandler( int inSig ) {
    printf( "\nGot INT signal (%d), exiting cleanly\n\n", inSig );
    inputLoopContinue = 0;
    }


/* Generates a test settings file that comprehensively tests
   every combination of control inputs */
void generateTestSettingsFile( const char *inOutputFileName );



/* some windows have long names, like firefox windo name for a long google
   search string */
char windowNameBuffer[1024];



int main( int inNumArgs, const char **inArgs ) {
    libusb_context *usbContext = NULL;
    libusb_device_handle *usbHandle = NULL;
    int usbResult;

    int numTransfered;
    ApplicationMapping *activeMapping = NULL;
    char switchResult;
    
    unsigned char initMessage[] =
        { 0x55, 0x00, 0x07, 0x88, 0x94, 0x00, 0x1a, 0xfe };

    unsigned char inputBuffer[ 512 ];

    const char *settingsFileName;

    FILE *settingsFile;

    char fileLineBuffer[ 512 ];

    char readLine = 1;

    int lineCount = 0;

    struct uinput_user_dev uinputUserDev;
    int uinputFile;
    const char *uinputDevName = "TourBox Elite";
    int nameI = 0;
    int kI;

    /*
    generateTestSettingsFile( "testSettings.txt" );
    */
    
    
    signal( SIGINT, SigIntHandler );
    
    populateSetupMap();
    
        
    uinputFile = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );

    if( uinputFile == -1 ) {
        printf( "Failed to open /dev/uinput\n" );
        return 1;
        }
    
    if( ioctl( uinputFile, UI_SET_EVBIT, EV_KEY ) < 0 ) {
        printf( "Error setting up key events on /dev/uinput\n" );
        close( uinputFile );
        return 1;
        }
    
    for( kI=0; kI<NUM_KEY_CODES; kI++ ) {
        if( ioctl( uinputFile, UI_SET_KEYBIT, keyCodes[ kI ] ) < 0 ) {
            printf( "Error enabling key code %s on /dev/uinput\n",
                    keyCodeToString( keyCodes[ kI ] ) );
            close( uinputFile );
            return 1;
            }
        }
    

    memset( &uinputUserDev, 0, sizeof(uinputUserDev) );

    while( uinputDevName[nameI] != '\0' &&
           nameI < UINPUT_MAX_NAME_SIZE - 1 ) {
        uinputUserDev.name[nameI] = uinputDevName[nameI];
        nameI++;
        }
    uinputUserDev.name[nameI] = '\0';
    
    write( uinputFile, &uinputUserDev, sizeof(uinputUserDev) );

    ioctl( uinputFile, UI_DEV_CREATE );
    
    /*
    Start parsing settings file
    */
    
    if( inNumArgs < 2 ) {
        printf( "Expecting settings file as argument\n" );
        return 1;
        }
    settingsFileName = inArgs[1];

    printf( "Using settings file '%s'\n", settingsFileName );


     settingsFile = fopen( settingsFileName, "r" );

    if( settingsFile == NULL ) {
        printf( "Failed to open settings file\n" );
        return 1;
        }


    
    while( readLine ) {
        char *result;

        /* end loop unless we read a valid line */
        readLine = 0;
        
        result =
            fgets( fileLineBuffer, sizeof( fileLineBuffer ), settingsFile );
        
        if( result != NULL ) {
            int nextCharPos = 0;
            
            /* we read a valid line, continue loop */
            readLine = 1;
            lineCount++;

            /* eat whitespace at start of line */
            while( fileLineBuffer[nextCharPos] == ' '
                   ||
                   fileLineBuffer[nextCharPos] == '\t' ) {
                nextCharPos++;
                }
            
            
            /* skip empty or comment line */
            if( fileLineBuffer[nextCharPos] == '#' ||
                fileLineBuffer[nextCharPos] == '\n' ||
                fileLineBuffer[nextCharPos] == '\r' ||
                fileLineBuffer[nextCharPos] == '\0' ) {
                continue;
                }

            if( fileLineBuffer[nextCharPos] == '"' ) {
                /* start of a new app mapping */
                unsigned int numCharsScanned = 0;
                ApplicationMapping *m;
                int h,k;
                
                if( numAppMappings >= MAX_NUM_APPS ) {
                    printf( "\nWARNING:\n"
                            "Reached application limit of %d, and "
                            "encountered another application definition "
                            "on line %d.  "
                            "Skipping rest of settings file.\n\n",
                            MAX_NUM_APPS, lineCount );
                    break;
                    }
                
                /* skip starting double-quote */
                nextCharPos++;

                m = &( appMappings[ numAppMappings ] );
                
                while( numCharsScanned < sizeof( m->name ) - 1
                       &&
                       fileLineBuffer[ nextCharPos ] != '"'
                       &&
                       fileLineBuffer[ nextCharPos ] != '\0'
                       &&
                       fileLineBuffer[ nextCharPos ] != '\n'
                       &&
                       fileLineBuffer[ nextCharPos ] != '\r') {

                    m->name[ numCharsScanned ] =
                        fileLineBuffer[ nextCharPos ];

                    nextCharPos++;
                    numCharsScanned++;
                    }
                m->name[ numCharsScanned ] = '\0';

                if( fileLineBuffer[ nextCharPos ] != '"' ) {
                    printf( "\nWARNING:\n"
                            "Quoted application name "
                            "on line %d is longer than %d characters or "
                            "does not end with a closing double-quote, "
                            "truncating.\n\n",
                            lineCount, sizeof( m->name ) - 1 );
                    }
                
                
                printf( "Processing mappings for \"%s\"\n", m->name );

                for( h=0; h<NUM_TOURBOX_TURN_WIDGETS; h++ ) {
                    for( k=0; k<NUM_TOURBOX_PRESS_CONTROLS + 1; k++ ) {
                        /* default for all unmapped controls
                           rotation slow, haptics off */
                        m->hapticStrength[h][k] = 0;
                        m->rotationSpeed[h][k] = 0;
                        }
                    }
                
                
                numAppMappings++;
                }
            else {
                /* not starting a new applicaiton block, continue app mapping */
                ApplicationMapping *m;
                
                char *nextParsePos;
                int nextCodeIndexA = -1;
                int nextCodeIndexB = -1;
                int nextCodeIndexC = -1;
                int nextSequenceStep = 0;
                int nextKeyCode = -1;
                char gotKeyCode = 1;
                char parseError = 0;
                int hapticStrength = 0;
                int rotationSpeed = 0;
                char hapticFound = 0;
                char rotationFound = 0;
                int nextModifier = -1;
                int nextSleepIndex = 0;
                
                if( numAppMappings == 0 ) {
                    printf( "\nWARNING:\n"
                            "Skipping mapping on line %d that occurs before an"
                            " application (window tile phrase in quotes)"
                            " is defined:\n\n    %s\n",
                            lineCount, &( fileLineBuffer[nextCharPos ] ) );
                    continue;
                    }
                
                /* keep loading mappings into our most recent application */
                m = &( appMappings[ numAppMappings - 1 ] );

                
                /* process the line and add it to mapping */

                nextParsePos = &( fileLineBuffer[ nextCharPos ] );

                nextParsePos =
                    getNextTourboxCodeIndexAndAdvance( nextParsePos,
                                                       &nextCodeIndexA );

                if( nextCodeIndexA == -1 ) {
                    printf( "\nWARNING:\n"
                            "Skipping mapping line %d that starts with "
                            "an invalid TourBox control code:"
                            "\n\n    %s\n",
                            lineCount, &( fileLineBuffer[ nextCharPos ] ) );
                    continue;
                    }
                
                if( isPressCode( nextCodeIndexA ) ) {
                    /* press codes can be a modifier for another
                       code in a 2-code combo */
                    nextParsePos =
                        getNextTourboxCodeIndexAndAdvance( nextParsePos,
                                                           &nextCodeIndexB );
                    }
                else {
                    /* make sure there's no additional code, if
                       nextCodeIndexA is NOT a press code
                       because non-press codes can't lead a 2-code combo */
                    nextParsePos =
                        getNextTourboxCodeIndexAndAdvance( nextParsePos,
                                                           &nextCodeIndexB );
                    if( nextCodeIndexB != -1 ) {
                        printf(
                            "\nWARNING:\n"
                            "Skipping mapping line %d that starts with "
                            "a turn (not press) code leading a 2-code combo:"
                            "\n\n    %s\n",
                            lineCount, &( fileLineBuffer[ nextCharPos ] ) );
                        continue;
                        }
                    }

                /* we've parsed either 1 or 2 codes into A and B
                   make sure there's not a 3rd code after that */
                nextParsePos =
                    getNextTourboxCodeIndexAndAdvance( nextParsePos,
                                                       &nextCodeIndexC );
                if( nextCodeIndexC != -1 ) {
                    printf(
                        "\nWARNING:\n"
                        "Skipping mapping line %d that starts with "
                        "a 3-code combo (only 2-code combos permitted):"
                        "\n\n    %s\n",
                        lineCount, &( fileLineBuffer[ nextCharPos ] ) );
                    continue;
                    }
                
                if( nextCodeIndexB == -1 ) {
                    /* no second code
                       map it into the "no code" index at the end */
                    nextCodeIndexB = NUM_TOURBOX_PRESS_CONTROLS;
                    }
                else {
                    /* a 2-code sequence
                       in our file, the primary control comes SECOND
                       but A is our primary control
                       so swap them */
                    int nextCodeTemp = nextCodeIndexB;
                    nextCodeIndexB = nextCodeIndexA;
                    nextCodeIndexA = nextCodeTemp;

                    /* B needs to be an index into tourBoxPressControlCodes
                       It's currently indexing into tourBoxControlCodes */
                    nextCodeIndexB = getPressCodeIndex( nextCodeIndexB );
                    }
                


                /* Need to parse optional H0, H1, H2, R0, R1, R2 flags
                   that might come after tourbox control pair
                   if second control is a TURN */
                nextModifier = -1;
                nextParsePos =
                    getNextTourboxModifierAndAdvance( nextParsePos,
                                                      &nextModifier );
                /* keep parsing these until there are none left
                   if there are duplicates (H1 followed by H2)
                   the last one will win */
                
                while( nextModifier != -1 ) {
                    switch( nextModifier ) {
                        case H0:
                            hapticFound = 1;
                            hapticStrength = 0;
                            break;
                        case H1:
                            hapticFound = 1;
                            hapticStrength = 1;
                            break; 
                        case H2:
                            hapticFound = 1;
                            hapticStrength = 2;
                            break;
                        case R0:
                            rotationFound = 1;
                            rotationSpeed = 0;
                            break;
                        case R1:
                            rotationFound = 1;
                            rotationSpeed = 1;
                            break; 
                        case R2:
                            rotationFound = 1;
                            rotationSpeed = 2;
                            break;
                        }
                    
                    nextModifier = -1;    
                    nextParsePos =
                        getNextTourboxModifierAndAdvance(
                            nextParsePos,
                            &nextModifier );
                    }
                
                    
                /* if not defined, default to Strong haptics and Fast
                   rotation for all mapped TURN controls */

                if( ! isPressCode( nextCodeIndexA ) ) {
                    /* our primary control is a TURN */
                    if( ! hapticFound ) {
                        hapticStrength = 2;
                        }
                    if( ! rotationFound ) {
                        rotationSpeed = 2;
                        }
                    }
                
                
                while( gotKeyCode ) {
                    gotKeyCode = 0;
                    
                    nextParsePos =
                        getNextKeyCodeAndAdvance( nextParsePos,
                                                  &nextKeyCode );
                    if( nextKeyCode != -1 ) {
                        if( nextSequenceStep >= MAX_KEY_SEQUENCE_STEPS ) {
                            printf(
                                "\nWARNING:\n"
                                "Skipping mapping line %d that has more than "
                                "%d sequence steps:"
                                "\n\n    %s\n",
                                lineCount, MAX_KEY_SEQUENCE_STEPS,
                                &( fileLineBuffer[ nextCharPos ] ) );
                            m->keyCodeSequenceLength
                                [ nextCodeIndexA ]
                                [ nextCodeIndexB ] = 0;
                            parseError = 1;
                            break;
                            }
                        
                        m->keyCodeSquence
                            [ nextCodeIndexA ]
                            [ nextCodeIndexB ]
                            [ nextSequenceStep ] = (unsigned short)nextKeyCode;

                        nextSequenceStep++;
                        
                        m->keyCodeSequenceLength
                            [ nextCodeIndexA ]
                            [ nextCodeIndexB ] = nextSequenceStep;
                        gotKeyCode = 1;
                        }
                    else if( startsWith( skipWhitespace( nextParsePos ),
                                         "SLEEP_" ) ) {
                        /* a SLEEP_ trigger */
                        char sleepToken[20];
                        int c = 0;
                        int parsedMS = -1;

                        /* first, make sure we have room for this sleep */
                        if( nextSequenceStep >= MAX_KEY_SEQUENCE_STEPS ) {
                            printf(
                                "\nWARNING:\n"
                                "Skipping mapping line %d that has more than "
                                "%d sequence steps:"
                                "\n\n    %s\n",
                                lineCount, MAX_KEY_SEQUENCE_STEPS,
                                &( fileLineBuffer[ nextCharPos ] ) );
                            m->keyCodeSequenceLength
                                [ nextCodeIndexA ]
                                [ nextCodeIndexB ] = 0;
                            parseError = 1;
                            break;
                            }
                        if( nextSleepIndex >= MAX_KEY_SEQUENCE_SLEEPS ) {
                            printf(
                                "\nWARNING:\n"
                                "Skipping mapping line %d that has more than "
                                "%d sleeps:"
                                "\n\n    %s\n",
                                lineCount, MAX_KEY_SEQUENCE_SLEEPS,
                                &( fileLineBuffer[ nextCharPos ] ) );
                            m->keyCodeSequenceLength
                                [ nextCodeIndexA ]
                                [ nextCodeIndexB ] = 0;
                            parseError = 1;
                            break;
                            }

                        
                        nextParsePos =
                            getNextTokenAndAdvance( nextParsePos,
                                                    sleepToken,
                                                    sizeof( sleepToken ) );
                        
                        while( sleepToken[c] != '_' &&
                               sleepToken[c] != '\0' ) {
                            c++;
                            }
                        
                        if( sleepToken[c] == '_' ) {
                            c++;
                            parsedMS = parseNumber( &( sleepToken[c] ) );
                            }
                        
                        if( parsedMS == -1 ) {
                            printf(
                                "\nWARNING:\n"
                                "Skipping mapping line %d that has "
                                "badly formatted sleep trigger [%s]."
                                "\n\n    %s\n",
                                lineCount, sleepToken,
                                &( fileLineBuffer[ nextCharPos ] ) );
                            parseError = 1;
                                
                            m->keyCodeSequenceLength
                                [ nextCodeIndexA ]
                                [ nextCodeIndexB ] = 0;
                            break;
                            }
                        
                        m->keyCodeSquence
                            [ nextCodeIndexA ]
                            [ nextCodeIndexB ]
                            [ nextSequenceStep ] =
                            (unsigned short)SLEEP_TRIGGER;

                        nextSequenceStep++;
                        
                        m->keyCodeSequenceLength
                            [ nextCodeIndexA ]
                            [ nextCodeIndexB ] = nextSequenceStep;

                        m->keySequenceSleepsMS
                            [ nextCodeIndexA ]
                            [ nextCodeIndexB ]
                            [ nextSleepIndex ] = parsedMS;

                        nextSleepIndex++;
                        
                        gotKeyCode = 1;
                        }
                    else {
                        /* not a straight-up KEY_ code or a SLEEP_ trigger
                           re-parse and see if it's a quoted string */

                        /* room for the longest quoted string that we
                           might support, since each character requires
                           at lest two key codes */
                        char nextToken[ MAX_KEY_SEQUENCE_STEPS * 2 ];
                        
                        nextParsePos =
                            getNextTokenAndAdvance( nextParsePos,
                                                    nextToken,
                                                    sizeof( nextToken ) );

                        if( nextToken[0] == '"' &&
                            getLastChar( nextToken ) == '"' ) {
                            
                            /* a quoted string */
                            
                            int tokenPos = 1;

                            int keyCodeCount =
                                countKeyCodeSequence( nextToken );

                            if( keyCodeCount == -1 ) {
                                printf(
                                    "\nWARNING:\n"
                                    "Skipping mapping line %d that has "
                                    "quoted string with non-printable "
                                    "character [%s]:"
                                    "\n\n    %s\n",
                                    lineCount, nextToken,
                                    &( fileLineBuffer[ nextCharPos ] ) );

                                parseError = 1;
                                
                                m->keyCodeSequenceLength
                                    [ nextCodeIndexA ]
                                    [ nextCodeIndexB ] = 0;
                                break;
                                }
                            else if( nextSequenceStep + keyCodeCount >
                                MAX_KEY_SEQUENCE_STEPS ) {
                                
                                printf(
                                    "\nWARNING:\n"
                                    "Skipping mapping line %d that has more "
                                    "than %d sequence steps (due to quoted "
                                    "string [%s] "
                                    "that itself requires %d sequence steps "
                                    "to press and send each key):"
                                    "\n\n    %s\n",
                                    lineCount, MAX_KEY_SEQUENCE_STEPS,
                                    nextToken, keyCodeCount,
                                    &( fileLineBuffer[ nextCharPos ] ) );

                                parseError = 1;
                                
                                m->keyCodeSequenceLength
                                    [ nextCodeIndexA ]
                                    [ nextCodeIndexB ] = 0;
                                break;
                                }
                            
                            /* all chars are printable in quoted string
                               AND we have enough room to type them */
                            while( nextToken[ tokenPos ] != '"' ) {
                                KeyCodePair pair =
                                    getKeyCodePair( nextToken[ tokenPos ] );

                                /* make sure we have a > (KEY_RESERVED)
                                   before each character
                                   so they are separate keystrokes
                                   We might have a > in there already
                                   BEFORE our quoted string started
                                   Don't insert KEY_RESERVED if our
                                   quoted string is the first key in our
                                   output sequence.*/
                                if( nextSequenceStep != 0
                                    &&
                                    m->keyCodeSquence
                                    [ nextCodeIndexA ]
                                    [ nextCodeIndexB ]
                                    [ nextSequenceStep - 1 ] != KEY_RESERVED ) {
                                    
                                    m->keyCodeSquence
                                        [ nextCodeIndexA ]
                                        [ nextCodeIndexB ]
                                        [ nextSequenceStep ] = KEY_RESERVED;
                                    nextSequenceStep++;
                                    }
                                
                                
                                m->keyCodeSquence
                                    [ nextCodeIndexA ]
                                    [ nextCodeIndexB ]
                                    [ nextSequenceStep ] =
                                    (unsigned short)( pair.first );
                                
                                nextSequenceStep++;

                                if( pair.second != -1 ) {
                                    m->keyCodeSquence
                                        [ nextCodeIndexA ]
                                        [ nextCodeIndexB ]
                                        [ nextSequenceStep ] =
                                        (unsigned short)( pair.second );
                                    nextSequenceStep++;
                                    }

                                m->keyCodeSequenceLength
                                    [ nextCodeIndexA ]
                                    [ nextCodeIndexB ] = nextSequenceStep;
                                
                                tokenPos++;
                                }
                            gotKeyCode = 1;
                            }
                        else if( nextToken[0] == '"' &&
                            getLastChar( nextToken ) != '"' ) {
                            /* an incomplete quoted string */
                            printf(
                                "\nWARNING:\n"
                                "Skipping mapping line %d that has incomplete "
                                "(or too long) quoted string."
                                "\n\n    %s\n",
                                lineCount,
                                &( fileLineBuffer[ nextCharPos ] ) );
                        
                            m->keyCodeSequenceLength
                                [ nextCodeIndexA ]
                                [ nextCodeIndexB ] = 0;
                            parseError = 1;
                            break;
                            }
                        else if( ! equal( nextToken, "" ) ) {
                            /* NOT a quoted string, and still an invalid
                               KEY_ code or > */
                            
                            /* didn't make it to end of line and parse
                               an empty token */
                            printf(
                                "\nWARNING:\n"
                                "Skipping mapping line %d that has invalid "
                                "key code [%s]:"
                                "\n\n    %s\n",
                                lineCount, nextToken,
                                &( fileLineBuffer[ nextCharPos ] ) );
                        
                            m->keyCodeSequenceLength
                                [ nextCodeIndexA ]
                                [ nextCodeIndexB ] = 0;
                            parseError = 1;
                            break;
                            }
                        }
                    }

                if( parseError ) {
                    /* skip doing anything else with this line */
                    continue;
                    }
                else {
                    int turnWidgetIndex;
                    
                    turnWidgetIndex =
                        controlToTurnWidgetIndex(
                            tourBoxControlCodes[ nextCodeIndexA ] );

                    if( turnWidgetIndex != -1 ) {
                        m->hapticStrength[ turnWidgetIndex ]
                            [ nextCodeIndexB ] = hapticStrength;
                        m->rotationSpeed[ turnWidgetIndex ]
                            [ nextCodeIndexB ] = rotationSpeed;
                        }
                    else if( hapticFound || rotationFound ) {
                        printf(
                            "\nWARNING:\n"
                            "Line %d that has H or R modifiers "
                            "for non-TURN control [%s], ignoring.\n\n",
                            lineCount,
                            controlIndexToString( nextCodeIndexA ) );
                        }
                    }             
                }

            }
        }
    
    
    
    
    usbResult = libusb_init( &usbContext );

    if( usbResult < 0 ) {
        printf( "Failed to initialize libusb context\n" );
        return 1;
        }

    usbHandle =
        libusb_open_device_with_vid_pid( usbContext, TOURBOX_VID, TOURBOX_PID );
    
    if( usbHandle == NULL ){
        printf( "Failed to open TourBox Elite USB device\n" );
        
        libusb_exit( usbContext );
        return 1;
        }
    

    libusb_set_auto_detach_kernel_driver( usbHandle, 1 );

    usbResult = libusb_claim_interface( usbHandle, IFACE );
    
    if( usbResult != 0 ) {
        printf( "Failed to claim TourBox Elite USB interface\n" );
        libusb_close( usbHandle );
        libusb_exit( usbContext );
        return 1;
        }
    
    /* Send the 8-byte init message */
    usbResult = libusb_bulk_transfer( usbHandle, EP_OUT, initMessage,
                                      sizeof( initMessage ),
                                      &numTransfered, USB_TIMEOUT );

    if( numTransfered != sizeof( initMessage ) ) {
        printf( "Failed to send 8-byte setup message to TourBox\n" );
        libusb_release_interface( usbHandle, IFACE );
        libusb_close( usbHandle );
        libusb_exit( usbContext );
        close( uinputFile );
        return 1;
        }

    /* read one response, should be 26 bytes */
    usbResult = libusb_bulk_transfer( usbHandle, EP_IN, inputBuffer,
                                      sizeof( inputBuffer ),
                                      &numTransfered, USB_TIMEOUT );

    if( numTransfered != 26 ) {
        printf( "Failed to read expected 26-byte setup message from "
                "TourBox\n" );
        libusb_release_interface( usbHandle, IFACE );
        libusb_close( usbHandle );
        libusb_exit( usbContext );
        close( uinputFile );
        return 1;
        }


    switchResult = sendDefaultSetupMessage( usbHandle );
    
    if( ! switchResult ) {
        printf( "Failed to send initial defaul setup message to TourBox "
                "for application switch to no mapping\n" );
        inputLoopContinue = 0;
        }


    
    while( inputLoopContinue ) {
        char gotWindowName;
        ApplicationMapping *match;
        char shouldCheckWindowChange = 0;

        /* read single bytes from TourBox and send uinput commands based
           on active mapping */

        usbResult = libusb_bulk_transfer( usbHandle, EP_IN, inputBuffer,
                                          sizeof( inputBuffer ),
                                          &numTransfered,
                                          USB_TIMEOUT );
        
        if( usbResult == 0 && numTransfered == 1 ) {
            /* trigger uniput commands based on active mapping
               even if mapping is NULL, call this to track button
               presses and releases */
            handleTourBoxInput( inputBuffer[0], activeMapping, uinputFile );   
            }
        else if( usbResult == LIBUSB_ERROR_TIMEOUT ) {
            shouldCheckWindowChange = 1;
            }
        else {
            printf( "Error reading single byte message "
                    "from TourBox device\n" );
            inputLoopContinue = 0;
            }
        
        
        
        /* only check for active window name change if we timed out
           waiting for a control to be pressed
           this avoids latency of running xprop when user
           is actively pressing controls quickly.
           Most likely, there will be a pause in TourBox input when the
           user is switching windows, allowing our USB read to timeout */

        if( shouldCheckWindowChange ) {
            
            gotWindowName =
                getActiveWindowName( windowNameBuffer,
                                     sizeof( windowNameBuffer ) );

            if( gotWindowName ) {
                match = getMatchingMapping( windowNameBuffer );

                if( match == NULL ) {
                    /* no mapping for active window */

                    if( activeMapping != NULL ) {
                        switchResult = sendDefaultSetupMessage( usbHandle );
                        if( ! switchResult ) {
                            printf( "Failed to send setup message to TourBox "
                                    "for application switch to no mapping\n" );
                            inputLoopContinue = 0;
                            }
                        }
                    }
                else {
                    /* found a matching mapping */
                    
                    if( match == activeMapping ) {
                        /* already active */
                        }
                    else {
                        switchResult = makeMappingActive( match, usbHandle );

                        if( ! switchResult ) {
                            printf( "Failed to send setup message to TourBox "
                                    "for application switch\n" );
                            inputLoopContinue = 0;
                            }
                        }
                    }
                activeMapping = match;
                }
            }
        }

    
    printf( "\n\nShutting down USB handle and cleaning up.\n" );
    
    libusb_release_interface( usbHandle, IFACE);

    libusb_close( usbHandle );

    libusb_exit( usbContext );

    
    printf( "\n\nClosing /dev/uinput.\n" );

    close( uinputFile );

    
    printf( "Exiting.\n\n" );
    
    return 0;
    }







void generateTestSettingsFile( const char *inOutputFileName ) {
    FILE *f = fopen( inOutputFileName, "w" );
    int p, c;
    
    if( f == NULL ) {
        printf( "Failed to open settings file %s for writing\n",
                inOutputFileName );
        return;
        }


    fprintf( f, "# Replace with a string that matches the window title\n"
             "#    of your text editor.\n\n" );

    fprintf( f, "\"emacs:\"\n\n" );
    
    for( p=-1; p<NUM_TOURBOX_PRESS_CONTROLS; p++ ) {
        const char *pName = "";
        if( p != -1 &&
            getControlCodeIndex(p) != -1 ) {
            
            pName = tourBoxControlNames[ getControlCodeIndex(p) ];
            }
        
        for( c=0; c<NUM_TOURBOX_CONTROLS; c++ ) {
            const char *cName = tourBoxControlNames[ c ];

            if( p != -1 ) {
                fprintf( f, "%s ", pName );
                }
            fprintf( f, "%s ", cName );
            
            if( ! isPressCode( c ) ) {
                /* a turn widget, turn on weak/slow haptics */
                fprintf( f, "H1 R1 " );
                }
            /* have it type a string */
            fprintf( f, "\"" );
            
            if( p != -1 ) {
                fprintf( f, "%s ", pName );
                }
            fprintf( f, "%s\" > KEY_ENTER\n\n", cName );
            }
        }
    

    
    fclose( f );
    }

#pragma once
// Tactical menu

// Menu commands
	// main menu
#define CMD_SEPARATOR			-1
#define CMD_NOTHING				-2
#define CMD_HIDE_MENU			-3
#define CMD_BACK					-4

enum
{
// note: this CMD should be if possible organized as is menu
// (items from the same menu together)
// this makes searching much easier

//main menu level
CMD_WATCH,
CMD_GETIN,
CMD_GETOUT,
CMD_ACTION,

// move submenu

// other commands
CMD_ADVANCE,
CMD_STAY_BACK,
CMD_FLANK_LEFT,
CMD_FLANK_RIGHT,
CMD_NEXT_WAYPOINT,
CMD_HIDE,
CMD_JOIN,
CMD_STOP,
CMD_EXPECT,
CMD_MOVE_SUBMENU,


	// formations
CMD_FORM_COLUMN,
CMD_FORM_STAGCOL,
CMD_FORM_WEDGE,
CMD_FORM_ECHLEFT,
CMD_FORM_ECHRIGHT,
CMD_FORM_VEE,
CMD_FORM_LINE,

// engage
CMD_ENGAGE,
CMD_LOOSE_FORM,
CMD_KEEP_FORM,

CMD_HOLD_FIRE,
CMD_OPEN_FIRE,
CMD_FIRE,
// status
CMD_WATCH_AROUND,
CMD_WATCH_AUTO,
CMD_WATCH_SUBMENU,

#define	N_WATCH_DIR				8
CMD_WATCH_FIRST,
CMD_WATCH_N,
CMD_WATCH_NE,
CMD_WATCH_E,
CMD_WATCH_SE,
CMD_WATCH_S,
CMD_WATCH_SW,
CMD_WATCH_W,
CMD_WATCH_NW,

	// combat modes
CMD_STEALTH,
CMD_COMBAT,
CMD_AWARE,
CMD_SAFE,

CMD_POS_UP,
CMD_POS_DOWN,
CMD_POS_AUTO,
	// teams
CMD_TEAM_MAIN,
CMD_TEAM_RED,
CMD_TEAM_GREEN,
CMD_TEAM_BLUE,
CMD_TEAM_YELLOW,
CMD_ASSIGN_MAIN,
CMD_ASSIGN_RED,
CMD_ASSIGN_GREEN,
CMD_ASSIGN_BLUE,
CMD_ASSIGN_YELLOW,


// radio
CMD_RADIO_ALPHA,
CMD_RADIO_BRAVO,
CMD_RADIO_CHARLIE,
CMD_RADIO_DELTA,
CMD_RADIO_ECHO,
CMD_RADIO_FOXTROT,
CMD_RADIO_GOLF,
CMD_RADIO_HOTEL,
CMD_RADIO_INDIA,
CMD_RADIO_JULIET,

// reply commands

CMD_REPLY_DONE,
CMD_REPLY_FAIL,
CMD_REPLY_COPY,
CMD_REPLY_REPEAT,
CMD_REPLY_WHERE_ARE_YOU,
CMD_REPORT,
CMD_REPLY_ENGAGING,
CMD_REPLY_UNDER_FIRE,
CMD_REPLY_HIT,
CMD_REPLY_ONE_LESS,
CMD_REPLY_FIREREADY,
CMD_REPLY_FIRENOTREADY,
CMD_REPLY_KILLED,
CMD_REPLY_AMMO_LOW,
CMD_REPLY_FUEL_LOW,
CMD_REPLY_INJURED,

CMD_SUPPORT_MEDIC,
CMD_SUPPORT_REPAIR,
CMD_SUPPORT_REARM,
CMD_SUPPORT_REFUEL,
CMD_SUPPORT_DONE,

CMD_RADIO_CUSTOM,
CMD_RADIO_CUSTOM_1,
CMD_RADIO_CUSTOM_2,
CMD_RADIO_CUSTOM_3,
CMD_RADIO_CUSTOM_4,
CMD_RADIO_CUSTOM_5,
CMD_RADIO_CUSTOM_6,
CMD_RADIO_CUSTOM_7,
CMD_RADIO_CUSTOM_8,
CMD_RADIO_CUSTOM_9,
CMD_RADIO_CUSTOM_0
};

#define	N_MOVE_DIR				8
#define	N_MOVE_DIST				6
#define CMD_MOVE_FIRST		0x1000
#define CMD_MOVE_N				CMD_MOVE_FIRST + 0 * N_MOVE_DIST
#define CMD_MOVE_NE				CMD_MOVE_FIRST + 1 * N_MOVE_DIST
#define CMD_MOVE_E				CMD_MOVE_FIRST + 2 * N_MOVE_DIST
#define CMD_MOVE_SE				CMD_MOVE_FIRST + 3 * N_MOVE_DIST
#define CMD_MOVE_S				CMD_MOVE_FIRST + 4 * N_MOVE_DIST
#define CMD_MOVE_SW				CMD_MOVE_FIRST + 5 * N_MOVE_DIST
#define CMD_MOVE_W				CMD_MOVE_FIRST + 6 * N_MOVE_DIST
#define CMD_MOVE_NW				CMD_MOVE_FIRST + 7 * N_MOVE_DIST


#define CMD_GETIN_TARGET	0x3000
#define N_GETIN_POS				5
#define CMD_WATCH_TARGET	0x5000
#define CMD_ACTION_TARGET	0x10000// last range - for enable / disable optimization
#define N_ACTIONS					0x100// max # of actions (except get in, get out)

// Controls

// Control types
#define CT_STATIC						0
#define CT_BUTTON						1
#define CT_EDIT							2
#define CT_SLIDER						3
#define CT_COMBO						4
#define CT_LISTBOX					5
#define CT_TOOLBOX					6
#define CT_CHECKBOXES				7
#define CT_PROGRESS					8
#define CT_HTML							9
#define CT_STATIC_SKEW			10
#define CT_ACTIVETEXT				11
#define CT_TREE							12
#define CT_3DSTATIC					20
#define CT_3DACTIVETEXT			21
#define CT_3DLISTBOX				22
#define CT_3DHTML						23
#define CT_3DSLIDER					24
#define CT_3DEDIT						25
#define CT_3DSCROLLBAR			26
#define CT_OBJECT						80
#define CT_OBJECT_ZOOM			81
#define CT_OBJECT_CONTAINER	82
#define CT_OBJECT_CONT_ANIM	83
#define CT_USER							99
#define CT_MAP							100
#define CT_MAP_MAIN					101

// Static styles
#define ST_HPOS						0x0F
#define ST_LEFT						0
#define ST_RIGHT					1
#define ST_CENTER					2
#define ST_UP							3
#define ST_DOWN						4
#define ST_VCENTER				5

#define ST_TYPE						0xF0
#define ST_SINGLE					0
#define ST_MULTI					16
#define ST_TITLE_BAR			32
#define ST_PICTURE				48
#define ST_FRAME					64
#define ST_BACKGROUND			80
#define ST_GROUP_BOX			96
#define ST_GROUP_BOX2			112
#define ST_HUD_BACKGROUND	128
#define ST_TILE_PICTURE		144
#define ST_WITH_RECT			160
#define ST_LINE						176

#define ST_SHADOW					256
#define ST_NO_RECT				512

#define ST_TITLE					ST_TITLE_BAR + ST_CENTER

// Slider styles
#define SL_DIR						0x01
#define SL_VERT						0
#define SL_HORZ						1

// Tree styles
#define TR_SHOWROOT				1
#define TR_AUTOCOLLAPSE		2

// MessageBox styles
#define MB_BUTTON_OK			1
#define MB_BUTTON_CANCEL	2

// Predefined controls
#define IDC_OK						1
#define IDC_CANCEL				2
#define IDC_AUTOCANCEL		3

#define IDC_MAP							51
#define IDC_WEATHER					52
#define IDC_POSITION				53
#define IDC_TIME						54
#define IDC_DATE						55
#define IDC_BRIEFING				56
#define IDC_MAP_NOTES				57
#define IDC_MAP_PLAN				58
#define IDC_MAP_GEAR				59
#define IDC_MAP_GROUP				60
#define IDC_RADIO_ALPHA			63
#define IDC_RADIO_BRAVO			64
#define IDC_RADIO_CHARLIE		65
#define IDC_RADIO_DELTA			66
#define IDC_RADIO_ECHO			67
#define IDC_RADIO_FOXTROT		68
#define IDC_RADIO_GOLF			69
#define IDC_RADIO_HOTEL			70
#define IDC_RADIO_INDIA			71
#define IDC_RADIO_JULIET		72
#define IDC_MAP_NAME				73
#define IDC_WARRANT					74
#define IDC_GPS							75

#define IDC_HSLIDER					98
#define IDC_VSLIDER					99

// Instances of display
#define IDD_MAIN							0
#define IDD_GAME							1
#define IDD_SINGLE_MISSION		2
#define IDD_OPTIONS						3
#define IDD_CONFIGURE					4
#define IDD_OPTIONS_VIDEO			5
#define IDD_OPTIONS_AUDIO			6
#define IDD_DIFFICULTY				7
#define IDD_MULTIPLAYER				8
#define IDD_MAIN_MAP					12
#define IDD_SAVE							13
#define IDD_END								14
#define IDD_SERVER						17
#define IDD_CLIENT						18
#define IDD_IP_ADDRESS				19
#define IDD_SERVER_SETUP			20
#define IDD_CLIENT_SETUP			21
#define IDD_CLIENT_WAIT				22
#define IDD_CHAT							24
#define IDD_CUSTOM_ARCADE			25
#define IDD_ARCADE_MAP				26
#define IDD_ARCADE_UNIT				27
#define IDD_ARCADE_WAYPOINT		28
#define IDD_TEMPLATE_SAVE			29
#define IDD_TEMPLATE_LOAD			30
#define IDD_LOGIN							31
#define IDD_INTEL							32
#define IDD_CAMPAIGN					33
#define IDD_CREDITS						34
#define IDD_INTEL_GETREADY		37
#define IDD_ARCADE_GROUP			40
#define IDD_ARCADE_SENSOR			41
#define IDD_NEW_USER					42
#define IDD_CAMPAIGN_LOAD			43
#define IDD_ARCADE_EFFECTS		44
#define IDD_ARCADE_MARKER			45
#define IDD_MISSION						46
#define IDD_INTRO							47
#define IDD_OUTRO							48
#define IDD_INTERRUPT					49
#define IDD_DEBRIEFING				50
#define IDD_SELECT_ISLAND			51
#define IDD_SERVER_GET_READY	52
#define IDD_CLIENT_GET_READY	53
#define IDD_INSERT_MARKER			54
#define IDD_VOICE_CHAT				55
#define IDD_DEBUG							56
#define IDD_HINTC							57
#define IDD_MISSION_END				58
#define IDD_SERVER_SIDE				59
#define IDD_CLIENT_SIDE				60
#define IDD_MULTIPLAYER_ROLE	61
#define IDD_AWARD							62
#define IDD_CHANNEL						63
#define IDD_PASSWORD					64
#define IDD_MP_PLAYERS				65
#define IDD_REVERT						66
#define IDD_WIZARD_TEMPLATE		67
#define IDD_WIZARD_MAP				68
#define IDD_PORT							69
#define IDD_MP_SETUP					70
#define IDD_FILTER						71
#define IDD_MODS							72
#define IDD_MODS_FILTER				73
#define IDD_MODS_DOWNLOAD			74
#define IDD_JOIN_REQUIREMENTS		75

// InGameUI
#define IDD_UNITINFO					100
#define IDD_HINT							101

// MessageBoxes
#define IDD_MSG_DELETEPLAYER	200
#define IDD_MSG_DELETEGAME		201
#define IDD_MSG_CLEARTEMPLATE	202
#define IDD_MSG_EXITTEMPLATE	203
#define IDD_MSG_LAUNCHGAME		204

// Main display controls
#define IDC_MAIN_GAME					101
#define IDC_MAIN_OPTIONS			102
#define IDC_MAIN_TRAINING			103
#define IDC_MAIN_CUSTOM				104
#define IDC_MAIN_MULTIPLAYER	105
#define IDC_MAIN_QUIT					106
#define IDC_MAIN_CREDITS			107
#define IDC_MAIN_ARCADE				108
#define IDC_MAIN_PLAYER				109
#define IDC_MAIN_RANK					110
#define IDC_MAIN_ISLAND				111
#define IDC_MAIN_DATE					112
#define IDC_MAIN_MISSION			113
#define IDC_MAIN_CONTINUE			114
#define IDC_MAIN_EDITOR				115
#define IDC_MAIN_BOOK					116
#define IDC_MAIN_SINGLE				117
#define IDC_MAIN_VERSION			118
#define IDC_MAIN_MODS					119

#define IDC_MAIN_LOAD					121
#define IDC_MAIN_SAVE					122

// Mods display controls (RscDisplayMods — top-level MODS screen)
#define IDC_MODS_TITLE				101
#define IDC_MODS_LIST				110
#define IDC_MODS_COL_NAME			111
#define IDC_MODS_COL_VERSION		112
#define IDC_MODS_COL_SIZE			113
#define IDC_MODS_COL_STATE			114
#define IDC_MODS_APPLY				115
#define IDC_MODS_ICON_NAME			116
#define IDC_MODS_ICON_VERSION		117
#define IDC_MODS_ICON_SIZE			118
#define IDC_MODS_ICON_STATE			119
#define IDC_MODS_COL_SOURCE			120
#define IDC_MODS_ICON_SOURCE			121
#define IDC_MODS_SOURCE				122
#define IDC_MODS_FILTER				123
#define IDC_MODS_FILTER_NAME			124
#define IDC_MODS_NOTEBOOK			106
#define IDC_MODS_MASTER_SERVER			7000

// Mods download dialog (RscDisplayModDownload) — opened from Apply when the
// ticked set includes not-yet-downloaded (Available) mods.
#define IDC_MODS_DOWNLOAD_GO			125
#define IDC_MODS_DL_NOTEBOOK			126
#define IDC_MODS_DL_PROMPT			127
#define IDC_MODS_DL_CURRENT_LABEL		128
#define IDC_MODS_DL_CURRENT_TRACK		129
#define IDC_MODS_DL_CURRENT_FILL		130
#define IDC_MODS_DL_OVERALL_LABEL		131
#define IDC_MODS_DL_OVERALL_TRACK		132
#define IDC_MODS_DL_OVERALL_FILL		133
#define IDC_MODS_DL_STATUS			134

// Join-requirements dialog (RscDisplayJoinRequirements)
#define IDC_JOINREQ_TITLE			135
#define IDC_JOINREQ_DIFF				136
#define IDC_JOINREQ_PASSWORD			137

// Single mission display controls
#define IDC_SINGLE_MISSION			101
#define IDC_SINGLE_OVERVIEW			102
#define IDC_SINGLE_MISSION_PAD	103
#define IDC_SINGLE_DIFF					104
#define IDC_SINGLE_LOAD					105

// Game display controls
#define IDC_GAME_SELECT				301
#define IDC_SIDE_NAME					101

// Options display controls
#define IDC_OPTIONS_VIDEO							101
#define IDC_OPTIONS_AUDIO							102
#define IDC_OPTIONS_CONFIGURE					103
#define IDC_OPTIONS_DIFFICULTY				104
#define IDC_OPTIONS_NOTEBOOK					105
#define IDC_OPTIONS_CREDITS						106

#define IDC_OPTIONS_QUALITY_VALUE			101
#define IDC_OPTIONS_QUALITY_SLIDER		102
#define IDC_OPTIONS_VISIBILITY_VALUE	103
#define IDC_OPTIONS_VISIBILITY_SLIDER	104
#define IDC_OPTIONS_RATE_VALUE				105
#define IDC_OPTIONS_RATE_SLIDER				106
#define IDC_OPTIONS_TEXTURES_VALUE		107
#define IDC_OPTIONS_TEXTURES_SLIDER		108
#define IDC_OPTIONS_GAMMA_VALUE 			109
#define IDC_OPTIONS_GAMMA_SLIDER 			110
#define IDC_OPTIONS_BRIGHT_VALUE 			111
#define IDC_OPTIONS_BRIGHT_SLIDER			112
#define IDC_OPTIONS_RESOLUTION				113
#define IDC_OPTIONS_REFRESH						114
#define IDC_OPTIONS_OBJSHADOWS				115
#define IDC_OPTIONS_VEHSHADOWS				116
#define IDC_OPTIONS_CLOUDLETS					117
#define IDC_OPTIONS_HWTL							118
#define IDC_OPTIONS_BLOOD							119
#define IDC_OPTIONS_MULTITEXTURING		120
#define IDC_OPTIONS_WBUFFER						121
#define IDC_OPTIONS_BLOOD_TEXT				122
#define IDC_OPTIONS_TERRAIN						123

#define IDC_OPTIONS_MUSIC_VALUE				101
#define IDC_OPTIONS_MUSIC_SLIDER			102
#define IDC_OPTIONS_EFFECTS_VALUE			103
#define IDC_OPTIONS_EFFECTS_SLIDER		104
#define IDC_OPTIONS_VOICES_VALUE			105
#define IDC_OPTIONS_VOICES_SLIDER			106
#define IDC_OPTIONS_SAMPLING					107
#define IDC_OPTIONS_HWACC							108
#define IDC_OPTIONS_EAX								109
#define IDC_OPTIONS_SINGLE_VOICE			110

#define IDC_DIFFICULTIES_DIFFICULTIES	101
#define IDC_OPTIONS_SUBTITLES					102
#define IDC_OPTIONS_RADIO							103
#define IDC_DIFFICULTIES_DEFAULT			104

// Configure display controls

#define IDC_CONFIG_DEFAULT						101
#define IDC_CONFIG_KEYS								102
#define IDC_CONFIG_XAXIS							103
#define IDC_CONFIG_YAXIS							104
#define IDC_CONFIG_YREVERSED					105
#define IDC_CONFIG_JOYSTICK						106
#define IDC_CONFIG_BUTTONS						107


// Multiplayer display controls
#define IDC_MULTI_TITLE								101
#define IDC_MULTI_SESSIONS						102
#define IDC_MULTI_REMOTE							103
#define IDC_MULTI_NEW									104
#define IDC_MULTI_JOIN								105
#define IDC_MULTI_NOTEBOOK						106
#define IDC_MULTI_PASSWORD						107
#define IDC_MULTI_PORT								108
#define IDC_MULTI_DPLAY								109
#define IDC_MULTI_MASTER_SERVER_LOGO		110
#define IDC_MULTI_SERVER_ICON					111
#define IDC_MULTI_SERVER_COLUMN				112
#define IDC_MULTI_MISSION_ICON				113
#define IDC_MULTI_MISSION_COLUMN			114
#define IDC_MULTI_STATE_ICON					115
#define IDC_MULTI_STATE_COLUMN				116
#define IDC_MULTI_PLAYERS_ICON				117
#define IDC_MULTI_PLAYERS_COLUMN			118
#define IDC_MULTI_PING_ICON						119
#define IDC_MULTI_PING_COLUMN					120
#define IDC_MULTI_PROGRESS						121
#define IDC_MULTI_INTERNET						122
#define IDC_MULTI_REFRESH							123
#define IDC_MULTI_FILTER							124
#define IDC_MULTI_SERVER_FILTER				125
#define IDC_MULTI_MISSION_FILTER			126	
#define IDC_MULTI_PLAYERS_FILTER			127
#define IDC_MULTI_PING_FILTER					128

#define IDC_PASSWORD									101

#define IDC_IP_ADDRESS								101
#define IDC_IP_PORT										102

#define IDC_PORT_PORT									101

#define IDC_FILTER_SERVER							101
#define IDC_FILTER_MISSION						102
#define IDC_FILTER_MAXPING						103
#define IDC_FILTER_MINPLAYERS					104
#define IDC_FILTER_MAXPLAYERS					105
#define IDC_FILTER_FULL								106
#define IDC_FILTER_PASSWORDED					107
#define IDC_FILTER_DEFAULT						108

#define IDC_CLIENT_TEXT								101
#define IDC_CLIENT_PLAYERS						102

#define IDC_SERVER_ISLAND							101
#define IDC_SERVER_MISSION						102
#define IDC_SERVER_EDITOR							103
#define IDC_SERVER_DIFF								104
#define IDC_SERVER_PLAYERS						105

#define IDC_WIZT_TEMPLATES						101
#define IDC_WIZT_OVERVIEW							102
#define IDC_WIZT_NAME									103

#define IDC_WIZM_EDIT									101

#define IDC_SRVSETUP_PLAYERS					101
#define IDC_SRVSETUP_UNITS						102
#define IDC_SRVSETUP_ISLAND						103
#define IDC_SRVSETUP_NAME							104
#define IDC_SRVSETUP_DESC							105
#define IDC_SRVSETUP_ASSIGN						106
#define IDC_SRVSETUP_UNASSIGN					107
#define IDC_SRVSETUP_RANDOM						108
#define IDC_SRVSETUP_PARAM1						109
#define IDC_SRVSETUP_PARAM1_TEXT			110
#define IDC_SRVSETUP_PARAM2						111
#define IDC_SRVSETUP_PARAM2_TEXT			112

#define IDC_SRVSIDE_NAME							101
#define IDC_SRVSIDE_ISLAND						102
#define IDC_SRVSIDE_POOL							103
#define IDC_SRVSIDE_WEST							104
#define IDC_SRVSIDE_EAST							105
#define IDC_SRVSIDE_RESIST						106
#define IDC_SRVSIDE_CIVIL							107
#define IDC_SRVSIDE_WEST_TEXT					108
#define IDC_SRVSIDE_EAST_TEXT					109
#define IDC_SRVSIDE_RESIST_TEXT				110
#define IDC_SRVSIDE_CIVIL_TEXT				111
#define IDC_SRVSIDE_DEFAULT						112
#define IDC_SRVSIDE_PARAM1						113
#define IDC_SRVSIDE_PARAM1_TEXT				114
#define IDC_SRVSIDE_PARAM2						115
#define IDC_SRVSIDE_PARAM2_TEXT				116
#define IDC_SRVSIDE_PLAYERS						117

#define IDC_MPROLE_TITLE							101
#define IDC_MPROLE_NAME								102
#define IDC_MPROLE_ISLAND							103
#define IDC_MPROLE_DESC								104
#define IDC_MPROLE_POOL								105
#define IDC_MPROLE_POOL_TEXT					106
#define IDC_MPROLE_ROLES							107
#define IDC_MPROLE_DEFAULT						108
#define IDC_MPROLE_ENABLE							109
#define IDC_MPROLE_SIDES							110
#define IDC_MPROLE_ENABLE_ALL					111

#define IDC_CLIENT_GAME								101

#define IDC_CLIENTMAP_START1					101
#define IDC_CLIENTMAP_START2					102

#define IDC_SERVER_READY_PLAYERS			110
#define IDC_CLIENT_READY_PLAYERS			110

#define IDC_MP_PLAYERS								101
#define IDC_MP_PL											102
#define IDC_MP_PL_NAME								103
#define IDC_MP_PL_MAIL								104
#define IDC_MP_PL_ICQ									105
#define IDC_MP_PL_REMARK							106
#define IDC_MP_SQ											107
#define IDC_MP_SQ_NAME								108
#define IDC_MP_SQ_ID									109
#define IDC_MP_SQ_MAIL								110
#define IDC_MP_SQ_WEB									111
#define IDC_MP_SQ_PICTURE							112
#define IDC_MP_SQ_TITLE								113
#define IDC_MP_KICKOFF								114
#define IDC_MP_BAN										115
#define IDC_MP_PL_MISSION							116
#define IDC_MP_PL_ISLAND							117
#define IDC_MP_PL_TIME								118
#define IDC_MP_PL_MINPING             119
#define IDC_MP_PL_AVGPING             120
#define IDC_MP_PL_MAXPING             121
#define IDC_MP_PL_MINBAND             122
#define IDC_MP_PL_AVGBAND             123
#define IDC_MP_PL_MAXBAND             124
#define IDC_MP_PL_DESYNC              125
#define IDC_MP_PL_REST								126
#define IDC_MP_MUTE									127
#define IDC_MP_IGNORE								128

#define IDC_CLIENT_WAIT_TITLE					130 // is used together with MP_PL

#define IDC_MPSETUP_NAME							101
#define IDC_MPSETUP_ISLAND						102
#define IDC_MPSETUP_DESC							103
#define IDC_MPSETUP_WEST							104
#define IDC_MPSETUP_EAST							105
#define IDC_MPSETUP_GUERRILA					106
#define IDC_MPSETUP_CIVILIAN					107
#define IDC_MPSETUP_ROLES_TITLE				108
#define IDC_MPSETUP_ROLES							109
#define IDC_MPSETUP_PARAM1_TITLE			110
#define IDC_MPSETUP_PARAM1						111
#define IDC_MPSETUP_PARAM2_TITLE			112
#define IDC_MPSETUP_PARAM2						113
#define IDC_MPSETUP_POOL							114
#define IDC_MPSETUP_MESSAGE						115
#define IDC_MPSETUP_KICK							116
#define IDC_MPSETUP_ENABLE_ALL				117
#define IDC_MPSETUP_LOCK							118

// Main map display controls
#define IDC_MAP_WATCH									101
#define IDC_MAP_COMPASS								102
#define IDC_MAP_WALKIE_TALKIE					103
#define IDC_MAP_NOTEPAD								104
#define IDC_MAP_WARRANT								105
#define IDC_MAP_GPS										106

// Select island display controls
#define IDC_SELECT_ISLAND							101
#define IDC_SELECT_ISLAND_NOTEBOOK		102
#define IDC_SELECT_ISLAND_WIZARD			103			

// Custom arcade display controls
#define IDC_CUST_GAME									101
#define IDC_CUST_PLAY									102
#define IDC_CUST_EDIT									103
#define IDC_CUST_DELETE								104

#define IDC_ARCMAP_LOAD								101
#define IDC_ARCMAP_SAVE								102
#define IDC_ARCMAP_CLEAR							103
#define IDC_ARCMAP_MODE								104
#define IDC_ARCMAP_INTEL							105
#define IDC_ARCMAP_MERGE							106
#define IDC_ARCMAP_PREVIEW						107
#define IDC_ARCMAP_CONTINUE						108
#define IDC_ARCMAP_SECTION						109
#define IDC_ARCMAP_DIFF								110
#define IDC_ARCMAP_IDS								111
#define IDC_ARCMAP_TEXTURES						112

#define IDC_ARCUNIT_TITLE							101
#define IDC_ARCUNIT_SIDE							102						
#define IDC_ARCUNIT_VEHICLE						103
#define IDC_ARCUNIT_RANK							104
#define IDC_ARCUNIT_CTRL							105
#define IDC_ARCUNIT_CLASS							107
#define IDC_ARCUNIT_HEALTH						108
#define IDC_ARCUNIT_FUEL							109
#define IDC_ARCUNIT_AMMO							110
#define IDC_ARCUNIT_AZIMUT						111
#define IDC_ARCUNIT_SPECIAL						112
#define IDC_ARCUNIT_AGE								113
#define IDC_ARCUNIT_AZIMUT_PICTURE		114
#define IDC_ARCUNIT_PLACE							115
#define IDC_ARCUNIT_PRESENCE					116
#define IDC_ARCUNIT_PRESENCE_COND			117
#define IDC_ARCUNIT_TEXT							118
#define IDC_ARCUNIT_LOCK							119
#define IDC_ARCUNIT_INIT							120
#define IDC_ARCUNIT_SKILL							121

#define IDC_ARCGRP_SIDE								101
#define IDC_ARCGRP_TYPE								102
#define IDC_ARCGRP_NAME								103
#define IDC_ARCGRP_AZIMUT							104
#define IDC_ARCGRP_AZIMUT_PICTURE			105

#define IDC_ARCWP_TITLE								101
#define IDC_ARCWP_TYPE								102
#define IDC_ARCWP_SEQ									103
#define IDC_ARCWP_DESC								104
#define IDC_ARCWP_SEMAPHORE						105
#define IDC_ARCWP_FORM								106
#define IDC_ARCWP_SPEED								107
#define IDC_ARCWP_COMBAT							108
#define IDC_ARCWP_PLACE								109
#define IDC_ARCWP_EFFECTS							110
#define IDC_ARCWP_TIMEOUT_MIN					111
#define IDC_ARCWP_TIMEOUT_MAX					112
#define IDC_ARCWP_TIMEOUT_MID					113
#define IDC_ARCWP_HOUSEPOS						114
#define	IDC_ARCWP_HOUSEPOSTEXT				115
#define IDC_ARCWP_EXPACTIV						116
#define IDC_ARCWP_SHOW								117
#define IDC_ARCWP_EXPCOND							118
#define IDC_ARCWP_SCRIPT							119

#define IDC_ARCEFF_CAMERA							101
#define IDC_ARCEFF_CAMPOS							102
#define IDC_ARCEFF_SOUND							103
#define IDC_ARCEFF_VOICE							104
#define IDC_ARCEFF_SOUND_ENV					105
#define IDC_ARCEFF_SOUND_DET					106
#define IDC_ARCEFF_MUSIC							107
#define IDC_ARCEFF_TITTYPE						108
#define IDC_ARCEFF_TITEFF							109
#define IDC_ARCEFF_TITTEXT						110
#define IDC_ARCEFF_TITRES							111
#define IDC_ARCEFF_TITOBJ							112
#define IDC_ARCEFF_CONDITION					113
#define IDC_ARCEFF_TEXT_TITTEXT				114

#define IDC_ARCSENS_TITLE							101
#define IDC_ARCSENS_A									102
#define IDC_ARCSENS_B									103
#define IDC_ARCSENS_ANGLE							104
#define IDC_ARCSENS_ACTIV							105
#define IDC_ARCSENS_PRESENCE					106
#define IDC_ARCSENS_REPEATING					107
#define IDC_ARCSENS_INTERRUPT					108
#define IDC_ARCSENS_TIMEOUT_MIN				109
#define IDC_ARCSENS_TIMEOUT_MAX				110
#define IDC_ARCSENS_TIMEOUT_MID				111
#define IDC_ARCSENS_TYPE							112
#define IDC_ARCSENS_OBJECT						113
#define IDC_ARCSENS_TEXT							114
#define IDC_ARCSENS_AGE								115
#define IDC_ARCSENS_EFFECTS						116
#define IDC_ARCSENS_EXPCOND						117
#define IDC_ARCSENS_EXPACTIV					118
#define IDC_ARCSENS_EXPDESACTIV				119
#define IDC_ARCSENS_RECT							120
#define IDC_ARCSENS_NAME							121

#define IDC_ARCMARK_TITLE							101
#define IDC_ARCMARK_NAME							102
#define IDC_ARCMARK_MARKER						103
#define IDC_ARCMARK_TYPE							104
#define IDC_ARCMARK_COLOR							105
#define IDC_ARCMARK_A									106
#define IDC_ARCMARK_B									107
#define IDC_ARCMARK_ANGLE							108
#define IDC_ARCMARK_TYPE_TEXT					109
#define IDC_ARCMARK_FILL							110
#define IDC_ARCMARK_TEXT							111

#define IDC_INTEL_RESISTANCE					101
#define IDC_INTEL_MONTH								102
#define IDC_INTEL_DAY									103
#define IDC_INTEL_HOUR								104
#define IDC_INTEL_MINUTE							105
#define IDC_INTEL_BRIEFING_NAME				106
#define IDC_INTEL_BRIEFING_DESC				107
#define IDC_INTEL_WEATHER							108
#define IDC_INTEL_FOG									109
#define IDC_INTEL_WEATHER_FORECAST		110
#define IDC_INTEL_FOG_FORECAST				111

// Chat
#define IDC_CHANNEL										101

#define IDC_CHAT											101

#define IDC_VOICE_CHAT								101

// Save / load template
#define IDC_TEMPL_NAME								101
#define IDC_TEMPL_TITLE								102
#define IDC_TEMPL_MODE								103
#define IDC_TEMPL_ISLAND							104

// Login display
#define IDC_LOGIN_USER								101
#define IDC_LOGIN_NEW									102
#define IDC_LOGIN_DELETE							103
#define IDC_LOGIN_EDIT								104
#define IDC_LOGIN_NOTEBOOK						105

#define IDC_NEW_USER_NAME							101
#define IDC_NEW_USER_FACE							102
#define IDC_NEW_USER_SPEAKER					103
#define IDC_NEW_USER_PITCH						104
#define IDC_NEW_USER_TITLE						105
#define IDC_NEW_USER_HEAD							106
#define IDC_NEW_USER_GLASSES					107
#define IDC_NEW_USER_NOTEBOOK					108
#define IDC_NEW_USER_HEAD_AREA				109
#define IDC_NEW_USER_ID								110
#define IDC_NEW_USER_SQUAD						111
#define IDC_NEW_USER_SQUAD_TEXT				112

// Interrupt display
#define IDC_INT_OPTIONS								101
#define IDC_INT_LOAD									102
#define IDC_INT_SAVE									103
#define IDC_INT_ABORT									104
#define IDC_INT_RETRY									105
#define IDC_INT_TITLE									106

// Mission end display
#define IDC_ME_SUBTITLE								101
#define IDC_ME_QUOTATION							102
#define IDC_ME_AUTHOR									103
#define IDC_ME_RETRY									104
#define IDC_ME_LOAD										105

// Get ready display
#define IDC_GETREADY_NAME							101
#define IDC_GETREADY_DESC							102
#define IDC_GETREADY_PRIMARY					105
#define IDC_GETREADY_SECONDARY				106
#define IDC_GETREADY_PLAYER						107
#define IDC_GETREADY_DATE							108
#define IDC_GETREADY_MODE							110
#define IDC_GETREADY_PRIMARY_TEXT			111
#define IDC_GETREADY_SECONDARY_TEXT		112
#define IDC_GETREADY_EDITMODE					113
#define	IDC_GETREADY_TITLE						114

// Debriefing
#define IDC_DEBRIEFING_LEFT						101
#define IDC_DEBRIEFING_RIGHT					102
#define IDC_DEBRIEFING_STAT						103
#define IDC_DEBRIEFING_RESTART				104
#define IDC_DEBRIEFING_PAD2						105
#define IDC_DEBRIEFING_PLAYERS_TITLE_BG	106
#define IDC_DEBRIEFING_PLAYERS_TITLE	107
#define IDC_DEBRIEFING_PLAYERS_BG			108
#define IDC_DEBRIEFING_PLAYERS				109

// Campaign display
#define IDC_CAMPAIGN_HISTORY					101
#define IDC_CAMPAIGN_MIS_NAME					102
#define IDC_CAMPAIGN_MIS_DESC					103
#define IDC_CAMPAIGN_BOOK							104
#define IDC_CAMPAIGN_CAMPAIGN					105
#define IDC_CAMPAIGN_PREV							106
#define IDC_CAMPAIGN_NEXT							107
#define IDC_CAMPAIGN_DESCRIPTION			108
#define IDC_CAMPAIGN_REPLAY						109
#define IDC_CAMPAIGN_DIFF							110
#define IDC_CAMPAIGN_MISSION					111

#define IDC_CAMPAIGN_DATE							112
#define IDC_CAMPAIGN_SCORE						113
#define IDC_CAMPAIGN_DURATION					114
#define IDC_CAMPAIGN_CASUALTIES				115
#define IDC_CAMPAIGN_KILLS_TITLE			116
#define IDC_CAMPAIGN_ENEMY_ROW				117
#define IDC_CAMPAIGN_FRIENDLY_ROW			118
#define IDC_CAMPAIGN_CIVILIAN_ROW			119
#define IDC_CAMPAIGN_INFANTRY_COLUMN	120
#define IDC_CAMPAIGN_SOFT_COLUMN			121
#define IDC_CAMPAIGN_ARMORED_COLUMN		122
#define IDC_CAMPAIGN_AIRCRAFT_COLUMN	123
#define IDC_CAMPAIGN_TOTAL_COLUMN			124
#define IDC_CAMPAIGN_EINF							125
#define IDC_CAMPAIGN_ESOFT						126
#define IDC_CAMPAIGN_EARM							127
#define IDC_CAMPAIGN_EAIR							128
#define IDC_CAMPAIGN_ETOT							129
#define IDC_CAMPAIGN_FINF							130
#define IDC_CAMPAIGN_FSOFT						131
#define IDC_CAMPAIGN_FARM							132
#define IDC_CAMPAIGN_FAIR							133
#define IDC_CAMPAIGN_FTOT							134
#define IDC_CAMPAIGN_CINF							135
#define IDC_CAMPAIGN_CSOFT						136
#define IDC_CAMPAIGN_CARM							137
#define IDC_CAMPAIGN_CAIR							138
#define IDC_CAMPAIGN_CTOT							139

// Revert display
#define IDC_REVERT_BOOK								101
#define IDC_REVERT_TITLE							102
#define IDC_REVERT_QUESTION						103

// Debug display
#define IDC_DEBUG_EXP									101
#define IDC_DEBUG_APPLY								102
#define IDC_DEBUG_LOG									103

#define IDC_DEBUG_EXP1								121
#define IDC_DEBUG_EXP2								122
#define IDC_DEBUG_EXP3								123
#define IDC_DEBUG_EXP4								124

#define IDC_DEBUG_RES1								141
#define IDC_DEBUG_RES2								142
#define IDC_DEBUG_RES3								143
#define IDC_DEBUG_RES4								144

// HintC display
#define IDC_HINTC_BG									101
#define IDC_HINTC_HINT								102

// Insert marker display
#define IDC_INSERT_MARKER							101
#define IDC_INSERT_MARKER_PICTURE			102

// InGameUI
// - unit info
#define IDC_IGUI_TIME									101
#define IDC_IGUI_DATE									102
#define IDC_IGUI_NAME									103
#define IDC_IGUI_UNIT									104
#define IDC_IGUI_VALUE_EXP						106
#define IDC_IGUI_COMBAT_MODE					107
#define IDC_IGUI_VALUE_HEALTH					109
#define IDC_IGUI_VALUE_ARMOR					111
#define IDC_IGUI_VALUE_FUEL						113
#define IDC_IGUI_CARGO_MAN						114
#define IDC_IGUI_CARGO_FUEL						115
#define IDC_IGUI_CARGO_REPAIR					116
#define IDC_IGUI_CARGO_AMMO						117
#define IDC_IGUI_WEAPON								118
#define IDC_IGUI_AMMO									119
#define IDC_IGUI_VEHICLE							120
#define IDC_IGUI_SPEED								121
#define IDC_IGUI_ALT									122
#define IDC_IGUI_FORMATION						123
#define IDC_IGUI_BG										124

// - hint
#define IDC_IGHINT_BG									101
#define IDC_IGHINT_HINT								102

// - load mission progress bar
#define IDC_LOAD_MISSION_NAME					101
#define IDC_LOAD_MISSION_DATE					102


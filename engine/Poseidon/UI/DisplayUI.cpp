
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>

#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Game/Scripting/Scripts.hpp>

#include <SDL3/SDL_scancode.h>
#include <Poseidon/Foundation/Common/Win.h>
#include <SDL3/SDL_keycode.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include <sys/stat.h>

#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/UI/DisplayUI.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <Poseidon/Core/SaveVersion.hpp>

#include <Poseidon/Foundation/Strings/Bstring.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Foundation/Common/Filenames.hpp>

#include <Poseidon/IO/Filesystem/DirTree.hpp>

#include <Poseidon/Core/Global.hpp>

extern bool AutoTest;

namespace Poseidon
{

void ShowCinemaBorder(bool show);

int GetNetworkPort();
RString GetNetworkPassword();

RString GetIdentityText(const PlayerIdentity& identity);

void CopyDirectoryStructure(const char* dst, const char* src)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s\\*.*", src);

    _finddata_t info;
    intptr_t h = _findfirst(buffer, &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0)
            {
                // FIX - do not delete only current and parent directory
                if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
                {
                    char srcNew[256];
                    char dstNew[256];
                    snprintf(srcNew, sizeof(srcNew), "%s\\%s", src, info.name);
                    snprintf(dstNew, sizeof(dstNew), "%s\\%s", dst, info.name);
                    CreateDirectory(dstNew, nullptr);
                    CopyDirectoryStructure(dstNew, srcNew);
                }
            }
            else
            {
                char srcNew[256];
                char dstNew[256];
                snprintf(srcNew, sizeof(srcNew), "%s\\%s", src, info.name);
                snprintf(dstNew, sizeof(dstNew), "%s\\%s", dst, info.name);
                ::CopyFile(srcNew, dstNew, FALSE);
            }
        } while (_findnext(h, &info) == 0);
        _findclose(h);
    }
}
void RunInitScript()
{
    RString initScript = GetMissionDirectory() + RString("init.sqs");
    if (QIFStreamB::FileExist(initScript))
    {
        Script* script = new Script("init.sqs", GameValue(), INT_MAX);
        GWorld->AddScript(script);
        GWorld->SimulateScripts();
    }

    // Also run init.sqf if present (SQF format, unscheduled execution)
    RString initSqf = GetMissionDirectory() + RString("init.sqf");
    if (QIFStreamB::FileExist(initSqf))
    {
        QIFStreamB in;
        in.AutoOpen(initSqf);
        if (!in.fail() && in.rest() > 0)
        {
            RString code(in.act(), in.rest());
            GameState* gs = GWorld->GetGameState();
            if (gs)
                gs->Execute((const char*)code);
        }
    }
}

void RunMissionScript(const char* filename, GameValue argument)
{
    RString scriptPath = GetMissionDirectory() + RString(filename);
    if (QIFStreamB::FileExist(scriptPath))
    {
        Script* script = new Script(filename, argument, INT_MAX);
        GWorld->AddScript(script);
        GWorld->SimulateScripts();
    }
}

// Configure controls display
static int ReservedKeys[] = {
    // pause
    SDL_SCANCODE_ESCAPE,
    // menu
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7,
    SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_BACKSPACE,
    // units
    SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
    SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12};

bool IsReservedKey(int dikCode)
{
    int n = sizeof(ReservedKeys) / sizeof(int);

    for (int i = 0; i < n; i++)
    {
        if (dikCode == ReservedKeys[i])
        {
            return true;
        }
    }

    return false;
};

RString GetKeyName(int dikCode)
{
    if (InputBindingIsDoubleTap(dikCode))
        return RString("2x ") + GetKeyName(InputBindingBaseCode(dikCode));

    switch (dikCode)
    {
        case SDL_SCANCODE_ESCAPE:
            return LocalizeString(IDS_SDL_SCANCODE_ESCAPE);
        case SDL_SCANCODE_1:
            return LocalizeString(IDS_SDL_SCANCODE_1);
        case SDL_SCANCODE_2:
            return LocalizeString(IDS_SDL_SCANCODE_2);
        case SDL_SCANCODE_3:
            return LocalizeString(IDS_SDL_SCANCODE_3);
        case SDL_SCANCODE_4:
            return LocalizeString(IDS_SDL_SCANCODE_4);
        case SDL_SCANCODE_5:
            return LocalizeString(IDS_SDL_SCANCODE_5);
        case SDL_SCANCODE_6:
            return LocalizeString(IDS_SDL_SCANCODE_6);
        case SDL_SCANCODE_7:
            return LocalizeString(IDS_SDL_SCANCODE_7);
        case SDL_SCANCODE_8:
            return LocalizeString(IDS_SDL_SCANCODE_8);
        case SDL_SCANCODE_9:
            return LocalizeString(IDS_SDL_SCANCODE_9);
        case SDL_SCANCODE_0:
            return LocalizeString(IDS_SDL_SCANCODE_0);
        case SDL_SCANCODE_MINUS:
            return LocalizeString(IDS_SDL_SCANCODE_MINUS);
        case SDL_SCANCODE_EQUALS:
            return LocalizeString(IDS_SDL_SCANCODE_EQUALS);
        case SDL_SCANCODE_BACKSPACE:
            return LocalizeString(IDS_SDL_SCANCODE_BACKSPACE);
        case SDL_SCANCODE_TAB:
            return LocalizeString(IDS_SDL_SCANCODE_TAB);
        case SDL_SCANCODE_Q:
            return LocalizeString(IDS_SDL_SCANCODE_Q);
        case SDL_SCANCODE_W:
            return LocalizeString(IDS_SDL_SCANCODE_W);
        case SDL_SCANCODE_E:
            return LocalizeString(IDS_SDL_SCANCODE_E);
        case SDL_SCANCODE_R:
            return LocalizeString(IDS_SDL_SCANCODE_R);
        case SDL_SCANCODE_T:
            return LocalizeString(IDS_SDL_SCANCODE_T);
        case SDL_SCANCODE_Y:
            return LocalizeString(IDS_SDL_SCANCODE_Y);
        case SDL_SCANCODE_U:
            return LocalizeString(IDS_SDL_SCANCODE_U);
        case SDL_SCANCODE_I:
            return LocalizeString(IDS_SDL_SCANCODE_I);
        case SDL_SCANCODE_O:
            return LocalizeString(IDS_SDL_SCANCODE_O);
        case SDL_SCANCODE_P:
            return LocalizeString(IDS_SDL_SCANCODE_P);
        case SDL_SCANCODE_LEFTBRACKET:
            return LocalizeString(IDS_SDL_SCANCODE_LEFTBRACKET);
        case SDL_SCANCODE_RIGHTBRACKET:
            return LocalizeString(IDS_SDL_SCANCODE_RIGHTBRACKET);
        case SDL_SCANCODE_RETURN:
            return LocalizeString(IDS_SDL_SCANCODE_RETURN);
        case SDL_SCANCODE_LCTRL:
            return LocalizeString(IDS_SDL_SCANCODE_LCTRL);
        case SDL_SCANCODE_A:
            return LocalizeString(IDS_SDL_SCANCODE_A);
        case SDL_SCANCODE_S:
            return LocalizeString(IDS_SDL_SCANCODE_S);
        case SDL_SCANCODE_D:
            return LocalizeString(IDS_SDL_SCANCODE_D);
        case SDL_SCANCODE_F:
            return LocalizeString(IDS_SDL_SCANCODE_F);
        case SDL_SCANCODE_G:
            return LocalizeString(IDS_SDL_SCANCODE_G);
        case SDL_SCANCODE_H:
            return LocalizeString(IDS_SDL_SCANCODE_H);
        case SDL_SCANCODE_J:
            return LocalizeString(IDS_SDL_SCANCODE_J);
        case SDL_SCANCODE_K:
            return LocalizeString(IDS_SDL_SCANCODE_K);
        case SDL_SCANCODE_L:
            return LocalizeString(IDS_SDL_SCANCODE_L);
        case SDL_SCANCODE_SEMICOLON:
            return LocalizeString(IDS_SDL_SCANCODE_SEMICOLON);
        case SDL_SCANCODE_APOSTROPHE:
            return LocalizeString(IDS_SDL_SCANCODE_APOSTROPHE);
        case SDL_SCANCODE_GRAVE:
            return LocalizeString(IDS_SDL_SCANCODE_GRAVE);
        case SDL_SCANCODE_LSHIFT:
            return LocalizeString(IDS_SDL_SCANCODE_LSHIFT);
        case SDL_SCANCODE_BACKSLASH:
            return LocalizeString(IDS_SDL_SCANCODE_BACKSLASH);
        case SDL_SCANCODE_Z:
            return LocalizeString(IDS_SDL_SCANCODE_Z);
        case SDL_SCANCODE_X:
            return LocalizeString(IDS_SDL_SCANCODE_X);
        case SDL_SCANCODE_C:
            return LocalizeString(IDS_SDL_SCANCODE_C);
        case SDL_SCANCODE_V:
            return LocalizeString(IDS_SDL_SCANCODE_V);
        case SDL_SCANCODE_B:
            return LocalizeString(IDS_SDL_SCANCODE_B);
        case SDL_SCANCODE_N:
            return LocalizeString(IDS_SDL_SCANCODE_N);
        case SDL_SCANCODE_M:
            return LocalizeString(IDS_SDL_SCANCODE_M);
        case SDL_SCANCODE_COMMA:
            return LocalizeString(IDS_SDL_SCANCODE_COMMA);
        case SDL_SCANCODE_PERIOD:
            return LocalizeString(IDS_SDL_SCANCODE_PERIOD);
        case SDL_SCANCODE_SLASH:
            return LocalizeString(IDS_SDL_SCANCODE_SLASH);
        case SDL_SCANCODE_RSHIFT:
            return LocalizeString(IDS_SDL_SCANCODE_RSHIFT);
        case SDL_SCANCODE_KP_MULTIPLY:
            return LocalizeString(IDS_SDL_SCANCODE_KP_MULTIPLY);
        case SDL_SCANCODE_LALT:
            return LocalizeString(IDS_SDL_SCANCODE_LALT);
        case SDL_SCANCODE_SPACE:
            return LocalizeString(IDS_SDL_SCANCODE_SPACE);
        case SDL_SCANCODE_CAPSLOCK:
            return LocalizeString(IDS_SDL_SCANCODE_CAPSLOCK);
        case SDL_SCANCODE_F1:
            return LocalizeString(IDS_SDL_SCANCODE_F1);
        case SDL_SCANCODE_F2:
            return LocalizeString(IDS_SDL_SCANCODE_F2);
        case SDL_SCANCODE_F3:
            return LocalizeString(IDS_SDL_SCANCODE_F3);
        case SDL_SCANCODE_F4:
            return LocalizeString(IDS_SDL_SCANCODE_F4);
        case SDL_SCANCODE_F5:
            return LocalizeString(IDS_SDL_SCANCODE_F5);
        case SDL_SCANCODE_F6:
            return LocalizeString(IDS_SDL_SCANCODE_F6);
        case SDL_SCANCODE_F7:
            return LocalizeString(IDS_SDL_SCANCODE_F7);
        case SDL_SCANCODE_F8:
            return LocalizeString(IDS_SDL_SCANCODE_F8);
        case SDL_SCANCODE_F9:
            return LocalizeString(IDS_SDL_SCANCODE_F9);
        case SDL_SCANCODE_F10:
            return LocalizeString(IDS_SDL_SCANCODE_F10);
        case SDL_SCANCODE_NUMLOCKCLEAR:
            return LocalizeString(IDS_SDL_SCANCODE_NUMLOCKCLEAR);
        case SDL_SCANCODE_SCROLLLOCK:
            return LocalizeString(IDS_SDL_SCANCODE_SCROLLLOCK);
        case SDL_SCANCODE_KP_7:
            return LocalizeString(IDS_SDL_SCANCODE_KP_7);
        case SDL_SCANCODE_KP_8:
            return LocalizeString(IDS_SDL_SCANCODE_KP_8);
        case SDL_SCANCODE_KP_9:
            return LocalizeString(IDS_SDL_SCANCODE_KP_9);
        case SDL_SCANCODE_KP_MINUS:
            return LocalizeString(IDS_SDL_SCANCODE_KP_MINUS);
        case SDL_SCANCODE_KP_4:
            return LocalizeString(IDS_SDL_SCANCODE_KP_4);
        case SDL_SCANCODE_KP_5:
            return LocalizeString(IDS_SDL_SCANCODE_KP_5);
        case SDL_SCANCODE_KP_6:
            return LocalizeString(IDS_SDL_SCANCODE_KP_6);
        case SDL_SCANCODE_KP_PLUS:
            return LocalizeString(IDS_SDL_SCANCODE_KP_PLUS);
        case SDL_SCANCODE_KP_1:
            return LocalizeString(IDS_SDL_SCANCODE_KP_1);
        case SDL_SCANCODE_KP_2:
            return LocalizeString(IDS_SDL_SCANCODE_KP_2);
        case SDL_SCANCODE_KP_3:
            return LocalizeString(IDS_SDL_SCANCODE_KP_3);
        case SDL_SCANCODE_KP_0:
            return LocalizeString(IDS_SDL_SCANCODE_KP_0);
        case SDL_SCANCODE_KP_PERIOD:
            return LocalizeString(IDS_SDL_SCANCODE_KP_PERIOD);
        case SDL_SCANCODE_NONUSBACKSLASH:
            return LocalizeString(IDS_SDL_SCANCODE_NONUSBACKSLASH);
        case SDL_SCANCODE_F11:
            return LocalizeString(IDS_SDL_SCANCODE_F11);
        case SDL_SCANCODE_F12:
            return LocalizeString(IDS_SDL_SCANCODE_F12);
        case SDL_SCANCODE_F13:
            return LocalizeString(IDS_SDL_SCANCODE_F13);
        case SDL_SCANCODE_F14:
            return LocalizeString(IDS_SDL_SCANCODE_F14);
        case SDL_SCANCODE_F15:
            return LocalizeString(IDS_SDL_SCANCODE_F15);
        case SDL_SCANCODE_INTERNATIONAL2:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL2);
        case SDL_SCANCODE_INTERNATIONAL6:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL6);
        case SDL_SCANCODE_INTERNATIONAL4:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL4);
        case SDL_SCANCODE_INTERNATIONAL5:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL5);
        case SDL_SCANCODE_INTERNATIONAL3:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL3);
        case SDL_SCANCODE_INTERNATIONAL7:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL7);
        case SDL_SCANCODE_KP_EQUALS:
            return LocalizeString(IDS_SDL_SCANCODE_KP_EQUALS);
        case SDL_SCANCODE_MEDIA_PREVIOUS_TRACK:
            return LocalizeString(IDS_SDL_SCANCODE_MEDIA_PREVIOUS_TRACK);
        case SDL_SCANCODE_INTERNATIONAL8:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL8);
        case SDL_SCANCODE_INTERNATIONAL9:
            return LocalizeString(IDS_SDL_SCANCODE_INTERNATIONAL9);
        case SDL_SCANCODE_LANG3:
            return LocalizeString(IDS_SDL_SCANCODE_LANG3);
        case SDL_SCANCODE_LANG2:
            return LocalizeString(IDS_SDL_SCANCODE_LANG2);
        case SDL_SCANCODE_STOP:
            return LocalizeString(IDS_SDL_SCANCODE_STOP);
        case SDL_SCANCODE_LANG6:
            return LocalizeString(IDS_SDL_SCANCODE_LANG6);
        case SDL_SCANCODE_LANG7:
            return LocalizeString(IDS_SDL_SCANCODE_LANG7);
        case SDL_SCANCODE_MEDIA_NEXT_TRACK:
            return LocalizeString(IDS_SDL_SCANCODE_MEDIA_NEXT_TRACK);
        case SDL_SCANCODE_KP_ENTER:
            return LocalizeString(IDS_SDL_SCANCODE_KP_ENTER);
        case SDL_SCANCODE_RCTRL:
            return LocalizeString(IDS_SDL_SCANCODE_RCTRL);
        case SDL_SCANCODE_MUTE:
            return LocalizeString(IDS_SDL_SCANCODE_MUTE);
        case SDL_SCANCODE_MEDIA_PLAY_PAUSE:
            return LocalizeString(IDS_SDL_SCANCODE_MEDIA_PLAY_PAUSE);
        case SDL_SCANCODE_MEDIA_STOP:
            return LocalizeString(IDS_SDL_SCANCODE_MEDIA_STOP);
        case SDL_SCANCODE_VOLUMEDOWN:
            return LocalizeString(IDS_SDL_SCANCODE_VOLUMEDOWN);
        case SDL_SCANCODE_VOLUMEUP:
            return LocalizeString(IDS_SDL_SCANCODE_VOLUMEUP);
        case SDL_SCANCODE_AC_HOME:
            return LocalizeString(IDS_SDL_SCANCODE_AC_HOME);
        case SDL_SCANCODE_KP_COMMA:
            return LocalizeString(IDS_SDL_SCANCODE_KP_COMMA);
        case SDL_SCANCODE_KP_DIVIDE:
            return LocalizeString(IDS_SDL_SCANCODE_KP_DIVIDE);
        case SDL_SCANCODE_PRINTSCREEN:
            return LocalizeString(IDS_SDL_SCANCODE_PRINTSCREEN);
        case SDL_SCANCODE_RALT:
            return LocalizeString(IDS_SDL_SCANCODE_RALT);
        case SDL_SCANCODE_PAUSE:
            return LocalizeString(IDS_SDL_SCANCODE_PAUSE);
        case SDL_SCANCODE_HOME:
            return LocalizeString(IDS_SDL_SCANCODE_HOME);
        case SDL_SCANCODE_UP:
            return LocalizeString(IDS_SDL_SCANCODE_UP);
        case SDL_SCANCODE_PAGEUP:
            return LocalizeString(IDS_SDL_SCANCODE_PAGEUP);
        case SDL_SCANCODE_LEFT:
            return LocalizeString(IDS_SDL_SCANCODE_LEFT);
        case SDL_SCANCODE_RIGHT:
            return LocalizeString(IDS_SDL_SCANCODE_RIGHT);
        case SDL_SCANCODE_END:
            return LocalizeString(IDS_SDL_SCANCODE_END);
        case SDL_SCANCODE_DOWN:
            return LocalizeString(IDS_SDL_SCANCODE_DOWN);
        case SDL_SCANCODE_PAGEDOWN:
            return LocalizeString(IDS_SDL_SCANCODE_PAGEDOWN);
        case SDL_SCANCODE_INSERT:
            return LocalizeString(IDS_SDL_SCANCODE_INSERT);
        case SDL_SCANCODE_DELETE:
            return LocalizeString(IDS_SDL_SCANCODE_DELETE);
        case SDL_SCANCODE_LGUI:
            return LocalizeString(IDS_SDL_SCANCODE_LGUI);
        case SDL_SCANCODE_RGUI:
            return LocalizeString(IDS_SDL_SCANCODE_RGUI);
        case SDL_SCANCODE_APPLICATION:
            return LocalizeString(IDS_SDL_SCANCODE_APPLICATION);
        case SDL_SCANCODE_POWER:
            return LocalizeString(IDS_SDL_SCANCODE_POWER);
        case SDL_SCANCODE_SLEEP:
            return LocalizeString(IDS_SDL_SCANCODE_SLEEP);
        case SDL_SCANCODE_WAKE:
            return LocalizeString(IDS_SDL_SCANCODE_WAKE);
        case SDL_SCANCODE_AC_SEARCH:
            return LocalizeString(IDS_SDL_SCANCODE_AC_SEARCH);
        case SDL_SCANCODE_AC_BOOKMARKS:
            return LocalizeString(IDS_SDL_SCANCODE_AC_BOOKMARKS);
        case SDL_SCANCODE_AC_REFRESH:
            return LocalizeString(IDS_SDL_SCANCODE_AC_REFRESH);
        case SDL_SCANCODE_AC_STOP:
            return LocalizeString(IDS_SDL_SCANCODE_AC_STOP);
        case SDL_SCANCODE_AC_FORWARD:
            return LocalizeString(IDS_SDL_SCANCODE_AC_FORWARD);
        case SDL_SCANCODE_AC_BACK:
            return LocalizeString(IDS_SDL_SCANCODE_AC_BACK);
        case SDL_SCANCODE_MEDIA_SELECT:
            return LocalizeString(IDS_SDL_SCANCODE_MEDIA_SELECT);
        case INPUT_DEVICE_MOUSE:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_0);
        case INPUT_DEVICE_MOUSE + 1:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_1);
        case INPUT_DEVICE_MOUSE + 2:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_2);
        case INPUT_DEVICE_MOUSE + 3:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_3);
        case INPUT_DEVICE_MOUSE + 4:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_4);
        case INPUT_DEVICE_MOUSE + 5:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_5);
        case INPUT_DEVICE_MOUSE + 6:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_6);
        case INPUT_DEVICE_MOUSE + 7:
            return LocalizeString(IDS_INPUT_DEVICE_MOUSE_7);

        case INPUT_DEVICE_STICK:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_0);
        case INPUT_DEVICE_STICK + 1:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_1);
        case INPUT_DEVICE_STICK + 2:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_2);
        case INPUT_DEVICE_STICK + 3:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_3);
        case INPUT_DEVICE_STICK + 4:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_4);
        case INPUT_DEVICE_STICK + 5:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_5);
        case INPUT_DEVICE_STICK + 6:
            return "LT";
        case INPUT_DEVICE_STICK + 7:
            return "RT";
        case INPUT_DEVICE_STICK + 8:
            return "Stick Btn. #9";
        case INPUT_DEVICE_STICK + 9:
            return "Stick Btn. #10";
        case INPUT_DEVICE_STICK + 10:
            return "LS";
        case INPUT_DEVICE_STICK + 11:
            return "RS";

        case INPUT_DEVICE_STICK_AXIS + 0:
            return "LS X";
        case INPUT_DEVICE_STICK_AXIS + 1:
            return "LS Y";
        case INPUT_DEVICE_STICK_AXIS + 2:
            return "RT";
        case INPUT_DEVICE_STICK_AXIS + 3:
            return "RS X";
        case INPUT_DEVICE_STICK_AXIS + 4:
            return "RS Y";
        case INPUT_DEVICE_STICK_AXIS + 5:
            return "LT";
        case INPUT_DEVICE_STICK_AXIS + 6:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_SLIDER_1);
        case INPUT_DEVICE_STICK_AXIS + 7:
            return LocalizeString(IDS_INPUT_DEVICE_STICK_SLIDER_2);

        case INPUT_DEVICE_STICK_POV + 0:
            return LocalizeString(IDS_INPUT_DEVICE_POV_N);
        case INPUT_DEVICE_STICK_POV + 1:
            return LocalizeString(IDS_INPUT_DEVICE_POV_NE);
        case INPUT_DEVICE_STICK_POV + 2:
            return LocalizeString(IDS_INPUT_DEVICE_POV_E);
        case INPUT_DEVICE_STICK_POV + 3:
            return LocalizeString(IDS_INPUT_DEVICE_POV_SE);
        case INPUT_DEVICE_STICK_POV + 4:
            return LocalizeString(IDS_INPUT_DEVICE_POV_S);
        case INPUT_DEVICE_STICK_POV + 5:
            return LocalizeString(IDS_INPUT_DEVICE_POV_SW);
        case INPUT_DEVICE_STICK_POV + 6:
            return LocalizeString(IDS_INPUT_DEVICE_POV_W);
        case INPUT_DEVICE_STICK_POV + 7:
            return LocalizeString(IDS_INPUT_DEVICE_POV_NW);
        default:
            return "";
    }
}

CKeys::CKeys(ControlsContainer* parent, int idc, const ParamEntry& cls) : C3DListBox(parent, idc, cls)
{
    _sb3DWidth = 0.05;
    _mode = -1;
    _ignoredKey = -1;
}

void CKeys::SetKeys(int action, const AutoArray<int>& keys)
{
    while (_keys[action].Size() > 0)
    {
        RemoveKey(action);
    }
    for (int i = 0; i < keys.Size(); i++)
    {
        AddKey(action, keys[i]);
    }
}

void CKeys::CheckCollisions(int key)
{
    int count = 0;
    int iCol = -1, jCol = -1;
    for (int i = 0; i < UAN; i++)
    {
        for (int j = 0; j < _keys[i].Size(); j++)
        {
            if (_keys[i][j] == key)
            {
                count++;
                if (count > 1)
                {
                    return; // collision still persist
                }
                iCol = i;
                jCol = j;
            }
        }
    }
    if (count > 0)
    {
        _collisions[iCol][jCol] = false;
    }
}

void CKeys::RemoveKey(int action)
{
    int index = _keys[action].Size() - 1;
    if (index < 0)
    {
        return;
    }

    int key = _keys[action][index];
    bool collision = _collisions[action][index];

    // delete
    _keys[action].Resize(index);
    _collisions[action].Resize(index);

    // check collisions
    if (collision)
    {
        CheckCollisions(key);
    }
}

void CKeys::AddKey(int action, int key)
{
    for (int j = 0; j < _keys[action].Size(); j++)
    {
        if (_keys[action][j] == key)
        {
            // remove key instead
            bool collision = _collisions[action][j];

            // delete
            _keys[action].Delete(j);
            _collisions[action].Delete(j);

            // check collisions
            if (collision)
            {
                CheckCollisions(key);
            }

            return;
        }
    }

    // check collisions
    bool collision = false;
    for (int i = 0; i < UAN; i++)
    {
        for (int j = 0; j < _keys[i].Size(); j++)
        {
            if (_keys[i][j] == key)
            {
                collision = true;
                _collisions[i][j] = true;
            }
        }
    }

    _keys[action].Add(key);
    _collisions[action].Add(collision);
}

inline PackedColor ModAlpha(PackedColor color, float alpha)
{
    int a = toInt(alpha * color.A8());
    saturate(a, 0, 255);
    return PackedColorRGB(color, a);
}

void CKeys::DrawItem(Vector3Par position, Vector3Par down, int i, float alpha)
{
    float y1c = 0;
    float y2c = 1;
    if (i < _topString)
    {
        y1c = _topString - i;
    }
    if (i > _topString + _rows - 1)
    {
        y2c = _topString + _rows - i;
    }

    Vector3 normal = _down.CrossProduct(_right).Normalized();

    Vector3 rightSB = _right;
    if (GetSize() > _rows)
    {
        rightSB = (1.0 - _sb3DWidth) * _right;
    }
    float rightSBSize = rightSB.Size();
    Vector3 border = 0.02 * _right;

    bool selected = i == GetCurSel() && IsEnabled();
    PackedColor color = ModAlpha(GetFtColor(i), alpha);
    PackedColor selColor = color;
    if (selected && _showSelected)
    {
        PackedColor selBgColor = ModAlpha(_selBgColor, alpha);
        switch (_mode)
        {
            case -1:
                GEngine->Draw3D(position, down, 0.4 * rightSB, ClipAll, selBgColor, DisableSun, nullptr, 0, y1c, 1,
                                y2c);
                break;
            case 0:
                GEngine->Draw3D(position + 0.4 * rightSB, down, 0.6 * rightSB, ClipAll, selBgColor, DisableSun, nullptr,
                                0, y1c, 1, y2c);
                break;
        }
        selColor = ModAlpha(GetSelColor(i), alpha);
    }

    Vector3 curPos = position - 0.002 * normal;

    Texture* texture = GetTexture(i);
    if (texture)
    {
        Vector3 right = (float)texture->AWidth() / (float)texture->AHeight() * down.Size() * _right.Normalized();
        float rightSize = right.Size();
        float x2c = 1;
        if (rightSize > rightSBSize)
        {
            x2c = rightSBSize / rightSize;
        }
        GEngine->Draw3D(curPos, down, right, ClipAll, color, // PackedColor(Color(1, 1, 1, alpha)),
                        DisableSun, texture, 0, y1c, x2c, y2c);
        curPos += right;
        rightSBSize -= rightSize;
        if (rightSBSize <= 0)
        {
            return;
        }
    }

    float top = 0.5 * (1.0 - _size);
    Vector3 pos = curPos + top * down + border;
    Vector3 up = -_size * down;
    Vector3 right = 0.75 * up.Size() * _right.Normalized();
    float invRightSize = 1.0 / right.Size();
    float x2ct = rightSBSize * invRightSize;
    float y1ct = 0;
    float y2ct = 1;
    if (y1c > top)
    {
        y1ct = (y1c - top) / _size;
    }
    if (y2c < top + _size)
    {
        y2ct = (y2c - top) / _size;
    }

    RString text = GetText(i);
    float x2c = 0.4 * x2ct - 2.0 * invRightSize * border.Size();
    GEngine->DrawText3D(pos, up, right, ClipAll, _font, _mode == -1 ? selColor : color, DisableSun, text, 0, y1ct, x2c,
                        y2ct);
    pos += 0.4 * rightSB;

    x2c = 0.6 * x2ct - 2.0 * invRightSize * border.Size();
    PackedColor noCollision = _mode == 0 ? selColor : color;
    PackedColor collision = PackedColor(Color(1, 0, 0, alpha));
    for (int j = 0; j < _keys[i].Size(); j++)
    {
        // prefix
        if (j > 0)
        {
            const char* prefix = ", ";
            GEngine->DrawText3D(pos, up, right, ClipAll, _font, noCollision, DisableSun, prefix, 0, y1ct, x2c, y2ct);
            Vector3 width = GEngine->GetText3DWidth(right, _font, prefix);
            x2c -= width.Size() * invRightSize;
            if (x2c <= 0)
            {
                break;
            }
            pos += width;
        }

        // value
        text = RString("\"") + GetKeyName(_keys[i][j]) + RString("\"");
        PackedColor col = _collisions[i][j] ? collision : noCollision;
        GEngine->DrawText3D(pos, up, right, ClipAll, _font, col, DisableSun, text, 0, y1ct, x2c, y2ct);
        Vector3 width = GEngine->GetText3DWidth(right, _font, text);
        x2c -= width.Size() * invRightSize;
        if (x2c <= 0)
        {
            break;
        }
        pos += width;
    }
}

bool CKeys::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (_mode == -1)
    {
        if (nChar == SDLK_RIGHT)
        {
            _mode = 0;
            _ignoredKey = SDL_SCANCODE_RIGHT;
            return true;
        }
        else
        {
            return C3DListBox::OnKeyDown(nChar, nRepCnt, nFlags);
        }
    }
    else
    {
        if (nChar == SDLK_ESCAPE)
        {
            _mode = -1;
        }
        return true;
    }
}

void CKeys::CheckIgnoredKey()
{
    if (_ignoredKey != -1)
    {
        InputSubsystem::Instance().ConsumeKeyPress(static_cast<SDL_Scancode>(_ignoredKey));
        _ignoredKey = -1;
    }
}

void CKeys::OnLButtonDown(float x, float y)
{
    IsInside(x, y);
    if (_scrollbar.IsEnabled())
    {
        if (_u > 1.0 - _sb3DWidth)
        {
            _scrollbar.SetPos(_topString);
            _scrollbar.OnLButtonDown(_v);
            _topString = _scrollbar.GetPos();
            return;
        }
        else
        {
            _u *= 1.0 / (1.0 - _sb3DWidth);
        }
    }

    float index = _v * _rows;
    if (index >= 0 && index < _rows)
    {
        _parent->OnLBDrag(IDC(), toIntFloor(_topString + index));
        _dragging = true;
    }
    if (_u < 0.4)
    {
        _mode = -1;
    }
    else
    {
        _mode = 0;
    }
}

DisplayConfigure::DisplayConfigure(ControlsContainer* parent, bool enableSimulation) : Display(parent)
{
    _enableSimulation = enableSimulation;
    _keys = nullptr;
    Load("RscDisplayConfigure");
    PoseidonAssert(_keys);

    auto& input = InputSubsystem::Instance();
    _oldRevMouse = input.IsReverseMouse();
    _oldJoystickEnabled = input.IsJoystickEnabled();
    _oldMouseButtonsReversed = input.IsMouseButtonsReversed();
    _oldMouseSensitivityX = input.GetMouseSensitivityX();
    _oldMouseSensitivityY = input.GetMouseSensitivityY();

    C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_YREVERSED));
    if (input.IsReverseMouse())
    {
        ctrl->SetText(LocalizeString(IDS_CONFIG_YREVERSED));
    }
    else
    {
        ctrl->SetText(LocalizeString(IDS_CONFIG_YNORMAL));
    }
    ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_JOYSTICK));
    if (input.IsJoystickEnabled())
    {
        ctrl->SetText(LocalizeString(IDS_CONFIG_JOYSTICK_ENABLED));
    }
    else
    {
        ctrl->SetText(LocalizeString(IDS_CONFIG_JOYSTICK_DISABLED));
    }
    ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_BUTTONS));
    if (input.IsMouseButtonsReversed())
    {
        ctrl->SetText(LocalizeString(IDS_RIGHT_BUTTON));
    }
    else
    {
        ctrl->SetText(LocalizeString(IDS_LEFT_BUTTON));
    }

    // CKeys caches row text at AddString time; action labels won't update from the
    // generic ReloadLocalizedText path, so subscribe explicitly.
    _langCbToken = RegisterLanguageChangedCallback([this]() { RefreshLanguage(); });
}

DisplayConfigure::~DisplayConfigure()
{
    if (_langCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langCbToken);
        _langCbToken = -1;
    }
}

void DisplayConfigure::RefreshLanguage()
{
    if (!_keys)
        return;
    UserActionDesc* userActionDesc = InputSubsystem::GetUserActionDesc();
    int n = _keys->GetSize();
    for (int i = 0; i < n && i < UAN; i++)
    {
        _keys->Set(i).text = LocalizeString(userActionDesc[i].desc);
    }

    // Also refresh the three C3DActiveText labels that reflect input state.
    auto& input = InputSubsystem::Instance();
    if (auto* c = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_YREVERSED)))
        c->SetText(LocalizeString(input.IsReverseMouse() ? IDS_CONFIG_YREVERSED : IDS_CONFIG_YNORMAL));
    if (auto* c = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_JOYSTICK)))
        c->SetText(
            LocalizeString(input.IsJoystickEnabled() ? IDS_CONFIG_JOYSTICK_ENABLED : IDS_CONFIG_JOYSTICK_DISABLED));
    if (auto* c = dynamic_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_BUTTONS)))
        c->SetText(LocalizeString(input.IsMouseButtonsReversed() ? IDS_RIGHT_BUTTON : IDS_LEFT_BUTTON));
}

void DisplayConfigure::Destroy()
{
    Display::Destroy();

    auto& input = InputSubsystem::Instance();
    if (_exit != IDC_OK)
    {
        input.SetReverseMouse(_oldRevMouse);
        input.SetJoystickEnabled(_oldJoystickEnabled);
        input.SetMouseSensitivityX(_oldMouseSensitivityX);
        input.SetMouseSensitivityY(_oldMouseSensitivityY);
        input.SetMouseButtonsReversed(_oldMouseButtonsReversed);
        return;
    }

    for (int i = 0; i < UAN; i++)
    {
        input.SetUserKeys(static_cast<UserAction>(i), _keys->GetKeys(i));
        input.CompactUserKeys(static_cast<UserAction>(i));
    }
    input.SaveKeys();
}

void DisplayConfigure::OnSimulate(EntityAI* vehicle)
{
    Display::OnSimulate(vehicle);

    if (!_keys->IsFocused())
    {
        return;
    }

    if (_keys->GetMode() < 0)
    {
        return;
    }

    int index = _keys->GetCurSel();
    if (index < 0)
    {
        return;
    }

    _keys->CheckIgnoredKey();

    auto& input = InputSubsystem::Instance();
    int count = 0;
    int dik = -1;
    // const AutoArray<int> keyList = _keys->GetKeys(index);

    if (!InputSubsystem::GetUserActionDesc()[index].axis)
    {
        for (int i = 0; i < 256; i++)
        {
            if (InputSubsystem::Instance().IsKeyPressed(static_cast<SDL_Scancode>(i)))
            {
                dik = i;
                count++;
            }
        }
        for (int i = 0; i < N_MOUSE_BUTTONS; i++)
        {
            if (i == 0)
            {
                continue;
            }
            if (input.GetMouseButtonToDo(i))
            {
                dik = INPUT_DEVICE_MOUSE + i;
                count++;
            }
        }
        for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
        {
            if (input.GetStickButtonToDo(i))
            {
                dik = INPUT_DEVICE_STICK + i;
                count++;
            }
        }
        for (int i = 0; i < N_JOYSTICK_POV; i++)
        {
            if (input.GetStickPovToDo(i))
            {
                dik = INPUT_DEVICE_STICK_POV + i;
                count++;
            }
        }
    }
    else
    {
        for (int i = 0; i < N_JOYSTICK_AXES; i++)
        {
            if (input.ConsumeAxisBigActive(i))
            {
                dik = INPUT_DEVICE_STICK_AXIS + i;
                count++;
            }
        }
    }
    if (count != 1)
    {
        return;
    }

    if (dik == SDL_SCANCODE_BACKSPACE)
    {
        _keys->RemoveKey(index);
    }
    else if (!IsReservedKey(dik))
    {
        _keys->AddKey(index, dik);
    }
}

Control* DisplayConfigure::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    auto& input = InputSubsystem::Instance();
    switch (idc)
    {
        case IDC_CONFIG_KEYS:
            _keys = new CKeys(this, idc, cls);
            {
                UserActionDesc* userActionDesc = InputSubsystem::GetUserActionDesc();
                for (int i = 0; i < UAN; i++)
                {
                    _keys->AddString(LocalizeString(userActionDesc[i].desc));
                    _keys->SetKeys(i, input.GetUserKeys(static_cast<UserAction>(i)));
                }
            }
            _keys->SetCurSel(0);
            return _keys;
        case IDC_CONFIG_XAXIS:
        {
            C3DSlider* ctrl = new C3DSlider(this, idc, cls);
            ctrl->SetRange(0.5, 2.0);
            ctrl->SetSpeed(0.2, 0.02);
            ctrl->SetThumbPos(input.GetMouseSensitivityX());
            return ctrl;
        }
        case IDC_CONFIG_YAXIS:
        {
            C3DSlider* ctrl = new C3DSlider(this, idc, cls);
            ctrl->SetRange(0.5, 2.0);
            ctrl->SetSpeed(0.2, 0.02);
            ctrl->SetThumbPos(input.GetMouseSensitivityY());
            return ctrl;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayConfigure::OnButtonClicked(int idc)
{
    auto& input = InputSubsystem::Instance();
    switch (idc)
    {
        case IDC_CONFIG_DEFAULT:
        {
            UserActionDesc* userActionDesc = InputSubsystem::GetUserActionDesc();
            for (int i = 0; i < UAN; i++)
            {
                _keys->SetKeys(i, userActionDesc[i].keys);
            }
        }
            {
                input.SetReverseMouse(false);
                C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_YREVERSED));
                ctrl->SetText(LocalizeString(IDS_CONFIG_YNORMAL));
            }
            {
                input.SetJoystickEnabled(true);
                C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_JOYSTICK));
                ctrl->SetText(LocalizeString(IDS_CONFIG_JOYSTICK_ENABLED));
            }
            {
                input.SetMouseButtonsReversed(false);
                C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_BUTTONS));
                ctrl->SetText(LocalizeString(IDS_LEFT_BUTTON));
            }
            {
                input.SetMouseSensitivityX(1.0);
                C3DSlider* ctrl = static_cast<C3DSlider*>(GetCtrl(IDC_CONFIG_XAXIS));
                ctrl->SetThumbPos(1.0);
            }
            {
                input.SetMouseSensitivityY(1.0);
                C3DSlider* ctrl = static_cast<C3DSlider*>(GetCtrl(IDC_CONFIG_YAXIS));
                ctrl->SetThumbPos(1.0);
            }
            break;
        case IDC_CONFIG_YREVERSED:
        {
            input.ToggleReverseMouse();
            C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_YREVERSED));
            if (input.IsReverseMouse())
            {
                ctrl->SetText(LocalizeString(IDS_CONFIG_YREVERSED));
            }
            else
            {
                ctrl->SetText(LocalizeString(IDS_CONFIG_YNORMAL));
            }
        }
        break;
        case IDC_CONFIG_JOYSTICK:
        {
            input.ToggleJoystickEnabled();
            C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_JOYSTICK));
            if (input.IsJoystickEnabled())
            {
                ctrl->SetText(LocalizeString(IDS_CONFIG_JOYSTICK_ENABLED));
            }
            else
            {
                ctrl->SetText(LocalizeString(IDS_CONFIG_JOYSTICK_DISABLED));
            }
        }
        break;
        case IDC_CONFIG_BUTTONS:
        {
            input.ToggleMouseButtonsReversed();
            C3DActiveText* ctrl = static_cast<C3DActiveText*>(GetCtrl(IDC_CONFIG_BUTTONS));
            if (input.IsMouseButtonsReversed())
            {
                ctrl->SetText(LocalizeString(IDS_RIGHT_BUTTON));
            }
            else
            {
                ctrl->SetText(LocalizeString(IDS_LEFT_BUTTON));
            }
        }
        break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayConfigure::OnSliderPosChanged(int idc, float pos)
{
    auto& input = InputSubsystem::Instance();
    switch (idc)
    {
        case IDC_CONFIG_XAXIS:
            input.SetMouseSensitivityX(pos);
            break;
        case IDC_CONFIG_YAXIS:
            input.SetMouseSensitivityY(pos);
            break;
        default:
            Display::OnSliderPosChanged(idc, pos);
            break;
    }
}

// Select island game display
Control* DisplaySelectIsland::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_SELECT_ISLAND:
        {
            C3DListBox* lbox = new C3DListBox(this, idc, cls);
            int sel = 0;
            //				int m = (Pars>>"CfgWorlds">>"worlds").GetSize();
            int m = (Pars >> "CfgWorldList").GetEntryCount();
            for (int j = 0; j < m; j++)
            {
                const ParamEntry& entry = (Pars >> "CfgWorldList").GetEntry(j);
                if (!entry.IsClass())
                {
                    continue;
                }
                RString name = entry.GetName();
                //					RString name = (Pars>>"CfgWorlds">>"worlds")[j];

                // ADDED - check if wrp file exists
                RString fullname = GetWorldName(name);
                if (!QIFStreamB::FileExist(fullname))
                {
                    continue;
                }

                int index = lbox->AddString(Pars >> "CfgWorlds" >> name >> "description");
                lbox->SetData(index, name);
                if (stricmp(name, Glob.header.worldname) == 0)
                {
                    sel = index;
                }
                RString textureName = Pars >> "CfgWorlds" >> name >> "icon";
                RString fullName = FindPicture(textureName);
                fullName.Lower();
                Ref<Texture> texture = GlobLoadTexture(fullName);
                lbox->SetTexture(index, texture);
            }
            lbox->SetCurSel(sel);
            return lbox;
        }
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplaySelectIsland::OnLBDblClick(int idc, int curSel)
{
    if (idc == IDC_SELECT_ISLAND)
    {
        OnButtonClicked(IDC_OK);
    }
    else
    {
        Display::OnLBDblClick(idc, curSel);
    }
}

void DisplaySelectIsland::OnButtonClicked(int idc)
{
    switch (idc)
    {
            //	case IDC_OK:
        case IDC_CANCEL:
        {
            _exitWhenClose = idc;
            ControlObjectContainerAnim* ctrl =
                dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_SELECT_ISLAND_NOTEBOOK));
            if (ctrl)
            {
                ctrl->Close();
            }
        }
        break;
        case IDC_SELECT_ISLAND_WIZARD:
        {
            C3DListBox* lbox = dynamic_cast<C3DListBox*>(GetCtrl(IDC_SELECT_ISLAND));
            if (!lbox)
            {
                break;
            }

            int index = lbox->GetCurSel();
            if (index < 0)
            {
                break;
            }
            RString world = lbox->GetData(index);

            CreateChild(new DisplayWizardTemplate(this, world, false));
        }
        break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplaySelectIsland::OnChildDestroyed(int idd, int exit)
{
    if (idd == IDD_WIZARD_TEMPLATE && exit == IDC_OK)
    {
        Display::OnChildDestroyed(idd, exit);
        Exit(IDC_CUST_PLAY);
    }
    else
    {
        Display::OnChildDestroyed(idd, exit);
    }
}

void DisplaySelectIsland::OnCtrlClosed(int idc)
{
    if (idc == IDC_SELECT_ISLAND_NOTEBOOK)
    {
        Exit(_exitWhenClose);
    }
    else
    {
        Display::OnCtrlClosed(idc);
    }
}

// Custom arcade game display
Control* DisplayCustomArcade::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    return Display::OnCreateCtrl(type, idc, cls);
}

void DisplayCustomArcade::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_CUST_PLAY:
        case IDC_CUST_EDIT:
            Exit(idc);
            break;
        case IDC_CUST_DELETE:
        {
            CTree* tree = dynamic_cast<CTree*>(GetCtrl(IDC_CUST_GAME));
            CTreeItem* item = tree->GetSelected();
            if (item && item->level == 3)
            {
                CreateMsgBox(MB_BUTTON_OK | MB_BUTTON_CANCEL, LocalizeString(IDS_SURE), IDD_MSG_DELETEGAME);
            }
        }
        break;
        default:
            Display::OnButtonClicked(idc);
            break;
    }
}

void DisplayCustomArcade::OnChildDestroyed(int idd, int exit)
{
    switch (idd)
    {
        case IDD_MSG_DELETEGAME:
            Display::OnChildDestroyed(idd, exit);
            if (exit == IDC_OK)
            {
                CTree* tree = dynamic_cast<CTree*>(GetCtrl(IDC_CUST_GAME));
                CTreeItem* item = tree->GetSelected();
                if (!item)
                {
                    break;
                }
                if (item->level != 3)
                {
                    break;
                }
                RString mission = item->data;
                item = item->parent;
                RString world = item->data;
                item = item->parent;
                RString campaign = item->data;
                char buffer[256];
                /*
                                if (campaign.GetLength() > 0)
                                {
                                    snprintf(buffer, sizeof(buffer), "Campaigns\\%s\\Missions\\%s.%s\\mission.sqm",
                                        (const char *)campaign,
                                        (const char *)mission,
                                        (const char *)world
                                    );
                                }
                                else
                                {
                                    snprintf(buffer, sizeof(buffer), "Missions\\%s.%s\\mission.sqm",
                                        (const char *)mission,
                                        (const char *)world
                                    );
                                }
                                unlink(buffer);
                */
                if (campaign.GetLength() > 0)
                {
                    snprintf(buffer, sizeof(buffer), "Campaigns\\%s\\Missions\\%s.%s", (const char*)campaign,
                             (const char*)mission, (const char*)world);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), "Missions\\%s.%s", (const char*)mission, (const char*)world);
                }
                DeleteDirectoryStructure(buffer);
                InsertGames();
            }
            break;
        default:
            Display::OnChildDestroyed(idd, exit);
            break;
    }
}

bool DisplayCustomArcade::CanDestroy()
{
    if (!Display::CanDestroy())
    {
        return false;
    }

    if (_exit == IDC_CUST_PLAY)
    {
        CTree* tree = dynamic_cast<CTree*>(GetCtrl(IDC_CUST_GAME));
        CTreeItem* item = tree->GetSelected();
        if (!item)
        {
            return false;
        }
        return item->level >= 3;
    }
    else if (_exit == IDC_CUST_EDIT)
    {
        CTree* tree = dynamic_cast<CTree*>(GetCtrl(IDC_CUST_GAME));
        CTreeItem* item = tree->GetSelected();
        if (!item)
        {
            return false;
        }
        return item->level >= 2;
    }
    else
    {
        return true;
    }
}

void DisplayCustomArcade::ShowButtons()
{
    CTree* tree = dynamic_cast<CTree*>(GetCtrl(IDC_CUST_GAME));
    IControl* play = GetCtrl(IDC_CUST_PLAY);
    CButton* edit = dynamic_cast<CButton*>(GetCtrl(IDC_CUST_EDIT));
    IControl* del = GetCtrl(IDC_CUST_DELETE);

    CTreeItem* item = tree->GetSelected();
    if (!item)
    {
        if (play)
        {
            play->ShowCtrl(false);
        }
        if (edit)
        {
            edit->ShowCtrl(false);
        }
        if (del)
        {
            del->ShowCtrl(false);
        }
        return;
    }
    if (play)
    {
        play->ShowCtrl(item->level >= 3);
    }
    if (del)
    {
        del->ShowCtrl(item->level >= 3);
    }
    if (edit)
    {
        edit->ShowCtrl(item->level >= 2);
        if (item->level >= 3)
        {
            edit->SetText(LocalizeString(IDS_CUST_EDIT));
        }
        else
        {
            edit->SetText(LocalizeString(IDS_CUST_NEW));
        }
    }
}

void DisplayCustomArcade::OnTreeSelChanged(int idc)
{
    if (idc == IDC_CUST_GAME)
    {
        ShowButtons();
    }
    Display::OnTreeSelChanged(idc);
}

void DisplayCustomArcade::InsertGames()
{
    CTree* tree = dynamic_cast<CTree*>(GetCtrl(IDC_CUST_GAME));
    if (!tree)
    {
        return;
    }

    tree->RemoveAll();

    CTreeItem* root = tree->GetRoot();
    CTreeItem* selected = nullptr;
    root->text = "";
    {
        CTreeItem* itemCampaign = root->AddChild();
        itemCampaign->text = LocalizeString("STR_DISP_MAIN_SINGLE");
        itemCampaign->data = "";
        // int m = (Pars>>"CfgWorlds">>"worlds").GetSize();
        int m = (Pars >> "CfgWorldList").GetEntryCount();
        for (int j = 0; j < m; j++)
        {
            const ParamEntry& entry = (Pars >> "CfgWorldList").GetEntry(j);
            if (!entry.IsClass())
            {
                continue;
            }
            RString name = entry.GetName();

            // RString name = (Pars>>"CfgWorlds">>"worlds")[j];

            // ADDED - check if wrp file exists
            RString fullname = GetWorldName(name);
            if (!QIFStreamB::FileExist(fullname))
            {
                continue;
            }

            CTreeItem* itemWorld = itemCampaign->AddChild();

            itemWorld->text = Pars >> "CfgWorlds" >> name >> "description";
            itemWorld->data = name;
            bool wexp = stricmp(Glob.header.worldname, name) == 0;
            if (wexp && Glob.header.filename[0] == 0)
            {
                selected = itemWorld;
            }
            _finddata_t info;
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Missions\\*.%s", (const char*)name);
            intptr_t h = _findfirst(buffer, &info);
            if (h != -1)
            {
                do
                {
                    if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
                    {
                        char name[256];
                        snprintf(name, sizeof(name), "%s", (const char*)info.name);
                        char* ext = strrchr(name, '.');
                        if (ext)
                        {
                            *ext = 0;
                        }
                        CTreeItem* itemMission = itemWorld->AddChild();
                        itemMission->text = name;
                        itemMission->data = name;
                        if (wexp && stricmp(Glob.header.filename, name) == 0)
                        {
                            selected = itemMission;
                        }
                    }
                } while (_findnext(h, &info) == 0);
                _findclose(h);
            }
            itemWorld->SortChildren();
        }
    }

    _finddata_t info;
    intptr_t h = _findfirst("Campaigns\\*.*", &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
            {
                RString campaign = info.name;
                CTreeItem* itemCampaign = root->AddChild();
                ParamFile cfg;
                cfg.Parse(GetCampaignDirectory(campaign) + RString("description.ext"));
                itemCampaign->text = cfg >> "Campaign" >> "name";
                itemCampaign->data = campaign;
                // int m = (Pars>>"CfgWorlds">>"worlds").GetSize();
                int m = (Pars >> "CfgWorldList").GetEntryCount();
                for (int j = 0; j < m; j++)
                {
                    const ParamEntry& entry = (Pars >> "CfgWorldList").GetEntry(j);
                    if (!entry.IsClass())
                    {
                        continue;
                    }
                    RString name = entry.GetName();

                    // RString name = (Pars>>"CfgWorlds">>"worlds")[j];

                    // ADDED - check if wrp file exists
                    RString fullname = GetWorldName(name);
                    if (!QIFStreamB::FileExist(fullname))
                    {
                        continue;
                    }

                    CTreeItem* itemWorld = itemCampaign->AddChild();

                    itemWorld->text = Pars >> "CfgWorlds" >> name >> "description";
                    itemWorld->data = name;
                    bool wexp = stricmp(Glob.header.worldname, name) == 0;
                    _finddata_t info;
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "Campaigns\\%s\\Missions\\*.%s", (const char*)campaign,
                             (const char*)name);
                    intptr_t h = _findfirst(buffer, &info);
                    if (h != -1)
                    {
                        do
                        {
                            if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
                            {
                                char name[256];
                                snprintf(name, sizeof(name), "%s", (const char*)info.name);
                                char* ext = strrchr(name, '.');
                                if (ext)
                                {
                                    *ext = 0;
                                }
                                CTreeItem* itemMission = itemWorld->AddChild();
                                itemMission->text = name;
                                itemMission->data = name;
                                if (wexp && stricmp(Glob.header.filename, name) == 0)
                                {
                                    selected = itemMission;
                                }
                            }
                        } while (_findnext(h, &info) == 0);
                        _findclose(h);
                    }
                    itemWorld->SortChildren();
                }
            }
        } while (_findnext(h, &info) == 0);
        _findclose(h);
    }
    if (selected)
    {
        tree->SetSelected(selected);
        while (selected)
        {
            tree->Expand(selected, true);
            selected = selected->parent;
        }
    }
}

} // namespace Poseidon

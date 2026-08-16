#include <Poseidon/Input/KeyboardState.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::EnumName;
using Poseidon::Foundation::GetEnumNames;

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(Poseidon::CheatCode dummy)
{
    using namespace Poseidon;
    static const EnumName CheatCodeNames[] = {EnumName(CheatNone, "NONE"),
                                              EnumName(CheatUnlockCampaign, "CAMPAIGN"),
                                              EnumName(CheatExportMap, "TOPOGRAPHY"),
                                              EnumName(CheatWinMission, "ENDMISSION"),
                                              EnumName(CheatSaveGame, "SAVEGAME"),
                                              EnumName(CheatGodMode, "IWILLBETHEONE"),
                                              EnumName(CheatCrash, "CRASH"),
                                              EnumName(CheatFreeze, "FREEZE"),
                                              EnumName()};
    return CheatCodeNames;
}

namespace Poseidon
{
extern RString GetKeyName(int dikCode);

namespace
{
RString GetCheatKeyName(int dikCode)
{
    if (dikCode >= SDL_SCANCODE_A && dikCode <= SDL_SCANCODE_Z)
    {
        char text[2] = {static_cast<char>('A' + (dikCode - SDL_SCANCODE_A)), '\0'};
        return RString(text);
    }
    if (dikCode >= SDL_SCANCODE_1 && dikCode <= SDL_SCANCODE_9)
    {
        char text[2] = {static_cast<char>('1' + (dikCode - SDL_SCANCODE_1)), '\0'};
        return RString(text);
    }
    if (dikCode == SDL_SCANCODE_0)
        return RString("0");
    return GetKeyName(dikCode);
}
} // namespace

void KeyboardState::BufferKeyEvent(int scancode, bool down, DWORD timestamp)
{
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
        return;
    if (keyCount_ >= kKeyBufferSize)
        return;
    keyBuffer_[keyCount_++] = {scancode, down, timestamp};
}

void KeyboardState::DiscardBuffered()
{
    keyCount_ = 0;
}

void KeyboardState::Update(DWORD sysTime, DWORD timeDelta, bool userInputEnabled)
{
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++)
    {
        keys[k] = 0;
        keysToDo[k] = false;
        keysDoubleTapToDo[k] = false;
    }

    if (!userInputEnabled)
    {
        keyCount_ = 0;
        return;
    }

    float invTimeDelta = timeDelta > 0 ? 1.0f / timeDelta : 1000.0f;

    for (int i = 0; i < keyCount_; i++)
    {
        const KeyEvent& ev = keyBuffer_[i];
        int k = ev.dik;

        if (ev.down)
        {
            if (keyPressed[k] == 0)
            {
                if (keyLastPressed[k] != 0 && ev.timestamp - keyLastPressed[k] <= kDoubleTapWindowMs)
                {
                    keysDoubleTapToDo[k] = true;
                    keysDoubleTapActive[k] = true;
                }
                keyLastPressed[k] = ev.timestamp;
                keyPressed[k] = ev.timestamp;
                ProcessKeyPressed(k);
            }
        }
        else
        {
            if (keyPressed[k])
            {
                DWORD start = keyPressed[k];
                keyPressed[k] = 0;
                keysDoubleTapActive[k] = false;
                if (start < sysTime - timeDelta)
                    start = sysTime - timeDelta;
                DWORD howLong = ev.timestamp - start;
                keys[k] += howLong * invTimeDelta;
            }
        }
    }
    keyCount_ = 0;

    // Update keys still held down
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++)
    {
        if (keyPressed[k])
        {
            DWORD howLong = sysTime - keyPressed[k];
            if (howLong >= timeDelta)
                keys[k] = 1;
            else
                keys[k] += howLong * invTimeDelta;
        }
    }

    if (cheatEntryTrigger)
    {
        awaitCheat = true;
        cheatInProgress = RString();
    }
}

void KeyboardState::ProcessKeyPressed(int dik)
{
    if (!awaitCheat)
    {
        keysToDo[dik] = true;
        return;
    }

    RString key = GetCheatKeyName(dik);
    cheatInProgress = cheatInProgress + key;

    const EnumName* cheatCodeNames = GetEnumNames(CheatCode(0));
    bool matchPossible = false;
    for (int i = 0;; i++)
    {
        const EnumName& chName = cheatCodeNames[i];
        if (chName.name.GetLength() == 0)
            break;
        if (chName.value == CheatNone)
            continue;
        if (!strcmpi(chName.name, cheatInProgress))
        {
            awaitCheat = false;
            cheatActive = (CheatCode)chName.value;
            Foundation::GlobalShowMessage(1000, "Activated %s", static_cast<const char*>(cheatInProgress));
            return;
        }
        int len = cheatInProgress.GetLength();
        if (!strnicmp(chName.name, cheatInProgress, len))
            matchPossible = true;
    }
    if (!matchPossible)
        awaitCheat = false;
}

void KeyboardState::ForgetKeys()
{
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++)
    {
        keysToDo[k] = false;
        keysDoubleTapToDo[k] = false;
        keysDoubleTapActive[k] = false;
        keyLastPressed[k] = 0;
        keyPressed[k] = 0;
        keys[k] = 0;
    }
    keyCount_ = 0;
    cheatActive = CheatNone;
    awaitCheat = false;
    cheatEntryTrigger = false;
    cheatInProgress = RString();
}
} // namespace Poseidon

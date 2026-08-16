#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/PlayerMuteIgnore.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/WorldChatInput.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Audio/Voice/VonNet.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyLights.hpp>
#include <SDL3/SDL_keycode.h>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include <algorithm>
#define CHAT_ITEMS 100
#define CHAT_ITEMS_VISIBLE 4

using namespace Poseidon;
namespace Poseidon
{
} // namespace Poseidon

RString GetFullPlayerName()
{
    const PlayerIdentity* identity = GetNetworkManager().FindIdentity(GetNetworkManager().GetPlayer());
    if (identity)
    {
        return identity->GetName();
    }
    else
    {
        return Glob.header.playerName;
    }
}

RString GetFullPlayerName(Person* person)
{
    int player = person->GetRemotePlayer();
    if (player == 1)
    {
        return person->GetInfo()._name;
    }

    const PlayerIdentity* identity = GetNetworkManager().FindIdentity(player);
    if (identity)
    {
        return identity->GetName();
    }
    else
    {
        return person->GetInfo()._name;
    }
}

static ChatChannel VoNChatChannelToChatChannel(int channel)
{
    switch (static_cast<VoNChatChannel>(channel))
    {
        case VoNChatChannel::Direct:
            return CCDirect;
        case VoNChatChannel::Vehicle:
            return CCVehicle;
        case VoNChatChannel::Group:
            return CCGroup;
        case VoNChatChannel::Side:
            return CCSide;
        case VoNChatChannel::Global:
        default:
            return CCGlobal;
    }
}

static AutoArray<SpeakingIndication>& TestSpeakingIndications()
{
    static AutoArray<SpeakingIndication> speakers;
    return speakers;
}

void SetTestSpeakingIndications(const AutoArray<SpeakingIndication>& speakers)
{
    auto& testSpeakers = TestSpeakingIndications();
    testSpeakers.Clear();
    for (int i = 0; i < speakers.Size(); i++)
    {
        testSpeakers.Append() = speakers[i];
    }
}

ChatList::ChatList()
{
    SetRows(CHAT_ITEMS_VISIBLE);
    SetRect(0.05, 0.02, 0.9, 0.02);

    _font = nullptr;
    _size = 0;
    _bgColor = PackedColor(Color(0, 0, 0, 0.8));
    _colors[CCGlobal] = PackedColor(Color(0.8, 0.8, 0.8, 1));
    _colors[CCSide] = PackedColor(Color(0, 0.9, 0.9, 1));
    _colors[CCGroup] = PackedColor(Color(0.1, 0.9, 0.2, 1));
    _colors[CCVehicle] = PackedColor(Color(0.9, 0.8, 0, 1));
    _colors[CCDirect] = PackedColor(Color(0.9, 0, 0.8, 1));
    _enable = true;

    _offset = -1;
    _lastSpeaking = Poseidon::Foundation::UITime(0);
}

void ChatList::BrowseUp()
{
    if (_offset < Size() - 1)
    {
        _offset++;
    }
}

void ChatList::BrowseDown()
{
    if (_offset > 0)
    {
        _offset--;
    }
}

void ChatList::BrowseReset()
{
    _offset = -1;
}

void ChatList::SetRect(float x, float y, float w, float h)
{
    _x = x;
    _y = y;
    _w = w;
    _h = h;
}

void ChatList::Add(ChatChannel channel, RString from, RString text, bool playerMsg, bool forceDisplay)
{
    if (channel == CCDirect)
    {
        GWorld->SetTitleEffect(CreateTitleEffect(TitPlainDown, text));
        return;
    }

    while (Size() >= CHAT_ITEMS)
    {
        Delete(CHAT_ITEMS - 1);
    }
    Insert(0);
    Set(0).channel = channel;
    Set(0).from = from;
    Set(0).time = Glob.clock;
    Set(0).text = text;
    Set(0).uiTime = Glob.uiTime;
    Set(0).playerMsg = playerMsg;
    Set(0).forceDisplay = forceDisplay;

    if (_offset > 0)
    {
        _offset++;
    }
}

void ChatList::Add(ChatChannel channel, AIUnit* sender, RString text, bool playerMsg, bool forceDisplay)
{
    char from[256];
    from[0] = 0;
    if (sender)
    {
        Person* person = sender->GetPerson();
        PoseidonAssert(person);

        // Client-local ignore: drop text chat from an ignored remote player.
        // Use !IsLocal() rather than IsRemotePlayer(): _remotePlayer may still
        // be 1 (the AI default) on a JIP client before role assignment completes,
        // which would cause IsRemotePlayer() to return false and bypass the filter.
        if (person && !person->IsLocal() && IsChatIgnored(person->GetRemotePlayer()))
            return;

        switch (channel)
        {
            case CCGlobal:
            {
                AIGroup* grp = sender->GetGroup();
                if (!grp)
                {
                    break;
                }
                AICenter* center = grp->GetCenter();
                if (!center)
                {
                    break;
                }
                switch (center->GetSide())
                {
                    case TWest:
                        snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_WEST));
                        break;
                    case TEast:
                        snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_EAST));
                        break;
                    case TGuerrila:
                        snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_GUERRILA));
                        break;
                    case TCivilian:
                        snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_CIVILIAN));
                        break;
                }
            }
            break;
            case CCSide:
            {
                AIGroup* grp = sender->GetGroup();
                if (!grp)
                {
                    break;
                }
                snprintf(from, sizeof(from), "%s %d", (const char*)grp->GetName(), sender->ID());
            }
            break;
            case CCGroup:
                snprintf(from, sizeof(from), "%d", sender->ID());
                break;
            case CCVehicle:
            {
                Transport* veh = sender->GetVehicleIn();
                if (!veh)
                {
                    break;
                }
                if (person == veh->Commander())
                {
                    snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_COMMANDER));
                }
                else if (person == veh->Driver())
                {
                    if (veh->GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)))
                    {
                        snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_PILOT));
                    }
                    else
                    {
                        snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_DRIVER));
                    }
                }
                else if (person == veh->Gunner())
                {
                    snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_GUNNER));
                }
                else if (sender == veh->CommanderUnit())
                {
                    snprintf(from, sizeof(from), "%s", (const char*)LocalizeString(IDS_COMMANDER));
                }
                else
                {
                    LOG_DEBUG(Script, "Warning: unknown position");
                }
            }
            break;
            case CCDirect:
                GWorld->SetTitleEffect(CreateTitleEffect(TitPlainDown, text));
                return;
        }

        if (person && person->IsNetworkPlayer())
        {
            if (from[0])
            {
                strncat(from, " (", sizeof(from) - strlen(from) - 1);
                strncat(from, GetFullPlayerName(person), sizeof(from) - strlen(from) - 1);
                strncat(from, ")", sizeof(from) - strlen(from) - 1);
            }
            else
            {
                snprintf(from, sizeof(from), "%s", (const char*)GetFullPlayerName(person));
            }
        }
    }
    Add(channel, from, text, playerMsg, forceDisplay);
}

void ChatList::OnDraw()
{
    if (_rows <= 0)
    {
        return;
    }

    const float startDim = 25, endDim = 30;
    int w = GEngine->Width2D();
    int h = GEngine->Height2D();

    if (!_font)
    {
        _font = GEngine->LoadFont(GetFontID("tahomaB24"));
        PoseidonAssert(_font);
        _size = 0.02;
    }

    float top = _y + (_rows - 1) * _h;
    int row = _rows - 1;

    GetSpeakingPlayers();
    if (_speaking.Size() > 0 || _lastSpeaking > Glob.uiTime - 2.0f)
    {
        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
        const float border = 0.004f;
        const float space = GEngine->GetTextWidth(_size, _font, " ");
        float left = _x + border;
        float lineWidth = _w;
        for (int i = 0; i < _speaking.Size(); i++)
        {
            const SpeakingIndication& item = _speaking[i];
            const float textWidth = GEngine->GetTextWidth(_size, _font, item.name);
            const float cellWidth = textWidth + 2.0f * border;
            if (cellWidth > lineWidth)
            {
                break;
            }

            GEngine->Draw2D(mip, _bgColor, Rect2DPixel((left - border) * w, top * h, cellWidth * w, _h * h));
            GEngine->DrawText(Point2DFloat(left, top), _size, Rect2DFloat(left - border, top, cellWidth, _h), _font,
                              _colors[item.channel], item.name);
            left += cellWidth + space;
            lineWidth -= cellWidth + space;
        }

        row--;
        if (row < 0)
        {
            return;
        }
        top -= _h;
    }

    int n = Size();
    if (n == 0)
    {
        return;
    }
    int begin = _offset;
    saturate(begin, 0, n - 1);
    for (int i = begin; i < n; i++)
    {
        const ChatItem& item = Get(i);
        if (item.text.GetLength() == 0)
        {
            continue;
        }
        if (!_enable && !item.forceDisplay)
        {
            continue;
        }
        float age = Glob.uiTime - item.uiTime;
        float alpha = 1;
        if (_offset == -1 && age > startDim)
        {
            // interpolate dim
            if (age > endDim)
            {
                continue;
            }
            alpha = (endDim - age) * (1.0 / (endDim - startDim));
        }
        RString buffer;
        if (item.from.GetLength() == 0)
        {
            buffer = item.text;
        }
        else
        {
            buffer = item.from + RString(": \"") + item.text + RString("\"");
        }
        PackedColor color, colorB;
        color = _colors[item.channel];
        color = PackedColorRGB(color, toIntFloor(color.A8() * alpha));
        colorB = PackedColorRGB(_bgColor, toIntFloor(_bgColor.A8() * alpha));

        if (item.playerMsg)
        {
            // make background less intensive
            colorB = PackedColorRGB(color, color.A8() / 2);
            // make foreground more intensive
            color = PackedColor(Color(0, 0, 0, alpha));
        }

        AUTO_STATIC_ARRAY(int, lines, 32);
        lines.Add(0);

        if (GEngine->GetTextWidth(_size, _font, buffer) > _w)
        {
            const char* p = buffer;
            const char* word = nullptr;
            float width = 0;
            while (*p != 0)
            {
                const char* q = p;
                char c = *p++;
                if ((unsigned char)c <= 32)
                {
                    word = p;
                }
                char temp[8];
                p = buffer + CopyTextElement(buffer, static_cast<int>(q - (const char*)buffer), _font->GetLangID(),
                                             temp, sizeof(temp));
                width += GEngine->GetTextWidth(_size, _font, temp);
                if (width > _w)
                {
                    if (word)
                    {
                        p = word;
                    }
                    else
                    {
                        p = q;
                    }
                    lines.Add(p - buffer);
                    word = nullptr;
                    width = 0;
                }
            }
        }

        for (int i = lines.Size() - 1; i >= 0; i--)
        {
            char* line = buffer.MutableData() + lines[i];

            float wline = GEngine->GetTextWidth(_size, _font, line);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
            GEngine->Draw2D(mip, colorB, Rect2DPixel(_x * w, top * h, wline * w, _h * h));
            GEngine->DrawText(Point2DFloat(_x, top), _size, Rect2DFloat(_x, top, _w, _h), _font, color, line);
            *line = 0;

            row--;
            if (row < 0)
            {
                return;
            }
            top -= _h;
        }
    }
}

void ChatList::GetSpeakingPlayers()
{
    AutoArray<NetVoiceSpeakerInfo, Poseidon::Foundation::MemAllocSA> activeSpeakers;
    GetNetworkManager().GetVoiceSpeakers(activeSpeakers);

    AutoArray<SpeakingIndication> speaking;
    auto& testSpeakers = TestSpeakingIndications();
    for (int i = 0; i < testSpeakers.Size(); i++)
    {
        SpeakingIndication item = testSpeakers[i];
        item.lastSpeaking = Glob.uiTime;
        speaking.Append() = item;
        _lastSpeaking = Glob.uiTime;
    }

    for (int i = 0; i < activeSpeakers.Size(); i++)
    {
        const NetVoiceSpeakerInfo& speaker = activeSpeakers[i];
        if (!speaker.active)
        {
            continue;
        }

        SpeakingIndication& item = speaking.Append();
        const PlayerIdentity* identity = GetNetworkManager().FindIdentity(speaker.player);
        item.name = identity ? identity->GetName() : Format("Player %d", speaker.player);
        item.channel = VoNChatChannelToChatChannel(speaker.channel);
        item.lastSpeaking = Glob.uiTime;
        _lastSpeaking = Glob.uiTime;
    }

    for (int i = 0; i < speaking.Size(); i++)
    {
        const SpeakingIndication& item = speaking[i];
        bool alreadyThere = false;
        for (int j = 0; j < _speaking.Size(); j++)
        {
            SpeakingIndication& stable = _speaking[j];
            if (strcmp(stable.name, item.name) == 0)
            {
                stable.lastSpeaking = item.lastSpeaking;
                stable.channel = item.channel;
                alreadyThere = true;
                break;
            }
        }
        if (!alreadyThere)
        {
            _speaking.Append() = item;
        }
    }

    int dst = 0;
    for (int src = 0; src < _speaking.Size(); src++)
    {
        if (_speaking[src].lastSpeaking >= Glob.uiTime - 1.0f)
        {
            if (dst != src)
            {
                _speaking[dst] = _speaking[src];
            }
            dst++;
        }
    }
    _speaking.Resize(dst);
}

static void WhatUnitsVehicle(RefArray<NetworkObject>& units, Transport* veh)
{
    Person* person = veh->Driver();
    if (person && person->IsRemotePlayer())
    {
        units.Add(person);
    }
    person = veh->Commander();
    if (person && person->IsRemotePlayer())
    {
        units.Add(person);
    }
    person = veh->Gunner();
    if (person && person->IsRemotePlayer())
    {
        units.Add(person);
    }
}

static void WhatUnitsGroup(RefArray<NetworkObject>& units, AIGroup* grp)
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        Person* person = unit->GetPerson();
        if (person && person->IsRemotePlayer())
        {
            units.Add(person);
        }
    }
}

static void WhatUnitsSide(RefArray<NetworkObject>& units, AICenter* center)
{
    for (int j = 0; j < center->NGroups(); j++)
    {
        AIGroup* grp = center->GetGroup(j);
        if (!grp)
        {
            continue;
        }
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = grp->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            Person* person = unit->GetPerson();
            if (person && person->IsRemotePlayer())
            {
                units.Add(person);
            }
        }
    }
}

static void WhatUnitsDirect(RefArray<NetworkObject>& units, Person* person)
{
    const float maxDist = 20.0;

    Vector3Val pos = person->Position();
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, maxDist);

    for (int x = xMin; x <= xMax; x++)
    {
        for (int z = zMin; z <= zMax; z++)
        {
            if (!InRange(x, z))
            {
                continue;
            }

            const ObjectList& list = GLandscape->GetObjects(z, x);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                if (!obj)
                {
                    continue;
                }
                if (obj->GetType() != TypeVehicle)
                {
                    continue;
                }

                EntityAI* veh = dyn_cast<EntityAI>(obj);
                if (!veh)
                {
                    continue;
                }
                if (veh->IsDammageDestroyed())
                {
                    continue;
                }

                AIUnit* unit = veh->CommanderUnit();
                if (!unit)
                {
                    continue;
                }
                if (!unit->GetPerson()->IsRemotePlayer())
                {
                    continue;
                }
                if (!unit->IsFreeSoldier())
                {
                    continue;
                }
                PoseidonAssert(unit->GetPerson() == veh);

                float dist2 = pos.Distance2(obj->Position());
                if (dist2 <= Square(maxDist))
                {
                    units.Add(veh);
                }
            }
        }
    }
}

// Players that can receive a message on the given channel from object.
void WhatUnits(RefArray<NetworkObject>& units, ChatChannel channel, NetworkObject* object)
{
    switch (channel)
    {
        case CCVehicle:
        {
            Transport* veh = dynamic_cast<Transport*>(object);
            if (veh)
            {
                WhatUnitsVehicle(units, veh);
            }
        }
        break;
        case CCGroup:
        {
            AIGroup* grp = dynamic_cast<AIGroup*>(object);
            if (grp)
            {
                WhatUnitsGroup(units, grp);
            }
        }
        break;
        case CCSide:
        {
            AICenter* center = dynamic_cast<AICenter*>(object);
            if (center)
            {
                WhatUnitsSide(units, center);
            }
        }
        break;
        case CCGlobal:
            /*
                        WhatUnitsAll(units);
            */
            break;
        case CCDirect:
        default:
            Fail("Bad radio channel");
            break;
    }
}

// Players that can receive a message on the given channel; unit is the sender.
static void WhatUnits(AIUnit* unit, RefArray<NetworkObject>& units, ChatChannel channel)
{
    switch (channel)
    {
        case CCVehicle:
        {
            Transport* veh = unit->GetVehicleIn();
            if (veh)
            {
                WhatUnitsVehicle(units, veh);
            }
        }
        break;
        case CCGroup:
        {
            AIGroup* grp = unit->GetGroup();
            if (grp)
            {
                WhatUnitsGroup(units, grp);
            }
        }
        break;
        case CCSide:
        {
            AIGroup* grp = unit->GetGroup();
            if (!grp)
            {
                break;
            }
            AICenter* center = grp->GetCenter();
            if (center)
            {
                WhatUnitsSide(units, center);
            }
        }
        break;
        case CCGlobal:
            break;
        case CCDirect:
        {
            if (unit->IsFreeSoldier())
            {
                WhatUnitsDirect(units, unit->GetPerson());
            }
        }
        break;
        case CCNone:
            break;
        default:
            Fail("Bad radio channel");
            break;
    }
}

// Players that can receive a message on the given channel, with the local player as sender.
static void WhatUnits(RefArray<NetworkObject>& units, ChatChannel channel)
{
    Person* person = GWorld->GetRealPlayer();
    if (!person)
    {
        return;
    }
    AIUnit* unit = person->Brain();
    if (!unit)
    {
        return;
    }

    WhatUnits(unit, units, channel);
}

// Sends a chat message from the player on the given channel.
void SendChat(ChatChannel channel, RString text)
{
    if (text.GetLength() > 0 && text[0] == '#')
    {
        GChatList.Add(channel, "", text, false, true);
        GetNetworkManager().ProcessCommand(text);
        return;
    }

    AIUnit* sender = GWorld->GetRealPlayer() ? GWorld->GetRealPlayer()->Brain() : nullptr;

    if (GetNetworkManager().GetGameState() < NGSPlay || !sender)
    {
        // use playerRoles info
        GetNetworkManager().Chat(channel, text);
        GChatList.Add(channel, GetFullPlayerName(), text, false, true);
    }
    else
    {
        // send actual state
        RefArray<NetworkObject> units;
        WhatUnits(units, channel);
        if (channel == CCGlobal || units.Size() > 0)
        {
            GetNetworkManager().Chat(channel, sender, units, text);
        }

        GChatList.Add(channel, sender, text, false, true);
    }
}

// Sends a mission radio message (wave) on the given channel.
void SendRadioChatWave(ChatChannel channel, NetworkObject* object, RString wave, AIUnit* sender, RString senderName)
{
    if (GetNetworkManager().GetGameState() < NGSPlay)
    {
        return;
    }
    RefArray<NetworkObject> units;
    WhatUnits(units, channel, object);
    if (channel == CCGlobal || units.Size() > 0)
    {
        GetNetworkManager().RadioChatWave(channel, units, wave, sender, senderName);
    }
}

// Sends a mission radio message (wave) on the given channel.
void SendRadioChatWave(ChatChannel channel, RString wave, AIUnit* sender, RString senderName)
{
    if (GetNetworkManager().GetGameState() < NGSPlay)
    {
        return;
    }
    RefArray<NetworkObject> units;
    WhatUnits(sender, units, channel);
    if (channel == CCGlobal || units.Size() > 0)
    {
        GetNetworkManager().RadioChatWave(channel, units, wave, sender, senderName);
    }
}

// Sends a radio message on the given channel.
void SendRadioChat(ChatChannel channel, AIUnit* sender, NetworkObject* object, RString text, RadioSentence& sentence)
{
    if (GetNetworkManager().GetGameState() < NGSPlay)
    {
        return;
    }
    RefArray<NetworkObject> units;
    WhatUnits(units, channel, object);
    if (channel == CCGlobal || units.Size() > 0)
    {
        GetNetworkManager().RadioChat(channel, sender, units, text, sentence);
    }
}

// Sends a map marker on the given channel.
void SendMarker(ChatChannel channel, AIUnit* sender, ArcadeMarkerInfo& marker)
{
    if (GetNetworkManager().GetGameState() < NGSBriefing)
    {
        return;
    }
    RefArray<NetworkObject> units;
    WhatUnits(units, channel);
    if (channel == CCGlobal || units.Size() > 0)
    {
        GetNetworkManager().MarkerCreate(channel, sender, units, marker);
    }
}

// Channel selection display.
class DisplayChannel : public Display
{
  public:
    static int _channel;

  public:
    DisplayChannel(ControlsContainer* parent);
    void OnButtonClicked(int idc) override {}
    void ResetHUD() override;
};

int DisplayChannel::_channel = CCGlobal;

AbstractOptionsUI* CreateChannelUI()
{
    return new DisplayChannel(nullptr);
}

ChatChannel ActualChatChannel()
{
    return (ChatChannel)DisplayChannel::_channel;
}

void SetChatChannel(ChatChannel channel)
{
    DisplayChannel::_channel = channel;
}

void NextChatChannel()
{
    DisplayChannel::_channel++;
    if (DisplayChannel::_channel >= CCN)
    {
        DisplayChannel::_channel = 0;
    }
}

void PrevChatChannel()
{
    DisplayChannel::_channel--;
    if (DisplayChannel::_channel < 0)
    {
        DisplayChannel::_channel = CCN - 1;
    }
}

DisplayChannel::DisplayChannel(ControlsContainer* parent) : Display(parent)
{
    Load("RscDisplayChannel");
    SetCursor(nullptr);
    ResetHUD();
}

void DisplayChannel::ResetHUD()
{
    CStatic* text = dynamic_cast<CStatic*>(GetCtrl(IDC_CHANNEL));
    if (text)
    {
        text->SetText(LocalizeString(IDS_CHANNEL_GLOBAL + _channel));
        text->SetFtColor(GChatList.GetColor(_channel));
    }
}

// Chat line display: lets the player type chat messages.
class DisplayChatLine : public Display
{
  public:
    DisplayChatLine(ControlsContainer* parent);
    void OnButtonClicked(int idc) override;
    bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    void Destroy() override;
    void DestroyHUD(int exit) override;
};

DisplayChatLine::DisplayChatLine(ControlsContainer* parent) : Display(parent)
{
    Load("RscDisplayChat");
    SetCursor(nullptr);
}

void DisplayChatLine::OnButtonClicked(int idc)
{
    if (idc == IDC_OK)
    {
        CEdit* edit = dynamic_cast<CEdit*>(GetCtrl(IDC_CHAT));
        if (edit)
        {
            SendChat(ActualChatChannel(), edit->GetText());
        }
    }
    Display::OnButtonClicked(idc);
    if (_exit >= 0 && !_destroyed)
    {
        if (CanDestroy())
        {
            Destroy();
        }
        else
        {
            _exit = -1;
        }
    }
}

ChatList GChatList;

bool DisplayChatLine::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    // catch submit/cancel/channel/scroll keys so they don't transmit into the game
    if (Poseidon::IsChatConsumedKey(nChar))
    {
        return true;
    }

    return Display::OnKeyDown(nChar, nRepCnt, nFlags);
}

bool IsPlayerDead();

bool DisplayChatLine::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (Poseidon::IsChatSubmitKey(nChar))
    {
        OnButtonClicked(IDC_OK);
        return true;
    }
    else if (nChar == SDLK_ESCAPE)
    {
        OnButtonClicked(IDC_CANCEL);
        return true;
    }
    else if (nChar == SDLK_DOWN)
    {
        if (Poseidon::ShouldSwitchMultiplayerChannelFromChatInput() && !IsPlayerDead())
        {
            PrevChatChannel();
            if (GWorld->ChatChannel())
            {
                GWorld->ChatChannel()->ResetHUD();
            }
            if (GWorld->VoiceChat())
            {
                GWorld->VoiceChat()->ResetHUD();
            }
            GWorld->OnChannelChanged();
        }
        return true;
    }
    else if (nChar == SDLK_UP)
    {
        if (Poseidon::ShouldSwitchMultiplayerChannelFromChatInput() && !IsPlayerDead())
        {
            NextChatChannel();
            if (GWorld->ChatChannel())
            {
                GWorld->ChatChannel()->ResetHUD();
            }
            if (GWorld->VoiceChat())
            {
                GWorld->VoiceChat()->ResetHUD();
            }
            GWorld->OnChannelChanged();
        }
        return true;
    }
    else if (nChar == SDLK_PAGEUP)
    {
        GChatList.BrowseUp();
        return true;
    }
    else if (nChar == SDLK_PAGEDOWN)
    {
        GChatList.BrowseDown();
        return true;
    }

    return Display::OnKeyUp(nChar, nRepCnt, nFlags);
}

void DisplayChatLine::Destroy()
{
    Display::Destroy();
    DestroyHUD(_exit);
}

void DisplayChatLine::DestroyHUD(int exit)
{
    GWorld->DestroyChat(exit);
    GChatList.BrowseReset();
}

AbstractOptionsUI* CreateChatUI()
{
    return new DisplayChatLine(nullptr);
}

// Voice chat indicator: shows a picture while voice recording is on; its color marks the channel.
class DisplayVoiceChat : public Display
{
  public:
    DisplayVoiceChat(ControlsContainer* parent, bool pushToTalk);
    bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    void OnSimulate(EntityAI* vehicle) override;
    void Destroy() override;
    void DestroyHUD(int exit) override;

    void ResetHUD() override;

  private:
    void UpdateTransmitIndicator();

    bool _pushToTalk = false;
    int _lastTransmitHealth = -1;
};

DisplayVoiceChat::DisplayVoiceChat(ControlsContainer* parent, bool pushToTalk)
    : Display(parent), _pushToTalk(pushToTalk)
{
    Load("RscDisplayVoiceChat");
    SetCursor(nullptr);

    ResetHUD();
}

bool DisplayVoiceChat::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return false;
}

bool DisplayVoiceChat::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    return false;
}

void DisplayVoiceChat::OnSimulate(EntityAI* vehicle)
{
    auto& input = InputSubsystem::Instance();
    bool close = _pushToTalk ? input.GetAction(UAVoiceOverNetPushToTalk, false) <= 0
                             : input.GetActionToDo(UAVoiceOverNet, true, false);
    if (close && !_destroyed)
    {
        // Destroy() frees *this via GWorld->DestroyVoiceChat(); SimulateHUD() still holds
        // the raw ptr and reads GetExitCode() after OnSimulate() returns — UAF.
        // Exit(0) defers the DestroyHUD() call to SimulateHUD() after it is done with ptr.
        GetNetworkManager().SetVoiceChannel(CCNone);
        Display::Destroy();
        Exit(0);
        return;
    }

    UpdateTransmitIndicator();
}

void DisplayVoiceChat::UpdateTransmitIndicator()
{
    // The indicator must reflect the voice pipeline, not the key state: with
    // a dead microphone or an unassigned voice identity the key is held, the
    // HUD shows, and nothing is sent — turn the indicator red so the sender
    // knows the transmission is failing.
    const int health = GetNetworkManager().GetVoiceTransmitHealth();
    if (health == _lastTransmitHealth)
        return;
    _lastTransmitHealth = health;

    const bool failing = health == static_cast<int>(VoNTransmitHealth::NoCapture) ||
                         health == static_cast<int>(VoNTransmitHealth::NotConnected) ||
                         health == static_cast<int>(VoNTransmitHealth::Stalled);
    CStatic* text = dynamic_cast<CStatic*>(GetCtrl(IDC_VOICE_CHAT));
    if (text)
    {
        // Red is not a chat-channel color, so a failing transmit cannot be
        // mistaken for any channel tint.
        text->SetFtColor(failing ? PackedColor(Color(0.9, 0.1, 0.1, 1)) : GChatList.GetColor(ActualChatChannel()));
    }
    if (failing)
        LOG_WARN(Network, "VoN: transmit requested but not sending (health={})", health);
}

void DisplayVoiceChat::Destroy()
{
    Display::Destroy();
    GetNetworkManager().SetVoiceChannel(CCNone);
    DestroyHUD(_exit);
}

void DisplayVoiceChat::ResetHUD()
{
    ChatChannel channel = ActualChatChannel();

    if (GetNetworkManager().GetGameState() < NGSPlay)
    {
        // use playerRoles info
        GetNetworkManager().SetVoiceChannel(channel);
    }
    else
    {
        // send actual state
        RefArray<NetworkObject> units;
        WhatUnits(units, (ChatChannel)channel);
        if (Poseidon::ShouldUseExplicitMultiplayerVoiceTargets(channel, units.Size(), CCGlobal, CCDirect))
        {
            GetNetworkManager().SetVoiceChannel(channel, units);
        }
        else
        {
            GetNetworkManager().SetVoiceChannel(channel);
        }
    }

    CStatic* text = dynamic_cast<CStatic*>(GetCtrl(IDC_VOICE_CHAT));
    if (text)
    {
        text->SetFtColor(GChatList.GetColor(channel));
    }
}

void DisplayVoiceChat::DestroyHUD(int exit)
{
    GWorld->DestroyVoiceChat(exit);
}

AbstractOptionsUI* CreateVoiceChatUI(bool pushToTalk)
{
    return new DisplayVoiceChat(nullptr, pushToTalk);
}

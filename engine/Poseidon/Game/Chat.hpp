#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Audio/Speaker.hpp>
#include <Poseidon/Network/NetTransport.hpp>

namespace Poseidon
{
} // namespace Poseidon

DEFINE_ENUM_BEG(ChatChannel)
	CCNone = -1,
	CCGlobal,
	CCSide,
	CCGroup,
	CCVehicle,
	CCDirect,
	CCN
DEFINE_ENUM_END(ChatChannel)

ChatChannel ActualChatChannel();
void SetChatChannel(ChatChannel channel);

struct ChatItem
{
	ChatChannel channel;
	RString from;
	Poseidon::Clock time;
	RString text;
	Poseidon::Foundation::UITime uiTime;
	bool playerMsg;
	bool forceDisplay;
};

struct SpeakingIndication
{
    RString name;
    ChatChannel channel = CCGlobal;
    Poseidon::Foundation::UITime lastSpeaking = Poseidon::Foundation::UITime(0);
};

class NetworkObject;
using Poseidon::RadioSentence;

// Chat history: stores and draws the chat rows.
class ChatList : public AutoArray<ChatItem>
{
protected:
	float _x;			// top
	float _y;			// left
	float _w;			// width
	float _h;			// row height
	int _rows;

	PackedColor _colors[CCN];
	PackedColor _bgColor;
	Ref<Font> _font;
	float _size;
	bool _enable;

	int _offset;
    Poseidon::Foundation::UITime _lastSpeaking;
    AutoArray<SpeakingIndication> _speaking;

public:
	ChatList();

	PackedColor GetColor(int channel) {return _colors[channel];}

	void SetRect(float x, float y, float w, float h);
	void SetRows(int rows) {_rows = rows;}

	// forceDisplay shows the message even when the radio protocol is off.
	void Add(ChatChannel channel, RString from, RString text, bool playerMsg, bool forceDisplay);
	void Add(ChatChannel channel, AIUnit *sender, RString text, bool playerMsg, bool forceDisplay);

	void Enable(bool enable) {_enable = enable;}
	bool Enabled() const {return _enable;}

	void OnDraw();
    void GetSpeakingPlayers();

	void BrowseUp();
	void BrowseDown();
	void BrowseReset();
};

void SendRadioChat(ChatChannel channel, AIUnit *sender, NetworkObject *object, RString text, RadioSentence &sentence);
void SendRadioChatWave(ChatChannel channel, NetworkObject *object, RString wave, AIUnit *sender, RString senderName);
void SendRadioChatWave(ChatChannel channel, RString wave, AIUnit *sender, RString senderName);
void SendMarker(ChatChannel channel, AIUnit *sender, ArcadeMarkerInfo &marker);

void SetTestSpeakingIndications(const AutoArray<SpeakingIndication>& speakers);

extern ChatList GChatList;


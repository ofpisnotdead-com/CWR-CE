#pragma once

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Input/ControllerUiInput.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>


namespace Poseidon
{
class AbstractOptionsUI : public SerializeClass
{
	public:
	
	virtual void DrawHUD(VehicleWithAI *vehicle, float alpha) = 0;

	
	virtual void SimulateHUD(VehicleWithAI *vehicle) = 0;

	virtual ~AbstractOptionsUI() {}

	
	virtual void DestroyHUD(int exit) {}
	virtual void ResetHUD() {};

	virtual bool DoKeyDown( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) {return false;}
	virtual bool DoKeyUp( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) {return false;}
	virtual ControllerUiScene GetControllerUiScene() const {return MenuControllerScene();}
	virtual bool DoControllerUiAction(ControllerUiAction action) {return false;}
	virtual bool DoChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) {return false;}
	virtual bool DoIMEChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) {return false;}
	virtual bool DoIMEComposition( unsigned nChar, unsigned nFlags ) {return false;}

	virtual bool DoUnregisteredAddonUsed( RString addon ) {return false;}

	virtual bool IsTopmost() const {return true;}
	virtual bool IsSimulationEnabled() const {return true;}
	virtual bool IsDisplayEnabled() const {return true;}
	virtual bool IsUIEnabled() const {return true;}

	LSError Serialize(ParamArchive &ar) override;
};

AbstractOptionsUI *CreateMainOptionsUI();
AbstractOptionsUI *CreateTurnOptionsUI();
AbstractOptionsUI *CreateEndOptionsUI(int mode);
AbstractOptionsUI *CreatePlayerKilledUI();

AbstractOptionsUI *CreatePrepareTurnUI();
AbstractOptionsUI *CreateMainMapUI();

const ParamEntry* FindRadio(RString name, SoundPars& pars);
const ParamEntry* FindSound(RString name, SoundPars& pars);
const ParamEntry* FindRscTitle(RString name);
const ParamEntry* FindMusic(RString name, SoundPars& pars);
void FindEnvSound(RString name, SoundPars& day, SoundPars& night);

} // namespace Poseidon

using ::Poseidon::FindEnvSound;
using ::Poseidon::FindMusic;
using ::Poseidon::FindRscTitle;
using ::Poseidon::FindSound;

// Defined at global scope in Game/Chat.cpp.
Poseidon::AbstractOptionsUI *CreateChatUI();
Poseidon::AbstractOptionsUI *CreateVoiceChatUI(bool pushToTalk = false);

#pragma once
#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/AI/Path/AITypes.hpp>


namespace Poseidon
{
class AbstractUI
{
protected:
	bool _showUnitInfo;
	bool _showTacticalDisplay;
	bool _showCompass;
	bool _showTankDirection;
	bool _showMenu;
	bool _showGroupInfo;

	bool _showCursors;

public:
	// draw overlay - called before FinishDraw
	virtual void DrawHUD
	(
		const Camera &camera, EntityAI *vehicle, CameraType cam
	) = 0;

	// simulate controls - called during simulation
	virtual void SimulateHUD
	(
		const Camera &camera, EntityAI *vehicle, CameraType cam, float deltaT
	) = 0;

	virtual void DrawHUDNonAI
	(
		const Camera &camera, Entity *vehicle, CameraType cam
	) = 0;

	// simulate controls - called during simulation
	virtual void SimulateHUDNonAI
	(
		const Camera &camera, Entity *vehicle, CameraType cam, float deltaT
	) = 0;

	// called in BrowseCamera - when HUD subject changes
	virtual void Init() = 0;
	virtual void ResetHUD() = 0;
	virtual void ResetVehicle( EntityAI *vehicle ) = 0; // when vehicle is changed
	virtual void OnWeaponRemoved(int slot) = 0;

	virtual const AutoArray<RString> &GetCustomRadio() const = 0;
	virtual bool PlayCustomRadio(int index) = 0;

	void ShowUnitInfo(bool show = true) {_showUnitInfo = show;}
	void ShowTacticalDisplay(bool show = true) {_showTacticalDisplay = show;}
	void ShowCompass(bool show = true) {_showCompass = show;}
	void ShowMenu(bool show = true) {_showMenu = show;}
	void ShowTankDirection(bool show = true) {_showTankDirection = show;}
	void ShowGroupInfo(bool show = true) {_showGroupInfo = show;}
	void ShowAll(bool show = true);

	void ShowCursors(bool show = true) {_showCursors = show;}

	virtual void ShowMe() = 0;
	virtual void ShowTarget() = 0;
	virtual void ShowFormPosition() = 0;
	virtual void ShowGroupDir() = 0;

	virtual void ShowHint(RString hint) = 0;
	virtual bool IsCommandMenuOpen() const { return false; }

	// in-game action menu entry labels, one per line (empty when no menu). For tri tests.
	virtual RString GetActionMenuTexts() const { return RString(); }

	// in-game command menu visible labels, one per line (empty when closed). For tri tests.
	virtual RString GetCommandMenuTexts() const { return RString(); }

	// used to handle mouse movements
	virtual Vector3 GetCursorDirection() const = 0;
	virtual void SetCursorDirection( Vector3Par dir ) = 0;

	virtual void SetCursorMode( bool world ) = 0; // mouse used - switch to world mode
	virtual bool GetCursorMode() const = 0; // mouse used - switch to world mode

	virtual Vector3 GetWorldCursor() const = 0;
	virtual void SetWorldCursor( Vector3Par dir ) = 0;
	virtual Vector3 GetModelCursor() const = 0;
	virtual void SetModelCursor( Vector3Par dir ) = 0;
	
	virtual float GetCursorAge() const = 0;
	
	virtual void SwitchToStrategy( EntityAI *vehicle ) = 0;
	virtual void SwitchToFire( EntityAI *vehicle ) = 0;
	
	virtual ~AbstractUI(){}
};

AbstractUI *CreateInGameUI();
AbstractUI *CreateMenuUI();

} // namespace Poseidon

#pragma once

#include <Poseidon/UI/Controls/UIControlsBase.hpp>
#include <Poseidon/UI/Controls/UIControlsWidgets.hpp>

class MsgBox;

RString ControlLineTextWithoutTrailingBreaks(const RString &text, int from, int to);

enum ControlList
{
	CLNone,
	CLBackground,
	CLObjects,
	CLForeground
};

struct ControlId
{
	ControlList list;
	int id;

	ControlId() {list = CLNone; id = -1;}
	ControlId(ControlList l, int i) {list = l; id = i;}
	void operator =(const ControlId &src)
	{
		list = src.list;
		id = src.id;
	}
	bool operator ==(const ControlId &with) const
	{
		return with.list == list && with.id == id;
	}
	bool operator !=(const ControlId &with) const
	{
		return with.list != list || with.id != id;
	}

	static ControlId Null() {return ControlId();}
	bool IsNull() const {return list == CLNone;}
};

class ControlsContainer : public RefCount
{
protected:
	int _idd;
	ControlsContainer *_parent;

	Ref<Texture> _cursorTexture;
	float _cursorX;
	float _cursorY;
	float _cursorW;
	float _cursorH;
	RString _cursorName;
	PackedColor _cursorColor;

	RefArray<Control> _controlsBackground;
	RefArray<ControlObject> _objects;
	RefArray<Control> _controlsForeground;
	ControlId _indexFocused;
	ControlId _indexLCaptured;
	ControlId _indexRCaptured;
	ControlId _indexDefault;
	ControlId _indexMouseOver;

// last mouse states
	float _lastX;
	float _lastY;
	Poseidon::Foundation::UITime _dblClkTimeout;
	float _dblClkX;
	float _dblClkY;
	bool _lastLButton;
	bool _lastRButton;

	bool _alwaysShow;
	bool _movingEnable;
	bool _moving;

	bool _enableSimulation;
	bool _enableDisplay;
	bool _enableUI;
	bool _destroyed;
	int _exit;

	Poseidon::Foundation::UITime _tooltipTime;

public:

	ControlsContainer(ControlsContainer *parent);
	~ControlsContainer() override;
	void Init();

	int IDD() const {return _idd;}
	int GetExitCode() const {return _exit;}
	bool AlwaysShow() const {return _alwaysShow;}
	bool SimulationEnabled() const {return _enableSimulation;}
	bool DisplayEnabled() const {return _enableDisplay;}
	bool UIEnabled() const {return _enableUI;}
	IControl *GetCtrl(int idc);
	IControl *GetCtrl(float x, float y);
	const RefArray<ControlObject>& GetObjects() const { return _objects; }
	void RefreshLocalizedText();
	IControl *GetFocused();
	int GetFocusedIdc();
	ControlsContainer *Parent() {return _parent;}
	virtual ControlsContainer *Child() {return nullptr;}
	virtual const ControlsContainer *Child() const {return nullptr;}
	virtual void CreateChild(ControlsContainer *child) {}
	virtual MsgBox *GetMsgBox() {return nullptr;}

	virtual void CreateMsgBox(int flags, RString text, int idd = -1) {}
	virtual void DestroyMsgBox() {}
	virtual bool IsTop() const {return true;}

	RString GetCursorName() const {return _cursorName;}
	Texture *GetCursorTexture() const {return _cursorTexture;}
	float GetCursorX() const {return _cursorX;}
	float GetCursorY() const {return _cursorY;}
	float GetCursorW() const {return _cursorW;}
	float GetCursorH() const {return _cursorH;}
	PackedColor GetCursorColor() const {return _cursorColor;}

	void SetCursor(const char *name);

	void LoadControl(const ParamEntry &ctrlCls);

	void LoadObject(const ParamEntry &ctrlCls);

	void LoadControlBackground(const ParamEntry &ctrlCls);

	void Load(const ParamEntry &clsEntry);
	void Load(const char *clsName);
	void Load(int idd);
	void NextCtrl();
	void PrevCtrl();
	void NextDefaultCtrl();
	void PrevDefaultCtrl();
	virtual bool CanDestroy();
	virtual void Destroy();
	void Exit(int code)
	{
		if (_exit < 0)
			_exit = code;
	}
	void ForceExit(int code)
	{
		_exit = code;
	}

	void FocusCtrl(int idc);

	void Move(float dx, float dy);

	virtual Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls);

	virtual ControlObject *OnCreateObject(int type, int idc, const ParamEntry &cls);

	virtual void OnDraw(EntityAI *vehicle, float alpha);
	virtual void OnSimulate(EntityAI *vehicle);
	virtual bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);
	virtual bool OnUnregisteredAddonUsed(RString addon);
	virtual void OnChildDestroyed(int idd, int exit) {}

	virtual void OnButtonClicked(int idc);

	virtual void OnLBSelChanged(int idc, int curSel) {}

	virtual void OnLBDblClick(int idc, int curSel) {}

	virtual void OnLBDrag(int idc, int curSel) {}
	virtual void OnLBDragging(float x, float y) {}
	virtual void OnLBDrop(float x, float y) {}

	virtual void OnComboSelChanged(int idc, int curSel) {}

	virtual void OnTreeSelChanged(int idc) {}

	virtual void OnTreeDblClick(int idc, CTreeItem *sel) {}

	virtual void OnToolBoxSelChanged(int idc, int curSel) {}

	virtual void OnCheckBoxesSelChanged(int idc, int curSel, bool value) {}

	virtual void OnHTMLLink(int idc, RString link) {}

	virtual void OnSliderPosChanged(int idc, float pos) {}

	virtual void OnCtrlClosed(int idc) {}

	virtual void OnObjectMoved(int idc, Vector3Par offset) {}

	void DrawCursor();

private:

	bool SetFocus(ControlId &id, bool up = true, bool def = false);
	IControl *Ctrl(ControlId &id);
	ControlId Next(ControlId &id);
	ControlId Prev(ControlId &id);
	void SortObjects();
};

namespace CursorDrawDebug
{
void Record(ControlsContainer *owner, int x, int y, int w, int h);
bool Read(int &idd, int &x, int &y, int &w, int &h);
} // namespace CursorDrawDebug

class MsgBox : public ControlsContainer
{
	RString _text;
public:

	MsgBox(ControlsContainer *parent, int flags, RString text, int idd);
};

class Display : public ControlsContainer, public AbstractOptionsUI
{
protected:
	Ref<ControlsContainer> _child;
	Ref<MsgBox> _msgBox;
	int _langCbToken = -1;

public:

	Display(ControlsContainer *parent);
	~Display() override;

	ControlsContainer *Child() override {return _child;}
	const ControlsContainer *Child() const override {return _child;}
	void CreateChild(ControlsContainer *child) override {_child = child;}
	void OnChildDestroyed(int idd, int exit) override {_child = 0;}
	bool IsTopmost() const override {return _child == nullptr && _msgBox == nullptr;}
	bool IsTop() const override {return _child == nullptr && _msgBox == nullptr;}

	void DrawHUD(VehicleWithAI *vehicle, float alpha) override;
	void SimulateHUD(VehicleWithAI *vehicle) override;
	bool DoKeyDown( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) override;
	bool DoKeyUp( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) override;
	Poseidon::ControllerUiScene GetControllerUiScene() const override;
	bool DoControllerUiAction(Poseidon::ControllerUiAction action) override;
	bool DoChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) override;
	bool DoIMEChar( unsigned nChar, unsigned nRepCnt, unsigned nFlags ) override;
	bool DoIMEComposition( unsigned nChar, unsigned nFlags ) override;
	bool DoUnregisteredAddonUsed( RString addon ) override;

	MsgBox *GetMsgBox() override {return _msgBox;}
	void CreateMsgBox(int flags, RString text, int idd = -1) override
	{_msgBox = new MsgBox(this, flags, text, idd);}
	void DestroyMsgBox() override
	{_msgBox = nullptr;}
	bool IsSimulationEnabled() const override;
	bool IsDisplayEnabled() const override;
	bool IsUIEnabled() const override;
};


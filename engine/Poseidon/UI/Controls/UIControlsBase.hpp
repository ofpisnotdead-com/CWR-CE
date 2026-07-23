#pragma once

#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>

#include <Poseidon/Foundation/Time/Time.hpp>

#include <Poseidon/Graphics/Rendering/Draw/Font.hpp>
#include <Poseidon/Core/Global.hpp>

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/Foundation/Math/Interpol.hpp>
#include <Poseidon/World/Simulation/Animation/RtAnimation.hpp>
#include <SDL3/SDL_keycode.h>

#include <Poseidon/Foundation/Types/LLinks.hpp>

class ControlsContainer;
class ControlObject;
struct ControlInObject;

int ResolveLegacyControlInt(const ParamEntry& entry);

inline bool IsUiConfirmKey(unsigned nChar)
{
    return nChar == SDLK_RETURN || nChar == SDLK_KP_ENTER;
}

class IControl
{
protected:
	ControlsContainer *_parent;
	int _idc;

	bool _visible;
	bool _enabled;
	bool _focused;
	bool _default;

	Poseidon::Foundation::UITime _timer;

	RString _tooltip;
	RString _semanticTestText;
	bool _hasSemanticTestText = false;

	PackedColor _tooltipColorText;
	PackedColor _tooltipColorBox;
	PackedColor _tooltipColorShade;

	static Ref<Font> _tooltipFont;
	static float _tooltipSize;

public:

	IControl(ControlsContainer *parent, int idc, const ParamEntry &cls);

	IControl(ControlsContainer *parent, int idc);
	virtual ~IControl() {}

	int IDC() {return _idc;}
	virtual int GetType() = 0;

	virtual int GetStyle() = 0;

	virtual void ReloadLocalizedText() {}

	bool IsVisible() {return _visible;}

	void ShowCtrl(bool show = true) {_visible = show;}
	bool IsEnabled() {return _enabled;}

	void EnableCtrl(bool enable = true) {_enabled = enable;}
	bool IsFocused() {return _focused;}

	virtual IControl *GetFocused() {return this;}

	virtual bool OnSetFocus(bool up = true, bool def = false);	// return false if focus cannot not be gained

	virtual bool OnKillFocus();	// return false if focus cannot be killed
	virtual bool CanBeDefault() const {return false;}
	bool IsDefault() {return _default;}
	void SetDefault() {_default = true;}
	RString GetTooltip() const {return _tooltip;}
	void SetTooltip(RString text) {_tooltip = text;}
	bool HasSemanticTestText() const {return _hasSemanticTestText;}
	RString GetSemanticTestText() const {return _semanticTestText;}
	void SetSemanticTestText(RString text) {_semanticTestText = text; _hasSemanticTestText = true;}
	void ClearSemanticTestText() {_semanticTestText = RString(); _hasSemanticTestText = false;}
	Poseidon::Foundation::UITime GetTimer() {return _timer;}

	void SetTimer(Poseidon::Foundation::UITime uiTime) {_timer = uiTime;}
	virtual void OnTimer();

	virtual bool IsInside(float x, float y) = 0;
	virtual void Move(float dx, float dy) = 0;

	virtual IControl *GetCtrl(int idc);

	virtual IControl *GetCtrl(float x, float y);

	virtual bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	virtual bool OnIMEComposition(unsigned nChar, unsigned nFlags);

	virtual void OnLButtonDown(float x, float y);

	virtual void OnLButtonUp(float x, float y);

	virtual void OnLButtonClick(float x, float y);

	virtual void OnLButtonDblClick(float x, float y);

	virtual void OnRButtonDown(float x, float y);

	virtual void OnRButtonUp(float x, float y);

	virtual void OnMouseMove(float x, float y, bool active = true);

	virtual void OnMouseHold(float x, float y, bool active = true);

	virtual void OnMouseZChanged(float dz);

	virtual void OnDraw(float alpha) = 0;

	virtual void DrawTooltip(float x, float y);

	virtual bool OnCanDestroy(int code);

	virtual void OnDestroy(int code);

	static void PlaySound(SoundPars &pars);
};

class Control : public RemoveOLinks, public IControl
{
protected:
	int _type;
	int _style;
	float _x;
	float _y;
	float _w;
	float _h;

	float _scale;

protected:

	Control(ControlsContainer *parent, int type, int idc,
		int style, float x, float y, float w, float h);

public:

	Control(ControlsContainer *parent, int type, int idc, const ParamEntry &cls);

	int GetType() override {return _type;}
	int GetStyle() override {return _style;}
	bool IsInside(float x, float y) override
		{ return x >= _x && x <= _x + _w && y >= _y && y <= _y + _h;}
	float X() const {return _x;}
	float Y() const {return _y;}
	float W() const {return _w;}
	float H() const {return _h;}

	float GetScale() const {return _scale;}
	void SetScale(float scale) {_scale = scale;}

	void Move(float dx, float dy) override;
	virtual void SetPos(float x, float y, float w, float h)
	{_x = x; _y = y; _w = w; _h = h;}

	virtual void UpdateInfo(ControlObject *object, ControlInObject &info);
};

namespace Poseidon { struct Point2DFloat; }
using Poseidon::Point2DFloat;

class ControlObject : public Object, public IControl
{
protected:
	Vector3 _position;
	Vector3 _modelPos;
	Vector3 _offset;

	bool _moving;

public:

	ControlObject(ControlsContainer *parent, int idc, const ParamEntry &cls);
	~ControlObject() override;

	int GetType() override;
	int GetStyle() override;
	Vector3Val GetBasePosition() const {return _position;}

	bool IsInside(float x, float y) override;
	static Vector3 Convert2DTo3D(const Point2DFloat &point, float z);

	using Object::Move;
	void Move(float dx, float dy) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnRButtonDown(float x, float y) override;
	void OnRButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	void OnDraw(float alpha) override;

	virtual void UpdatePosition() {}

protected:
	bool _rotating = false;
	float _rotLastX = 0;
	float _rotLastY = 0;
};

class ControlObjectWithZoom : public ControlObject, public SerializeClass
{
protected:
	Vector3 _positionBack;

	bool _inBack;
	bool _zooming;
	bool _enableZoom;

	float _zoomDuration;

	Poseidon::Foundation::UITime _zoomStart;
	Matrix4 _zoomMatrices[3];
	float _zoomTimes[3];
	SRef<M4Function> _interpolator;

public:

	ControlObjectWithZoom(ControlsContainer *parent, int idc, const ParamEntry &cls);

	int GetType() override;

	void OnLButtonDblClick(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;

	void UpdatePosition() override;
	LSError Serialize(ParamArchive &ar) override;

	void Zoom(bool zoom);
	bool IsZooming() const { return _zooming; }
};

struct ControlInObject
{
	Ref<Control> _control;
	float x, y, w, h;
	RString selection;
	Vector3 posTL, posTR, posBL, posBR;
};

class ControlObjectContainer : public ControlObjectWithZoom
{
protected:
	typedef ControlObjectWithZoom base;

	AutoArray<ControlInObject> _controls;
	int _indexFocused;
	int _indexL;
	int _indexR;
	int _indexMove;

public:

	ControlObjectContainer(ControlsContainer *parent, int idc, const ParamEntry &cls);

	int GetType() override;
	IControl *GetCtrl(int idc) override;
	IControl *GetCtrl(float x, float y) override;

	bool SetSubControlPos(int idc, float x, float y, float w, float h);

	int GetHoveredIdc() const;

	int GetFocusedIdc();

	bool FocusCtrlByIdc(int idc);

	int GetLeftPressedIdc() const;

	bool DebugSetLeftPressedIdc(int idc);

	void ReloadLocalizedText() override;

	IControl *GetFocused() override;
	bool OnSetFocus(bool up = true, bool def = false) override;
	bool CanBeDefault() const override;

	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnLButtonClick(float x, float y) override;
	void OnLButtonDblClick(float x, float y) override;
	void OnRButtonDown(float x, float y) override;
	void OnRButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnIMEComposition(unsigned nChar, unsigned nFlags) override;

	void OnDraw(float alpha) override;
	void DrawTooltip(float x, float y) override;

	void SetPosition(Vector3Par pos) override;
public:
	Control *LoadControl(const ParamEntry &ctrlCls);
	bool RemoveControl(int idc);
protected:
	void LoadControls(const ParamEntry &cls);
	int FindControl(float x, float y);

	void SetFocus(int i, bool def = false);
	bool NextCtrl();
	bool PrevCtrl();
	bool NextDefaultCtrl();
	bool PrevDefaultCtrl();
};

using Poseidon::AnimationRT;
using Poseidon::WeightInfo;
using Poseidon::Skeleton;

class ControlObjectContainerAnim : public ControlObjectContainer
{
protected:
	bool _open;
	bool _close;
	bool _autoZoom;
	Poseidon::Foundation::UITime _animStart;
	Ref<AnimationRT> _animation;
	WeightInfo _weights;
	Ref<Skeleton> _skeleton;

	float _phase;
	float _animSpeed;
	float _invAnimSpeed;

public:

	ControlObjectContainerAnim(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void Animate(int level) override;
	void Deanimate(int level) override;
	bool IsAnimated(int level) const override {return true;}

	void Open();
	void Close();

	float GetPhase() const {return _phase;}
	bool IsOpen() {return !_open && !_close && _phase >= 0.5;}
	bool IsClosed() {return !_open && !_close && _phase <= 0;}
	bool IsAnimating() const { return _open || _close; }
};

namespace Poseidon { struct Point2DFloat; }
using Poseidon::Point2DFloat;

#define DrawCoord Point2DFloat

class CStatic : public Control
{
friend class MsgBox;	// Message Box created static directly
private:
	RString _text;
protected:
	const ParamEntry *_textCls = nullptr;
	PackedColor _bgColor;
	PackedColor _ftColor;
	Ref<Font> _font;
	float _size;
	Ref<Texture> _texture;

	PackedColor _color1;
	PackedColor _color2;
	PackedColor _color3;
	PackedColor _color4;
	PackedColor _color5;
	float _alpha;

	AutoArray<int> _lines;
	float _firstLine;
	float _lineSpacing;
	float _shadowOffsetXRatio;
	float _shadowOffsetYRatio;
	bool _shadowOffsetXAspectCorrect;

protected:

	CStatic
	(
		ControlsContainer *parent, const ParamEntry &cls,
		float x, float y, float w, float h, RString text
	);

public:

	CStatic(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnDraw(float alpha) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	RString GetText() const {return _text;}

	RString GetLine(int i) const;
	virtual bool SetText(RString text);	// returns true if changed
	virtual bool SetText(const char *text);	// returns true if changed
	void ReloadLocalizedText() override
	{
		// SetText clears _textCls; save & restore so subsequent language switches still fire.
		if (const ParamEntry* saved = _textCls)
		{
			SetText(RString(*saved >> "text"));
			_textCls = saved;
		}
	}
	PackedColor GetBgColor() {return _bgColor;}
	void SetBgColor(PackedColor color) {_bgColor = color;}
	PackedColor GetFtColor() {return _ftColor;}
	void SetFtColor(PackedColor color) {_ftColor = color;}

	int NLines() const;
	float GetTextHeight() const;

protected:
	void InitColors();
	void InitTextShadow(const ParamEntry &cls);
	virtual void DrawText(const char *text, float top, float alpha);
	virtual bool IsText();

	void FormatText();
};

class CStaticTime : public CStatic
{
protected:
	Clock _time;
	bool _blink;
public:

	CStaticTime(ControlsContainer *parent, int idc, const ParamEntry &cls, bool blink)
		: CStatic(parent, idc, cls) {_blink = blink;}
	Clock GetTime() const {return _time;}
	void SetTime(Clock time) {_time = time;}

protected:
	void DrawText(const char *text, float top, float alpha) override;
	bool IsText() override;
};

class CSkewStatic : public Control
{
protected:
	PackedColor _color;
	PackedColor _lineColors[5];

	float _xTL;
	float _yTL;
	float _alphaTL;
	float _xTR;
	float _yTR;
	float _alphaTR;
	float _xBL;
	float _yBL;
	float _alphaBL;
	float _xBR;
	float _yBR;
	float _alphaBR;
public:
	CSkewStatic(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void Move(float dx, float dy) override;

	void OnDraw(float alpha) override;
};

enum HTMLFormat
{
	HFError = -1,
	HFP = 0,
	HFH1,
	HFH2,
	HFH3,
	HFH4,
	HFH5,
	HFH6,
	HFImg,
};

enum HTMLAlign
{
	HALeft,
	HACenter,
	HARight
};

struct HTMLField
{
	bool nextline;
	bool bottom;
	bool bold;
	bool exclude;
	RString text;
	HTMLFormat format;
	HTMLAlign align;
	RString href;
	// image parameters
	RString condition;
	Ref<Texture> texture1;
	Ref<Texture> texture2;
	float width;
	float height;
	float indent;
	float tableWidth;

	Texture *GetTexture();
};

struct HTMLRow
{
	HTMLAlign align;
	bool bottom;
	float width;
	float height;
	int firstField;
	int firstPos;
	int lastField;
	int lastPos;
};

struct HTMLSection
{
	AutoArray <HTMLField> fields;
	AutoArray <RString> names;
	AutoArray <HTMLRow> rows;
};

class CHTMLContainer
{
protected:
	AutoArray <HTMLSection> _sections;
	AutoArray <RString> _bookmarks;
	int _currentSection;
	int _activeField;

	PackedColor _bgColor;
	PackedColor _textColor;
	PackedColor _boldColor;
	PackedColor _linkColor;
	PackedColor _activeLinkColor;

	Ref<Font> _fontH1;
	Ref<Font> _fontH1Bold;
	float _sizeH1;
	Ref<Font> _fontH2;
	Ref<Font> _fontH2Bold;
	float _sizeH2;
	Ref<Font> _fontH3;
	Ref<Font> _fontH3Bold;
	float _sizeH3;
	Ref<Font> _fontH4;
	Ref<Font> _fontH4Bold;
	float _sizeH4;
	Ref<Font> _fontH5;
	Ref<Font> _fontH5Bold;
	float _sizeH5;
	Ref<Font> _fontH6;
	Ref<Font> _fontH6Bold;
	float _sizeH6;
	Ref<Font> _fontP;
	Ref<Font> _fontPBold;
	float _sizeP;

	RString _filename;
	float _indent;
public:

	CHTMLContainer(const ParamEntry &cls);

	int NSections() const {return _sections.Size();}
	const HTMLSection &GetSection(int s) const {return _sections[s];}

	float GetIndent() const {return _indent;}
	void SetIndent(float indent) {_indent = indent;}

	int AddSection();
	void InitSection(int section);
	void RemoveSection(int s);
	void CopySection(int from, int to);
	int FindSection(const char *name);
	void SwitchSection(const char *name);
	int CurrentSection() const {return _currentSection;}
	// Name of the current section's first bookmark (test hook; empty if none).
	RString CurrentSectionName() const
	{
		if (_currentSection < 0 || _currentSection >= _sections.Size()) return RString();
		const HTMLSection &s = _sections[_currentSection];
		return s.names.Size() > 0 ? s.names[0] : RString();
	}

	void AddBreak(int section, bool bottom);
	void AddText
	(
		int section, RString text,
		HTMLFormat format, HTMLAlign align, bool bottom, bool bold,
		RString href, float tableWidth = 0
	);
	void AddName(int section, RString name);
	HTMLField *AddImage
	(
		int section, RString image, HTMLAlign align, bool bottom,
		float w, float h, RString href, RString text = RString(),
		float tableWidth = 0
	);
	void AddBookmark(RString link);

	void Init();

	void Load(const char *filename, bool add = false);
	void LoadBuffer(const char *filenameHint, const char *utf8Text, bool add = false);

	int ActiveBookmark();

	const HTMLField *GetActiveField() const;

	PackedColor GetBgColor() const {return _bgColor;}
	void SetBgColor(PackedColor color) {_bgColor = color;}
	PackedColor GetTextColor() const {return _textColor;}
	void SetTextColor(PackedColor color) {_textColor = color;}
	PackedColor GetLinkColor() const {return _linkColor;}
	void SetLinkColor(PackedColor color) {_linkColor = color;}

	void FormatSection(int s);
	void SplitSection(int s);

	virtual float GetPageWidth() const = 0;
	virtual float GetPageHeight() const = 0;

	virtual float GetTextWidth(float size, Font *font, const char *text) const = 0;

	float GetPHeight() const {return _sizeP;}
protected:
	virtual int FindField(float x, float y);
};

class CHTML : public Control, public CHTMLContainer
{
public:

	CHTML(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnDraw(float alpha) override;
	void OnLButtonDown(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;

	// Test hook: find the field whose href matches and replay the click action
	// (same as OnLButtonDown). Returns "OK:section=<name>,img=<n>" describing the
	// resulting current section, or "FAIL:no_link". Used by triClickBriefingLink.
	RString ActivateHRef(const char *href);

	// Test hook: like ActivateHRef but goes through the REAL hit-test — switches to
	// the link's section, scans screen positions until FindField maps one to the
	// link, then drives OnMouseMove + OnLButtonDown there. Distinguishes a hit-test
	// miss ("FAIL:findfield_miss") from a working click path.
	RString ProbeClickHRef(const char *href);

	// Test hook: locate the on-screen centre of the link field whose href matches.
	// Switches to the link's section (FindField only sees _currentSection), scans
	// for the field's first on-screen hit, and returns that point in screen coords.
	// Returns false (outX/outY untouched) when no such link exists. Used by
	// triBriefingLinkRoute / triBriefingClickAt to drive a click at the link.
	bool FindLinkScreenPos(const char *href, float &outX, float &outY);

	float GetPageWidth() const override;
	float GetPageHeight() const override;
	float GetTextWidth(float size, Font *font, const char *text) const override;

protected:
	int FindField(float x, float y) override;
};

namespace Poseidon { class IAutoComplete; }
using Poseidon::IAutoComplete;

#if __ICL
	#include <Poseidon/UI/Locale/AutoComplete.hpp>
#endif

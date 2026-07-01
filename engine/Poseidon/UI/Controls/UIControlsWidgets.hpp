#pragma once

#include <Poseidon/UI/Controls/UIControlsBase.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
namespace Poseidon { class ParamEntry; }
namespace Poseidon { class Font; }
namespace Poseidon { class IAutoComplete; }
class CEditContainer
{
protected:
	RString _text;

	Ref<IAutoComplete> _autoComplete;

	PackedColor _ftColor;
	PackedColor _selColor;
	Ref<Font> _font;
	float _size;

	int _firstVisible;
	int _blockBegin;
	int _blockEnd;

	int _maxChars;
	bool _enableToolip;

	AutoArray<int> _lines;

public:

	CEditContainer(const ParamEntry &cls);

	virtual void SetText(RString text);
	virtual void SetAutoComplete(IAutoComplete *ac);
	RString GetText() const {return _text;}
	void SetMaxChars(int value) {_maxChars = value;}
	int GetMaxChars() const {return _maxChars;}
	void SetCaretPos(int pos);
	bool DoKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	bool DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	bool DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags);
	bool DoIMEComposition(unsigned nChar, unsigned nFlags);

protected:
//	virtual int FindPos(float x, float y);
	virtual bool IsMulti() const = 0;

	virtual float PosToX(RString text, int pos) const = 0;
	virtual int XToPos(RString text, float x) const = 0;

	virtual void EnsureVisible(int pos) = 0;

	int CurLine() const;
	int FirstLine() const;
	RString GetLine(int i) const;

	virtual void FormatText() = 0;

	void BlockDelete();
	void BlockCopy();
	void BlockCut();
	void BlockPaste();

	int NextPos(int pos);
	int PrevPos(int pos);
};

class CEdit : public Control, public CEditContainer
{
public:

	CEdit(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnDraw(float alpha) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnIMEComposition(unsigned nChar, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;

protected:
	bool IsMulti() const override;

	int FindPos(float x, float y);

	float PosToX(RString text, int pos) const override;
	int XToPos(RString text, float x) const override;

	void EnsureVisible(int pos) override;
	void DrawText(const char *text, int offset, float top, float alpha);

	void FormatText() override;
};

class CButton : public Control
{
friend class MsgBox;
protected:
	RString _text;
	const ParamEntry *_textCls = nullptr;
	PackedColor _ftColor;
	Ref<Font> _font;
	float _size;

	PackedColor _color1;
	PackedColor _color2;
	PackedColor _color3;
	PackedColor _color4;
	PackedColor _color5;

	SoundPars _pushSound;
	SoundPars _clickSound;
	SoundPars _escapeSound;

	bool _state;

	RString _action;

protected:

	CButton(ControlsContainer *parent, const ParamEntry &cls, float x, float y, float w, float h);

public:

	CButton(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnDraw(float alpha) override;

	virtual void OnClicked();

	RString GetText() {return _text;}
	void SetText(RString text) {_text = text; _textCls = nullptr;}
	void ReloadLocalizedText() override
	{
		if (_textCls) _text = *_textCls >> "text";
	}
	PackedColor GetFtColor() {return _ftColor;}
	void SetFtColor(PackedColor color) {_ftColor = color;}

	RString GetAction() {return _action;}
	void SetAction(RString action) {_action = action;}

	bool CanBeDefault() const override {return true;}

protected:
	void InitColors();
};

class CActiveText : public Control
{
protected:
	RString _text;
	const ParamEntry *_textCls = nullptr;
	Ref<Font> _font;
	float _size;
	PackedColor _color;
	PackedColor _colorActive;
	PackedColor _colorFocusBg;
	float _pressX = 0.0f;
	float _pressY = 0.0f;
	float _pressW = 0.0f;
	float _pressH = 0.0f;

	bool _active;

	bool _returnDown = false;

	SoundPars _enterSound;
	SoundPars _pushSound;
	SoundPars _clickSound;
	SoundPars _escapeSound;

	RString _action;

public:

	CActiveText(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnDraw(float alpha) override;

	RString GetText() {return _text;}
	void SetText(RString text) {_text = text; _textCls = nullptr;}
	void ReloadLocalizedText() override
	{
		if (_textCls) _text = *_textCls >> "text";
	}

	RString GetAction() {return _action;}
	void SetAction(RString action) {_action = action;}
	void SetColor(PackedColor color) {_color = color;}
	PackedColor GetColor() const { return _color; }
	void SetActiveColor(PackedColor color) {_colorActive = color;}
	PackedColor GetActiveColor() const { return _colorActive; }

	bool IsActive() const {return _active;}

	bool CanBeDefault() const override {return true;}
};

class CSliderContainer
{
protected:
	float _minPos;
	float _maxPos;
	float _curPos;
	float _lineStep;
	float _pageStep;

	float _thumbOffset;
	bool _thumbLocked;

	PackedColor _color;

public:

	CSliderContainer(const ParamEntry &cls);

	float GetThumbPos() {return _curPos;}
	void GetRange(float &min, float &max) {min = _minPos; max = _maxPos;}
	float GetRange() {return _maxPos - _minPos;}
	void GetSpeed(float &line, float &page) {line = _lineStep; page = _pageStep;}
	void SetThumbPos(float pos)
	{
		if (pos <= _minPos)
			_curPos = _minPos;
		else if (pos >= _maxPos)
			_curPos = _maxPos;
		else
			_curPos = pos;
	}
	void SetRange(float min, float max)
	{
		if (min <= max)
		{
			_minPos = min;
			_maxPos = max;
		}
		if (_curPos < _minPos)
			_curPos = _minPos;
		else if (_curPos > _maxPos)
			_curPos = _maxPos;
	}
	void SetSpeed(float line, float page) {_lineStep = line; _pageStep = page;}
};

class CSlider : public Control, public CSliderContainer
{
public:

	CSlider(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnDraw(float alpha) override;
};

class CScrollBar
{
	float _x;
	float _y;
	float _w;
	float _h;

	float _minPos;
	float _maxPos;

	float _curPos;
	float _page;
	float _lineStep;
	float _pageStep;

	float _thumbOffset;
	bool _thumbLocked;

	bool _enabled;

	PackedColor _color;

public:
	CScrollBar();

	void OnLButtonDown(float x, float y);
	void OnLButtonUp(float x, float y);
	void OnMouseHold(float x, float y);
	void OnDraw(float alpha);

	bool IsEnabled() const {return _enabled;}
	void Enable(bool enable = true) {_enabled = enable;}
	bool IsInside(float x, float y)
		{ return x >= _x && x < _x + _w && y >= _y && y < _y + _h;}
	float GetPos() {return _curPos;}
	void SetPos(float pos)
	{
		if (pos <= _minPos)
			_curPos = _minPos;
		else if (pos >= _maxPos)
			_curPos = _maxPos;
		else
			_curPos = pos;
	}
	void SetRange(float min, float max, float page)
	{
		_minPos = min;
		_maxPos = max;
		_page = page;
		saturateMax(_maxPos, _minPos + page);
		saturate(_curPos, _minPos, _maxPos - page);
	}
	void SetSpeed(float line, float page) {_lineStep = line; _pageStep = page;}
	void SetPosition(float x, float y, float w, float h)
	{
		_x = x; _y = y; _w = w; _h = h;
	}
	void SetColor(PackedColor color) {_color = color;}
	bool IsLocked() const {return _thumbLocked;}
};

class CProgressBar : public Control
{
	float _minPos;
	float _maxPos;
	float _curPos;

	PackedColor _frameColor;
	PackedColor _barColor;

public:

	CProgressBar(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnDraw(float alpha) override;

	float GetPos() {return _curPos;}
	using Control::SetPos;
	void SetPos(float pos)
	{
		if (pos <= _minPos)
			_curPos = _minPos;
		else if (pos >= _maxPos)
			_curPos = _maxPos;
		else
			_curPos = pos;
	}
	void GetRange(float &min, float &max) {min = _minPos; max = _maxPos;}
	float GetRange() {return _maxPos - _minPos;}
	void SetRange(float min, float max)
	{
		if (min <= max)
		{
			_minPos = min;
			_maxPos = max;
		}
		if (_curPos < _minPos)
			_curPos = _minPos;
		else if (_curPos > _maxPos)
			_curPos = _maxPos;
	}

	PackedColor GetFrameColor() const {return _frameColor;}
	void SetFrameColor(PackedColor color) {_frameColor = color;}
	PackedColor GetBarColor() const {return _barColor;}
	void SetBarColor(PackedColor color) {_barColor = color;}
};

struct CListBoxItem
{
	RString text;
	RString data;
	int value;
	PackedColor ftColor;
	PackedColor selColor;
	Ref<Texture> texture;
};

class CListBoxContainer : public AutoArray<CListBoxItem>
{
protected:
	PackedColor _ftColor;
	PackedColor _selColor;
	int _selString;
	float _topString;
	bool _showSelected;
	bool _readOnly;

public:

	CListBoxContainer(const ParamEntry &cls);

	virtual int GetSize() {return Size();}
	int GetCurSel() {return _selString;};

	bool IsReadOnly() const {return _readOnly;}
	void SetReadOnly(bool set = true) {_readOnly = set;}

	PackedColor GetSelColor() {return _selColor;}
	void SetSelColor(PackedColor color) {_selColor = color;}
	PackedColor GetFtColor() {return _ftColor;}
	void SetFtColor(PackedColor color) {_ftColor = color;}
	void ShowSelected(bool show = true) {_showSelected = show;}

	virtual RString GetText(int i)
	{
		if (i < 0 || i >= GetSize())
			return nullptr;
		return Get(i).text;
	}
	virtual RString GetData(int i)
	{
		if (i < 0 || i >= GetSize())
			return nullptr;
		return Get(i).data;
	}
	virtual int GetValue(int i)
	{
		if (i < 0 || i >= GetSize())
			return 0;
		return Get(i).value;
	}
	PackedColor GetFtColor(int i)
	{
		if (i < 0 || i >= GetSize())
			return PackedBlack;
		return Get(i).ftColor;
	}
	PackedColor GetSelColor(int i)
	{
		if (i < 0 || i >= GetSize())
			return PackedBlack;
		return Get(i).selColor;
	}
	Texture *GetTexture(int i)
	{
		if (i < 0 || i >= GetSize())
			return nullptr;
		return Get(i).texture;
	}
	virtual void ClearStrings()
	{
		Clear();
	}
	virtual void RemoveAll()
	{
		ClearStrings();
		_selString = -1;
		_topString = 0;
	}
	virtual int AddString(RString text)
	{
		int index = Add();
		Set(index).text = text;
		Set(index).data = "";
		Set(index).value = 0;
		Set(index).ftColor = _ftColor;
		Set(index).selColor = _selColor;
		return index;
	}
	void SetData(int i, RString data)
	{
		if (i < 0 || i >= GetSize()) return;
		Set(i).data = data;
	}
	void SetValue(int i, int value)
	{
		if (i < 0 || i >= GetSize()) return;
		Set(i).value = value;
	}
	void SetFtColor(int i, PackedColor color)
	{
		if (i < 0 || i >= GetSize()) return;
		Set(i).ftColor = color;
	}
	void SetSelColor(int i, PackedColor color)
	{
		if (i < 0 || i >= GetSize()) return;
		Set(i).selColor = color;
	}
	void SetTexture(int i, Texture *texture)
	{
		if (i < 0 || i >= GetSize()) return;
		Set(i).texture = texture;
	}
	void InsertString(int i, RString text);
	void DeleteString(int i);
	void SortItems();
	void SortItemsByValue();

	void OnMouseZChanged(float dz);
	virtual	float NRows() const = 0;

	void Check();
};

namespace Poseidon { struct Rect2DFloat; }
using Poseidon::Rect2DFloat;

class CListBox : public Control, public CListBoxContainer
{
protected:
	Ref<Font> _font;
	float _size;
	float _rowHeight;

	float _showStrings;

	bool _scrollUp;
	bool _scrollDown;
	Poseidon::Foundation::UITime _scrollTime;

	CScrollBar _scrollbar;
public:

	CListBox(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void SetPos(float x, float y, float w, float h) override;

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnLButtonDblClick(float x, float y) override;
	void OnDraw(float alpha) override;

	virtual void OnSelChanged(int curSel);
	bool OnKillFocus() override;	// return false if focus cannot be killed

	void SetCurSel(int sel, bool sendUpdate = true);
	void SetRowHeight(float height)
	{
		_rowHeight = floatMax(_size, height);
	}
	void DrawItem
	(
		float alpha, int i, bool selected, float top,
		Rect2DFloat &rect
//		float topClip, float bottomClip
	);
	float NRows() const override {return _showStrings;}
};

struct CTreeItem : public RemoveLLinks
{
friend class CTree;
protected:
	bool expanded;	// use CTree::Expand() to set

public:
	int level;
	LLink<CTreeItem> parent;
	RefArray<CTreeItem> children;

	RString text;
	RString data;
	int value;

	CTreeItem(int level, CTreeItem *parent);
	CTreeItem *AddChild();
	CTreeItem *InsertChild(int pos);

	void SortChildren(bool reversed = false);

	void ExpandTree(bool expand = true);
	CTreeItem *GetNextVisible();
	CTreeItem *GetPrevVisible();
	CTreeItem *GetLastVisible();
	CTreeItem *GetYBrother();
	CTreeItem *GetOBrother();
	int NVisibleItems();
};

class CTree : public Control
{
protected:
	Ref<CTreeItem> _root;
	LLink<CTreeItem> _selected;
	LLink<CTreeItem> _firstVisible;

	PackedColor _bgColor;
	PackedColor _ftColor;
	PackedColor _selColor;

	Ref<Font> _font;
	float _size;

	bool _scrollUp;
	bool _scrollDown;
	Poseidon::Foundation::UITime _scrollTime;
	float _offset;

public:

	CTree(ControlsContainer *parent, int idc, const ParamEntry &cls);

	CTreeItem *GetRoot() const {return _root;}
	CTreeItem *GetSelected() const {return _selected;}
	void SetSelected(CTreeItem *item);

	void Expand(CTreeItem *item, bool expand = true);
	void EnsureVisible(CTreeItem *item);

	void RemoveAll();

	void OnDraw(float alpha) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonDblClick(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;

	void DrawItem
	(
		float alpha, CTreeItem *item,
		float top, float topClip, float bottomClip, bool first
	);

protected:
	CTreeItem *FindItem(float x, float y);
	CTreeItem *FindFirstVisible();
};

class CToolBox : public Control
{
protected:
	AutoArray<RString> _strings;
	const ParamEntry *_textCls = nullptr;
	int _rows;
	int _columns;

	PackedColor _ftColor;
	PackedColor _bgColor;
	PackedColor _ftSelectColor;
	PackedColor _bgSelectColor;
	PackedColor _ftDisabledColor;
	PackedColor _bgDisabledColor;
	Ref<Font> _font;
	float _size;

	int _selected;

public:

	CToolBox(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;
	void OnDraw(float alpha) override;

	RString GetText(int i)
	{
		if (i < 0 || i >= _strings.Size())
			return nullptr;
		return _strings[i];
	}
	void SetText(int i, RString text)
	{
		if (i < 0 || i >= _strings.Size())
			return;
		_strings[i] = text;
	}
	void ReloadLocalizedText() override
	{
		if (const ParamEntry* saved = _textCls)
		{
			const ParamEntry& strings = *saved >> "strings";
			const int n = strings.GetSize();
			for (int i = 0; i < n && i < _strings.Size(); ++i)
			{
				_strings[i] = strings[i];
			}
		}
	}
	int GetSize() const {return _strings.Size();}
	int GetCurSel() {return _selected;}
	void SetCurSel(int sel)
	{
		if (sel < 0) sel = 0;
		if (sel >= _strings.Size()) sel = _strings.Size() - 1;
		_selected = sel;
	}

	PackedColor GetFtColor() {return _ftColor;}
	void SetFtColor(PackedColor color) {_ftColor = color;}

	virtual bool IsSelected(int i) const;
	virtual void ChangeSelection(int i);
};

class CCheckBoxes : public CToolBox
{
protected:

	PackedBoolArray _array;

public:

	CCheckBoxes(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;

	bool IsSelected(int i) const override;
	void ChangeSelection(int i) override;

	virtual PackedBoolArray GetArray() const {return _array;}
	void SetArray(PackedBoolArray array) {_array = array;}
};

class CCombo : public Control, public CListBoxContainer
{
protected:
	float _wholeHeight;
	PackedColor _bgColor;
	Ref<Font> _font;
	float _size;

	float _showStrings;

	bool _expanded;
	bool _scrollUp;
	bool _scrollDown;
	Poseidon::Foundation::UITime _scrollTime;

	int _mouseSel;
	CScrollBar _scrollbar;
public:

	CCombo(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool IsInside(float x, float y) override;
	void SetPos(float x, float y, float w, float h) override;

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	void OnDraw(float alpha) override;

	virtual void OnSelChanged(int curSel);
	bool OnKillFocus() override;	// return false if focus cannot be killed

	PackedColor GetSelColor() {return _selColor;}
	void SetSelColor(PackedColor color) {_selColor = color;}
	PackedColor GetFtColor() {return _ftColor;}
	void SetFtColor(PackedColor color) {_ftColor = color;}
	PackedColor GetBgColor() {return _bgColor;}
	void SetBgColor(PackedColor color) {_bgColor = color;}

	void SetCurSel(int sel, bool sendUpdate = true);
	void RemoveAll() override
	{
		CListBoxContainer::RemoveAll();
		_expanded = false;
	}
	void DeleteString(int i)
	{
		CListBoxContainer::DeleteString(i);
		if (GetSize() == 0) _expanded = false;
	}

	float NRows() const override {return _showStrings;}

protected:
	void SetTopString();
	bool IsInsideExt(float x, float y);
};

class Control3D : public Control
{
protected:
	Vector3 _position;
	Vector3 _right;
	Vector3 _down;

	float _angle;

	float _u;
	float _v;

public:

	Control3D(ControlsContainer *parent, int type, int idc, const ParamEntry &cls);

	void UpdateInfo(ControlObject *object, ControlInObject &info) override;
	bool IsInside(float x, float y) override;

	Vector3 GetCenter() {return _position + 0.5 * (_right + _down);}

	float GetU() const { return _debugU >= 0.0f ? _debugU : _u; }
	float GetV() const { return _debugV >= 0.0f ? _debugV : _v; }

	void DebugSetUV(float u, float v) { _debugU = u; _debugV = v; }
	void DebugClearUV()               { _debugU = -1.0f; _debugV = -1.0f; }

protected:
	float _debugU = -1.0f;
	float _debugV = -1.0f;
};

class C3DStatic : public Control3D
{
protected:
	RString _text;
	const ParamEntry *_textCls = nullptr;
	Ref<Texture> _texture;
	Ref<Font> _font;
	PackedColor _color;
	PackedColor _bgColor;
	float _hCoef;
	float _zBias;

	AutoArray<int> _lines;
	int _maxLines;

public:

	C3DStatic(ControlsContainer *parent, int idc, const ParamEntry &cls);

	RString GetText() {return _text;}
	void SetText(RString text);
	void ReloadLocalizedText() override
	{
		if (const ParamEntry* saved = _textCls)
		{
			SetText(RString(*saved >> "text"));
			_textCls = saved;
		}
	}
	void SetColor(PackedColor color) {_color = color;}
	PackedColor GetColor() const { return _color; }
	void SetBgColor(PackedColor color) {_bgColor = color;}
	PackedColor GetBgColor() const { return _bgColor; }

	void FormatText();
	int GetLineCount() const { return _lines.Size(); } // wrapped/explicit-break line count (ST_MULTI)
	RString GetLine(int i) const;

	void OnDraw(float alpha) override;

protected:
	void DrawText(const char *text, Vector3Par top, Vector3Par down, PackedColor color);
};

class C3DEdit : public Control3D, public CEditContainer
{
protected:
	int _maxLines;

public:

	C3DEdit(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnDraw(float alpha) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnIMEComposition(unsigned nChar, unsigned nFlags) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;

protected:
	bool IsMulti() const override;

	int FindPos(float x, float y);

	Vector3 PosToDir(RString text, int pos) const;
	float PosToX(RString text, int pos) const override;
	int XToPos(RString text, float x) const override;

	void EnsureVisible(int pos) override;
	void DrawText(const char *text, int offset, Vector3Par top, Vector3Par down, float alpha);

	void FormatText() override;
};

class C3DActiveText : public Control3D
{
protected:
	RString _text;
	const ParamEntry *_textCls = nullptr;
	Ref<Texture> _texture;
	Ref<Font> _font;
	PackedColor _color;
	PackedColor _colorActive;
	PackedColor _colorFocusBg;
	float _pressX = 0.0f;
	float _pressY = 0.0f;
	float _pressW = 0.0f;
	float _pressH = 0.0f;

	bool _active;

	bool _drawFocusLine = true;

	bool _returnDown = false;

	SoundPars _enterSound;
	SoundPars _pushSound;
	SoundPars _clickSound;
	SoundPars _escapeSound;

	RString _action;

public:

	C3DActiveText(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	bool OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnDraw(float alpha) override;

	RString GetText() {return _text;}
	void SetText(RString text);
	void ReloadLocalizedText() override
	{
		if (const ParamEntry* saved = _textCls)
		{
			SetText(RString(*saved >> "text"));
			_textCls = saved;
		}
	}

	RString GetAction() {return _action;}
	void SetAction(RString action) {_action = action;}
	void SetColor(PackedColor color) {_color = color;}
	PackedColor GetColor() const { return _color; }
	void SetActiveColor(PackedColor color) {_colorActive = color;}
	PackedColor GetActiveColor() const { return _colorActive; }

	bool IsActive() const {return _active;}

	bool CanBeDefault() const override {return true;}
};

class C3DSlider : public Control3D, public CSliderContainer
{
public:

	C3DSlider(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnDraw(float alpha) override;
};

class C3DScrollBar
{
	Vector3 _position;
	Vector3 _right;
	Vector3 _down;

	float _minPos;
	float _maxPos;
	float _curPos;
	float _page;
	float _lineStep;
	float _pageStep;

	float _thumbOffset;
	bool _thumbLocked;

	bool _enabled;

	PackedColor _color;

public:
	C3DScrollBar();

	void OnLButtonDown(float v);
	void OnLButtonUp();
	void OnMouseHold(float v);
	void OnDraw(float alpha);

	bool IsEnabled() const {return _enabled;}
	void Enable(bool enable = true) {_enabled = enable;}
	float GetPos() {return _curPos;}
	void SetPos(float pos)
	{
		if (pos <= _minPos)
			_curPos = _minPos;
		else if (pos >= _maxPos)
			_curPos = _maxPos;
		else
			_curPos = pos;
	}
	void SetRange(float min, float max, float page)
	{
		_minPos = min;
		_maxPos = max;
		_page = page;
		saturateMax(_maxPos, _minPos + page);
		saturate(_curPos, _minPos, _maxPos - page);
	}
	void SetSpeed(float line, float page) {_lineStep = line; _pageStep = page;}
	void SetPosition(Vector3Par pos, Vector3Par right, Vector3Par down)
	{
		_position = pos;
		_right = right;
		_down = down;
	}
	void SetColor(PackedColor color) {_color = color;}
	bool IsLocked() const {return _thumbLocked;}
};

class C3DScrollBarStandalone : public Control3D
{
protected:
	C3DScrollBar _scrollbar;

public:
	C3DScrollBarStandalone(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnDraw(float alpha) override;

	void SetScrollRange(float minPos, float maxPos, float page) { _scrollbar.SetRange(minPos, maxPos, page); }
	void SetScrollPos(float pos) { _scrollbar.SetPos(pos); }
	float GetScrollPos() { return _scrollbar.GetPos(); }
	void SetScrollSpeed(float line, float page) { _scrollbar.SetSpeed(line, page); }
	void SetColor(PackedColor color) { _scrollbar.SetColor(color); }
	bool IsScrollLocked() const { return _scrollbar.IsLocked(); }
};

// Shared column-table row painter for the 3D notebook listboxes (CSessions,
// CModsList). C3DListBox::BeginRow() computes the per-row clip + selection
// background and the column cursor for one text line; DrawColumn paints one
// cell (clipped to a width fraction of the list) and advances. Both DrawItem
// overrides build their columns from this instead of duplicating the 3D maths.
struct C3DTableRow
{
	Vector3 pos;        // left edge of the next column on this line
	Vector3 up;         // text up vector
	Vector3 right;      // text width basis (full list width)
	Vector3 dir;        // unit right
	float invRightSize;
	float rightSBSize;  // list width minus the scrollbar
	float border;
	float y1ct;         // vertical clip for a partially-scrolled row
	float y2ct;
	float y1c;          // full-row clip extents (for column dividers)
	float y2c;
	float top;          // this line's vertical offset on the row
	Vector3 down;       // row down vector (for column dividers)
	Font *font;
	PackedColor color;  // base text colour (selected or normal) for this row

	void DrawColumn(float widthFraction, RString text, PackedColor textColor, bool divider = false);
	void DrawColumn(float widthFraction, RString text) { DrawColumn(widthFraction, text, color, false); }
	void Advance(float widthFraction) { pos += widthFraction * rightSBSize * dir; }
};

class C3DListBox : public Control3D, public CListBoxContainer
{
protected:
	PackedColor _selBgColor;
	Ref<Font> _font;
	float _size;
	float _rows;

	bool _colorPicture;
	bool _dragging;
	bool _scrollUp;
	bool _scrollDown;
	Poseidon::Foundation::UITime _scrollTime;

	C3DScrollBar _scrollbar;

	float _sb3DWidth;

	// Optional row-filtering layer (shared by CSessions + CModsList). When a
	// filter is active, _visible holds the actual row indices that pass it, in
	// display order; otherwise rows show 1:1 and VisibleRow is the identity.
	Poseidon::Foundation::AutoArray<int> _visible;
	bool _filterActive = false;

public:

	C3DListBox(ControlsContainer *parent, int idc, const ParamEntry &cls);

	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	void OnMouseMove(float x, float y, bool active = true) override;
	void OnMouseHold(float x, float y, bool active = true) override;
	void OnMouseZChanged(float dz) override;
	void OnLButtonDown(float x, float y) override;
	void OnLButtonUp(float x, float y) override;
	void OnLButtonDblClick(float x, float y) override;
	void OnDraw(float alpha) override;

	virtual void OnSelChanged(int curSel) {}

	void SetCurSel(int sel, bool sendUpdate = true);
	void SetCurSel(float x, float y, bool sendUpdate = true);

	virtual void DrawItem
	(
		Vector3Par position, Vector3Par down, int i, float alpha
	);

	void UpdateInfo(ControlObject *object, ControlInObject &info) override;

	float NRows() const override {return _rows;}

	void SetColorPicture(bool set) {_colorPicture = set;}

	// Build the paint cursor for table row i's text line at vertical offset top
	// (height size). Draws the selection background only when drawSelection (pass
	// false for a 2nd line so it isn't drawn twice). See C3DTableRow.
	C3DTableRow BeginRow(Vector3Par position, Vector3Par down, int i, float alpha, float top, float size,
	                     bool drawSelection);

	// Reorder the subclass's rows via sortFn, keeping the cursor on the same
	// row (matched by GetData identity). Shared by CSessions / CModsList Sort.
	template <class SortFn> void SortPreservingSelection(SortFn sortFn)
	{
		RString selData;
		int sel = GetCurSel();
		if (sel >= 0 && sel < GetSize())
			selData = GetData(sel);
		sortFn();
		sel = 0;
		for (int i = 0; i < GetSize(); i++)
		{
			if (GetData(i) == selData)
			{
				sel = i;
				break;
			}
		}
		SetCurSel(sel);
	}

	// Row-filtering hooks. A subclass that filters overrides FilterRowCount()
	// (its total row count) + FilterRowVisible(actualRow), routes its
	// GetSize/GetData/GetText/DrawItem through VisibleCount()/VisibleRow(), and
	// calls SetFilterActive()/RebuildVisibleRows() when its rows or filter change.
	// Defaults make the layer a no-op (every row visible, identity mapping).
	virtual int FilterRowCount() const { return 0; }
	virtual bool FilterRowVisible(int /*actualRow*/) const { return true; }
	void RebuildVisibleRows();
	void SetFilterActive(bool active);
	bool IsFilterActive() const { return _filterActive; }
	int VisibleCount() const { return _filterActive ? _visible.Size() : FilterRowCount(); }
	int VisibleRow(int displayIdx) const;
};

// Shared sort-direction caret for clickable column headers (MP server browser
// + MODS catalog). Each sortable column owns a C3DStatic picture control beside
// its label; this shows the up/down arrow texture on the active sort column (in
// the sort direction) and hides it on the others. icon may be null / not a
// C3DStatic (then a no-op). Both screens drive their header carets through this.
void UpdateSortCaret(IControl *icon, bool active, bool ascending, RString upTex, RString downTex);

class C3DHTML : public Control3D, public CHTMLContainer
{
protected:
	float _width;
	float _height;
	float _invWidth;
	float _invHeight;

public:

	C3DHTML(ControlsContainer *parent, int idc, const ParamEntry &cls);

	void OnDraw(float alpha) override;
	void OnLButtonDown(float x, float y) override;
	void OnMouseMove(float x, float y, bool active = true) override;

	float GetPageWidth() const override {return _width;}
	float GetPageHeight() const override {return _height;}
	float GetTextWidth(float size, Font *font, const char *text) const override;

	void UpdateInfo(ControlObject *object, ControlInObject &info) override;

protected:
	int FindField(float x, float y) override;
};

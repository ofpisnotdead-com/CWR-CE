
#include <Poseidon/UI/Controls/UIControlsExtShared.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#define SCROLL_SPEED 100.0
#define SCROLL_MIN 2.0
#define SCROLL_MAX 10.0

using namespace Poseidon;
CTreeItem::CTreeItem(int l, CTreeItem* p)
{
    level = l;
    parent = p;

    text = "";
    data = "";
    value = 0;

    expanded = false;
}

CTreeItem* CTreeItem::AddChild()
{
    CTreeItem* item = new CTreeItem(level + 1, this);
    children.Add(item);
    return item;
}

CTreeItem* CTreeItem::InsertChild(int pos)
{
    saturate(pos, 0, children.Size());
    CTreeItem* item = new CTreeItem(level + 1, this);
    children.Insert(pos, item);
    return item;
}

int CompChildren(const Ref<CTreeItem>* pitem1, const Ref<CTreeItem>* pitem2)
{
    CTreeItem* item1 = *pitem1;
    CTreeItem* item2 = *pitem2;
    return stricmp(item1->text, item2->text);
}

int CompChildrenRev(const Ref<CTreeItem>* pitem1, const Ref<CTreeItem>* pitem2)
{
    CTreeItem* item1 = *pitem1;
    CTreeItem* item2 = *pitem2;
    return stricmp(item2->text, item1->text);
}

void CTreeItem::SortChildren(bool reversed)
{
    if (reversed)
    {
        QSort(children.Data(), children.Size(), CompChildrenRev);
    }
    else
    {
        QSort(children.Data(), children.Size(), CompChildren);
    }
}

void CTreeItem::ExpandTree(bool expand)
{
    expanded = expand;

    int n = children.Size();
    for (int i = 0; i < n; i++)
    {
        children[i]->ExpandTree(expand);
    }
}

CTreeItem* CTreeItem::GetOBrother()
{
    if (!parent)
    {
        return nullptr;
    }
    int n = parent->children.Size();
    for (int i = 0; i < n - 1; i++)
    {
        CTreeItem* item = parent->children[i];
        if (item == this)
        {
            return parent->children[i + 1];
        }
    }
    return nullptr;
}

CTreeItem* CTreeItem::GetYBrother()
{
    if (!parent)
    {
        return nullptr;
    }
    int n = parent->children.Size();
    CTreeItem* item = parent->children[0];
    if (item == this)
    {
        return nullptr;
    }
    for (int i = 1; i < n; i++)
    {
        item = parent->children[i];
        if (item == this)
        {
            return parent->children[i - 1];
        }
    }
    return nullptr;
}

CTreeItem* CTreeItem::GetNextVisible()
{
    if (children.Size() > 0 && expanded)
    {
        return children[0];
    }

    CTreeItem* item = this;
    while (item->parent)
    {
        CTreeItem* brother = item->GetOBrother();
        if (brother)
        {
            return brother;
        }
        item = item->parent;
    }
    return nullptr;
}

CTreeItem* CTreeItem::GetLastVisible()
{
    if (expanded)
    {
        int n = children.Size();
        if (n > 0)
        {
            return children[n - 1]->GetLastVisible();
        }
    }
    return this;
}

CTreeItem* CTreeItem::GetPrevVisible()
{
    if (!parent)
    {
        return nullptr;
    }
    CTreeItem* brother = GetYBrother();
    if (brother)
    {
        return brother->GetLastVisible();
    }
    return parent;
}

int CTreeItem::NVisibleItems()
{
    int n = 0;
    CTreeItem* item = this;
    while (item)
    {
        n++;
        item = item->GetNextVisible();
    }
    return n;
}

CTree::CTree(ControlsContainer* parent, int idc, const ParamEntry& cls) : Control(parent, CT_TREE, idc, cls)
{
    _bgColor = GetPackedColor(cls >> "colorBackground");
    _ftColor = GetPackedColor(cls >> "colorText");
    _selColor = GetPackedColor(cls >> "colorSelect");
    _font = GLOB_ENGINE->LoadFont(GetFontID(cls >> "font"));
    const ParamEntry* entry = cls.FindEntry("size");
    if (entry)
    {
        _size = (float)(*entry) * _font->Height();
    }
    else
    {
        _size = cls >> "sizeEx";
    }

    CTreeItem* item = new CTreeItem(0, nullptr);
    _root = item;
    if ((_style & TR_SHOWROOT) == 0)
    {
        _root->expanded = true;
    }
    _firstVisible = FindFirstVisible();
    _selected = _firstVisible;

    _scrollUp = false;
    _scrollDown = false;
    _scrollTime = Glob.uiTime;
    _offset = 0;
}

void CTree::SetSelected(CTreeItem* item)
{
    if ((_style & TR_SHOWROOT) == 0 && item == _root)
    {
        return;
    }

    _selected = item;
    _parent->OnTreeSelChanged(IDC());

    EnsureVisible(_selected);
}

void CTree::Expand(CTreeItem* item, bool expand)
{
    item->expanded = expand;

    if (expand && item == _selected && (_style & TR_AUTOCOLLAPSE))
    {
        CTreeItem* except = _selected;
        CTreeItem* parent = _selected->parent;
        while (parent)
        {
            int n = parent->children.Size();
            for (int i = 0; i < n; i++)
            {
                if (parent->children[i] != except)
                {
                    parent->children[i]->ExpandTree(false);
                }
            }

            except = parent;
            parent = except->parent;
        }
        EnsureVisible(_selected);
    }
}

void CTree::EnsureVisible(CTreeItem* item)
{
    while (_firstVisible && _firstVisible->parent && !_firstVisible->parent->expanded)
    {
        _firstVisible = _firstVisible->parent;
    }

    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return;
    }

    // special case
    if (item == _firstVisible)
    {
        _offset = 0;
        return;
    }

    int lineItem = -1;
    int lineFirst = -1;

    CTreeItem* p = firstVisible;
    int line = 0;
    while (p)
    {
        if (p == item)
        {
            lineItem = line;
            if (lineFirst >= 0)
            {
                goto EVOK;
            }
        }
        if (p == _firstVisible)
        {
            lineFirst = line;
            if (lineItem >= 0)
            {
                goto EVOK;
            }
        }
        p = p->GetNextVisible();
        line++;
    }
    return;

EVOK:
    if (lineItem < lineFirst)
    {
        _offset = 0;
        int n = lineFirst - lineItem;
        for (int i = 0; i < n; i++)
        {
            CTreeItem* it = _firstVisible->GetPrevVisible();
            if (!it)
            {
                break;
            }
            _firstVisible = it;
        }
    }
    else
    {
        int n = lineItem - lineFirst;

        const float border = 0.005;
        float textHeight = _size;
        float h = _h + _offset * textHeight - 2 * border;
        if (_offset > 0 || _firstVisible != firstVisible)
        {
            h -= textHeight;
        }
        int nItems = _firstVisible->NVisibleItems();
        if (nItems * textHeight > h)
        {
            h -= textHeight;
        }
        if ((n + 1) * textHeight <= h)
        {
            return;
        }

        _offset = 0;
        h = _h - 2 * border - textHeight;
        if (item->GetNextVisible())
        {
            h -= textHeight;
        }
        int nn = toIntCeil(((n + 1) * textHeight - h) / textHeight);
        for (int i = 0; i < nn; i++)
        {
            CTreeItem* it = _firstVisible->GetNextVisible();
            if (!it)
            {
                break;
            }
            _firstVisible = it;
        }
    }
}

void CTree::RemoveAll()
{
    _root->children.Clear();

    _root->text = "";
    _root->data = "";
    _root->value = 0;

    _root->expanded = (_style & TR_SHOWROOT) == 0;

    // CTreeItem *item = _root;
    _firstVisible = FindFirstVisible();
    _selected = _firstVisible;
}

void CTree::OnDraw(float alpha)
{
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    float xx = CX(_x);
    float yy = CY(_y);
    float ww = toInt((_w + _x) * w) - xx;
    float hh = toInt((_h + _y) * h) - yy;

    PackedColor bgColor = ModAlpha(_bgColor, alpha);
    PackedColor ftColor = ModAlpha(_ftColor, alpha);
    PackedColor selColor = ModAlpha(_selColor, alpha);

    // draw list
    Texture* texture = GLOB_SCENE->Preloaded(TextureWhite);
    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    GLOB_ENGINE->Draw2D(mip, bgColor, Rect2DPixel(xx, yy, ww, hh));
    DrawLeft(0, ftColor);
    DrawTop(0, ftColor);
    DrawBottom(0, ftColor);
    DrawRight(0, ftColor);

    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return;
    }
    CTreeItem* item = _firstVisible;

    const float border = 0.005;

    float top = _y;
    float textHeight = _size;
    if (_offset > 0 || item != firstVisible)
    {
        float y0 = CY(_y + textHeight);
        GEngine->DrawLine(Line2DPixel(xx, y0, xx + ww, y0), ftColor, ftColor);

        float x0 = CX(_x + 0.5 * _w);
        float x1 = CX(_x + 0.5 * _w - 0.3 * textHeight);
        float x2 = CX(_x + 0.5 * _w + 0.3 * textHeight);
        float y1 = CY(_y + border);
        float y2 = CY(_y + textHeight - border);
        GEngine->DrawLine(Line2DPixel(x0, y1, x1, y2), ftColor, ftColor);
        GEngine->DrawLine(Line2DPixel(x1, y2, x2, y2), ftColor, ftColor);
        GEngine->DrawLine(Line2DPixel(x2, y2, x0, y1), ftColor, ftColor);

        top += textHeight;
    }
    top += border;
    float topClip = top;
    top -= _offset * textHeight;

    float bottomClip = _y + _h - border;
    int nItems = item->NVisibleItems();
    if (top + nItems * textHeight > bottomClip)
    {
        float y0 = CY(_y + _h - textHeight);
        GEngine->DrawLine(Line2DPixel(xx, y0, xx + ww, y0), ftColor, ftColor);

        float x0 = CX(_x + 0.5 * _w);
        float x1 = CX(_x + 0.5 * _w - 0.3 * textHeight);
        float x2 = CX(_x + 0.5 * _w + 0.3 * textHeight);
        float y1 = CY(_y + _h - border);
        float y2 = CY(_y + _h - textHeight + border);
        GEngine->DrawLine(Line2DPixel(x0, y1, x1, y2), ftColor, ftColor);
        GEngine->DrawLine(Line2DPixel(x1, y2, x2, y2), ftColor, ftColor);
        GEngine->DrawLine(Line2DPixel(x2, y2, x0, y1), ftColor, ftColor);

        bottomClip -= textHeight;
    }

    while (item && top < bottomClip)
    {
        DrawItem(alpha, item, top, topClip, bottomClip, item == firstVisible);
        top += textHeight;
        item = item->GetNextVisible();
    }
}

void CTree::DrawItem(float alpha, CTreeItem* item, float top, float topClip, float bottomClip, bool first)
{
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    const float border = 0.005;

    Rect2DPixel clip(_x * w, topClip * h, _w * w, (bottomClip - topClip) * h);

    float textHeight = _size;
    float textWidth = 0.75 * textHeight;
    float left = _x + border + textWidth * item->level;

    PackedColor color = ModAlpha(_ftColor, alpha);
    float lineleft = left - textWidth;
    CTreeItem* cur = item;
    if (cur->parent)
    {
        if (cur->children.Size() == 0)
        {
            float x1 = CX(lineleft + 0.5 * textWidth);
            float x2 = CX(lineleft + textWidth + 0.5 * border);
            float y2 = CY(top + 0.5 * textHeight);
            if (!first)
            {
                float y1 = CY(top);
                GLOB_ENGINE->DrawLine(Line2DPixel(x1, y1, x1, y2), color, color, clip);
            }
            GLOB_ENGINE->DrawLine(Line2DPixel(x1, y2, x2, y2), color, color, clip);
            if (cur->GetOBrother())
            {
                float y3 = CY(top + textHeight);
                GLOB_ENGINE->DrawLine(Line2DPixel(x1, y2, x1, y3), color, color, clip);
            }
        }
        else
        {
            float x1 = CX(lineleft + 0.15 * textWidth);
            float x2 = CX(lineleft + 0.30 * textWidth);
            float x3 = CX(lineleft + 0.50 * textWidth);
            float x4 = CX(lineleft + 0.70 * textWidth);
            float x5 = CX(lineleft + 0.85 * textWidth);
            float x6 = CX(lineleft + 1.00 * textWidth + 0.5 * border);
            float y1 = CY(top + 0.15 * textHeight);
            float y3 = CY(top + 0.50 * textHeight);
            float y5 = CY(top + 0.85 * textHeight);
            GEngine->DrawLine(Line2DPixel(x1, y1, x1, y5), color, color, clip);
            GEngine->DrawLine(Line2DPixel(x1, y5, x5 + 1, y5), color, color, clip);
            GEngine->DrawLine(Line2DPixel(x5, y5, x5, y1), color, color, clip);
            GEngine->DrawLine(Line2DPixel(x5, y1, x1, y1), color, color, clip);
            GEngine->DrawLine(Line2DPixel(x2, y3, x4 + 1, y3), color, color, clip);
            if (!cur->expanded)
            {
                float y2 = CY(top + 0.3 * textHeight);
                float y4 = CY(top + 0.7 * textHeight);
                GEngine->DrawLine(Line2DPixel(x3, y2, x3, y4 + 1), color, color, clip);
            }
            if (!first)
            {
                float y0 = CY(top);
                GEngine->DrawLine(Line2DPixel(x3, y0, x3, y1), color, color, clip);
            }
            GEngine->DrawLine(Line2DPixel(x5, y3, x6, y3), color, color, clip);
            if (cur->GetOBrother())
            {
                float y6 = CY(top + textHeight);
                GEngine->DrawLine(Line2DPixel(x3, y5, x3, y6), color, color, clip);
            }
        }
        cur = cur->parent;
        lineleft -= textWidth;
        while (cur->parent)
        {
            if (cur->GetOBrother())
            {
                float x1 = CX(lineleft + 0.5 * textWidth);
                float y1 = CY(top);
                float y2 = CY(top + textHeight);
                GLOB_ENGINE->DrawLine(Line2DPixel(x1, y1, x1, y2), color, color, clip);
            }
            cur = cur->parent;
            lineleft -= textWidth;
        }
    }

    if (item == _selected)
    {
        float width = GEngine->GetTextWidth(_size, _font, item->text) + 2 * border;
        float height = floatMin(textHeight, bottomClip - topClip);

        float xx = toInt(left * w) + 0.5;
        float yy = toInt(top * h) + 0.5;
        float ww = toInt(width * w);
        float hh = toInt(height * h);

        color = ModAlpha(_selColor, alpha);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx, yy, xx + ww, yy), color, color, clip);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww, yy, xx + ww, yy + hh + 1), color, color, clip);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx + ww, yy + hh, xx, yy + hh), color, color, clip);
        GLOB_ENGINE->DrawLine(Line2DPixel(xx, yy + hh, xx, yy), color, color, clip);
    }
    GLOB_ENGINE->DrawText(Point2DFloat(left + border, top), _size, Rect2DFloat(_x, topClip, _w, bottomClip - topClip),
                          _font, color, item->text);
}

bool CTree::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return false;
    }
    if (!_selected)
    {
        _selected = _firstVisible;
    }

    switch (nChar)
    {
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            Expand(_selected, true);
            return true;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            Expand(_selected, false);
            return true;
        case SDLK_UP:
            if (_selected != firstVisible)
            {
                CTreeItem* item = _selected->GetPrevVisible();
                PoseidonAssert(item);
                SetSelected(item);
            }
            return true;
        case SDLK_DOWN:
        {
            CTreeItem* item = _selected->GetNextVisible();
            if (item)
            {
                SetSelected(item);
            }
        }
            return true;
        case SDLK_PAGEUP:
        {
            CTreeItem* item = _selected;
            for (int i = 0; i < 10; i++)
            {
                if (item == firstVisible)
                {
                    break;
                }
                CTreeItem* it = item->GetPrevVisible();
                PoseidonAssert(it);
                item = it;
            }
            SetSelected(item);
        }
            return true;
        case SDLK_PAGEDOWN:
        {
            CTreeItem* item = _selected;
            for (int i = 0; i < 10; i++)
            {
                CTreeItem* it = item->GetNextVisible();
                if (!it)
                {
                    break;
                }
                item = it;
            }
            SetSelected(item);
        }
            return true;
        case SDLK_LEFT:
            if (_selected->expanded)
            {
                Expand(_selected, false);
            }
            else
            {
                CTreeItem* item = _selected->parent;
                if (_style & TR_SHOWROOT)
                {
                    if (item)
                    {
                        SetSelected(item);
                    }
                }
                else
                {
                    if (item && item != _root)
                    {
                        SetSelected(item);
                    }
                }
            }
            return true;
        case SDLK_RIGHT:
            if (!_selected->expanded)
            {
                Expand(_selected, true);
            }
            else
            {
                if (_selected->children.Size() > 0)
                {
                    SetSelected(_selected->children[0]);
                }
            }
            return true;
        default:
            return false;
    }
}

bool CTree::OnChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return false;
    }
    if (!_selected)
    {
        _selected = _firstVisible;
    }

    switch (nChar)
    {
        case '+':
            Expand(_selected, true);
            return true;
        case '-':
            Expand(_selected, false);
            return true;
        default:
            return false;
    }
}

void CTree::OnLButtonDown(float x, float y)
{
    CTreeItem* item = FindItem(x, y);
    if (!item)
    {
        return;
    }

    const float border = 0.005;
    float textHeight = _size;
    float textWidth = 0.75 * textHeight;
    float left = _x + border + textWidth * item->level;

    if (x < left - textWidth)
    {
        return;
    }
    SetSelected(item);
    if (x < left)
    {
        Expand(item, !item->expanded);
    }
}

void CTree::OnLButtonDblClick(float x, float y)
{
    /*
        CTreeItem *item = FindItem(x, y);
        if (!item) return;

        const float border = 0.005;
        float textHeight = _size;
        float textWidth = 0.75 * textHeight;
        float left = _x + border + textWidth * item->level;

        if (x < left) return;
        Expand(item, !item->expanded);
    */
    _parent->OnTreeDblClick(IDC(), GetSelected());
}

void CTree::OnMouseMove(float x, float y, bool active)
{
    if (!InputSubsystem::Instance().IsMouseLeftDown())
    {
        return;
    }

    OnMouseHold(x, y, active);
}

void CTree::OnMouseHold(float x, float y, bool active)
{
    if (!InputSubsystem::Instance().IsMouseLeftDown())
    {
        return;
    }

    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return;
    }

    const float border = 0.005;

    bool canScrollUp = false;
    bool canScrollDown = false;

    float top = _y;
    float textHeight = _size;
    if (_offset > 0 || _firstVisible != firstVisible)
    {
        canScrollUp = true;
        top += textHeight;
    }
    top += border - _offset * textHeight;
    float bottomClip = _y + _h - border;
    int nItems = _firstVisible->NVisibleItems();
    if (top + nItems * textHeight > bottomClip)
    {
        canScrollDown = true;
    }

    float dy;
    if (canScrollUp && (dy = _y + textHeight - y) > 0)
    {
        _scrollDown = false;
        // scroll up
        if (_scrollUp)
        {
            float scroll = SCROLL_SPEED * dy;
            saturate(scroll, SCROLL_MIN, SCROLL_MAX);
            _offset -= scroll * (Glob.uiTime - _scrollTime);
            CTreeItem* item = _firstVisible;
            while (_offset < 0)
            {
                if (item == firstVisible)
                {
                    _offset = 0;
                    break;
                }
                item = item->GetPrevVisible();
                _offset++;
            }
            if (item == firstVisible)
            {
                _offset = 0;
            }
            _firstVisible = item;
            _scrollTime = Glob.uiTime;
        }
        else
        {
            _scrollUp = true;
            _scrollTime = Glob.uiTime;
        }
    }
    else if (canScrollDown && (dy = y - _y - _h + textHeight) > 0)
    {
        _scrollUp = false;
        // scroll down
        if (_scrollDown)
        {
            float scroll = SCROLL_SPEED * dy;
            saturate(scroll, SCROLL_MIN, SCROLL_MAX);
            _offset += scroll * (Glob.uiTime - _scrollTime);
            CTreeItem* item = _firstVisible;
            while (_offset > 1.0)
            {
                if (item->GetNextVisible() == nullptr)
                {
                    _offset = 0;
                    break;
                }
                item = item->GetNextVisible();
                _offset--;
            }
            if (item == firstVisible && _offset > 0)
            {
                item = item->GetNextVisible();
            }
            _firstVisible = item;
            _scrollTime = Glob.uiTime;
        }
        else
        {
            _scrollDown = true;
            _scrollTime = Glob.uiTime;
        }
    }
    else
    {
        _scrollUp = _scrollDown = false;
    }
}

void CTree::OnMouseZChanged(float dz)
{
    if (dz == 0)
    {
        return;
    }

    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return;
    }

    const float border = 0.005;

    bool canScrollUp = false;
    bool canScrollDown = false;

    float top = _y;
    float textHeight = _size;
    if (_offset > 0 || _firstVisible != firstVisible)
    {
        canScrollUp = true;
        top += textHeight;
    }
    top += border - _offset * textHeight;
    float bottomClip = _y + _h - border;
    int nItems = _firstVisible->NVisibleItems();
    if (top + nItems * textHeight > bottomClip)
    {
        canScrollDown = true;
    }

    if (canScrollUp && dz > 0)
    {
        // scroll up
        float scroll = -SCROLL_SPEED * dz;
        saturate(scroll, -SCROLL_MAX, -SCROLL_MIN);
        _offset += 0.1 * scroll;
        CTreeItem* item = _firstVisible;
        while (_offset < 0)
        {
            if (item == firstVisible)
            {
                _offset = 0;
                break;
            }
            item = item->GetPrevVisible();
            _offset++;
        }
        if (item == firstVisible)
        {
            _offset = 0;
        }
        _firstVisible = item;
    }
    else if (canScrollDown && dz < 0)
    {
        // scroll down
        float scroll = -SCROLL_SPEED * dz;
        saturate(scroll, SCROLL_MIN, SCROLL_MAX);
        _offset += 0.1 * scroll;
        CTreeItem* item = _firstVisible;
        while (_offset > 1.0)
        {
            if (item->GetNextVisible() == nullptr)
            {
                _offset = 0;
                break;
            }
            item = item->GetNextVisible();
            _offset--;
        }
        if (item == firstVisible && _offset > 0)
        {
            item = item->GetNextVisible();
        }
        _firstVisible = item;
    }
}

CTreeItem* CTree::FindItem(float x, float y)
{
    PoseidonAssert(IsInside(x, y));
    CTreeItem* firstVisible = FindFirstVisible();
    if (!_firstVisible)
    {
        _firstVisible = firstVisible;
    }
    if (!_firstVisible)
    {
        return nullptr;
    }
    CTreeItem* item = _firstVisible;

    const float border = 0.005;

    float top = _y;
    float textHeight = _size;
    if (_offset > 0 || item != firstVisible)
    {
        top += textHeight;
        if (y < top)
        {
            return nullptr;
        }
    }
    top += border - _offset * textHeight;

    float bottomClip = _y + _h - border;
    int nItems = item->NVisibleItems();
    if (top + nItems * textHeight > bottomClip)
    {
        bottomClip -= textHeight;
        if (y > bottomClip)
        {
            return nullptr;
        }
    }

    while (item && top < bottomClip)
    {
        top += textHeight;
        if (y < top)
        {
            return item;
        }
        item = item->GetNextVisible();
    }
    return nullptr;
}

CTreeItem* CTree::FindFirstVisible()
{
    if (_style & TR_SHOWROOT)
    {
        return _root;
    }
    else
    {
        return _root->GetNextVisible();
    }
}

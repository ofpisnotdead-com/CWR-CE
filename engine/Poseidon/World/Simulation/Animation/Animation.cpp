#include <Poseidon/Foundation/Strings/RString.hpp>

#include <Poseidon/Core/Application.hpp>

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/IO/Streams/SerializeBin.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Core/Global.hpp>

#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{

#ifdef _DEBUG
inline int offsetToInt(Offset o)
{
    return o.GetOffset();
}
#else
inline int offsetToInt(Offset o)
{
    return o;
}
#endif

__forceinline void CheckMinMaxIter(Vector3& min, Vector3& max, Vector3Par val)
{
#if _KNI
    // KNI optimization
    min = _mm_min_ps(min.GetM128(), val.GetM128());
    max = _mm_max_ps(max.GetM128(), val.GetM128());
#elif __ICL
    if (min[0] > val[0])
        min[0] = val[0];
    if (max[0] < val[0])
        max[0] = val[0];
    if (min[1] > val[1])
        min[1] = val[1];
    if (max[1] < val[1])
        max[1] = val[1];
    if (min[2] > val[2])
        min[2] = val[2];
    if (max[2] < val[2])
        max[2] = val[2];
#else
    if (min[0] > val[0])
    {
        min[0] = val[0];
    }
    else if (max[0] < val[0])
    {
        max[0] = val[0];
    }
    if (min[1] > val[1])
    {
        min[1] = val[1];
    }
    else if (max[1] < val[1])
    {
        max[1] = val[1];
    }
    if (min[2] > val[2])
    {
        min[2] = val[2];
    }
    else if (max[2] < val[2])
    {
        max[2] = val[2];
    }
#endif
}

void Selection::DoConstruct(const Selection& src)
{
    _sel.Init(src._sel);
    _weights.Init(src._weights);
}

Selection::Selection(const SelInfo* sel, int nSel)
{
    if (nSel > 0)
    {
        _sel.Init(nSel);
        // check if weight is singular
        bool singular = true;
        for (int i = 0; i < nSel; i++)
        {
            if (sel[i].weight < 254)
            {
                singular = false;
            }
        }
        if (singular)
        {
            for (int i = 0; i < nSel; i++)
            {
                _sel[i] = sel[i].index;
            }
        }
        else
        {
            _weights.Init(nSel);
            for (int i = 0; i < nSel; i++)
            {
                _sel[i] = sel[i].index;
                _weights[i] = sel[i].weight;
            }
        }
    }
}

FaceSelection::FaceSelection()
{
    _needsSections = false;
}

FaceSelection::~FaceSelection() = default;

void FaceSelection::SerializeBin(SerializeBinStream& f)
{
    f.TransferBinaryArray(*this);
    f.Transfer(_needsSections);
    f.TransferBinaryArray(_sections);
}

void FaceSelection::RescanSections(Shape* shape, const char* debugName)
{
    AUTO_STATIC_ARRAY(int, sections, 256);

    for (int s = 0; s < shape->NSections(); s++)
    {
        const ShapeSection& sec = shape->GetSection(s);
        // check what is sec relation to offsets
        bool some = false;
        bool all = true;
        for (Offset so = sec.beg; so < sec.end; shape->NextFace(so))
        {
            bool found = false;
            for (int oo = 0; oo < Size(); oo++)
            {
                if ((*this)[oo] == so)
                {
                    found = true;
                    break;
                }
            }
            if (found)
            {
                some = true; // face is there
            }
            else
            {
                all = false; // face is not there
            }
        }
        if (some != all)
        {
            LOG_DEBUG(Physics, "Section {} - {} ({}..{}) partially contained in {}", s,
                      (const char*)sec.properties.GetDebugText(), offsetToInt(sec.beg), offsetToInt(sec.end),
                      debugName);
        }
        if (all)
        {
            sections.Add(s);
        }
    }
    _sections.Init(sections.Data(), sections.Size());
}

Selection::Selection(const VertexIndex* sel, int nSel)
{
    if (nSel > 0)
    {
        _sel.Init(sel, nSel);
    }
}

void Selection::DoDestruct()
{
    _sel.Delete();
    _weights.Delete();
}

int Selection::Find(int index) const
{
    for (int i = 0; i < _sel.Size(); i++)
    {
        if (_sel[i] == index)
        {
            return i;
        }
    }
    return -1;
}

bool Selection::IsSelected(int index) const
{
    for (int i = 0; i < _sel.Size(); i++)
    {
        if (_sel[i] == index)
        {
            return true;
        }
    }
    return false;
}

bool Selection::IsSubset(const Selection& sel) const
{
    if (this == &sel)
    {
        return true; // easy solution
    }
    // check: sel is subset of this
    // (all points selected in sel  are also selected in this)
    int t = 0;
    int s = 0;
    // each member of sel must be in this

    while (s < sel.Size())
    {
        int si = sel[s];
        // check if si is it t
        for (;;)
        {
            if (t >= Size())
            {
                return false; // si is not contained - end of set
            }
            int ti = (*this)[t];
            if (si < ti)
            {
                return false; // si not contained - missing in set
            }
            t++;
            if (si == ti)
            {
                break; // si found - advance to next s
            }
        }
        s++;
    }
    return true;
}

NamedProperty::NamedProperty(const char* name, const char* value)
{
    RString nameLow = name;
    RString valueLow = value;
    nameLow.Lower();
    valueLow.Lower();
    _name = nameLow;
    _value = valueLow;
}

void Selection::Add(int vi, byte weigth)
{
    bool weighted = _weights.Size() > 0;

    Buffer<VertexIndex> temp(_sel.Size() + 1);
    Buffer<VertexIndex> tempW(_sel.Size() + 1);
    int i = 0;
    while (i < _sel.Size())
    {
        if (_sel[i] > vi)
        {
            break;
        }
        temp[i] = _sel[i];
        if (weighted)
        {
            tempW[i] = _weights[i];
        }
        i++;
    }
    temp[i] = vi;
    while (i < _sel.Size())
    {
        temp[i + 1] = _sel[i];
        if (weighted)
        {
            tempW[i + 1] = _weights[i];
        }
        i++;
    }
}

NamedSelection::NamedSelection(const char* name, const SelInfo* points, int nPoints, const VertexIndex* faces,
                               int nFaces)
    : Selection(points, nPoints), _faces(faces, nFaces), _faceSelReady(false)
{
    RString nameLow = name;
    nameLow.Lower();
    _name = nameLow;
}

void NamedSelection::DoConstruct(const NamedSelection& src)
{
    Selection::DoConstruct(src);
    _faces = src.Faces();
    _name = src._name;
    _faceSel = src._faceSel;
    _faceSelReady = src._faceSelReady;
}

void NamedSelection::DoConstruct()
{
    _faceSel = FaceSelection();
    _faceSelReady = false;
}

void NamedSelection::DoDestruct()
{
    Selection::Delete();
    _faces.Delete();
}

const FaceSelection& NamedSelection::FaceOffsets(Shape* shape) const
{
    if (!_faceSelReady)
    {
        _faceSel.Init(_faces.Size());
        int validFaces = 0;
        shape->BuildFaceIndexToOffset();
        // convert indices to offsets
        for (int i = 0; i < _faces.Size(); i++)
        {
            int faceIndex = _faces[i];
            if (faceIndex < 0 || faceIndex >= shape->NFaces())
            {
                LOG_WARN(Physics, "Skipping invalid face index {} in selection {} ({} faces)", faceIndex, Name(),
                         shape->NFaces());
                continue;
            }
            _faceSel[validFaces++] = shape->FaceIndexToOffset(faceIndex);
        }
        _faceSel.Resize(validFaces);
        _faceSelReady = true;
    }
    return _faceSel;
}

bool NamedSelection::FaceOffsetsReady() const
{
    return _faceSelReady;
}

int Shape::FindNamedSel(const char* name) const
{
    int i;
    for (i = 0; i < _sel.Size(); i++)
    {
        const NamedSelection& sel = _sel[i];
        if (!strcmpi(name, sel.Name()))
        {
            return i;
        }
    }
    return -1;
}

int Shape::FindNamedSel(const char* name, const char* altName) const
{
    int index = FindNamedSel(name);
    if (index >= 0 || !altName)
    {
        return index;
    }
    return FindNamedSel(altName);
}

void Shape::AddNamedSel(const NamedSelection& sel)
{
    _sel.Add(sel);
}

const Plane& ConvexComponent::GetPlane(int i) const
{
    _shape->InitPlanes();
    int index = _planes[i];
    return _shape->GetPlane(index);
}
const Poly& ConvexComponent::GetFace(int i) const
{
    // note: this function needs BuildFaceIndexToOffset
    // called on shape before using
    int index = _planes[i];
    Offset o = _shape->FaceIndexToOffset(index);
    return _shape->Face(o);
}

void ConvexComponent::Init(Shape* shape, const char* name)
{
    _shape = shape;
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    int index = shape->FindNamedSel(name);
    if (index < 0)
    {
        return;
    }
    _minMax[0] = Vector3(+1e10, +1e10, +1e10);
    _minMax[1] = Vector3(-1e10, -1e10, -1e10);
    NamedSelection::DoDestruct();
    NamedSelection::DoConstruct(shape->NamedSel(index));

    int size = Size();
    {
        int index = (*this)[0];

        Vector3Val p = _shape->Pos(index);
        _minMax[0] = p;
        _minMax[1] = p;

        for (int si = 1; si < size; si++)
        {
            int index = (*this)[si];
            CheckMinMaxIter(_minMax[0], _minMax[1], _shape->Pos(index));
        }
    }

    // find bounding center
    _center = (_minMax[0] + _minMax[1]) * 0.5;
    // find bounding sphere radius
    float maxRadius2 = 0;
    for (int si = 0; si < size; si++)
    {
        int index = (*this)[si];
        float radius2 = shape->Pos(index).Distance2(_center);
        saturateMax(maxRadius2, radius2);
    }
    _radius = sqrt(maxRadius2);
    // convert face representation to half space
    const Selection& faces = Faces();
    // check first face of the selection
    int faceIndex = faces[0];
    const Poly& poly = shape->FaceIndexed(faceIndex);
    // get texture of any face
    _texture = poly.GetTexture();
    for (int fi = 0; fi < faces.Size(); fi++)
    {
        _planes.Add(faces[fi]);
    }
}

void ConvexComponent::Recalculate()
{
    int size = Size();
    if (size > 0)
    {
        int index = (*this)[0];

        Vector3Val p = _shape->Pos(index);
        _minMax[0] = p;
        _minMax[1] = p;

        for (int si = 1; si < size; si++)
        {
            int index = (*this)[si];
            CheckMinMaxIter(_minMax[0], _minMax[1], _shape->Pos(index));
        }
        // find bounding center
        _center = (_minMax[0] + _minMax[1]) * 0.5;
        // find bounding sphere radius
        float maxRadius2 = 0;
        for (int si = 0; si < size; si++)
        {
            int index = (*this)[si];
            float radius2 = _shape->Pos(index).Distance2(_center);
            saturateMax(maxRadius2, radius2);
        }
        _radius = sqrt(maxRadius2);
    }
    else
    {
        _minMax[0] = VZero;
        _minMax[1] = VZero;
        _center = VZero;
        _radius = 0;
    }
}

bool ConvexComponent::IsInside(Vector3Val point) const
{
    PoseidonAssert(NPlanes() >= 4);
    for (int i = 0; i < NPlanes(); i++)
    {
        const Plane& plane = GetPlane(i);
        float dist = plane.Distance(point);
        if (dist < 0)
        {
            return false;
        }
    }
    return true;
}

int Shape::FindProperty(const char* name) const
{
#if 1
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", (const char*)name);
    strlwr(buf);
    if (strcmp(name, buf))
    {
        Fail("Upper case property find");
    }
#endif
    for (int i = 0; i < _prop.Size(); i++)
    {
        if (!strcmp(_prop[i].Name(), name))
        {
            return i;
        }
    }
    return -1;
}

void Shape::SetProperty(const char* name, const char* value)
{
    int index = FindProperty(name);
    if (index >= 0)
    {
        _prop[index] = NamedProperty(name, value);
        return;
    }
    _prop.Add(NamedProperty(name, value));
}

const RStringB& Shape::PropertyValue(const char* name) const
{
    int index = FindProperty(name);
    if (index < 0)
    {
        return Foundation::RStringBEmpty;
    }
    const NamedProperty& prop = NamedProp(index);
    return prop.Value();
}

Vector3 Shape::CalculateCenter(const Selection& sel) const
{
    // calculate geometrical center
    Vector3 sum(VZero);
    if (sel.Size() <= 0)
    {
        return sum;
    }
    for (int i = 0; i < sel.Size(); i++)
    {
        sum += Pos(sel[i]);
    }
    return sum * (1 / float(sel.Size()));
}

void Animation::DoConstruct()
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        _selection[level] = -1;
    }
}

Animation::Animation()
{
    DoConstruct();
}

void Animation::Init(LODShape* shape, const char* nameSel, const char* altName)
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        _selection[level] = -1;
    }
    for (int level = 0; level < shape->NLevels(); level++)
    {
        Shape* lShape = shape->Level(level);
        if (!lShape)
        {
            continue;
        }
        _selection[level] = lShape->FindNamedSel(nameSel, altName);
        if (_selection[level] < 0)
        {
            continue;
        }
    }
}

void Animation::Transform(LODShape* shape, const Matrix4& trans, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    lShape->SaveOriginalPos();
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    int i;
    for (i = 0; i < sel.Size(); i++)
    {
        int posI = sel[i];
        lShape->SetPos(posI).SetFastTransform(trans, lShape->OrigPos(posI));
        lShape->SetNorm(posI).SetRotate(trans, lShape->OrigNorm(posI));
    }
    lShape->InvalidateNormals();
    lShape->InvalidateBuffer();
}

void Animation::TransformOver(LODShape* shape, const Matrix4& trans, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    int i;
    for (i = 0; i < sel.Size(); i++)
    {
        int posI = sel[i];
        Vector3& pos = lShape->SetPos(posI);
        Vector3& norm = lShape->SetNorm(posI);
        pos.SetFastTransform(trans, pos);
        norm.SetRotate(trans, norm);
    }
    lShape->InvalidateNormals();
    lShape->InvalidateBuffer();
}

Vector3 Animation::Transform(LODShape* shape, const Matrix4& trans, int level, int index) const
{
    Shape* lShape = shape->Level(level);
    if (_selection[level] < 0)
    {
        return lShape->Pos(index);
    }
    PoseidonAssert(lShape);
    lShape->SaveOriginalPos();
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    if (sel.IsSelected(index))
    {
        return trans.FastTransform(lShape->OrigPos(index));
    }
    else
    {
        return lShape->OrigPos(index);
    }
}

Vector3 Animation::TransformWithWeight(LODShape* shape, const Matrix4& trans, int level, int index) const
{
    Fail("Not implemented");
    Shape* lShape = shape->Level(level);
    return lShape->Pos(index);
}

void Animation::TransformWithWeight(LODShape* shape, const Matrix4& trans, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    int i;
    for (i = 0; i < sel.Size(); i++)
    {
        int posI = sel[i];
        float w = sel.Weight(i) * (1.0 / 255);
        V3& sPos = lShape->SetPos(posI);
        if (w >= 0.99)
        {
            sPos.SetFastTransform(trans, sPos);
        }
        else
        {
            Matrix4 t = trans * w + MIdentity * (1 - w);
            sPos.SetFastTransform(t, sPos);
        }
    }
    lShape->InvalidateNormals();
    lShape->InvalidateBuffer();
}

bool Animation::IsEmpty() const
{
    for (int i = 0; i < MAX_LOD_LEVELS; i++)
    {
        if (_selection[i] >= 0)
        {
            return false;
        }
    }
    return true;
}

void Animation::Restore(LODShape* shape, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    if (!lShape->OriginalPosValid())
    {
        return;
    }
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    for (int i = 0; i < sel.Size(); i++)
    {
        int posI = sel[i];
        lShape->SetPos(posI) = lShape->OrigPos(posI);
    }
    lShape->InvalidateNormals();
    lShape->InvalidateBuffer();
}

void AnimationSection::DoConstruct() {}

AnimationSection::AnimationSection()
{
    DoConstruct();
}

#define LOG_SECTIONS 1
void AnimationSection::Register(LODShape* shape, const char* nameSel, const char* altName)
{
#if LOG_SECTIONS
    // check if sections are already registered
    bool needRegister = false;
    for (int level = 0; level < shape->NLevels(); level++)
    {
        Shape* lod = shape->Level(level);
        int si = GetSelection(level);
        if (si < 0)
        {
            continue;
        }
        NamedSelection& sel = lod->NamedSel(si);
        if (!sel.GetNeedsSections())
        {
            needRegister = true;
        }
    }

    if (needRegister)
    {
        // ParamFile sections;
        // sections.Parse();

        char shortName[256];
        GetFilename(shortName, shape->GetName());
        while (strpbrk(shortName, " -/()"))
        {
            *strpbrk(shortName, " -/()") = '_';
        }

        RptF("Selection missing in CfgModels");
        RptF("  class %s", shortName);
        RptF("    \"%s\",", nameSel);
        if (altName)
        {
            LOG_DEBUG(Physics, "\"{}\",", altName);
        }
    }
#endif
    for (int level = 0; level < shape->NLevels(); level++)
    {
        Shape* lod = shape->Level(level);
        int si = GetSelection(level);
        if (si < 0)
        {
            continue;
        }
        NamedSelection& sel = lod->NamedSel(si);
        sel.SetNeedsSections(true);
    }
}

void AnimationSection::Init(LODShape* shape, const char* nameSel, const char* altName)
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        _selection[level] = -1;
    }
    for (int level = 0; level < shape->NLevels(); level++)
    {
        Shape* lShape = shape->Level(level);
        if (!lShape)
        {
            continue;
        }
        _selection[level] = lShape->FindNamedSel(nameSel, altName);
    }

    Register(shape, nameSel, altName);
}

void AnimationSection::Deinit()
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        _selection[level] = -1;
    }
}

Texture* AnimationSection::GetTexture(LODShape* shape) const
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        if (_selection[level] < 0)
        {
            continue;
        }
        Shape* lShape = shape->Level(level);
        const NamedSelection& sel = lShape->NamedSel(_selection[level]);
        if (sel.Faces().Size() <= 0)
        {
            continue;
        }
        const Poly& face = lShape->Face(sel.FaceOffsets(lShape)[0]);
        return face.GetTexture();
    }
    return nullptr;
}

void AnimationSection::SetTexture(LODShape* shape, int level, Texture* texture) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    const FaceSelection& faces = sel.FaceOffsets(lShape);
    for (int i = 0; i < faces.Size(); i++)
    {
        Poly& face = lShape->Face(faces[i]);
        face.SetTexture(texture);
    }
    for (int i = 0; i < sel.NSections(); i++)
    {
        int sec = sel.GetSection(i);
        ShapeSection& ss = lShape->GetSection(sec);
        ss.properties.SetTexture(texture);
    }
}

void AnimationSection::Hide(LODShape* shape, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    const FaceSelection& faces = sel.FaceOffsets(lShape);
    for (int i = 0; i < faces.Size(); i++)
    {
        Poly& face = lShape->Face(faces[i]);
        face.OrSpecial(IsHidden);
    }
    for (int i = 0; i < sel.NSections(); i++)
    {
        int sec = sel.GetSection(i);
        ShapeSection& ss = lShape->GetSection(sec);
        ss.properties.OrSpecial(IsHidden);
    }
}

void AnimationSection::Unhide(LODShape* shape, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    const FaceSelection& faces = sel.FaceOffsets(lShape);
    for (int i = 0; i < faces.Size(); i++)
    {
        Poly& face = lShape->Face(faces[i]);
        face.AndSpecial(~IsHidden);
    }
    for (int i = 0; i < sel.NSections(); i++)
    {
        int sec = sel.GetSection(i);
        ShapeSection& ss = lShape->GetSection(sec);
        ss.properties.AndSpecial(~IsHidden);
    }
}

AnimationWithCenter::AnimationWithCenter() = default;

void AnimationWithCenter::Init(LODShape* shape, const char* name, const char* altName, const char* center,
                               const char* altCenter)
{
    Animation::Init(shape, name, altName);
    for (int i = 0; i < MAX_LOD_LEVELS; i++)
    {
        _centerSelection[i] = -1;
        _center[i] = VZero;
    }
    for (int i = 0; i < shape->NLevels(); i++)
    {
        Shape* lShape = shape->Level(i);
        _centerSelection[i] = -1;
        if (_selection[i] < 0)
        {
            continue;
        }
        if (center)
        {
            // explicit center
            int centerSel = lShape->FindNamedSel(center, altCenter);
            _centerSelection[i] = centerSel;
            if (centerSel >= 0)
            {
                const Selection& sel = lShape->NamedSel(centerSel);
                if (sel.Size() > 0)
                {
                    _center[i] = lShape->CalculateCenter(lShape->NamedSel(centerSel));
                }
            }
            else
            {
                Vector3Val mCenter = shape->MemoryPoint(center);
                if (mCenter.SquareSize() >= 0.1)
                {
                    _center[i] = mCenter;
                }
            }
        }
        else
        {
            const Selection& sel = lShape->NamedSel(_selection[i]);
            if (sel.Size() > 0)
            {
                _center[i] = lShape->CalculateCenter(sel);
            }
        }
    }
}

void AnimationWithCenter::Apply(LODShape* shape, const Matrix4& trans, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Matrix4 transCenter(Matrix4(MTranslation, _center[level]) * trans * Matrix4(MTranslation, -_center[level]));
    Transform(shape, transCenter, level);
}
void AnimationWithCenter::ApplyWithWeight(LODShape* shape, const Matrix4& trans, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Matrix4 transCenter(Matrix4(MTranslation, _center[level]) * trans * Matrix4(MTranslation, -_center[level]));
    TransformWithWeight(shape, transCenter, level);
}

AnimationRotation::AnimationRotation() = default;

const char* LevelName(float resolution);

void AnimationRotation::Init(LODShape* shape, const char* name, const char* altName, const char* axis,
                             const char* altAxis, bool inMemory)
{
    Animation::Init(shape, name, altName);
    for (int i = 0; i < shape->NLevels(); i++)
    {
        if (_selection[i] < 0)
        {
            continue;
        }
        PoseidonAssert(axis);
        Vector3 begP, endP;
        Shape* sShape = inMemory ? shape->MemoryLevel() : shape->Level(i);
        int index = sShape->FindNamedSel(axis, altAxis);
        // PoseidonAssert( index>=0 );
        if (index < 0)
        {
            RptF("Missing axis in model %s:%s, sel %s, axis %s", (const char*)shape->GetName(),
                 LevelName(shape->Resolution(i)), name, axis);
            _center[i] = VZero;
            _direction[i] = VUp;
            continue;
        }
        const NamedSelection& axisSel = sShape->NamedSel(index);
        if (axisSel.Size() < 2)
        {
            RptF("Axis has less than two points, in model %s:%s, sel %s", (const char*)shape->GetName(),
                 LevelName(shape->Resolution(i)), axis);
            if (axisSel.Size() < 1)
            {
                _center[i] = VZero;
                _direction[i] = VAside;
                RptF("  no point");
            }
            else
            {
                _center[i] = sShape->Pos(axisSel[0]);
                _direction[i] = VAside;
            }
        }
        else
        {
            Vector3 beg = sShape->Pos(axisSel[0]);
            Vector3 end = sShape->Pos(axisSel[1]);
            _center[i] = beg;
            Vector3 norm = (end - beg);
            _direction[i] = norm.Normalized();
        }
    }
}

void AnimationRotation::Init2(LODShape* shape, const char* name, const char* begin, const char* end, bool inMemory)
{
    Animation::Init(shape, name, nullptr);
    for (int i = 0; i < shape->NLevels(); i++)
    {
        if (_selection[i] < 0)
        {
            continue;
        }
        Shape* sShape = inMemory ? shape->MemoryLevel() : shape->Level(i);
        Vector3 begP = sShape->NamedPosition(begin);
        Vector3 endP = sShape->NamedPosition(end);

        _center[i] = begP;
        Vector3 norm = (endP - begP);
        _direction[i] = norm.Normalized();
    }
}

void AnimationRotation::Deinit() {}

void AnimationRotation::GetRotation(Matrix4& mat, float angle, int level) const
{
    Matrix4 align(MIdentity);
    Vector3Val dir = _direction[level];
    if (fabs(dir * VForward) > 0.9)
    {
        align.SetDirectionAndUp(dir, VUp);
    }
    else
    {
        align.SetDirectionAndUp(dir, VForward);
    }
    Matrix4 invAlign(MInverseRotation, align);
    mat = (Matrix4(MTranslation, _center[level]) * align * Matrix4(MRotationZ, angle) * invAlign *
           Matrix4(MTranslation, -_center[level]));
}

void AnimationRotation::Rotate(LODShape* shape, float angle, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Matrix4 transform;
    GetRotation(transform, angle, level);
    Transform(shape, transform, level);
}

void AnimationUV::DoConstruct() {}

void AnimationUV::Init(LODShape* shape, const char* nameSel)
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        _selection[level] = -1;
    }
    for (int level = 0; level < shape->NLevels(); level++)
    {
        Shape* lShape = shape->Level(level);
        if (!lShape)
        {
            continue;
        }
        _selection[level] = lShape->FindNamedSel(nameSel);
        if (_selection[level] < 0)
        {
            continue;
        }
        const FaceSelection& sel = lShape->NamedSel(_selection[level]).FaceOffsets(lShape);
        int m = 0;
        for (int i = 0; i < sel.Size(); i++)
        {
            const Poly& face = lShape->Face(sel[i]);
            m += face.N();
        }
        _origU[level].Realloc(m);
        _origV[level].Realloc(m);
        m = 0;
        for (int i = 0; i < sel.Size(); i++)
        {
            const Poly& face = lShape->Face(sel[i]);
            const Texture* tex = face.GetTexture();
            for (int j = 0; j < face.N(); j++)
            {
                int posI = face.GetVertex(j);
                if (tex)
                {
                    _origU[level][m] = tex->UToLogical(lShape->U(posI));
                    _origV[level][m] = tex->VToLogical(lShape->V(posI));
                }
                else
                {
                    _origU[level][m] = 0;
                    _origV[level][m] = 0;
                }
                m++;
            }
        }
    }
}

void AnimationUV::UVOffset(LODShape* shape, float offsetU, float offsetV, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const FaceSelection& sel = lShape->NamedSel(_selection[level]).FaceOffsets(lShape);
    int m = 0;
    for (int i = 0; i < sel.Size(); i++)
    {
        const Poly& face = lShape->Face(sel[i]);
        const Texture* tex = face.GetTexture();
        for (int j = 0; j < face.N(); j++)
        {
            int posI = face.GetVertex(j);
            if (tex)
            {
                lShape->SetU(posI, tex->UToPhysical(_origU[level][m] + offsetU));
                lShape->SetV(posI, tex->VToPhysical(_origV[level][m] + offsetV));
            }
            else
            {
                lShape->SetU(posI, 0);
                lShape->SetV(posI, 0);
            }
            m++;
        }
    }
}

void AnimationUV::Restore(LODShape* shape, int level) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const FaceSelection& sel = lShape->NamedSel(_selection[level]).FaceOffsets(lShape);
    int m = 0;
    for (int i = 0; i < sel.Size(); i++)
    {
        const Poly& face = lShape->Face(sel[i]);
        const Texture* tex = face.GetTexture();
        if (tex)
        {
            for (int j = 0; j < face.N(); j++)
            {
                int posI = face.GetVertex(j);
                lShape->SetU(posI, tex->UToPhysical(_origU[level][m]));
                lShape->SetV(posI, tex->VToPhysical(_origV[level][m]));
                m++;
            }
        }
    }
}

void AnimationAnimatedTexture::DoConstruct() {}

void AnimationAnimatedTexture::Init(LODShape* shape, const char* nameSel, const char* altNameSel)
{
    AnimationSection::Init(shape, nameSel, altNameSel);
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        int sel = _selection[level];
        if (sel < 0)
        {
            continue;
        }
        Shape* lShape = shape->Level(level);
        const NamedSelection& ns = lShape->NamedSel(sel);
        const FaceSelection& faces = ns.FaceOffsets(lShape);
        _animTexF[level].Realloc(faces.Size());
        _animTexS[level].Realloc(ns.NSections());
        for (int i = 0; i < faces.Size(); i++)
        {
            const Poly& face = lShape->Face(faces[i]);
            _animTexF[level][i] = face.GetTexture();
        }
        for (int i = 0; i < ns.NSections(); i++)
        {
            int si = ns.GetSection(i);
            const ShapeSection& sec = lShape->GetSection(si);
            _animTexS[level][i] = sec.properties.GetTexture();
        }
    }
}

void AnimationAnimatedTexture::Deinit()
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++)
    {
        _animTexF[level].Free();
        _animTexS[level].Free();
    }
    AnimationSection::Deinit();
}

void AnimationAnimatedTexture::SetPhase(LODShape* shape, int level, int phase) const
{
    PoseidonAssert(phase >= 0);
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    const FaceSelection& faces = sel.FaceOffsets(lShape);
    for (int i = 0; i < faces.Size(); i++)
    {
        Texture* texture = _animTexF[level][i];
        if (!texture || phase >= texture->AnimationLength())
        {
            continue;
        }
        Poly& face = lShape->Face(faces[i]);
        face.SetTexture(texture->GetAnimation(phase));
    }
    for (int i = 0; i < sel.NSections(); i++)
    {
        int si = sel.GetSection(i);
        ShapeSection& ss = lShape->GetSection(si);

        Texture* texture = _animTexS[level][i];
        if (!texture || phase >= texture->AnimationLength())
        {
            continue;
        }

        ss.properties.SetTexture(texture->GetAnimation(phase));
    }
}

void AnimationAnimatedTexture::AnimateTexture(LODShape* shape, int level, float anim) const
{
    if (_selection[level] < 0)
    {
        return;
    }
    Shape* lShape = shape->Level(level);
    PoseidonAssert(lShape);
    const NamedSelection& sel = lShape->NamedSel(_selection[level]);
    const FaceSelection& faces = sel.FaceOffsets(lShape);
    for (int i = 0; i < faces.Size(); i++)
    {
        Texture* texture = _animTexF[level][i];
        if (!texture)
        {
            continue;
        }
        int n = texture->AnimationLength();
        if (n <= 1)
        {
            continue;
        }
        int phase = toIntFloor(anim * n);
        Poly& face = lShape->Face(faces[i]);
        face.SetTexture(texture->GetAnimation(phase));
    }
    for (int i = 0; i < sel.NSections(); i++)
    {
        int si = sel.GetSection(i);
        ShapeSection& ss = lShape->GetSection(si);

        Texture* texture = _animTexS[level][i];
        if (!texture)
        {
            continue;
        }
        int n = texture->AnimationLength();
        if (n <= 1)
        {
            continue;
        }
        int phase = toIntFloor(anim * n);
        ss.properties.SetTexture(texture->GetAnimation(phase));
    }
}

AnimationType* AnimationType::CreateObject(const ParamEntry& cls, LODShape* shape)
{
    RString type = cls >> "type";
    if (stricmp(type, "rotation") == 0)
    {
        AnimationType* object = new AnimationRotationType();
        object->Init(cls, shape);
        return object;
    }

    Fail("Unknown animation type");
    return nullptr;
}

void AnimationType::Init(const ParamEntry& cls, LODShape* shape)
{
    _name = cls.GetName();
    float animPeriod = cls >> "animPeriod";
    _animSpeed = animPeriod > 0 ? 1.0f / animPeriod : 0;
}

void AnimationRotationType::Init(const ParamEntry& cls, LODShape* shape)
{
    base::Init(cls, shape);
    RString selection = cls >> "selection";
    bool memory = true;
    const ParamEntry* entry = cls.FindEntry("memory");
    if (entry)
    {
        memory = *entry;
    }
    entry = cls.FindEntry("axis");
    if (entry)
    {
        RString axis = *entry;
        _animation.Init(shape, selection, nullptr, axis, nullptr, memory);
    }
    else
    {
        RString begin = cls >> "begin";
        RString end = cls >> "end";
        _animation.Init2(shape, selection, begin, end, memory);
    }
    _angle0 = cls >> "angle0";
    _angle1 = cls >> "angle1";
}

int AnimationRotationType::GetSelection(int level) const
{
    return _animation.GetSelection(level);
}

void AnimationRotationType::Animate(LODShape* shape, int level, float phase, Matrix4Par baseAnim)
{
    saturate(phase, 0, 1);
    float angle = _angle0 + phase * (_angle1 - _angle0);
    Matrix4 rot;
    _animation.GetRotation(rot, angle, level);
    _animation.Transform(shape, baseAnim * rot, level);
}

void AnimationRotationType::Deanimate(LODShape* shape, int level)
{
    _animation.Restore(shape, level);
}

AnimationInstance::AnimationInstance()
{
    Init();
}

void AnimationInstance::Init()
{
    _phase = _phaseWanted = 0;
    _lastAnimation = Glob.time;
}

int AnimationInstance::GetSelection(int level) const
{
    return _type->GetSelection(level);
}

void AnimationInstance::Animate(LODShape* shape, int level, Matrix4Par baseAnim)
{
    AdvanceTime();
    _type->Animate(shape, level, _phase, baseAnim);
}

void AnimationInstance::Deanimate(LODShape* shape, int level)
{
    _type->Deanimate(shape, level);
}

void AnimationInstance::AdvanceTime()
{
    float deltaT = Glob.time - _lastAnimation;
    _lastAnimation = Glob.time;

    saturateMax(deltaT, 0);
    float offset = _phaseWanted - _phase;
    float maxOffset = deltaT * _type->GetAnimSpeed();
    saturate(offset, -maxOffset, maxOffset);
    _phase += offset;
}

} // namespace Poseidon

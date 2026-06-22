#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/IO/Serialization/SerializeBinExt.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <cmath>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

// Defined at global scope in World/Object code (unwrapped subsystems).
namespace Poseidon
{
Object* NewObject(Poseidon::Foundation::RString typeName, Poseidon::Foundation::RString shapeName);
}
Poseidon::Object* NewProxyObject(Poseidon::Foundation::RString shapeName);

namespace Poseidon
{

void NamedProperty::SerializeBin(SerializeBinStream& f)
{
    f.Transfer(_name);
    f.Transfer(_value);
}

void Selection::SerializeBin(SerializeBinStream& f)
{
    f.TransferBinaryArray(_sel);
    f.TransferBinaryArray(_weights);
}

void NamedSelection::SerializeBin(SerializeBinStream& f)
{
    f.Transfer(_name);
    _faces.SerializeBin(f);
#if _DEBUG
    for (int i = 0; i < _faces.Size(); i++)
    {
        int index = _faces[i];
        PoseidonAssert(index >= 0);
    }
#endif
    _faceSel.SerializeBin(f);
    Selection::SerializeBin(f);
}

void ProxyObject::SerializeBin(SerializeBinStream& f)
{
    f.Transfer(name);
    if (f.IsLoading())
    {
        // load object shape name
        RString shapeName;
        Matrix4 trans;

        f.Transfer(trans);

        // create corresponding object
        extern bool GReplaceProxies;

        RString modelName(name);
        if (GReplaceProxies)
        {
            // create object from shape name
            // shape name may be name of some vehicle class
            RString proxyType = RString("Proxy") + modelName;
            RString proxyShape = GetShapeName(modelName);
            obj = NewObject(proxyType, proxyShape);
        }
        else
        {
            obj = NewProxyObject(modelName);
        }

        if (obj)
        {
            LODShapeWithShadow* pshape = obj->GetShape();
            // add bounding center
            if (pshape)
            {
                trans.SetPosition(trans.FastTransform(pshape->BoundingCenter()));
            }

            obj->SetTransform(trans);
        }

        invTransform = trans.InverseScaled();
    }
    else
    {
        // save object name and transform
        Matrix4 trans = obj->Transform();
        LODShapeWithShadow* pshape = obj->GetShape();
        // sub bounding center
        trans.SetPosition(trans.FastTransform(-pshape->BoundingCenter()));
        f.Transfer(trans);
    }
    // color/state identical to parent object
    f.Transfer(id);
    f.Transfer(selection); // source selection index
}

void AnimationPhase::SerializeBin(SerializeBinStream& f)
{
    f.Transfer(_time);
    f.TransferBasicArray(_points);
}

/*
\patch_internal 1.31 Date 11/23/2001 by Ondra.
- Fixed: Shared proxy objects (like building interior equipment)
are now undestructable.
*/

void Shape::SerializeBin(SerializeBinStream& f)
{
    _loadWarning = false;
    // save all VertexTable members
    f.TransferBinaryArray(_clip);
    f.TransferBinaryArray(_tex);
    f.TransferBasicArray(_pos);
    f.TransferBasicArray(_norm);

    f.BIN_TRANSFER(_orHints);
    f.BIN_TRANSFER(_andHints);
    f.Transfer(_minMax[0]);
    f.Transfer(_minMax[1]);
    f.Transfer(_bCenter);
    f.Transfer(_bRadius);

    if (f.IsLoading())
    {
        int size = f.LoadInt();
        // texture count off the wire; each entry reads at least a 1-byte (NUL) name below,
        // so reject a count the remaining stream cannot back before a huge/negative Realloc.
        if (size < 0 || size > f.GetRest())
        {
            f.SetError(SerializeBinStream::EFileStructure);
            return;
        }
        _textures.Realloc(size);
        _textures.Resize(size);
        for (int i = 0; i < _textures.Size(); i++)
        {
            RString str;
            f.Transfer(str);
            if (str.GetLength() > 0)
            {
                _textures[i] = GlobLoadTexture(str);
            }
            else
            {
                _textures[i] = nullptr;
            }
        }
    }
    else
    {
        f.SaveInt(_textures.Size());
        for (int i = 0; i < _textures.Size(); i++)
        {
            Texture* txt = _textures[i];
            RString txtName = txt ? txt->GetName() : "";
            f.Transfer(txtName);
        }
    }

    // f.TransferBinaryArray(_areaOTex); // are associated with texture
    f.TransferBinaryArray(_pointToVertex); // are associated with texture
    f.TransferBinaryArray(_vertexToPoint); // are associated with texture

    int spec = 0;
    if (f.IsLoading())
    {
        int count = f.LoadInt();
        int size = f.LoadInt();
        // face count + raw buffer size off the wire: each face reads several bytes below,
        // and ReserveRaw(size) allocates size — reject values the stream cannot back.
        if (count < 0 || count > f.GetRest() || size < 0 || size > f.GetRest())
        {
            f.SetError(SerializeBinStream::EFileStructure);
            return;
        }
        // load data to temporary buffer
        _face.ReserveRaw(size);
        for (int i = 0; i < count; i++)
        {
            Poly poly;
            poly.SetSpecial(f.LoadInt());
            int ti = f.LoadShort();
            // index off the wire — was only checked >= 0, letting ti >= count read _textures OOB.
            Texture* texture = (ti >= 0 && ti < _textures.Size()) ? _textures[ti] : nullptr;
            poly.SetTexture(texture);
            int polyN = f.LoadChar();
            // vertex count off the wire: Set()/the area math index _vertex[MaxPoly], and
            // ItemSize() (Duplicate's memcpy size) overflows for a bad N — reject out of range.
            if (polyN < 0 || polyN > MaxPoly)
            {
                f.SetError(SerializeBinStream::EFileStructure);
                return;
            }
            poly.SetN(polyN);
            for (int i = 0; i < poly.N(); i++)
            {
                VertexIndex index = f.LoadShort();
                // vertex ref off the wire — reject one past the loaded vertex table before
                // it indexes _pos out of bounds in the area / normal math after load.
                if (index >= _pos.Size())
                {
                    f.SetError(SerializeBinStream::EFileStructure);
                    return;
                }
                poly.Set(i, index);
            }
            _face.Add(poly);
        }
    }
    else
    {
        // faces must be saved one by one
        // and texture pointers converted to texture indices
        f.SaveInt(_face.Size());
        f.SaveInt(_face.RawSize());
        for (Offset o = _face.Begin(); o < _face.End(); _face.Next(o))
        {
            const Poly& face = _face[o];

            f.SaveInt(face.Special());
            f.SaveShort(_textures.Find(face.GetTexture()));
            f.SaveChar(face.N());
            f.Save(face.GetVertexList(), face.N() * sizeof(VertexIndex));
        }
    }

    // we may need to convert texture coordinates to physical values
    if (GEngine && GEngine->TextBank() && GEngine->TextBank()->NeedUVConversion())
    {
        AUTO_STATIC_ARRAY(bool, converted, 2048);
#define DIAG_UV_CONFLICTS 1
        converted.Resize(NVertex());
        for (int i = 0; i < NVertex(); i++)
        {
            converted[i] = false;
        }
#if DIAG_UV_CONFLICTS
        AUTO_STATIC_ARRAY(UVPair, originalUV, 2048);
        originalUV.Resize(NVertex());
#endif
        // scan all faces, convert all non-converted vertices
        for (Offset o = _face.Begin(); o < _face.End(); _face.Next(o))
        {
            const Poly& face = _face[o];
            Texture* tex = face.GetTexture();
            if (!tex)
            {
                continue;
            }
            for (int vi = 0; vi < face.N(); vi++)
            {
                int v = face.GetVertex(vi);
                if (converted[v])
                {
#if DIAG_UV_CONFLICTS
                    // check if conversion would have the same result
                    const UVPair& uvo = originalUV[v];
                    const UVPair& uv = UV(v);
                    if (fabs(uv.u - tex->UToPhysical(uvo.u)) > 0.001 || fabs(uv.v - tex->VToPhysical(uvo.v)) > 0.001)
                    {
                        LOG_DEBUG(Graphics, "Warning - uv conflict ({})", tex->Name());
                        _loadWarning = true;
                    }
#endif
                    continue;
                }
                const UVPair& uv = UV(v);
#if DIAG_UV_CONFLICTS
                originalUV[v] = uv;
#endif
                SetUV(v, tex->UToPhysical(uv.u), tex->VToPhysical(uv.v));
                converted[v] = true;
            }
        }
    }

    // transfer shape sections
    // cannot use f.TranserArray, because we need to pass this
    if (f.IsLoading())
    {
        int size = f.LoadInt();
        // section count off the wire; each reads via SerializeBin below — reject a count the
        // remaining stream cannot back before a huge/negative Realloc.
        if (size < 0 || size > f.GetRest())
        {
            f.SetError(SerializeBinStream::EFileStructure);
            return;
        }
        _face._sections.Realloc(size);
        _face._sections.Resize(size);
        for (int i = 0; i < size; i++)
        {
            _face._sections[i].SerializeBin(f, this);
        }
        DoAssert(_face._sections.Size() == 0 || _face._sections[_face._sections.Size() - 1].end == EndFaces());
    }
    else
    {
        f.SaveInt(_face._sections.Size());
        for (int i = 0; i < _face._sections.Size(); i++)
        {
            _face._sections[i].SerializeBin(f, this);
        }
    }

    f.TransferArray(_sel);
#if _DEBUG
    if (f.IsLoading())
    {
        // verify is selections are OK
        // if not, repair them
        for (int i = 0; i < _sel.Size(); i++)
        {
            NamedSelection& sel = _sel[i];
            for (int i = 0; i < sel.Size(); i++)
            {
                int vi = sel[i];
                if (vi < 0 || vi >= NVertex())
                {
                    LOG_ERROR(Graphics, "{}: Bad vertex index {} (of {})", sel.Name(), vi, NVertex());
                }
            }
            Selection& fsel = sel.Faces();
            for (int i = 0; i < fsel.Size(); i++)
            {
                int fi = fsel[i];
                if (fi < 0 || fi >= NFaces())
                {
                    LOG_ERROR(Graphics, "{}: Bad face index {} (of {})", sel.Name(), fi, NFaces());
                }
            }
        }
    }
#endif
    f.TransferArray(_prop);
    f.TransferArray(_phase);

    f.BIN_TRANSFER(_colorTop);
    f.BIN_TRANSFER(_color);
    f.Transfer(_special);

    if (f.IsLoading())
    {
        _special |= spec;
        // reinit mutable/temporary members
        _faceNormalsValid = true;
        RecalculateAreas();
        StoreOriginalMinMax();
    }

    f.TransferRefArray(_proxy);

    if (f.IsLoading())
    {
        for (int i = 0; i < _proxy.Size(); i++)
        {
            _proxy[i]->obj->SetDestructType(DestructNo);
        }
    }
}

inline void ReverseVector(Vector3& v)
{
    v[0] = -v[0], v[2] = -v[2];
}
static void ReverseMinMax(Vector3* v)
{
    Vector3 min = v[0];
    Vector3 max = v[1];
    // swapped min becomes max
    v[1][0] = -min[0];
    v[1][2] = -min[2];
    v[0][0] = -max[0];
    v[0][2] = -max[2];
}

inline Vector3 ReverseV(const Vector3& v)
{
    return Vector3(-v[0], v[1], -v[2]);
}

void Shape::Reverse()
{
    // reverse all positions and normals
    // note: bounding center reversion is non-trivial
    // when using LODShape::Load, all data is reversed before substracting bc
    // reversing can be imagined as applying transformation T
    // any model coordinate position should be
    // T*X-bc
    // actualy it is
    //

    int n = NVertex();
    for (int i = 0; i < n; i++)
    {
        ReverseVector(_pos[i]);
        ReverseVector(_norm[i]);
    }

    ReverseMinMax(_minMaxOrig);
    ReverseVector(_bCenterOrig);

    ReverseMinMax(_minMax);
    ReverseVector(_bCenter);

    // reverse all proxy object matrices
    for (int i = 0; i < _proxy.Size(); i++)
    {
        ProxyObject* pobj = _proxy[i];
        Object* obj = pobj->obj;
        // reverse object transform matrix
        // reverse inverse matrix
        {
            FrameBase& trans = *obj;

            trans.SetPosition(ReverseV(trans.Position()));
            trans.SetDirection(ReverseV(trans.Direction()));
            trans.SetDirectionUp(ReverseV(trans.DirectionUp()));
            trans.SetDirectionAside(ReverseV(trans.DirectionAside()));
        }

        {
            // reverse inverse matrix
            Matrix4& trans = pobj->invTransform;

            trans.SetPosition(ReverseV(trans.Position()));
            trans.SetDirection(ReverseV(trans.Direction()));
            trans.SetDirectionUp(ReverseV(trans.DirectionUp()));
            trans.SetDirectionAside(ReverseV(trans.DirectionAside()));
        }
    }

    // reverse all animation data
    for (int i = 0; i < _phase.Size(); i++)
    {
        AnimationPhase& phase = _phase[i];
        for (int p = 0; p < phase.Size(); p++)
        {
            ReverseVector(phase[i]);
        }
    }
}

void LODShape::Reverse()
{
    // reverse all relevant vector data
    ReverseVector(_aimingCenter); // where should I lock/aim?

    for (int i = 0; i < NLevels(); i++)
    {
        _lods[i]->Reverse();
    }

    // note: min-max reve
    ReverseMinMax(_minMax);
    // the point with smallest bounding sphere
    // we aproximate it as (_min+_max)/2
    ReverseVector(_boundingCenter);
    ReverseVector(_geometryCenter);

    ReverseVector(_centerOfMass);
    // _invInertia needs to be recalculated
    CalculateMass();
}

const char* LevelName(float resolution);

/*
\patch_internal 1.53 Date 5/3/2002 by Ondra
- Optimized: Properties class and dammage are not stored lowercase.
Exact compare of RStringB or strcmp is therefore possible instead of strcmpi.
*/

void LODShape::SerializeBin(SerializeBinStream& f)
{
#ifdef _MSC_VER
    if (!f.Version('LODO'))
#else
    if (!f.Version(StrToInt("ODOL")))
#endif
    {
        f.SetError(f.EBadFileType);
        return;
    }
    if (!f.Version(7))
    {
        f.SetError(f.EBadVersion);
        return;
    }
    // save all shapes
    if (f.IsLoading())
    {
        _nLods = f.LoadInt();
        // _lods is a fixed MAX_LOD_LEVELS array; a count off the wire past it is an OOB write.
        if (_nLods < 0 || _nLods > MAX_LOD_LEVELS)
        {
            _nLods = 0;
            f.SetError(SerializeBinStream::EFileStructure);
            return;
        }
        for (int l = 0; l < NLevels(); l++)
        {
            _lods[l] = new Shape;
            _lods[l]->SerializeBin(f);
            _lods[l]->SetLevel(l);
        }
    }
    else
    {
        f.SaveInt(NLevels());
        for (int l = 0; l < NLevels(); l++)
        {
            _lods[l]->SerializeBin(f);
        }
    }
    f.TransferBinary(_resolutions, NLevels() * sizeof(*_resolutions));
    for (int l = 0; l < NLevels(); l++)
    {
        if (_lods[l]->_loadWarning)
        {
            LOG_DEBUG(Graphics, "{} ({}): warnings", Name(), LevelName(_resolutions[l]));
        }
    }

    // save common information

    f.Transfer(_special);
    f.Transfer(_boundingSphere);
    f.Transfer(_geometrySphere);

    f.Transfer(_remarks);
    f.BIN_TRANSFER(_andHints);
    f.BIN_TRANSFER(_orHints);

    f.Transfer(_aimingCenter);
    f.BIN_TRANSFER(_color);
    f.BIN_TRANSFER(_colorTop);
    f.Transfer(_viewDensity);
    f.Transfer(_minMax[0]);
    f.Transfer(_minMax[1]);

    f.Transfer(_boundingCenter);
    f.Transfer(_geometryCenter);

    f.Transfer(_centerOfMass);
    f.Transfer(_invInertia);

    f.Transfer(_autoCenter);
    f.Transfer(_lockAutoCenter);
    f.Transfer(_canOcclude);
    f.Transfer(_canBeOccluded);
    f.Transfer(_allowAnimation);
    f.BIN_TRANSFER(_mapType);

    f.TransferBinaryArray(_massArray);

    f.Transfer(_mass);
    f.Transfer(_invMass);

    f.Transfer(_armor);
    f.Transfer(_invArmor);
    if (f.IsLoading())
    {
        if (_armor > 1e-10)
        {
            _logArmor = log(_armor);
        }
        else
        {
            _logArmor = 25;
        }
    }

    f.Transfer(_memory);
    f.Transfer(_geometry);
    f.Transfer(_geometryFire);
    f.Transfer(_geometryView);
    f.Transfer(_geometryViewPilot);
    f.Transfer(_geometryViewGunner);
    f.Transfer(_geometryViewCommander);
    f.Transfer(_geometryViewCargo);
    f.Transfer(_landContact);
    f.Transfer(_roadway);
    f.Transfer(_paths);
    f.Transfer(_hitpoints);

    if (f.IsLoading())
    {
        OptimizeShapes(); // remove lods not needed
        CheckForcedProperties();
        InitConvexComponents();
        _propertyClass = PropertyValue("class");
        _propertyDammage = PropertyValue("dammage");
        // recalculate view density

        float alpha = _color.A8() * (1.0 / 255);
        float transparency = 1 - alpha * 1.5;

        if (transparency >= 0.99)
        {
            _viewDensity = 0;
        }
        if (transparency > 0.01)
        {
            _viewDensity = log(transparency) * 4;
        }
        else
        {
            _viewDensity = -10;
        }
    }
}

bool LODShape::LoadOptimized(QIStream& f)
{
    SerializeBinStream str(&f);
    SerializeBin(str);
    if (str.GetError() == str.EBadFileType)
    {
        return false;
    }
    // some version error
    switch (str.GetError())
    {
        case str.EOK:
            break;
        case str.EBadVersion:
            Fail("Bad version in LODShape::LoadOptimized");
            Poseidon::Foundation::ErrorMessage("Bad version in p3d file.");
            break;
        default:
            Fail("Error in LODShape::LoadOptimized");
            Poseidon::Foundation::ErrorMessage("Bad file format in p3d file.");
            break;
    }
    return true;
}
void LODShape::SaveOptimized(QOStream& f)
{
    SerializeBinStream str(&f);
    SerializeBin(str);
}

bool LODShape::LoadOptimized(const char* name)
{
    QIFStreamB f;
    f.AutoOpen(name);
    return LoadOptimized(f);
}

void LODShape::SaveOptimized(const char* name)
{
    QOFStream f;
    f.open(name);
    SaveOptimized(f);
    f.close();
}
} // namespace Poseidon

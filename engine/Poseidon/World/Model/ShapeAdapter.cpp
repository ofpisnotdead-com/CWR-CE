#include <Poseidon/World/Model/ShapeAdapter.hpp>
#include <Poseidon/World/Model/Model.hpp>

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Material.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <string.h>
#include <string>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
extern Object* NewObject(RString typeName, RString shapeName);
}
extern Object* NewProxyObject(RString shapeName);

namespace Poseidon
{
namespace Model
{
namespace ShapeAdapter
{

ProxyModelName normalizeProxyModelName(const std::string& selectionName)
{
    constexpr const char* proxyPrefix = "proxy:";
    constexpr size_t proxyPrefixLen = 6;

    std::string modelName = selectionName;
    if (modelName.rfind(proxyPrefix, 0) == 0)
    {
        modelName.erase(0, proxyPrefixLen);
    }

    int id = -1;
    size_t dot = modelName.find('.');
    if (dot != std::string::npos)
    {
        std::string idText = modelName.substr(dot + 1);
        modelName.erase(dot);
        id = atoi(idText.c_str());
    }

    return {modelName, id};
}

static int convertFaceFlagsToSpecial(FaceFlags flags)
{
    int spec = 0;
    uint32_t raw = static_cast<uint32_t>(flags);

    if (raw & static_cast<uint32_t>(FaceFlags::IsShadow))
    {
        spec |= 0x40;
    }
    if (raw & static_cast<uint32_t>(FaceFlags::NoShadow))
    {
        spec |= 0x20;
    }

    if (raw & static_cast<uint32_t>(FaceFlags::ZBiasMask))
    {
        int bias = (raw & static_cast<uint32_t>(FaceFlags::ZBiasMask)) / static_cast<uint32_t>(FaceFlags::ZBiasStep);
        spec |= bias * ZBiasStep;
    }

    // Texture clamping (values match between FaceFlags and old special flags)
    if (raw & 0x2000)
        spec |= 0x2000; // NoClamp
    if (raw & 0x4000)
        spec |= 0x4000; // ClampU
    if (raw & 0x8000)
        spec |= 0x8000; // ClampV

    // Texture merging
    if (raw & static_cast<uint32_t>(FaceFlags::DisableTexMerge))
    {
        spec |= 0x1000000;
    }

    return spec;
}

// vertex.flags always holds pre-encoded ClipFlags regardless of source format:
//   ODOL: SaveOptimized() pre-computes ClipFlags (materials 200-203, fog, land, decal);
//         ODOLLoader stores them directly in vertex.flags.
//   MLOD: MLODLoader converts raw POINT_* flags (data3d.h) to ClipFlags, encodes
//         materials into ClipUserMask, then stores in vertex.flags.
// Both paths are identical here — always cast vertex.flags to ClipFlags directly.
LODShapeWithShadow* convertToLODShape(const Poseidon::Model::Model& model, bool reversed)
{
    auto shape = new LODShapeWithShadow();

    // Must match the lowered name used in ShapeBank::New
    shape->_name = model.sourcePath.c_str();

    int lodCount = static_cast<int>(model.lodLevels.size());
    shape->_nLods = static_cast<signed char>(lodCount);

    for (int i = 0; i < lodCount; ++i)
    {
        const LODLevel& lodLevel = model.lodLevels[i];
        const Mesh& mesh = lodLevel.mesh;
        int vertexCount = static_cast<int>(mesh.vertices.size());
        int triangleCount = static_cast<int>(mesh.triangles.size());
        int quadCount = static_cast<int>(mesh.quads.size());
        bool isODOL = (model.sourceFormat == "ODOL");

        shape->_resolutions[i] = lodLevel.resolution;
        shape->_lods[i] = new Shape();
        shape->_lods[i]->Init(vertexCount);

        if (isODOL && !mesh.edges.mlodIndices.empty())
        {
            int nPoints = static_cast<int>(mesh.edges.mlodIndices.size());
            int nVerts = static_cast<int>(mesh.edges.vertexIndices.size());
            shape->_lods[i]->_pointToVertex.Resize(nPoints);
            for (int v = 0; v < nPoints; ++v)
                shape->_lods[i]->_pointToVertex[v] = static_cast<VertexIndex>(mesh.edges.mlodIndices[v]);
            shape->_lods[i]->_vertexToPoint.Resize(nVerts);
            for (int v = 0; v < nVerts; ++v)
                shape->_lods[i]->_vertexToPoint[v] = static_cast<VertexIndex>(mesh.edges.vertexIndices[v]);
        }
        else
        {
            shape->_lods[i]->_pointToVertex.Resize(vertexCount);
            shape->_lods[i]->_vertexToPoint.Resize(vertexCount);
            for (int v = 0; v < vertexCount; ++v)
            {
                shape->_lods[i]->_pointToVertex[v] = static_cast<VertexIndex>(v);
                shape->_lods[i]->_vertexToPoint[v] = static_cast<VertexIndex>(v);
            }
        }

        for (int v = 0; v < vertexCount; ++v)
        {
            const Vertex& vertex = mesh.vertices[v];
            shape->_lods[i]->SetPos(v) = ::Vector3(vertex.position.x, vertex.position.y, vertex.position.z);
            shape->_lods[i]->SetNorm(v) = ::Vector3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            shape->_lods[i]->SetU(v, vertex.uv.u);
            shape->_lods[i]->SetV(v, vertex.uv.v);

            ClipFlags c = static_cast<ClipFlags>(vertex.flags);
            // MLOD: clear ClipBack for sky/disable fog vertices (matches Shape::Load lines 729-733)
            if (!isODOL)
            {
                ClipFlags fog = c & ClipFogMask;
                if (fog == ClipFogSky || fog == ClipFogDisable)
                    c = static_cast<ClipFlags>(c & ~ClipBack);
            }
            shape->_lods[i]->SetClip(v, c);
        }

        if (isODOL && !mesh.frames.empty())
        {
            Shape* lod = shape->_lods[i];
            for (const auto& frame : mesh.frames)
            {
                if (frame.positions.empty())
                    continue;

                AnimationPhase phase;
                phase.Resize(vertexCount);

                const bool directVertexSpace = frame.positions.size() == static_cast<size_t>(vertexCount);
                for (int v = 0; v < vertexCount; ++v)
                {
                    int sourceIndex = v;
                    if (!directVertexSpace)
                    {
                        sourceIndex = (v < lod->_vertexToPoint.Size()) ? static_cast<int>(lod->_vertexToPoint[v]) : -1;
                    }

                    if (sourceIndex >= 0 && sourceIndex < static_cast<int>(frame.positions.size()))
                    {
                        const auto& pos = frame.positions[sourceIndex];
                        phase[v] = ::Vector3(pos.x, pos.y, pos.z);
                    }
                    else
                    {
                        phase[v] = lod->Pos(v);
                    }
                }

                phase.SetTime(frame.time);
                lod->AddPhase(phase);
            }
        }

        int nMaterials = static_cast<int>(mesh.materials.size());
        shape->_lods[i]->_textures.Realloc(nMaterials);
        shape->_lods[i]->_textures.Resize(nMaterials);
        shape->_lods[i]->_areaOTex.Resize(nMaterials);
        std::vector<Texture*> materialIndexToTexture(mesh.materials.size(), nullptr);

        for (size_t matIdx = 0; matIdx < mesh.materials.size(); ++matIdx)
        {
            const Material& mat = mesh.materials[matIdx];

            if (mat.texturePath.empty())
            {
                shape->_lods[i]->_textures[static_cast<int>(matIdx)] = nullptr;
            }
            else
            {
                std::string texPath = mat.texturePath;
                for (char& c : texPath)
                {
                    if (c >= 'A' && c <= 'Z')
                        c = c - 'A' + 'a';
                }
                Ref<Texture> texture = GlobLoadTexture(texPath.c_str());
                shape->_lods[i]->_textures[static_cast<int>(matIdx)] = texture;
                materialIndexToTexture[matIdx] = texture;
            }
        }

        struct FaceRef
        {
            bool isQuad;
            uint32_t index;
            uint32_t originalIndex;
        };

        std::vector<FaceRef> faceRefs;
        faceRefs.reserve(triangleCount + quadCount);

        for (int f = 0; f < triangleCount; ++f)
        {
            faceRefs.push_back({false, static_cast<uint32_t>(f), mesh.triangles[f].originalIndex});
        }
        for (int f = 0; f < quadCount; ++f)
        {
            faceRefs.push_back({true, static_cast<uint32_t>(f), mesh.quads[f].originalIndex});
        }

        // Sort by originalIndex to restore file order. Loaders split triangles and quads
        // into separate arrays; originalIndex tracks position in the original face stream.
        std::sort(faceRefs.begin(), faceRefs.end(),
                  [](const FaceRef& a, const FaceRef& b) { return a.originalIndex < b.originalIndex; });

        // Index needed for converting selection face indices (MLOD path below)
        std::vector<uint32_t> triangleToFace(triangleCount, 0xFFFFFFFF);
        std::vector<uint32_t> quadToFace(quadCount, 0xFFFFFFFF);
        for (size_t finalFaceIdx = 0; finalFaceIdx < faceRefs.size(); ++finalFaceIdx)
        {
            const FaceRef& ref = faceRefs[finalFaceIdx];
            if (ref.isQuad)
            {
                quadToFace[ref.index] = static_cast<uint32_t>(finalFaceIdx);
            }
            else
            {
                triangleToFace[ref.index] = static_cast<uint32_t>(finalFaceIdx);
            }
        }

        for (const auto& faceRef : faceRefs)
        {
            Poly poly;
            poly.Init();

            uint32_t materialIndex = 0;
            FaceFlags faceFlags = FaceFlags::None;

            if (faceRef.isQuad)
            {
                const Quad& quad = mesh.quads[faceRef.index];
                materialIndex = quad.materialIndex;
                faceFlags = quad.flags;
                poly.SetN(4);
                poly.Set(0, static_cast<VertexIndex>(quad.indices[0]));
                poly.Set(1, static_cast<VertexIndex>(quad.indices[1]));
                poly.Set(2, static_cast<VertexIndex>(quad.indices[2]));
                poly.Set(3, static_cast<VertexIndex>(quad.indices[3]));

                if (quad.materialIndex < materialIndexToTexture.size())
                {
                    poly.SetTexture(materialIndexToTexture[quad.materialIndex]);
                }
                else
                {
                    poly.SetTexture(nullptr);
                }

                poly.SetSpecial(isODOL ? static_cast<int>(quad.flags) : convertFaceFlagsToSpecial(quad.flags));
            }
            else
            {
                const Triangle& tri = mesh.triangles[faceRef.index];
                materialIndex = tri.materialIndex;
                faceFlags = tri.flags;
                poly.SetN(3);
                poly.Set(0, static_cast<VertexIndex>(tri.indices[0]));
                poly.Set(1, static_cast<VertexIndex>(tri.indices[1]));
                poly.Set(2, static_cast<VertexIndex>(tri.indices[2]));

                if (tri.materialIndex < materialIndexToTexture.size())
                {
                    poly.SetTexture(materialIndexToTexture[tri.materialIndex]);
                }
                else
                {
                    poly.SetTexture(nullptr);
                }

                poly.SetSpecial(isODOL ? static_cast<int>(tri.flags) : convertFaceFlagsToSpecial(tri.flags));
            }

            // MLOD: derive face specials from texture and vertex flags (Shape::Load lines 756-797)
            if (!isODOL)
            {
                Texture* tex = poly.GetTexture();
                if (tex)
                {
                    tex->LoadHeaders();
                    int spec = poly.Special();
                    if (tex->IsAlpha())
                        spec |= IsAlpha | IsAlphaOrdered;
                    if (tex->IsTransparent())
                        spec |= IsTransparent;
                    if (tex->IsAnimated())
                        spec |= ::IsAnimated;
                    const char* ext = strrchr(tex->Name(), '.');
                    if (ext && !stricmp(ext, ".paa") && !strstr(tex->Name(), "\\000"))
                        spec |= IsAlphaOrdered;
                    poly.SetSpecial(spec);
                }
                int nVerts = poly.N();
                for (int vi = 0; vi < nVerts; vi++)
                {
                    int vertIdx = poly.GetVertex(vi);
                    if (vertIdx >= 0 && vertIdx < vertexCount)
                    {
                        ClipFlags land = static_cast<ClipFlags>(mesh.vertices[vertIdx].flags) & ClipLandMask;
                        if (land == ClipLandOn)
                        {
                            poly.OrSpecial(OnSurface);
                            break;
                        }
                    }
                }
            }

            shape->_lods[i]->AddFace(poly);
        }

        for (const auto& selection : mesh.selections)
        {
            std::vector<SelInfo> selInfos;
            selInfos.reserve(selection.vertexIndices.size());
            bool hasWeights = !selection.vertexWeights.empty();
            for (size_t vi = 0; vi < selection.vertexIndices.size(); ++vi)
            {
                uint32_t vertIdx = selection.vertexIndices[vi];
                uint8_t weight =
                    (hasWeights && vi < selection.vertexWeights.size()) ? selection.vertexWeights[vi] : 255;
                selInfos.push_back(SelInfo(static_cast<VertexIndex>(vertIdx), weight));
            }

            // ODOL face indices are positions in the mixed tri/quad stream and map 1:1 to
            // faceRefs (already sorted by originalIndex). MLOD needs the triangleToFace remap.
            std::vector<VertexIndex> faceIndices;
            faceIndices.reserve(selection.triangleIndices.size());
            if (isODOL)
            {
                for (uint32_t combinedIdx : selection.triangleIndices)
                {
                    if (combinedIdx < faceRefs.size())
                        faceIndices.push_back(static_cast<VertexIndex>(combinedIdx));
                }
            }
            else
            {
                for (uint32_t triIdx : selection.triangleIndices)
                {
                    if (triIdx < triangleToFace.size())
                    {
                        uint32_t faceIdx = triangleToFace[triIdx];
                        if (faceIdx != 0xFFFFFFFF)
                            faceIndices.push_back(static_cast<VertexIndex>(faceIdx));
                    }
                }
            }

            ::NamedSelection namedSel(
                selection.name.c_str(), selInfos.empty() ? nullptr : selInfos.data(), static_cast<int>(selInfos.size()),
                faceIndices.empty() ? nullptr : faceIndices.data(), static_cast<int>(faceIndices.size()));

            shape->_lods[i]->AddNamedSel(namedSel);
        }

        for (const auto& prop : lodLevel.mesh.properties)
        {
            shape->_lods[i]->_prop.Add(::NamedProperty(prop.name.c_str(), prop.value.c_str()));
        }

        shape->_lods[i]->Compact();

        if (!isODOL)
        {
            shape->_lods[i]->CalculateHints();
            // AutoClamp must run before FindSections (Shape::Load order: line 846 then 4788)
            shape->_lods[i]->AutoClamp();
            shape->_lods[i]->RecalculateNormals(true);
            shape->_lods[i]->CalculateMinMax();
            shape->_lods[i]->StoreOriginalMinMax();
        }

        // ODOL section offsets are x86 byte offsets (sizeof(PolyProperties)==8).
        // On x64 (sizeof==16) we derive face counts from those offsets then recompute
        // native offsets by walking the actual face stream.
        shape->_lods[i]->Faces().SetSections(nullptr, 0);
        if (isODOL && !mesh.sections.empty())
        {
            int nSections = static_cast<int>(mesh.sections.size());
            int totalFaces = static_cast<int>(faceRefs.size());

            constexpr int kFilePolyPropsSize = 8; // sizeof(PolyProperties) on x86

            std::vector<int> secEndFile(nSections);
            for (int s = 0; s < nSections; ++s)
                secEndFile[s] = static_cast<int>(mesh.sections[s].startTriangle + mesh.sections[s].triangleCount);

            std::vector<int> facesPerSection(nSections, 0);
            int fileBytePos = 0;
            int curSec = 0;
            for (int f = 0; f < totalFaces; ++f)
            {
                while (curSec < nSections - 1 && fileBytePos >= secEndFile[curSec])
                    curSec++;
                facesPerSection[curSec]++;
                int nVerts = faceRefs[f].isQuad ? 4 : 3;
                fileBytePos += kFilePolyPropsSize + static_cast<int>(sizeof(VertexIndex)) * (1 + nVerts);
            }

            std::vector<ShapeSection> shapeSections(nSections);
            Offset nativePos(0);
            auto& faceStream = shape->_lods[i]->Faces();
            for (int s = 0; s < nSections; ++s)
            {
                const auto& sec = mesh.sections[s];
                shapeSections[s].beg = nativePos;
                shapeSections[s].material = sec.specialMaterial;
                Texture* tex = nullptr;
                if (sec.materialIndex != UINT32_MAX && sec.materialIndex < materialIndexToTexture.size())
                    tex = materialIndexToTexture[sec.materialIndex];
                shapeSections[s].properties.SetTexture(tex);
                shapeSections[s].properties.SetSpecial(static_cast<int>(sec.hints));
                shapeSections[s].surfMat = GTexMaterialBank.TextureToMaterial(tex);
                for (int f = 0; f < facesPerSection[s]; ++f)
                    faceStream.Next(nativePos);
                shapeSections[s].end = nativePos;
            }
            faceStream.SetSections(shapeSections.data(), nSections);
        }
        else
        {
            shape->_lods[i]->FindSections();
        }

        if (isODOL)
        {
            // ODOL: use pre-computed section data from binary (FaceSelection::SerializeBin)
            for (size_t si = 0; si < mesh.selections.size() && si < static_cast<size_t>(shape->_lods[i]->NNamedSel());
                 si++)
            {
                const auto& modelSel = mesh.selections[si];
                ::NamedSelection& sel = shape->_lods[i]->NamedSel(static_cast<int>(si));
                sel.SetNeedsSections(modelSel.needsSections);
                if (!modelSel.sectionIndices.empty())
                {
                    std::vector<int> sections(modelSel.sectionIndices.begin(), modelSel.sectionIndices.end());
                    sel.SetSections(sections.data(), static_cast<int>(sections.size()));
                }
            }
        }
        else
        {
            for (int si = 0; si < shape->_lods[i]->NNamedSel(); si++)
            {
                ::NamedSelection& sel = shape->_lods[i]->NamedSel(si);
                if (sel.Faces().Size() > 0)
                {
                    sel.FaceOffsets(shape->_lods[i]);
                    sel.RescanSections(shape->_lods[i]);
                }
            }
        }

        if (isODOL)
        {
            shape->_lods[i]->_special = static_cast<int>(lodLevel.mesh.special);
            shape->_lods[i]->_colorTop = PackedColor(mesh.iconColor);
            shape->_lods[i]->_color = PackedColor(mesh.selectedColor);
            shape->_lods[i]->SetHints(static_cast<ClipFlags>(lodLevel.mesh.orHints),
                                      static_cast<ClipFlags>(lodLevel.mesh.andHints));
        }
        else
        {
            shape->_lods[i]->CalculateColor();
            // Compute _special from section textures and face flags (Shape::ScanShapes lines 763-826)
            {
                Shape* lod = shape->_lods[i];
                int orSpec = 0, andSpec = -1;
                for (int s = 0; s < lod->NSections(); s++)
                {
                    int spec = lod->GetSection(s).properties.Special();
                    Texture* tex = lod->GetSection(s).properties.GetTexture();
                    if (tex)
                    {
                        tex->LoadHeaders();
                        if (tex->IsAlpha())
                            spec |= IsAlpha | IsAlphaOrdered;
                        if (tex->IsTransparent())
                            spec |= IsTransparent;
                        if (tex->IsAnimated())
                            spec |= ::IsAnimated;
                        const char* ext = strrchr(tex->Name(), '.');
                        if (ext && !strcmpi(ext, ".paa"))
                            if (!strstr(tex->Name(), "\\000"))
                                spec |= IsAlphaOrdered;
                    }
                    orSpec |= spec;
                    andSpec &= spec;
                }
                for (int v = 0; v < lod->NVertex(); v++)
                {
                    if ((lod->Clip(v) & ClipLandMask) == ClipLandOn)
                    {
                        orSpec |= OnSurface;
                        break;
                    }
                }
                lod->_special = orSpec & (IsAlpha | IsTransparent | ::IsAnimated | OnSurface);
                lod->_special |= andSpec & (NoShadow | ZBiasMask);
            }
        }
        shape->_lods[i]->_faceNormalsValid = true;
        shape->_lods[i]->RecalculateAreas();
        shape->_lods[i]->StoreOriginalMinMax();
    }

    if (model.sourceFormat == "ODOL")
    {
        for (int i = 0; i < lodCount; ++i)
        {
            if (shape->_lods[i])
            {
                const Mesh& m = model.lodLevels[i].mesh;
                shape->_lods[i]->_minMax[0] = ::Vector3(m.boundingBox.min.x, m.boundingBox.min.y, m.boundingBox.min.z);
                shape->_lods[i]->_minMax[1] = ::Vector3(m.boundingBox.max.x, m.boundingBox.max.y, m.boundingBox.max.z);
                shape->_lods[i]->_bCenter = ::Vector3(m.bCenter.x, m.bCenter.y, m.bCenter.z);
                shape->_lods[i]->_bRadius = m.bRadius;
                shape->_lods[i]->_minMaxDirty = false;
                shape->_lods[i]->StoreOriginalMinMax();
            }
        }

        // LODShape::SerializeBin fields
        shape->_special = static_cast<int>(model.special);
        shape->_boundingSphere = model.boundingSphere.radius;
        shape->_geometrySphere = model.geometrySphere.radius;
        shape->_minMax[0] = ::Vector3(model.boundingBox.min.x, model.boundingBox.min.y, model.boundingBox.min.z);
        shape->_minMax[1] = ::Vector3(model.boundingBox.max.x, model.boundingBox.max.y, model.boundingBox.max.z);
        shape->_boundingCenter = ::Vector3(model.boundingCenter.x, model.boundingCenter.y, model.boundingCenter.z);
        shape->_geometryCenter = ::Vector3(model.geometryCenter.x, model.geometryCenter.y, model.geometryCenter.z);
        shape->_aimingCenter = ::Vector3(model.aimingCenter.x, model.aimingCenter.y, model.aimingCenter.z);
        shape->_centerOfMass = ::Vector3(model.centerOfMass.x, model.centerOfMass.y, model.centerOfMass.z);
        shape->_color = PackedColor(model.color);
        shape->_colorTop = PackedColor(model.colorTop);
        shape->_viewDensity = model.viewDensity;
        shape->_autoCenter = (model.autoCenterEnabled != 0);
        shape->_lockAutoCenter = (model.lockAutoCenter != 0);
        shape->_canOcclude = (model.canOcclude != 0);
        shape->_canBeOccluded = (model.canBeOccluded != 0);
        shape->_allowAnimation = (model.allowAnimation != 0);
        shape->SetRemarks(model.remarksFlags);
        shape->_andHints = static_cast<int>(model.andHints);
        shape->_orHints = static_cast<int>(model.orHints);
        shape->_mapType = static_cast<MapType>(model.mapType);
        memcpy(&shape->_invInertia, model.invInertia, sizeof(model.invInertia));

        // LODShape::SerializeBin lines 541-558
        if (!model.massArray.empty())
        {
            shape->_massArray.Resize(static_cast<int>(model.massArray.size()));
            for (int m = 0; m < static_cast<int>(model.massArray.size()); m++)
                shape->_massArray[m] = model.massArray[m];
        }
        shape->_mass = model.mass;
        shape->_invMass = model.invMass;
        shape->_armor = model.armor;
        shape->_invArmor = model.invArmor;
        if (model.armor > 1e-10f)
            shape->_logArmor = std::log(model.armor);
        else
            shape->_logArmor = 25.0f;

        // shapeFile.cpp:560-571
        shape->_memory = model.memoryIdx;
        shape->_geometry = model.geometryIdx;
        shape->_geometryFire = model.geometryFireIdx;
        shape->_geometryView = model.geometryViewIdx;
        shape->_geometryViewPilot = model.geometryViewPilotIdx;
        shape->_geometryViewGunner = model.geometryViewGunnerIdx;
        shape->_geometryViewCommander = model.geometryViewCommanderIdx;
        shape->_geometryViewCargo = model.geometryViewCargoIdx;
        shape->_landContact = model.landContactIdx;
        shape->_roadway = model.roadwayIdx;
        shape->_paths = model.pathsIdx;
        shape->_hitpoints = model.hitpointsIdx;

        // Proxies must be created before OptimizeShapes (Shape::SerializeBin order)
        {
            for (int li = 0; li < lodCount; li++)
            {
                Shape* lod = shape->_lods[li];
                if (!lod)
                    continue;
                if (li >= static_cast<int>(model.lodLevels.size()))
                    continue;
                const auto& lodLevel = model.lodLevels[li];
                for (size_t pi = 0; pi < lodLevel.mesh.proxies.size(); pi++)
                {
                    const auto& proxy = lodLevel.mesh.proxies[pi];
                    ProxyModelName proxyName = normalizeProxyModelName(proxy.name);
                    RString modelName(proxyName.modelName.c_str());

                    Ref<ProxyObject> po = new ProxyObject;
                    if (GReplaceProxies)
                    {
                        RString proxyType = RString("Proxy") + modelName;
                        RString proxyShape = GetShapeName(modelName);
                        po->obj = NewObject(proxyType, proxyShape);
                    }
                    else
                    {
                        po->obj = NewProxyObject(modelName);
                    }
                    if (!po->obj)
                        continue;

                    po->name = modelName;
                    po->id = proxy.id >= 0 ? proxy.id : proxyName.id;
                    po->selection = static_cast<int>(proxy.selectionIndex);

                    const auto& m = proxy.transform.m;
                    Matrix4 trans;
                    trans.SetDirectionAside(::Vector3(m[0][0], m[0][1], m[0][2]));
                    trans.SetDirectionUp(::Vector3(m[1][0], m[1][1], m[1][2]));
                    trans.SetDirection(::Vector3(m[2][0], m[2][1], m[2][2]));
                    trans.SetPosition(::Vector3(m[0][3], m[1][3], m[2][3]));

                    LODShapeWithShadow* pshape = po->obj->GetShape();
                    if (pshape)
                        trans.SetPosition(trans.FastTransform(pshape->BoundingCenter()));
                    po->obj->SetTransform(trans);
                    po->invTransform = trans.InverseScaled();
                    po->obj->SetDestructType(DestructNo);
                    lod->_proxy.Add(po);
                }
            }
        }

        // Oxygen creates sections before marking proxy faces hidden, so section hints
        // lack IsHiddenProxy. Patch any section containing a proxy face selection.
        for (int li = 0; li < lodCount; li++)
        {
            Shape* lod = shape->_lods[li];
            if (!lod)
                continue;
            for (int si = 0; si < lod->NNamedSel(); si++)
            {
                const ::NamedSelection& sel = lod->NamedSel(si);
                if (strncmp(sel.Name(), "proxy:", 6) != 0)
                    continue;
                if (sel.Faces().Size() < 1)
                    continue;
                int faceIdx = sel.Faces()[0];
                Offset faceOfs = lod->Faces().Find(faceIdx);
                for (int s = 0; s < lod->NSections(); s++)
                {
                    ShapeSection& sec = lod->GetSection(s);
                    if (faceOfs >= sec.beg && faceOfs < sec.end)
                    {
                        sec.properties.OrSpecial(IsHiddenProxy);
                        sec.properties.SetTexture(nullptr);
                        break;
                    }
                }
            }
        }

        // LODShape::SerializeBin IsLoading block
        shape->OptimizeShapes();
        shape->CheckForcedProperties();
        shape->InitConvexComponents();
        shape->_propertyClass = shape->PropertyValue("class");
        shape->_propertyDammage = shape->PropertyValue("dammage");

        // shapeFile.cpp:588-598
        {
            float alpha = shape->_color.A8() * (1.0f / 255.0f);
            float transparency = 1.0f - alpha * 1.5f;
            if (transparency >= 0.99f)
                shape->_viewDensity = 0;
            if (transparency > 0.01f)
                shape->_viewDensity = std::log(transparency) * 4.0f;
            else
                shape->_viewDensity = -10;
        }

        if (reversed)
        {
            shape->Reverse();
            shape->SetRemarks(shape->Remarks() | REM_REVERSED);
        }
    }
    else
    {
        shape->CalculateMinMax(true);
        shape->OptimizeShapes();

        // Shape.cpp:4961-4971
        for (int i = 0; i < lodCount; i++)
            if (shape->_resolutions[i] < 900 && shape->_lods[i])
                shape->_special |= shape->_lods[i]->Special();

        shape->CheckForcedProperties();
        shape->InitConvexComponents();
        shape->ScanProxies();
        shape->ScanProperties();
        shape->CalculateHints();
        shape->_propertyClass = shape->PropertyValue("class");
        shape->_propertyDammage = shape->PropertyValue("dammage");
    }

    return shape;
}

} // namespace ShapeAdapter
} // namespace Model
} // namespace Poseidon

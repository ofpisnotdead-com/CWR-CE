#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Scene/Thing.hpp>
#include <Poseidon/AI/Path/PathSteer.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Game/OperMap.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/World/Scene/ObjLine.hpp>

using namespace Poseidon;
namespace Poseidon
{
using namespace Foundation;
using Foundation::EnumName;

void EntityAI::DrawDiags()
{
#if _ENABLE_CHEATS
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        base::DrawDiags();
        return;
    }
    // draw object target
    LODShapeWithShadow* forceArrow = GScene->ForceArrow();

#if 1

    if (CHECK_DIAG(DEPath) && !GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeStatic)) && !_isDead &&
        !(GWorld->PlayerManual() && GWorld->PlayerOn() && GWorld->PlayerOn()->Brain() == PilotUnit()))
    {
        {
            Point3 steerPos = SteerPoint(GetSteerAheadSimul(), GetSteerAheadPlan());

            steerPos += DirectionAside() * _avoidAside;

            Ref<Object> arrow = new ObjectColored(forceArrow, -1);
            Point3 pos = steerPos;

            float size = 0.6;
            arrow->SetPosition(pos);
            arrow->SetOrient(VUp, VForward);
            arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(0.7, 0.5, 0)));
#define DRAW_OBJ(obj) GScene->ObjectForDrawing(obj);
            DRAW_OBJ(arrow);
        }

        {
            Point3 steerPos = SteerPoint(GetPredictTurnSimul(), GetPredictTurnPlan());

            Ref<Object> arrow = new ObjectColored(forceArrow, -1);
            Point3 pos = steerPos;

            float size = 0.4;
            arrow->SetPosition(pos);
            arrow->SetOrient(VUp, VForward);
            arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(0.9, 0.7, 0.3)));
            DRAW_OBJ(arrow);
        }

        if (_hideBehind)
        {
            Ref<Object> arrow = new ObjectColored(forceArrow, -1);
            Point3 pos = _hideBehind->Position();

            float size = 1.5;
            arrow->SetPosition(pos);
            arrow->SetOrient(VUp, VForward);
            arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(0, 0, 0, 0.5)));
            DRAW_OBJ(arrow);
        }

        if (unit->IsSubgroupLeader())
        {
            { // draw strategic GoTo destination
                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                float size = 0.5;
                if (GLOB_WORLD->CameraOn() == this)
                    size = 2;
                arrow->SetPosition(_stratGoToPos);
                arrow->SetOrient(VUp, VForward);
                arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                arrow->SetScale(size);
                arrow->SetConstantColor(PackedColor(Color(1, 1, 1)));

                DRAW_OBJ(arrow);
            }
            { // draw command destination
                AISubgroup* subgrp = unit->GetSubgroup();
                if (subgrp && subgrp->GetCommand())
                {
                    Vector3Val pos = subgrp->GetCommand()->_destination;
                    Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                    float size = 0.5;
                    if (GLOB_WORLD->CameraOn() == this)
                        size = 2;
                    arrow->SetPosition(pos);
                    arrow->SetOrient(VUp, VForward);
                    arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                    arrow->SetScale(size);
                    arrow->SetConstantColor(PackedColor(Color(0.5, 1, 1)));

                    DRAW_OBJ(arrow);
                }
            }
        }
        else
        {
            AIGroup* grp = unit->GetGroup();
            AIUnit* leader = grp ? grp->Leader() : nullptr;
            if (leader)
            {
                EntityAI* lVehicle = leader->GetVehicle();
                // predict leader position
                const float estT = GetFormationTime();

                float maxSpeed = lVehicle->GetType()->GetMaxSpeedMs();
                Vector3 predSpeed = lVehicle->Speed();
                float spSize2 = predSpeed.SquareSize();
                if (spSize2 < Square(maxSpeed * 0.5) && spSize2 > Square(maxSpeed * 0.1))
                {
                    predSpeed.Normalize();
                    predSpeed *= maxSpeed * 0.5;
                }
                Vector3Val estPos = lVehicle->Position() + estT * predSpeed;

                // predict leader orientation
                Matrix4 estTransform;
                estTransform.SetPosition(estPos);
                // get formation orientation
                Vector3Val formDir = leader->GetSubgroup()->GetFormationDirection();
                estTransform.SetDirectionAndUp(formDir, VUp);
                Vector3Val formPos = unit->GetFormationRelative() - leader->GetFormationRelative();
                Vector3Val movePos = estTransform.FastTransform(formPos);

                { // draw predicted formation position
                    Vector3Val pos = movePos;
                    Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                    float size = 0.5;
                    if (GLOB_WORLD->CameraOn() == this)
                        size = 2;
                    arrow->SetPosition(pos);
                    arrow->SetOrient(VUp, VForward);
                    arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                    arrow->SetScale(size);
                    arrow->SetConstantColor(PackedColor(Color(0.5, 1, 1)));

                    DRAW_OBJ(arrow);
                }

                { // draw predicted formation position
                    Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                    arrow->SetPosition(unit->GetFormationAbsolute());
                    arrow->SetOrient(VUp, VForward);
                    float size = 0.5;
                    arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                    arrow->SetScale(size);
                    arrow->SetConstantColor(PackedColor(Color(1, 1, 1)));

                    DRAW_OBJ(arrow);
                }
            }
        }
    }
#endif

#if 1
    // draw current weapon direction
    if (CHECK_DIAG(DECombat))
    {
        {
            Vector3Val dir = GetWeaponDirection(0);
            if (dir.SquareSize() > 0.1)
            {
                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                float size = 0.1;
                arrow->SetPosition(Position());
                arrow->SetOrient(dir, VUp);
                arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                arrow->SetScale(size);
                arrow->SetConstantColor(PackedColor(Color(1, 0, 0, 0.5)));

                DRAW_OBJ(arrow);
            }
        }
        {
            Vector3Val dir = GetWeaponDirectionWanted(0);
            if (dir.SquareSize() > 0.1)
            {
                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                float size = 0.2;
                arrow->SetPosition(Position());
                arrow->SetOrient(dir, VUp);
                arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                arrow->SetScale(size);
                arrow->SetConstantColor(PackedColor(Color(1, 0.1, 0, 0.5)));

                DRAW_OBJ(arrow);
            }
        }
        {
            Vector3Val dir = GetEyeDirection();
            if (dir.SquareSize() > 0.1)
            {
                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                float size = 0.4;
                arrow->SetPosition(Position());
                arrow->SetOrient(dir, VUp);
                arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                arrow->SetScale(size);
                arrow->SetConstantColor(PackedColor(Color(1, 1, 0, 0.5)));

                DRAW_OBJ(arrow);
            }
        }
    }
#endif
#if 1
    if (!QIsManual() && CHECK_DIAG(DECombat))
    {
        float attackSize = _attackTarget.IdExact() ? 1 : 0.3;
        {
            Ref<Object> arrow = new ObjectColored(forceArrow, -1);
            float size = attackSize;
            Vector3 aPos = _attackAggresivePos;
            arrow->SetPosition(aPos);
            arrow->SetOrient(VUp, VForward);
            arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(1, 0.5, 0)));
            DRAW_OBJ(arrow);
        }
        {
            Ref<Object> arrow = new ObjectColored(forceArrow, -1);
            float size = attackSize;
            Vector3 aPos = _attackEconomicalPos;
            arrow->SetPosition(aPos);
            arrow->SetOrient(Vector3(0, -1, 0), Vector3(0, 0, -1));
            arrow->SetPosition(arrow->PositionModelToWorld(-forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(1, 0, 0)));
            DRAW_OBJ(arrow);
        }
    }
#endif

    if (CHECK_DIAG(DEPath))
    {
        Ref<Object> arrow = new ObjectColored(forceArrow, -1);
        float size = 0.2;
        Vector3 aPos = PositionModelToWorld(GetType()->_supplyPoint);
        arrow->SetPosition(aPos);
        arrow->SetOrient(Vector3(0, -1, 0), Vector3(0, 0, -1));
        arrow->SetPosition(arrow->PositionModelToWorld(-forceArrow->BoundingCenter() * size));
        arrow->SetScale(size);
        arrow->SetConstantColor(PackedColor(Color(0, 1, 0)));
        DRAW_OBJ(arrow);
    }

    // draw path
    if (PilotUnit())
    {
        if (CHECK_DIAG(DEPath))
        {
            const Path& path = PilotUnit()->GetPath();
            float size = 0.05;
            PackedColor color(Color(1, 0, 1, 1));
            if (this == GLOB_WORLD->CameraOn())
                size *= 2;
            if (unit->IsSubgroupLeader())
                color = PackedColor(Color(1, 1, 0, 1));
            for (int i = 1; i < path.Size(); i++)
            {
                Ref<LODShapeWithShadow> shape = ObjectLine::CreateShape();
                Ref<Object> lineObj = new ObjectLineDiag(shape);
                lineObj->SetConstantColor(color);

                const OperInfoResult& cur = path[i - 1];
                const OperInfoResult& nxt = path[i];
                Vector3Val cPos = cur._pos;
                Vector3Val nPos = nxt._pos;
                lineObj->SetPosition(cPos);
                ObjectLine::SetPos(shape, VZero, nPos - cPos);

                GScene->ObjectForDrawing(lineObj);
            }
            for (int i = 0; i < path.Size(); i++)
            {
                const OperInfoResult& info = path[i];

                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                Vector3Val pos = info._pos;
                arrow->SetPosition(pos);
                bool revert = (i >= unit->MaxOperIndex());
                if (revert)
                {
                    arrow->SetOrient(Vector3(0, -1, 0), Vector3(0, 0, -1));
                    arrow->SetPosition(arrow->PositionModelToWorld(-forceArrow->BoundingCenter() * size));
                }
                else
                {
                    arrow->SetOrient(Vector3(0, +1, 0), Vector3(0, 0, +1));
                    arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                }
                arrow->SetScale(size);
                arrow->SetConstantColor(color);

                DRAW_OBJ(arrow);
            }
        }

        if (this == GLOB_WORLD->CameraOn())
        {
            LODShapeWithShadow* rectShape = GScene->Preloaded(RectangleModel);
            int index = 0;

#if 1

            // draw fields costs
            if (CHECK_DIAG(DECostMap))
            {
                bool soldier = unit->IsFreeSoldier();
                int x, xLand = toIntFloor(Position().X() * InvLandGrid);
                int z, zLand = toIntFloor(Position().Z() * InvLandGrid);
                for (z = zLand - 1; z <= zLand + 1; z++)
                    for (x = xLand - 1; x <= xLand + 1; x++)
                    {
                        Ref<OperField> fld =
                            GLOB_LAND->OperationalCache()->GetOperField(x, z, MASK_AVOID_OBJECTS | MASK_PREFER_ROADS);
                        if (!fld)
                            continue;
                        int xx, zz;
                        for (zz = 0; zz < OperItemRange; zz++)
                            for (xx = 0; xx < OperItemRange; xx++)
                            {
                                PackedColor color;
                                OperItemType type =
                                    (soldier ? fld->_items[zz][xx]._typeSoldier : fld->_items[zz][xx]._type);
                                static const PackedColor typeColor[NOperItemType] = {
                                    PackedColor(Color(1, 1, 1, 0)),       // OITNormal,
                                    PackedColor(Color(0, 0.5, 0.5, 1)),   // OITAvoidBush,
                                    PackedColor(Color(0, 0.5, 0, 1)),     // OITAvoidTree,
                                    PackedColor(Color(0.5, 0.5, 0.5, 1)), // OITAvoid,
                                    PackedColor(Color(0, 0, 1, 1)),       // OITWater,
                                    PackedColor(Color(1, 0, 1, 1)),       // OITSpaceRoad,
                                    PackedColor(Color(1, 1, 1, 1)),       // OITRoad,
                                    PackedColor(Color(0, 1, 1, 1)),       // OITSpaceBush,
                                    PackedColor(Color(0, 1, 0, 1)),       // OITSpaceTree,
                                    PackedColor(Color(0, 0, 0, 1)),       // OITSpace
                                    PackedColor(Color(1, 1, 0, 1)),       // OITRoadForced
                                };
                                color = typeColor[type];

                                Vector3 pos;
                                pos.Init();
                                pos[0] = x * LandGrid + xx * OperItemGrid + 0.5 * OperItemGrid;
                                pos[2] = z * LandGrid + zz * OperItemGrid + 0.5 * OperItemGrid;
                                pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]) + 0.2;

                                if ((pos - Position()).SquareSizeXZ() <= Square(50))
                                {
                                    Ref<Object> rect;
                                    if (index < MapDiags.Size())
                                        rect = MapDiags[index];
                                    else
                                    {
                                        rect = new ObjectColored(rectShape, -1);
                                        MapDiags.Access(index);
                                        MapDiags[index] = rect;
                                    }
                                    index++;
                                    rect->SetPosition(pos);
                                    rect->SetConstantColor(color);
                                    GScene->ObjectForDrawing(rect);
                                }
                            }
                    }
            }
#endif

// done: draw field costs
#if DIAG_ROAD
            // draw road connections
            {
                int x, xLand = toIntFloor(Position().X() * InvLandGrid);
                int z, zLand = toIntFloor(Position().Z() * InvLandGrid);
                for (z = zLand - 1; z <= zLand + 1; z++)
                    for (x = xLand - 1; x <= xLand + 1; x++)
                        if (InRange(x, z))
                        {
                            RoadList& list = GRoadNet->GetRoadList(x, z);
                            for (int r = 0; r < list.Size(); r++)
                            {
                                RoadLink* road = list[r];
                                // draw beg-end connection
                                if (road->NConnections() >= 2)
                                {
                                    Vector3Val beg = road->PosConnections()[0];
                                    Vector3Val end = road->PosConnections()[1];
                                    bool begCon = road->Connections()[0] != 0;
                                    bool endCon = road->Connections()[1] != 0;
                                    // draw arrow from beg to end

                                    if (!begCon)
                                    {
                                        Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                                        arrow->SetPosition(beg + Vector3(0, 1.5, 0));
                                        arrow->SetOrient(VUp, VAside);
                                        float size = 0.05;
                                        PackedColor color(Color(1, 0, 1, 0.5));
                                        arrow->SetPosition(
                                            arrow->PositionModelToWorld(-forceArrow->BoundingCenter() * size));
                                        arrow->SetScale(size);
                                        arrow->SetConstantColor(color);
                                        DRAW_OBJ(arrow);
                                    }
                                    if (!endCon)
                                    {
                                        Ref<Object> arrow = new ObjectColored(forceArrow, -1);
                                        arrow->SetPosition(end + Vector3(0, 1.5, 0));
                                        arrow->SetOrient(VUp, VAside);
                                        float size = 0.05;
                                        PackedColor color(Color(1, 0, 1, 0.5));
                                        arrow->SetPosition(
                                            arrow->PositionModelToWorld(-forceArrow->BoundingCenter() * size));
                                        arrow->SetScale(size);
                                        arrow->SetConstantColor(color);
                                        DRAW_OBJ(arrow);
                                    }

                                    Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                                    arrow->SetPosition(beg + VUp);
                                    arrow->SetOrient(beg - end, VUp);
                                    float size = end.Distance(beg) * 0.1;
                                    if (size > 0.001)
                                    {
                                        PackedColor color;
                                        if (road->IsLocked())
                                            color = PackedColor(Color(1, 0.2, 0, 0.5));
                                        else
                                            color = PackedColor(Color(1, 0, 1, 0.5));
                                        arrow->SetPosition(
                                            arrow->PositionModelToWorld(-forceArrow->BoundingCenter() * size));
                                        arrow->SetScale(size);
                                        arrow->SetConstantColor(color);

                                        DRAW_OBJ(arrow);
                                    }
                                    else
                                    {
                                        Fail("Beg..end same");
                                    }
                                }
                            }
                        }
            }
#endif

#if 1
            if (CHECK_DIAG(DELockMap))
            {
                int operX = toInt(Position().X() * InvOperItemGrid);
                int operZ = toInt(Position().Z() * InvOperItemGrid);
                ILockCache* locks = GLandscape->LockingCache();
                const int range = 50;
                for (int x = operX - range; x <= operX + range; x++)
                    for (int z = operZ - range; z <= operZ + range; z++)
                    {
                        bool lockV = locks->IsLocked(x, z, false);
                        bool lockS = locks->IsLocked(x, z, true);

                        if (!lockV && !lockS)
                            continue;

                        PackedColor color(Color(lockV, 0, lockS));

                        Vector3 pos;
                        pos.Init();
                        pos[0] = x * OperItemGrid + 0.5 * OperItemGrid;
                        pos[2] = z * OperItemGrid + 0.5 * OperItemGrid;
                        pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]) + 0.2;

                        if ((pos - Position()).SquareSizeXZ() <= Square(50))
                        {
                            Ref<Object> rect;
                            if (index < MapDiags.Size())
                                rect = MapDiags[index];
                            else
                            {
                                rect = new ObjectColored(rectShape, -1);
                                MapDiags.Access(index);
                                MapDiags[index] = rect;
                            }
                            index++;
                            rect->SetPosition(pos);
                            rect->SetConstantColor(color);
                            GScene->ObjectForDrawing(rect);
                        }
                    }
            }
#endif
        }
    }

#if DIAG_VEHICLE
    base::DrawDiags();
#endif
#endif
}

Matrix4 EntityAI::AnimateProxyMatrix(int level, const ProxyObject& proxy) const
{
    Object* obj = proxy.obj;
    LODShapeWithShadow* lodShape = obj->GetShape();

    Matrix4 proxyTransform = obj->Transform();
    proxyTransform.SetPosition(proxyTransform.FastTransform(lodShape->BoundingCenter()));

    AnimateMatrix(proxyTransform, level, proxy.selection);

    return proxyTransform;
}

int EntityAI::GetProxyComplexity(int level, const FrameBase& pos, float dist2) const
{
    return 0;
}

void EntityAI::DrawProxies(int level, ClipFlags clipFlags, const Matrix4& transform, const Matrix4& invTransform,
                           float dist2, float z2, const LightList& lights)
{
    // draw flag
    Shape* sShape = _shape->LevelOpaque(level);
    for (int i = 0; i < sShape->NProxies(); i++)
    {
        const ProxyObject& proxy = sShape->Proxy(i);
        Object* obj = proxy.obj;
        const EntityType* type = obj->GetVehicleType();
        if (!type)
        {
            continue;
        }
        RString simulation = type->_simName;
        if (strcmp(simulation, "alwaysshow") == 0)
        {
            // smart clipping par of obj->Draw
            Matrix4Val pTransform = transform * obj->Transform();
            Matrix4Val invPTransform = proxy.invTransform * invTransform;

            // LOD detection
            LODShapeWithShadow* pshape = obj->GetShapeOnPos(pTransform.Position());
            if (!pshape)
            {
                continue;
            }
            int level = GScene->LevelFromDistance2(pshape, dist2, pTransform.Scale(), pTransform.Direction(),
                                                   GScene->GetCamera()->Direction());
            if (level == LOD_INVISIBLE)
            {
                continue;
            }

            FrameWithInverse pFrame(pTransform, invPTransform);

            // construct FrameWithInverse from transform and invTransform

            obj->Draw(level, ClipAll, pFrame);
        }
        else if (_flag && _showFlag)
        {
            if (stricmp(simulation, "flag") == 0)
            {
                LODShapeWithShadow* lodShape = obj->GetShape();

                Matrix4 proxyTransform = AnimateProxyMatrix(level, proxy);

                Matrix4Val pTransform = transform * proxyTransform;
                Matrix4Val invPTransform = pTransform.InverseScaled();

                int pLevel = GScene->LevelFromDistance2(lodShape, dist2, pTransform.Scale(), pTransform.Direction(),
                                                        GScene->GetCamera()->Direction());
                if (pLevel == LOD_INVISIBLE)
                {
                    continue;
                }

                _flag->SetFlagTexture(GetFlagTexture());

                FrameWithInverse pFrame(pTransform, invPTransform);

                _flag->FlagAnimate(pFrame, pLevel);
                Shape* shape = lodShape->LevelOpaque(pLevel);
                shape->PrepareTextures(z2, shape->Special());
                shape->Draw(this, lights, ClipAll, shape->Special(), pTransform, invPTransform);
                _flag->FlagDeanimate(pFrame, pLevel);
            }
        }
    }
}

Texture* EntityAI::GetCursorTexture(Person* person)
{
    // check actual weapon
    int weapon = SelectedWeapon();
    if (weapon < 0 || weapon > NMagazineSlots())
    {
        // return no weapon cursor
        return nullptr;
    }
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    const MuzzleType* muzzle = slot._muzzle;
    return muzzle->_cursorTexture;
}

Texture* EntityAI::GetCursorAimTexture(Person* person)
{
    // check actual weapon
    int weapon = SelectedWeapon();
    if (weapon < 0 || weapon > NMagazineSlots())
    {
        // return no weapon cursor
        return nullptr;
    }
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    const MuzzleType* muzzle = slot._muzzle;
    return muzzle->_cursorAimTexture;
}

/*!
\note Function is currently not used
*/

PackedColor EntityAI::GetCursorColor(Person* person)
{
    return PackedWhite;
}

/*!
\note
Object::Draw2D is used to draw this model
*/

LODShapeWithShadow* EntityAI::GetOpticsModel(Person* person)
{
    return nullptr;
}

/*!
Example of situation when true is returned is when binocular is used.
Any other weapon that can be used only through optics could do the same.
*/

bool EntityAI::GetForceOptics(Person* person) const
{
    return false;
}

PackedColor EntityAI::GetOpticsColor(Person* person)
{
    return PackedBlack;
}

Texture* EntityAI::GetFlagTexture()
{
    return nullptr;
}

Texture* EntityAI::GetFlagTextureInternal()
{
    return nullptr;
}

void EntityAI::SetFlagTexture(RString name) {}

Person* EntityAI::GetFlagOwner()
{
    return nullptr;
}

void EntityAI::SetFlagOwner(Person* veh) {}

TargetSide EntityAI::GetFlagSide() const
{
    return TSideUnknown;
}

void EntityAI::SetFlagSide(TargetSide side) {}

void EntityAI::GetMaterial(TLMaterial& mat, int index) const
{
    // IAnimator interface implementation
    // check special materials

    if (index > 0 && index < 200)
    {
        // expected materials range in 50..69 (range 20

        // offset +20 -> shining
        // offset +40 -> in shadow
        bool shining = false;
        bool inShadow = false;
        if (index >= 90)
        {
            inShadow = true;
            index -= 40;
        }
        if (index >= 70)
        {
            shining = true;
            index -= 20;
        }

        const EntityAIType* type = GetType();
        // search hits to find this material
        const HitPointList& hits = type->GetHitPoints();
        for (int i = 0; i < hits.Size(); i++)
        {
            if (hits[i]->GetMaterial() == index)
            {
                // dammaged material
                float dammage = GetHitCont(*hits[i]);
                float brightFactor = 1 - dammage * 0.85;
                Color color(brightFactor, brightFactor, brightFactor);

#if _ENABLE_CHEATS
                if (CHECK_DIAG(DEDammage))
                {
                    color = Color(dammage, 1 - dammage, 0);
                }
#endif

                color = color * GEngine->GetAccomodateEye();
                if (shining)
                {
                    CreateMaterialShining(mat, color);
                }
                else if (inShadow)
                {
                    CreateMaterialConstant(mat, color, 0);
                }
                else
                {
                    CreateMaterialNormal(mat, color);
                }
                return;
            }
        }
    }
    base::GetMaterial(mat, index);
}

Vector3 EntityAI::ExternalCameraPosition(CameraType camType) const
{
    return GetType()->_extCameraPosition;
}

void EntityAI::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    // if( camType==CamInternal ) fov=GetType()->_fov;
    if (camType == CamInternal)
    {
        GetType()->_viewPilot.LimitVirtual(camType, heading, dive, fov);
    }
    else
    {
        base::LimitVirtual(camType, heading, dive, fov);
    }
}

void EntityAI::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    if (camType == CamInternal)
    {
        GetType()->_viewPilot.InitVirtual(camType, heading, dive, fov);
    }
    else
    {
        base::InitVirtual(camType, heading, dive, fov);
    }
}

float EntityAI::GetAmmoCost() const
{
    // sum-up ammo state
    float curAmmo = 0;
    int i, n = NMagazines();
    for (i = 0; i < n; i++)
    {
        const Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        if (magazine->_ammo == 0)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (!type)
        {
            continue;
        }
        if (type->_modes.Size() == 0)
        {
            continue;
        }
        const AmmoType* ammo = type->_modes[0]->_ammo;
        if (!ammo)
        {
            continue;
        }
        curAmmo += magazine->_ammo * ammo->cost;
    }
    return curAmmo;
}
float EntityAI::GetAmmoHit() const
{
    // sum-up ammo state
    float curAmmo = 0;
    int i, n = NMagazines();
    for (i = 0; i < n; i++)
    {
        const Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        if (magazine->_ammo == 0)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (!type)
        {
            continue;
        }
        if (type->_modes.Size() == 0)
        {
            continue;
        }
        const AmmoType* ammo = type->_modes[0]->_ammo;
        if (!ammo)
        {
            continue;
        }
        curAmmo += magazine->_ammo * ammo->hit;
    }
    return curAmmo;
}
float EntityAI::GetMaxAmmoCost() const
{
    // sum-up ammo state
    float curAmmo = 0;
    int i, n = NMagazines();
    for (i = 0; i < n; i++)
    {
        const Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (!type)
        {
            continue;
        }
        if (type->_maxAmmo == 0)
        {
            continue;
        }
        if (type->_modes.Size() == 0)
        {
            continue;
        }
        const AmmoType* ammo = type->_modes[0]->_ammo;
        if (!ammo)
        {
            continue;
        }
        curAmmo += type->_maxAmmo * ammo->cost;
    }
    return curAmmo;
}
float EntityAI::GetMaxAmmoHit() const
{
    // sum-up ammo state
    float curAmmo = 0;
    int i, n = NMagazines();
    for (i = 0; i < n; i++)
    {
        const Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (!type)
        {
            continue;
        }
        if (type->_maxAmmo == 0)
        {
            continue;
        }
        if (type->_modes.Size() == 0)
        {
            continue;
        }
        const AmmoType* ammo = type->_modes[0]->_ammo;
        if (!ammo)
        {
            continue;
        }
        curAmmo += type->_maxAmmo * ammo->hit;
    }
    return curAmmo;
}

float EntityAI::Rearm(float resources)
{
    resources += _rearmCredit;
    // sum-up ammo state
    int i, n = NMagazines();
    for (i = 0; i < n; i++)
    {
        Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (!type)
        {
            continue;
        }
        if (type->_maxAmmo == 0)
        {
            continue;
        }
        if (type->_modes.Size() == 0)
        {
            continue;
        }
        const AmmoType* ammo = type->_modes[0]->_ammo;
        if (!ammo)
        {
            continue;
        }
        float cost = ammo->cost;
        int need = type->_maxAmmo - magazine->_ammo;
        if (need * cost > resources)
        {
            need = toIntFloor(resources / cost);
        }
        if (need == 0)
        {
            continue;
        }

        magazine->_ammo += need;
        resources -= need * cost;
    }
    _rearmCredit = resources; // accumulate what's rest
    return resources;
}

float EntityAI::NeedsRearm() const
{
    if (IsDammageDestroyed())
    {
        return 0;
    }

    // sum-up ammo state
    float minWeapon = 1.0;
    int i, n = NMagazines();
    for (i = 0; i < n; i++)
    {
        const Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (!type)
        {
            continue;
        }
        if (type->_maxAmmo == 0)
        {
            continue;
        }
        float ratio = (float)magazine->_ammo / type->_maxAmmo;
        saturateMin(minWeapon, ratio * 1.5);
    }
    saturate(minWeapon, 0, 1);

    float max = GetMaxAmmoHit();
    if (max <= 0)
    {
        return 0;
    }
    float act = GetAmmoHit();
    return 1.0 - floatMin(act / max, minWeapon);
}

float EntityAI::NeedsAmbulance() const
{
    if (!CommanderUnit())
    {
        return 0;
    }
    return CommanderUnit()->GetPerson()->NeedsAmbulance();
}

float EntityAI::NeedsRepair() const
{
    if (IsDammageDestroyed())
    {
        return 1;
    }
    return GetTotalDammage();
}

float EntityAI::NeedsRefuel() const
{
    if (IsDammageDestroyed())
    {
        return 0;
    }
    float max = GetType()->GetFuelCapacity();
    if (max <= 0)
    {
        return 0;
    }
    float act = GetFuel();
    return 1 - act / max;
}

float EntityAI::NeedsInfantryRearm() const
{
    return 0;
}

float EntityAI::NeedsLoadFuel() const
{
    return 0;
}
float EntityAI::NeedsLoadAmmo() const
{
    return 0;
}
float EntityAI::NeedsLoadRepair() const
{
    return 0;
}

bool EntityAI::IsMoveTarget() const
{
    // no vehicles with AI may be move targets
    return false;
    // return CommanderUnit()==nullptr && PilotUnit()==nullptr;
}

// accuracy simulation

const EntityAIType* EntityAI::GetTypeAtLeast(float accuracy) const
{
    // start with certain information
    const EntityType* type = GetType();
    while (type->_parentType.NotNull() && type->_parentType->_accuracy >= accuracy)
    {
        type = type->_parentType;
    }
    const EntityAIType* vType = dynamic_cast<const EntityAIType*>(type);
    if (!vType)
    {
        Fail("Non-ai type");
    }
    return vType;
}

const EntityAIType* EntityAI::GetType(float accuracy) const
{
    // start with certain information
    const EntityType* type = GetType();
    while (type->_parentType.NotNull() && type->_accuracy >= accuracy)
    {
        type = type->_parentType;
    }
    AI_ERROR(dynamic_cast<const EntityAIType*>(type));
    const EntityAIType* vType = static_cast<const EntityAIType*>(type);
    return vType;
}

TargetSide EntityAI::GetTargetSide() const
{
    if (_isDead)
    {
        return TCivilian;
    }

    AIUnit* unit = CommanderUnit();
    if (unit)
    {
        if (unit->GetPerson()->GetExperience() < ExperienceRenegadeLimit)
        {
            // he is crazy - shooting at friendlies - enemy to all sides
            return TEnemy;
        }
    }

    return base::GetTargetSide();
}

TargetSide EntityAI::GetTargetSide(float accuracy) const
{
    AIUnit* unit = CommanderUnit();
    if (accuracy < 1.5)
    {
        const EntityAIType* type = GetType(accuracy);
        TargetSide side = type->_typicalSide;
        if (side != TSideUnknown)
        {
            if (unit && unit->GetCaptive())
            {
                return TCivilian;
            }
        }
        return side;
    }
    if (unit && unit->GetCaptive())
    {
        return TCivilian;
    }
    TargetSide side = GetTargetSide();
    if (unit)
    {
        if (unit->GetPerson()->GetExperience() < ExperienceRenegadeLimit)
        {
            // he is crazy - shooting at friendlies - enemy to all sides
            return TEnemy;
        }
    }
    return side;
}

RString EntityAI::GetDebugName() const
{
    if (CommanderUnit())
    {
        return CommanderUnit()->GetDebugName();
    }
    char ptr[256];
    snprintf(ptr, sizeof(ptr), "%p# ", static_cast<const void*>(this));
    return RString(ptr) + base::GetDebugName() + (IsLocal() ? RString() : RString(" REMOTE"));
}

void EntityAI::ResetStatus()
{
    _isDead = false;
    _aimObserverAsked = VZero;
    _aimWeaponAsked.Clear();
    _sensorColID = SensorColID(-1);
    for (int i = 0; i < _hit.Size(); i++)
    {
        _hit[i] = 0;
    }
    _visTracker.Clear();
    for (int i = 0; i < NEntityEvent; i++)
    {
        _eventHandlers[i].Clear();
    }
    base::ResetStatus();
}

template <>
const EnumName* Foundation::GetEnumNames(EntityAI::FireState dummy)
{
    static const EnumName FireStateNames[] = {EnumName(EntityAI::FireInit, "INIT"), EnumName(EntityAI::FireAim, "AIM"),
                                              EnumName(EntityAI::FireAimed, "AIMED"),
                                              EnumName(EntityAI::FireDone, "DONE"), EnumName()};
    return FireStateNames;
}

template <>
const EnumName* Foundation::GetEnumNames(TargetState dummy)
{
    static const EnumName TargetStateNames[] = {
        EnumName(TargetDestroyed, "DESTROYED"),   EnumName(TargetAlive, "ALIVE"),
        EnumName(TargetEnemyEmpty, "ENEMYEMPTY"), EnumName(TargetEnemy, "ENEMY"),
        EnumName(TargetEnemyCombat, "COMBAT"),    EnumName()};
    return TargetStateNames;
}

LSError UserActionDescription::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("id", id, 1))
    PARAM_CHECK(ar.Serialize("text", text, 1, ""))
    PARAM_CHECK(ar.Serialize("script", script, 1, ""))

    return LSOK;
}

LSError EntityAI::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    if (ar.IsSaving())
    {
        RString type = GetType()->GetName();
        PARAM_CHECK(ar.Serialize("type", type, 1))
    }
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        bool moveOut = (MoveOutState)_moveOutState > MOIn;
        AI_ERROR((MoveOutState)_moveOutState != MOMovingOut);
        PARAM_CHECK(ar.Serialize("moveOut", moveOut, 1, false))
        if (ar.IsLoading())
        {
            _moveOutState = moveOut ? MOMovedOut : MOIn;
        }

        PARAM_CHECK(ar.Serialize("stratGoToPos", _stratGoToPos, 1, VZero))
    }
    PARAM_CHECK(ar.SerializeArray("hit", _hit, 1))
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.Serialize("limitSpeed", _limitSpeed, 1, 0))
        PARAM_CHECK(ar.Serialize("fireMode", _fire._fireMode, 1, -1))
        PARAM_CHECK(ar.Serialize("firePrepareOnly", _fire._firePrepareOnly, 1, true))
        PARAM_CHECK(ar.SerializeRef("fireTarget", _fire._fireTarget, 1))
        PARAM_CHECK(ar.SerializeEnum("fireState", _fireState, 1, FireDone))
        PARAM_CHECK(ar.Serialize("fireStateDelay", _fireStateDelay, 1))
    }
    SerializeBitBool(ar, "isDead", _isDead, 1, false) if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.Serialize("sensorColID", _sensorColID, 1, -1))

        PARAM_CHECK(ar.Serialize("forceFireWeapon", _forceFireWeapon, 1, -1))

        PARAM_CHECK(ar.SerializeRef("attackTarget", _attackTarget, 1))
        PARAM_CHECK(ar.Serialize("attackAggresivePos", _attackAggresivePos, 1, VZero))
        PARAM_CHECK(ar.Serialize("attackEconomicalPos", _attackEconomicalPos, 1, VZero))
        PARAM_CHECK(ar.Serialize("lastShotAtAssignedTarget", _lastShotAtAssignedTarget, 1, Time(0)))
        PARAM_CHECK(ar.Serialize("lastShotTime", _lastShotTime, 1, Time(0)))
        PARAM_CHECK(ar.SerializeRef("lastShot", _lastShot, 1))

        PARAM_CHECK(ar.SerializeRef("hideTarget", _hideTarget, 1))
        PARAM_CHECK(ar.SerializeRef("hideBehind", _hideBehind, 1))
        PARAM_CHECK(ar.Serialize("allowDammage", _allowDammage, 1, true))
        PARAM_CHECK(ar.Serialize("hideRefreshTime", _hideRefreshTime, 1, Glob.time))

        PARAM_CHECK(ar.Serialize("newTargetsTime", _newTargetsTime, 1, Glob.time))
        PARAM_CHECK(ar.Serialize("trackTargetsTime", _trackTargetsTime, 1, Glob.time))
        PARAM_CHECK(ar.Serialize("trackNearTargetsTime", _trackNearTargetsTime, 1, Glob.time))

        SerializeBitBool(ar, "landContact", _landContact, 1, false);
        SerializeBitBool(ar, "objectContact", _objectContact, 1, false);
        SerializeBitBool(ar, "objectContact", _waterContact, 1, false);
        SerializeBitBool(ar, "inFormation", _inFormation, 1, true);

        PARAM_CHECK(ar.Serialize("isStopped", _isStopped, 1, false))
        PARAM_CHECK(ar.Serialize("upsideDown", _isUpsideDown, 1, false))

        PARAM_CHECK(ar.Serialize("nextUserActionId", _nextUserActionId, 1, 0))
        PARAM_CHECK(ar.Serialize("UserActions", _userActions, 1))
        for (int i = 0; i < NEntityEvent; i++)
        {
            const char* eventName = FindEnumName((EntityEvent)i);
            BString<80> eHandlerName;
            sprintf(eHandlerName, "EventHandlers%s", eventName);
            PARAM_CHECK(ar.SerializeArray(eHandlerName.cstr(), _eventHandlers[i], 1))
        }
    }

    // serialize weapons
    if (ar.GetArVersion() >= 7)
    {
        if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
        {
            RemoveAllWeapons();
            RemoveAllMagazines();
        }
        PARAM_CHECK(ar.Serialize("Weapons", _weapons, 1));
        if (!ar.IsSaving() && ar.GetPass() == ParamArchive::PassSecond)
        {
            // remove NULLs
            for (int i = 0; i < _weapons.Size();)
            {
                if (_weapons[i])
                {
                    i++;
                }
                else
                {
                    _weapons.Delete(i);
                }
            }
        }
        PARAM_CHECK(ar.Serialize("Magazines", _magazines, 1));
        if (ar.IsSaving())
        {
            AutoArray<int> slots;
            int n = _magazineSlots.Size();
            slots.Resize(n);
            for (int i = 0; i < n; i++)
            {
                slots[i] = -1;
                Magazine* magazine = _magazineSlots[i]._magazine;
                if (magazine)
                {
                    for (int j = 0; j < _magazines.Size(); j++)
                    {
                        if (_magazines[j] == magazine)
                        {
                            slots[i] = j;
                            break;
                        }
                    }
                }
            }
            PARAM_CHECK(ar.SerializeArray("magazineSlots", slots, 1));
        }
        else if (ar.GetPass() == ParamArchive::PassSecond)
        {
            int n = 0;
            for (int i = 0; i < _weapons.Size(); i++)
            {
                WeaponType* weapon = _weapons[i];
                for (int j = 0; j < weapon->_muzzles.Size(); j++)
                {
                    MuzzleType* muzzle = weapon->_muzzles[j];
                    n += muzzle->_nModes;
                }
            }
            _magazineSlots.Resize(n);
            int slot = 0;
            for (int i = 0; i < _weapons.Size(); i++)
            {
                WeaponType* weapon = _weapons[i];

                // load shape
                weapon->ShapeAddRef();

                for (int j = 0; j < weapon->_muzzles.Size(); j++)
                {
                    MuzzleType* muzzle = weapon->_muzzles[j];
                    Magazine* magazine = nullptr;
                    for (int mode = 0; mode < muzzle->_nModes; mode++)
                    {
                        _magazineSlots[slot]._weapon = weapon;
                        _magazineSlots[slot]._muzzle = muzzle;
                        _magazineSlots[slot]._mode = mode;
                        _magazineSlots[slot]._magazine = magazine;
                        slot++;
                    }
                }
            }
            AutoArray<int> slots;
            ar.FirstPass();
            PARAM_CHECK(ar.SerializeArray("magazineSlots", slots, 1));
            AI_ERROR(slots.Size() == n);
            for (int i = 0; i < n; i++)
            {
                MagazineSlot& slot = _magazineSlots[i];
                int index = slots[i];
                if (index < 0 || index >= _magazines.Size())
                {
                    continue;
                }
                Magazine* magazine = _magazines[index];
                if (!magazine || !magazine->_type)
                {
                    continue;
                }
                if (!slot._muzzle->CanUse(magazine->_type))
                {
                    RptF("Cannot use magazine %s in muzzle %s", (const char*)magazine->_type->GetName(),
                         (const char*)slot._muzzle->GetName());
                    continue;
                }
                slot._magazine = magazine;
            }
            ar.SecondPass();

            if (n > 0 && _currentWeapon < 0)
            {
                SelectWeapon(0, true);
            }
            AutoReloadAll();
        }
    }

    // must be after RemoveAllWeapons
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        int currentWeapon = _currentWeapon;
        PARAM_CHECK(ar.Serialize("currentWeapon", currentWeapon, 1, -1))
        if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
        {
            SelectWeapon(currentWeapon, true);
        }
    }

    return LSOK;

    /* Note: need serialization ?

        Vector3 _lockedBeg; // where is the lock?

        // locking
        bool _locked :1,_tempLocked:1; // is locked?
        bool _isUpsideDown:1;
        bool _lockedSoldier; // type of lock
        Time _lastMovement; // when last movement prevented stopping
        float _shootVisible; // how much are we visible (1.0 default)
        float _shootTimeRest; // how long will current visibility last (before returning to 1.0)
        OLink<Object> _shootTarget; // what we fired at
        float _lockedRadius; // how large is the lock?
        //Ref<AILocker> _locker; // who locked - no real AI ...
        Ptr<AILocker> _locker; // who locked - no real AI ...
        SensorColID _sensorColID;
        AutoArray<MuzzleState> _muzzleState;
        OLink<FlashGunFire> _gunFlash;
        mutable VisibilityTrackerCache _visTracker;
        float _rearmCredit; // used during rearm
        // GoTo/FireAt
        float _avoidAside,_avoidAsideWanted; // obstacle avoidance offset
        Time _lastSimplePath; // formation pilot helper
        LinkTarget _fireTarget;

        // current attack test status
        // set during engage
        mutable LinkTarget _attackTarget; // which target
        mutable Time _attackEngageTime; // how is this information old
        mutable Time _attackRefreshTime; // last refresh of _attackDebug markerResult
        mutable Vector3 _attackAggresivePos;
        mutable Vector3 _attackEconomicalPos;
        mutable FireResult _attackAggresiveResult;
        mutable FireResult _attackEconomicalResult;
        FFEffects _ff; // result of ff simulation - only in SimCamera mode

        // support for getting in/out
        float _nearestEnemy; // nearest known enemy
    */
}

DEFINE_NETWORK_OBJECT_SIMPLE(UpdateEntityAIWeaponsMessage, UpdateEntityAIWeapons)

DEFINE_NET_INDICES_ERR(UpdateEntityAIWeapons, UPDATE_ENTITY_AI_WEAPONS_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateEntityAIWeapons)

namespace Poseidon
{

NetworkMessageFormat& UpdateEntityAIWeaponsMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    UPDATE_ENTITY_AI_WEAPONS_MSG(MSG_FORMAT_ERR)
    return format;
}

TMError UpdateEntityAIWeaponsMessage::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(_vehicle);
    return _vehicle->TransferMsgWeapons(ctx);
}

DEFINE_NET_INDICES_EX_ERR(UpdateDammageVehicleAI, UpdateDammageObject, UPDATE_DAMMAGE_VEHICLE_AI_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateDammageVehicleAI)

namespace Poseidon
{

DEFINE_NET_INDICES_EX_ERR(UpdateVehicleAI, UpdateVehicle, UPDATE_VEHICLE_AI_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateVehicleAI)

namespace Poseidon
{

NetworkMessageType EntityAI::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateDammage:
            return NMTUpdateDammageVehicleAI;
        case NMCUpdateGeneric:
            return NMTUpdateVehicleAI;
        default:
            return base::GetNMType(cls);
    }
}

NetworkMessageFormat& EntityAI::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateDammage:
            base::CreateFormat(cls, format);
            UPDATE_DAMMAGE_VEHICLE_AI_MSG(MSG_FORMAT_ERR)
            break;
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_VEHICLE_AI_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError EntityAI::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateDammage:
            TMCHECK(base::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateDammageVehicleAI*>(ctx.GetIndices()))
                const IndicesUpdateDammageVehicleAI* indices =
                    static_cast<const IndicesUpdateDammageVehicleAI*>(ctx.GetIndices());

                ITRANSF(isDead)
                if (ctx.IsSending())
                {
                    ITRANSF(hit)
                }
                else
                {
                    AUTO_STATIC_ARRAY(float, hit, 32);
                    TMCHECK(ctx.IdxTransfer(indices->hit, hit))
                    for (int i = 0; i < _hit.Size(); i++)
                    {
                        if (i < hit.Size())
                        {
                            ChangeHit(i, hit[i]);
                        }
                    }
                }
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateVehicleAI*>(ctx.GetIndices()))
                const IndicesUpdateVehicleAI* indices = static_cast<const IndicesUpdateVehicleAI*>(ctx.GetIndices());

                ITRANSF(pilotLight);
                if (ctx.IsSending())
                {
                    EntityAI* target = nullptr;
                    if (_fire._fireTarget)
                    {
                        target = _fire._fireTarget->idExact;
                    }
                    TMCHECK(ctx.IdxTransferRef(indices->fireTarget, target));
                }
                else
                {
                    _fire._fireTarget = nullptr;
                    EntityAI* target = nullptr;
                    TMCHECK(ctx.IdxTransferRef(indices->fireTarget, target));
                    if (target)
                    {
                        AIUnit* unit = CommanderUnit();
                        AIGroup* grp = unit ? unit->GetGroup() : nullptr;
                        if (grp)
                        {
                            _fire._fireTarget = grp->FindTarget(target);
                        }
                    }
                }
                UpdateEntityAIWeaponsMessage weapons(this);
                TMCHECK(ctx.IdxTransferObject(indices->weapons, weapons))
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

TMError EntityAI::TransferMsgWeapons(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesUpdateEntityAIWeapons*>(ctx.GetIndices()))
    const IndicesUpdateEntityAIWeapons* indices = static_cast<const IndicesUpdateEntityAIWeapons*>(ctx.GetIndices());

    ITRANSF(currentWeapon)
    // FIX
    bool initialUpdate;
    if (ctx.IsSending())
    {
        initialUpdate = ctx.GetInitialUpdate();
    }
    TMCHECK(ctx.IdxTransfer(indices->initialUpdate, initialUpdate))
    // magazines
    if (ctx.IsSending() || initialUpdate || !(GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
    {
        TMCHECK(ctx.IdxTransferObjArray(indices->magazines, _magazines))
    }
    else
    {
        AUTO_STATIC_ARRAY(MagazineNetworkInfo, magazines, 32);
        TMCHECK(ctx.IdxTransferArray(indices->magazines, magazines))
        int n = magazines.Size();
        RefArray<Magazine> oldMagazines = _magazines;
        int oldN = oldMagazines.Size();
        _magazines.Resize(n);
        for (int i = 0; i < n; i++)
        {
            int creator = magazines[i]._creator;
            int id = magazines[i]._id;
            int found = -1;
            if (i < oldN && oldMagazines[i]->_creator == creator && oldMagazines[i]->_id == id)
            {
                found = i;
            }
            else
            {
                for (int j = 0; j < oldN; j++)
                {
                    if (j != i && oldMagazines[j]->_creator == creator && oldMagazines[j]->_id == id)
                    {
                        found = j;
                        break;
                    }
                }
            }
            if (found >= 0)
            {
                _magazines[i] = oldMagazines[found];
            }
            else
            {
                _magazines[i] = new Magazine(MagazineTypes.New(magazines[i]._type));
                _magazines[i]->_creator = creator;
                _magazines[i]->_id = id;
                _magazines[i]->_reload = magazines[i]._reload;
                _magazines[i]->_reloadMagazine = magazines[i]._reloadMagazine;
            }
            _magazines[i]->_ammo = magazines[i]._ammo;
            _magazines[i]->_burstLeft = magazines[i]._burstLeft;
        }
    }
    if (ctx.IsSending())
    {
        // weapons
        AutoArray<RString> weapons;
        weapons.Resize(_weapons.Size());
        for (int i = 0; i < _weapons.Size(); i++)
        {
            weapons[i] = _weapons[i]->GetName();
        }
        TMCHECK(ctx.IdxTransfer(indices->weapons, weapons))
        // magazine slots
        AutoArray<int> slots;
        int n = _magazineSlots.Size();
        slots.Resize(n);
        for (int i = 0; i < n; i++)
        {
            slots[i] = -1;
            Magazine* magazine = _magazineSlots[i]._magazine;
            if (magazine)
            {
                for (int j = 0; j < _magazines.Size(); j++)
                {
                    if (_magazines[j] == magazine)
                    {
                        slots[i] = j;
                        break;
                    }
                }
            }
        }
        TMCHECK(ctx.IdxTransfer(indices->magazineSlots, slots));
    }
    else
    {
        // weapons
        AutoArray<RString> weapons;
        TMCHECK(ctx.IdxTransfer(indices->weapons, weapons));
        bool changed = weapons.Size() != _weapons.Size();
        if (!changed)
        {
            for (int i = 0; i < _weapons.Size(); i++)
            {
                if (weapons[i] != _weapons[i]->GetName())
                {
                    changed = true;
                    break;
                }
            }
        }
        if (changed)
        {
            RemoveAllWeapons();
            for (int i = 0; i < weapons.Size(); i++)
            {
                AddWeapon(weapons[i], true);
            }
        }
        // magazine slots
        // FIX
        if (initialUpdate || !(GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
        {
            AutoArray<int> slots;
            TMCHECK(ctx.IdxTransfer(indices->magazineSlots, slots));
            int n = _magazineSlots.Size();
            // FIX
            int m = slots.Size();
            AI_ERROR(m == n);
            for (int i = 0; i < n; i++)
            {
                MagazineSlot& slot = _magazineSlots[i];
                slot._magazine = nullptr;
                if (i >= m)
                {
                    continue;
                }
                int index = slots[i];
                if (index < 0 || index >= _magazines.Size())
                {
                    continue;
                }
                Magazine* magazine = _magazines[index];
                if (!magazine || !magazine->_type)
                {
                    continue;
                }
                if (!slot._muzzle->CanUse(magazine->_type))
                {
                    RptF("Cannot use magazine %s in muzzle %s", (const char*)magazine->_type->GetName(),
                         (const char*)slot._muzzle->GetName());
                    continue;
                }
                slot._magazine = magazine;
            }
        }

        if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this && _currentWeapon < 0)
        {
            GWorld->UI()->ResetVehicle(this);
        }
    }
    return TMOK;
}

float EntityAI::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateDammage:
        {
            error += base::CalculateError(ctx);
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateDammageVehicleAI*>(ctx.GetIndices()))
                const IndicesUpdateDammageVehicleAI* indices =
                    static_cast<const IndicesUpdateDammageVehicleAI*>(ctx.GetIndices());
                ICALCERR_NEQ(bool, isDead, ERR_COEF_STRUCTURE)
                // hitpoints
                AutoArray<float> hit;
                float hitError = 0;
                if (ctx.IdxTransfer(indices->hit, hit) == TMOK)
                {
                    int minSize = _hit.Size();
                    saturateMin(minSize, hit.Size());
                    hitError += _hit.Size() - minSize + hit.Size() - minSize;
                    for (int i = 0; i < minSize; i++)
                    {
                        hitError += fabs(_hit[i] - hit[i]);
                    }
                }
                error += hitError * ERR_COEF_STRUCTURE;
            }
        }
        break;
        case NMCUpdateGeneric:
        {
            error += base::CalculateError(ctx);
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateVehicleAI*>(ctx.GetIndices()))
                const IndicesUpdateVehicleAI* indices = static_cast<const IndicesUpdateVehicleAI*>(ctx.GetIndices());

                ICALCERR_NEQ(bool, pilotLight, ERR_COEF_VALUE_MAJOR)

                EntityAI* target = nullptr;
                if (_fire._fireTarget)
                {
                    target = _fire._fireTarget->idExact;
                }
                ICALCERRE_NEQREF(Object, fireTarget, target, ERR_COEF_MODE)

                int index = indices->weapons;
                if (index >= 0)
                {
                    NetworkMessageFormatBase* format = const_cast<NetworkMessageFormatBase*>(ctx.GetFormat());
                    NetworkMessageFormatItem& item = format->GetItem(index);
                    CHECK_ASSIGN(typeVal, item.defValue, const RefNetworkDataTyped<int>);
                    int type = typeVal.GetVal();
                    NetworkMessageFormatBase* subformat = ctx.GetComponent()->GetFormat((NetworkMessageType)type);

                    if (subformat)
                    {
                        const RefNetworkData& val = ctx.GetMessage()->values[index];
                        CHECK_ASSIGN(msgVal, val, const RefNetworkDataTyped<NetworkMessage>);
                        NetworkMessage& submsg = msgVal.GetVal();
                        NetworkMessageContext subctx(&submsg, subformat, ctx);
                        error += CalculateErrorWeapons(subctx);
                    }
                }
            }
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

float EntityAI::CalculateErrorWeapons(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesUpdateEntityAIWeapons*>(ctx.GetIndices()))
    const IndicesUpdateEntityAIWeapons* indices = static_cast<const IndicesUpdateEntityAIWeapons*>(ctx.GetIndices());

    float error = 0.0f;

    int currentWeapon;
    if (ctx.IdxTransfer(indices->currentWeapon, currentWeapon) == TMOK)
    {
        if (currentWeapon != _currentWeapon)
        {
            error += ERR_COEF_MODE;
        }
    }
    // weapons
    AutoArray<RString> weapons;
    if (ctx.IdxTransfer(indices->weapons, weapons) == TMOK)
    {
        bool changed = weapons.Size() != _weapons.Size();
        if (!changed)
        {
            for (int i = 0; i < _weapons.Size(); i++)
            {
                if (weapons[i] != _weapons[i]->GetName())
                {
                    changed = true;
                    break;
                }
            }
        }
        if (changed)
        {
            error += ERR_COEF_MODE;
        }
    }
    // magazines
    AUTO_STATIC_ARRAY(MagazineNetworkInfo, magazines, 32);
    if (ctx.IdxTransferArraySimple(indices->magazines, magazines) == TMOK)
    {
        bool changed = magazines.Size() != _magazines.Size();
        int ammoDiff = 0;
        if (!changed)
        {
            for (int i = 0; i < _magazines.Size(); i++)
            {
                if (magazines[i]._creator != _magazines[i]->_creator || magazines[i]._id != _magazines[i]->_id)
                {
                    changed = true;
                    break;
                }
                else
                {
                    ammoDiff += abs(magazines[i]._ammo - _magazines[i]->_ammo);
                }
            }
        }
        if (changed)
        {
            error += ERR_COEF_MODE;
        }
        else
        {
            error += ammoDiff * ERR_COEF_VALUE_MINOR;
        }
    }

    // magazine slots
    AutoArray<int> slots;
    if (ctx.IdxTransfer(indices->magazineSlots, slots) == TMOK)
    {
        bool changed = slots.Size() != _magazineSlots.Size();
        if (!changed)
        {
            for (int i = 0; i < _magazineSlots.Size(); i++)
            {
                if (slots[i] >= _magazines.Size())
                {
                    changed = true;
                    break;
                }
                else if (slots[i] >= 0)
                {
                    if (_magazineSlots[i]._magazine != _magazines[slots[i]])
                    {
                        changed = true;
                        break;
                    }
                }
                else
                {
                    if (_magazineSlots[i]._magazine != nullptr)
                    {
                        changed = true;
                        break;
                    }
                }
            }
        }
        if (changed)
        {
            error += ERR_COEF_MODE;
        }
    }
    return error;
}

HitPoint::HitPoint()
{
    _selection = -1; // selection index (in Hitpoints LOD)
    _armor = 10;     // hit point armor
    _index = -1;
    _indexCC.Clear();
    _material = -1;
    _passThrough = 1;
    _invArmor = 1 / _armor;
}

HitPoint::HitPoint(Shape* shape, const char* name, const char* altName, float armor, int material)
{
    _selection = shape ? shape->FindNamedSel(name, altName) : -1;
    _armor = armor;
    _index = -1;
    _indexCC.Clear();
    _material = material;
    _passThrough = 1;
    if (_selection >= 0)
    {
        const NamedSelection& sel = shape->NamedSel(_selection);
        _armor *= sel.Size();
    }

    _invArmor = 1 / _armor;
}

HitPoint::HitPoint(Shape* shape, const ParamEntry& par, float armor)
{
    RStringB selName = par >> "name";
    _selection = shape ? shape->FindNamedSel(selName) : -1;
    _armor = par >> "armor";
    _armor *= armor;
    _material = par >> "material";
    _index = -1;
    _indexCC.Clear();
    _passThrough = par >> "passThrough";

    if (_selection >= 0)
    {
        const NamedSelection& sel = shape->NamedSel(_selection);
        _armor *= sel.Size();
    }

    _invArmor = 1 / _armor;
}

} // namespace Poseidon

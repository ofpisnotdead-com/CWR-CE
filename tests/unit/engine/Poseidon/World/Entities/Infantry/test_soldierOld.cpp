#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>

#include <cstring>

using namespace Poseidon;

namespace
{
ParamFile ParseConfig(const char* text)
{
    ParamFile pf;
    QIStream in(text, static_cast<int>(strlen(text)));
    pf.Parse(in);
    return pf;
}

WeaponType MakeWeapon(const ParamEntry& entry, int weaponType)
{
    WeaponType weapon;
    weapon._parClass = &entry;
    weapon._weaponType = weaponType;
    return weapon;
}

Ref<WeaponType> MakeWeaponRef(const ParamEntry& entry, int weaponType)
{
    Ref<WeaponType> weapon = new WeaponType();
    weapon->_parClass = &entry;
    weapon->_weaponType = weaponType;
    return weapon;
}
} // namespace

TEST_CASE("soldierOld.hpp compiles", "[Infantry][soldierOld]")
{
    SUCCEED("header included successfully");
}

TEST_CASE("WeaponType::IsBinocular centralizes canonical binocular detection", "[Infantry][soldierOld][binocular]")
{
    ParamFile pf = ParseConfig("class Binocular {};\n"
                               "class CustomBinocular {};\n"
                               "class NVGoggles {};\n");

    const WeaponType binocular = MakeWeapon(pf >> "Binocular", MaskSlotBinocular);
    REQUIRE(binocular.IsBinocular());

    const WeaponType customBinocular = MakeWeapon(pf >> "CustomBinocular", MaskSlotBinocular);
    REQUIRE_FALSE(customBinocular.IsBinocular());

    const WeaponType nvgInBinocularSlot = MakeWeapon(pf >> "NVGoggles", MaskSlotBinocular);
    REQUIRE_FALSE(nvgInBinocularSlot.IsBinocular());

    const WeaponType binocularWithoutSlot = MakeWeapon(pf >> "Binocular", MaskSlotPrimary);
    REQUIRE_FALSE(binocularWithoutSlot.IsBinocular());
}

TEST_CASE("FindBinocularWeapon skips inherited binocular-slot equipment", "[Infantry][soldierOld][binocular]")
{
    ParamFile pf = ParseConfig("class Binocular {};\n"
                               "class NVGoggles {};\n"
                               "class Phone {};\n");

    Ref<WeaponType> nvg = MakeWeaponRef(pf >> "NVGoggles", MaskSlotBinocular);
    Ref<WeaponType> phone = MakeWeaponRef(pf >> "Phone", MaskSlotBinocular);
    Ref<WeaponType> binocular = MakeWeaponRef(pf >> "Binocular", MaskSlotBinocular);

    RefArray<WeaponType> weapons;
    weapons.Add(nvg);
    weapons.Add(phone);
    weapons.Add(binocular);

    REQUIRE(FindBinocularWeapon(weapons) == binocular);

    RefArray<WeaponType> nonBinocularWeapons;
    nonBinocularWeapons.Add(nvg);
    nonBinocularWeapons.Add(phone);

    REQUIRE(FindBinocularWeapon(nonBinocularWeapons) == nullptr);
}

TEST_CASE("stealth stance exposure tolerates soldiers without a group", "[Infantry][soldierOld]")
{
    REQUIRE(Poseidon::SoldierStealthStanceExposure(nullptr, false, 0, 0) == 0.0f);
    REQUIRE(Poseidon::SoldierStealthStanceExposure(nullptr, true, 0, 0) == 0.0f);
}

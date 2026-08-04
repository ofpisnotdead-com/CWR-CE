#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

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

TEST_CASE("FindBinocularWeapon prefers the Binocular over other slot items", "[Infantry][soldierOld][binocular]")
{
    ParamFile pf = ParseConfig("class Binocular {};\n"
                               "class NVGoggles {};\n"
                               "class Phone {};\n"
                               "class M16 {};\n");

    Ref<WeaponType> nvg = MakeWeaponRef(pf >> "NVGoggles", MaskSlotBinocular);
    Ref<WeaponType> phone = MakeWeaponRef(pf >> "Phone", MaskSlotBinocular);
    Ref<WeaponType> binocular = MakeWeaponRef(pf >> "Binocular", MaskSlotBinocular);
    Ref<WeaponType> rifle = MakeWeaponRef(pf >> "M16", MaskSlotPrimary);

    RefArray<WeaponType> weapons;
    weapons.Add(nvg);
    weapons.Add(phone);
    weapons.Add(binocular);

    REQUIRE(FindBinocularWeapon(weapons) == binocular);

    RefArray<WeaponType> phoneOnly;
    phoneOnly.Add(rifle);
    phoneOnly.Add(phone);

    REQUIRE(FindBinocularWeapon(phoneOnly) == phone);

    RefArray<WeaponType> noSlotItems;
    noSlotItems.Add(rifle);

    REQUIRE(FindBinocularWeapon(noSlotItems) == nullptr);
}

TEST_CASE("stealth stance exposure tolerates soldiers without a group", "[Infantry][soldierOld]")
{
    REQUIRE(Poseidon::SoldierStealthStanceExposure(nullptr, false, 0, 0) == 0.0f);
    REQUIRE(Poseidon::SoldierStealthStanceExposure(nullptr, true, 0, 0) == 0.0f);
}

TEST_CASE("Man flare lookup validates the selected weapon slot", "[Infantry][soldierOld]")
{
    const std::filesystem::path source =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine/Poseidon/World/Entities/Infantry/SoldierOld.cpp";
    std::ifstream input(source);
    REQUIRE(input.is_open());

    std::stringstream stream;
    stream << input.rdbuf();
    const std::string body = stream.str();
    const std::size_t begin = body.find("bool Man::HasFlares");
    REQUIRE(begin != std::string::npos);
    const std::size_t end = body.find("Matrix4 Man::InsideCamera", begin);
    REQUIRE(end != std::string::npos);

    const std::string function = body.substr(begin, end - begin);
    CHECK(function.find("_currentWeapon >= 0") != std::string::npos);
    CHECK(function.find("_currentWeapon < NMagazineSlots()") != std::string::npos);
    CHECK(function.find("if (slot._muzzle)") != std::string::npos);
}

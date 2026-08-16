// Fixture for editor_select_island_nordic_chars.test.sqf. NordicTest has no base
// class: CfgWorlds/CfgWorldList are add-only (access=1), but the stock worlds are
// access=3 (fully locked) and inheriting from one here breaks the overlay's
// standalone parse. worldName points at the Demo package's own terrain so no .wrp
// is needed. description is a real on-disk CP1252 byte for "Malmö" (0xF6) -- the
// config parser has no \xNN escape, so this must be a raw byte, not a UTF-8
// encoding of it (same approach as tests/fixtures/mods-configmerge).
class CfgPatches
{
    class NordicNames
    {
        units[] = {};
        weapons[] = {};
        requiredVersion = 0.1;
    };
};
class CfgWorldList
{
    class NordicTest {};
};
class CfgWorlds
{
    class NordicTest
    {
        worldName = "\demo\demo.wrp";
        icon = "_abel.paa";
        description = "Malmö";
    };
};
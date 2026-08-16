// Fixture for card #146 crash #3: a CfgVehicles class with an empty model.
// Creating this shape-requiring vehicle with an empty model returns null.
class CfgPatches
{
    class BadModelFixture
    {
        units[] = {"BadModelUnit"};
        weapons[] = {};
        requiredVersion = 0.1;
    };
};
class CfgVehicles
{
    class SyntheticSoldierWest {};
    class BadModelUnit : SyntheticSoldierWest
    {
        scope = 2;
        displayName = "Bad Model Unit";
        model = "";
    };
};

// Fixture: a mod-supplied weapon occupying the binocular slot, the slot the
// hand proxies draw from. The model is empty because the test asserts which
// class the proxies resolve, not how it renders.
class CfgPatches
{
    class HandItemFixture
    {
        units[] = {};
        weapons[] = {"SyntheticHandItem"};
        requiredVersion = 0.1;
    };
};
class CfgWeapons
{
    class Binocular {};
    class SyntheticHandItem : Binocular
    {
        scopeWeapon = 1;
        displayName = "Synthetic Hand Item";
        model = "";
        modelOptics = "";
    };
};

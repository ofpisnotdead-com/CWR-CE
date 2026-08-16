class CfgPatches
{
	class Test_Mod_Cfg
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 1.0;
	};
};
class CfgVehicles
{
	class Car {};
	class ModTank : Car
	{
		displayName = "Mod Tank";
	};
};
class CfgGroups
{
	class West
	{
		class Infantry
		{
			class GrpShared
			{
				name = "Shared Squad";
			};
			class GrpModOnly
			{
				name = "Mod Squad";
			};
		};
	};
};

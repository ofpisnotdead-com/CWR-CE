class CfgPatches
{
	class Test_Base_Cfg
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 1.0;
	};
};
class CfgVehicles
{
	class Car {};
	class VanillaJeep : Car
	{
		displayName = "Vanilla Jeep";
	};
};
class CfgGroups
{
	class West
	{
		class Infantry
		{
			class GrpVanilla
			{
				name = "Vanilla Squad";
			};
			class GrpShared
			{
				name = "Shared Squad";
			};
		};
	};
};

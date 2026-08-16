#define APP_VERSION_MAJOR 3
#define APP_VERSION_MINOR 03
#define APP_VERSION_TEXT "3.03"

#if _GALATEA
#define APP_NAME				"Galatea"
#define APP_NAME_SHORT	"Galatea"
#else
#define APP_NAME				"Arma: Cold War Assault - Remastered CE"
#define APP_NAME_SHORT	"CWR-CE"
#endif

#define APP_VERSION_NUM	APP_VERSION_MAJOR*100+APP_VERSION_MINOR

#define FILEVER					APP_VERSION_MAJOR,0,0,APP_VERSION_MINOR
#define PRODUCTVER			APP_VERSION_MINOR,0,0,APP_VERSION_MINOR

#define STRFILEVER			APP_VERSION_TEXT "\0"
#define STRPRODUCTVER		APP_VERSION_TEXT "\0" 

#define PRODUCTNAME			APP_NAME "\0"
#define INTERNALNAME		APP_NAME_SHORT "\0"


namespace Poseidon::Foundation
{
} // namespace Poseidon::Foundation

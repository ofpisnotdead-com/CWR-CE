#ifndef POSEIDON_TEST_HARNESS_BUILTINS_HPP
#define POSEIDON_TEST_HARNESS_BUILTINS_HPP

// Shared harness command bodies reused by multiple apps. Each function
// registers one command (or a related pair) on the passed server — apps opt
// in to whichever builtins fit their role.

#include <string>

struct cJSON;

namespace Poseidon { class UITestEngine; }
using Poseidon::UITestEngine;

namespace Poseidon::Dev {

class HarnessServer;

namespace HarnessBuiltins
{

/// `screenshot` — capture PNG via GEngine::Screenshot(path). Client apps only.
void RegisterScreenshot(HarnessServer& hs);

/// `eval` + `exec` — SQF via GWorld->GetGameState(). Any app with a world.
void RegisterSqf(HarnessServer& hs);

/// `key` + `key_up` — inject SDL scancodes into the event queue. Any SDL app.
void RegisterKeyInjection(HarnessServer& hs);

/// `debug_cmd` — run a line through the DebugCommands registry, the same
/// dispatch the dev console uses, and return its output. Lets a scripted
/// client drive any registered dev command without keyboard input.
void RegisterDebugCommand(HarnessServer& hs);

/// `query` — answer `what=display` with active display IDD + a per-control
/// dump (idc, type, visible, enabled, pos, text).  UI-focused; apps that
/// need additional `what` keys (network, mission, ngs) dispatch them
/// before falling through to AnswerNetworkQuery.
void RegisterUIQuery(HarnessServer& hs, ::UITestEngine& uiTestEngine);

/// `wait_display` — block (via re-enqueue with decreasing timeout) until
/// the active display's IDD matches the requested value, or timeout.
/// The trident scenario runner uses this to gate test steps until the
/// next screen has loaded.
void RegisterWaitDisplay(HarnessServer& hs, ::UITestEngine& uiTestEngine);

/// Answer a `query` request against the NetworkManager for `players`, `mission`,
/// `ngs`, and server-only `connections`. Returns the response JSON if `what` is
/// one of those targets, or an
/// empty string if not — callers (Game/Server) dispatch app-specific `what`
/// values first and fall through here.
std::string AnswerNetworkQuery(const char* what);

/// Answer master-server browser queries (`master_server_server_detail`,
/// `master_server_mod_detail`, `master_server_mod_versions`,
/// `master_server_mod_servers`) against the MasterServerService. `root` carries
/// the query parameters. Returns empty if `what` isn't a master-server target.
std::string AnswerServiceQuery(const char* what, cJSON* root);

} // namespace HarnessBuiltins
} // namespace Poseidon::Dev

#endif // POSEIDON_TEST_HARNESS_BUILTINS_HPP

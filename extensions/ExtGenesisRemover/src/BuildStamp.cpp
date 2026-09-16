#include "MatteIpc.h"

// Dedicated TU so stamp bumps always rebuild DLL + MatteHost (header-only
// constexpr was leaving stale .obj files with the old string embedded).
namespace genesis::matte_ipc {

char const kBuildStamp[] = "remover-demo-v63f3-force-dll-rebuild-20260916";

}  // namespace genesis::matte_ipc

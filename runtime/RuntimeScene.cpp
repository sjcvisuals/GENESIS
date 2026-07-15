#include "runtime/RuntimeScene.hpp"

#include "scene/GnsPackage.hpp"
#include "scene/SceneRuntime.hpp"

namespace genesis {

bool tryLoadRuntimeScene(SceneRuntime& target, const RuntimeSceneResource& resource,
                         std::string* out_error) {
    try {
        switch (resource.kind) {
        case RuntimeSceneKind::Demo:
            target.loadDemo();
            break;
        case RuntimeSceneKind::Document:
            target.loadDocumentFile(resource.path);
            break;
        case RuntimeSceneKind::Gltf:
            target.loadGltfFile(resource.path);
            break;
        case RuntimeSceneKind::Gns:
            target.loadGnsFile(resource.path);
            break;
        }
        target.updateTransforms();
        return true;
    } catch (const std::exception& ex) {
        if (out_error) {
            *out_error = ex.what();
        }
        return false;
    }
}

} // namespace genesis

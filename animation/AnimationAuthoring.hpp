#pragma once

#include "animation/AnimationClip.hpp"
#include "scene/Scene.hpp"

namespace genesis {

// Insert or replace a key at `time_seconds` (within a small epsilon). Keys stay sorted.
void upsertVec3Key(std::vector<AnimationVec3Key>& keys, float time_seconds, const glm::vec3& value);
void upsertQuatKey(std::vector<AnimationQuatKey>& keys, float time_seconds, const glm::quat& value);
bool removeVec3KeyAt(std::vector<AnimationVec3Key>& keys, float time_seconds);
bool removeQuatKeyAt(std::vector<AnimationQuatKey>& keys, float time_seconds);

AnimationChannel* findOrCreateChannel(AnimationClip& clip, NodeId node_id, const std::string& node_name);

// Writes the node's current local TRS into keys at `time_seconds` for the selected tracks.
void keyframeNode(AnimationClip& clip, const Node& node, float time_seconds,
                  AnimationTrack tracks = AnimationTrack::Transform);

void removeKeysAtTime(AnimationClip& clip, NodeId node_id, float time_seconds,
                      AnimationTrack tracks = AnimationTrack::Transform);

// duration covers the last key (keeps a usable range when only t=0 keys exist).
void refreshClipDuration(AnimationClip& clip);

// Bind channels to live nodes (id from path/name, keep name in sync).
void bindClipChannels(Scene& scene, AnimationClip& clip);

} // namespace genesis

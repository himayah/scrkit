#pragma once
// Classifies GPU capability from glGetString(GL_VENDOR/RENDERER/VERSION)
// strings into a particle-count preset for the "Auto" setting (要件.txt §6).
// This module never calls OpenGL itself -- the platform layer queries the
// strings from a real GL context and passes them in here, which keeps this
// classification logic testable without a GPU.

#include <string>

#include "ConfigModel.h"

namespace core {

// Returns a concrete particle count (not the Auto enum value) appropriate
// for the given GPU identification strings. Comparisons are case-insensitive
// substring matches against known low-end / software-renderer markers;
// anything unrecognized is treated as a capable discrete/integrated GPU and
// gets the Mid/High tier rather than failing.
int ClassifyGpuParticleCount(const std::string& vendor, const std::string& renderer,
                              const std::string& version);

} // namespace core

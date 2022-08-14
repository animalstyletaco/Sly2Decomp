#pragma once

/*!
 * @file versions.h
 * Version numbers for GOAL Language, Kernel, etc...
 */

#include <string>

namespace versions {
// language version (OpenGOAL)
constexpr int32_t SLY_VERSION_MAJOR = 0;
constexpr int32_t SLY_VERSION_MINOR = 0;

namespace jak1 {
// these versions are from the game
constexpr uint32_t ART_FILE_VERSION = 6;
constexpr uint32_t LEVEL_FILE_VERSION = 30;
constexpr uint32_t RES_FILE_VERSION = 1;
constexpr uint32_t TX_PAGE_VERSION = 7;
}  // namespace jak1

}  // namespace versions

// GOAL kernel version (OpenGOAL changes this version from the game's version)
constexpr int KERNEL_VERSION_MAJOR = 0;
constexpr int KERNEL_VERSION_MINOR = 0;

// OVERLORD version returned by an RPC
constexpr int IRX_VERSION_MAJOR = 0;
constexpr int IRX_VERSION_MINOR = 0;

enum class GameVersion { Sly1 = 1, Sly2 = 2 };

template <typename T>
struct PerGameVersion {
  constexpr PerGameVersion(T sly1, T sly2) : data{sly1, sly2} {}
  constexpr T operator[](GameVersion v) const { return data[(int)v - 1]; }
  T data[2];
};

constexpr PerGameVersion<const char*> game_version_names = {"sly1", "sly2"};

GameVersion game_name_to_version(const std::string& name);
bool valid_game_version(const std::string& name);

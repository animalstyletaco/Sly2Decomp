#include "versions.h"

#include "common/util/Assert.h"

#include "fmt/core.h"

GameVersion game_name_to_version(const std::string& name) {
  if (name == "sly1") {
    return GameVersion::Sly1;
  } else if (name == "sly2") {
    return GameVersion::Sly2;
  } else {
    ASSERT_MSG(false, fmt::format("invalid game name: {}", name));
  }
}

bool valid_game_version(const std::string& name) {
  return name == "sly1" || name == "sly2";
}

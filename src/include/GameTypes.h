/**
 * @file GameTypes.h
 * @brief 游戏层基础类型
 */
#pragma once

#include <cstdint>

enum class EntityType : uint8_t {
  kPlayer = 1,
  kMonster = 2,
};

enum Direction : uint8_t {
  kDirUp = 0,
  kDirDown = 1,
  kDirLeft = 2,
  kDirRight = 3,
};

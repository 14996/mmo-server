/**
 * @file GameConfig.h
 * @brief 数值配置、地图、怪物、技能表
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr int kCellSize = 8;  // AOI 九宫格边长（单位：格）
constexpr int kMaxLevel = 60;
constexpr int kMaxNameBytes = 24;
constexpr int kMaxPasswordBytes = 32;
constexpr int kMaxChatBytes = 200;

constexpr int64_t kPlayerMoveCooldownMs = 150;
constexpr int64_t kPlayerAttackCooldownMs = 800;
constexpr int64_t kPlayerChatCooldownMs = 1000;
constexpr int64_t kPlayerRespawnMs = 5000;
constexpr int64_t kMonsterRespawnMs = 8000;
constexpr int64_t kOfflineRetainMs = 60000;
constexpr int64_t kSaveIntervalMs = 30000;
constexpr int64_t kPlayerRegenIntervalMs = 5000;

struct MonsterSpawnConfig {
  std::string name;
  int level{1};
  int max_hp{50};
  int atk{5};
  int def{0};
  int exp_reward{10};
  int gold_reward{1};
  int x{0};
  int y{0};
  int aggro_range{8};
  int attack_range{1};
};

struct PortalConfig {
  int x{0};
  int y{0};
  int target_map{0};
  int target_x{0};
  int target_y{0};
};

struct MapConfig {
  int id{0};
  std::string name;
  int width{0};
  int height{0};
  int spawn_x{0};
  int spawn_y{0};
  std::vector<PortalConfig> portals;
  std::vector<MonsterSpawnConfig> monsters;
};

struct SkillConfig {
  uint32_t id{0};
  std::string name;
  int mp_cost{0};
  int64_t cooldown_ms{0};
  int range{1};
  int flat_damage{0};
  double multiplier{1.0};
};

std::vector<MapConfig> BuildMapConfigs();
const SkillConfig *FindSkill(uint32_t skill_id);

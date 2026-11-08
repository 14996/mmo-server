/**
 * @file GameConfig.cpp
 */
#include "GameConfig.h"

#include <algorithm>

namespace {

MonsterSpawnConfig Monster(const std::string &name, int level, int max_hp, int atk,
                           int def, int exp_reward, int gold_reward, int x, int y,
                           int aggro_range = 8) {
  MonsterSpawnConfig config;
  config.name = name;
  config.level = level;
  config.max_hp = max_hp;
  config.atk = atk;
  config.def = def;
  config.exp_reward = exp_reward;
  config.gold_reward = gold_reward;
  config.x = x;
  config.y = y;
  config.aggro_range = aggro_range;
  config.attack_range = 1;
  return config;
}

}  // namespace

std::vector<MapConfig> BuildMapConfigs() {
  std::vector<MapConfig> maps;

  // 地图 1：青石村（新手区）
  {
    MapConfig map;
    map.id = 1;
    map.name = "青石村";
    map.width = 96;
    map.height = 64;
    map.spawn_x = 10;
    map.spawn_y = 10;
    map.portals.push_back({16, 10, 2, 10, 10});
    map.monsters = {
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 24, 12, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 30, 20, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 38, 14, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 45, 25, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 54, 18, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 62, 30, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 72, 22, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 82, 42, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 20, 34, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 34, 46, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 48, 38, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 58, 48, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 68, 12, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 76, 36, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 86, 20, 7),
        Monster("史莱姆", 1, 50, 6, 0, 20, 3, 90, 50, 7),
    };
    maps.push_back(std::move(map));
  }

  // 地图 2：幽暗森林（练级区）
  {
    MapConfig map;
    map.id = 2;
    map.name = "幽暗森林";
    map.width = 128;
    map.height = 80;
    map.spawn_x = 10;
    map.spawn_y = 10;
    map.portals.push_back({6, 10, 1, 16, 10});
    map.portals.push_back({120, 70, 3, 10, 10});
    map.monsters = {
        Monster("野狼", 2, 90, 10, 1, 45, 8, 40, 20, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 50, 28, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 60, 18, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 68, 36, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 78, 24, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 88, 44, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 96, 30, 9),
        Monster("野狼", 2, 90, 10, 1, 45, 8, 104, 52, 9),
        Monster("头狼", 5, 350, 20, 4, 200, 60, 60, 50, 10),
    };
    maps.push_back(std::move(map));
  }

  // 地图 3：熔岩矿洞（高级区）
  {
    MapConfig map;
    map.id = 3;
    map.name = "熔岩矿洞";
    map.width = 112;
    map.height = 80;
    map.spawn_x = 10;
    map.spawn_y = 10;
    map.portals.push_back({6, 10, 2, 120, 70});
    map.monsters = {
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 36, 20, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 46, 26, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 56, 16, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 64, 34, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 74, 22, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 84, 40, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 92, 28, 9),
        Monster("骷髅兵", 4, 150, 16, 3, 85, 18, 100, 50, 9),
        Monster("矿洞守卫", 7, 800, 30, 8, 500, 150, 70, 56, 11),
    };
    maps.push_back(std::move(map));
  }

  return maps;
}

const SkillConfig *FindSkill(uint32_t skill_id) {
  static const std::vector<SkillConfig> skills = {
      {1001, "重击", 10, 5000, 1, 5, 2.0},
      {1002, "火球术", 15, 3000, 6, 3, 1.5},
  };
  auto it = std::find_if(skills.begin(), skills.end(),
                         [skill_id](const SkillConfig &skill) {
                           return skill.id == skill_id;
                         });
  return it == skills.end() ? nullptr : &(*it);
}

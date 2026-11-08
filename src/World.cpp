/**
 * @file World.cpp
 * @brief 精简 MMORPG 世界逻辑实现
 */
#include "World.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "util.h"

using namespace proto;

namespace {

int ClampInt(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

uint8_t EntityTypeToWire(EntityType type) {
  return type == EntityType::kPlayer ? 1 : 2;
}

}  // namespace

// ---------------------------------------------------------------- MapRuntime

void World::MapRuntime::Init(const MapConfig &cfg) {
  config = cfg;
  cells_x = (config.width + kCellSize - 1) / kCellSize;
  cells_y = (config.height + kCellSize - 1) / kCellSize;
  cells.assign(static_cast<size_t>(cells_x) * static_cast<size_t>(cells_y), {});
}

int World::MapRuntime::CellIndex(int x, int y) const {
  int cell_x = x / kCellSize;
  int cell_y = y / kCellSize;
  cell_x = ClampInt(cell_x, 0, cells_x - 1);
  cell_y = ClampInt(cell_y, 0, cells_y - 1);
  return cell_y * cells_x + cell_x;
}

void World::MapRuntime::AddEntity(uint64_t id, int x, int y) {
  cells[static_cast<size_t>(CellIndex(x, y))].insert(id);
}

void World::MapRuntime::RemoveEntity(uint64_t id, int x, int y) {
  cells[static_cast<size_t>(CellIndex(x, y))].erase(id);
}

std::unordered_set<uint64_t> World::MapRuntime::CollectNeighborhoodIds(int cell_x,
                                                                       int cell_y) const {
  std::unordered_set<uint64_t> ids;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cell_x + dx;
      int ny = cell_y + dy;
      if (nx < 0 || nx >= cells_x || ny < 0 || ny >= cells_y) {
        continue;
      }
      const auto &cell = cells[static_cast<size_t>(ny * cells_x + nx)];
      ids.insert(cell.begin(), cell.end());
    }
  }
  return ids;
}

// ---------------------------------------------------------------- lifecycle

World::World(SendFn send) : send_(std::move(send)), rng_(std::random_device{}()) {
  const int64_t now = NowMs();
  last_save_ms_ = now;
  last_log_ms_ = now;
  InitMaps();
  Load();
}

World::~World() = default;

void World::InitMaps() {
  for (auto &config : BuildMapConfigs()) {
    MapRuntime runtime;
    runtime.Init(config);
    maps_[config.id] = std::move(runtime);
  }

  for (auto &item : maps_) {
    int map_id = item.first;
    MapRuntime &map = item.second;
    for (const auto &spawn : map.config.monsters) {
      auto monster = std::make_unique<Monster>();
      monster->id = next_monster_id_++;
      monster->type = EntityType::kMonster;
      monster->name = spawn.name;
      monster->map_id = map_id;
      monster->x = spawn.x;
      monster->y = spawn.y;
      monster->spawn_x = spawn.x;
      monster->spawn_y = spawn.y;
      monster->level = spawn.level;
      monster->max_hp = spawn.max_hp;
      monster->hp = spawn.max_hp;
      monster->atk = spawn.atk;
      monster->def = spawn.def;
      monster->aggro_range = spawn.aggro_range;
      monster->attack_range = spawn.attack_range;
      monster->exp_reward = spawn.exp_reward;
      monster->gold_reward = spawn.gold_reward;
      monster->alive = true;
      monster->in_world = true;
      map.AddEntity(monster->id, monster->x, monster->y);
      monsters_[monster->id] = std::move(monster);
    }
  }
  LogInfo("world initialized: maps=%zu monsters=%zu", maps_.size(), monsters_.size());
}

void World::Load() {
  std::ifstream in(data_file_);
  if (!in.is_open()) {
    LogInfo("no save file at %s, starting fresh", data_file_.c_str());
    return;
  }

  std::string line;
  int loaded = 0;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
      size_t pos = line.find('|', start);
      if (pos == std::string::npos) {
        fields.push_back(line.substr(start));
        break;
      }
      fields.push_back(line.substr(start, pos - start));
      start = pos + 1;
    }
    if (fields.size() < 11) {
      LogWarn("skip malformed save line");
      continue;
    }

    try {
      uint64_t id = std::stoull(fields[0]);
      std::string name = fields[1];
      if (name.empty() || name.size() > kMaxNameBytes || players_.count(id) != 0 ||
          name_to_id_.count(name) != 0) {
        LogWarn("skip invalid/duplicate character id=%llu", static_cast<unsigned long long>(id));
        continue;
      }

      auto player = std::make_unique<Player>();
      player->id = id;
      player->type = EntityType::kPlayer;
      player->name = name;
      player->password_hash = fields[2];
      player->level = ClampInt(std::stoi(fields[3]), 1, kMaxLevel);
      player->exp = std::max(0, std::stoi(fields[4]));
      player->hp = std::max(0, std::stoi(fields[5]));
      player->mp = std::max(0, std::stoi(fields[6]));
      player->gold = std::max(0, std::stoi(fields[7]));
      player->map_id = std::stoi(fields[8]);
      int x = std::stoi(fields[9]);
      int y = std::stoi(fields[10]);

      MapRuntime *map = FindMap(player->map_id);
      if (map == nullptr) {
        player->map_id = 1;
        map = FindMap(1);
      }
      if (map != nullptr) {
        player->x = ClampInt(x, 0, map->config.width - 1);
        player->y = ClampInt(y, 0, map->config.height - 1);
      }

      RecomputeStats(*player);
      player->hp = std::min(player->hp, player->max_hp);
      player->mp = std::min(player->mp, player->max_mp);
      player->alive = player->hp > 0;
      player->in_world = false;
      player->online = false;

      if (id >= next_player_id_) {
        next_player_id_ = id + 1;
      }
      name_to_id_[name] = id;
      players_[id] = std::move(player);
      ++loaded;
    } catch (const std::exception &ex) {
      LogWarn("skip save line: %s", ex.what());
    }
  }
  LogInfo("loaded %d characters from %s", loaded, data_file_.c_str());
}

void World::SaveAll() {
  std::filesystem::path path(data_file_);
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  const std::string tmp_file = data_file_ + ".tmp";
  {
    std::ofstream out(tmp_file, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      LogError("cannot open save file %s", tmp_file.c_str());
      return;
    }
    for (const auto &item : players_) {
      const Player &player = *item.second;
      out << player.id << '|' << player.name << '|' << player.password_hash << '|'
          << player.level << '|' << player.exp << '|' << player.hp << '|'
          << player.mp << '|' << player.gold << '|' << player.map_id << '|'
          << player.x << '|' << player.y << '\n';
    }
  }

  std::filesystem::remove(data_file_, ec);
  std::filesystem::rename(tmp_file, data_file_, ec);
  if (ec) {
    LogError("save rename failed: %s", ec.message().c_str());
  }
}

// ---------------------------------------------------------------- session

uint64_t World::Login(const std::string &name, const std::string &password,
                      std::string &error, bool &is_new) {
  is_new = false;
  error.clear();

  if (name.empty() || name.size() > kMaxNameBytes) {
    error = "角色名长度需在 1~24 字节之间";
    return 0;
  }
  for (unsigned char ch : name) {
    if (ch < 0x20 || ch == '|') {
      error = "角色名包含非法字符";
      return 0;
    }
  }
  if (password.empty() || password.size() > kMaxPasswordBytes) {
    error = "密码长度需在 1~32 字节之间";
    return 0;
  }

  const std::string password_hash = std::to_string(Fnv1a64(name + ":" + password));

  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    Player *player = FindPlayer(it->second);
    if (player == nullptr) {
      error = "角色数据异常";
      return 0;
    }
    if (player->online) {
      error = "该角色已在线，请勿重复登录";
      return 0;
    }
    if (player->password_hash != password_hash) {
      error = "密码错误";
      return 0;
    }
    player->online = true;
    player->offline_deadline_ms = 0;
    LogInfo("login ok id=%llu name=%s level=%d",
            static_cast<unsigned long long>(player->id), player->name.c_str(),
            player->level);
    return player->id;
  }

  uint64_t id = next_player_id_++;
  auto player = std::make_unique<Player>();
  player->id = id;
  player->type = EntityType::kPlayer;
  player->name = name;
  player->password_hash = password_hash;
  player->level = 1;
  player->exp = 0;
  player->gold = 0;
  RecomputeStats(*player);
  player->hp = player->max_hp;
  player->mp = player->max_mp;
  player->map_id = 1;
  MapRuntime *map = FindMap(1);
  if (map != nullptr) {
    player->x = map->config.spawn_x;
    player->y = map->config.spawn_y;
  }
  player->online = true;
  player->in_world = false;

  name_to_id_[name] = id;
  players_[id] = std::move(player);
  is_new = true;
  LogInfo("create character id=%llu name=%s", static_cast<unsigned long long>(id),
          name.c_str());
  return id;
}

void World::EnterGame(uint64_t player_id) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr) {
    return;
  }

  MapRuntime *map = FindMap(player->map_id);
  if (map == nullptr) {
    player->map_id = 1;
    map = FindMap(1);
  }
  if (map == nullptr) {
    return;
  }
  player->x = ClampInt(player->x, 0, map->config.width - 1);
  player->y = ClampInt(player->y, 0, map->config.height - 1);

  if (!player->alive) {
    player->alive = true;
    player->hp = player->max_hp;
    player->mp = player->max_mp;
    player->dead_until_ms = 0;
    player->x = map->config.spawn_x;
    player->y = map->config.spawn_y;
    if (player->in_world) {
      NotifyLeaveMap(*player);
    }
  }

  if (player->in_world) {
    // 断线重连：实体还在场景里，只需要补一份完整场景
    SendSceneInfo(*player);
    return;
  }

  NotifyEnterMap(*player, true);
  LogInfo("enter world id=%llu name=%s map=%d pos=(%d,%d)",
          static_cast<unsigned long long>(player->id), player->name.c_str(),
          player->map_id, player->x, player->y);
}

void World::OnDisconnect(uint64_t player_id) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr || !player->online) {
    return;
  }
  player->online = false;
  player->offline_deadline_ms = NowMs() + kOfflineRetainMs;
  LogInfo("disconnect id=%llu name=%s (retain %llds)",
          static_cast<unsigned long long>(player->id), player->name.c_str(),
          static_cast<long long>(kOfflineRetainMs / 1000));
}

void World::Logout(uint64_t player_id) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr) {
    return;
  }
  if (player->in_world) {
    NotifyLeaveMap(*player);
  }
  player->online = false;
  player->offline_deadline_ms = 0;
  SaveAll();
  LogInfo("logout id=%llu name=%s", static_cast<unsigned long long>(player->id),
          player->name.c_str());
}

// ---------------------------------------------------------------- player input

void World::HandleMove(uint64_t player_id, uint8_t dir) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr || !player->online || !player->in_world || !player->alive) {
    return;
  }

  int64_t now = NowMs();
  if (now - player->last_move_ms < kPlayerMoveCooldownMs) {
    return;  // 客户端移动太快：服务端直接忽略，防止加速外挂
  }

  MapRuntime *map = FindMap(player->map_id);
  if (map == nullptr) {
    return;
  }

  int nx = player->x;
  int ny = player->y;
  switch (dir) {
    case kDirUp:
      --ny;
      break;
    case kDirDown:
      ++ny;
      break;
    case kDirLeft:
      --nx;
      break;
    case kDirRight:
      ++nx;
      break;
    default:
      return;
  }

  if (nx < 0 || nx >= map->config.width || ny < 0 || ny >= map->config.height) {
    return;
  }

  int old_x = player->x;
  int old_y = player->y;
  map->RemoveEntity(player->id, old_x, old_y);
  player->x = nx;
  player->y = ny;
  player->last_move_ms = now;
  map->AddEntity(player->id, nx, ny);
  AfterMove(*player, old_x, old_y, dir);
}

void World::HandleAttack(uint64_t player_id, uint64_t target_id) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr || !player->online || !player->in_world || !player->alive) {
    return;
  }

  Monster *target = FindMonster(target_id);
  if (target == nullptr || !target->alive || !target->in_world ||
      target->map_id != player->map_id) {
    SendError(player_id, 404, "目标不存在或已死亡");
    return;
  }

  if (Chebyshev(player->x, player->y, target->x, target->y) > 1) {
    SendError(player_id, 400, "距离太远，无法近战攻击");
    return;
  }

  int64_t now = NowMs();
  if (now - player->last_attack_ms < kPlayerAttackCooldownMs) {
    return;
  }
  player->last_attack_ms = now;
  int damage = CalcDamage(player->atk, target->def, 1.0, 0);
  ApplyDamage(*player, *target, 0, damage, now);
}

void World::HandleSkill(uint64_t player_id, uint32_t skill_id, uint64_t target_id) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr || !player->online || !player->in_world || !player->alive) {
    return;
  }

  const SkillConfig *skill = FindSkill(skill_id);
  if (skill == nullptr) {
    SendError(player_id, 400, "技能不存在");
    return;
  }

  Monster *target = FindMonster(target_id);
  if (target == nullptr || !target->alive || !target->in_world ||
      target->map_id != player->map_id) {
    SendError(player_id, 404, "目标不存在或已死亡");
    return;
  }

  if (Chebyshev(player->x, player->y, target->x, target->y) > skill->range) {
    SendError(player_id, 400, "目标超出技能范围");
    return;
  }

  int64_t now = NowMs();
  auto it = player->skill_ready_ms.find(skill_id);
  int64_t ready_at = it == player->skill_ready_ms.end() ? 0 : it->second;
  if (now < ready_at) {
    return;  // 冷却中，静默忽略
  }
  if (player->mp < skill->mp_cost) {
    SendError(player_id, 400, "魔法值不足");
    return;
  }

  player->mp -= skill->mp_cost;
  player->skill_ready_ms[skill_id] = now + skill->cooldown_ms;
  SendStats(*player);

  int damage = CalcDamage(player->atk, target->def, skill->multiplier,
                          skill->flat_damage);
  ApplyDamage(*player, *target, skill->id, damage, now);
}

void World::HandleChat(uint64_t player_id, const std::string &text) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr || !player->online || !player->in_world) {
    return;
  }

  const size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return;
  }
  const size_t end = text.find_last_not_of(" \t\r\n");
  std::string content = text.substr(begin, end - begin + 1);
  if (content.size() > kMaxChatBytes) {
    SendError(player_id, 400, "消息过长");
    return;
  }

  int64_t now = NowMs();
  if (now - player->last_chat_ms < kPlayerChatCooldownMs) {
    return;
  }
  player->last_chat_ms = now;

  ChatNotify notify;
  notify.sender_id = player->id;
  notify.sender_name = player->name;
  notify.content = content;
  BroadcastToMap(player->map_id, kChatNotify, EncodeChatNotify(notify));
}

void World::HandleChangeMap(uint64_t player_id, uint32_t map_id) {
  Player *player = FindPlayer(player_id);
  if (player == nullptr || !player->online || !player->in_world || !player->alive) {
    return;
  }

  MapRuntime *target_map = FindMap(static_cast<int>(map_id));
  if (target_map == nullptr) {
    SendError(player_id, 404, "地图不存在");
    return;
  }

  MapRuntime *current_map = FindMap(player->map_id);
  if (current_map == nullptr) {
    return;
  }

  bool found = false;
  int dest_x = 0;
  int dest_y = 0;
  for (const auto &portal : current_map->config.portals) {
    if (portal.target_map == static_cast<int>(map_id) &&
        std::abs(portal.x - player->x) <= 1 &&
        std::abs(portal.y - player->y) <= 1) {
      found = true;
      dest_x = portal.target_x;
      dest_y = portal.target_y;
      break;
    }
  }

  if (!found) {
    SendError(player_id, 400, "附近没有通往该地图的传送门");
    return;
  }

  RelocatePlayer(*player, static_cast<int>(map_id), dest_x, dest_y);
  SendNotice(player_id, "你进入了 " + target_map->config.name);
}

// ---------------------------------------------------------------- tick

void World::Tick(int64_t now_ms) {
  TickPlayers(now_ms);
  TickMonsters(now_ms);

  if (now_ms - last_save_ms_ >= kSaveIntervalMs) {
    SaveAll();
    last_save_ms_ = now_ms;
  }

  if (now_ms - last_log_ms_ >= 10000) {
    int online = 0;
    for (const auto &item : players_) {
      if (item.second->online) {
        ++online;
      }
    }
    LogInfo("world stats: online=%d characters=%zu monsters=%zu", online,
            players_.size(), monsters_.size());
    last_log_ms_ = now_ms;
  }
}

void World::TickPlayers(int64_t now_ms) {
  for (auto &item : players_) {
    Player &player = *item.second;

    if (player.online && player.in_world && player.alive &&
        now_ms - player.last_regen_ms >= kPlayerRegenIntervalMs) {
      player.last_regen_ms = now_ms;
      int new_hp = std::min(player.max_hp, player.hp + std::max(1, player.max_hp / 50));
      int new_mp = std::min(player.max_mp, player.mp + std::max(1, player.max_mp / 20));
      if (new_hp != player.hp || new_mp != player.mp) {
        player.hp = new_hp;
        player.mp = new_mp;
        SendStats(player);
      }
    }

    if (!player.alive && player.dead_until_ms > 0 && now_ms >= player.dead_until_ms) {
      if (player.online) {
        RespawnPlayer(player, now_ms);
      }
    }

    if (!player.online && player.in_world && player.offline_deadline_ms > 0 &&
        now_ms >= player.offline_deadline_ms) {
      NotifyLeaveMap(player);
      player.offline_deadline_ms = 0;
      LogInfo("offline retain expired, remove from map id=%llu name=%s",
              static_cast<unsigned long long>(player.id), player.name.c_str());
    }
  }
}

void World::TickMonsters(int64_t now_ms) {
  for (auto &item : monsters_) {
    Monster &monster = *item.second;
    if (!monster.alive) {
      if (monster.dead_until_ms > 0 && now_ms >= monster.dead_until_ms) {
        RespawnMonster(monster, now_ms);
      }
      continue;
    }
    if (now_ms < monster.next_ai_ms) {
      continue;
    }
    monster.next_ai_ms = now_ms + 200;
    UpdateMonsterAi(monster, now_ms);
  }
}

void World::UpdateMonsterAi(Monster &monster, int64_t now_ms) {
  int distance_to_spawn =
      Chebyshev(monster.x, monster.y, monster.spawn_x, monster.spawn_y);
  if (distance_to_spawn > 18) {
    monster.returning = true;
  }

  if (monster.returning) {
    if (distance_to_spawn == 0) {
      monster.returning = false;
      monster.hp = monster.max_hp;
      return;
    }
    if (now_ms - monster.last_move_ms >= 500) {
      int nx = monster.x;
      int ny = monster.y;
      if (std::abs(monster.spawn_x - nx) >= std::abs(monster.spawn_y - ny)) {
        nx += monster.spawn_x > nx ? 1 : -1;
      } else {
        ny += monster.spawn_y > ny ? 1 : -1;
      }
      MoveMonster(monster, nx, ny, now_ms);
    }
    return;
  }

  Player *target = nullptr;
  int best_distance = 1 << 30;
  for (auto &item : players_) {
    Player &player = *item.second;
    if (!player.online || !player.in_world || !player.alive ||
        player.map_id != monster.map_id) {
      continue;
    }
    int distance = Chebyshev(monster.x, monster.y, player.x, player.y);
    if (distance <= monster.aggro_range && distance < best_distance) {
      best_distance = distance;
      target = &player;
    }
  }

  if (target == nullptr) {
    if (distance_to_spawn > 0 && now_ms - monster.last_move_ms >= 800) {
      int nx = monster.x;
      int ny = monster.y;
      if (std::abs(monster.spawn_x - nx) >= std::abs(monster.spawn_y - ny)) {
        nx += monster.spawn_x > nx ? 1 : -1;
      } else {
        ny += monster.spawn_y > ny ? 1 : -1;
      }
      MoveMonster(monster, nx, ny, now_ms);
    }
    return;
  }

  int distance = Chebyshev(monster.x, monster.y, target->x, target->y);
  if (distance <= monster.attack_range) {
    if (now_ms - monster.last_attack_ms >= 1000) {
      monster.last_attack_ms = now_ms;
      MonsterAttack(monster, *target, now_ms);
    }
    return;
  }

  if (now_ms - monster.last_move_ms >= 400) {
    int nx = monster.x;
    int ny = monster.y;
    if (std::abs(target->x - nx) >= std::abs(target->y - ny)) {
      nx += target->x > nx ? 1 : -1;
    } else {
      ny += target->y > ny ? 1 : -1;
    }
    MoveMonster(monster, nx, ny, now_ms);
  }
}

// ---------------------------------------------------------------- combat

int World::CalcDamage(int atk, int def, double multiplier, int flat_damage) {
  int base = static_cast<int>(static_cast<double>(atk) * multiplier) + flat_damage -
             def / 2;
  if (base < 1) {
    base = 1;
  }
  int variance = static_cast<int>(rng_() % 5) - 2;
  int damage = base + variance;
  return damage < 1 ? 1 : damage;
}

void World::ApplyDamage(Entity &attacker, Entity &target, uint32_t skill_id,
                        int damage, int64_t now_ms) {
  if (!target.alive || damage <= 0) {
    return;
  }

  target.hp -= damage;
  if (target.hp < 0) {
    target.hp = 0;
  }
  target.alive = target.hp > 0;

  CombatNotify notify;
  notify.attacker_id = attacker.id;
  notify.target_id = target.id;
  notify.skill_id = skill_id;
  notify.damage = static_cast<uint32_t>(damage);
  notify.target_hp = static_cast<uint32_t>(target.hp);
  notify.target_max_hp = static_cast<uint32_t>(target.max_hp);
  notify.target_alive = target.alive ? 1 : 0;

  std::unordered_set<uint64_t> recipients = VisiblePlayerIds(attacker);
  std::unordered_set<uint64_t> target_viewers = VisiblePlayerIds(target);
  recipients.insert(target_viewers.begin(), target_viewers.end());
  if (attacker.type == EntityType::kPlayer) {
    recipients.insert(attacker.id);
  }
  if (target.type == EntityType::kPlayer) {
    recipients.insert(target.id);
  }

  const std::string body = EncodeCombatNotify(notify);
  for (uint64_t id : recipients) {
    SendToPlayer(id, kCombatNotify, body);
  }

  if (!target.alive) {
    HandleDeath(target, &attacker, now_ms);
  }
}

void World::HandleDeath(Entity &target, Entity *killer, int64_t now_ms) {
  DeathNotify notify;
  notify.entity_id = target.id;
  notify.entity_type = EntityTypeToWire(target.type);
  notify.killer_id = killer != nullptr ? killer->id : 0;

  std::unordered_set<uint64_t> recipients = VisiblePlayerIds(target);
  if (killer != nullptr && killer->type == EntityType::kPlayer) {
    Player *killer_player = static_cast<Player *>(killer);
    if (killer_player->online) {
      recipients.insert(killer_player->id);
    }
  }
  if (target.type == EntityType::kPlayer) {
    Player &target_player = static_cast<Player &>(target);
    if (target_player.online) {
      recipients.insert(target_player.id);
    }
  }

  if (target.type == EntityType::kMonster) {
    Monster &monster = static_cast<Monster &>(target);
    notify.exp_reward = static_cast<uint32_t>(monster.exp_reward);
    notify.gold_reward = static_cast<uint32_t>(monster.gold_reward);
    notify.respawn_ms = static_cast<uint16_t>(kMonsterRespawnMs);

    if (killer != nullptr && killer->type == EntityType::kPlayer) {
      Player *killer_player = static_cast<Player *>(killer);
      if (killer_player->online) {
        AddExp(*killer_player, monster.exp_reward, monster.gold_reward);
      }
    }

    monster.alive = false;
    monster.hp = 0;
    monster.dead_until_ms = now_ms + kMonsterRespawnMs;

    const std::string death_body = EncodeDeathNotify(notify);
    const std::string disappear_body = EncodeEntityDisappear(monster.id);
    for (uint64_t id : recipients) {
      SendToPlayer(id, kDeathNotify, death_body);
      SendToPlayer(id, kEntityDisappear, disappear_body);
    }

    MapRuntime *map = FindMap(monster.map_id);
    if (map != nullptr && monster.in_world) {
      map->RemoveEntity(monster.id, monster.x, monster.y);
      monster.in_world = false;
    }
    LogInfo("monster died id=%llu name=%s killer=%llu",
            static_cast<unsigned long long>(monster.id), monster.name.c_str(),
            static_cast<unsigned long long>(notify.killer_id));
    return;
  }

  Player &player = static_cast<Player &>(target);
  notify.exp_reward = 0;
  notify.gold_reward = 0;
  notify.respawn_ms = static_cast<uint16_t>(kPlayerRespawnMs);

  player.alive = false;
  player.hp = 0;
  player.dead_until_ms = now_ms + kPlayerRespawnMs;

  const std::string death_body = EncodeDeathNotify(notify);
  for (uint64_t id : recipients) {
    SendToPlayer(id, kDeathNotify, death_body);
  }
  SendStats(player);
  LogInfo("player died id=%llu name=%s killer=%llu",
          static_cast<unsigned long long>(player.id), player.name.c_str(),
          static_cast<unsigned long long>(notify.killer_id));
}

void World::MonsterAttack(Monster &monster, Player &player, int64_t now_ms) {
  int damage = CalcDamage(monster.atk, player.def, 1.0, 0);
  ApplyDamage(monster, player, 0, damage, now_ms);
}

void World::AddExp(Player &player, int exp, int gold) {
  if (exp <= 0 && gold <= 0) {
    return;
  }
  player.exp += std::max(0, exp);
  player.gold += std::max(0, gold);

  int level_ups = 0;
  while (player.level < kMaxLevel && player.exp >= player.exp_next) {
    player.exp -= player.exp_next;
    ++player.level;
    RecomputeStats(player);
    player.hp = player.max_hp;
    player.mp = player.max_mp;
    ++level_ups;
  }

  if (level_ups > 0) {
    NoticeNotify notice;
    notice.content = "玩家 " + player.name + " 升到了 Lv." +
                     std::to_string(player.level);
    const std::string body = EncodeNoticeNotify(notice);
    BroadcastToVisible(player, kNoticeNotify, body);
    LogInfo("level up id=%llu name=%s level=%d",
            static_cast<unsigned long long>(player.id), player.name.c_str(),
            player.level);
  }

  SendStats(player);
}

void World::RecomputeStats(Player &player) {
  player.max_hp = 100 + (player.level - 1) * 25;
  player.max_mp = 50 + (player.level - 1) * 10;
  player.atk = 10 + (player.level - 1) * 4;
  player.def = 2 + (player.level - 1) * 2;
  player.exp_next = 100 + (player.level - 1) * 80;

  if (player.hp > player.max_hp) {
    player.hp = player.max_hp;
  }
  if (player.mp > player.max_mp) {
    player.mp = player.max_mp;
  }
}

// ---------------------------------------------------------------- respawn

void World::RespawnMonster(Monster &monster, int64_t now_ms) {
  monster.alive = true;
  monster.hp = monster.max_hp;
  monster.x = monster.spawn_x;
  monster.y = monster.spawn_y;
  monster.dead_until_ms = 0;
  monster.returning = false;
  monster.last_move_ms = 0;
  monster.last_attack_ms = 0;
  monster.next_ai_ms = now_ms + 500;

  MapRuntime *map = FindMap(monster.map_id);
  if (map != nullptr) {
    map->AddEntity(monster.id, monster.x, monster.y);
    monster.in_world = true;
  }

  BroadcastToVisible(monster, kEntityAppear,
                     EncodeEntityAppear(MakeEntityInfo(monster)));
  LogInfo("monster respawn id=%llu name=%s",
          static_cast<unsigned long long>(monster.id), monster.name.c_str());
}

void World::RespawnPlayer(Player &player, int64_t now_ms) {
  (void)now_ms;
  bool was_in_world = player.in_world;

  MapRuntime *map = FindMap(player.map_id);
  if (map == nullptr) {
    player.map_id = 1;
    map = FindMap(1);
  }

  if (was_in_world) {
    NotifyLeaveMap(player);
  }

  player.alive = true;
  player.hp = player.max_hp;
  player.mp = player.max_mp;
  player.dead_until_ms = 0;
  player.last_move_ms = 0;
  player.last_attack_ms = 0;
  player.last_regen_ms = NowMs();
  if (map != nullptr) {
    player.x = map->config.spawn_x;
    player.y = map->config.spawn_y;
  }

  if (was_in_world || player.online) {
    NotifyEnterMap(player, player.online);
  }

  if (player.online) {
    SendStats(player);
    RespawnNotify notify;
    notify.entity_id = player.id;
    notify.x = static_cast<uint16_t>(player.x);
    notify.y = static_cast<uint16_t>(player.y);
    notify.hp = static_cast<uint32_t>(player.hp);
    notify.max_hp = static_cast<uint32_t>(player.max_hp);
    SendToPlayer(player.id, kRespawnNotify, EncodeRespawnNotify(notify));
  }

  LogInfo("player respawn id=%llu name=%s pos=(%d,%d)",
          static_cast<unsigned long long>(player.id), player.name.c_str(),
          player.x, player.y);
}

// ---------------------------------------------------------------- AOI / view

std::unordered_set<uint64_t> World::VisibleIds(const Entity &entity) const {
  std::unordered_set<uint64_t> ids;
  auto map_it = maps_.find(entity.map_id);
  if (map_it == maps_.end() || !entity.in_world) {
    return ids;
  }
  const MapRuntime &map = map_it->second;
  ids = map.CollectNeighborhoodIds(entity.x / kCellSize, entity.y / kCellSize);
  ids.erase(entity.id);
  return ids;
}

std::unordered_set<uint64_t> World::VisiblePlayerIds(const Entity &entity) const {
  std::unordered_set<uint64_t> result;
  for (uint64_t id : VisibleIds(entity)) {
    auto it = players_.find(id);
    if (it != players_.end() && it->second->online) {
      result.insert(id);
    }
  }
  return result;
}

void World::NotifyEnterMap(Entity &entity, bool send_scene) {
  MapRuntime *map = FindMap(entity.map_id);
  if (map == nullptr) {
    return;
  }
  if (!entity.in_world) {
    map->AddEntity(entity.id, entity.x, entity.y);
    entity.in_world = true;
  }

  const std::string appear = EncodeEntityAppear(MakeEntityInfo(entity));
  for (uint64_t id : VisibleIds(entity)) {
    auto it = players_.find(id);
    if (it != players_.end() && it->second->online) {
      SendToPlayer(id, kEntityAppear, appear);
    }
  }

  if (send_scene && entity.type == EntityType::kPlayer) {
    SendSceneInfo(static_cast<Player &>(entity));
  }
}

void World::NotifyLeaveMap(Entity &entity) {
  if (!entity.in_world) {
    return;
  }
  MapRuntime *map = FindMap(entity.map_id);
  if (map == nullptr) {
    entity.in_world = false;
    return;
  }

  const std::string disappear = EncodeEntityDisappear(entity.id);
  for (uint64_t id : VisibleIds(entity)) {
    auto it = players_.find(id);
    if (it != players_.end() && it->second->online) {
      SendToPlayer(id, kEntityDisappear, disappear);
    }
  }

  map->RemoveEntity(entity.id, entity.x, entity.y);
  entity.in_world = false;
}

void World::AfterMove(Entity &entity, int old_x, int old_y, uint8_t dir) {
  MapRuntime *map = FindMap(entity.map_id);
  if (map == nullptr) {
    return;
  }

  MoveNotify move_notify;
  move_notify.id = entity.id;
  move_notify.type = EntityTypeToWire(entity.type);
  move_notify.x = static_cast<uint16_t>(entity.x);
  move_notify.y = static_cast<uint16_t>(entity.y);
  move_notify.dir = dir;
  const std::string move_body = EncodeMoveNotify(move_notify);

  const int old_cell_x = old_x / kCellSize;
  const int old_cell_y = old_y / kCellSize;
  const int new_cell_x = entity.x / kCellSize;
  const int new_cell_y = entity.y / kCellSize;

  if (old_cell_x == new_cell_x && old_cell_y == new_cell_y) {
    std::unordered_set<uint64_t> neighbors =
        map->CollectNeighborhoodIds(new_cell_x, new_cell_y);
    for (uint64_t id : neighbors) {
      auto it = players_.find(id);
      if (it != players_.end() && it->second->online) {
        SendToPlayer(id, kMoveNotify, move_body);
      }
    }
    if (entity.type == EntityType::kPlayer) {
      SendToPlayer(entity.id, kMoveNotify, move_body);
    }
    return;
  }

  std::unordered_set<uint64_t> before =
      map->CollectNeighborhoodIds(old_cell_x, old_cell_y);
  std::unordered_set<uint64_t> after =
      map->CollectNeighborhoodIds(new_cell_x, new_cell_y);
  before.erase(entity.id);
  after.erase(entity.id);

  const std::string appear_body = EncodeEntityAppear(MakeEntityInfo(entity));
  const std::string disappear_body = EncodeEntityDisappear(entity.id);

  std::unordered_set<uint64_t> observers = before;
  observers.insert(after.begin(), after.end());

  for (uint64_t id : observers) {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second->online) {
      continue;
    }
    bool in_before = before.count(id) != 0;
    bool in_after = after.count(id) != 0;
    if (in_after && !in_before) {
      SendToPlayer(id, kEntityAppear, appear_body);
    } else if (in_before && !in_after) {
      SendToPlayer(id, kEntityDisappear, disappear_body);
    } else {
      SendToPlayer(id, kMoveNotify, move_body);
    }
  }

  if (entity.type == EntityType::kPlayer) {
    for (uint64_t id : after) {
      if (before.count(id) != 0) {
        continue;
      }
      Entity *other = FindEntity(id);
      if (other != nullptr && other->in_world) {
        SendToPlayer(entity.id, kEntityAppear,
                     EncodeEntityAppear(MakeEntityInfo(*other)));
      }
    }
    for (uint64_t id : before) {
      if (after.count(id) == 0) {
        SendToPlayer(entity.id, kEntityDisappear, EncodeEntityDisappear(id));
      }
    }
    SendToPlayer(entity.id, kMoveNotify, move_body);
  }
}

void World::RelocatePlayer(Player &player, int map_id, int x, int y) {
  MapRuntime *target_map = FindMap(map_id);
  if (target_map == nullptr) {
    return;
  }
  if (player.in_world) {
    NotifyLeaveMap(player);
  }
  player.map_id = map_id;
  player.x = ClampInt(x, 0, target_map->config.width - 1);
  player.y = ClampInt(y, 0, target_map->config.height - 1);
  NotifyEnterMap(player, player.online);
}

// ---------------------------------------------------------------- sending

void World::SendToPlayer(uint64_t player_id, uint16_t msg_id,
                         const std::string &body) {
  if (send_) {
    send_(player_id, msg_id, body);
  }
}

void World::SendError(uint64_t player_id, uint16_t code,
                      const std::string &message) {
  ErrorNotify notify;
  notify.code = code;
  notify.message = message;
  SendToPlayer(player_id, kErrorNotify, EncodeErrorNotify(notify));
}

void World::SendNotice(uint64_t player_id, const std::string &message) {
  NoticeNotify notify;
  notify.content = message;
  SendToPlayer(player_id, kNoticeNotify, EncodeNoticeNotify(notify));
}

void World::SendSceneInfo(Player &player) {
  MapRuntime *map = FindMap(player.map_id);
  if (map == nullptr) {
    return;
  }

  SceneInfo info;
  info.map_id = static_cast<uint32_t>(player.map_id);
  info.width = static_cast<uint16_t>(map->config.width);
  info.height = static_cast<uint16_t>(map->config.height);
  info.self_id = player.id;
  info.self_name = player.name;
  info.x = static_cast<uint16_t>(player.x);
  info.y = static_cast<uint16_t>(player.y);
  info.level = static_cast<uint8_t>(player.level);
  info.hp = static_cast<uint32_t>(player.hp);
  info.max_hp = static_cast<uint32_t>(player.max_hp);
  info.mp = static_cast<uint32_t>(player.mp);
  info.max_mp = static_cast<uint32_t>(player.max_mp);
  info.atk = static_cast<uint32_t>(player.atk);
  info.def = static_cast<uint32_t>(player.def);
  info.exp = static_cast<uint32_t>(player.exp);
  info.exp_next = static_cast<uint32_t>(player.exp_next);
  info.gold = static_cast<uint32_t>(player.gold);

  std::unordered_set<uint64_t> visible = VisibleIds(player);
  info.entities.reserve(visible.size());
  for (uint64_t id : visible) {
    Entity *entity = FindEntity(id);
    if (entity != nullptr && entity->in_world) {
      info.entities.push_back(MakeEntityInfo(*entity));
    }
  }

  SendToPlayer(player.id, kSceneInfo, EncodeSceneInfo(info));
}

void World::SendStats(Player &player) {
  StatsInfo info;
  info.level = static_cast<uint8_t>(player.level);
  info.hp = static_cast<uint32_t>(player.hp);
  info.max_hp = static_cast<uint32_t>(player.max_hp);
  info.mp = static_cast<uint32_t>(player.mp);
  info.max_mp = static_cast<uint32_t>(player.max_mp);
  info.atk = static_cast<uint32_t>(player.atk);
  info.def = static_cast<uint32_t>(player.def);
  info.exp = static_cast<uint32_t>(player.exp);
  info.exp_next = static_cast<uint32_t>(player.exp_next);
  info.gold = static_cast<uint32_t>(player.gold);
  SendToPlayer(player.id, kStatsNotify, EncodeStatsInfo(info));
}

void World::BroadcastToVisible(const Entity &center, uint16_t msg_id,
                               const std::string &body) {
  std::unordered_set<uint64_t> ids = VisiblePlayerIds(center);
  if (center.type == EntityType::kPlayer) {
    const Player &player = static_cast<const Player &>(center);
    if (player.online) {
      ids.insert(player.id);
    }
  }
  for (uint64_t id : ids) {
    SendToPlayer(id, msg_id, body);
  }
}

void World::BroadcastToMap(int map_id, uint16_t msg_id, const std::string &body) {
  for (auto &item : players_) {
    Player &player = *item.second;
    if (player.online && player.in_world && player.map_id == map_id) {
      SendToPlayer(player.id, msg_id, body);
    }
  }
}

// ---------------------------------------------------------------- lookup / util

World::MapRuntime *World::FindMap(int map_id) {
  auto it = maps_.find(map_id);
  return it == maps_.end() ? nullptr : &it->second;
}

Player *World::FindPlayer(uint64_t player_id) {
  auto it = players_.find(player_id);
  return it == players_.end() ? nullptr : it->second.get();
}

Monster *World::FindMonster(uint64_t monster_id) {
  auto it = monsters_.find(monster_id);
  return it == monsters_.end() ? nullptr : it->second.get();
}

Entity *World::FindEntity(uint64_t entity_id) {
  auto player_it = players_.find(entity_id);
  if (player_it != players_.end()) {
    return player_it->second.get();
  }
  auto monster_it = monsters_.find(entity_id);
  if (monster_it != monsters_.end()) {
    return monster_it->second.get();
  }
  return nullptr;
}

proto::EntityInfo World::MakeEntityInfo(const Entity &entity) {
  EntityInfo info;
  info.id = entity.id;
  info.type = EntityTypeToWire(entity.type);
  info.name = entity.name;
  info.x = static_cast<uint16_t>(entity.x);
  info.y = static_cast<uint16_t>(entity.y);
  info.hp = static_cast<uint32_t>(entity.hp);
  info.max_hp = static_cast<uint32_t>(entity.max_hp);
  info.level = static_cast<uint8_t>(entity.level);
  info.alive = entity.alive ? 1 : 0;
  return info;
}

int World::Chebyshev(int x1, int y1, int x2, int y2) {
  return std::max(std::abs(x1 - x2), std::abs(y1 - y2));
}

uint8_t World::DirectionFromDelta(int dx, int dy) {
  if (dx > 0) {
    return kDirRight;
  }
  if (dx < 0) {
    return kDirLeft;
  }
  if (dy > 0) {
    return kDirDown;
  }
  return kDirUp;
}

void World::MoveMonster(Monster &monster, int nx, int ny, int64_t now_ms) {
  MapRuntime *map = FindMap(monster.map_id);
  if (map == nullptr) {
    return;
  }
  if (nx < 0 || nx >= map->config.width || ny < 0 || ny >= map->config.height) {
    return;
  }
  if (nx == monster.x && ny == monster.y) {
    return;
  }

  int dx = nx - monster.x;
  int dy = ny - monster.y;
  int old_x = monster.x;
  int old_y = monster.y;

  map->RemoveEntity(monster.id, old_x, old_y);
  monster.x = nx;
  monster.y = ny;
  monster.last_move_ms = now_ms;
  map->AddEntity(monster.id, nx, ny);

  AfterMove(monster, old_x, old_y, DirectionFromDelta(dx, dy));
}

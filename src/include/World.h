/**
 * @file World.h
 * @brief 服务端权威的游戏世界：地图、AOI、移动、战斗、怪物 AI、存档
 *
 * 线程模型：World 的所有方法都由 GameServer 在持有同一把互斥锁的情况下调用，
 * 因此内部不需要再加锁；网络发送通过 SendFn 回调投递回各连接所属的 reactor。
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "GameConfig.h"
#include "Macros.h"
#include "Messages.h"

struct Entity {
  uint64_t id{0};
  EntityType type{EntityType::kPlayer};
  std::string name;
  int map_id{1};
  int x{0};
  int y{0};
  int hp{1};
  int max_hp{1};
  int level{1};
  bool alive{true};
  bool in_world{false};
};

struct Player : Entity {
  std::string password_hash;
  int exp{0};
  int exp_next{100};
  int mp{50};
  int max_mp{50};
  int atk{10};
  int def{2};
  int gold{0};
  bool online{false};
  int64_t offline_deadline_ms{0};
  int64_t last_move_ms{0};
  int64_t last_attack_ms{0};
  int64_t last_chat_ms{0};
  int64_t last_regen_ms{0};
  int64_t dead_until_ms{0};
  std::unordered_map<uint32_t, int64_t> skill_ready_ms;
};

struct Monster : Entity {
  int spawn_x{0};
  int spawn_y{0};
  int atk{1};
  int def{0};
  int aggro_range{8};
  int attack_range{1};
  int exp_reward{0};
  int gold_reward{0};
  int64_t last_move_ms{0};
  int64_t last_attack_ms{0};
  int64_t next_ai_ms{0};
  int64_t dead_until_ms{0};
  bool returning{false};
};

class World {
 public:
  using SendFn = std::function<void(uint64_t player_id, uint16_t msg_id,
                                   const std::string &body)>;

  explicit World(SendFn send);
  ~World();

  DISALLOW_COPY_AND_MOVE(World);

  void Load();
  void SaveAll();

  /// 登录/校验，成功返回 playerId；此时尚未进入地图
  uint64_t Login(const std::string &name, const std::string &password,
                 std::string &error, bool &is_new);

  /// 登录成功后进入地图并下发场景信息
  void EnterGame(uint64_t player_id);

  void OnDisconnect(uint64_t player_id);
  void Logout(uint64_t player_id);

  void HandleMove(uint64_t player_id, uint8_t dir);
  void HandleAttack(uint64_t player_id, uint64_t target_id);
  void HandleSkill(uint64_t player_id, uint32_t skill_id, uint64_t target_id);
  void HandleChat(uint64_t player_id, const std::string &text);
  void HandleChangeMap(uint64_t player_id, uint32_t map_id);

  void Tick(int64_t now_ms);

 private:
  struct MapRuntime {
    MapConfig config;
    int cells_x{0};
    int cells_y{0};
    std::vector<std::unordered_set<uint64_t>> cells;

    void Init(const MapConfig &cfg);
    int CellIndex(int x, int y) const;
    void AddEntity(uint64_t id, int x, int y);
    void RemoveEntity(uint64_t id, int x, int y);
    std::unordered_set<uint64_t> CollectNeighborhoodIds(int cell_x, int cell_y) const;
  };

  MapRuntime *FindMap(int map_id);
  Player *FindPlayer(uint64_t player_id);
  Monster *FindMonster(uint64_t monster_id);
  Entity *FindEntity(uint64_t entity_id);

  static proto::EntityInfo MakeEntityInfo(const Entity &entity);
  static int Chebyshev(int x1, int y1, int x2, int y2);
  static uint8_t DirectionFromDelta(int dx, int dy);

  std::unordered_set<uint64_t> VisibleIds(const Entity &entity) const;
  std::unordered_set<uint64_t> VisiblePlayerIds(const Entity &entity) const;

  void SendToPlayer(uint64_t player_id, uint16_t msg_id, const std::string &body);
  void SendError(uint64_t player_id, uint16_t code, const std::string &message);
  void SendNotice(uint64_t player_id, const std::string &message);
  void SendSceneInfo(Player &player);
  void SendStats(Player &player);
  void BroadcastToVisible(const Entity &center, uint16_t msg_id,
                          const std::string &body);
  void BroadcastToMap(int map_id, uint16_t msg_id, const std::string &body);

  void NotifyEnterMap(Entity &entity, bool send_scene);
  void NotifyLeaveMap(Entity &entity);
  void AfterMove(Entity &entity, int old_x, int old_y, uint8_t dir);
  void RelocatePlayer(Player &player, int map_id, int x, int y);

  void InitMaps();
  void RecomputeStats(Player &player);
  void AddExp(Player &player, int exp, int gold);
  int CalcDamage(int atk, int def, double multiplier, int flat_damage);
  void ApplyDamage(Entity &attacker, Entity &target, uint32_t skill_id,
                   int damage, int64_t now_ms);
  void HandleDeath(Entity &target, Entity *killer, int64_t now_ms);
  void MonsterAttack(Monster &monster, Player &player, int64_t now_ms);

  void MoveMonster(Monster &monster, int nx, int ny, int64_t now_ms);
  void RespawnMonster(Monster &monster, int64_t now_ms);
  void RespawnPlayer(Player &player, int64_t now_ms);

  void TickPlayers(int64_t now_ms);
  void TickMonsters(int64_t now_ms);
  void UpdateMonsterAi(Monster &monster, int64_t now_ms);

  SendFn send_;
  std::string data_file_{"data/characters.dat"};
  std::unordered_map<uint64_t, std::unique_ptr<Player>> players_;
  std::unordered_map<uint64_t, std::unique_ptr<Monster>> monsters_;
  std::unordered_map<std::string, uint64_t> name_to_id_;
  std::map<int, MapRuntime> maps_;
  uint64_t next_player_id_{1};
  uint64_t next_monster_id_{1000000};
  std::mt19937 rng_;
  int64_t last_save_ms_{0};
  int64_t last_log_ms_{0};
};

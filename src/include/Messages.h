/**
 * @file Messages.h
 * @brief 客户端 <-> 服务端消息体编解码
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GameTypes.h"
#include "Protocol.h"

namespace proto {

struct EntityInfo {
  uint64_t id{0};
  uint8_t type{0};
  std::string name;
  uint16_t x{0};
  uint16_t y{0};
  uint32_t hp{0};
  uint32_t max_hp{0};
  uint8_t level{1};
  uint8_t alive{1};
};

void EncodeEntity(Writer &writer, const EntityInfo &entity);
bool DecodeEntity(Reader &reader, EntityInfo &entity);

struct LoginReq {
  std::string name;
  std::string password;
};

struct LoginResp {
  uint8_t ok{0};
  uint64_t player_id{0};
  std::string error;
};

struct MoveReq {
  uint8_t dir{0};
};

struct ChatReq {
  std::string text;
};

struct AttackReq {
  uint64_t target_id{0};
};

struct SkillReq {
  uint32_t skill_id{0};
  uint64_t target_id{0};
};

struct ChangeMapReq {
  uint32_t map_id{0};
};

struct SceneInfo {
  uint32_t map_id{0};
  uint16_t width{0};
  uint16_t height{0};
  uint64_t self_id{0};
  std::string self_name;
  uint16_t x{0};
  uint16_t y{0};
  uint8_t level{1};
  uint32_t hp{0};
  uint32_t max_hp{0};
  uint32_t mp{0};
  uint32_t max_mp{0};
  uint32_t atk{0};
  uint32_t def{0};
  uint32_t exp{0};
  uint32_t exp_next{0};
  uint32_t gold{0};
  std::vector<EntityInfo> entities;
};

struct StatsInfo {
  uint8_t level{1};
  uint32_t hp{0};
  uint32_t max_hp{0};
  uint32_t mp{0};
  uint32_t max_mp{0};
  uint32_t atk{0};
  uint32_t def{0};
  uint32_t exp{0};
  uint32_t exp_next{0};
  uint32_t gold{0};
};

struct MoveNotify {
  uint64_t id{0};
  uint8_t type{0};
  uint16_t x{0};
  uint16_t y{0};
  uint8_t dir{0};
};

struct CombatNotify {
  uint64_t attacker_id{0};
  uint64_t target_id{0};
  uint32_t skill_id{0};
  uint32_t damage{0};
  uint32_t target_hp{0};
  uint32_t target_max_hp{0};
  uint8_t target_alive{1};
};

struct DeathNotify {
  uint64_t entity_id{0};
  uint8_t entity_type{0};
  uint64_t killer_id{0};
  uint32_t exp_reward{0};
  uint32_t gold_reward{0};
  uint16_t respawn_ms{0};
};

struct RespawnNotify {
  uint64_t entity_id{0};
  uint16_t x{0};
  uint16_t y{0};
  uint32_t hp{0};
  uint32_t max_hp{0};
};

struct ChatNotify {
  uint64_t sender_id{0};
  std::string sender_name;
  std::string content;
};

struct ErrorNotify {
  uint16_t code{0};
  std::string message;
};

struct NoticeNotify {
  std::string content;
};

std::string EncodeLoginReq(const LoginReq &msg);
bool DecodeLoginReq(const std::string &body, LoginReq &msg);

std::string EncodeLoginResp(const LoginResp &msg);
bool DecodeLoginResp(const std::string &body, LoginResp &msg);

std::string EncodeMoveReq(const MoveReq &msg);
bool DecodeMoveReq(const std::string &body, MoveReq &msg);

std::string EncodeChatReq(const ChatReq &msg);
bool DecodeChatReq(const std::string &body, ChatReq &msg);

std::string EncodeAttackReq(const AttackReq &msg);
bool DecodeAttackReq(const std::string &body, AttackReq &msg);

std::string EncodeSkillReq(const SkillReq &msg);
bool DecodeSkillReq(const std::string &body, SkillReq &msg);

std::string EncodeChangeMapReq(const ChangeMapReq &msg);
bool DecodeChangeMapReq(const std::string &body, ChangeMapReq &msg);

std::string EncodeSceneInfo(const SceneInfo &msg);
bool DecodeSceneInfo(const std::string &body, SceneInfo &msg);

std::string EncodeStatsInfo(const StatsInfo &msg);
bool DecodeStatsInfo(const std::string &body, StatsInfo &msg);

std::string EncodeEntityAppear(const EntityInfo &entity);
bool DecodeEntityAppear(const std::string &body, EntityInfo &entity);

std::string EncodeEntityDisappear(uint64_t entity_id);
bool DecodeEntityDisappear(const std::string &body, uint64_t &entity_id);

std::string EncodeMoveNotify(const MoveNotify &msg);
bool DecodeMoveNotify(const std::string &body, MoveNotify &msg);

std::string EncodeCombatNotify(const CombatNotify &msg);
bool DecodeCombatNotify(const std::string &body, CombatNotify &msg);

std::string EncodeDeathNotify(const DeathNotify &msg);
bool DecodeDeathNotify(const std::string &body, DeathNotify &msg);

std::string EncodeRespawnNotify(const RespawnNotify &msg);
bool DecodeRespawnNotify(const std::string &body, RespawnNotify &msg);

std::string EncodeChatNotify(const ChatNotify &msg);
bool DecodeChatNotify(const std::string &body, ChatNotify &msg);

std::string EncodeErrorNotify(const ErrorNotify &msg);
bool DecodeErrorNotify(const std::string &body, ErrorNotify &msg);

std::string EncodeNoticeNotify(const NoticeNotify &msg);
bool DecodeNoticeNotify(const std::string &body, NoticeNotify &msg);

}  // namespace proto

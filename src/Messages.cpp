/**
 * @file Messages.cpp
 */
#include "Messages.h"

namespace proto {

void EncodeEntity(Writer &writer, const EntityInfo &entity) {
  writer.U64(entity.id);
  writer.U8(entity.type);
  writer.Str(entity.name);
  writer.U16(entity.x);
  writer.U16(entity.y);
  writer.U32(entity.hp);
  writer.U32(entity.max_hp);
  writer.U8(entity.level);
  writer.U8(entity.alive);
}

bool DecodeEntity(Reader &reader, EntityInfo &entity) {
  if (!reader.U64(entity.id) || !reader.U8(entity.type) ||
      !reader.Str(entity.name, 64) || !reader.U16(entity.x) ||
      !reader.U16(entity.y) || !reader.U32(entity.hp) ||
      !reader.U32(entity.max_hp) || !reader.U8(entity.level) ||
      !reader.U8(entity.alive)) {
    return false;
  }
  return true;
}

std::string EncodeLoginReq(const LoginReq &msg) {
  Writer writer;
  writer.Str(msg.name);
  writer.Str(msg.password);
  return writer.Take();
}

bool DecodeLoginReq(const std::string &body, LoginReq &msg) {
  Reader reader(body.data(), body.size());
  return reader.Str(msg.name, 64) && reader.Str(msg.password, 64);
}

std::string EncodeLoginResp(const LoginResp &msg) {
  Writer writer;
  writer.U8(msg.ok);
  writer.U64(msg.player_id);
  writer.Str(msg.error);
  return writer.Take();
}

bool DecodeLoginResp(const std::string &body, LoginResp &msg) {
  Reader reader(body.data(), body.size());
  return reader.U8(msg.ok) && reader.U64(msg.player_id) &&
         reader.Str(msg.error, 256);
}

std::string EncodeMoveReq(const MoveReq &msg) {
  Writer writer;
  writer.U8(msg.dir);
  return writer.Take();
}

bool DecodeMoveReq(const std::string &body, MoveReq &msg) {
  Reader reader(body.data(), body.size());
  return reader.U8(msg.dir);
}

std::string EncodeChatReq(const ChatReq &msg) {
  Writer writer;
  writer.Str(msg.text);
  return writer.Take();
}

bool DecodeChatReq(const std::string &body, ChatReq &msg) {
  Reader reader(body.data(), body.size());
  return reader.Str(msg.text, 512);
}

std::string EncodeAttackReq(const AttackReq &msg) {
  Writer writer;
  writer.U64(msg.target_id);
  return writer.Take();
}

bool DecodeAttackReq(const std::string &body, AttackReq &msg) {
  Reader reader(body.data(), body.size());
  return reader.U64(msg.target_id);
}

std::string EncodeSkillReq(const SkillReq &msg) {
  Writer writer;
  writer.U32(msg.skill_id);
  writer.U64(msg.target_id);
  return writer.Take();
}

bool DecodeSkillReq(const std::string &body, SkillReq &msg) {
  Reader reader(body.data(), body.size());
  return reader.U32(msg.skill_id) && reader.U64(msg.target_id);
}

std::string EncodeChangeMapReq(const ChangeMapReq &msg) {
  Writer writer;
  writer.U32(msg.map_id);
  return writer.Take();
}

bool DecodeChangeMapReq(const std::string &body, ChangeMapReq &msg) {
  Reader reader(body.data(), body.size());
  return reader.U32(msg.map_id);
}

std::string EncodeSceneInfo(const SceneInfo &msg) {
  Writer writer;
  writer.U32(msg.map_id);
  writer.U16(msg.width);
  writer.U16(msg.height);
  writer.U64(msg.self_id);
  writer.Str(msg.self_name);
  writer.U16(msg.x);
  writer.U16(msg.y);
  writer.U8(msg.level);
  writer.U32(msg.hp);
  writer.U32(msg.max_hp);
  writer.U32(msg.mp);
  writer.U32(msg.max_mp);
  writer.U32(msg.atk);
  writer.U32(msg.def);
  writer.U32(msg.exp);
  writer.U32(msg.exp_next);
  writer.U32(msg.gold);

  writer.U16(static_cast<uint16_t>(msg.entities.size()));
  for (const auto &entity : msg.entities) {
    EncodeEntity(writer, entity);
  }
  return writer.Take();
}

bool DecodeSceneInfo(const std::string &body, SceneInfo &msg) {
  Reader reader(body.data(), body.size());
  if (!reader.U32(msg.map_id) || !reader.U16(msg.width) ||
      !reader.U16(msg.height) || !reader.U64(msg.self_id) ||
      !reader.Str(msg.self_name, 64) || !reader.U16(msg.x) ||
      !reader.U16(msg.y) || !reader.U8(msg.level) || !reader.U32(msg.hp) ||
      !reader.U32(msg.max_hp) || !reader.U32(msg.mp) ||
      !reader.U32(msg.max_mp) || !reader.U32(msg.atk) ||
      !reader.U32(msg.def) || !reader.U32(msg.exp) ||
      !reader.U32(msg.exp_next) || !reader.U32(msg.gold)) {
    return false;
  }
  uint16_t count = 0;
  if (!reader.U16(count)) {
    return false;
  }
  msg.entities.clear();
  for (uint16_t i = 0; i < count; ++i) {
    EntityInfo entity;
    if (!DecodeEntity(reader, entity)) {
      return false;
    }
    msg.entities.push_back(std::move(entity));
  }
  return true;
}

std::string EncodeStatsInfo(const StatsInfo &msg) {
  Writer writer;
  writer.U8(msg.level);
  writer.U32(msg.hp);
  writer.U32(msg.max_hp);
  writer.U32(msg.mp);
  writer.U32(msg.max_mp);
  writer.U32(msg.atk);
  writer.U32(msg.def);
  writer.U32(msg.exp);
  writer.U32(msg.exp_next);
  writer.U32(msg.gold);
  return writer.Take();
}

bool DecodeStatsInfo(const std::string &body, StatsInfo &msg) {
  Reader reader(body.data(), body.size());
  return reader.U8(msg.level) && reader.U32(msg.hp) &&
         reader.U32(msg.max_hp) && reader.U32(msg.mp) &&
         reader.U32(msg.max_mp) && reader.U32(msg.atk) &&
         reader.U32(msg.def) && reader.U32(msg.exp) &&
         reader.U32(msg.exp_next) && reader.U32(msg.gold);
}

std::string EncodeEntityAppear(const EntityInfo &entity) {
  Writer writer;
  EncodeEntity(writer, entity);
  return writer.Take();
}

bool DecodeEntityAppear(const std::string &body, EntityInfo &entity) {
  Reader reader(body.data(), body.size());
  return DecodeEntity(reader, entity);
}

std::string EncodeEntityDisappear(uint64_t entity_id) {
  Writer writer;
  writer.U64(entity_id);
  return writer.Take();
}

bool DecodeEntityDisappear(const std::string &body, uint64_t &entity_id) {
  Reader reader(body.data(), body.size());
  return reader.U64(entity_id);
}

std::string EncodeMoveNotify(const MoveNotify &msg) {
  Writer writer;
  writer.U64(msg.id);
  writer.U8(msg.type);
  writer.U16(msg.x);
  writer.U16(msg.y);
  writer.U8(msg.dir);
  return writer.Take();
}

bool DecodeMoveNotify(const std::string &body, MoveNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.U64(msg.id) && reader.U8(msg.type) && reader.U16(msg.x) &&
         reader.U16(msg.y) && reader.U8(msg.dir);
}

std::string EncodeCombatNotify(const CombatNotify &msg) {
  Writer writer;
  writer.U64(msg.attacker_id);
  writer.U64(msg.target_id);
  writer.U32(msg.skill_id);
  writer.U32(msg.damage);
  writer.U32(msg.target_hp);
  writer.U32(msg.target_max_hp);
  writer.U8(msg.target_alive);
  return writer.Take();
}

bool DecodeCombatNotify(const std::string &body, CombatNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.U64(msg.attacker_id) && reader.U64(msg.target_id) &&
         reader.U32(msg.skill_id) && reader.U32(msg.damage) &&
         reader.U32(msg.target_hp) && reader.U32(msg.target_max_hp) &&
         reader.U8(msg.target_alive);
}

std::string EncodeDeathNotify(const DeathNotify &msg) {
  Writer writer;
  writer.U64(msg.entity_id);
  writer.U8(msg.entity_type);
  writer.U64(msg.killer_id);
  writer.U32(msg.exp_reward);
  writer.U32(msg.gold_reward);
  writer.U16(msg.respawn_ms);
  return writer.Take();
}

bool DecodeDeathNotify(const std::string &body, DeathNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.U64(msg.entity_id) && reader.U8(msg.entity_type) &&
         reader.U64(msg.killer_id) && reader.U32(msg.exp_reward) &&
         reader.U32(msg.gold_reward) && reader.U16(msg.respawn_ms);
}

std::string EncodeRespawnNotify(const RespawnNotify &msg) {
  Writer writer;
  writer.U64(msg.entity_id);
  writer.U16(msg.x);
  writer.U16(msg.y);
  writer.U32(msg.hp);
  writer.U32(msg.max_hp);
  return writer.Take();
}

bool DecodeRespawnNotify(const std::string &body, RespawnNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.U64(msg.entity_id) && reader.U16(msg.x) &&
         reader.U16(msg.y) && reader.U32(msg.hp) &&
         reader.U32(msg.max_hp);
}

std::string EncodeChatNotify(const ChatNotify &msg) {
  Writer writer;
  writer.U64(msg.sender_id);
  writer.Str(msg.sender_name);
  writer.Str(msg.content);
  return writer.Take();
}

bool DecodeChatNotify(const std::string &body, ChatNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.U64(msg.sender_id) && reader.Str(msg.sender_name, 64) &&
         reader.Str(msg.content, 512);
}

std::string EncodeErrorNotify(const ErrorNotify &msg) {
  Writer writer;
  writer.U16(msg.code);
  writer.Str(msg.message);
  return writer.Take();
}

bool DecodeErrorNotify(const std::string &body, ErrorNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.U16(msg.code) && reader.Str(msg.message, 256);
}

std::string EncodeNoticeNotify(const NoticeNotify &msg) {
  Writer writer;
  writer.Str(msg.content);
  return writer.Take();
}

bool DecodeNoticeNotify(const std::string &body, NoticeNotify &msg) {
  Reader reader(body.data(), body.size());
  return reader.Str(msg.content, 512);
}

}  // namespace proto

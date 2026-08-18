#include "game_kit/anim_player.h"

#include "game_kit/entity.h"
#include "game_kit/event_bus.h"
#include "game_kit/script_component.h"

namespace game_kit {

void AnimPlayer::EnsureState(std::string_view name, bool loop) {
  const std::string n(name);
  for (const auto& s : sm_.states()) {
    if (s.name == n) {
      return;
    }
  }
  engine::animation::AnimState st;
  st.name = n;
  st.loop = loop;
  st.clip.name = n;
  st.clip.duration = 1.f;
  engine::animation::ClipKey k0;
  k0.t = 0.f;
  engine::animation::ClipKey k1;
  k1.t = 1.f;
  k1.translation = {0.f, 0.f, 1.f};
  st.clip.tracks.push_back({k0, k1});
  sm_.AddState(std::move(st));
}

void AnimPlayer::Play(std::string_view state, bool loop) {
  EnsureState(state, loop);
  sm_.SetState(state);
  playing_ = true;
  prev_root_ = {};
  root_delta_ = {};
}

void AnimPlayer::Stop() {
  playing_ = false;
  root_delta_ = {};
}

void AnimPlayer::SetTrigger(std::string_view name) { sm_.SetTrigger(name); }

void AnimPlayer::AddNotify(std::string_view state, std::string name, float time) {
  EnsureState(state, true);
  engine::animation::AnimNotify n;
  n.name = std::move(name);
  n.time = time;
  sm_.AddNotify(state, n);
}

void AnimPlayer::Update(float dt) {
  root_delta_ = {};
  if (!playing_) {
    return;
  }
  sm_.Update(dt);
  const auto& states = sm_.states();
  const engine::animation::AnimState* cur = nullptr;
  for (const auto& s : states) {
    if (s.name == sm_.current_state()) {
      cur = &s;
      break;
    }
  }
  if (!cur || cur->clip.tracks.empty() || cur->clip.tracks[0].empty()) {
    return;
  }
  const auto& keys = cur->clip.tracks[0];
  const float t = sm_.state_time();
  engine::Vec3 now = keys.back().translation;
  for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t >= keys[i].t && t <= keys[i + 1].t) {
      const float span = keys[i + 1].t - keys[i].t;
      const float a = span > 0.f ? (t - keys[i].t) / span : 0.f;
      now = keys[i].translation * (1.f - a) + keys[i + 1].translation * a;
      break;
    }
  }
  root_delta_ = now - prev_root_;
  prev_root_ = now;
}

AnimPlayer& AnimPlayerWorld::GetOrCreate(std::string entity_name) {
  for (auto& p : players_) {
    if (p.first == entity_name) {
      return p.second;
    }
  }
  players_.push_back({std::move(entity_name), AnimPlayer{}});
  return players_.back().second;
}

void AnimPlayerWorld::Update(float dt, EventBus& events, EntityWorld& entities,
                             ScriptComponentWorld& scripts, engine::scene::World* world) {
  for (auto& p : players_) {
    p.second.Update(dt);
    auto* e = entities.FindByName(p.first);
    if (e && e->node != engine::scene::kInvalidNode) {
      const auto delta = p.second.root_motion_delta();
      if (delta.length_squared() > 0.f) {
        events.Publish("anim.root_motion", p.first);
        if (p.second.apply_root_motion() && world && world->valid(e->node)) {
          auto t = world->local_transform(e->node);
          t.position = t.position + delta;
          world->set_local_transform(e->node, t);
        }
      }
      auto fired = p.second.machine().DrainNotifies();
      for (const auto& n : fired) {
        events.Publish("anim.notify", n.name);
        scripts.DispatchNotify(e->node, "on_anim_notify", n.name);
      }
    }
  }
}

void AnimPlayerWorld::Clear() { players_.clear(); }

}  // namespace game_kit

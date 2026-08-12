#include "engine/app/application.h"

#include "engine/core/log.h"
#include "engine/media/media.h"
#include "engine/net/net_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/post/post_stack.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/shadow_csm.h"

#include <memory>

int main() {
  engine::ApplicationDesc desc;
  desc.window.title = "Sandbox (M4–M19 skeleton)";
  desc.window.width = 1280;
  desc.window.height = 720;

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  a.set_net(std::make_shared<engine::net::NetSystem>());

  // Seed a few visible nodes for extract.
  auto id = a.world().CreateNode("cube");
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "builtin.cube";
  a.world().set_mesh(id, std::move(mesh));

  engine::render::Environment env;
  engine::render::CascadedShadowMap csm;
  csm.set_cascade_count(2);
  csm.Build(env.sun_direction, 0.1f, 100.f);

  engine::post::PostStack post;
  post.Configure(engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium));

  auto physics = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc box;
  box.position = {0, 3, 0};
  physics->CreateBox(box);

  auto video = engine::media::CreateD3D12VaDecoderOrStub();
  if (auto st = video->Open("sandbox://demo.mp4"); !st) {
    engine::LogInfo(std::string("Video feature diagnostic: ") + st.message());
  }

  int frames = 0;
  const auto status = a.Run([&](engine::Application& app_ref) {
    physics->Step(app_ref.delta_time());
    ++frames;
    // Auto-close after a few frames so CI/smoke can launch without hanging.
    if (frames > 180) {
      // Window has no Close API in skeleton — rely on user/manual stop for interactive.
    }
    (void)app_ref.render_scene();
    (void)post;
    (void)csm;
  });
  return status ? 0 : 1;
}

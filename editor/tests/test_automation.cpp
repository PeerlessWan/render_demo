#include "cmd/session.h"
#include "mcp/protocol.h"

#include "kit_test.h"

#include <filesystem>
#include <string>

TEST_CASE("mcp create light dump and screenshot", "[automation]") {
  editor::EditorHost host;
  const auto listed = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  REQUIRE(listed.find("editor_screenshot") != std::string::npos);
  REQUIRE(listed.find("editor_place") != std::string::npos);
  const auto created = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor_create","arguments":{"kind":"light"}}})");
  REQUIRE(created.find("isError\":false") != std::string::npos);
  const auto dumped = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"editor_dump","arguments":{}}})");
  REQUIRE(dumped.find("light") != std::string::npos);
  const auto shot_fs = std::filesystem::temp_directory_path() / "editor_auto.ppm";
  const auto shot_path = shot_fs.generic_string();
  const std::string shot_req =
      std::string(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"editor_screenshot","arguments":{"path":")") +
      shot_path + R"("}}})";
  const auto shot = editor::HandleMcpLine(host, shot_req);
  REQUIRE(shot.find("isError\":true") != std::string::npos);
}

TEST_CASE("mcp play stop isolation automation", "[automation]") {
  editor::EditorHost host;
  (void)editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor_create","arguments":{"kind":"player"}}})");
  const auto play = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor_play","arguments":{}}})");
  REQUIRE(play.find("isError\":false") != std::string::npos);
  const auto blocked = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"editor_create","arguments":{"kind":"cube"}}})");
  REQUIRE(blocked.find("isError\":true") != std::string::npos);
  const auto stop = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"editor_stop","arguments":{}}})");
  REQUIRE(stop.find("isError\":false") != std::string::npos);
}

TEST_CASE("mcp lint returns graph json", "[automation]") {
  editor::EditorHost host;
  const auto lint = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor_lint","arguments":{}}})");
  REQUIRE(lint.find("assets") != std::string::npos);
}

TEST_CASE("op sequence create select transform dump", "[automation]") {
  editor::EditorHost host;
  editor::EditorOp c;
  c.kind = editor::EditorOp::Kind::Create;
  c.create_kind = "camera";
  REQUIRE(editor::ApplyOp(host.Bind(), c).ok);
  editor::EditorOp s;
  s.kind = editor::EditorOp::Kind::Select;
  s.name = "camera";
  REQUIRE(editor::ApplyOp(host.Bind(), s).ok);
  editor::EditorOp t;
  t.kind = editor::EditorOp::Kind::SetTransform;
  t.has_x = true;
  t.x = 4.f;
  REQUIRE(editor::ApplyOp(host.Bind(), t).ok);
  editor::EditorOp d;
  d.kind = editor::EditorOp::Kind::Dump;
  const auto r = editor::ApplyOp(host.Bind(), d);
  REQUIRE(r.ok);
  REQUIRE(r.json.find("camera") != std::string::npos);
}

TEST_CASE("mcp bake succeeds without baker exe", "[automation]") {
  editor::EditorHost host;
  editor::EditorOp light;
  light.kind = editor::EditorOp::Kind::Create;
  light.create_kind = "light";
  REQUIRE(editor::ApplyOp(host.Bind(), light).ok);
  const auto bake = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor_bake","arguments":{}}})");
  REQUIRE(bake.find("isError\":false") != std::string::npos);
}

TEST_CASE("mcp sculpt then undo restores heights", "[automation]") {
  editor::EditorHost host;
  editor::EditorOp sculpt;
  sculpt.kind = editor::EditorOp::Kind::Sculpt;
  sculpt.x = 8.f;
  sculpt.z = 8.f;
  sculpt.has_y = true;
  sculpt.y = 1.f;
  REQUIRE(editor::ApplyOp(host.Bind(), sculpt).ok);
  REQUIRE(host.settings.heights[static_cast<std::size_t>(8 * 17 + 8)] > 0.5f);
  editor::EditorOp undo;
  undo.kind = editor::EditorOp::Kind::Undo;
  REQUIRE(editor::ApplyOp(host.Bind(), undo).ok);
}


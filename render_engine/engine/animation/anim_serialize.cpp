#include "engine/animation/anim_serialize.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace engine::animation {
namespace {

std::string Escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == '"' || c == ' ' || c == '\n' || c == '\r') {
      out.push_back('\\');
      if (c == '\n') {
        out.push_back('n');
      } else if (c == '\r') {
        out.push_back('r');
      } else {
        out.push_back(c);
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string Unescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      if (n == 'n') {
        out.push_back('\n');
      } else if (n == 'r') {
        out.push_back('\r');
      } else {
        out.push_back(n);
      }
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

const char* OpName(BlendTreeOp op) {
  switch (op) {
    case BlendTreeOp::Clip:
      return "Clip";
    case BlendTreeOp::Blend1D:
      return "Blend1D";
    case BlendTreeOp::Blend2D:
      return "Blend2D";
    case BlendTreeOp::Masked:
      return "Masked";
  }
  return "Clip";
}

bool ParseOp(std::string_view s, BlendTreeOp& out) {
  if (s == "Clip") {
    out = BlendTreeOp::Clip;
    return true;
  }
  if (s == "Blend1D") {
    out = BlendTreeOp::Blend1D;
    return true;
  }
  if (s == "Blend2D") {
    out = BlendTreeOp::Blend2D;
    return true;
  }
  if (s == "Masked") {
    out = BlendTreeOp::Masked;
    return true;
  }
  return false;
}

void WriteNode(std::ostringstream& os, const BlendTreeNode& n, int depth) {
  const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
  os << indent << "node op=" << OpName(n.op) << " px=" << n.param_x << " py=" << n.param_y
     << " alpha=" << n.alpha << " clip=" << Escape(n.clip.name) << " dur=" << n.clip.duration
     << "\n";
  if (!n.mask.weights.empty()) {
    os << indent << "  mask";
    for (float w : n.mask.weights) {
      os << " " << w;
    }
    os << "\n";
  }
  for (const auto& c : n.children) {
    WriteNode(os, c, depth + 1);
  }
  os << indent << "end\n";
}

// Recursive descent: consume lines starting at *line_i.
bool ReadNode(const std::vector<std::string>& lines, std::size_t& line_i, BlendTreeNode& out) {
  if (line_i >= lines.size()) {
    return false;
  }
  std::istringstream ls(lines[line_i]);
  std::string tag;
  ls >> tag;
  if (tag != "node") {
    return false;
  }
  ++line_i;
  out = BlendTreeNode{};
  std::string tok;
  while (ls >> tok) {
    const auto eq = tok.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = tok.substr(0, eq);
    const std::string val = tok.substr(eq + 1);
    if (key == "op") {
      BlendTreeOp op{};
      if (!ParseOp(val, op)) {
        return false;
      }
      out.op = op;
    } else if (key == "px") {
      out.param_x = std::stof(val);
    } else if (key == "py") {
      out.param_y = std::stof(val);
    } else if (key == "alpha") {
      out.alpha = std::stof(val);
    } else if (key == "clip") {
      out.clip.name = Unescape(val);
    } else if (key == "dur") {
      out.clip.duration = std::stof(val);
    }
  }
  while (line_i < lines.size()) {
    std::istringstream peek(lines[line_i]);
    std::string ptag;
    peek >> ptag;
    if (ptag == "end") {
      ++line_i;
      return true;
    }
    if (ptag == "mask") {
      out.mask.weights.clear();
      float w = 0.f;
      while (peek >> w) {
        out.mask.weights.push_back(w);
      }
      ++line_i;
      continue;
    }
    if (ptag == "node") {
      BlendTreeNode child;
      if (!ReadNode(lines, line_i, child)) {
        return false;
      }
      out.children.push_back(std::move(child));
      continue;
    }
    return false;
  }
  return false;
}

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : text) {
    if (c == '\n') {
      if (!cur.empty() && cur.back() == '\r') {
        cur.pop_back();
      }
      // trim leading spaces for indent-insensitive tags we care about
      std::size_t i = 0;
      while (i < cur.size() && (cur[i] == ' ' || cur[i] == '\t')) {
        ++i;
      }
      if (i < cur.size()) {
        lines.push_back(cur.substr(i));
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    std::size_t i = 0;
    while (i < cur.size() && (cur[i] == ' ' || cur[i] == '\t')) {
      ++i;
    }
    if (i < cur.size()) {
      lines.push_back(cur.substr(i));
    }
  }
  return lines;
}

}  // namespace

std::string SerializeBlendTree(const BlendTreeNode& root) {
  std::ostringstream os;
  os << "bt {\n";
  WriteNode(os, root, 1);
  os << "}\n";
  return os.str();
}

Status DeserializeBlendTree(std::string_view text, BlendTreeNode& out) {
  const auto lines = SplitLines(text);
  if (lines.empty() || lines[0] != "bt {") {
    return Status::Fail(ErrorCode::InvalidArgument, "DeserializeBlendTree: expected 'bt {'");
  }
  std::size_t i = 1;
  if (!ReadNode(lines, i, out)) {
    return Status::Fail(ErrorCode::InvalidArgument, "DeserializeBlendTree: node parse failed");
  }
  return Status::Ok();
}

std::string SerializeStateMachine(const AnimationStateMachine& sm) {
  std::ostringstream os;
  os << "sm {\n";
  os << "  crossfade_default=" << sm.default_crossfade_duration() << "\n";
  for (const auto& s : sm.states()) {
    os << "  state name=" << Escape(s.name) << " clip=" << Escape(s.clip.name)
       << " dur=" << s.clip.duration << " loop=" << (s.loop ? 1 : 0) << "\n";
  }
  for (const auto& t : sm.transitions()) {
    os << "  trans from=" << Escape(t.from) << " to=" << Escape(t.to)
       << " exit=" << t.exit_time << " has_exit=" << (t.has_exit_time ? 1 : 0)
       << " fade=" << t.crossfade_duration << " trigger=" << Escape(t.trigger) << "\n";
  }
  os << "  current=" << Escape(sm.current_state()) << "\n";
  os << "  time=" << sm.state_time() << "\n";
  os << "}\n";
  return os.str();
}

Status DeserializeStateMachine(std::string_view text, AnimationStateMachine& out) {
  const auto lines = SplitLines(text);
  if (lines.empty() || lines[0] != "sm {") {
    return Status::Fail(ErrorCode::InvalidArgument, "DeserializeStateMachine: expected 'sm {'");
  }
  out = AnimationStateMachine{};
  std::string current;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    if (lines[i] == "}") {
      break;
    }
    std::istringstream ls(lines[i]);
    std::string tag;
    ls >> tag;
    if (tag.rfind("crossfade_default=", 0) == 0) {
      out.SetDefaultCrossfadeDuration(std::stof(tag.substr(18)));
      continue;
    }
    if (tag == "state") {
      AnimState st;
      std::string tok;
      while (ls >> tok) {
        const auto eq = tok.find('=');
        if (eq == std::string::npos) {
          continue;
        }
        const std::string key = tok.substr(0, eq);
        const std::string val = tok.substr(eq + 1);
        if (key == "name") {
          st.name = Unescape(val);
        } else if (key == "clip") {
          st.clip.name = Unescape(val);
        } else if (key == "dur") {
          st.clip.duration = std::stof(val);
        } else if (key == "loop") {
          st.loop = (val != "0");
        }
      }
      out.AddState(std::move(st));
      continue;
    }
    if (tag == "trans") {
      AnimTransition tr;
      std::string tok;
      while (ls >> tok) {
        const auto eq = tok.find('=');
        if (eq == std::string::npos) {
          continue;
        }
        const std::string key = tok.substr(0, eq);
        const std::string val = tok.substr(eq + 1);
        if (key == "from") {
          tr.from = Unescape(val);
        } else if (key == "to") {
          tr.to = Unescape(val);
        } else if (key == "exit") {
          tr.exit_time = std::stof(val);
        } else if (key == "has_exit") {
          tr.has_exit_time = (val != "0");
        } else if (key == "fade") {
          tr.crossfade_duration = std::stof(val);
        } else if (key == "trigger") {
          tr.trigger = Unescape(val);
        }
      }
      out.AddTransition(std::move(tr));
      continue;
    }
    if (tag.rfind("current=", 0) == 0) {
      current = Unescape(tag.substr(8));
      continue;
    }
    // time= is informational; topology round-trip does not restore clock.
  }
  if (!current.empty()) {
    // Snap without default crossfade so deserialize restores current cleanly.
    const float saved_fade = out.default_crossfade_duration();
    out.SetDefaultCrossfadeDuration(0.f);
    out.SetState(current);
    out.SetDefaultCrossfadeDuration(saved_fade);
  }
  return Status::Ok();
}

Status WriteBlendTreeFile(const std::filesystem::path& path, const BlendTreeNode& root) {
  if (path.empty()) {
    return Status::Fail(ErrorCode::InvalidArgument, "WriteBlendTreeFile: empty path");
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "WriteBlendTreeFile: open failed");
  }
  out << SerializeBlendTree(root);
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "WriteBlendTreeFile: write failed");
  }
  return Status::Ok("anim-bt-file");
}

Status ReadBlendTreeFile(const std::filesystem::path& path, BlendTreeNode& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Status::Fail(ErrorCode::NotFound, "ReadBlendTreeFile: open failed");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return DeserializeBlendTree(ss.str(), out);
}

Status WriteStateMachineFile(const std::filesystem::path& path, const AnimationStateMachine& sm) {
  if (path.empty()) {
    return Status::Fail(ErrorCode::InvalidArgument, "WriteStateMachineFile: empty path");
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "WriteStateMachineFile: open failed");
  }
  out << SerializeStateMachine(sm);
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "WriteStateMachineFile: write failed");
  }
  return Status::Ok("anim-sm-file");
}

Status ReadStateMachineFile(const std::filesystem::path& path, AnimationStateMachine& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Status::Fail(ErrorCode::NotFound, "ReadStateMachineFile: open failed");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return DeserializeStateMachine(ss.str(), out);
}

}  // namespace engine::animation

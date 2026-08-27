// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/snapshot/snapshot_composer.h"

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"

#include <functional>
#include <map>
#include <set>

namespace prism {

namespace {

// ego-parity curated roles (docs/ego-parity-notes.md): semantic lowercase
// HTML-flavored names instead of raw AX roles.
std::string MapRole(const std::string& role) {
  static const std::map<std::string, std::string>* mapping =
      new std::map<std::string, std::string>{
          {"RootWebArea", "root"}, {"link", "anchor"},
          {"StaticText", "text"},  {"heading", "heading"},
          {"paragraph", "paragraph"}, {"list", "list"},
          {"listitem", "item"},    {"image", "image"},
          {"img", "image"},        {"button", "button"},
          {"searchbox", "textbox"}, {"textbox", "textbox"},
          {"checkbox", "checkbox"}, {"radio", "radio"},
          {"switch", "switch"},    {"combobox", "combobox"},
          {"listbox", "listbox"},  {"menuitem", "menuitem"},
          {"tab", "tab"},          {"option", "option"},
          {"slider", "slider"},    {"spinbutton", "spinbutton"},
      };
  auto it = mapping->find(role);
  if (it != mapping->end()) {
    return it->second;
  }
  // Fallback: lowercase, spaces stripped (LayoutTableRow -> layouttablerow).
  std::string out;
  out.reserve(role.size());
  for (char c : role) {
    if (c == ' ') {
      continue;
    }
    out.push_back(static_cast<char>(tolower(c)));
  }
  return out;
}

// Actionable roles (in the MAPPED namespace) get refs and loc= annotations;
// everything else renders bare. Same set as the JS composer pre-densify.
const std::set<std::string>& ActionableRoles() {
  static const std::set<std::string>* roles = new std::set<std::string>{
      "button",   "anchor",   "textbox", "combobox", "listbox",
      "menuitem", "tab",      "checkbox", "radio",   "switch",
      "slider",   "spinbutton", "option",
  };
  return *roles;
}

// Container roles print bare and carry their text as folded `text` children.
const std::set<std::string>& ContainerRoles() {
  static const std::set<std::string>* roles = new std::set<std::string>{
      "root", "heading", "paragraph", "list", "item",
  };
  return *roles;
}

std::string EscapeLocatorName(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

// A composed tree node: points at the source AX node and carries spliced
// children (same-frame children plus attached child frames).
struct ComposedNode {
  raw_ptr<const SnapshotAXNode> ax =
      nullptr;  // null only for the virtual super-root
  raw_ptr<const SnapshotFrameData> frame;  // the frame this node came from
  std::vector<ComposedNode> children;
};

bool HasActionableDescendant(const ComposedNode& node) {
  for (const auto& child : node.children) {
    if (child.ax && ActionableRoles().count(MapRole(child.ax->role))) {
      return true;
    }
    if (HasActionableDescendant(child)) {
      return true;
    }
  }
  return false;
}

struct FrameTree {
  raw_ptr<const SnapshotFrameData> frame = nullptr;
  std::map<std::string, raw_ptr<const SnapshotAXNode>> by_id;
  raw_ptr<const SnapshotAXNode> root = nullptr;
};

FrameTree BuildFrameTree(const SnapshotFrameData& frame) {
  FrameTree out;
  out.frame = &frame;
  std::set<std::string> referenced;
  for (const auto& node : frame.nodes) {
    out.by_id[node.id] = &node;
    for (const auto& child : node.child_ids) {
      referenced.insert(child);
    }
  }
  for (const auto& node : frame.nodes) {
    if (node.parent_id.empty() && !referenced.count(node.id)) {
      out.root = &node;
      break;
    }
  }
  return out;
}

bool InViewport(const SnapshotAXNode& node, const SnapshotFrameData& frame) {
  if (!node.has_box) {
    return true;  // unknown geometry: keep (structure stays complete)
  }
  if (node.box_w <= 0 || node.box_h <= 0) {
    return false;  // zero-area (display:none, collapsed)
  }
  const float right = node.box_x + node.box_w;
  const float bottom = node.box_y + node.box_h;
  return right > 0 && bottom > 0 && node.box_x < frame.viewport_width &&
         node.box_y < frame.viewport_height;
}

class Renderer {
 public:
  Renderer(const SnapshotOptions& options, std::vector<SnapshotRefEntry>* refs,
           std::string* content)
      : options_(options), refs_(refs), content_(content) {}

  void Render(const ComposedNode& node, int depth) {
    const SnapshotAXNode* ax = node.ax;
    if (ax && ax->ignored) {
      for (const auto& child : node.children) {
        Render(child, depth);
      }
      return;
    }
    if (ax) {
      if (options_->only_within_viewport && ax->backend_node_id > 0 &&
          node.frame && !InViewport(*ax, *node.frame)) {
        return;  // off-viewport element: drop line and subtree
      }
      RenderNode(node, depth);
    } else {
      RenderChildren(node, depth);
    }
  }

 private:
  void RenderChildren(const ComposedNode& node, int depth) {
    // Consecutive `text` children merge into one line (space-joined).
    std::string pending_text;
    for (const auto& child : node.children) {
      const SnapshotAXNode* cax = child.ax;
      if (cax && MapRole(cax->role) == "text") {
        const std::string& piece =
            !cax->name.empty() ? cax->name : cax->value;
        if (!piece.empty()) {
          if (!pending_text.empty()) {
            pending_text += " ";
          }
          pending_text += piece;
          continue;
        }
      }
      FlushText(pending_text, depth);
      Render(child, depth);
    }
    FlushText(pending_text, depth);
  }

  void FlushText(std::string& pending_text, int depth) {
    if (pending_text.empty()) {
      return;
    }
    std::string line(depth * 2, ' ');
    line += "text \"" + pending_text + "\"";
    *content_ += line + "\n";
    pending_text.clear();
  }

  void RenderNode(const ComposedNode& node, int depth) {
    const SnapshotAXNode& ax = *node.ax;
    const std::string role = MapRole(ax.role);

    if (role == "text") {
      // Rendered by the parent's merge pass.
      RenderChildren(node, depth);
      return;
    }
    if (ax.role == "InlineTextBox") {
      return;  // folded into the parent StaticText; never rendered
    }

    const bool actionable = ActionableRoles().count(role) > 0;
    const bool container = ContainerRoles().count(role) > 0;
    const std::string& display = !ax.name.empty() ? ax.name : ax.value;

    // Decorative/noise curation: unnamed, non-actionable, non-container nodes
    // are skipped; their children hoist. Containers with interactive
    // descendants keep their line (structure preserved).
    if (!actionable && !container && display.empty()) {
      if (!HasActionableDescendant(node)) {
        RenderChildren(node, depth);
        return;
      }
    }

    std::string line(depth * 2, ' ');
    line += role;

    // Names: containers never print inline (folded text carries it); anchors
    // never print inline (their text child carries it); other actionable
    // nodes keep the name inside loc= only. Non-curated roles with a display
    // string print it.
    const bool print_inline =
        !display.empty() && !container && !actionable;
    if (print_inline) {
      line += " \"" + display + "\"";
    }

    std::string annotations;
    if (actionable && ax.backend_node_id > 0) {
      annotations += "ref=" + std::to_string(ax.backend_node_id);
      if (!seen_refs_.count(ax.backend_node_id)) {
        seen_refs_.insert(ax.backend_node_id);
        refs_->push_back({ax.backend_node_id, role, ax.name});
      }
      if (options_->include_stable_locator) {
        annotations += ", ";
        if (role == "anchor" && !ax.url.empty()) {
          annotations += "loc=href:" + ax.url;
        } else if (!ax.name.empty()) {
          annotations += "loc=role:" + role + "[name=\"" +
                         EscapeLocatorName(ax.name) + "\"]";
        } else {
          annotations.pop_back();  // drop the dangling ", "
          annotations.pop_back();
        }
      }
      if (!ax.url.empty()) {
        annotations += ", url=" + ax.url;
      }
    }
    if (!annotations.empty()) {
      line += " [" + annotations + "]";
    }
    *content_ += line + "\n";
    RenderChildren(node, depth + 1);
  }

  const raw_ref<const SnapshotOptions> options_;
  raw_ptr<std::vector<SnapshotRefEntry>> refs_;
  raw_ptr<std::string> content_;
  std::set<int> seen_refs_;
};

}  // namespace

SnapshotResult ComposeSnapshot(const std::vector<SnapshotFrameData>& frames,
                               const SnapshotOptions& options) {
  SnapshotResult result;
  if (frames.empty() || frames[0].nodes.empty()) {
    return result;
  }

  std::vector<FrameTree> frame_trees;
  frame_trees.reserve(frames.size());
  for (const auto& frame : frames) {
    frame_trees.push_back(BuildFrameTree(frame));
  }

  // Blink's serializer inlines same-PROCESS subframes into an ancestor's tree
  // (a leaf frame hosted in the top frame's renderer shows up there even when
  // its parent frame lives in another process). When such a frame is spliced
  // under its real owner below, its inline copy must be pruned or the content
  // appears twice. Keyed by the frame root's URL (the RootWebArea node's AX
  // "url" property).
  std::set<std::string> spliced_root_urls;
  for (size_t i = 1; i < frames.size(); ++i) {
    const int parent_index = frames[i].parent_frame_index;
    if (parent_index < 0 || frames[i].owner_backend_node_id == 0) {
      continue;
    }
    const FrameTree& parent_tree = frame_trees[parent_index];
    bool owner_found = false;
    for (const auto& [id, node] : parent_tree.by_id) {
      if (node->backend_node_id == frames[i].owner_backend_node_id) {
        owner_found = true;
        break;
      }
    }
    const SnapshotAXNode* root = frame_trees[i].root;
    if (owner_found && root && !root->url.empty()) {
      spliced_root_urls.insert(root->url);
    }
  }

  // Recursive builder as a std::function (frames are few; trees are shallow).
  std::function<void(const SnapshotAXNode*, int, ComposedNode*)> build;
  build = [&](const SnapshotAXNode* ax, int frame_index, ComposedNode* out) {
    out->ax = ax;
    out->frame = &frames[frame_index];
    const FrameTree& own = frame_trees[frame_index];
    for (const auto& child_id : ax->child_ids) {
      auto it = own.by_id.find(child_id);
      if (it == own.by_id.end()) {
        continue;
      }
      const SnapshotAXNode* child_ax = it->second;
      // Prune the inline copy of a frame that is spliced at its real owner.
      if (child_ax->role == "RootWebArea" && !child_ax->url.empty() &&
          spliced_root_urls.count(child_ax->url) &&
          frame_trees[frame_index].root != child_ax) {
        continue;
      }
      ComposedNode& child = out->children.emplace_back();
      build(child_ax, frame_index, &child);
    }
    // Splice: this node is the <iframe> element owning a child frame.
    // IMPORTANT: backendNodeIds are unique per renderer process, not per
    // page — match only child frames whose parent is THIS frame's tree,
    // never an unrelated frame that happens to share the id.
    if (ax->backend_node_id > 0) {
      for (size_t j = 1; j < frames.size(); ++j) {
        if (frames[j].parent_frame_index != frame_index ||
            frames[j].owner_backend_node_id != ax->backend_node_id) {
          continue;
        }
        if (const SnapshotAXNode* child_root = frame_trees[j].root) {
          ComposedNode& child = out->children.emplace_back();
          build(child_root, static_cast<int>(j), &child);
        }
      }
    }
  };

  ComposedNode super_root;
  if (const SnapshotAXNode* top_root = frame_trees[0].root) {
    build(top_root, 0, &super_root.children.emplace_back());
  }

  std::string content;
  Renderer renderer(options, &result.refs, &content);
  for (const auto& child : super_root.children) {
    renderer.Render(child, 0);
  }

  if (options.max_result_length > 0 &&
      static_cast<int>(content.size()) > options.max_result_length) {
    content.resize(options.max_result_length);
  }
  result.content = std::move(content);
  return result;
}

}  // namespace prism

// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/snapshot/snapshot_job.h"

#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "content/browser/devtools/render_frame_devtools_agent_host.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/devtools_agent_host.h"

namespace prism {

namespace {

constexpr base::TimeDelta kJobTimeout = base::Seconds(15);

// Reads the string out of an AXValue dict: {"type":"...", "value": "..."}.
std::string AXValueString(const base::DictValue& ax_value) {
  const base::Value* value = ax_value.Find("value");
  return (value && value->is_string()) ? value->GetString() : std::string();
}

// The actionable-role set mirrored from the composer; box queries only run
// for these nodes (they are the ref candidates the viewport filter drops).
bool NeedsBox(const SnapshotAXNode& ax) {
  if (ax.backend_node_id <= 0) {
    return false;
  }
  static const std::set<std::string>* actionable = new std::set<std::string>{
      "button",  "link",     "textbox",  "searchbox", "combobox",
      "listbox", "menuitem", "tab",      "checkbox",  "radio",
      "switch",  "slider",   "spinbutton", "option",
  };
  return actionable->count(ax.role) > 0;
}

}  // namespace

SnapshotJob::SnapshotJob(content::WebContents* web_contents,
                         const SnapshotOptions& options,
                         DoneCallback callback)
    : web_contents_(web_contents),
      options_(options),
      callback_(std::move(callback)) {}

SnapshotJob::~SnapshotJob() {
  DetachAll();
}

void SnapshotJob::Run() {
  watchdog_.Start(FROM_HERE, kJobTimeout,
                  base::BindOnce(&SnapshotJob::Fail, weak_factory_.GetWeakPtr(),
                                 "timeout"));
  CollectFrames();
  if (frames_.empty()) {
    Fail("no frames in the active tab");
    return;
  }
  ProcessNextFrame();
}

// ---------------------------------------------------------------- frame walk

void SnapshotJob::CollectFrames() {
  content::WebContentsImpl* wci =
      static_cast<content::WebContentsImpl*>(web_contents_);
  content::FrameTree& tree = wci->GetPrimaryFrameTree();

  // Preorder walk collecting local roots (the top frame plus every OOPIF
  // root). `enclosing` tracks the nearest enclosing local root — the frame
  // whose renderer holds this frame's owner <iframe> element.
  struct StackEntry {
    raw_ptr<content::FrameTreeNode> node;
    int enclosing;
  };
  std::vector<StackEntry> stack{{tree.root(), -1}};
  while (!stack.empty()) {
    auto [node, enclosing] = stack.back();
    stack.pop_back();
    content::RenderFrameHostImpl* rfh = node->current_frame_host();
    if (!rfh) {
      continue;
    }
    int my_index = enclosing;
    if (rfh->is_local_root()) {
      auto work = std::make_unique<FrameWork>();
      work->node = node;
      work->parent_local_root_index = enclosing;
      work->frame_token = rfh->GetDevToolsFrameToken().ToString();
      work->data.frame_token = work->frame_token;
      work->data.parent_frame_index = enclosing;
      my_index = static_cast<int>(frames_.size());
      frames_.push_back(std::move(work));
    }
    for (size_t i = node->child_count(); i > 0; --i) {
      stack.push_back({node->child_at(i - 1), my_index});
    }
  }
}

// ------------------------------------------------------------ per-frame flow

void SnapshotJob::ProcessNextFrame() {
  if (current_frame_ >= frames_.size()) {
    std::vector<SnapshotFrameData> composed_input;
    composed_input.reserve(frames_.size());
    for (const auto& frame : frames_) {
      composed_input.push_back(std::move(frame->data));
    }
    Finish(ComposeSnapshot(composed_input, options_));
    return;
  }

  FrameWork& frame = *frames_[current_frame_];
  frame.agent_host =
      content::RenderFrameDevToolsAgentHost::GetOrCreateFor(frame.node);
  if (!frame.agent_host) {
    Fail("no devtools agent host for frame " + frame.frame_token);
    return;
  }
  frame.agent_host->AttachClient(this);

  if (options_.only_within_viewport) {
    SendCommand(frame.agent_host.get(), "Runtime.evaluate",
                R"({"expression":"[innerWidth,innerHeight]","returnByValue":true})",
                base::BindOnce(&SnapshotJob::OnViewportDims,
                               weak_factory_.GetWeakPtr()));
    return;
  }
  RequestAXTree();
}

void SnapshotJob::OnViewportDims(bool ok, const std::string& body) {
  if (!ok) {
    Fail("Runtime.evaluate viewport failed: " + body);
    return;
  }
  auto parsed = base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  const base::ListValue* dims =
      parsed ? parsed->GetDict().FindListByDottedPath("result.value")
             : nullptr;
  if (dims && dims->size() == 2) {
    frames_[current_frame_]->data.viewport_width =
        static_cast<float>((*dims)[0].GetDouble());
    frames_[current_frame_]->data.viewport_height =
        static_cast<float>((*dims)[1].GetDouble());
  }
  RequestAXTree();
}

void SnapshotJob::RequestAXTree() {
  FrameWork& frame = *frames_[current_frame_];
  SendCommand(frame.agent_host.get(), "Accessibility.getFullAXTree", "{}",
              base::BindOnce(&SnapshotJob::OnAXTree,
                             weak_factory_.GetWeakPtr()));
}

void SnapshotJob::OnAXTree(bool ok, const std::string& body) {
  if (!ok) {
    Fail("Accessibility.getFullAXTree failed: " + body);
    return;
  }
  auto parsed = base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  const base::ListValue* nodes =
      parsed ? parsed->GetDict().FindList("nodes") : nullptr;
  if (!nodes) {
    Fail("getFullAXTree returned no nodes");
    return;
  }
  FrameWork& frame = *frames_[current_frame_];
  for (const base::Value& node_value : *nodes) {
    const base::DictValue* node = node_value.GetIfDict();
    if (!node) {
      continue;
    }
    SnapshotAXNode ax;
    if (const std::string* id = node->FindString("nodeId")) {
      ax.id = *id;
    }
    if (const std::string* parent = node->FindString("parentId")) {
      ax.parent_id = *parent;
    }
    ax.ignored = node->FindBool("ignored").value_or(false);
    if (const base::DictValue* role = node->FindDict("role")) {
      ax.role = AXValueString(*role);
    }
    if (const base::DictValue* name = node->FindDict("name")) {
      ax.name = AXValueString(*name);
    }
    if (const base::DictValue* value = node->FindDict("value")) {
      ax.value = AXValueString(*value);
    }
    ax.backend_node_id = node->FindInt("backendDOMNodeId").value_or(0);
    if (const base::ListValue* children = node->FindList("childIds")) {
      for (const base::Value& child : *children) {
        if (child.is_string()) {
          ax.child_ids.push_back(child.GetString());
        }
      }
    }
    if (const base::ListValue* properties = node->FindList("properties")) {
      for (const base::Value& property : *properties) {
        const base::DictValue* prop = property.GetIfDict();
        if (!prop) {
          continue;
        }
        const std::string* prop_name = prop->FindString("name");
        if (prop_name && *prop_name == "url") {
          if (const base::DictValue* prop_value = prop->FindDict("value")) {
            ax.url = AXValueString(*prop_value);
          }
        }
      }
    }
    if (!ax.id.empty()) {
      frame.data.nodes.push_back(std::move(ax));
    }
  }

  if (current_frame_ == 0) {
    MaybeQueryBoxes();
    return;
  }
  // Child frame: resolve the owner element's backendNodeId via the parent's
  // (still attached) session.
  FrameWork& parent = *frames_[frame.parent_local_root_index];
  SendCommand(parent.agent_host.get(), "DOM.getFrameOwner",
              "{\"frameId\":\"" + frame.frame_token + "\"}",
              base::BindOnce(&SnapshotJob::OnFrameOwner,
                             weak_factory_.GetWeakPtr()));
}

void SnapshotJob::OnFrameOwner(bool ok, const std::string& body) {
  if (!ok) {
    Fail("DOM.getFrameOwner failed: " + body);
    return;
  }
  auto parsed = base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  int owner =
      parsed ? parsed->GetDict().FindInt("backendNodeId").value_or(0) : 0;
  if (owner == 0) {
    Fail("DOM.getFrameOwner returned no owner for frame " +
         frames_[current_frame_]->frame_token);
    return;
  }
  frames_[current_frame_]->data.owner_backend_node_id = owner;
  MaybeQueryBoxes();
}

void SnapshotJob::MaybeQueryBoxes() {
  FrameWork& frame = *frames_[current_frame_];
  pending_box_nodes_.clear();
  current_box_node_ = 0;
  if (!options_.only_within_viewport) {
    FinishFrame();
    return;
  }
  for (size_t i = 0; i < frame.data.nodes.size(); ++i) {
    if (NeedsBox(frame.data.nodes[i])) {
      pending_box_nodes_.push_back(i);
    }
  }
  SendNextBoxModelQuery();
}

void SnapshotJob::SendNextBoxModelQuery() {
  FrameWork& frame = *frames_[current_frame_];
  if (current_box_node_ >= pending_box_nodes_.size()) {
    FinishFrame();
    return;
  }
  const size_t node_index = pending_box_nodes_[current_box_node_++];
  const int backend_id = frame.data.nodes[node_index].backend_node_id;
  SendCommand(
      frame.agent_host.get(), "DOM.getBoxModel",
      "{\"backendNodeId\":" + base::NumberToString(backend_id) + "}",
      base::BindOnce(&SnapshotJob::OnBoxModel, weak_factory_.GetWeakPtr(),
                     node_index));
}

void SnapshotJob::OnBoxModel(size_t node_index, bool ok,
                             const std::string& body) {
  if (ok) {
    auto parsed = base::JSONReader::Read(body, base::JSON_PARSE_RFC);
    const base::ListValue* quad =
        parsed ? parsed->GetDict().FindListByDottedPath("model.content")
               : nullptr;
    if (quad && quad->size() == 8) {
      const auto& q = *quad;
      SnapshotAXNode& ax =
          frames_[current_frame_]->data.nodes[node_index];
      ax.has_box = true;
      ax.box_x = static_cast<float>(q[0].GetDouble());
      ax.box_y = static_cast<float>(q[1].GetDouble());
      ax.box_w = static_cast<float>(q[2].GetDouble()) - ax.box_x;
      ax.box_h = static_cast<float>(q[5].GetDouble()) - ax.box_y;
    }
  }
  SendNextBoxModelQuery();
}

void SnapshotJob::FinishFrame() {
  ++current_frame_;
  ProcessNextFrame();
}

// --------------------------------------------------------------- wire & done

void SnapshotJob::SendCommand(content::DevToolsAgentHost* host,
                              const std::string& method,
                              const std::string& params_json,
                              base::OnceCallback<void(bool, const std::string&)>
                                  on_response) {
  const int id = next_request_id_++;
  pending_requests_[id] = std::move(on_response);
  const std::string message =
      "{\"id\":" + base::NumberToString(id) + ",\"method\":\"" + method +
      "\",\"params\":" + params_json + "}";
  host->DispatchProtocolMessage(this, base::as_byte_span(message));
}

void SnapshotJob::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  std::string_view raw(reinterpret_cast<const char*>(message.data()),
                       message.size());
  auto parsed = base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  std::optional<int> id = parsed->GetDict().FindInt("id");
  if (!id) {
    return;  // protocol event; this job only reads responses
  }
  auto it = pending_requests_.find(*id);
  if (it == pending_requests_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  pending_requests_.erase(it);

  if (const base::DictValue* error = parsed->GetDict().FindDict("error")) {
    const std::string* message_str = error->FindString("message");
    std::move(callback).Run(false,
                            message_str ? *message_str : "protocol error");
    return;
  }
  std::string body;
  if (const base::Value* result = parsed->GetDict().Find("result")) {
    base::JSONWriter::Write(*result, &body);
  }
  std::move(callback).Run(true, body);
}

void SnapshotJob::AgentHostClosed(content::DevToolsAgentHost* agent_host) {
  if (callback_) {
    Fail("agent host closed mid-snapshot");
  }
}

std::string SnapshotJob::GetTypeForMetrics() {
  return "Other";
}

void SnapshotJob::DetachAll() {
  std::set<content::DevToolsAgentHost*> detached;
  for (const auto& frame : frames_) {
    if (frame->agent_host && !detached.count(frame->agent_host.get())) {
      frame->agent_host->DetachClient(this);
      detached.insert(frame->agent_host.get());
      frame->agent_host = nullptr;  // safe against the destructor re-detaching
    }
  }
}

void SnapshotJob::Finish(SnapshotResult result) {
  watchdog_.Stop();
  DetachAll();
  if (callback_) {
    std::move(callback_).Run(std::move(result), std::string());
  }
  delete this;  // self-owned; last statement, touches nothing afterwards
}

void SnapshotJob::Fail(const std::string& error) {
  watchdog_.Stop();
  DetachAll();
  if (callback_) {
    std::move(callback_).Run(std::nullopt, error);
  }
  delete this;  // self-owned; last statement, touches nothing afterwards
}

}  // namespace prism

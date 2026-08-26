// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "prism/browser/snapshot/snapshot_composer.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace prism {

namespace {

SnapshotAXNode Node(std::string id, std::string role, std::string name,
                    int backend_id) {
  SnapshotAXNode node;
  node.id = std::move(id);
  node.role = std::move(role);
  node.name = std::move(name);
  node.backend_node_id = backend_id;
  return node;
}

void Link(SnapshotAXNode& parent, std::initializer_list<std::string> kids) {
  parent.child_ids.assign(kids);
}

}  // namespace

TEST(SnapshotComposerTest, EmptyInputYieldsEmptyResult) {
  auto result = ComposeSnapshot({}, {});
  EXPECT_TRUE(result.content.empty());
  EXPECT_TRUE(result.refs.empty());
}

TEST(SnapshotComposerTest, FlatTreeWithRefsAndLocators) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  auto button = Node("2", "button", "Save", 21);
  auto link = Node("3", "link", "Docs", 22);
  link.url = "https://example.com/docs";
  Link(root, {"2", "3"});
  frame.nodes = {root, button, link};

  auto result = ComposeSnapshot({frame}, {});
  EXPECT_NE(result.content.find("- RootWebArea \"Top\""), std::string::npos);
  EXPECT_NE(result.content.find(
                "- button \"Save\" [ref=21, loc=role:button[name=\"Save\"]]"),
            std::string::npos);
  EXPECT_NE(result.content.find("url=https://example.com/docs"),
            std::string::npos);
  ASSERT_EQ(result.refs.size(), 2u);
  EXPECT_EQ(result.refs[0].backend_node_id, 21);
  EXPECT_EQ(result.refs[1].role, "link");
}

TEST(SnapshotComposerTest, CrossProcessFrameSplicesUnderOwnerNode) {
  SnapshotFrameData top;
  auto root = Node("1", "RootWebArea", "Host", 10);
  auto iframe = Node("2", "InlineFrame", "child frame", 50);
  Link(root, {"2"});
  top.nodes = {root, iframe};

  SnapshotFrameData child;
  child.parent_frame_index = 0;
  child.owner_backend_node_id = 50;
  auto child_root = Node("c1", "RootWebArea", "Child", 60);
  auto child_button = Node("c2", "button", "Inner", 61);
  Link(child_root, {"c2"});
  child.nodes = {child_root, child_button};

  auto result = ComposeSnapshot({top, child}, {});
  // Child content appears indented under the owner iframe node.
  const auto owner_pos = result.content.find("- InlineFrame \"child frame\"");
  const auto inner_pos = result.content.find("- button \"Inner\" [ref=61");
  ASSERT_NE(owner_pos, std::string::npos);
  ASSERT_NE(inner_pos, std::string::npos);
  EXPECT_GT(inner_pos, owner_pos);
  EXPECT_NE(result.content.find("\n    - button \"Inner\""), std::string::npos);
}

TEST(SnapshotComposerTest, UnresolvableOwnerDropsChildFrame) {
  SnapshotFrameData top;
  top.nodes = {Node("1", "RootWebArea", "Host", 10)};
  SnapshotFrameData child;
  child.parent_frame_index = 0;
  child.owner_backend_node_id = 999;  // no such node in the parent
  child.nodes = {Node("c1", "RootWebArea", "Child", 60)};
  auto result = ComposeSnapshot({top, child}, {});
  EXPECT_EQ(result.content.find("Child"), std::string::npos);
}

TEST(SnapshotComposerTest, MaxResultLengthTruncates) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  Link(root, {"2"});
  frame.nodes = {root, Node("2", "button", "Save", 21)};
  SnapshotOptions options;
  options.max_result_length = 20;
  auto result = ComposeSnapshot({frame}, options);
  EXPECT_EQ(result.content.size(), 20u);
}

TEST(SnapshotComposerTest, BackendIdCollisionAcrossProcessesDoesNotMisplace) {
  // backendNodeIds are per-process: the child frame's owner id (9 in the
  // parent's process) collides with an unrelated top-frame node (also 9).
  // The child must splice ONLY under the parent frame's owner node.
  SnapshotFrameData top;
  auto top_root = Node("1", "RootWebArea", "Host", 10);
  auto heading = Node("2", "heading", "Top heading", 9);  // id collision!
  auto iframe = Node("3", "InlineFrame", "", 50);
  Link(top_root, {"2", "3"});
  top.nodes = {top_root, heading, iframe};

  SnapshotFrameData child;
  child.parent_frame_index = 0;
  child.owner_backend_node_id = 50;
  auto child_root = Node("c1", "RootWebArea", "Child", 60);
  child_root.url = "http://b/child";
  auto child_button = Node("c2", "button", "Inner", 61);
  Link(child_root, {"c2"});
  child.nodes = {child_root, child_button};

  auto result = ComposeSnapshot({top, child}, {});
  // Spliced under the Iframe (which comes after the heading), not under it.
  EXPECT_GT(result.content.find("Inner"),
            result.content.find("- InlineFrame"));
  EXPECT_GT(result.content.find("Inner"), result.content.find("Top heading"));
  // Exactly one copy.
  const size_t first = result.content.find("Inner");
  EXPECT_EQ(result.content.find("Inner", first + 1), std::string::npos);
}

TEST(SnapshotComposerTest, InlineCopyOfSplicedFrameIsPruned) {
  // The top frame's tree contains the leaf document inline (same-process
  // hosting, blink serializer behavior); the leaf is also fetched separately
  // and spliced under its real owner in the mid frame. The inline copy must
  // not appear twice.
  SnapshotFrameData top;
  auto top_root = Node("1", "RootWebArea", "Host", 10);
  top_root.url = "http://a/top";
  auto top_iframe = Node("2", "InlineFrame", "", 50);  // owns mid
  auto inline_leaf = Node("3", "RootWebArea", "Leaf", 70);
  inline_leaf.url = "http://a/leaf";
  Link(top_root, {"2", "3"});
  top.nodes = {top_root, top_iframe, inline_leaf};

  SnapshotFrameData mid;
  mid.parent_frame_index = 0;
  mid.owner_backend_node_id = 50;
  auto mid_root = Node("m1", "RootWebArea", "Mid", 60);
  mid_root.url = "http://b/mid";
  auto mid_iframe = Node("m2", "InlineFrame", "", 61);  // owns leaf
  Link(mid_root, {"m2"});
  mid.nodes = {mid_root, mid_iframe};

  SnapshotFrameData leaf;
  leaf.parent_frame_index = 1;
  leaf.owner_backend_node_id = 61;
  auto leaf_root = Node("l1", "RootWebArea", "Leaf", 80);
  leaf_root.url = "http://a/leaf";
  auto leaf_button = Node("l2", "button", "LeafAction", 81);
  Link(leaf_root, {"l2"});
  leaf.nodes = {leaf_root, leaf_button};

  auto result = ComposeSnapshot({top, mid, leaf}, {});
  // Exactly one leaf occurrence, nested under mid's iframe.
  size_t first = result.content.find("LeafAction");
  EXPECT_NE(first, std::string::npos);
  EXPECT_EQ(result.content.find("LeafAction", first + 1), std::string::npos);
  EXPECT_NE(result.content.find("\n        - button \"LeafAction\""),
            std::string::npos);
  // The inline RootWebArea copy under the top frame is gone.
  EXPECT_EQ(result.content.find("\n  - RootWebArea \"Leaf\""), std::string::npos);
}

TEST(SnapshotComposerTest, UnsplicedFrameKeepsItsInlineCopy) {
  // Owner unresolvable (e.g. display:none iframe): the inline copy is the only
  // representation and must survive.
  SnapshotFrameData top;
  auto top_root = Node("1", "RootWebArea", "Host", 10);
  auto inline_leaf = Node("3", "RootWebArea", "Leaf", 70);
  inline_leaf.url = "http://a/leaf";
  Link(top_root, {"3"});
  top.nodes = {top_root, inline_leaf};

  SnapshotFrameData leaf;
  leaf.parent_frame_index = 0;
  leaf.owner_backend_node_id = 999;
  auto leaf_root = Node("l1", "RootWebArea", "Leaf", 80);
  leaf_root.url = "http://a/leaf";
  leaf.nodes = {leaf_root};

  auto result = ComposeSnapshot({top, leaf}, {});
  EXPECT_NE(result.content.find("- RootWebArea \"Leaf\""), std::string::npos);
}

TEST(SnapshotComposerTest, ViewportFilterDropsOffscreenActionables) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  auto visible = Node("2", "button", "Visible", 21);
  visible.has_box = true;
  visible.box_x = 10;
  visible.box_y = 10;
  visible.box_w = 100;
  visible.box_h = 40;
  auto offscreen = Node("3", "button", "Far", 22);
  offscreen.has_box = true;
  offscreen.box_x = 10;
  offscreen.box_y = 5000;  // below the 800px viewport
  offscreen.box_w = 100;
  offscreen.box_h = 40;
  Link(root, {"2", "3"});
  frame.nodes = {root, visible, offscreen};

  SnapshotOptions options;
  options.only_within_viewport = true;
  options.viewport_width = 1280;
  options.viewport_height = 800;
  auto result = ComposeSnapshot({frame}, options);
  EXPECT_NE(result.content.find("Visible"), std::string::npos);
  EXPECT_EQ(result.content.find("Far"), std::string::npos);
  ASSERT_EQ(result.refs.size(), 1u);
  EXPECT_EQ(result.refs[0].backend_node_id, 21);
}

TEST(SnapshotComposerTest, StableLocatorCanBeDisabled) {
  SnapshotFrameData frame;
  frame.nodes = {Node("1", "button", "Save", 21)};
  SnapshotOptions options;
  options.include_stable_locator = false;
  auto result = ComposeSnapshot({frame}, options);
  EXPECT_EQ(result.content.find("loc="), std::string::npos);
  EXPECT_NE(result.content.find("ref=21"), std::string::npos);
}

}  // namespace prism

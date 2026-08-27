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

TEST(SnapshotComposerTest, CuratedRolesAndLinkAnnotations) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  auto button = Node("2", "button", "Save", 21);
  auto link = Node("3", "link", "Docs", 22);
  link.url = "https://example.com/docs";
  Link(root, {"2", "3"});
  frame.nodes = {root, button, link};

  auto result = ComposeSnapshot({frame}, {});
  // ego-parity: containers bare, actionable names live in loc= only, links
  // annotate loc=href + url= (in that order: ref, loc, url).
  EXPECT_NE(result.content.find("root"), std::string::npos);
  EXPECT_NE(result.content.find("button [ref=21, loc=role:button[name=\"Save\"]]"),
            std::string::npos);
  EXPECT_NE(result.content.find(
                "anchor [ref=22, loc=href:https://example.com/docs, url=https://example.com/docs]"),
            std::string::npos);
  // Refs are actionable-only.
  ASSERT_EQ(result.refs.size(), 2u);
  EXPECT_EQ(result.refs[0].backend_node_id, 21);
  EXPECT_EQ(result.refs[0].role, "button");
  EXPECT_EQ(result.refs[0].name, "Save");
  EXPECT_EQ(result.refs[1].role, "anchor");
}

TEST(SnapshotComposerTest, TextFoldsIntoTextLinesAndInlineTextBoxDrops) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  auto heading = Node("2", "heading", "Example Domain", 11);
  auto text = Node("3", "StaticText", "Example Domain", 12);
  auto inline_box = Node("4", "InlineTextBox", "Example Domain", 0);
  Link(root, {"2"});
  Link(heading, {"3"});
  Link(text, {"4"});
  frame.nodes = {root, heading, text, inline_box};

  auto result = ComposeSnapshot({frame}, {});
  EXPECT_EQ(result.content, "root\n  heading\n    text \"Example Domain\"\n");
}

TEST(SnapshotComposerTest, ConsecutiveTextSiblingsMerge) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  auto paragraph = Node("2", "paragraph", "", 11);
  auto t1 = Node("3", "StaticText", "First.", 12);
  auto t2 = Node("4", "StaticText", "Second.", 13);
  Link(root, {"2"});
  Link(paragraph, {"3", "4"});
  frame.nodes = {root, paragraph, t1, t2};

  auto result = ComposeSnapshot({frame}, {});
  EXPECT_EQ(result.content,
            "root\n  paragraph\n    text \"First. Second.\"\n");
}

TEST(SnapshotComposerTest, DecorativeUnnamedNodesSkippedChildrenHoisted) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  auto generic = Node("2", "generic", "", 20);  // no name, nothing actionable
  auto text = Node("3", "StaticText", "Body", 30);
  Link(root, {"2"});
  Link(generic, {"3"});
  frame.nodes = {root, generic, text};

  auto result = ComposeSnapshot({frame}, {});
  // The unnamed generic is skipped; the text line hoists to root depth.
  EXPECT_EQ(result.content, "root\n  text \"Body\"\n");
}

TEST(SnapshotComposerTest, CrossProcessFrameSplicesUnderOwnerNode) {
  SnapshotFrameData top;
  auto root = Node("1", "RootWebArea", "Host", 10);
  auto iframe = Node("2", "InlineFrame", "", 50);
  Link(root, {"2"});
  top.nodes = {root, iframe};

  SnapshotFrameData child;
  child.parent_frame_index = 0;
  child.owner_backend_node_id = 50;
  auto child_root = Node("c1", "RootWebArea", "Child", 60);
  child_root.url = "http://b/child";
  auto child_button = Node("c2", "button", "Inner", 61);
  Link(child_root, {"c2"});
  child.nodes = {child_root, child_button};

  auto result = ComposeSnapshot({top, child}, {});
  const auto owner_pos = result.content.find("inlineframe");
  const auto inner_pos = result.content.find("button [ref=61");
  ASSERT_NE(owner_pos, std::string::npos);
  ASSERT_NE(inner_pos, std::string::npos);
  EXPECT_GT(inner_pos, owner_pos);
  // Spliced deeper than the owner line.
  EXPECT_NE(result.content.find("\n      button [ref=61"), std::string::npos);
  // Exactly one copy of the child button line.
  const std::string needle = "button [ref=61";
  EXPECT_EQ(result.content.find(needle, result.content.find(needle) + 1),
            std::string::npos);
}

TEST(SnapshotComposerTest, UnresolvableOwnerDropsChildFrame) {
  SnapshotFrameData top;
  top.nodes = {Node("1", "RootWebArea", "Host", 10)};
  SnapshotFrameData child;
  child.parent_frame_index = 0;
  child.owner_backend_node_id = 999;  // no such node in the parent
  auto child_root = Node("c1", "RootWebArea", "Child", 60);
  child_root.url = "http://b/child";
  child.nodes = {child_root};
  auto result = ComposeSnapshot({top, child}, {});
  EXPECT_EQ(result.content.find("Child"), std::string::npos);
}

TEST(SnapshotComposerTest, BackendIdCollisionAcrossProcessesDoesNotMisplace) {
  // backendNodeIds are per-process: the child frame's owner id (9 in the
  // parent's process) collides with an unrelated top-frame node (also 9).
  SnapshotFrameData top;
  auto top_root = Node("1", "RootWebArea", "Host", 10);
  auto heading = Node("2", "heading", "Top heading", 9);  // id collision!
  auto heading_text = Node("2t", "StaticText", "Top heading", 0);
  auto iframe = Node("3", "InlineFrame", "", 50);
  Link(top_root, {"2", "3"});
  Link(heading, {"2t"});
  top.nodes = {top_root, heading, heading_text, iframe};

  SnapshotFrameData child;
  child.parent_frame_index = 0;
  child.owner_backend_node_id = 50;
  auto child_root = Node("c1", "RootWebArea", "Child", 60);
  child_root.url = "http://b/child";
  auto child_button = Node("c2", "button", "Inner", 61);
  Link(child_root, {"c2"});
  child.nodes = {child_root, child_button};

  auto result = ComposeSnapshot({top, child}, {});
  EXPECT_GT(result.content.find("button [ref=61"),
            result.content.find("inlineframe"));
  EXPECT_GT(result.content.find("button [ref=61"),
            result.content.find("Top heading"));
  const std::string line = "button [ref=61";
  const size_t first = result.content.find(line);
  ASSERT_NE(first, std::string::npos);
  EXPECT_EQ(result.content.find(line, first + 1), std::string::npos);
}

TEST(SnapshotComposerTest, InlineCopyOfSplicedFrameIsPruned) {
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
  const std::string line = "button [ref=81";
  const size_t first = result.content.find(line);
  ASSERT_NE(first, std::string::npos);
  EXPECT_EQ(result.content.find(line, first + 1), std::string::npos);
  // The inline RootWebArea copy under the top frame is gone.
  EXPECT_EQ(result.content.find("\n  root \"Leaf\""), std::string::npos);
}

TEST(SnapshotComposerTest, UnsplicedFrameKeepsItsInlineCopy) {
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
  EXPECT_NE(result.content.find("root"), std::string::npos);
}

TEST(SnapshotComposerTest, MaxResultLengthTruncates) {
  SnapshotFrameData frame;
  auto root = Node("1", "RootWebArea", "Top", 10);
  Link(root, {"2"});
  frame.nodes = {root, Node("2", "button", "Save", 21)};
  SnapshotOptions options;
  options.max_result_length = 12;
  auto result = ComposeSnapshot({frame}, options);
  EXPECT_EQ(result.content.size(), 12u);
}

TEST(SnapshotComposerTest, ViewportFilterDropsOffscreenActionables) {
  SnapshotFrameData frame;
  frame.viewport_width = 1280;
  frame.viewport_height = 800;
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
  auto result = ComposeSnapshot({frame}, options);
  EXPECT_NE(result.content.find("Visible"), std::string::npos);
  EXPECT_EQ(result.content.find("Far"), std::string::npos);
  // Actionable-only refs: the visible button.
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

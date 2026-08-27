// Copyright 2026 The Prism Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include <optional>

#include "base/test/gtest_util.h"
#include "prism/browser/spaces/space_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace prism {

TEST(SpaceManagerTest, CreateAssignsSequentialIdsAndAgentOwnership) {
  SpaceManager manager;
  auto a = manager.Create("alpha", SpaceManager::Owner::kAgent);
  auto b = manager.Create("beta", SpaceManager::Owner::kAgent);
  ASSERT_TRUE(a.space.has_value());
  ASSERT_TRUE(b.space.has_value());
  EXPECT_EQ(a.space->id + 1, b.space->id);
  EXPECT_EQ(a.space->ownership, SpaceManager::Ownership::kAgent);
  EXPECT_EQ(a.space->task_id, std::to_string(a.space->id));
}

TEST(SpaceManagerTest, UseRejectsUserOwnedSpaces) {
  SpaceManager manager;
  auto created = manager.Create("user-space", SpaceManager::Owner::kUser);
  auto used = manager.Use(created.space->id);
  EXPECT_EQ(used.error, SpaceManager::Error::kUserInControl);
  EXPECT_FALSE(used.space.has_value());
}

TEST(SpaceManagerTest, UseUnknownIdIsNotFound) {
  SpaceManager manager;
  EXPECT_EQ(manager.Use(999).error, SpaceManager::Error::kNotFound);
}

TEST(SpaceManagerTest, ClaimTurnsUserSpaceAgentOwned) {
  SpaceManager manager;
  auto created = manager.Create("shared", SpaceManager::Owner::kUser);
  auto claimed = manager.Claim(created.space->id, "");
  ASSERT_TRUE(claimed.space.has_value());
  EXPECT_EQ(claimed.space->ownership, SpaceManager::Ownership::kAgent);
  EXPECT_EQ(manager.Use(created.space->id).error, SpaceManager::Error::kNone);
}

TEST(SpaceManagerTest, HandOffThenTakeOverRoundTripsControl) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kAgent);
  const int id = created.space->id;

  auto handed_off = manager.HandOff(id);
  EXPECT_EQ(handed_off.space->ownership,
            SpaceManager::Ownership::kAgentDelegatedToUser);

  auto taken_over = manager.TakeOver(id);
  EXPECT_EQ(taken_over.space->ownership, SpaceManager::Ownership::kAgent);
}

TEST(SpaceManagerTest, HandOffOnUserSpaceKeepsOwnership) {
  SpaceManager manager;
  auto created = manager.Create("user-space", SpaceManager::Owner::kUser);
  auto handed_off = manager.HandOff(created.space->id);
  EXPECT_EQ(handed_off.space->ownership, SpaceManager::Ownership::kUser);
}

TEST(SpaceManagerTest, CloseRemovesTheSpace) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kAgent);
  const int id = created.space->id;
  EXPECT_EQ(manager.Close(id), SpaceManager::Error::kNone);
  EXPECT_EQ(manager.Close(id), SpaceManager::Error::kNotFound);
  EXPECT_EQ(manager.Find(id), nullptr);
}

TEST(SpaceManagerTest, AddTabMarksTheNewTabActive) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kAgent);
  const int id = created.space->id;

  SpaceManager::TabRecord first;
  first.target_id = "target-1";
  first.url = "https://a.example/";
  EXPECT_EQ(manager.AddTab(id, first), SpaceManager::Error::kNone);

  SpaceManager::TabRecord second;
  second.target_id = "target-2";
  second.url = "https://b.example/";
  EXPECT_EQ(manager.AddTab(id, second), SpaceManager::Error::kNone);

  const SpaceManager::Space* space = manager.Find(id);
  ASSERT_TRUE(space);
  ASSERT_EQ(space->tabs.size(), 2u);
  EXPECT_FALSE(space->tabs[0].active);
  EXPECT_TRUE(space->tabs[1].active);
}

TEST(SpaceManagerTest, AddTabOnMissingSpaceIsNotFound) {
  SpaceManager manager;
  SpaceManager::TabRecord tab;
  tab.target_id = "target-1";
  EXPECT_EQ(manager.AddTab(999, tab), SpaceManager::Error::kNotFound);
}

TEST(SpaceManagerTest, RemoveTabPromotesThePreviousTab) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kAgent);
  const int id = created.space->id;

  SpaceManager::TabRecord first;
  first.target_id = "target-1";
  SpaceManager::TabRecord second;
  second.target_id = "target-2";
  manager.AddTab(id, first);
  manager.AddTab(id, second);

  EXPECT_TRUE(manager.RemoveTab("target-2"));
  const SpaceManager::Space* space = manager.Find(id);
  ASSERT_TRUE(space);
  ASSERT_EQ(space->tabs.size(), 1u);
  EXPECT_TRUE(space->tabs[0].active);

  EXPECT_FALSE(manager.RemoveTab("target-2"));
  EXPECT_TRUE(manager.RemoveTab("target-1"));
  EXPECT_TRUE(space->tabs.empty());
}

TEST(SpaceManagerTest, FocusedSpaceDefaultsToZeroAndIsSettable) {
  SpaceManager manager;
  EXPECT_EQ(manager.focused_space_id(), 0);

  auto created = manager.Create("demo", SpaceManager::Owner::kUser);
  manager.set_focused_space_id(created.space->id);
  EXPECT_EQ(manager.focused_space_id(), created.space->id);
}

TEST(SpaceManagerTest, ClosingTheFocusedSpaceResetsFocusToDefault) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kAgent);
  const int id = created.space->id;
  manager.set_focused_space_id(id);

  EXPECT_EQ(manager.Close(id), SpaceManager::Error::kNone);
  EXPECT_EQ(manager.focused_space_id(), 0);
}

TEST(SpaceManagerTest, StopAgentMakesAgentSpaceUserOwned) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kAgent);
  const int id = created.space->id;

  auto stopped = manager.StopAgent(id);
  EXPECT_EQ(stopped.error, SpaceManager::Error::kNone);
  EXPECT_EQ(stopped.space->ownership, SpaceManager::Ownership::kUser);
  // A user-owned space rejects agent selection (binding contract §2.3).
  EXPECT_EQ(manager.Use(id).error, SpaceManager::Error::kUserInControl);

  EXPECT_EQ(manager.StopAgent(999).error, SpaceManager::Error::kNotFound);
}

TEST(SpaceManagerTest, StopAgentOnUserSpaceIsANoOp) {
  SpaceManager manager;
  auto created = manager.Create("demo", SpaceManager::Owner::kUser);
  auto stopped = manager.StopAgent(created.space->id);
  EXPECT_EQ(stopped.error, SpaceManager::Error::kNone);
  EXPECT_EQ(stopped.space->ownership, SpaceManager::Ownership::kUser);
}

}  // namespace prism

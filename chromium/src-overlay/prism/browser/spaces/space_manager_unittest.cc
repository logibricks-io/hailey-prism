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

}  // namespace prism

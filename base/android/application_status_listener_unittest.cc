// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/application_status_listener.h"

#include <memory>

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/task_environment.h"
#include "base/threading/thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace android {

namespace {

using base::android::ScopedJavaLocalRef;

// An invalid ApplicationState value.
const ApplicationState kInvalidApplicationState =
    static_cast<ApplicationState>(100);

// Used to generate a callback that stores the new state at a given location.
void StoreStateTo(ApplicationState* target, ApplicationState state) {
  *target = state;
}

void RunTasksUntilIdle() {
  RunLoop run_loop;
  run_loop.RunUntilIdle();
}

// Shared state for the multi-threaded test.
// This uses a thread to register for events and listen to them, while state
// changes are forced on the main thread.
class MultiThreadedTest {
 public:
  MultiThreadedTest()
      : state_(kInvalidApplicationState),
        event_(WaitableEvent::ResetPolicy::AUTOMATIC,
               WaitableEvent::InitialState::NOT_SIGNALED),
        thread_("ApplicationStatusTest thread") {}

  void Run() {
    // Start the thread and tell it to register for events.
    thread_.Start();
    thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&MultiThreadedTest::RegisterThreadForEvents,
                                  base::Unretained(this)));

    // Wait for its completion.
    event_.Wait();

    // Change state, then wait for the thread to modify state.
    ApplicationStatusListener::NotifyApplicationStateChange(
        APPLICATION_STATE_HAS_RUNNING_ACTIVITIES);
    event_.Wait();
    EXPECT_EQ(APPLICATION_STATE_HAS_RUNNING_ACTIVITIES, state_);

    // Again
    ApplicationStatusListener::NotifyApplicationStateChange(
        APPLICATION_STATE_HAS_DESTROYED_ACTIVITIES);
    event_.Wait();
    EXPECT_EQ(APPLICATION_STATE_HAS_DESTROYED_ACTIVITIES, state_);
  }

 private:
  void ExpectOnThread() {
    EXPECT_TRUE(thread_.task_runner()->BelongsToCurrentThread());
  }

  void RegisterThreadForEvents() {
    ExpectOnThread();
    listener_ = ApplicationStatusListener::New(base::BindRepeating(
        &MultiThreadedTest::StoreStateAndSignal, base::Unretained(this)));
    EXPECT_TRUE(listener_.get());
    event_.Signal();
  }

  void StoreStateAndSignal(ApplicationState state) {
    ExpectOnThread();
    state_ = state;
    event_.Signal();
  }

  ApplicationState state_;
  base::WaitableEvent event_;
  base::Thread thread_;
  test::TaskEnvironment task_environment_;
  std::unique_ptr<ApplicationStatusListener> listener_;
};

}  // namespace

TEST(ApplicationStatusListenerTest, SingleThread) {
  test::TaskEnvironment task_environment;

  ApplicationState result = kInvalidApplicationState;

  // Create a new listener that stores the new state into |result| on every
  // state change.
  auto listener = ApplicationStatusListener::New(
      base::BindRepeating(&StoreStateTo, base::Unretained(&result)));

  EXPECT_EQ(kInvalidApplicationState, result);

  ApplicationStatusListener::NotifyApplicationStateChange(
      APPLICATION_STATE_HAS_RUNNING_ACTIVITIES);
  RunTasksUntilIdle();
  EXPECT_EQ(APPLICATION_STATE_HAS_RUNNING_ACTIVITIES, result);

  ApplicationStatusListener::NotifyApplicationStateChange(
      APPLICATION_STATE_HAS_DESTROYED_ACTIVITIES);
  RunTasksUntilIdle();
  EXPECT_EQ(APPLICATION_STATE_HAS_DESTROYED_ACTIVITIES, result);
}

TEST(ApplicationStatusListenerTest, TwoThreads) {
  MultiThreadedTest test;
  test.Run();
}

}  // namespace android
}  // namespace base

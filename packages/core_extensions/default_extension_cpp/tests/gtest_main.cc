//
// This file is part of TEN Framework, an open source project.
// Licensed under the Apache License, Version 2.0.
// See the LICENSE file for more information.
//
#include <cstdio>

#include "gtest/gtest.h"
#include "ten_runtime/binding/cpp/ten.h"
#include "ten_utils/lib/thread.h"

namespace {

class fake_app_t : public ten::app_t {
 public:
  void set_configured_callback(std::function<void()> cb) {
    configured_cb_ = std::move(cb);
  }

  void on_configure(ten::ten_env_t &ten_env) override {
    ten_env.on_configure_done();
  }

  // For fake apps, we use `on_init` instead of `on_configure` to unblock the
  // testing fixture. This is because in the TEN runtime C core, the binding
  // between the addon manager and the fake app occurs after `on_configure_done`
  // is called. We only need to unblock the testing fixture after this binding
  // is complete. The earliest point where we can do this in the upper layer is
  // within the TEN app's `on_init()` function. Therefore, we release the
  // testing fixture lock in the user layer's `on_init()` implementation.
  void on_init(ten::ten_env_t &ten_env) override {
    TEN_ASSERT(configured_cb_, "Configured callback is not set.");
    configured_cb_();

    ten_env.on_init_done();
  }

 private:
  std::function<void()> configured_cb_;
};

struct fake_app_thread_args {
  ten_event_t *event;
  fake_app_t *fake_app;
};

void *fake_app_thread_main(void *args) {
  auto *args_ = static_cast<fake_app_thread_args *>(args);
  TEN_ASSERT(args_->event, "Invalid argument.");

  args_->fake_app = new fake_app_t();
  args_->fake_app->set_configured_callback(
      [args_]() { ten_event_set(args_->event); });
  args_->fake_app->run();

  return nullptr;
}

}  // namespace

class GlobalTestEnvironment : public ::testing::Environment {
 public:
  // This method is run before any test cases.
  void SetUp() override {
    fake_app_thread_args args = {nullptr, nullptr};

    args.event = ten_event_create(0, 1);
    TEN_ASSERT(args.event, "Failed to create event.");

    fake_app_thread_ =
        ten_thread_create("fake_app_thread", fake_app_thread_main, &args);
    TEN_ASSERT(fake_app_thread_, "Failed to create fake app thread.");

    // Wait for the fake app configured.
    ten_event_wait(args.event, -1);
    fake_app_ = args.fake_app;

    TEN_ASSERT(fake_app_, "Failed to create fake app.");

    ten_event_destroy(args.event);
  }

  // This method is run after all test cases.
  void TearDown() override {
    TEN_ASSERT(fake_app_, "Failed to create fake app.");
    fake_app_->close();

    ten_thread_join(fake_app_thread_, -1);

    delete fake_app_;
  }

 private:
  ten::app_t *fake_app_{};
  ten_thread_t *fake_app_thread_{};
};

GTEST_API_ int main(int argc, char **argv) {
  printf("Running main() from %s\n", __FILE__);
  testing::InitGoogleTest(&argc, argv);

  // Add the environment to Google Test.
  ::testing::AddGlobalTestEnvironment(new GlobalTestEnvironment);

  return RUN_ALL_TESTS();
}

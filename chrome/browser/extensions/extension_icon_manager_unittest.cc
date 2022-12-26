// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_icon_manager.h"

#include <memory>

#include "base/command_line.h"
#include "base/json/json_file_value_serializer.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_command_line.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/testing_profile.h"
#include "components/crx_file/id_util.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/common/extension.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/layout.h"
#include "ui/display/display_list.h"
#include "ui/display/display_switches.h"
#include "ui/display/test/scoped_screen_override.h"
#include "ui/display/test/test_screen.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_unittest_util.h"

namespace extensions {
namespace {

class ScopedSetDeviceScaleFactor {
 public:
  explicit ScopedSetDeviceScaleFactor(float scale) {
    display::Display::ResetForceDeviceScaleFactorForTesting();
    // It should be enough just to call Display::SetScaleAndBounds, but on Mac
    // that rounds the scale unless there's a forced device scale factor.
    command_line_.GetProcessCommandLine()->AppendSwitchASCII(
        switches::kForceDeviceScaleFactor, base::StringPrintf("%3.2f", scale));
    // This has to be inited after fiddling with the command line.
    test_screen_ = std::make_unique<display::test::TestScreen>();
    screen_override_ = std::make_unique<display::test::ScopedScreenOverride>(
        test_screen_.get());
  }

  ScopedSetDeviceScaleFactor(const ScopedSetDeviceScaleFactor&) = delete;
  ScopedSetDeviceScaleFactor& operator=(const ScopedSetDeviceScaleFactor&) =
      delete;

  ~ScopedSetDeviceScaleFactor() {
    display::Display::ResetForceDeviceScaleFactorForTesting();
  }

 private:
  std::unique_ptr<display::test::TestScreen> test_screen_;
  std::unique_ptr<display::test::ScopedScreenOverride> screen_override_;
  base::test::ScopedCommandLine command_line_;
};

// Our test class that takes care of managing the necessary threads for loading
// extension icons, and waiting for those loads to happen.
class ExtensionIconManagerTest : public testing::Test,
                                 public ExtensionIconManager::Observer {
 public:
  ExtensionIconManagerTest() : unwaited_image_loads_(0), waiting_(false) {}

  ExtensionIconManagerTest(const ExtensionIconManagerTest&) = delete;
  ExtensionIconManagerTest& operator=(const ExtensionIconManagerTest&) = delete;

  ~ExtensionIconManagerTest() override = default;

  void OnImageLoaded(const std::string& extension_id) override {
    unwaited_image_loads_++;
    if (waiting_) {
      base::RunLoop::QuitCurrentWhenIdleDeprecated();
    }
  }

  void WaitForImageLoad() {
    if (unwaited_image_loads_ == 0) {
      waiting_ = true;
      base::RunLoop().Run();
      waiting_ = false;
    }
    ASSERT_GT(unwaited_image_loads_, 0);
    unwaited_image_loads_--;
  }

 private:
  content::BrowserTaskEnvironment task_environment_;

  // The number of observed image loads that have not been waited for.
  int unwaited_image_loads_;

  // Whether we are currently waiting for an image load.
  bool waiting_;
};

// Returns the default icon that ExtensionIconManager gives when an extension
// doesn't have an icon.
gfx::Image GetDefaultIcon() {
  std::string dummy_id = crx_file::id_util::GenerateId("whatever");
  ExtensionIconManager manager;
  return manager.GetIcon(dummy_id);
}

// Tests loading an icon for an extension, removing it, then re-loading it.
TEST_F(ExtensionIconManagerTest, LoadRemoveLoad) {
  std::unique_ptr<Profile> profile(new TestingProfile());
  gfx::Image default_icon = GetDefaultIcon();

  base::FilePath test_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &test_dir));
  base::FilePath manifest_path = test_dir.AppendASCII(
      "extensions/image_loading_tracker/app.json");

  JSONFileValueDeserializer deserializer(manifest_path);
  std::unique_ptr<base::DictionaryValue> manifest =
      base::DictionaryValue::From(deserializer.Deserialize(NULL, NULL));
  ASSERT_TRUE(manifest.get() != NULL);

  std::string error;
  scoped_refptr<Extension> extension(Extension::Create(
      manifest_path.DirName(), mojom::ManifestLocation::kInvalidLocation,
      *manifest, Extension::NO_FLAGS, &error));
  ASSERT_TRUE(extension.get());
  ExtensionIconManager icon_manager;
  icon_manager.set_observer(this);

  // Load the icon.
  icon_manager.LoadIcon(profile.get(), extension.get());
  WaitForImageLoad();
  gfx::Image first_icon = icon_manager.GetIcon(extension->id());
  EXPECT_FALSE(gfx::test::AreImagesEqual(first_icon, default_icon));

  // Remove the icon from the manager.
  icon_manager.RemoveIcon(extension->id());

  // Now re-load the icon - we should get the same result bitmap (and not the
  // default icon).
  icon_manager.LoadIcon(profile.get(), extension.get());
  WaitForImageLoad();
  gfx::Image second_icon = icon_manager.GetIcon(extension->id());
  EXPECT_FALSE(gfx::test::AreImagesEqual(second_icon, default_icon));

  EXPECT_TRUE(gfx::test::AreImagesEqual(first_icon, second_icon));
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Tests loading an icon for a component extension.
TEST_F(ExtensionIconManagerTest, LoadComponentExtensionResource) {
  std::unique_ptr<Profile> profile(new TestingProfile());
  gfx::Image default_icon = GetDefaultIcon();

  base::FilePath test_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &test_dir));
  base::FilePath manifest_path = test_dir.AppendASCII(
      "extensions/file_manager/app.json");

  JSONFileValueDeserializer deserializer(manifest_path);
  std::unique_ptr<base::DictionaryValue> manifest =
      base::DictionaryValue::From(deserializer.Deserialize(NULL, NULL));
  ASSERT_TRUE(manifest.get() != NULL);

  std::string error;
  scoped_refptr<Extension> extension(Extension::Create(
      manifest_path.DirName(), mojom::ManifestLocation::kComponent,
      *manifest.get(), Extension::NO_FLAGS, &error));
  ASSERT_TRUE(extension.get());

  ExtensionIconManager icon_manager;
  icon_manager.set_observer(this);
  // Load the icon.
  icon_manager.LoadIcon(profile.get(), extension.get());
  WaitForImageLoad();
  gfx::Image first_icon = icon_manager.GetIcon(extension->id());
  EXPECT_FALSE(gfx::test::AreImagesEqual(first_icon, default_icon));

  // Remove the icon from the manager.
  icon_manager.RemoveIcon(extension->id());

  // Now re-load the icon - we should get the same result bitmap (and not the
  // default icon).
  icon_manager.LoadIcon(profile.get(), extension.get());
  WaitForImageLoad();
  gfx::Image second_icon = icon_manager.GetIcon(extension->id());
  EXPECT_FALSE(gfx::test::AreImagesEqual(second_icon, default_icon));

  EXPECT_TRUE(gfx::test::AreImagesEqual(first_icon, second_icon));
}
#endif

// Test what bitmaps are loaded when various combinations of scale factors are
// supported.
TEST_F(ExtensionIconManagerTest, ScaleFactors) {
  auto profile = std::make_unique<TestingProfile>();
  const gfx::Image default_icon = GetDefaultIcon();

  base::FilePath test_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &test_dir));
  base::FilePath manifest_path =
      test_dir.AppendASCII("extensions/context_menus/icons/manifest.json");

  JSONFileValueDeserializer deserializer(manifest_path);
  std::unique_ptr<base::DictionaryValue> manifest =
      base::DictionaryValue::From(deserializer.Deserialize(nullptr, nullptr));
  ASSERT_TRUE(manifest);

  std::string error;
  scoped_refptr<Extension> extension(Extension::Create(
      manifest_path.DirName(), mojom::ManifestLocation::kInvalidLocation,
      *manifest, Extension::NO_FLAGS, &error));
  ASSERT_TRUE(extension);

  constexpr int kMaxIconSizeInManifest = 32;
  std::vector<std::vector<ui::ResourceScaleFactor>> supported_scales = {
      // Base case.
      {ui::k100Percent},
      // Two scale factors.
      {ui::k100Percent, ui::k200Percent},
      // One scale factor for which we have an icon, one scale factor for which
      // we don't.
      {ui::k100Percent, ui::k300Percent},
      // Just a scale factor where we don't have any icon. This falls back to
      // the default icon.
      {ui::k300Percent}};

  for (size_t i = 0; i < supported_scales.size(); ++i) {
    SCOPED_TRACE(testing::Message() << "Test case: " << i);
    // Since active Displays' scale factors are also taken into account, to make
    // the logic in this test work, we need to set the scale factor to one of
    // the "supported" scales.
    ScopedSetDeviceScaleFactor scoped_dsf(
        ui::GetScaleForResourceScaleFactor(supported_scales[i][0]));
    ui::test::ScopedSetSupportedResourceScaleFactors scoped(
        supported_scales[i]);
    ExtensionIconManager icon_manager;
    icon_manager.set_observer(this);

    icon_manager.LoadIcon(profile.get(), extension.get());
    WaitForImageLoad();
    gfx::Image icon = icon_manager.GetIcon(extension->id());
    // Determine if the default icon fallback will be used. We'll use the
    // default when none of the supported scale factors can find an appropriate
    // icon.
    bool should_fall_back_to_default = true;
    for (auto supported_scale : supported_scales[i]) {
      if (gfx::kFaviconSize *
              ui::GetScaleForResourceScaleFactor(supported_scale) <=
          kMaxIconSizeInManifest) {
        should_fall_back_to_default = false;
        break;
      }
    }
    if (should_fall_back_to_default) {
      EXPECT_TRUE(gfx::test::AreImagesEqual(icon, default_icon));
      continue;
    }

    gfx::ImageSkia image_skia = icon.AsImageSkia();

    for (int scale_factor_iter = ui::kScaleFactorNone + 1;
         scale_factor_iter < ui::NUM_SCALE_FACTORS; ++scale_factor_iter) {
      auto scale_factor =
          static_cast<ui::ResourceScaleFactor>(scale_factor_iter);
      float scale = ui::GetScaleForResourceScaleFactor(scale_factor);
      SCOPED_TRACE(testing::Message() << "Scale: " << scale);

      const bool has_representation = image_skia.HasRepresentation(scale);
      // We shouldn't have a representation if the extension didn't provide a
      // big enough icon.
      if (gfx::kFaviconSize * scale > kMaxIconSizeInManifest)
        EXPECT_FALSE(has_representation);
      else
        EXPECT_EQ(ui::IsSupportedScale(scale), has_representation);
    }
  }

  // Now check that the scale factors for active displays are respected, even
  // when it's not a supported scale.
  ScopedSetDeviceScaleFactor scoped_dsf(1.5f);
  ExtensionIconManager icon_manager;
  icon_manager.set_observer(this);
  icon_manager.LoadIcon(profile.get(), extension.get());
  WaitForImageLoad();
  gfx::ImageSkia icon = icon_manager.GetIcon(extension->id()).AsImageSkia();
  EXPECT_TRUE(icon.HasRepresentation(1.5f));
}

}  // namespace
}  // namespace extensions

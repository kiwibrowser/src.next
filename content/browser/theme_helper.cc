// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/theme_helper.h"

#include "base/no_destructor.h"
#include "build/build_config.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/common/renderer.mojom.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_utils.h"

namespace content {

// static
ThemeHelper* ThemeHelper::GetInstance() {
  static base::NoDestructor<ThemeHelper> s_theme_helper;
  return s_theme_helper.get();
}

ThemeHelper::ThemeHelper() : theme_observation_(this) {
  theme_observation_.Observe(ui::NativeTheme::GetInstanceForWeb());
}

ThemeHelper::~ThemeHelper() {}

mojom::UpdateSystemColorInfoParamsPtr MakeUpdateSystemColorInfoParams(
    ui::NativeTheme* native_theme) {
  mojom::UpdateSystemColorInfoParamsPtr params =
      mojom::UpdateSystemColorInfoParams::New();
  params->is_dark_mode = native_theme->ShouldUseDarkColors();
  params->forced_colors = native_theme->InForcedColorsMode();
  const auto& colors = native_theme->GetSystemColors();
  params->colors.insert(colors.begin(), colors.end());

#if BUILDFLAG(IS_CHROMEOS)
  params->accent_color = native_theme->user_color();
#endif

  // TODO(crbug.com/1251637): We should not be using ColorProviders sourced from
  // the global NativeTheme web instance and instead have WebContents instances
  // propagate their specific ColorProviders to hosted frames.
  const auto get_renderer_color_map =
      [](ui::ColorProviderKey::ColorMode color_mode,
         bool override_forced_colors) {
        auto key =
            ui::NativeTheme::GetInstanceForWeb()->GetColorProviderKey(nullptr);
        key.color_mode = color_mode;
        // TODO(samomekarajr): Currently, the light/dark providers are used to
        // paint controls when the OS triggers forced colors mode. To keep
        // current behavior, we shouldn't modify the `forced_colors` key. We
        // should remove the conditional check when we use the forced colors
        // provider for painitng.
        if (override_forced_colors) {
          key.forced_colors = ui::ColorProviderKey::ForcedColors::kActive;
        }
        const auto* color_provider =
            ui::ColorProviderManager::Get().GetColorProviderFor(key);
        DCHECK(color_provider);
        return ui::CreateRendererColorMap(*color_provider);
      };
  params->light_colors =
      get_renderer_color_map(ui::ColorProviderKey::ColorMode::kLight,
                             /*override_forced_colors=*/false);
  params->dark_colors = get_renderer_color_map(
      ui::ColorProviderKey::ColorMode::kDark, /*override_forced_colors=*/false);
  params->forced_colors_map =
      get_renderer_color_map(native_theme->ShouldUseDarkColors()
                                 ? ui::ColorProviderKey::ColorMode::kDark
                                 : ui::ColorProviderKey::ColorMode::kLight,
                             /*override_forced_colors=*/true);
  return params;
}

void ThemeHelper::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  DCHECK(theme_observation_.IsObservingSource(observed_theme));

  mojom::UpdateSystemColorInfoParamsPtr params =
      MakeUpdateSystemColorInfoParams(observed_theme);
  for (auto iter = RenderProcessHost::AllHostsIterator(); !iter.IsAtEnd();
       iter.Advance()) {
    if (iter.GetCurrentValue()->IsInitializedAndNotDead()) {
      iter.GetCurrentValue()->GetRendererInterface()->UpdateSystemColorInfo(
          params->Clone());
    }
  }
}

void ThemeHelper::SendSystemColorInfo(mojom::Renderer* renderer) const {
  mojom::UpdateSystemColorInfoParamsPtr params =
      MakeUpdateSystemColorInfoParams(ui::NativeTheme::GetInstanceForWeb());
  renderer->UpdateSystemColorInfo(std::move(params));
}

}  // namespace content

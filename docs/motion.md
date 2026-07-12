# Motion

`motionSpec(VkMotionRole)` returns a duration and easing curve for immediate changes, state
transitions, entry, exit, and emphasized entry or exit. These restrained specifications let
application animations align with vkui without exposing a controller or QObject ownership model.

Each widget owns its private Qt animation objects. On retargeting, it starts from the current visual
value and replaces the obsolete destination. If animations are disabled through `VkThemeManager`,
drivers stop and immediately apply their final values. vkui never animates stylesheets or widget
layout geometry.


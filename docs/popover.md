# VkPopover

`VkPopover` is a frameless, translucent `Qt::Popup` that owns one caller-supplied content widget.
Opening for a widget anchors to its complete rectangle; the rectangle overload targets a sub-region
such as one icon inside a larger button. Replacing content reparents the new widget and releases the
previous one according to the ownership contract documented in the header.

Automatic placement evaluates Below, Above, Right, and Left against the screen available geometry.
It first chooses a fully fitting candidate, then ranks remaining valid candidates by visible area,
required displacement, and direction priority. Coordinates may be negative. After body clamping,
the arrow is recalculated toward the closest suitable point in the anchor rectangle and its base is
kept outside rounded corners.

The body and curved-arrow shoulders form one continuous `QPainterPath`, giving fill, border, and
shadow a single seam-free outline in all four directions. While open, temporary event filters watch
the anchor, its window, and screen geometry. Repositioning is coalesced to one queued update per
event-loop iteration, and all filters are removed on close.

Opening fades and scales from near the final geometry around the arrow tip, briefly overshoots, and
settles. Closing fades and slightly shrinks to that origin. Interruptions retarget cleanly; disabled
motion policy shows or hides immediately. Close-policy flags independently cover outside click,
Escape, anchor destruction, and window deactivation.

The shadow cache rasterizes only final geometry and applies the pixmap device-pixel ratio exactly
once. Animation transforms that cached result in logical coordinates, avoiding the high-DPI source
rectangle bug that can shift or crop the lower-right blur. A separable in-place blur uses one
scanline scratch buffer instead of a second full-size image. Cache identity follows actual geometry,
DPR, color, blur, and offset inputs, so accent-only theme generations do not rebuild the shadow.

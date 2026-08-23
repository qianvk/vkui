# icon-chosen

`icon-chosen` is a temporary standalone selection tool for the SVG-converted Nerd Fonts v3.4.0
symbol catalog. It is deliberately isolated from `vkui-gallery` and the installed VkUI libraries.

Selections are saved automatically to the platform application-data directory as
`selected-icons.json`. The same stable JSON manifest can be loaded or exported from the app. Loading
a manifest replaces the current choice set and immediately persists the result; the fixed SVG pool
remains embedded in the executable and is never duplicated into the selection file.

#include "ua_stylesheet.h"

// The engine's baked-in UA sheet, Chromium-parity form controls. This text is
// byte-identical to the constant the managed host used to own; it now lives
// here so the native core is the single owner of the engine defaults. The
// string is a static literal valid for the process lifetime.
AETHER_API const char* aether_style_runtime_defaults_css() {
    return R"css(html, body { display: block; }
body { margin: 8px; color: #202124; font-family: "Segoe UI"; font-size: 16px; line-height: 1.25; }
head, title, meta, link, style, script { display: none; }
div, form, p, section, article, header, footer, main, nav, h1, h2, h3, h4, h5, h6 { display: block; }
table { display: table; }
tr { display: table-row; }
td, th { display: table-cell; }
span, label, strong, em, a { display: inline; }
h1 { font-size: 2em; font-weight: 700; margin: .67em 0; }
h2 { font-size: 1.5em; font-weight: 700; margin: .83em 0; }
h3 { font-size: 1.17em; font-weight: 700; margin: 1em 0; }
h4 { font-size: 1em; font-weight: 700; margin: 1.33em 0; }
p { margin: 1em 0; }
input, button { display: inline-block; font-family: "Segoe UI"; font-size: 14px; box-sizing: border-box; }
/* Chromium UA parity for form controls: no min-width floor (a fixed author
   width must win, e.g. a 22px round button), and widget-style padding/border
   instead of the old invented values. System colors (ButtonText/ButtonBorder/
   ButtonFace/FieldText) are not supported by the engine yet, so these UA
   defaults use static light-theme literals as stand-ins. */
input { padding: 1px 0; border: 2px inset #9aa0a6; background-color: white; color: #202124; cursor: text; }
input:focus { border-color: #2563eb; }
input:invalid { border-color: #dc2626; }
button { padding: 2px 6px 3px 6px; border: 2px outset #9aa0a6; background-color: #efefef; color: #202124; cursor: default; text-align: center; }
button:hover { background-color: #e2e4e6; }
button:active { background-color: #d4d7da; }
button:disabled { opacity: .55; cursor: default; })css";
}

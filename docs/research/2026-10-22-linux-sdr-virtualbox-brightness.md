# Linux SDR versus Windows in VirtualBox: brightness investigation

## Conclusion and disposition

The darker Windows VM comparison was affected by a presentation mismatch:
Windows SunPlayer emits piecewise-sRGB pixels, VirtualBox preserves those
values, and Mutter interprets the XWayland window through its gamma-2.2
default. A temporary Linux XWayland build reproduced the darker Windows
appearance; changing only its final encoder to gamma 2.2 made it brighter.
Do not darken correctly managed Wayland output to imitate that reference.

All experimental source/test changes were discarded at the user's request.
Production retains managed gamma-2.2 SDR, display-gamut mapping, the existing
reported-minimum policy, and PQ HDR. X11/XWayland remains outside product scope.

## Experiments and observations

The user compared Windows ordinary SDR in VirtualBox with native Linux SDR
on the same Philips monitor. Original diagnostics showed BT.2446A on both,
but source reference white was 100 nits on Windows and 203 on Linux. Lowering
that setting brightens the result; it does not explain Windows being darker.

| Temporary change | User observation |
| --- | --- |
| Linux SDR minimum changed from reported 0.2 nit to unknown | Slight improvement |
| Gamma-2.2 pixels and declaration changed together to piecewise sRGB | Little difference |
| Mapper target changed from display gamut to BT.709 | No visible difference |
| Linux presented ordinary BT.709/sRGB through XWayland | Looked the same as Windows |
| Same XWayland test emitted gamma-2.2 pixels instead | Became brighter |

Correctly paired encoders and declarations preserve the same light. For
linear gray 0.01, gamma-2.2 encoding gives 0.12328 and sRGB gives 0.09985;
their respective decoders both recover 0.01. Decoding the sRGB value as
gamma 2.2 instead gives 0.006289, about 37% darker. At linear gray 0.1 the
error is only about 1.2%, so the mismatch particularly affects shadows.
Little difference between the two correctly tagged Wayland variants did not
exclude a transfer mismatch in the VM reference.

## Evidence

- Host packages: VirtualBox 7.2.6-dfsg-4, Mutter 50.1-0ubuntu2.4. X11 window
  properties confirmed that the VirtualBox viewer used XWayland. The user
  confirmed the original screenshots came from this viewer, not Remmina.
- Direct guest capture (2560x1357) and VirtualBox window-buffer capture
  (5120x2806) matched exactly over 1,720,000 video pixels after removing the
  46-pixel menu and accounting for 2x replication. All RGB components matched
  in all four replication phases. These captures precede host composition.
  Temporary files were `/tmp/sunplayer-windows-current-framebuffer-2.png` and
  `/tmp/sunplayer-virtualbox-window-buffer-2.png`; they are not durable assets.
- VirtualBox's exact-version source wraps raw RGB32 guest data in QImage and
  draws it through QPixmap/QPainter without a color-space conversion.
  Mutter's XWayland surface inherits the default actor color state:
  BT.709/sRGB primaries with gamma-2.2 transfer.
- Three independent reviews found no remaining intentional mapper
  normalization difference after the unknown-minimum/BT.709 prototypes and
  equal source reference white R: both use peak R, black R/1000, headroom 1,
  composition scale 1, and `q=(203*libraryLinearOutput-B)/(R-B)`.
- Linux libplacebo v7.360.0 and Windows pinned 7.360.1 commit
  `cee9b076f2c63104ccfd497fa79c39a867293ec4` have identical colorspace,
  shader/colorspace, renderer, tone-mapping, gamut-mapping, and libav helper
  source files. No second D3D11 hardware sRGB encode was identified.
- With the observed Philips maximum/reference white of 80/80 and our SDR
  declaration's 80/80, Mutter's luminance multiplier is 1 and its additional
  tone-mapping predicate is false. Linux's final display-primary conversion
  remains paired with a matching declaration.

The diagnostic builds compiled, and the changed Wayland capability test
passed. The XWayland launches used isolated preferences/cache/data under
`/tmp/sunplayer-xwayland-color-probe`; original preferences and VM power
settings were not changed. An initial black capture was unusable; the user
explained that the VM goes to sleep, and the retry supplied the evidence above.

## Still unresolved

The full original screenshot difference was not attributed quantitatively:
frames/crops and reference-white settings differed, and no synchronized final
compositor capture was measured. The A/B results establish a presentation
confound, not universal correctness of Linux HDR or SDR mapping.

The suitability of the reported SDR minimum remains open: 0.2/80 describes
400:1 contrast, while unknown LINEAR minimum makes libplacebo infer 1000:1.
Reported values can be compositor defaults rather than panel measurements.
The separate observation that darkened SDR resembles HDR also remains open.
SDR uses BT.2446A and HDR uses spline with available headroom; resemblance
alone neither validates nor disproves HDR target metadata and normalization.

## Primary sources

- [VirtualBox 7.2.6 framebuffer drawing](https://github.com/VirtualBox/virtualbox/blob/v7.2.6/src/VBox/Frontends/VirtualBox/src/runtime/UIFrameBuffer.cpp#L1440)
- [Mutter 50.1 default color state](https://github.com/GNOME/mutter/blob/50.1/clutter/clutter/clutter-color-manager.c#L147)
- [Mutter XWayland surface](https://github.com/GNOME/mutter/blob/50.1/src/wayland/meta-xwayland-surface.c)
- [Mutter actor surface color-state selection](https://github.com/GNOME/mutter/blob/50.1/src/wayland/meta-wayland-actor-surface.c#L186)
- [Mutter luminance and tone mapping](https://github.com/GNOME/mutter/blob/50.1/clutter/clutter/clutter-color-state-params.c#L564)

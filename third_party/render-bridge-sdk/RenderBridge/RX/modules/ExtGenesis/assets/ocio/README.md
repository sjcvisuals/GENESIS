# ACES / OpenColorIO configs for GENESIS

Bundled default: `studio-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio`
(ACES 2.0 Studio config from AcademySoftwareFoundation/OpenColorIO-Config-ACES v4.0.0).

GENESIS defaults to **Rec.709** display encoding for PIXERA compositing.
Use **Color → Display: ACES OCIO view** with this config (or Import OCIO…) for ACES transforms.

When built with `GENESIS_WITH_OCIO=ON`, the runtime can also open:
`ocio://studio-config-latest`

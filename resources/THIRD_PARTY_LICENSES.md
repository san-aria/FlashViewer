# FlashViewer — Licenses

FlashViewer is released under the **MIT License**.

Copyright (c) FlashViewer contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in the
Software without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so,
subject to the inclusion of the above copyright notice and this permission notice
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

---

## Third-Party Components

FlashViewer bundles or links the following third-party components. Each is
distributed under its own license, reproduced or referenced below.

| Component | Version (typical) | License |
|---|---|---|
| Qt 6 | 6.6+ | LGPLv3 (dynamically linked) |
| GDAL | 3.8+ | MIT / X11 |
| muParser | 2.3+ | BSD 2-Clause |
| spdlog | 1.x | MIT |
| nlohmann/json | 3.x | MIT |
| GLM | 1.x | MIT (Happy Bunny / MIT) |
| Catch2 (tests only) | 3.x | BSL-1.0 |

### Qt 6 — GNU Lesser General Public License v3 (LGPLv3)
Qt is used under the terms of the LGPLv3. Qt libraries are linked **dynamically**
and are shipped unmodified, so a user may relink against a different compatible
build of Qt. The full LGPLv3 text is available at
<https://www.gnu.org/licenses/lgpl-3.0.html>.

### GDAL — MIT / X11 style
Copyright (c) the GDAL/OGR contributors. Permission to use, copy, modify, and
distribute is granted under MIT/X11-style terms. See
<https://gdal.org/license.html>.

### muParser — BSD 2-Clause
Copyright (c) Ingo Berg. Redistribution and use in source and binary forms, with
or without modification, are permitted provided that the copyright notice and
disclaimer are retained.

### spdlog — MIT
Copyright (c) Gabime. Permission is granted under the MIT License.

### nlohmann/json — MIT
Copyright (c) Niels Lohmann. Permission is granted under the MIT License.

### GLM — MIT
Copyright (c) G-Truc Creation. Permission is granted under the MIT License.

### Catch2 — Boost Software License 1.0 (test builds only)
Copyright (c) the Catch2 authors. Distributed under the Boost Software License,
Version 1.0. Catch2 is used only by the test suite and is **not** shipped in the
distributed application.

---

*This manifest realizes requirement C-LIC-5 and is surfaced in-app via
Help → Licenses (FR-APP-8).*

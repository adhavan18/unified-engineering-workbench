# dependencies and licences

## runtime (shipped in the zip)

| component | source | licence | notes |
|---|---|---|---|
| Launch.exe | this project | see ownership statement | statically linked |
| libstdc++, libgcc (static) | gcc 16.2.0 | gpl-3.0 **with the gcc runtime library exception** | the exception allows distributing statically linked binaries under any licence |
| winpthreads (static) | mingw-w64 | mit / zpl style permissive | provides std::thread on mingw |
| windows dlls (kernel32, user32, advapi32, shell32, ws2_32, ucrt) | microsoft windows | part of the os | not redistributed; present on every windows 10/11 install |

no third-party c++ libraries. no third-party javascript, css or fonts: the frontend is hand-written and uses the
browser's built-in webgl 1 and canvas 2d. nothing is fetched from the internet at runtime.

## build-time only (not shipped)

| tool | version | licence |
|---|---|---|
| gcc (winlibs mingw-w64 ucrt posix) | 16.2.0 r3 | gpl-3.0 |
| cmake | 4.4.4 | bsd-3-clause |
| ninja | 1.13.2 | apache-2.0 |
| microsoft edge (headless) | system | used only to render the report pdf and plot pngs |

exact versions are pinned in [toolchain.lock](toolchain.lock).

## borrowed numbers, not code

- the viridis colour map stop values (`src/frontend/js/viewer3d.js`, css legend) are sampled from
  matplotlib's viridis map by van der walt and smith, released cc0 (public domain).
- the fe formulation (element stiffness kA/h, consistent load qAh/2), the thomas algorithm and the central
  difference formula are standard textbook methods, implemented from scratch here.

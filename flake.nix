{
  description = "vkDaedalus: a Vulkan post-processing layer with an in-game overlay, on Wayland and X11";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    git-hooks = {
      url = "github:cachix/git-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    std = {
      url = "github:Daaboulex/nix-packaging-standard/release";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.git-hooks.follows = "git-hooks";
    };
    stb = {
      url = "github:nothings/stb";
      flake = false;
    };
    imgui = {
      url = "github:ocornut/imgui/docking";
      flake = false;
    };
    imgui-filebrowser = {
      url = "github:AirGuanZ/imgui-filebrowser";
      flake = false;
    };
    reshade = {
      url = "github:crosire/reshade";
      flake = false;
    };
    filedetectionrulesets = {
      url = "github:SteamDatabase/FileDetectionRuleSets";
      flake = false;
    };
    tomlplusplus = {
      url = "github:marzer/tomlplusplus";
      flake = false;
    };
    reshade-shaders = {
      url = "github:crosire/reshade-shaders/slim";
      flake = false;
    };
    barlow = {
      url = "github:jpt/barlow";
      flake = false;
    };
  };

  outputs =
    inputs@{ flake-parts, self, ... }:
    let
      inherit (inputs.nixpkgs) lib;

      wraps =
        lib.mapAttrs'
          (file: _: lib.nameValuePair (lib.removeSuffix ".wrap" file) (./subprojects + "/${file}"))
          (
            lib.filterAttrs (file: type: type == "regular" && lib.hasSuffix ".wrap" file) (
              builtins.readDir ./subprojects
            )
          );

      wrapField =
        name: field:
        let
          lines = lib.filter (lib.hasPrefix "${field} = ") (
            lib.splitString "\n" (builtins.readFile wraps.${name})
          );
        in
        if lines == [ ] then
          null
        else if builtins.length lines > 1 then
          throw "subprojects/${name}.wrap has more than one ${field} line"
        else
          lib.removePrefix "${field} = " (builtins.head lines);

      requiredWrapField =
        name: field:
        let
          value = wrapField name field;
        in
        if value == null then throw "subprojects/${name}.wrap has no ${field} line" else value;

      wrapSource =
        name:
        if builtins.hasAttr name inputs then
          inputs.${name}
        else
          throw "subprojects/${name}.wrap has no flake input named ${name}";

      placeSubprojects = lib.concatStrings (
        lib.mapAttrsToList (
          name: _:
          let
            directory = requiredWrapField name "directory";
            patchDirectory = wrapField name "patch_directory";
            diffFiles = lib.optionals (wrapField name "diff_files" != null) (
              map lib.trim (lib.splitString "," (wrapField name "diff_files"))
            );
          in
          ''
            cp -r ${wrapSource name} subprojects/${directory}
            chmod -R u+w subprojects/${directory}
          ''
          + lib.optionalString (patchDirectory != null) ''
            cp -r subprojects/packagefiles/${patchDirectory}/. subprojects/${directory}/
          ''
          + lib.concatMapStrings (diff: ''
            patch -p1 --forward --directory subprojects/${directory} < subprojects/packagefiles/${diff}
          '') diffFiles
        ) wraps
      );

      mkVkbasaltOverlay = pkgs': mkVkbasaltOverlayWith pkgs' pkgs'.stdenv;

      mkVkbasaltOverlayWith =
        pkgs': stdenv':
        stdenv'.mkDerivation {
          pname = "vkdaedalus";
          version = "0.1.0-unstable-${self.shortRev or "dirty"}";

          src = self;

          nativeBuildInputs = with pkgs'.buildPackages; [
            meson
            ninja
            pkg-config
            glslang
            wayland-scanner
          ];

          buildInputs = with pkgs'; [
            vulkan-headers
            vulkan-loader
            spirv-headers
            libx11
            libxi
            libxcb
            wayland
            wayland-protocols
            libxkbcommon
          ];

          mesonBuildType = "release";

          postPatch = placeSubprojects;

          mesonFlags = [
            "-Dappend_libdir_vkdaedalus=false"
            "--sysconfdir=/etc"
          ];

          doCheck = true;

          env = {
            VKDAEDALUS_TEST_COMMA_LOCALE = "de_DE.UTF-8";
            LOCALE_ARCHIVE = "${
              pkgs'.glibcLocales.override {
                allLocales = false;
                locales = [ "de_DE.UTF-8/UTF-8" ];
              }
            }/lib/locale/locale-archive";
          };

          postInstall = ''
            substituteInPlace "$out/share/vulkan/implicit_layer.d/vkdaedalus.json" \
              --replace-fail '"library_path": "libvkdaedalus.so"' \
                             '"library_path": "'"$out/lib/libvkdaedalus.so"'"'

          '';

          postFixup = ''
            sed -i '1s|.*|#!${pkgs'.busybox-sandbox-shell}/bin/sh|' "$out/bin/vkdaedalus-run"
          '';

          meta = with pkgs'.lib; {
            description = "Vulkan post-processing layer with real-time overlay UI (Wayland + X11)";
            homepage = "https://github.com/Daaboulex/vkDaedalus";
            license = licenses.zlib;
            platforms = pkgs'.lib.platforms.linux;
            mainProgram = "vkdaedalus-run";
          };
        };
    in
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      imports = [ inputs.std.flakeModules.base ];

      flake.overlays.default = final: _prev: {
        vkdaedalus = mkVkbasaltOverlay final;
      };

      perSystem =
        { pkgs, self', ... }:
        {
          pre-commit.settings.hooks.rumdl = {
            excludes = [ "^src/" ];
            settings.configuration.MD033.enabled = false;
          };

          pre-commit.settings.hooks.typos.settings.config.default.extend-words.lod = "lod";

          pre-commit.settings.hooks.typos.excludes = [
            "^src/"
            "^subprojects/packagefiles/.*\\.patch$"
            "^test/shader-corpus-baseline\\.txt$"
            "^SHADER-COMPATIBILITY\\.md$"
            "^scripts/shader-support-table\\.sh$"
          ];

          pre-commit.settings.hooks.shfmt.excludes = [
            "^scripts/"
            "^test/"
            "^format\\.sh$"
            "^vkdaedalus-run\\.sh$"
          ];
          pre-commit.settings.hooks.trim-trailing-whitespace.excludes = [
            "^src/"
            "^subprojects/packagefiles/.*\\.patch$"
            "^config/"
          ];
          pre-commit.settings.hooks.end-of-file-fixer.excludes = [
            "^src/"
            "^subprojects/packagefiles/.*\\.patch$"
          ];
          pre-commit.settings.hooks.mixed-line-endings.excludes = [
            "^src/"
            "^subprojects/packagefiles/.*\\.patch$"
          ];

          checks.compiles-against-wayland-1-20-headers =
            pkgs.runCommand "compiles-against-wayland-1-20-headers"
              {
                nativeBuildInputs = [
                  pkgs.stdenv.cc
                  pkgs.wayland-scanner
                ];
                wayland120 = pkgs.fetchurl {
                  url = "https://wayland.freedesktop.org/releases/wayland-1.20.0.tar.xz";
                  hash = "sha256-uKA0FUxwWXcuD9vSfb/Npscy3ynK5WqCJ09uxdfNhyU=";
                };
              }
              ''
                tar -xf "$wayland120" wayland-1.20.0/protocol/wayland.xml
                mkdir old
                cp ${pkgs.wayland.dev}/include/wayland-client.h \
                   ${pkgs.wayland.dev}/include/wayland-client-core.h \
                   ${pkgs.wayland.dev}/include/wayland-util.h \
                   ${pkgs.wayland.dev}/include/wayland-version.h old/
                wayland-scanner client-header wayland-1.20.0/protocol/wayland.xml old/wayland-client-protocol.h

                for member in WL_POINTER_WARP_SINCE_VERSION WL_POINTER_AXIS_VALUE120_SINCE_VERSION WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION; do
                  grep -q "$member" old/wayland-client-protocol.h \
                    && { echo "the 1.20 header defines $member -- this check no longer tests an old header"; exit 1; }
                done

                mkdir gen
                wayland-scanner client-header \
                  ${pkgs.wayland-protocols}/share/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml \
                  gen/relative-pointer-unstable-v1-client-protocol.h
                wayland-scanner client-header \
                  ${pkgs.wayland-protocols}/share/wayland-protocols/unstable/xdg-output/xdg-output-unstable-v1.xml \
                  gen/xdg-output-unstable-v1-client-protocol.h

                printf '#include <wayland-client.h>\n#ifdef WL_POINTER_WARP_SINCE_VERSION\n#error newer protocol header\n#endif\n' > probe.cpp
                $CXX -fsyntax-only -std=c++20 -Iold probe.cpp \
                  || { echo "wayland-client.h resolved a protocol header newer than 1.20, so this check would not test the old one"; exit 1; }

                cp -r ${./src} src
                $CXX -fsyntax-only -std=c++20 -Isrc -Iold -Igen -I${pkgs.lib.getDev pkgs.libxcb}/include \
                  src/mouse_input_wayland.cpp src/input_hook.cpp src/wayland_input_common.cpp \
                  || { echo "a Wayland protocol member newer than wayland 1.20, the oldest supported floor (Ubuntu 22.04), is used without its SINCE_VERSION guard"; exit 1; }
                touch $out
              '';

          checks.docs-read-plainly = pkgs.runCommand "docs-read-plainly" { } ''
            cp ${./test/docs/bad.md} bad.md
            chmod +w bad.md
            printf 'The overlay is fast \342\200\224 and it runs everywhere.\n' >>bad.md
            printf 'Read more [here](https://example.org).\n' >>bad.md
            status=0
            found=$(bash ${./scripts/docs-check.sh} bad.md) || status=$?
            [ "$status" -eq 1 ] || { echo "the docs check passed its failing fixture with $status, so it checks nothing"; exit 1; }
            for rule in "plain ASCII" "double dash" "an arrow" "an ellipsis" "says here" "padded word" "title case"; do
              grep -qF "$rule" <<<"$found" || { echo "the docs check missed the fixture's line for: $rule"; exit 1; }
            done
            bash ${./scripts/docs-check.sh} ${./README.md} ${./docs}/*.md
            touch $out
          '';

          checks.builds-with-clang = mkVkbasaltOverlayWith pkgs pkgs.clangStdenv;

          checks.compiler-fails-cleanly =
            pkgs.runCommand "compiler-fails-cleanly"
              {
                nativeBuildInputs = [ self'.checks.unit-tests-under-sanitizers ];
                ASAN_OPTIONS = "detect_leaks=1:abort_on_error=1";
                UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1";
              }
              ''
                rc=0
                vkdaedalus-test-shaders ${./test/must-fail} > report.txt 2>&1 || rc=$?
                cat report.txt
                if grep -qE 'runtime error|AddressSanitizer|LeakSanitizer' report.txt; then
                  echo "the compiler hit undefined behaviour on a shader it must reject cleanly"
                  exit 1
                fi
                [ "$rc" -ne 0 ] || { echo "shaders that must fail were all reported as compiling"; exit 1; }
                for f in ${./test/must-fail}/*.fx; do
                  name=$(basename "$f" .fx)
                  grep -qE "^FAIL +$name\b" report.txt || { echo "$name was not reported as a failure"; exit 1; }
                done
                touch $out
              '';

          checks.wraps-match-the-lock =
            let
              mismatches = lib.filter (m: m != null) (
                lib.mapAttrsToList (
                  name: _:
                  let
                    pinned = builtins.match "https://github.com/[^/]+/[^/]+/archive/([0-9a-f]{40})\\.tar\\.gz" (
                      requiredWrapField name "source_url"
                    );
                  in
                  if pinned == null then
                    "${name}: source_url is not a GitHub archive at a commit"
                  else if builtins.head pinned != (wrapSource name).rev then
                    "${name}: the wrap pins ${builtins.head pinned}, flake.lock pins ${(wrapSource name).rev}"
                  else
                    null
                ) wraps
              );
            in
            pkgs.runCommand "wraps-match-the-lock"
              {
                tarballs = lib.mapAttrsToList (
                  name: _:
                  pkgs.fetchurl {
                    url = requiredWrapField name "source_url";
                    sha256 = requiredWrapField name "source_hash";
                  }
                ) wraps;
              }
              (
                if mismatches == [ ] then
                  ''touch "$out"''
                else
                  lib.concatMapStrings (m: "echo ${lib.escapeShellArg m}\n") mismatches
                  + ''
                    echo "regenerate the wraps from the lock: nix run .#after-lock-update"
                    exit 1
                  ''
              );

          apps.after-lock-update = {
            type = "app";
            program = lib.getExe (
              pkgs.writeShellApplication {
                name = "after-lock-update";
                runtimeInputs = [
                  pkgs.git
                  pkgs.jq
                  pkgs.curl
                  pkgs.coreutils
                  pkgs.gnused
                  pkgs.gnutar
                  pkgs.gzip
                  pkgs.findutils
                ];
                text = builtins.readFile ./scripts/sync-wraps.sh;
              }
            );
            meta.description = "Rewrite every subprojects wrap's pin from flake.lock";
          };

          checks.unit-tests-under-sanitizers = self'.packages.vkdaedalus.overrideAttrs (old: {
            pname = "vkdaedalus-sanitized";
            mesonBuildType = "debug";
            mesonFlags = old.mesonFlags ++ [
              "-Db_sanitize=address,undefined"
              "-Db_lundef=false"
            ];
            mesonCheckFlags = [ "--print-errorlogs" ];
            env = (old.env or { }) // {
              ASAN_OPTIONS = "detect_leaks=1:abort_on_error=1:strict_string_checks=1";
              UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1";
            };
          });

          apps.sync-corpus = {
            type = "app";
            program = lib.getExe (
              pkgs.writeShellApplication {
                name = "sync-corpus";
                runtimeInputs = [
                  pkgs.git
                  pkgs.jq
                  pkgs.coreutils
                  pkgs.gnused
                  pkgs.gnugrep
                ];
                text = builtins.readFile ./scripts/sync-corpus.sh;
              }
            );
            meta.description = "Pin every pack in ReShade's official shader index to its current revision and hash";
          };

          apps.study = {
            type = "app";
            program = lib.getExe (
              pkgs.writeShellApplication {
                name = "study";
                runtimeInputs = [
                  pkgs.gawk
                  pkgs.coreutils
                  pkgs.gnugrep
                  pkgs.diffutils
                ];
                text = builtins.readFile ./scripts/study.sh;
              }
            );
            meta.description = "Collect a live run's measurements into docs/performance.tsv and write its tables into docs/performance.md";
          };

          apps.tour-gif = {
            type = "app";
            program = lib.getExe (
              pkgs.writeShellApplication {
                name = "tour-gif";
                runtimeInputs = [ pkgs.imagemagick ];
                text = builtins.readFile ./scripts/tour-gif.sh;
              }
            );
            meta.description = "Make docs/screenshots/tour.gif from a live run's gallery";
          };

          checks.performance-matches-its-data =
            pkgs.runCommand "performance-matches-its-data"
              {
                nativeBuildInputs = [
                  pkgs.gawk
                  pkgs.diffutils
                ];
              }
              ''
                bash ${./scripts/study.sh} check ${./docs/performance.tsv} ${./docs/performance.md}
                awk -F'\t' 'NR == 1 { $4 = $4 " drifted" } { print }' OFS='\t' ${./docs/performance.tsv} > drifted.tsv
                if bash ${./scripts/study.sh} check drifted.tsv ${./docs/performance.md} > drift.log; then
                  echo "the study check passed a data file it no longer matches, so it checks nothing"
                  exit 1
                fi
                touch $out
              '';

          checks.shader-corpus-matches-the-baseline =
            pkgs.runCommand "shader-corpus-matches-the-baseline" { }
              ''
                if ! diff -u ${./test/shader-corpus-baseline.txt} ${self'.packages.shader-corpus-verdicts} > verdicts.diff; then
                  cat verdicts.diff
                  echo "the pinned shader corpus no longer compiles as test/shader-corpus-baseline.txt records; record a new baseline only once every changed verdict is explained"
                  exit 1
                fi
                touch $out
              '';

          apps.live = {
            type = "app";
            program = pkgs.lib.getExe self'.packages.vkdaedalus-live;
            meta.description = "Run every test lane on every Vulkan GPU of this machine with validation under the layer";
          };

          checks.shader-support-table-is-current =
            pkgs.runCommand "shader-support-table-is-current" { nativeBuildInputs = [ pkgs.bash ]; }
              ''
                mkdir -p work/scripts work/test
                cp ${./scripts/shader-support-table.sh} work/scripts/shader-support-table.sh
                cp ${./test/shader-corpus-baseline.txt} work/test/shader-corpus-baseline.txt
                cp ${./SHADER-COMPATIBILITY.md} work/SHADER-COMPATIBILITY.md
                chmod +w work/SHADER-COMPATIBILITY.md work/scripts/shader-support-table.sh
                bash work/scripts/shader-support-table.sh --check
                help=$(bash work/scripts/shader-support-table.sh --help)
                grep -qF -- '--check' <<< "$help" \
                  || { echo "shader-support-table.sh --help no longer says how to check the table"; exit 1; }
                status=0
                refused=$(bash work/scripts/shader-support-table.sh --no-such-option 2>&1) || status=$?
                [ "$status" -eq 2 ] && grep -qF 'usage:' <<< "$refused" \
                  || { echo "shader-support-table.sh ran with an argument it does not know instead of refusing it with its usage"; exit 1; }
                touch $out
              '';

          checks.allocations-are-tracked = pkgs.runCommand "allocations-are-tracked" { src = ./src; } ''
            if ! callers=$(grep -rl 'vkd.AllocateMemory' "$src"); then
              echo "nothing calls vkd.AllocateMemory any more, so the routing this check guards has moved"
              exit 1
            fi
            [ "$callers" = "$src/memory.cpp" ] \
              || { echo "vkd.AllocateMemory is called outside memory.cpp ($callers), so that memory escapes the VRAM count and the soft limit"; exit 1; }
            touch $out
          '';

          checks.handle-lookups-never-insert =
            pkgs.runCommand "handle-lookups-never-insert" { src = ./src/basalt.cpp; }
              ''
                maps=$(sed -n 's/^    std::unordered_map<.*> *\([A-Za-z]*Map\);$/\1/p' "$src")
                [ -n "$maps" ] \
                  || { echo "basalt.cpp declares no handle map at namespace scope any more, so this check guards nothing"; exit 1; }
                for map in $maps; do
                  if grep -nE "(^|[^A-Za-z0-9_])$map(\[|\.at\()" "$src"; then
                    echo "$map is indexed with operator[] or at(): operator[] inserts an empty entry for a handle the layer never created and the caller dereferences it, at() throws through a Vulkan call; look it up through findInstance, findDevice or findSwapchain"
                    exit 1
                  fi
                done
                touch $out
              '';

          checks.next-layer-queries-name-their-instance =
            pkgs.runCommand "next-layer-queries-name-their-instance"
              {
                src = ./src;
                mock = ./test/mock-framegen;
                fault = ./test/fault-inject;
              }
              ''
                grep -rqF 'gpa(VK_NULL_HANDLE, "vkCreateInstance")' "$src" \
                  || { echo "basalt.cpp no longer asks the next layer for vkCreateInstance as this check expects, so it guards nothing"; exit 1; }
                if grep -rnoE 'VK_NULL_HANDLE, "vk[A-Za-z0-9]+"' "$src" "$mock" "$fault" | grep -vE '"vk(CreateInstance|GetInstanceProcAddr|EnumerateInstance[A-Za-z]*)"$'; then
                  echo "a layer asks the next one for a command that is not global with VK_NULL_HANDLE; the Vulkan spec leaves that undefined and lsfg-vk answers it with NULL, so a game crashed at vkCreateDevice with lsfg-vk active (2026-10-15): pass the instance"
                  exit 1
                fi
                touch $out
              '';

          checks.overlay-draws-belong-to-their-swapchain =
            pkgs.runCommand "overlay-draws-belong-to-their-swapchain"
              {
                overlay = ./src/overlay/imgui_overlay.hpp;
                swapchain = ./src/logical_swapchain.hpp;
                draws = ./src/overlay/swapchain_overlay.hpp;
                renderer = ./src/overlay/overlay_renderer.hpp;
                drawing = ./src/overlay/imgui_overlay.cpp;
                layer = ./src/basalt.cpp;
              }
              ''
                status=0
                shared=$(grep -nE '^ +(std::vector<Vk(Buffer|Fence|Framebuffer|CommandBuffer)>|Vk(RenderPass|CommandPool|Buffer|Framebuffer) )' "$overlay") || status=$?
                [ "$status" -le 1 ] || { echo "the scan of the overlay's members failed with $status"; exit 1; }
                [ -z "$shared" ] \
                  || { echo "the device-wide overlay holds draw state again, which every swapchain of the device shares: one vertex ring, one fence per image index and one render pass for whichever format came last, so two swapchains overwrite each other's draws: $shared"; exit 1; }
                status=0
                kept=$(grep -nE '^ +(std::vector<)?(OverlayBuffer|OverlayDraw|VkFence|VkCommandBuffer|VkFramebuffer)[ >]' "$renderer") || status=$?
                [ "$status" -le 1 ] || { echo "the scan of the renderer's members failed with $status"; exit 1; }
                [ -z "$kept" ] \
                  || { echo "the device-wide renderer holds a draw's buffers, fence, command buffer or framebuffer, which every swapchain of the device would then share: $kept"; exit 1; }

                frame=$(sed -n '/ImGuiOverlay::Frame ImGuiOverlay::drawFrame/,/^    }$/p' "$drawing")
                gate=$(awk '/WaitForFences\(pLogicalDevice->device, 1, &draw\.fence, VK_TRUE, 0\)/ && !g { g = NR } /renderer->record\(/ && !r { r = NR } END { print (g && r && g < r) ? "ok" : "wrong" }' <<< "$frame")
                [ "$gate" = ok ] \
                  || { echo "drawFrame records into a draw without first finding its fence signalled. The bounded wait for it runs off the lock and may time out, so this check is all that keeps a pending command buffer from being begun again and its vertex buffer from being overwritten under the GPU"; exit 1; }
                grep -Eq 'SwapchainOverlay +overlay;' "$swapchain" \
                  || { echo "a swapchain no longer owns its overlay draws"; exit 1; }
                grep -Eq 'OverlayBuffer +vertices;' "$draws" && grep -Eq 'OverlayBuffer +indices;' "$draws" \
                  || { echo "an overlay draw no longer owns its vertex and index buffers, so it can reuse storage another swapchain's draw is still reading"; exit 1; }

                wait=$(sed -n '/VkResult waitForPreviousSubmissions/,/^    }/p' "$layer")
                order=$(awk '/l\.unlock\(\);/ && !u { u = NR } /waitForFences\(device, 1, &overlayDraw, VK_TRUE, overlayPatienceNs\)/ && !w { w = NR } /l\.lock\(\);/ && u && !k { k = NR } END { print (u && w && k && u < w && w < k) ? "ok" : "wrong" }' <<< "$wait")
                [ "$order" = ok ] \
                  || { echo "the present waits for the overlay's previous draw of this image while it holds globalLock, so every other thread's call into the layer stalls behind that GPU wait"; exit 1; }
                touch $out
              '';

          checks.overlay-follows-its-swapchain =
            pkgs.runCommand "overlay-follows-its-swapchain"
              {
                overlay = ./src/overlay/imgui_overlay.cpp;
                layer = ./src/basalt.cpp;
                swapchain = ./src/logical_swapchain.cpp;
              }
              ''
                setup=$(sed -n '/vkBasalt_GetSwapchainImagesKHR/,/^    }$/p' "$layer")
                grep -Fq 'prepareSwapchain(*pLogicalSwapchain)' <<< "$setup" \
                  || { echo "swapchain setup no longer makes the swapchain's overlay draws, so the overlay has no render pass, framebuffers or fences for it"; exit 1; }

                prepare=$(sed -n '/void ImGuiOverlay::prepareSwapchain/,/^    }$/p' "$overlay")
                grep -Fq 'switchOff(' <<< "$prepare" \
                  || { echo "a swapchain whose overlay draws could not all be made leaves the overlay on, to record against half-made state. It must switch itself off"; exit 1; }

                destroy=$(sed -n '/void LogicalSwapchain::destroy()/,/^    }$/p' "$swapchain")
                order=$(awk '/overlay\.fences\(\)/ && !w { w = NR } /overlay\.destroy\(/ && !d { d = NR } END { print (w && d && w < d) ? "ok" : "wrong" }' <<< "$destroy")
                [ "$order" = ok ] \
                  || { echo "a swapchain frees its overlay draws without waiting for them, so a submitted overlay frame can still be reading the framebuffers and buffers being freed"; exit 1; }
                grep -Fq 'detach(overlay)' <<< "$destroy" \
                  || { echo "a destroyed swapchain stays attached to the overlay, which then waits on fences that no longer exist before a texture update"; exit 1; }
                touch $out
              '';

          checks.every-dl-user-declares-the-library =
            pkgs.runCommand "every-dl-user-declares-the-library" { nativeBuildInputs = [ pkgs.bash ]; }
              ''
                cp -r ${./src} src
                bash ${./scripts/every-dl-user-declares-the-library.sh} src
                touch $out
              '';

          checks.no-dormant-modules =
            pkgs.runCommand "no-dormant-modules" { nativeBuildInputs = [ pkgs.bash ]; }
              ''
                cp -r ${./src} src
                cp -r ${./tools} tools
                bash ${./scripts/no-dormant-modules.sh} src tools
                touch $out
              '';

          checks.optional-gpu-probe-stays-optional =
            pkgs.runCommand "optional-gpu-probe-stays-optional" { }
              ''
                src=${./src/gpu_probe.cpp}
                body=$(sed -n '/dlopen("libnvidia-ml/,/missing required symbols/p' "$src")
                grep -q 'if (!nvml.lib)' <<< "$body" \
                  || { echo "NVML is dlopened without checking the result -- every non-NVIDIA system would fault"; exit 1; }
                grep -q 'return false' <<< "$body" \
                  || { echo "a failed NVML load must leave diagnostics disabled, not continue"; exit 1; }
                touch $out
              '';

          checks.overlay-cursor-survives-a-pointer-lock =
            pkgs.runCommand "overlay-cursor-survives-a-pointer-lock" { }
              ''
                mouse=${./src/mouse_input_wayland.cpp}
                grep -q 'zwp_relative_pointer_manager_v1_get_relative_pointer' "$mouse" \
                  || { echo "the overlay pointer binds no relative pointer, so its cursor freezes while a game holds the pointer locked"; exit 1; }
                grep -q 'state.relativeMotion(' "$mouse" \
                  || { echo "relative motion is bound but never reaches the pointer state"; exit 1; }
                grep -q 'setWaylandPointerBounds(' ${./src/overlay/imgui_overlay.cpp} \
                  || { echo "the overlay never hands the pointer its bounds, so a relative-driven cursor can run past the screen and stick there"; exit 1; }
                touch $out
              '';

          checks.layer-never-constrains-the-game-surface =
            pkgs.runCommand "layer-never-constrains-the-game-surface" { }
              ''
                if grep -rq 'zwp_pointer_constraints_v1\|zwp_locked_pointer_v1\|zwp_confined_pointer_v1' ${./src}; then
                  echo "the layer requests a pointer constraint on the game's surface. The protocol allows one per surface and seat across every wl_pointer, and KWin answers a second with the fatal already_constrained error, so a game that locks or confines its own cursor dies the moment the overlay opens"
                  exit 1
                fi
                touch $out
              '';

          checks.effect-wait-covers-the-whole-submission =
            pkgs.runCommand "effect-wait-covers-the-whole-submission" { }
              ''
                present=$(sed -n '/vkBasalt_QueuePresentKHR/,/^    }/p' ${./src/basalt.cpp})
                assign=$(awk '/waitStages\.assign/ { print }' <<< "$present")
                grep -q 'VK_PIPELINE_STAGE_ALL_COMMANDS_BIT' <<< "$assign" \
                  || { echo "the effect submission waits on the application's present semaphores at a stage narrower than ALL_COMMANDS. The recorded buffer can open with image barriers and a transfer copy, and a ReShade pass can be compute-only, so that work would be free to run before the presented image is available"; exit 1; }
                touch $out
              '';

          checks.uniform-bounds-check-can-actually-fail =
            pkgs.runCommand "uniform-bounds-check-can-actually-fail"
              {
                uniforms = ./src/reshade_uniforms.cpp;
                effect = ./src/effects/effect_reshade.cpp;
              }
              ''
                if grep -qE 'writeReshadeUniform[A-Za-z]*\([^,]*,[[:space:]]*uniformEnd' "$uniforms"; then
                  echo "a uniform write is bounded by uniformEnd (offset + size), which is derived from the very variable being bounded: 'offset > offset + size' and 'size > size' are both always false, so the check cannot fail. Pass the real buffer size"
                  exit 1
                fi

                grep -Fq 'uniform->update(stagingBuffersMapped[imageIndex], bufferSize);' "$effect" \
                  || { echo "updateEffect does not pass the real uniform buffer size, so the bounds check has nothing true to test against"; exit 1; }

                touch $out
              '';

          checks.uniform-writes-cannot-race-the-gpu =
            pkgs.runCommand "uniform-writes-cannot-race-the-gpu"
              {
                nativeBuildInputs = [ pkgs.python3 ];
                present = ./src/basalt.cpp;
                effect = ./src/effects/effect_reshade.cpp;
                header = ./src/effects/effect_reshade.hpp;
              }
              ''
                python3 - <<'PY'
                from pathlib import Path

                present = Path('${./src/basalt.cpp}').read_text()
                start = present.index('vkBasalt_QueuePresentKHR(VkQueue queue')
                wait = present.index('waitForPreviousSubmissions(l, pLogicalDevice', start)
                update = present.index('effect->updateEffect(index);', start)
                submit = present.index('vkd.QueueSubmit(', start)
                if not wait < update < submit:
                    raise SystemExit(
                        'updateEffect writes mapped uniform memory the pending submission still reads. '
                        'It must run AFTER the fence wait and BEFORE the submit')
                PY

                grep -Fq 'std::vector<VkBuffer>       stagingBuffers;' "$header"                   || { echo "the uniform buffer is shared across swapchain images again. One buffer is read by every image in flight, so image i's fence cannot prove a CPU write to it is safe"; exit 1; }

                grep -Fq 'bindSetIfAllocated(VK_PIPELINE_BIND_POINT_GRAPHICS, 0, bufferDescriptorSets)' "$effect"                   || { echo "the graphics pass does not bind THIS image's uniform set"; exit 1; }

                grep -Fq 'bindSetIfAllocated(VK_PIPELINE_BIND_POINT_COMPUTE, 0, bufferDescriptorSets)' "$effect"                   || { echo "the compute pass does not bind THIS image's uniform set, so compute reads another image's uniforms"; exit 1; }

                touch $out
              '';

          checks.absent-effect-fence-still-proves-completion =
            pkgs.runCommand "absent-effect-fence-still-proves-completion" { src = ./src/basalt.cpp; }
              ''
                body=$(sed -n '/VkResult waitForPreviousSubmissions/,/^    }/p' "$src")

                grep -Fq 'l.unlock();' <<< "$body" \
                  || { echo "the present waits for the layer's previous frame while holding globalLock, so every other thread's Vulkan call into the layer stalls behind a GPU wait"; exit 1; }

                grep -Fq 'QueueWaitIdle' <<< "$body"                   || { echo "with no effect fence there is no proof the previous submission of this command buffer finished, and the uniform write below races it. A queue wait is the coarse proof"; exit 1; }

                grep -Fq 'return waitResult;' <<< "$body"                   || { echo "a fence wait failure does not reach the caller, so the layer continues with an unsynchronised command buffer"; exit 1; }

                relock_at=$(awk '/l\.lock\(\);/ { print NR; exit }' <<< "$body")
                drain_at=$(awk '/QueueWaitIdle/ { print NR; exit }' <<< "$body")
                if [ -z "$relock_at" ] || [ -z "$drain_at" ] || [ "$drain_at" -le "$relock_at" ]; then
                  echo "the fallback drain runs off globalLock. It can drain the layer's own queue, which another thread may be submitting to under the lock, and a queue is externally synchronised"
                  exit 1
                fi

                present=$(sed -n '/VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR/,/^    }/p' "$src")
                grep -Fq 'return resetResult;' <<< "$present" \
                  || { echo "a fence reset failure does not reach the caller"; exit 1; }

                if grep -q 'VK_ERROR_DEVICE_LOST' <<< "$body"; then
                  echo "an absent effect fence is reported as device loss. Fence creation is ALLOWED to fail here (swapchain setup and submit-failure recovery both leave VK_NULL_HANDLE), the device is not lost, and the state is permanent -- every later frame would return it and tear the application down instead of degrading"
                  exit 1
                fi

                touch $out
              '';

          checks.every-param-type-can-be-reset =
            pkgs.runCommand "every-param-type-can-be-reset" { fields = ./src/overlay/params/fields; }
              ''
                grep -q 'defaultMark(' ${./src/overlay/params/field_editor.cpp} \
                  || { echo "the default mark is not in the shared field-editor dispatcher, so it reaches only the types that copy it. That is how the bool editor once ended up as the one param type with no reset"; exit 1; }
                copies=$(grep -lE 'defaultMark\(|ui::beginContextMenu|ImGui::BeginPopupContextItem' "$fields"/*.cpp 2>/dev/null || true)
                [ -z "$copies" ] \
                  || { echo "a field editor carries its own reset: $copies. One writer, in the dispatcher, or a type gets missed again"; exit 1; }
                touch $out
              '';

          checks.render-pass-clear-values-outlive-recording =
            pkgs.runCommand "render-pass-clear-values-outlive-recording" { }
              ''
                block=$(sed -n '/VkRenderPassBeginInfo renderPassBeginInfo/,/renderPassBeginInfos.push_back/p' ${./src/effects/effect_reshade.cpp})
                grep -q 'renderPassClearValues' <<< "$block" \
                  || { echo "the render pass clear values are not held in a member that outlives the recorded buffer"; exit 1; }
                grep -q 'VkClearValue clearValues\[' <<< "$block" \
                  && { echo "pClearValues points at a constructor-stack array. The command buffer is recorded once and submitted many times, so the driver reads it long after that scope dies, and the first pass of every ReShade effect clears stencil from it"; exit 1; }
                touch $out
              '';

          checks.effect-timing-never-blocks = pkgs.runCommand "effect-timing-never-blocks" { } ''
            ! grep -Fq 'VK_QUERY_RESULT_WAIT_BIT' ${./src/logical_swapchain.cpp} \
              || { echo "the optional timing read can block the presentation path"; exit 1; }
            touch $out
          '';

          checks.effect-timing-is-collected-before-fence-reset =
            pkgs.runCommand "effect-timing-is-collected-before-fence-reset"
              { nativeBuildInputs = [ pkgs.python3 ]; }
              ''
                python3 - <<'PY'
                from pathlib import Path
                source = Path('${./src/basalt.cpp}').read_text()
                wait = source.index('waitForPreviousSubmissions(l, pLogicalDevice, pPresentInfo)')
                collect = source.index('pLogicalSwapchain->collectEffectTimings(index);', wait)
                reset = source.index('pLogicalDevice->vkd.ResetFences(', collect)
                if not wait < collect < reset:
                    raise SystemExit('timestamp results are not collected after fence completion and before fence reset')
                PY
                touch $out
              '';

          checks.depth-barriers-cover-compute-readers =
            pkgs.runCommand "depth-barriers-cover-compute-readers" { }
              ''
                barriers=$(sed -n '/void writeCommandBuffers/,/^    }/p' ${./src/command_buffer.cpp})
                grep -q 'VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT' <<< "$barriers" \
                  || { echo "the depth barriers name no compute shader stage. The depth image view is bound at the compute bind point for ReShade compute passes, so those reads would sit outside the barrier"; exit 1; }
                covered=$(grep -o 'VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT' <<< "$barriers" | wc -l)
                [ "$covered" = 2 ] \
                  || { echo "the depth acquire and restore barriers must each name the compute shader stage, found $covered of 2. Naming it on only one leaves the other barrier's transition unordered against ReShade compute passes reading the depth image"; exit 1; }
                touch $out
              '';

          checks.mouse-frame-is-marked-in-present = pkgs.runCommand "mouse-frame-is-marked-in-present" { } ''
            present=$(sed -n '/vkBasalt_QueuePresentKHR/,/^    }/p' ${./src/basalt.cpp})
            grep -q 'beginMouseInputFrame(' <<< "$present" \
              || { echo "the present path does not mark the mouse frame -- every reader would share one frozen snapshot"; exit 1; }
            touch $out
          '';

          checks.textures-start-black = pkgs.runCommand "textures-start-black" { } ''
            effect=${./src/effects/effect_reshade.cpp}
            [ "$(grep -c 'clearAndReadyImages(pLogicalDevice, images' "$effect")" = "2" ] \
              || { echo "ReShade effects expect textures to start zeroed, and a merely transitioned image samples leftover memory: both texture-creation branches must clear their images before anything can sample them"; exit 1; }
            impl=$(sed -n '/void clearAndReadyImages/,/^    }/p' ${./src/image.cpp})
            grep -q 'CmdClearColorImage' <<< "$impl" \
              || { echo "clearAndReadyImages no longer clears -- it only transitions, so images sample leftover memory"; exit 1; }
            grep -q 'TRANSFER_DST_OPTIMAL' <<< "$impl" \
              || { echo "the clear needs the image in TRANSFER_DST first"; exit 1; }
            grep -rq 'changeImageLayout' ${./src}/*.cpp ${./src}/*.hpp ${./src}/effects/*.cpp \
              && { echo "a bare layout-transition helper is back -- every creation path must clear"; exit 1; }
            touch $out
          '';

          checks.shared-texture-pool-is-one-per-build =
            pkgs.runCommand "shared-texture-pool-is-one-per-build" { }
              ''
                build=$(sed -n '/^    std::vector<std::string> createEffectsForSwapchain(/,/^    }$/p' ${./src/basalt.cpp})
                [ "$(grep -c 'make_shared<SharedReshadeTexturePool>' ${./src/basalt.cpp})" = "2" ] \
                  && [ "$(grep -c 'make_shared<SharedReshadeTexturePool>' <<< "$build")" = "2" ] \
                  && grep -q 'segmentPools.push_back(std::make_shared<SharedReshadeTexturePool>(device));' <<< "$build" \
                  || { echo "the pool is no longer one per effect-collection build and GPU, the game's and the helper's; separate swapchains can collide on a name"; exit 1; }
                touch $out
              '';

          checks.render-pass-hands-its-writes-to-readers =
            pkgs.runCommand "render-pass-hands-its-writes-to-readers" { }
              ''
                pass=$(sed -n '/VkSubpassDependency subpassDependencies/,/renderPassCreateInfo.dependencyCount/p' ${./src/effects/effect_reshade.cpp})
                grep -q 'dstSubpass      = VK_SUBPASS_EXTERNAL' <<< "$pass" \
                  || { echo "the pass declares no outgoing dependency, so what it renders into a texture is never made visible to the sampler read that consumes it. Synchronization validation does not report this one, so nothing else would"; exit 1; }
                grep -q 'dstAccessMask   = VK_ACCESS_SHADER_READ_BIT' <<< "$pass" \
                  || { echo "the outgoing dependency does not make the colour writes visible to a shader read"; exit 1; }
                grep -q 'renderPassCreateInfo.dependencyCount = 2;' <<< "$pass" \
                  || { echo "the render pass no longer carries both the incoming and the outgoing dependency"; exit 1; }
                touch $out
              '';

          checks.language-features-compile =
            pkgs.runCommand "language-features-compile" { nativeBuildInputs = [ self'.packages.vkdaedalus ]; }
              ''
                cp -r ${./test/language} shaders
                status=0
                vkdaedalus-test-shaders shaders > report.txt 2>&1 || status=$?

                expected=$(ls shaders/*.fx | wc -l)
                actual=$(grep -c '^PASS  ' report.txt || true)
                if [ "$status" -ne 0 ] || [ "$expected" != "$actual" ]; then
                  cat report.txt
                  echo "expected all $expected language feature shaders to compile and the tester to exit 0: $actual did, it exited $status"
                  exit 1
                fi
                touch $out
              '';

          checks.boolean-varying-crosses-as-an-integer =
            pkgs.runCommand "boolean-varying-crosses-as-an-integer"
              {
                nativeBuildInputs = [
                  self'.packages.vkdaedalus
                  pkgs.spirv-tools
                ];
              }
              ''
                mkdir shaders spv
                cp ${./test/language/bool_varying.fx} shaders/bool_varying.fx
                vkdaedalus-test-shaders --dump-spirv spv shaders

                emitted=0
                for m in spv/bool_varying__*.spv; do
                  [ -e "$m" ] || continue
                  emitted=$((emitted + 1))
                  spirv-val --target-env vulkan1.1 "$m" || { echo "$m is invalid SPIR-V -- a driver may reject the pipeline"; exit 1; }
                  spirv-dis "$m" >> dis.txt
                done
                [ "$emitted" -eq 2 ] || { echo "expected a vertex and a pixel module, got $emitted"; exit 1; }

                if grep -qE 'OpVariable %_ptr_(Input|Output)_[A-Za-z0-9_]*bool' dis.txt; then
                  echo "a boolean Input or Output variable was declared, which SPIR-V forbids"
                  exit 1
                fi
                grep -q 'OpINotEqual' dis.txt || { echo "a boolean varying is not converted back after being loaded"; exit 1; }

                awk '
                  $3 == "OpConstant" && $4 == "%uint" { if ($5 == "1") one[$1] = 1; if ($5 == "0") zero[$1] = 1 }
                  $3 == "OpConstantComposite" && $4 ~ /^%v[0-9]uint$/ {
                    all1 = 1; all0 = 1
                    for (i = 5; i <= NF; i++) { if (!($i in one)) all1 = 0; if (!($i in zero)) all0 = 0 }
                    if (all1) one[$1] = 1
                    if (all0) zero[$1] = 1
                  }
                  $3 == "OpSelect" && $4 ~ /^%(v[0-9])?uint$/ {
                    if (($6 in one) && ($7 in zero)) good++
                    else if (($6 in zero) && ($7 in one)) inverted++
                  }
                  END {
                    if (inverted > 0) { print "a stored boolean selects zero for true -- every boolean arrives inverted"; exit 1 }
                    if (good == 0) { print "no boolean varying is converted to an integer before being stored"; exit 1 }
                  }' dis.txt
                touch $out
              '';

          checks.bundled-headers-compile-a-bare-pack =
            let
              corpus = builtins.fromJSON (builtins.readFile ./test/shader-corpus.lock.json);
              pack = lib.findFirst (
                pack: pack.owner == "CeeJayDK" && pack.repo == "SweetFX"
              ) (throw "test/shader-corpus.lock.json pins no CeeJayDK/SweetFX") corpus.packs;
              sweetfx = pkgs.fetchFromGitHub {
                inherit (pack)
                  owner
                  repo
                  rev
                  hash
                  ;
              };
            in
            pkgs.runCommand "bundled-headers-compile-a-bare-pack"
              { nativeBuildInputs = [ self'.packages.vkdaedalus ]; }
              ''
                cp -r ${sweetfx}/Shaders/SweetFX bare
                chmod -R u+w bare
                if [ -e bare/ReShade.fxh ] || [ -e bare/ReShadeUI.fxh ]; then
                  echo "SweetFX now ships ReShade.fxh or ReShadeUI.fxh in its own folder, so this check no longer tests the bundled ones"
                  exit 1
                fi
                status=0
                HOME=$PWD/home XDG_CACHE_HOME=$PWD/cache vkdaedalus-test-shaders bare > report.txt 2>&1 || status=$?
                missing=""
                for fx in bare/*.fx; do
                  name=$(basename "$fx" .fx)
                  if grep -qx "PASS $name" ${./test/shader-corpus-baseline.txt} && ! grep -qE "^PASS  $name( |\$)" report.txt; then
                    missing="$missing $name"
                  fi
                done
                if [ -n "$missing" ]; then
                  cat report.txt
                  echo "with only the layer's own ReShade.fxh and ReShadeUI.fxh, these SweetFX effects did not compile, though they do beside their pack's headers:$missing"
                  exit 1
                fi
                echo "test-shaders exit $status"
                touch $out
              '';

          checks.shallower-header-wins =
            pkgs.runCommand "shallower-header-wins" { nativeBuildInputs = [ self'.packages.vkdaedalus ]; }
              ''
                mkdir -p deep/Shaders/Nested shallow/Shaders user/Shaders
                printf '#define SHARED_VALUE 1\n' > deep/Shaders/Nested/shared_header.fxh
                printf '#define SHARED_VALUE 2\n' > shallow/Shaders/shared_header.fxh

                cat > user/Shaders/uses_shared.fx <<'EOF'
                #include "shared_header.fxh"

                #if SHARED_VALUE != 2
                    #error "a header nested deeper won over the one at the root of a search path"
                #endif

                float4 SharedVS(uint id : SV_VertexID) : SV_Position
                {
                    return float4(0.0, 0.0, 0.0, 1.0);
                }

                float4 SharedPS(float4 pos : SV_Position) : SV_Target
                {
                    return float4(SHARED_VALUE, 0.0, 0.0, 1.0);
                }

                technique UsesShared
                {
                    pass
                    {
                        VertexShader = SharedVS;
                        PixelShader  = SharedPS;
                    }
                }
                EOF

                status=0
                vkdaedalus-test-shaders --include deep/Shaders/Nested --include shallow/Shaders user/Shaders > report.txt 2>&1 || status=$?
                if [ "$status" -ne 0 ] || ! grep -q '^PASS  uses_shared' report.txt; then
                  cat report.txt
                  echo "a header at the root of a search path must win over one nested deeper"
                  exit 1
                fi
                touch $out
              '';

          checks.same-pack-header-wins =
            pkgs.runCommand "same-pack-header-wins" { nativeBuildInputs = [ self'.packages.vkdaedalus ]; }
              ''
                mkdir -p other/Shaders own/Shaders/inner
                printf '#define SHARED_VALUE 1\n' > other/Shaders/shared_header.fxh
                printf '#define SHARED_VALUE 2\n' > own/Shaders/shared_header.fxh

                cat > own/Shaders/inner/uses_shared.fx <<'EOF'
                #include "shared_header.fxh"

                #if SHARED_VALUE != 2
                    #error "resolved to another pack's copy of the header"
                #endif

                float4 SharedVS(uint id : SV_VertexID) : SV_Position
                {
                    return float4(0.0, 0.0, 0.0, 1.0);
                }

                float4 SharedPS(float4 pos : SV_Position) : SV_Target
                {
                    return float4(SHARED_VALUE, 0.0, 0.0, 1.0);
                }

                technique UsesShared
                {
                    pass
                    {
                        VertexShader = SharedVS;
                        PixelShader  = SharedPS;
                    }
                }
                EOF

                status=0
                vkdaedalus-test-shaders other/Shaders own/Shaders own/Shaders/inner > report.txt 2>&1 || status=$?
                if [ "$status" -ne 0 ] || ! grep -q '^PASS  uses_shared' report.txt; then
                  cat report.txt
                  echo "a header shipped by the shader's own pack must win over another pack's copy"
                  exit 1
                fi
                touch $out
              '';

          checks.precision-is-never-relaxed =
            pkgs.runCommand "precision-is-never-relaxed"
              {
                nativeBuildInputs = [
                  self'.packages.vkdaedalus
                  pkgs.spirv-tools
                ];
              }
              ''
                cp -r ${./test/language} shaders
                mkdir spv
                if ! vkdaedalus-test-shaders --dump-spirv spv shaders > report.txt 2>&1; then
                  cat report.txt
                  echo "the tester failed on the language shaders, so precision would be judged from a partial run"
                  exit 1
                fi

                seen=0
                for m in spv/*.spv; do
                  [ -e "$m" ] || continue
                  seen=1
                  dis=$(spirv-dis "$m")
                  if grep -qF RelaxedPrecision <<< "$dis"; then
                    echo "$m relaxes precision by default -- a driver may then accumulate bloom at fp16, which flickers"
                    exit 1
                  fi
                done
                [ "$seen" = "1" ] || { echo "no modules were emitted -- the check verified nothing"; exit 1; }

                mkdir spv-relaxed
                if ! vkdaedalus-test-shaders --relax-precision --dump-spirv spv-relaxed shaders > report-relaxed.txt 2>&1; then
                  cat report-relaxed.txt
                  echo "the tester failed on the language shaders with precision relaxed, so the opt-in would be judged from a partial run"
                  exit 1
                fi
                emitted=0
                relaxed=0
                for m in spv-relaxed/min_precision__*.spv; do
                  [ -e "$m" ] || continue
                  emitted=1
                  dis=$(spirv-dis "$m")
                  if grep -qF RelaxedPrecision <<< "$dis"; then relaxed=1; fi
                done
                [ "$emitted" = "1" ] \
                  || { echo "no min_precision modules were emitted -- the check verified nothing"; exit 1; }
                [ "$relaxed" = "1" ] \
                  || { echo "opting in must actually relax min16float math, or the half-precision toggle is a dead switch"; exit 1; }
                touch $out
              '';

          checks.compute-spirv-is-valid =
            pkgs.runCommand "compute-spirv-is-valid"
              {
                nativeBuildInputs = [
                  self'.packages.vkdaedalus
                  pkgs.spirv-tools
                ];
              }
              ''
                mkdir -p shaders
                cp ${./test/effects/compute_smoke.fx} shaders/compute_smoke.fx

                status=0
                vkdaedalus-test-shaders --dump-spirv "$PWD" shaders > report.txt 2>&1 || status=$?
                [ "$status" -eq 0 ] && grep -q '^PASS  compute_smoke' report.txt || { cat report.txt; echo "the compute smoke shader stopped compiling (tester exit $status)"; exit 1; }

                for spv in compute_smoke__*.spv; do
                  spirv-val --target-env vulkan1.1 "$spv" \
                    || { echo "the compute SPIR-V this compiler emits is invalid: $spv"; exit 1; }
                done

                cat compute_smoke__*.spv > /dev/null
                spirv-dis compute_smoke__*SmokeCS*.spv > dis.txt
                grep -q 'OpEntryPoint GLCompute' dis.txt \
                  || { echo "no GLCompute entry point -- the compute function was compiled as a graphics stage"; exit 1; }
                grep -qE 'OpExecutionMode .* LocalSize 8 8 1' dis.txt \
                  || { echo "the <8, 8> thread group size did not reach the LocalSize execution mode"; exit 1; }
                grep -q 'OpImageWrite' dis.txt \
                  || { echo "tex2Dstore did not become an image write"; exit 1; }
                grep -q 'OpControlBarrier' dis.txt \
                  || { echo "barrier() did not become a control barrier"; exit 1; }
                grep -q 'OpVariable %_ptr_Workgroup[A-Za-z0-9_]* Workgroup' dis.txt \
                  || { echo "groupshared is not Workgroup storage -- every invocation would get a private copy and barrier() would guard nothing"; exit 1; }
                touch $out
              '';

          checks.waits-are-scoped-to-our-own-work = pkgs.runCommand "waits-are-scoped-to-our-own-work" { } ''
            src=${./src/basalt.cpp}

            wait=$(sed -n '/void LogicalSwapchain::waitForEffects/,/^    }/p' ${./src/logical_swapchain.cpp})
            grep -q 'WaitForFences' <<< "$wait" \
              || { echo "the shared wait drains the queue again instead of waiting on the layer's own passes"; exit 1; }

            reload=$(sed -n '/void reloadEffectsForSwapchain/,/^        Logger::info("reloading/p' "$src")
            grep -q 'waitForEffects()' <<< "$reload" \
              || { echo "the reload frees effects without waiting for the submissions that reference them"; exit 1; }

            realloc=$(sed -n '/void reallocateCommandBuffers/,/^    }/p' "$src")
            grep -q 'waitForEffects()' <<< "$realloc" \
              || { echo "command buffers are freed and re-recorded while a submission of them may still be pending"; exit 1; }

            destroy=$(sed -n '/VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroyImage/,/^    }/p' "$src")
            record=$(grep -n -m1 'reallocateCommandBuffers(' <<< "$destroy" || true)
            view=$(grep -n -m1 'DestroyImageView(' <<< "$destroy" || true)
            [ -n "$record" ] && [ -n "$view" ] \
              || { echo "vkDestroyImage no longer re-records the effect command buffers, or no longer releases the depth view"; exit 1; }
            record=$(cut -d: -f1 <<< "$record")
            view=$(cut -d: -f1 <<< "$view")
            [ "$record" -lt "$view" ] \
              || { echo "the depth view is destroyed before the command buffers naming it are waited out and re-recorded"; exit 1; }

            grep -q 'QueueSubmit(submitQueue, 1, &submitInfo, effectFence)' "$src" \
              || { echo "the effect submission carries no fence, so nothing can wait on just that work"; exit 1; }

            queueWaitIdleCount=$(grep -cE 'QueueWaitIdle' ${./src/image.cpp} || true)
            grep -qx 1 <<<"$queueWaitIdleCount" \
              || { echo "image.cpp should hold exactly one QueueWaitIdle, the fallback inside submitAndWait"; exit 1; }
            grep -q 'static VkResult submitAndWait' ${./src/image.cpp} \
              || { echo "the one-shot uploads no longer share a single scoped wait"; exit 1; }
            touch $out
          '';

          checks.layer-submits-where-the-game-presents =
            pkgs.runCommand "layer-submits-where-the-game-presents"
              {
                src = ./src;
              }
              ''
                present=$(sed -n '/VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR/,/^    }/p' "$src/basalt.cpp")
                grep -Fq 'submitQueueFor(*pLogicalDevice, queue)' <<< "$present" \
                  || { echo "the present no longer picks its submit queue from the queue the game presents on. Submitting on the device's first graphics queue races a game thread that submits there while another presents"; exit 1; }

                found=0
                grep -rnE '(QueueSubmit|QueueWaitIdle)\(pLogicalDevice->queue\b' "$src" || found=$?
                case "$found" in
                  0)
                    echo "the layer submits to or drains the game's first graphics queue by name. A game may be using it on another thread; go through submitQueueFor or backgroundQueue"
                    exit 1
                    ;;
                  1) ;;
                  *)
                    echo "the source scan failed with exit $found"
                    exit 1
                    ;;
                esac

                grep -Fq 'requestLayerQueue(pCreateInfo->pQueueCreateInfos, pCreateInfo->queueCreateInfoCount, families)' "$src/basalt.cpp" \
                  || { echo "the device is no longer asked for a queue of the layer's own"; exit 1; }

                record=$(sed -n '/ImGuiOverlay::Frame ImGuiOverlay::drawFrame/,/^    }$/p' "$src/overlay/imgui_overlay.cpp")
                order=$(awk '/texturesPending\(/ && !t { t = NR } /waitForAllDraws\(\);/ && t && !w { w = NR } /updateTextures\(/ && !r { r = NR } END { print (t && w && r && t < w && w < r) ? "ok" : "wrong" }' <<< "$record")
                [ "$order" = ok ] \
                  || { echo "ImGui updates a texture on the layer's queue while an overlay draw submitted on the game's present queue may still sample it. Wait for the overlay's draws before an update"; exit 1; }

                fenceless=$(sed -n '/void LogicalSwapchain::waitForEffects/,/^    }/p' "$src/logical_swapchain.cpp")
                present=$(sed -n '/VkResult waitForPreviousSubmissions/,/^    }/p' "$src/basalt.cpp")
                if grep -qE '^ *else if .*fencelessEffectQueues' <<< "$fenceless$present"; then
                  echo "a fenceless effect submit is drained only while its image has no fence. Once a later fence exists, the wait returns at once and the earlier fenceless submit may still be running"
                  exit 1
                fi
                grep -Eq 'u\.pLayerInfo *= *linkBelow;' "$src/basalt.cpp" \
                  || { echo "the device creation retried without the layer's own queue reuses the layer chain the first attempt moved past every layer below, so the next layer reads a missing link"; exit 1; }
                grep -Fq 'fencelessEffectQueues' <<< "$fenceless" && ! grep -Fq 'backgroundQueue' <<< "$fenceless" \
                  || { echo "an effect submitted without a fence is waited out by draining a queue it was not submitted on. Drain the queue each fenceless submit went to"; exit 1; }

                touch $out
              '';

          checks.depth-tracking-cannot-drift = pkgs.runCommand "depth-tracking-cannot-drift" { } ''
            grep -qE 'depthFormats|depthImageViews' ${./src/logical_device.hpp} ${./src/basalt.cpp} \
              && { echo "depth tracking is back to parallel vectors -- they drift apart when images are created from several threads"; exit 1; }
            grep -q 'std::vector<DepthImage>  depthImages;' ${./src/logical_device.hpp} \
              || { echo "depth images are no longer one record each"; exit 1; }
            grep -q 'depthImage.image == image' ${./src/basalt.cpp} \
              || { echo "depth images are matched by position again rather than by handle"; exit 1; }
            touch $out
          '';

          checks.compiles-never-hold-the-lock = pkgs.runCommand "compiles-never-hold-the-lock" { } ''
            src=${./src/basalt.cpp}

            compile=$(sed -n '/^    void warmEffectCompile(/,/^    }/p' "$src")
            grep -q 'getOrCompileReshadeEffect' <<< "$compile" \
              || { echo "warmEffectCompile no longer compiles anything"; exit 1; }

            outside=$(awk '/getOrCompileReshadeEffect/ { n++ } END { print n + 0 }' "$src")
            inside=$(awk '/getOrCompileReshadeEffect/ { n++ } END { print n + 0 }' <<< "$compile")
            [ "$outside" = "$inside" ] \
              || { echo "the layer compiles an effect outside warmEffectCompile -- that call may be holding globalLock"; exit 1; }

            warm=$(sed -n '/void runEffectCompileWarmUp/,/^    }/p' "$src")
            jobs=$(sed -n '/^    std::vector<EffectCompileJobs::Compile> chainCompilesForLocked(/,/^    }/p' "$src")
            callers=$(awk '/warmEffectCompile\(/ { n++ } END { print n + 0 }' "$src")
            released=$(awk '/warmEffectCompile\(/ { n++ } END { print n + 0 }' <<< "$warm")
            background=$(awk '/warmEffectCompile\(/ { n++ } END { print n + 0 }' <<< "$jobs")
            [ "$released" -ge 1 ] && [ "$background" -ge 1 ] && [ "$callers" -eq $((released + background + 1)) ] \
              || { echo "warmEffectCompile is called outside the released warm-up and the added-effect job, so a compile may hold globalLock"; exit 1; }

            awk 'match($0, /runEffectCompileWarmUp\([A-Za-z_]+, /) && !/void runEffectCompileWarmUp/ { lock = substr($0, RSTART + 23, RLENGTH - 25); calls++; if (index(previous, lock ".unlock();") == 0) held++ } NF { previous = $0 } END { exit (calls > 0 && held == 0) ? 0 : 1 }' "$src" \
              || { echo "a compile warm-up runs without releasing globalLock on the line before it, which is the whole point of warming"; exit 1; }
            touch $out
          '';

          checks.down-chain-calls-never-hold-the-lock =
            pkgs.runCommand "down-chain-calls-never-hold-the-lock" { }
              ''
                src=${./src/basalt.cpp}

                create=$(sed -n '/VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR/,/^    }/p' "$src")
                [ -n "$create" ] || { echo "vkBasalt_CreateSwapchainKHR is gone"; exit 1; }

                grep -q 'pLogicalDevice->vkd.CreateSwapchainKHR(' <<< "$create" \
                  && { echo "the down-chain create reads the dispatch table at call time again -- copy the pointer before releasing the lock"; exit 1; }
                grep -q '\*pSwapchain = VK_NULL_HANDLE;' <<< "$create" \
                  || { echo "a failed swapchain creation hands the application a live-looking handle"; exit 1; }

                present=$(sed -n '/VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR/,/^    }/p' "$src")
                grep -q 'l.unlock();' <<< "$present" \
                  || { echo "the present no longer releases globalLock for the down-chain present"; exit 1; }
                grep -q 'pLogicalDevice->vkd.QueuePresentKHR(' <<< "$present" \
                  && { echo "a present path calls down the chain while it holds globalLock -- copy the pointer and release the lock first, as the effect path does"; exit 1; }
                awk '/downchainPresent\(/ { calls++; if (previous !~ /l\.unlock\(\);/) held++ } NF { previous = $0 } END { exit (calls > 1 && held == 0) ? 0 : 1 }' <<< "$present" \
                  || { echo "a present path calls down the chain without releasing globalLock on the line before"; exit 1; }
                touch $out
              '';

          checks.effect-image-pool-is-bounded = pkgs.runCommand "effect-image-pool-is-bounded" { } ''
            body=$(sed -n '/bool appendFakeSwapchainImages/,/^    }/p' ${./src/basalt.cpp})
            [ -n "$body" ] || { echo "appendFakeSwapchainImages is gone -- the pool can no longer grow"; exit 1; }
            grep -q 'settingsManager.getMaxEffects()' <<< "$body" \
              || { echo "growth is unbounded -- a runaway effect list could exhaust video memory. No lane grows past maxEffects, so nothing else would notice"; exit 1; }
            touch $out
          '';

          checks.added-effects-compile-off-the-frame =
            pkgs.runCommand "added-effects-compile-off-the-frame" { }
              ''
                done=$(sed -n '/const bool addChosen = ui::button(addLabel/,/^        }/p' ${./src/overlay/view_add_effects.cpp})
                [ -n "$done" ] || { echo "the Add Effects view has no Add button for the chosen effects any more"; exit 1; }
                grep -q 'startAddedEffects' <<< "$done" \
                  || { echo "Add no longer hands the chosen effects to a background compile"; exit 1; }
                poll=$(sed -n '/^    ReloadReason pollControls(/,/^    }/p' ${./src/basalt.cpp})
                grep -q 'processAddedEffects()' <<< "$poll" \
                  || { echo "added effects no longer join from the present path, so with the overlay closed they never join"; exit 1; }
                drawers=$(cat ${./src/overlay}/*.cpp | awk '/processAddedEffects\(\)/ { n++ } END { print n + 0 }')
                [ "$drawers" -eq 1 ] \
                  || { echo "the overlay calls processAddedEffects itself, so a join waits for the overlay to be open"; exit 1; }
                warm=$(sed -n '/^    std::vector<EffectCompileJobs::Compile> chainCompilesForLocked(/,/^    }/p' ${./src/basalt.cpp})
                grep -q 'gatherEffectCompileRequestsLocked' <<< "$warm" && grep -q 'warmEffectCompile' <<< "$warm" \
                  || { echo "an added effect's warm-up no longer asks for the chain's own compile requests, so the chain compiles it again inside the present"; exit 1; }
                touch $out
              '';

          checks.order-layers-name-both-layers =
            pkgs.runCommand "order-layers-name-both-layers" { nativeBuildInputs = [ pkgs.jq ]; }
              ''
                ours=$(jq -er .layer.name ${./config/vkdaedalus.json.in})
                lsfg=$(sed -n 's/.*lsfgLayerName = sizeof(void\*) == 8 ? "\([A-Za-z0-9_]*\)".*/\1/p' ${./src/frame_generation.hpp})
                [ -n "$lsfg" ] || { echo "lsfgLayerName is gone from frame_generation.hpp, so the order layers cannot be checked against it"; exit 1; }
                before=$(jq -ec '.layer.component_layers' ${./config/order-layers/vkdaedalus_before_lsfgvk.json})
                after=$(jq -ec '.layer.component_layers' ${./config/order-layers/lsfgvk_before_vkdaedalus.json})
                [ "$before" = "[\"$ours\",\"$lsfg\"]" ] \
                  || { echo "the default order layer lists $before, want $ours then $lsfg, so the loader drops it or keeps the wrong order"; exit 1; }
                [ "$after" = "[\"$lsfg\",\"$ours\"]" ] \
                  || { echo "the after order layer lists $after, want $lsfg then $ours, so the loader drops it or keeps the wrong order"; exit 1; }
                touch $out
              '';

          checks.profile-changes-save-in-order = pkgs.runCommand "profile-changes-save-in-order" { } ''
            src=${./src/overlay/view_main.cpp}
            create=$(sed -n '/ui::beginPopup("NewProfilePopup")/,/ui::endPopup();/p' "$src")
            rename=$(sed -n '/ui::beginPopup("RenameProfilePopup")/,/ui::endPopup();/p' "$src")
            delete=$(sed -n '/ui::beginPopup("DeleteProfilePopup")/,/ui::endPopup();/p' "$src")
            switch=$(sed -n '/const auto switchTo = \[&\]/,/};/p' "$src")
            [ -n "$create" ] && [ -n "$rename" ] && [ -n "$delete" ] || { echo "the new, rename or delete profile popup is gone"; exit 1; }
            grep -q 'pendingConfigPath = activeProfilePath' <<< "$switch" \
              || { echo "switchTo does not switch the layer to the profile's file, so it watches and saves the old one"; exit 1; }
            order=$(awk '/autoSaveProfile\(\)/ && !s { s = NR } /createProfile\(/ && !c { c = NR } /switchTo\(/ && !p { p = NR } END { print (s && c && p && s < c && c < p) ? "ok" : "wrong" }' <<< "$create")
            [ "$order" = ok ] \
              || { echo "a new profile is copied before the unsaved changes are saved, or the layer is not switched to it, so it watches and saves the old file"; exit 1; }
            renamed=$(awk '/autoSaveProfile\(\)/ && !s { s = NR } /renameProfile\(/ && !r { r = NR } /switchTo\(/ && !p { p = NR } END { print (s && r && p && s < r && r < p) ? "ok" : "wrong" }' <<< "$rename")
            [ "$renamed" = ok ] \
              || { echo "a profile is renamed before its unsaved changes are saved, or the layer keeps the old file, which no longer exists"; exit 1; }
            cleared=$(awk '/deleteProfile\(/ && !d { d = NR } /profileDirty *= false/ && !f { f = NR } /switchTo\(/ && !p { p = NR } END { print (d && f && p && f < p) ? "ok" : "wrong" }' <<< "$delete")
            [ "$cleared" = ok ] \
              || { echo "deleting a profile keeps its unsaved changes, which the next autosave writes over the default profile"; exit 1; }
            touch $out
          '';

          checks.overlay-rows-close-what-they-open =
            pkgs.runCommand "overlay-rows-close-what-they-open" { }
              ''
                remove=$(sed -n '/ui::option("Remove"/,/ui::endPopup();/p' ${./src/overlay/view_main.cpp})
                [ -n "$remove" ] || { echo "the effect row has no Remove item any more"; exit 1; }
                grep -qE 'break;|PopID|erase\(' <<< "$remove" \
                  && { echo "Remove leaves the effect row early or erases inside it; with the row expanded that leaves an ID and a TreePop on ImGui's stacks, and the assert kills the game"; exit 1; }
                touch $out
              '';

          checks.each-overlay-draws-its-own-context =
            pkgs.runCommand "each-overlay-draws-its-own-context" { src = ./src/overlay/imgui_overlay.cpp; }
              ''
                frame=$(sed -n '/ImGuiOverlay::Frame ImGuiOverlay::drawFrame/,/^    }$/p' "$src")
                order=$(awk '/ImGui::SetCurrentContext\(context\);/ && !c { c = NR } /ImGui::NewFrame\(\);/ && !n { n = NR } END { print (c && n && c < n) ? "ok" : "wrong" }' <<< "$frame")
                [ "$order" = ok ] \
                  || { echo "a frame starts in whichever ImGui context was made last, so with two devices one overlay draws the other's windows and settings"; exit 1; }
                grep -Fq 'ImGui::DestroyContext(context);' "$src" \
                  || { echo "the overlay destroys the current ImGui context, which may be another device's"; exit 1; }
                touch $out
              '';

          checks.overlay-errors-never-abort-the-game =
            pkgs.runCommand "overlay-errors-never-abort-the-game" { }
              ''
                src=${./src/overlay/imgui_overlay.cpp}
                grep -q 'io.ConfigErrorRecoveryEnableAssert = false;' "$src" \
                  || { echo "ImGui asserts on a recoverable UI error, which aborts the game, since the layer builds without NDEBUG"; exit 1; }
                grep -q 'ErrorCallback = ' "$src" \
                  || { echo "ImGui's recoverable errors no longer reach the layer's log, so a UI bug would go unseen"; exit 1; }
                touch $out
              '';

          checks.safe-anti-cheat-belongs-to-the-game =
            pkgs.runCommand "safe-anti-cheat-belongs-to-the-game" { }
              ''
                switch=$(sed -n '/void switchConfig(const std::string& configPath)/,/^    }/p' ${./src/basalt.cpp})
                save=$(sed -n '/bool ConfigSerializer::saveToPath(/,/^    }/p' ${./src/config_serializer.cpp})
                [ -n "$switch" ] && [ -n "$save" ] || { echo "switchConfig or saveToPath is gone"; exit 1; }
                if grep -qE 'readSafeAntiCheat|safeAntiCheatSource' <<< "$switch"; then
                  echo "a profile switch reads Safe Anti-Cheat, so sharing or switching a profile changes it for the game"
                  exit 1
                fi
                if grep -q 'file << "safeAntiCheat' <<< "$save"; then
                  echo "a profile save writes Safe Anti-Cheat into the profile, so every game sharing it would take one game's choice"
                  exit 1
                fi
                grep -q 'ConfigSerializer::saveGameSettings(activeGameName' ${./src/overlay/view_main.cpp} \
                  || { echo "the Safe Anti-Cheat tile does not save to the game's own file"; exit 1; }
                touch $out
              '';

          checks.depth-setting-survives-safe-anti-cheat =
            pkgs.runCommand "depth-setting-survives-safe-anti-cheat" { }
              ''
                status=0
                writers=$(grep -rn 'setDepthCapture(false)' ${./src}) || status=$?
                [ "$status" -le 1 ] || { echo "the scan for depth capture writes failed with $status"; exit 1; }
                [ -z "$writers" ] \
                  || { echo "something writes depth capture off instead of Safe Anti-Cheat masking it, so the next settings save erases the user's choice for every game: $writers"; exit 1; }
                touch $out
              '';

          checks.shader-scans-have-one-walker = pkgs.runCommand "shader-scans-have-one-walker" { } ''
            src=${./src/overlay/view_shader_manager.cpp}
            grep -q 'directory_iterator' "$src" \
              && { echo "the shader manager walks folders itself; Rescan All has to find what the first-run scan finds, so it calls ConfigSerializer::scanShaderTree"; exit 1; }
            grep -q 'ConfigSerializer::scanShaderTree' "$src" \
              || { echo "Rescan All no longer uses the shared scan"; exit 1; }
            touch $out
          '';

          checks.layer-never-reorders-gpus = pkgs.runCommand "layer-never-reorders-gpus" { } ''
            intercepts=$(awk '/#define INTERCEPT_CALLS/ { on = 1 } on { print; if ($0 !~ /\\$/) exit }' ${./src/basalt.cpp})
            [ -n "$intercepts" ] || { echo "INTERCEPT_CALLS is gone, so nothing shows which calls the layer takes"; exit 1; }
            if grep -nE 'EnumeratePhysicalDevice|GetPhysicalDeviceProperties' <<< "$intercepts"; then
              echo "the layer takes the game's device enumeration or properties, which can change the GPU a game sees first or picks"
              exit 1
            fi
            status=0
            found=$(grep -rnE '"vkEnumeratePhysicalDevice|"vkGetPhysicalDeviceProperties|\b(setenv|putenv)[[:space:]]*\(' ${./src}) || status=$?
            [ "$status" -le 1 ] || { echo "the scan for device enumeration intercepts failed with $status"; exit 1; }
            [ -z "$found" ] || { echo "the layer intercepts device enumeration by name or sets the environment the loader reads: $found"; exit 1; }
            touch $out
          '';

          checks.sharing-extensions-follow-the-profile =
            pkgs.runCommand "sharing-extensions-follow-the-profile" { }
              ''
                names='EXTERNAL_MEMORY_FD_EXTENSION_NAME|EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME|EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME|QUEUE_FAMILY_FOREIGN_EXTENSION_NAME|VK_KHR_external_memory_fd|VK_EXT_external_memory_dma_buf|VK_KHR_external_semaphore_fd|VK_EXT_queue_family_foreign'
                status=0
                elsewhere=$(grep -rlE "$names" ${./src} | grep -v '/helper_device\.cpp$') || status=$?
                [ "$status" -le 1 ] || { echo "the scan for the sharing extension names failed with $status"; exit 1; }
                [ -z "$elsewhere" ] || { echo "the sharing extensions are named outside addSharingExtensions in helper_device.cpp: $elsewhere"; exit 1; }
                awk -v names="$names" '
                  /^    std::string addSharingExtensions\(/ { inside = 1; defined = 1 }
                  !inside && $0 ~ names { print "helper_device.cpp:" NR " names a sharing extension outside addSharingExtensions"; bad = 1 }
                  inside && /^    }$/ { inside = 0 }
                  END { if (!defined) { print "addSharingExtensions is gone from helper_device.cpp"; bad = 1 } exit bad }' ${./src/helper_device.cpp}
                awk '
                  { line[NR] = $0 }
                  /addSharingExtensions\(/ { calls[NR] = 1 }
                  END {
                    for (c in calls) {
                      n++; depth = 0; guarded = 0
                      for (i = c - 1; i > 0; i--) {
                        if (line[i] ~ /^[[:space:]]*}[[:space:]]*$/) depth++
                        else if (line[i] ~ /^[[:space:]]*\{[[:space:]]*$/) {
                          if (depth == 0) { guarded = line[i - 1] ~ /^[[:space:]]*if \(effectRegistry\.listsAnotherGpu\(\)\)[[:space:]]*$/; break }
                          depth--
                        }
                      }
                      if (!guarded) { print "basalt.cpp:" c " adds the sharing extensions to the device of the game outside if (effectRegistry.listsAnotherGpu())"; bad = 1 }
                    }
                    if (!n) { print "the device of the game never adds the sharing extensions, so no profile can share a frame with another GPU"; bad = 1 }
                    exit bad
                  }' ${./src/basalt.cpp}
                touch $out
              '';

          checks.helper-device-is-made-off-the-lock =
            pkgs.runCommand "helper-device-is-made-off-the-lock" { }
              ''
                body=$(sed -n '/^    std::variant<std::shared_ptr<HelperDevice>, std::string> makeHelperDevice(/,/^    }$/p' ${./src/helper_device.cpp})
                [ -n "$body" ] || { echo "makeHelperDevice is gone, so nothing shows how a device on another GPU is made"; exit 1; }
                first=$(sed -n '3p' <<< "$body")
                [[ "$first" =~ ^[[:space:]]*if\ \(released\.owns_lock\(\)\)$ ]] \
                  || { echo "makeHelperDevice does not refuse a held layer lock first: the loader calls back into the layer while it makes the device"; exit 1; }
                status=0
                callbacks=$(grep -rnE '(\.|->)create\(' ${./src}) || status=$?
                [ "$status" -le 1 ] || { echo "the scan for the loader device callback failed with $status"; exit 1; }
                [ "$(grep -c . <<< "$callbacks")" -eq 1 ] && grep -q 'helper_device.cpp:' <<< "$callbacks" && grep -qF "$(cut -d: -f3- <<< "$callbacks")" <<< "$body" \
                  || { echo "the loader device callback is called outside makeHelperDevice: $callbacks"; exit 1; }
                awk 'match($0, /makeHelperDevice\([A-Za-z_]+, /) && !/makeHelperDevice\(const / { lock = substr($0, RSTART + 17, RLENGTH - 19); calls++; if (index(previous, lock ".unlock();") == 0) { print "basalt.cpp:" NR " makes a device on another GPU without unlocking " lock " on the line before"; held++ } } NF { previous = $0 } END { if (!calls) print "nothing makes a device on another GPU"; exit (calls > 0 && held == 0) ? 0 : 1 }' ${./src/basalt.cpp}
                touch $out
              '';

          checks.helper-never-enters-the-layer = pkgs.runCommand "helper-never-enters-the-layer" { } ''
            create=$(sed -n '/^    VkResult VKAPI_CALL vkBasalt_CreateDevice(/,/^    }$/p' ${./src/basalt.cpp})
            [ -n "$create" ] || { echo "vkBasalt_CreateDevice is gone"; exit 1; }
            first=$(awk '/^    {$/ { getline; print; exit }' <<< "$create")
            [[ "$first" =~ ^[[:space:]]*if\ \(makingHelperDevice\(\)\)$ ]] \
              || { echo "vkBasalt_CreateDevice does not refuse a device on another GPU before anything else, so a chain built through the layer would re-enter it"; exit 1; }
            helper=${./src/helper_device.cpp}
            grep -q 'RTLD_NOLOAD' "$helper" && grep -q 'dlsym(self, "vkDaedalus_GetInstanceProcAddr")' "$helper" \
              || { echo "the layer's vkGetInstanceProcAddr for the loader is not looked up in the layer's own library, so a symbol of another vkBasalt could answer"; exit 1; }
            if grep -n '&vkDaedalus_GetInstanceProcAddr' "$helper"; then
              echo "the loader is handed the address of vkDaedalus_GetInstanceProcAddr, which can resolve to another library's symbol"
              exit 1
            fi
            inserts=$(grep -c 'deviceMap.insert_or_assign' ${./src/basalt.cpp})
            inCreate=$(grep -c 'deviceMap.insert_or_assign' <<< "$create")
            [ "$inserts" -ge 1 ] && [ "$inserts" -eq "$inCreate" ] \
              || { echo "a device enters the layer's device map outside vkBasalt_CreateDevice, so a helper could be treated as a game's device"; exit 1; }
            touch $out
          '';

          checks.present-waits-the-helper = pkgs.runCommand "present-waits-the-helper" { } ''
            wait=$(sed -n '/^    VkResult waitForPreviousSubmissions(/,/^    }$/p' ${./src/basalt.cpp})
            awk '/l\.unlock\(\);/ { open = 1 } /helper->device\.vkd\.WaitForFences/ { if (open) waited = 1 } /l\.lock\(\);/ { if (open) closedAfter = waited; open = 0 } END { exit closedAfter ? 0 : 1 }' <<< "$wait" \
              || { echo "the present does not wait GPU 2's fence for the image with the layer lock released, so a reused tail can be overwritten while it runs"; exit 1; }
            present=$(sed -n '/VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(/,/^    }$/p' ${./src/basalt.cpp})
            awk '/for \(ChainSegment& segment : chain->segments\)/ { tailUpdate = NR } /submitThroughChain\(/ { handOff = NR } END { exit (tailUpdate && handOff && tailUpdate < handOff) ? 0 : 1 }' <<< "$present" \
              || { echo "the uniforms of the chain's segments are not written before its hand-off, after the wait that makes them safe to write"; exit 1; }
            grep -qF 'const VkFence fence            = last ? effectFence : fenced ? segment.fences[index] : VK_NULL_HANDLE;' ${./src/hand_off.cpp} \
              && grep -qF 'QueueSubmit(side.queue, 1, &onSegment, fence)' ${./src/hand_off.cpp} \
              || { echo "the submit on GPU 2 carries no fence, so nothing tells the present when the tail is done with an image"; exit 1; }
            touch $out
          '';

          checks.unimported-fds-are-closed = pkgs.runCommand "unimported-fds-are-closed" { } ''
            [ "$(grep -rc 'ImportSemaphoreFdKHR(' ${./src} | awk -F: '{ n += $NF } END { print n }')" = "1" ] \
              || { echo "a SYNC_FD is imported in more than one place, so not every import is the one that closes its fd on failure"; exit 1; }
            import=$(sed -n '/bool importSyncFd(/,/^        }$/p' ${./src/hand_off.cpp})
            grep -q 'close(fd);' <<< "$import" || { echo "a SYNC_FD a failed import did not take is left open"; exit 1; }
            [ "$(grep -rc 'VkImportMemoryFdInfoKHR ' ${./src} | awk -F: '{ n += $NF } END { print n }')" = "1" ] \
              || { echo "a dma-buf is imported in more than one place"; exit 1; }
            awk '/const auto refuseClosing/ { on = 1 } /BindBufferMemory\(importer\.device/ { on = 0 } on && /throw / && !/refuseClosing/ { print "gpu_chain.cpp:" NR " refuses while the exported dma-buf fd is open, without closing it"; bad = 1 } END { exit bad }' ${./src/gpu_chain.cpp}
            touch $out
          '';

          checks.depth-never-crosses = pkgs.runCommand "depth-never-crosses" { } ''
            for file in ${./src/gpu_chain.cpp} ${./src/hand_off.cpp}; do
              if grep -nE 'getDepthState|depthImages|effectDepthImage' "$file"; then
                echo "the hand-off reaches the game's depth buffer, which stays on the game's GPU"
                exit 1
              fi
            done
            awk '/recordCommandBuffers\(/ { calls++; if ($0 !~ /VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED/) { print "gpu_chain.cpp:" NR " records a segment of the chain with a depth image"; bad = 1 } } END { exit (calls > 0 && !bad) ? 0 : 1 }' ${./src/gpu_chain.cpp}
            grep -q 'if (compiled->usesDepth && pLogicalDevice->isHelper && settingsManager.getDepthCapture())' ${./src/effects/effect_reshade.cpp} \
              || { echo "an effect reading depth is built on GPU 2 without refusal, where no depth image exists"; exit 1; }
            touch $out
          '';

          checks.layer-never-hides-itself = pkgs.runCommand "layer-never-hides-itself" { } ''
            body=$(sed -n '/VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties/,/^    }/p' ${./src/basalt.cpp})
            [ -n "$body" ] || { echo "vkBasalt_EnumerateInstanceLayerProperties is gone"; exit 1; }
            grep -qE 'SafeAntiCheat|\*pPropertyCount = 0' <<< "$body" \
              && { echo "the layer reports itself absent from its own layer list; Safe Anti-Cheat removes depth access and never claims to hide the layer"; exit 1; }
            touch $out
          '';

          checks.depth-view-follows-depth-capture = pkgs.runCommand "depth-view-follows-depth-capture" { } ''
            body=$(sed -n '/^    DepthState getDepthState(LogicalDevice\* pLogicalDevice)$/,/^    }/p' ${./src/basalt.cpp})
            [ -n "$body" ] || { echo "getDepthState is gone"; exit 1; }
            order=$(awk '/if \(!settingsManager\.getDepthCapture\(\)\)/ && !g { g = NR } /pLogicalDevice->depthImages/ && !l { l = NR } END { print (g && l && g < l) ? "guarded" : "open" }' <<< "$body")
            [ "$order" = guarded ] \
              || { echo "getDepthState hands out the depth view without asking getDepthCapture, so an effect still reads depth after Safe Anti-Cheat or the setting turned capture off mid-session"; exit 1; }
            touch $out
          '';

          checks.refusals-never-disable-an-effect = pkgs.runCommand "refusals-never-disable-an-effect" { } ''
            for kind in SafeAntiCheatRefusal DeviceRefusal DepthOnOtherGpuRefusal ChainRefusal; do
              refusal=$(sed -n "/catch (const $kind& e)/,/^ *}$/p" ${./src/basalt.cpp})
              [ -n "$refusal" ] || { echo "nothing catches $kind any more"; exit 1; }
              grep -qE 'setEffectError|setEffectEnabled' <<< "$refusal" \
                && { echo "the $kind catch marks the effect failed or disabled, which the next save writes as the user's choice, so the effect never returns where it can run"; exit 1; }
              case $kind in
                SafeAntiCheatRefusal | DeviceRefusal)
                  grep -q 'setEffectOnDevice' <<< "$refusal" \
                    || { echo "the $kind catch no longer marks the effect left out on its GPU, so the overlay cannot say why it does not run"; exit 1; }
                  ;;
              esac
            done
            touch $out
          '';

          checks.overlay-keeps-no-safe-anti-cheat-copy =
            pkgs.runCommand "overlay-keeps-no-safe-anti-cheat-copy" { }
              ''
                status=0
                copies=$(grep -niE '^ +bool +[A-Za-z]*SafeAntiCheat[A-Za-z]* *(=|;)' ${./src/overlay}/*.hpp) || status=$?
                [ "$status" -le 1 ] || { echo "the scan for Safe Anti-Cheat copies failed with $status"; exit 1; }
                [ -z "$copies" ] \
                  || { echo "the overlay keeps its own Safe Anti-Cheat value, which drifts from the layer's on a profile switch, delete or edit; it reads settingsManager: $copies"; exit 1; }
                touch $out
              '';

          checks.add-effects-never-compiles = pkgs.runCommand "add-effects-never-compiles" { } ''
            status=0
            compiles=$(grep -nE 'startShaderTest\(|testShaderCompilation\(|getOrCompileReshadeEffect\(|ensureEffect\(' ${./src/overlay/view_add_effects.cpp}) || status=$?
            [ "$status" -le 1 ] || { echo "the scan for compiles in the Add Effects view failed with $status"; exit 1; }
            [ -z "$compiles" ] \
              || { echo "the Add Effects view compiles shaders, inside the present with globalLock held, which freezes the game: $compiles"; exit 1; }
            touch $out
          '';

          checks.shader-test-runs-off-the-frame = pkgs.runCommand "shader-test-runs-off-the-frame" { } ''
            src=${./src/overlay/view_shader_test.cpp}
            poll=$(sed -n '/void ImGuiOverlay::processShaderTest()/,/^    }/p' "$src")
            start=$(sed -n '/void ImGuiOverlay::startShaderTest()/,/^    }/p' "$src")
            [ -n "$poll" ] && [ -n "$start" ] || { echo "the shader test start or poll is gone"; exit 1; }
            grep -q 'testShaderCompilation' <<< "$poll" \
              && { echo "the shader test compiles in its per-frame poll, inside the present with globalLock held"; exit 1; }
            grep -q 'std::async' <<< "$start" \
              || { echo "the shader test no longer compiles on its own thread"; exit 1; }
            touch $out
          '';

          checks.overlay-lists-every-built-in = pkgs.runCommand "overlay-lists-every-built-in" { } ''
            grep -q 'BuiltInEffects::instance().getTypeNames()' ${./src/overlay/view_add_effects.cpp} \
              || { echo "the add-effects view keeps its own list of built-in effects, which already left RCAS out; it must ask BuiltInEffects"; exit 1; }
            touch $out
          '';

          checks.compiles-copy-the-definitions = pkgs.runCommand "compiles-copy-the-definitions" { } ''
            status=0
            users=$(grep -rl 'getPreprocessorDefs(' ${./src}) || status=$?
            [ "$status" -le 1 ] || { echo "the scan for the definition accessors failed with $status"; exit 1; }
            for user in $users; do
              case "$user" in
                ${./src}/overlay/* | ${./src}/effects/effect_registry.*) ;;
                *) echo "$user takes a reference to the registry's preprocessor definitions, which escapes the registry mutex. A compile reads its copy through getCompilePreprocessorDefs"; exit 1 ;;
              esac
            done
            touch $out
          '';

          checks.compile-defines-have-one-source = pkgs.runCommand "compile-defines-have-one-source" { } ''
            status=0
            found=$(grep -rlF '{"BUFFER_WIDTH",' ${./src} ${./tools}) || status=$?
            [ "$status" -le 1 ] || { echo "the scan for the buffer defines failed with $status"; exit 1; }
            [ "$found" = "${./src}/reshade_compile_defines.hpp" ] \
              || { echo "the buffer defines are assembled outside reshade_compile_defines.hpp: $found. A copy that drifts gives the same effect a second cache key and a second compile"; exit 1; }
            touch $out
          '';

          checks.gpu-times-have-one-conversion = pkgs.runCommand "gpu-times-have-one-conversion" { } ''
            status=0
            found=$(grep -rnE --exclude=effect_timing.hpp '(timestampPeriod[A-Za-z]*[[:space:]]*[*/])|([*/][[:space:]]*[A-Za-z_.>-]*timestampPeriod)|(<<[[:space:]]*[A-Za-z_.>-]*timestampValidBits)' ${./src}) || status=$?
            case $status in
              0) echo "a GPU time is converted outside effect_timing.hpp: $found. A second conversion drifts from the one LATENCY.md documents and the latency-stats test recomputes"; exit 1 ;;
              1) ;;
              *) echo "the scan for timestamp conversions failed with $status"; exit 1 ;;
            esac
            touch $out
          '';

          checks.effect-views-are-owned-at-once = pkgs.runCommand "effect-views-are-owned-at-once" { } ''
            files=$(find ${./src/effects} -name '*.cpp')
            [ -n "$files" ] || { echo "no effect sources to scan"; exit 1; }
            status=0
            awk '
              { statement = statement $0 " " }
              /;[[:space:]]*$/ {
                if (statement ~ /(std::vector<VkImageView>|VkImageView)[[:space:]]+[A-Za-z_]+[[:space:]]*=/ && statement ~ /createImageViews\(/) {
                  print FILENAME ": " statement; held = 1
                }
                statement = ""
              }
              END { exit held ? 0 : 1 }' $files || status=$?
            case $status in
              0) echo "an effect holds a view it just created in a local: if the next creation throws, the destructor never sees it and the view leaks. Assign it to the member the destructor frees"; exit 1 ;;
              1) ;;
              *) echo "the scan of the effect sources failed with $status"; exit 1 ;;
            esac
            touch $out
          '';

          checks.typed-characters-are-bounded = pkgs.runCommand "typed-characters-are-bounded" { } ''
            src=${./src/keyboard_input_wayland.cpp}
            grep -q 'TYPED_CHARS_LIMIT' "$src" \
              || { echo "the typed character accumulator has no bound -- it is drained only while the overlay is open, so it would grow for the life of the game"; exit 1; }
            grep -q 'typedCharsAccumulator.size() + len <= TYPED_CHARS_LIMIT' "$src" \
              || { echo "the bound is declared but the append does not check it"; exit 1; }
            touch $out
          '';

          packages = {
            vkdaedalus = mkVkbasaltOverlay pkgs;

            vkdaedalus-debug = self'.packages.vkdaedalus.overrideAttrs {
              mesonBuildType = "debug";
            };

            default = self'.packages.vkdaedalus;

            shader-corpus-verdicts =
              let
                corpus = builtins.fromJSON (builtins.readFile ./test/shader-corpus.lock.json);
                packs = map (
                  pack:
                  pkgs.fetchFromGitHub {
                    inherit (pack)
                      owner
                      repo
                      rev
                      hash
                      ;
                  }
                ) corpus.packs;
              in
              pkgs.runCommand "shader-corpus-verdicts"
                {
                  nativeBuildInputs = [
                    self'.packages.vkdaedalus
                    pkgs.spirv-tools
                  ];
                }
                ''
                  bash ${./scripts/shader-corpus.sh} ${lib.escapeShellArgs packs} > $out
                '';

            vkdaedalus-live =
              let
                liveHelpers = pkgs.stdenv.mkDerivation {
                  name = "vkdaedalus-live-helpers";
                  src = ./test;
                  buildInputs = [
                    pkgs.vulkan-headers
                    pkgs.vulkan-loader
                    pkgs.libxcb
                    pkgs.libx11
                  ];
                  dontConfigure = true;
                  buildPhase = ''
                    bash ${./scripts/build-live-helpers.sh} . "$out"
                  '';
                  dontInstall = true;
                };
              in
              pkgs.writeShellApplication {
                name = "vkdaedalus-live";
                runtimeInputs = [
                  pkgs.vulkan-tools
                  pkgs.coreutils
                  pkgs.gnugrep
                  pkgs.gnused
                  pkgs.gawk
                  pkgs.findutils
                  pkgs.weston
                  pkgs.xwayland
                  pkgs.xdotool
                  pkgs.xwd
                  pkgs.netpbm
                  pkgs.procps
                  liveHelpers
                ]
                ++ pkgs.lib.optional (pkgs.lib.meta.availableOn pkgs.stdenv.hostPlatform pkgs.gamescope) pkgs.gamescope;
                runtimeEnv = {
                  VKB_LAYER = "${self'.packages.vkdaedalus}";
                  VKB_MOCK = "${liveHelpers}";
                  VKB_FAULT = "${liveHelpers}";
                  VKB_VALIDATION = "${pkgs.vulkan-validation-layers}";
                  VKB_SCREENSHOT = "${pkgs.vulkan-tools-lunarg}";
                  VKB_FIXTURES = "${./test}";
                  VKB_WSI_LIBRARIES = pkgs.lib.makeLibraryPath [
                    pkgs.wayland
                    pkgs.libxcb
                    pkgs.libx11
                  ];
                };
                text = builtins.readFile ./scripts/live.sh;
              };
          }
          // pkgs.lib.optionalAttrs (pkgs.stdenv.hostPlatform.system == "x86_64-linux") {
            vkdaedalus-i686 = mkVkbasaltOverlay pkgs.pkgsi686Linux;
          };
        };
    };
}

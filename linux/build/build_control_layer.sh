#!/usr/bin/env bash
# NR Control, the OptiScaler routes' NR menu (linux/src/layer/nr_control_layer.cpp): a 64-bit Linux Vulkan layer
# (the OptiScaler route is 64-bit only), into <out>/libVkLayer_dlssnr_control.so + VkLayer_dlssnr_control.json.
# Its two composite shaders (linux/shaders/control) are compiled into headers and built in.
#   build_control_layer.sh <out dir>
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=$(realpath -m -- "$1")
mkdir -p -- "$out"
imgui=${NR_IMGUI_ROOT:-toolchain/imgui-nr}
# X11 and xcb headers: the system's (libx11-dev, libxcb1-dev) unless given
x11=${NR_X11_INCLUDE:-/usr/include}
xcb=${NR_XCB_INCLUDE:-/usr/include}
glslang=${NR_GLSLANG:-toolchain/glslang/bin/glslang}
gen=$(mktemp -d -p "$out" .shaders.XXXXXX)
trap 'rm -rf -- "$gen"' EXIT
"$glslang" -V --target-env vulkan1.0 --vn kMenuComposeVert linux/shaders/control/menu_compose.vert -o "$gen/menu_compose_vert.h" > /dev/null
"$glslang" -V --target-env vulkan1.0 --vn kMenuComposeFrag linux/shaders/control/menu_compose.frag -o "$gen/menu_compose_frag.h" > /dev/null
g++ -std=c++17 -O2 -fPIC -shared -Wl,-Bsymbolic -Wl,-z,defs -static-libstdc++ -static-libgcc \
    -Wl,--version-script=linux/src/layer/nr_control_layer.map \
    -Itoolchain/Vulkan-Headers/include -Ilinux/src -Ilinux/src/core -Ilinux/src/layer -Ilinux/src/ui -I"$imgui" -I"$x11" -I"$xcb" \
    -I"$gen" -DVK_NO_PROTOTYPES \
    linux/src/layer/nr_control_layer.cpp linux/src/layer/nr_menu_input.cpp \
    "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_tables.cpp" "$imgui/imgui_widgets.cpp" "$imgui/imgui_impl_vulkan.cpp" \
    -Wl,-l:libX11.so.6 -Wl,-l:libxcb.so.1 -lpthread -o "$out/libVkLayer_dlssnr_control.so"
cp -- linux/src/layer/VkLayer_dlssnr_control.json "$out/"
echo "NR Control layer in $out"

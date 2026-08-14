# M5CardputerZero-Emulator

一个同时支持两件事的工程：

1. **Cardputer 模拟器（默认）**——把 M5CardputerZero 固件（APPLaunch）跑在本机 / 网页。
2. **通用 LVGL → Web 外壳（`EMU_GENERIC_WEB=ON`）**——把**任意** LVGL 显示工程编译成网页，支持触摸交互，分辨率任意。

本 README 重点讲第 2 种：怎么把你的 LVGL 工程部署到网页、怎么快速构建、怎么交接给 AI。

---

## 1. 通用外壳：能做什么

- 显示任意 LVGL 工程的 `ui_init()`，分辨率通过参数指定（已验证 1280×720、800×480）。
- **触摸 + 鼠标**开箱即用：点击/滑动 → SDL → LVGL 指针设备 → 你的 `LV_EVENT_*` 回调。
- **物理键盘**也能用（复用 LVGL 自带 SDL 键盘驱动），文本框输入没问题。
- 产物是纯静态文件 `index.html / index.js / index.wasm / index.data`，任何静态托管都能跑（本地 `python3 -m http.server` 即可）。

**你的 LVGL 工程只需要满足 3 个契约：**

| 契约 | 说明 |
|---|---|
| ① `extern "C" void ui_init(void)` | 唯一入口，模拟器只调用它（在你 UI 初始化代码里提供） |
| ② 只用 LVGL API | 不能 `#include` M5 / 其它硬件 SDK；硬件相关逻辑要剥离 |
| ③ 分辨率可指定 | 通过 `EMU_LCD_W/H` 传入，代码里**不要写死**屏幕尺寸 |

> LVGL 版本：默认用仓库里 `lib/lvgl` 子模块（v9.3.0 分支）。你可以自换版本（见 §4.4）。

---

## 2. 工程放哪里

随便放，构建时用路径指过去即可。仓库内建议约定 `apps/<工程名>/`：

```
apps/myproject/
├── ui/ui.c          # 定义 ui_init()（SquareLine / 手写都行）
├── ui/ui.h
├── ui/…             # 其它 ui 源码
├── images/          # 运行时加载的图片（可选）
└── fonts/           # 自定义字体（可选）
```

构建时 `-DEMU_APP_UI_DIR=apps/myproject` 或绝对路径都行。工程源码会以 glob 方式编译
（`*.c *.cpp` 递归），**不要**把你的 `main.cpp` 放进去——模拟器有自己的 main。

---

## 3. 手动构建（完整步骤）

### 3.1 装 Emscripten（只需一次）

```bash
git clone --depth 1 --branch 3.1.73 https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install 3.1.73 && ./emsdk activate 3.1.73
```

### 3.2 配置 + 编译

```bash
cd <repo>   # 本工程根目录
mkdir -p build-myproject && cd build-myproject
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DEMU_GENERIC_WEB=ON \
    -DEMU_APP_UI_DIR=/abs/path/to/apps/myproject \
    -DEMU_LCD_W=1280 -DEMU_LCD_H=720 \
    -DEMU_PRELOAD="/abs/path/apps/myproject/images@/images" \
    -DEMU_APP_INCLUDE_DIRS="/abs/path/apps/myproject"
emmake make -j$(nproc)
```

### 3.3 跑起来

```bash
cd build-myproject && python3 -m http.server 8123
# 浏览器打开 http://localhost:8123/
```

> 必须走 http(s)（localhost 也可以）——pthread 需要 SharedArrayBuffer，shell 里的
> `coi-serviceworker.js` 会自动补 COOP/COEP 头。直接用 `file://` 打开会白屏。

---

## 4. 配置参数一览

| 参数 | 默认 | 作用 |
|---|---|---|
| `-DEMU_GENERIC_WEB=ON` | OFF | 用通用外壳替换 Cardputer 模拟器 |
| `-DEMU_APP_UI_DIR` | (必填) | 你的工程 ui 源码目录（含 `ui_init`） |
| `-DEMU_LCD_W` / `-DEMU_LCD_H` | 1280 / 720 | 显示分辨率 |
| `-DEMU_PRELOAD` | 空 | `"src@/dest;src2@/dest2"`，把资源嵌入 wasm 文件系统 |
| `-DEMU_APP_INCLUDE_DIRS` | 空 | 工程需要的额外 include 目录（`;` 分隔） |

### 4.1 一键脚本（推荐）

```bash
./scripts/build_generic_web.sh apps/myproject 1280 720 \
    --preload "apps/myproject/images@/images"
```

产物输出到 `apps/myproject/dist-web/`（可用 `--out <dir>` 改）。脚本自动找 emsdk
（`$EMSDK` 或 `~/emsdk`），每工程独立 build 目录互不干扰。

### 4.2 触摸 / 键盘

什么都不用配。鼠标、触屏、物理键盘全通：
- 触屏：SDL `FINGER*` → 归一化坐标 → LVGL 指针设备。
- 键盘：SDL 键事件 → LVGL `lv_sdl_keyboard` 驱动（文本框输入、方向键导航都行）。

### 4.3 资源（图片/字体/音频）

`ui_init()` 里 `lv_image_set_src(img, "A:/images/logo.png")` 这种运行时读文件的，
必须用 `-DEMU_PRELOAD` 把文件塞进 wasm 文件系统：

```bash
-DEMU_PRELOAD="apps/myproject/images@/images;apps/myproject/fonts@/fonts"
```

`"A:/images/logo.png"` 的 `A:` 前缀 LVGL 会忽略，实际读 `/images/logo.png`。

### 4.4 换 LVGL 版本

三种方式，任选：
1. 换子模块：`git -C lib/lvgl checkout <tag>`（如 `v9.2.0`）。
2. 指向你自己的 LVGL 源码：改 `CMakeLists.txt` 里 `add_subdirectory(lib/lvgl)` 的路径。
3. 把你的 LVGL 源码**打进你的工程目录**（用户当前偏好），在 `CMakeLists.txt` 顶部注释
   `add_subdirectory(lib/lvgl)`，改为你自己的构建（确保产出 `lvgl` 静态库 + 头文件）。

> 注意：工程 UI 代码用的 LVGL API 版本必须和编译进去的 LVGL 匹配。
> SquareLine 按 LVGL 8 生成的 `ui.c` 不能直接配 LVGL 9，需要 `lvgl_compat` 或重新生成。

### 4.5 剥离硬件依赖

SquareLine 工程一般 UI 部分是纯 LVGL 的，硬件相关都在它自己的 `main.cpp` 里——
**不要编译那个 main.cpp**，只编 `ui/` 目录。若 `ui/` 里有个别文件 `#include` 了硬件头，
把那几行（通常是一个文件里的几个函数）删掉或改成空实现即可。

---

## 5. 给 AI（新会话）的交接说明

让一个新 Claude 会话快速接手，给它下面这段（按你的情况改路径）：

```
在仓库 <repo 根目录> 里，我要把 LVGL 工程部署到网页。通用外壳已就绪：

- 通用 main:      src/main_web_generic.cpp（不要改，除非改输入）
- 通用 shell:     web/lvgl_shell.html（进度条 + 拦浏览器快捷键 + service worker）
- 构建入口:       CMakeLists.txt 里 EMU_GENERIC_WEB=ON 的 lvgl-web 目标
- 一键构建:       ./scripts/build_generic_web.sh <工程目录> <宽> <高> \
                    --preload "…" --include "…"
- 触摸/键盘:      已内置，工程只要提供 extern "C" void ui_init(void)

我的 LVGL 工程在: <路径>（分辨率 <WxH>，LVGL <版本>）
它运行时会读的资源: <列表>

请：1) 用脚本构建出 index.*；2) 起 http 服务；3) 用 playwright 无头浏览器点一下
     canvas 中央确认画面变化、Console 无报错；4) 把产物和命令贴给我。
注意：工程 ui 源码里不得 include 硬件 SDK；不要编译工程的 main.cpp。
```

**AI 需要准备的 3 样东西**（你的工程必须提供）：
1. `ui_init()` 入口（或让 AI 按 LVGL API 补一个）。
2. 分辨率、运行时资源清单（图片/字体路径）。
3. LVGL 版本号（决定要不要换 `lib/lvgl`）。

---

## 6. 网页交互原理（简述）

```
你的 ui_init() 建好界面
        │
LVGL timer（lv_timer_handler）每 16ms 驱动渲染 + 轮询输入设备
        │
   ┌────┴────────────────────┐
   │ flush_cb: LVGL RGB565 帧 │   main_web_generic.cpp
   │ → ARGB8888 → SDL 贴图    │
   └─────────────────────────┘
        │
   鼠标/触摸 → SDL 事件 → 共享坐标状态 → 指针 read_cb 喂给 LVGL
   键盘      → SDL 事件 → lv_sdl_keyboard 驱动
```

核心代码就一个文件：[src/main_web_generic.cpp](src/main_web_generic.cpp)（约 90 行）。
`emscripten_set_main_loop(main_loop, 0, 1)` 是浏览器版主循环，替代原生 `while(1)`。

---

## 7. Cardputer 模拟器（默认模式，未改动）

- 原生：`cmake .. && make`，产物 `build/cardputer-emu`。
- 网页（带键盘皮肤）：`emcmake cmake .. && emmake make`，产物 `build-web/index.*`。
- `EMU_GENERIC_WEB` 关闭（默认 OFF）即回到此模式。

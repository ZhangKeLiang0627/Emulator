# LVGL-Web-Emulator

把任意 LVGL 工程编译成静态网页，浏览器直接跑，触摸/鼠标/键盘开箱即用。

## 用法速查

| 目标 | 命令 |
|---|---|
| 跑演示工程(验证链路) | `./scripts/build_generic_web.sh app/demo 1280 720` 然后 `cd app/demo/dist-web && python3 -m http.server 8123` |
| 跑你的工程 | 见下方「部署你的工程」 |

---

## 部署你的工程

### 1. 工程放哪里

统一放 `vendor/<工程名>/`，例如 `vendor/myproject/`:

```
vendor/myproject/
├── ui.c / ui.cpp ...   # 含 ui_init()，递归全量编译
├── images/             # 运行时读的图片(可选)
└── fonts/              # 自定义字体(可选)
```

> `vendor/` 下的用户工程被 `.gitignore` 忽略，不会提交。

### 2. 工程必须满足

| 契约 | 要求 |
|---|---|
| ① `extern "C" void ui_init(void)` | 唯一入口，模拟器启动后只调它 |
| ② 只用 LVGL API | 不include硬件SDK，硬件逻辑剥离 |
| ③ 分辨率可指定 | 尺寸由构建参数传入，代码尽量别写死，写死也没关系 |

### 3. 构建 & 运行

```bash
./scripts/build_generic_web.sh vendor/myproject 800 480 --preload "vendor/myproject/images@/images"
cd vendor/myproject/dist-web && python3 -m http.server 8123
# 浏览器 http://localhost:8123/
```

脚本自动找 emsdk(`$EMSDK` / `~/emsdk` / `/opt/emsdk`)，产物拷到 `vendor/<名字>/dist-web/`。
`index.data` 只有 `--preload` 了资源才生成。

> 必须 http(s) 访问，`file://` 会白屏(pthread 需要 SharedArrayBuffer，页面自动补 COOP/COEP)。

### 4. 部署线上

`dist-web/` 就是整个网站，拷到任意静态托管：GitHub Pages / nginx / 对象存储 / 局域网。

---

## 运行时读文件(图片/字体/音频)

`ui_init()` 里 `lv_image_set_src(img, "A:/images/logo.png")` 这类**运行时读文件**必须 `--preload`，
否则读不到：

```bash
--preload "vendor/myproject/images@/images;vendor/myproject/fonts@/fonts"
```

---

## 工程没有 ui_init?

交给 AI 处理：让它按 CLAUDE.md 的方法定位入口并包一层，包装一个
`extern "C" void ui_init(void)` 调原来的接口。需要的话直接把你自定义的LVGL工程目录指给它。

---

## 手动构建 / 参数表

```bash
mkdir -p build-myproject && cd build-myproject
emcmake cmake .. \
    -DEMU_GENERIC_WEB=ON \
    -DEMU_APP_UI_DIR=/abs/path/vendor/myproject \
    -DEMU_LCD_W=800 -DEMU_LCD_H=480 \
    -DEMU_PRELOAD="/abs/path/vendor/myproject/images@/images" \
    -DEMU_APP_INCLUDE_DIRS="/abs/path/vendor/myproject"
emmake make -j$(nproc)
```

| 参数 | 默认 | 作用 |
|---|---|---|
| `-DEMU_GENERIC_WEB=ON` | OFF | 通用外壳(本 README)；OFF 回到 Cardputer 模拟器 |
| `-DEMU_APP_UI_DIR` | 必填 | 工程源码目录(含 `ui_init`) |
| `-DEMU_LCD_W/H` | 1280/720 | 分辨率 |
| `-DEMU_PRELOAD` | 空 | `"src@/dest;…"` 打包进 wasm 文件系统 |
| `-DEMU_APP_INCLUDE_DIRS` | 空 | 额外 include 目录(`;` 分隔) |

---

## 常见问题

| 问题 | 解法 |
|---|---|
| 白屏 | 用 `http://`，别用 `file://` |
| 资源读不到 | 加 `--preload`；`A:` 前缀会被忽略 |
| `ui_init` 带参数/签名不符 | 包一层 `extern "C" void ui_init(void)` |
| include 硬件 SDK | 硬件剥离；SquareLine 工程只编 `ui/`，硬件在它的 `main.cpp`，别编译那个文件 |
| 工程有 main 冲突 | 模拟器有自己的 main，别放进工程源码 |

---

## 给 AI 的交接说明

AI新会话直接看 [CLAUDE.md](CLAUDE.md) (:p

---

## 网页交互原理

一句话：你的 LVGL 界面被编译成 **WebAssembly** 装进浏览器标签页；一个 ~90 行的 C++ 外壳
([src/main_web_generic.cpp](src/main_web_generic.cpp)) 扮演**虚拟开发板**——屏幕、触摸、键盘——
LVGL 全程不知道自己身在浏览器。

### 心智模型：这是一块虚拟开发板

| 真实开发板 | 浏览器里的对应物 |
|---|---|
| LCD 屏 (LCD_W×LCD_H, RGB565) | `<canvas>` 元素 |
| 触摸屏 + 鼠标 | 浏览器的 touch / mouse 事件 |
| 物理键盘 | 浏览器 keydown → LVGL 键盘驱动 |
| 定时器(每 16ms 中断) | `requestAnimationFrame` 驱动的 `main_loop` |
| 板上跑的应用 | 你的 `ui_init()` 建的界面 |
| 图形框架 | LVGL(静态编译进 wasm) |
| 供电 | 静态服务器 + COOP/COEP |

LVGL 是为真实单片机写的，它只认识两样东西：

- **画布契约**：给我一块 `LCD_W×LCD_H` 的 RGB565 内存，屏脏了我会调你的 `flush_cb` 把像素交给你；
- **输入契约**：每个周期我会调 `read_cb` 问你「指针在哪、按没按」。

外壳的全部工作，就是把浏览器的现实「翻译」成这两个契约。

### 第一步：C 源码 → 网页(编译期)

`emcmake cmake` 驱动 clang(Emscripten 工具链)把 **LVGL 库 + 你的 ui 源码 + 外壳** 一起编成
一个 WebAssembly 文件 `index.wasm`。wasm 是浏览器沙箱里跑的二进制，速度接近原生。

产物三个文件：

- `index.wasm` — 编译后的 C/C++(含整个 LVGL 引擎)
- `index.js` — 胶水：加载 wasm、把 SDL 调用接到浏览器 API、把浏览器事件喂给 wasm
- `index.html` — 页面壳([web/lvgl_shell.html](web/lvgl_shell.html))：进度条、`<canvas>`、拦快捷键、加载 service worker

> 构建带 `-sUSE_SDL=2`，让 Emscripten 提供 SDL2 实现——SDL 在这里不是窗口库，
> 而是「把 canvas 当窗口、把浏览器事件包装成 SDL 事件」的适配层。
> 所以外壳代码能原封不动地跨桌面 / 网页两用。

### 第二步：启动(初始化)

页面加载 → 下载 wasm → `main()` 开始跑：

1. `SDL_CreateWindow(...)` — 在 canvas 上开一块 `LCD_W×LCD_H` 的画布(虚拟屏)；
2. `lv_init()` — 启动 LVGL；
3. 注册显示：建 display，把 `flush_cb` 交出去，配一块整屏大小的 RGB565 绘制缓冲；
4. 注册输入：建 pointer indev，把 `pointer_read_cb` 交出去；再 `lv_sdl_keyboard_create()` 建键盘驱动；
5. **`ui_init()` — 你的界面在这里建**(外壳唯一的契约)；
6. `emscripten_set_main_loop(main_loop, 0, 1)` — 把控制权交给浏览器，进入主循环。

### 第三步：主循环(每帧做什么)

原生程序是 `while(1)` 死循环——浏览器不行，主线程一卡住整个页面就死了。
`emscripten_set_main_loop` 是它的替代品：浏览器每帧(vsync)自动调一次 `main_loop`，调完就还回控制权。

`main_loop()` 每帧五件事，和真实单片机的中断循环一一对应：

```
① SDL_PollEvent      收一轮事件(鼠标/触摸/键盘) → 更新坐标状态 / 喂键盘驱动
② lv_tick_inc(16)    告诉 LVGL「过了 16ms」(喂它的内部时钟)
③ lv_timer_handler() LVGL 引擎：推进动画、跑定时器、重绘脏区 → 调 flush_cb
④ flush_cb           把 LVGL 画的 RGB565 像素转 ARGB8888，写进 g_buf
⑤ SDL_UpdateTexture  把 g_buf 整帧上传 GPU 纹理 → canvas 刷新
```

`lv_timer_handler()` 是 LVGL 的心脏——所有动画、定时器、命中检测、重绘都由它驱动。
外壳不需要知道界面里有什么，只要「每帧踢它一脚、把它吐的像素搬上屏」就够了。

### 像素怎么上屏(渲染链路)

LVGL 是给 LCD 屏写的，只认 **RGB565**(16 位色，嵌入式标配，省内存)。
浏览器/GPU 用 **ARGB8888**(32 位)。`flush_cb` 干的就是这份「翻译」：

```c
uint16_t c = src[y * w + x];                                  // RGB565
g_buf[(area->y1+y) * LCD_W + area->x1 + x] = 0xFF000000        // 不透明 alpha
    | (((c>>11 & 0x1F) << 3 | (c>>11 & 0x1F) >> 2) << 16)      // R: 5bit→8bit
    | (((c>>5  & 0x3F) << 2 | (c>>5  & 0x3F) >> 4) << 8)       // G: 6bit→8bit
    | ((c & 0x1F) << 3 | (c & 0x1F) >> 2);                     // B: 5bit→8bit
```

5 bit 扩 8 bit 的小技巧 `v<<3 | v>>2`：高位先左移，再用最高 2 位填充低位——
0→0、31→255，中间误差均匀，比查表省事。

随后 `SDL_UpdateTexture(g_tex, NULL, g_buf, LCD_W*4)` 把整帧拷给 GPU 纹理，
`SDL_RenderCopy + SDL_RenderPresent` 画出来。canvas 内部分辨率就是 `LCD_W×LCD_H`，
CSS 负责按比例缩放到窗口大小。

> 这里用**整屏单缓冲**，不做 LVGL 常用的局部刷新——网页端每次整帧上传纹理足够快，
> 代码注释就写着 "simple & fine for web"。

### 输入怎么喂给 LVGL(交互链路)

LVGL 的输入是**拉模式**——它不「等」事件，而是每个周期主动调 `read_cb` 问一次。
外壳的做法：浏览器事件随时来，先存进三个全局变量；LVGL 每帧来取一次。

```c
static int  g_px = 0, g_py = 0;   // 最近一次指针位置
static bool g_pressed = false;    // 最近一次按下状态

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    data->point.x = g_px; data->point.y = g_py;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
```

浏览器事件 → SDL 事件 → 更新这三个变量：

```
鼠标  mousemove/mousedown/up → SDL_MOUSEMOTION / SDL_MOUSEBUTTONDOWN/UP → g_px,g_py,g_pressed
触摸  touchmove/down/up      → SDL_FINGERMOTION / SDL_FINGERDOWN/UP (坐标归一化 0..1)
                                                          → ×LCD_W / ×LCD_H 还原像素坐标 → 同上
键盘  keydown/keyup          → SDL_KEYDOWN/KEYUP → lv_sdl_keyboard_handler(ev) → LVGL 键盘驱动
```

- **鼠标**坐标本来就是 canvas 像素坐标，直接存；
- **触摸**事件给的是 0..1 归一化坐标，乘上 `LCD_W/H` 还原(见 `handle_event` 的 `SDL_FINGER*` 分支)；
- **键盘**是唯一「推」的：所有 SDL 事件直接丢给 LVGL 自带的 `lv_sdl_keyboard_handler`，
  它把按键翻译成 `LV_EVENT_KEY` 发给当前焦点控件。这也是页面里要把
  Tab/方向键/空格/退格 `preventDefault` 的原因——否则焦点和滚动先被浏览器抢走，LVGL 收不到键。

拿到指针状态后，命中检测、按下/抬起、滚动、`LV_EVENT_CLICK` 全部由 LVGL 自己完成，
**和你在真机上跑没有任何区别**——这就是「开箱即用」的来源。

### 为什么必须 http + coi-serviceworker

构建带 `-pthread`，浏览器里对应 **SharedArrayBuffer**(多线程共享内存)，它被
COOP/COEP 头锁成跨源隔离特性——普通静态服务器不会发这两个头。

解法是页面开头加载的 [web/coi-serviceworker.js](web/coi-serviceworker.js)：
一个 service worker，拦截页面自己的所有请求，重新带上
`Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy` 再回给页面，wasm 才能开线程。

`file://` 协议根本不让注册 service worker → 白屏。所以必须 http(s)。

### 总结：图解

简单流程图：
```
用户在浏览器中打开 index.html
        ↓
shell.html 加载 index.js
        ↓
index.js 初始化 WASM 模块和内存
        ↓
WASM 运行 main_web.cpp
        ↓
SDL2 (Emscripten) 调用浏览器 Canvas API
        ↓
LVGL 在 Canvas 上绘制 GUI
        ↓
浏览器事件 → SDL → LVGL 处理
        ↓
模拟设备交互
```

调用关系图：
```
浏览器标签页
├── coi-serviceworker.js    补 COOP/COEP，解锁 SharedArrayBuffer(pthread)
└── <canvas>               = 那块「LCD」，内部分辨率 LCD_W×LCD_H，CSS 缩放适配
        │  浏览器每帧(requestAnimationFrame)调 main_loop()
        ▼
   main_loop()                          main_web_generic.cpp
   ├─ SDL_PollEvent ← 鼠标/触摸/键盘事件
   ├─ lv_tick_inc(16) + lv_timer_handler()   ← 踢 LVGL 引擎
   │      │  LVGL 内部：动画/定时器/命中检测/重绘
   │      ▼
   │  flush_cb: RGB565 → ARGB8888 → g_buf
   └─ SDL_UpdateTexture → RenderPresent → canvas 上屏
        │
        你看到的：一块 LCD_W×LCD_H 的 LCD，能点、能滑、能敲键盘
```

外壳的**全部**逻辑就是 [src/main_web_generic.cpp](src/main_web_generic.cpp) 这 ~90 行。
换工程你唯一要保证的契约：提供一个 `extern "C" void ui_init(void)`。

---

## 部署到 GitHub Pages

产物是纯静态文件，任何静态托管都能跑，GitHub Pages 也不例外。步骤：

### 1. 先构建

跑 §8 的命令把网页构建出来（产物在 `app/guiproc/dist-web/`）。

### 2. 一键推到 gh-pages 分支

```bash
./scripts/deploy_gh_pages.sh                # 默认 app/guiproc/dist-web → origin/gh-pages
# 或指定参数：./scripts/deploy_gh_pages.sh <dist-dir> <remote> <branch>
```

部署到子路径（比如想挂在 `/demo` 而不是根目录）时，用第 4 个参数指定子目录，分支名自定：

```bash
./scripts/deploy_gh_pages.sh build-generic-demo origin deploy/demo demo
# 产物会放进 deploy/demo 分支的 demo/ 子目录，Pages 从分支根服务 → URL 为 /<repo>/demo/
```

脚本会把静态文件 + `.nojekyll` 拷到目标分支的工作树并提交推送。


### 3. 在 GitHub 网页开启 Pages

仓库 → **Settings → Pages → Build and deployment → Source** 选
`Deploy from a branch`，分支选 `gh-pages`，目录 `/ (root)`，保存。

### 4. 打开网页

```
https://<owner>.github.io/<repo>/
https://<owner>.github.io/<repo>/demo/

# 即 https://ZhangKeLiang0627.github.io/LVGL-Web-Emulator/
# 或 https://ZhangKeLiang0627.github.io/LVGL-Web-Emulator/demo/
```

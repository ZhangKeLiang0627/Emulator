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

AI新会话直接看 [CLAUDE.md](CLAUDE.md)(:p

---

## 网页交互原理(简述)

```
你的 ui_init() 建好界面
        │
LVGL timer(lv_timer_handler)每 16ms 驱动渲染 + 轮询输入设备
        │
   ┌────┴────────────────────┐
   │ flush_cb: LVGL RGB565 帧 │   main_web_generic.cpp
   │ → ARGB8888 → SDL 贴图    │
   └─────────────────────────┘
        │
   鼠标/触摸 → SDL 事件 → 共享坐标状态 → 指针 read_cb 喂给 LVGL
   键盘      → SDL 事件 → lv_sdl_keyboard 驱动
```

核心代码就一个文件 [src/main_web_generic.cpp](src/main_web_generic.cpp)。
`emscripten_set_main_loop(main_loop, 0, 1)` 是浏览器版主循环，替代原生 `while(1)`。

---

## 9. 部署到 GitHub Pages

产物是纯静态文件，任何静态托管都能跑，GitHub Pages 也不例外。步骤：

### 9.1 先构建

跑 §8 的命令把网页构建出来（产物在 `app/guiproc/dist-web/`）。

### 9.2 一键推到 gh-pages 分支

```bash
./scripts/deploy_gh_pages.sh                # 默认 app/guiproc/dist-web → origin/gh-pages
# 或指定参数：./scripts/deploy_gh_pages.sh <dist-dir> <remote> <branch>
```

部署到子路径（比如想挂在 `/demo` 而不是根目录）时，用第 4 个参数指定子目录，分支名自定：

```bash
./scripts/deploy_gh_pages.sh build-generic-demo origin deploy/demo demo
# 产物会放进 deploy/demo 分支的 demo/ 子目录，Pages 从分支根服务 → URL 为 /<repo>/demo/
```

脚本会把静态文件 + `.nojekyll` 拷到目标分支的工作树并提交推送——


### 9.3 在 GitHub 网页开启 Pages

仓库 → **Settings → Pages → Build and deployment → Source** 选
`Deploy from a branch`，分支选 `gh-pages`，目录 `/ (root)`，保存。

### 9.4 打开

```
https://<owner>.github.io/<repo>/
https://<owner>.github.io/<repo>/demo/

# 即 https://ZhangKeLiang0627.github.io/LVGL-Web-Emulator/
# 或 https://ZhangKeLiang0627.github.io/LVGL-Web-Emulator/demo/
```

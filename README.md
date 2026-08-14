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
>
> ⚠️ 不要放工程自己的 `main()` / `main.cpp`——模拟器有自己的 main。

### 2. 工程必须满足 3 个契约

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

交给 AI 处理：让它按 CLAUDE.md 的方法定位入口并包一层——签名不符（带参数）也一样，包一个
`extern "C" void ui_init(void)` 调原来的。需要的话直接把这个工程目录指给它。

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

新会话直接看根目录 [CLAUDE.md](CLAUDE.md)(自动加载)，里面是架构 + 契约 + 构建命令 + 坑。
也可以把这一段贴给 AI：

```
通用 LVGL→网页 模拟器仓库。我要部署 LVGL 工程到网页。
- 外壳: src/main_web_generic.cpp(别改);页面壳: web/lvgl_shell.html
- 一键构建: ./scripts/build_generic_web.sh <工程目录> <宽> <高> [--preload "…"]
- 工程只需提供 extern "C" void ui_init(void);触摸/键盘已内置
我的工程: <路径>(分辨率 <WxH>， LVGL <版本>)， 运行时读: <资源清单>
请: 构建出 index.* → 起 http 服务 → 无头浏览器验证画面正常、Console 无报错 → 贴产物和命令。
注意: 工程别 include 硬件 SDK;别编译它的 main.cpp。
```

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

核心代码就一个文件 [src/main_web_generic.cpp](src/main_web_generic.cpp)(约 90 行)。
`emscripten_set_main_loop(main_loop, 0, 1)` 是浏览器版主循环，替代原生 `while(1)`。

---

## Cardputer 模拟器(默认模式)

`EMU_GENERIC_WEB` 默认 OFF，构建仓库原有的 M5CardputerZero 模拟器：`cmake .. && make`。
与本 README 无关。

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
**不污染 main 分支历史**（52MB 的 index.data 只存在于 gh-pages 分支）。重复执行是幂等的：
文件没变化就不产生新 commit。

### 9.3 在 GitHub 网页开启 Pages

仓库 → **Settings → Pages → Build and deployment → Source** 选
`Deploy from a branch`，分支选 `gh-pages`，目录 `/ (root)`，保存。

### 9.4 打开

```
https://<owner>.github.io/<repo>/
# 本仓库即 https://ZhangKeLiang0627.github.io/LVGL-Web-Emulator/
# （仓库原名 Emulator 已重命名，旧地址会自动跳转）
```

### 注意

- **子路径没问题**：Pages 把网页挂在 `/repo/` 子路径下，本外壳的 wasm/data 都是相对路径加载，
  已在子路径下验证可正常启动和交互（§8 的冒烟脚本可直接指向子路径验证）。
- **首次打开会刷新一次**：pthread 需要 SharedArrayBuffer，GitHub Pages 默认不带 COOP/COEP 头，
  `coi-serviceworker.js` 会在首访后拦截请求补头并自动刷新一次（控制台可见
  "Reloading page to make use of updated COOP/COEP Service Worker"），之后正常。
- **体积**：GitHub Pages 单文件上限 100MB，`index.data` 约 52MB（图片+精简字体），没问题。
- **换机器/手机**：用浏览器直接访问上面的 URL 即可，无需 git。

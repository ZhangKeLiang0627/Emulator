# LVGL-Web-Emulator — AI 交接说明

把任意 LVGL 工程编译成静态网页,浏览器跑,触摸/鼠标/键盘开箱即用。人话版见 [README.md](README.md)。

## 架构

- **`src/main_web_generic.cpp`**(≈90 行)是唯一外壳:SDL 窗口 → LVGL 显示 → `flush_cb` 把 RGB565
  帧转 ARGB8888 → 鼠标/触摸喂 LVGL pointer indev、键盘喂 LVGL SDL 键盘驱动。**别改**,除非改输入。
- **`CMakeLists.txt`**:`EMU_GENERIC_WEB=ON` → 构建 `lvgl-web` 目标,把 `-DEMU_APP_UI_DIR` 目录下
  所有 `*.c *.cpp`(递归)和 `lib/lvgl` 一起编译。
- **`web/lvgl_shell.html`**:页面壳(进度条 + 拦快捷键 + `coi-serviceworker.js` 补 COOP/COEP,
  让 pthread/SharedArrayBuffer 可用)。
- **`lib/lvgl`** 子模块,当前 v9.5.0。
- 工程统一放 `vendor/<名字>/`(被 gitignore,不提交)。

## 契约

```cpp
extern "C" void ui_init(void);   // 模拟器启动后只调它
```

- 禁止 include 硬件 SDK;硬件在工程自己的 `main.cpp` → **不要编译它**。
- 分辨率由构建参数传,代码别写死。

## 构建 + 跑

```bash
./scripts/build_generic_web.sh <工程目录> <宽> <高> [--preload "src@/dest;..."]
cd <工程目录>/dist-web && python3 -m http.server 8123
# http://localhost:8123/  (必须 http,file:// 白屏)
```

演示:`./scripts/build_generic_web.sh app/demo 1280 720`。改代码后必须重跑脚本(唯一构建入口)。

## 工程没有 ui_init 时:找入口 + 包装

1. 定位界面初始化:`grep -rn "ui_init\|lv_init\|lv_scr_load\|lv_screen_init\|lv_obj_create" <工程目录>`
   - 常见形态:SquareLine 生成的 `ui_init()`(C 函数)、带参的 `ui_init(config)`、
     某函数里 `lv_init()` 后建界面。
2. 在工程里加个 shim,包一层:

   ```cpp
   extern "C" void ui_init(void) {
       my_project_init(...);   // 调找到的入口,给默认参数
   }
   ```

3. 若入口里有硬件调用(串口/文件/传感器),删掉或空实现,只留纯 LVGL。
   SquareLine 工程一般只编 `ui/` 目录,硬件在它自己的 `main.cpp`。

## 坑

- 运行时读文件 → `--preload` 打包;`A:/xxx` 前缀被 LVGL 忽略,实际是 `/xxx`。
- 工程源码里不要有 `main()`。
- 没 preload 时产物没有 `index.data`,正常。

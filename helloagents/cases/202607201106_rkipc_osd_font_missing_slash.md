# RKIPC OSD 子集字库缺少 `/` 字符

## 结论

GB OSD 使用的 `simsun_cn_3000.ttf` 是 Droid Sans Fallback 子集，并非宋体。其 Unicode cmap 缺少 U+002F `/`，FreeType 加载该字符时落到空的 `.notdef` 字形，所以画面上表现为空格。

采用 Noto Serif CJK SC（思源宋体同源）的 GB2312 OSD 子集：保留完整可打印 ASCII、GB2312 简体字符和常用中文标点，共 7544 个 Unicode 映射，文件约 2.56 MB。字体内保留 SIL Open Font License 1.1 元数据。

## 证据

- `packaging/oem_ipc/usr/share/simsun_en.ttf`：26,364 bytes，103 个 Unicode 映射，包含 U+002F，但不包含 U+005C。
- `packaging/oem_ipc/usr/share/simsun_cn_3000.ttf`：473,292 bytes，2970 个 Unicode 映射，U+002F 和 U+005C 均缺失。
- `Middleware/libmpp/rkipc/src/rv1106_dual_ipc/video/video.c` 的 GB OSD 初始化原来只加载 `simsun_cn_3000.ttf`。
- `Middleware/libmpp/rkipc/common/osd/font_factory.c` 使用 `FT_Load_Char()` 按 Unicode 直接加载字形。
- `Middleware/libmpp/Include/freetype2/freetype/config/ftmodule.h` 启用了 `cff_driver_class`，支持新字体的 OpenType/CFF 轮廓。

## 处理

- 新增 `Middleware/libmpp/rkipc/common/osd/noto_serif_sc_gb2312.otf`。
- CMake 安装字体到目标 `share` 目录。
- GB OSD 初始化改为加载 `/oem/usr/share/noto_serif_sc_gb2312.otf`。
- 同步字体到被 Git 忽略的 `packaging/oem_ipc/usr/share/` 镜像目录，方便当前镜像直接打包。
- 后续板端反馈 `/` 仍缺失时复查发现，远端受版本控制的 `packaging.7z` 仍只有两个旧字体；实际 `build.sh` 只构建 `libmpp.a`，不会执行独立 RKIPC CMake 的字体安装规则。现改为在 `build.sh image()` 生成 OEM 镜像前，从受版本控制的字体源强制同步新字体到当前 `PACKAGING` 目录，源字体缺失或复制失败时终止打包。
- 同期检查发现旧 `Bin/dgiot` 仍硬编码 `/oem/usr/share/simsun_cn_3000.ttf`；只运行 `build.sh image` 会继续复用旧应用，因此必须先完整重编 Middleware 和 App，再生成镜像。

## 验证

- 新字体字符表包含 U+0020–U+007E，确认 `/` 与 `\` 均有非零 glyph ID。
- 新字体包含 7555 个 glyph、7544 个 Unicode 映射；相较旧中文子集增加 4574 个 Unicode 字符。
- `file` 识别为 OpenType，`fc-scan` 识别为 `Noto Serif CJK SC Regular`。
- 当前 `packaging/image/partition.ini` 和 `packaging/Makefile` 均配置 OEM 分区为 48 MiB；加入字体后的完整 OEM 目录经同参数 xz SquashFS 临时打包为 9074.86 KiB（8.86 MiB），容量余量充足。
- 使用与设备相同的 FreeType 加载和变换流程渲染 `2026/07/20`，两个 `/` 在 16/32/64 字号下分别有 31/80/248 个非透明像素，证明字体轮廓与绘制算法可正常输出斜杠。
- 临时假 OEM 目录集成测试确认 `build.sh image()` 在调用 `make` 前已复制字体，目标文件与源码 SHA-256 一致。
- 将交叉工具链 `bin` 加入 `PATH` 并设置绝对 `CROSS` 后完成正式全量编译与打包；新 `Bin/dgiot` 和镜像内应用只包含新字体路径，不再包含 `simsun_cn_3000.ttf`。最终 `packaging/image/oem.img` 为 11,141,120 bytes，完整 `raw.bin/linux.bin/upgrade.bin` 均成功输出。
- 目标板仍需在实际 OSD 上验证类似 `2026/07/20 A\B 中文，。℃` 的混合文本、字号和基线效果。

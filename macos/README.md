# macOS（Apple Silicon）原生打包：まどそふと Artemis 游戏

本目录把 openartemis 打成「一个游戏一个 .app」：app 里只有 arm64 运行时、图标和一份
`game.conf`（记录游戏目录在哪），游戏数据始终从原游戏目录只读加载。不用 Wine。

已打包并验证的两款：

| 游戏目录 | 启动器 | 挂载顺序（低 → 高） | App 名 |
|---|---|---|---|
| `ハミダシクリエイティブ` | `hamidashi_CHS.exe`（Enigma Virtual Box） | `root.pfs` + `.000–.003/.020` → EVB 解出的 `root.pfs.099` → EVB 解出的目录（`font/`、`movie/`） | 常轨脱离 Creative |
| `ハミダシクリエイティブ凸` | `hamidashitotsu_chs.exe` | `hamidashitotsu.pfs` + `.000/.001` → `hamidashitotsu_chs.pfs.025` | 灵感满溢的甜蜜创想凸 |

## 构建

需要 Xcode 命令行工具、Homebrew 的 `cmake ninja nasm pkgconf`，以及 vcpkg。

```bash
brew install cmake ninja nasm pkgconf
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

预设 `macos-arm64` 使用 `cmake/triplets/arm64-osx-14.cmake`：依赖全部静态链接，最低系统
macOS 14。产物在 `build/macos-arm64/src/app/`：

- `openartemis`：播放器，打进 app 的就是它；
- `openartemis_drive`：同一个宿主加脚本输入，只用于无人值守测试，不进 app。

产物只链接系统库，可以这样检查：

```bash
otool -L build/macos-arm64/src/app/openartemis   # 只应出现 /usr/lib 与 /System/Library
```

## 打包

EVB 启动器（第一款）需要 [evbunpack](https://github.com/mos9527/evbunpack)（Apache-2.0）
来解出虚拟文件。只有打包时用到，app 运行时不需要：

```bash
python3 -m venv ~/code/.venvs/evb && ~/code/.venvs/evb/bin/pip install evbunpack
```

打包并安装到 /Applications：

```bash
G="/Volumes/BIWIN NV 3500 1TB Media/まどそふと"
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ" \
    --evbunpack ~/code/.venvs/evb/bin/evbunpack --install
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ凸" --install
```

| 选项 | 默认 |
|---|---|
| `--game DIR` | 必填，游戏目录 |
| `--name` | EXE 版本信息里的产品名；这两款的 EXE 没有版本信息，于是用游戏自己的窗口标题（`list_windows_ja.tbl` 的 `gametitle`），再不行用目录名 |
| `--icon` | 启动器 EXE 里最大的图标，经 `sips`/`iconutil` 转成 icns；也可以给图片或 .icns |
| `--sign` | `auto`：钥匙串里有 Developer ID Application 就用它，否则用第一个签名身份，都没有就 ad-hoc（`-`） |
| `--install [DIR]` | 不给就只输出到 `build/distribution/`；给了不带值装到 `/Applications` |
| `--build` | `build/macos-arm64` |
| `--evbunpack` | `evbunpack`（PATH 里找） |

打包脚本会自动选启动器（有 `*_chs.exe` 用它，否则用最大的 exe），并加上它自己的
`<启动器名>.pfs.NNN` 补丁包。启动器若是 EVB 外置包，脚本只解一次，放到
`~/Library/Application Support/Madosoft Artemis/<游戏>/evb/`（第一款约 54 MB），
不写进游戏目录。打包前后都会检查二进制没有链接 /opt/homebrew 之类的外部库。

## 运行时行为

- 双击 app 直接进游戏。`Contents/Resources/game.conf` 里写着游戏和补丁的路径。
- 存档放在 `~/Library/Application Support/Madosoft Artemis/<游戏>/save/`，游戏目录不写任何东西。
- 游戏所在的盘没接上时，会弹「重试 / 退出」对话框。
- 键位：

| 按键 | 作用 |
|---|---|
| Esc | 交给游戏（关菜单、隐藏消息框）；不再退出程序 |
| ⌘1 … ⌘9、⌘0 | 等于 F1 … F9、F10（F1 语音、F2 自动、F3 跳过已读、F4 快存、F5 快读、F6 存档、F7 读档、F8 回想、F9 回到上一选项、F10 设置） |
| 按住 Ctrl | 强制快进，未读文本也跳 |
| ⌃⌘F | 切换全屏 |
| ⌘Q | 退出 |

Mac 键盘上 F 键本身也能用（可能需要按住 fn）。

## 迁移原来的存档

把 Windows 版的存档目录**复制**（不是移动）到 app 的存档目录，引擎会读入官方的
BOWX/BOWG 格式：

```bash
S=~/Library/Application\ Support/Madosoft\ Artemis
mkdir -p "$S/hamidashi/save/まどそふと/ハミダシクリエイティブ" "$S/hamidashitotsu/save/savedata_chs"
cp -p "$G/ハミダシクリエイティブ/savedata_cn/"*.dat "$S/hamidashi/save/まどそふと/ハミダシクリエイティブ/"
cp -p "$G/ハミダシクリエイティブ凸/savedata_chs/"*.dat "$S/hamidashitotsu/save/savedata_chs/"
```

存档子目录名由游戏脚本决定：第一款的中文启动器用 `まどそふと/ハミダシクリエイティブ`，
第二款用 `savedata_chs`。

## 无人值守测试

`openartemis_drive` 从 `OA_DRIVE_SCRIPT` 指定的文件逐行读命令：

- 等待：`wait N`（帧）、`waitms N`；
- 鼠标：`move X Y`、`click X Y`、`rclick X Y`、`clicks N EVERY X Y`、`wheel D`；
- 键盘：`key VK`、`hold VK` / `release VK`；
- 输出：`dump PATH`（存 PPM 帧）、`state`（打印 `OA_TXTDUMP` 等报告）；
- 窗口：`resize W H`、`fullscreen 0|1`、`quit`。

例子：

```bash
OA_AUDIO_MIXDIAG=1 OA_SAVE_ROOT=/tmp/save OA_DRIVE_SCRIPT=play.drv \
  build/macos-arm64/src/app/openartemis_drive \
  --overlay "$G/ハミダシクリエイティブ凸/hamidashitotsu_chs.pfs.025" \
  "$G/ハミダシクリエイティブ凸/hamidashitotsu.pfs"
```

`OA_AUDIO_MIXDIAG=1` 会把每次 SE/语音开始播放（`se play ... :vo/...`）和每秒的 BGM/SE
电平写进日志，用来在没有耳机的环境下证明声音在出。

## 测试范围（2026-11-09，macOS 27.0.1，Apple Silicon）

两款都做过：

- 启动、品牌 logo 影片、中文标题画面；
- 新游戏，几十句正文，中文正文、名字和选项无乱码、无缺字；
- 选项，存档，退出重开，读回同一句；鼠标点对话框「YES」与 Enter 都能确认；
- 存档、读档、回想（含滚轮）、设置画面；
- 自动模式、Ctrl 强制快进；
- 改窗口大小；设置里的「全屏 / 窗口」按钮把请求交给窗口，按钮状态跟着切换；
- 自动推进 10 分钟以上不崩溃；
- 日志证明语音、BGM、SE 都在出；
- 原存档复制后能读入；
- 装好的 app 二进制从终端启动，能读到 `game.conf` 和 Application Support 里的存档。

OP 影片两款都完整播放过。第二款在妃愛线开头，影片里带中文字幕。第一款在共通线约第三个选项前，影片里的歌词字幕是英文。

## 已知缺口

- **已读记录**：官方存档里的已读记录不会导入，引擎本身也不保存已读状态。因此 F3（跳过已读）会停在每句；要快进请按住 Ctrl。
- **第一款的 v1.1 修正**：日文启动器把 v1.1 内容虚拟成 `hamidashi.pfs.099`。中文启动器在 Windows 上也看不到它，app 与官方中文启动器挂载的内容一致。
- **第一款的旧补丁文件**：`hamidashi_cn.dll` 和 `root_cn.pfs` 来自 2022 年的旧补丁，现行的 `hamidashi_CHS.exe` 不引用它们，app 也不使用。
- **真实全屏动画**：测试时屏幕锁着，macOS 不执行全屏切换。已验证的是设置按钮发出请求、游戏读回状态；⌃⌘F 走同一条路径。窗口实际进出全屏需要在解锁的屏幕上确认。
- **⌘ 快捷键**：只做了代码检查，测试脚本绕过系统键盘直接注入按键，没有用真实按键试过。
- **按住 Ctrl 时点选项**：一直按住 Ctrl 时点击选项不生效，松开 Ctrl 再点即可。
- **快速连点后存档**：有一次在快速连点后立刻存档，存到了上一句；之后没有复现。
- **签名与公证**：app 用 Developer ID 签名，但没有公证。本机构建的 app 不带隔离标记，可以直接打开；拷到别的 Mac 需要右键打开。
- **外接盘权限**：app 第一次读外接盘时，macOS 会弹一次访问权限框，需要手动允许。

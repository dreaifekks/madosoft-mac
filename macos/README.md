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
G="/Volumes/<外接盘>/まどそふと"   # 两款游戏所在的目录
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
不写进游戏目录。启动器引用的 EVB 包不存在、或文件头不是 EVB 时，脚本报出包的路径并退出。
签名前，脚本用 `otool -L` 检查复制进 app 的播放器；只要链接了系统库以外的库（例如
/opt/homebrew 里的），就报错停止。

## 运行时行为

- 双击 app 直接进游戏。`Contents/Resources/game.conf` 里写着游戏和补丁的路径。
- 存档放在 `~/Library/Application Support/Madosoft Artemis/<游戏>/save/`，游戏目录不写任何东西。
  从终端直接运行 `openartemis` 且没设 `OA_SAVE_ROOT` 时，存档放在
  `~/Library/Application Support/Madosoft Artemis/<游戏目录名>/save/`。
- 游戏所在的盘没接上时，会弹「重试 / 退出」对话框。
- 点窗口的关闭按钮或按 ⌘Q，由游戏自己弹出「是否要退出游戏？」。选「是」，游戏先保存
  系统数据（已读记录、设置等）再退出；选「否」继续游戏。游戏里的确认框还开着、或者正在
  放影片时，这个提问要等确认框答完、影片放完或被跳过才出现；其间多按几次也只问一次。
- 注销和关机时同样会先弹这个确认。选「否」，app 继续运行，这次注销或关机也就取消了。
  真实的注销和关机还没测过。
- 已读记录由游戏脚本自己维护，存在全局存档 `saveg.dat` 里，不属于某个存档槽，重开后仍有效。
  F3（跳过已读）只跳读过的正文，停在第一句未读；设置里「允许跳过未读文本」（第二款叫
  「快进未读文本」）打开后才连未读一起跳。
- 键位：

| 按键 | 作用 |
|---|---|
| Esc | 交给游戏（关菜单、隐藏消息框）；不再退出程序 |
| ⌘S、⌘L | 等于 F4、F5：快速存档、快速读档 |
| ⌘1 … ⌘9、⌘0 | 等于 F1 … F9、F10（F1 语音、F2 自动、F3 跳过已读、F4 快存、F5 快读、F6 存档、F7 读档、F8 回看、F9 回到上一选项、F10 设置） |
| 按住 Ctrl | 强制快进，未读文本也跳 |
| ⌃⌘F | 切换全屏；按住不放不会反复切换 |
| ⌘Q | 退出，先经过游戏的确认框 |

Mac 键盘上 F 键本身也能用（可能需要按住 fn）。

## 迁移原来的存档

把 Windows 版的存档目录**复制**（不是移动）到 app 的存档目录，引擎会读入官方的
BOWX/BOWG 格式，`saveg.dat` 里的已读记录也一起读入：

```bash
S=~/Library/Application\ Support/Madosoft\ Artemis
mkdir -p "$S/hamidashi/save/まどそふと/ハミダシクリエイティブ" "$S/hamidashitotsu/save/savedata_chs"
cp -p "$G/ハミダシクリエイティブ/savedata_cn/"*.dat "$S/hamidashi/save/まどそふと/ハミダシクリエイティブ/"
cp -p "$G/ハミダシクリエイティブ凸/savedata_chs/"*.dat "$S/hamidashitotsu/save/savedata_chs/"
```

存档子目录名由游戏脚本决定：第一款的中文启动器用 `まどそふと/ハミダシクリエイティブ`，
第二款用 `savedata_chs`。

引擎第一次保存时会改写成自己的格式，原来的官方文件留作同目录下的 `<文件名>.official.bak`。
本机复制过来的两份 `saveg.dat` 里已读表是空的，所以没有已读记录可导入；用游戏目录里
「全CG存档」的 `saveg.dat` 副本验证过：导入后 F3 在第一款共通线开头连续跳了两分多钟，
在第二款妃愛篇从开头一直跳到 OP 影片。

## 无人值守测试

`openartemis_drive` 从 `OA_DRIVE_SCRIPT` 指定的文件逐行读命令：

- 等待：`wait N`（帧）、`waitms N`；
- 鼠标：`move X Y`、`click X Y`、`rclick X Y`、`clicks N EVERY X Y`、`wheel D`；
- 键盘：`key VK`、`hold VK` / `release VK`；
- 输出：`dump PATH`（存 PPM 帧）、`state`（打印 `OA_TXTDUMP` 等报告）；
- 窗口：`resize W H`、`fullscreen 0|1`（走宿主自己的全屏切换）、`close`（发出与关闭按钮、
  ⌘Q 相同的退出请求）、`quit`（立即停止）；
- 对话框：`dialog N [MS]`，之后游戏弹出的系统对话框都回答 N（1 是 / 确定，0 否；默认 0）；
  给了 MS 就先让对话框停 MS 毫秒再回答，模拟用户想一会儿。

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

两款都做过。测试宿主注入鼠标和按键，存帧核对画面：

- 启动、品牌 logo 影片、中文标题画面；
- OP 影片：第二款在妃愛篇开头，带中文字幕；第一款在共通线第二、三个选项之间，歌词字幕是英文；
- 新游戏，几十句正文，中文正文、名字和选项无乱码、无缺字；
- Ctrl 快进到第一个选项，点选项后剧情继续；
- 存档后退出重开，从标题读档，读回同一句；读档和快读的确认框用鼠标点「YES」；
- 快存、快读（F4、F5，即 ⌘S、⌘L 送出的键）；
- 读几十句后用关闭请求退出、重开，F3 跳过读过的正文，停在第一句未读；
- 导入「全CG存档」的已读记录后，F3 连续跳过已读正文；
- 回看（按钮和滚轮），设置的页签、开关按钮和滑块；
- 自动模式；
- 关闭请求：分别从正文、游戏的确认框、OP 影片三处发出，选「是」保存后退出，选「否」继续。
  确认框和影片期间的请求等确认框答完、影片放完才提问，连发两次只问一次，确认框的鼠标按钮照常可用；
- 对话框停 3 秒再回答：之后自动模式、标题画面的淡入和混音都按原节奏继续，不补算等待的时间；
- 不设 `OA_SAVE_ROOT` 时，存档写到 Application Support 下以游戏目录命名的目录，游戏目录没有新文件；
- 改窗口大小；设置里的「全屏 / 窗口」按钮把请求交给窗口，按钮状态跟着切换；
- 自动推进 10 分钟以上不崩溃；
- 日志证明语音、BGM、SE 都在出；
- 原存档复制后能读入；
- 装好的 app 二进制从终端启动，能读到 `game.conf` 和 Application Support 里的存档。

## 已知缺口

- **第一款的 v1.1 修正**：日文启动器把 v1.1 内容虚拟成 `hamidashi.pfs.099`。中文启动器在 Windows 上也看不到它，app 与官方中文启动器挂载的内容一致。
- **第一款的旧补丁文件**：`hamidashi_cn.dll` 和 `root_cn.pfs` 来自 2022 年的旧补丁，现行的 `hamidashi_CHS.exe` 不引用它们，app 也不使用。
- **真实全屏动画**：测试时屏幕锁着，macOS 不执行全屏切换。已验证的是设置按钮发出请求、游戏读回状态；⌃⌘F 和测试宿主的 `fullscreen` 命令调用同一个宿主全屏函数。窗口实际进出全屏需要在解锁的屏幕上确认。
- **⌘ 快捷键**：⌘ 键对应的 F 键动作已用测试宿主验证（F3、F4、F5 等）；⌘ 组合键转成 F 键这一步只做了代码检查，没有用真实键盘按过。
- **关闭确认框的外观**：测试宿主替用户回答对话框；app 里弹的是系统消息框（「是 / 否」），还没在解锁的屏幕上看过。
- **按住 Ctrl 时点选项**：一直按住 Ctrl 时点击选项不生效，松开 Ctrl 再点即可。
- **快速连点后存档**：有一次在快速连点后立刻存档，存到了上一句；之后没有复现。
- **签名与公证**：app 用 Developer ID 签名，但没有公证。本机构建的 app 不带隔离标记，可以直接打开；拷到别的 Mac 需要右键打开。
- **外接盘权限**：app 第一次读外接盘时，macOS 会弹一次访问权限框，需要手动允许。

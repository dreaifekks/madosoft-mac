# macOS（Apple Silicon）原生打包：まどそふと Artemis 游戏

本目录把 openartemis 打成「一个游戏一个 .app」：app 里只有 arm64 运行时、图标和一份
`game.conf`（记录游戏目录和要挂载的文件在哪），游戏数据始终从原游戏目录只读加载。不用 Wine。

默认打原版。加 `--cn` 时，再挂上游戏目录里的中文补丁，或者用 `--cn PATH` 指定一个补丁。
原版和补丁版的 app 名、bundle id、存档目录都不同，可以同时安装。

已打包并验证的两款：

| 游戏目录 | 版本 | 启动器 | 挂载顺序（低 → 高） | App 名 | 数据目录 |
|---|---|---|---|---|---|
| `ハミダシクリエイティブ` | 原版 | `hamidashi.exe`（只取图标，见「已知缺口」） | `root.pfs` + `.000–.003/.020` | ハミダシクリエイティブ | `hamidashi-ja` |
| | `--cn` | `hamidashi_CHS.exe`（Enigma Virtual Box） | `root.pfs` + 分卷 → EVB 解出的 `root.pfs.099` → EVB 解出的目录（`font/`、`movie/`） | 常轨脱离 Creative | `hamidashi` |
| `ハミダシクリエイティブ凸` | 原版 | `hamidashitotsu.exe` | `hamidashitotsu.pfs` + `.000/.001` | ハミダシクリエイティブ凸 | `hamidashitotsu-ja` |
| | `--cn` | `hamidashitotsu_chs.exe` | `hamidashitotsu.pfs` + `.000/.001` → `hamidashitotsu_chs.pfs.025` | 灵感满溢的甜蜜创想凸 | `hamidashitotsu` |

数据目录在 `~/Library/Application Support/Madosoft Artemis/` 下，bundle id 是
`com.dreaife.madosoft-artemis.<数据目录名>`。

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

```bash
G="/Volumes/<外接盘>/まどそふと"   # 游戏所在的目录

# 原版：只输出到 build/distribution/
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ"
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ凸"

# 加 --install 装到 /Applications
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ凸" --install

# 中文补丁版：在游戏目录里自动找补丁
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ" --cn \
    --evbunpack ~/code/.venvs/evb/bin/evbunpack --install
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ凸" --cn --install

# 指定补丁
python3 macos/package_macos.py --game "$G/ハミダシクリエイティブ凸" \
    --cn "$G/ハミダシクリエイティブ凸/hamidashitotsu_chs.pfs.025"
```

| 选项 | 默认 |
|---|---|
| `--game DIR` | 必填，游戏目录 |
| `--cn [PATH]` | 不写：打原版。只写 `--cn`：在游戏目录里找补丁。写了 PATH：用这个补丁，可以是启动器 exe、EVB 外部包、pfs 或目录 |
| `--name` | EXE 版本信息里的产品名。这两款的 EXE 没有版本信息，于是用游戏自己的窗口标题（`list_windows_ja.tbl` 的 `gametitle`，补丁版取补丁里的），再不行用目录名 |
| `--icon` | 启动器 EXE 里最大的图标，经 `sips`/`iconutil` 转成 icns；补丁是启动器时用补丁启动器的。也可以给图片或 .icns |
| `--sign` | `auto`：钥匙串里有 Developer ID Application 就用它，否则用第一个签名身份，都没有就 ad-hoc（`-`） |
| `--install [DIR]` | 不写就只输出到 `build/distribution/`；只写 `--install` 装到 `/Applications` |
| `--build` | `build/macos-arm64` |
| `--evbunpack` | `evbunpack`（PATH 里找） |

### 原版（默认）

- 启动器是游戏目录里文件名不含 cn、chs 的 exe 中最大的一个；`*.exe.backup` 不算 exe。
- 挂载基础包（`root.pfs` 或 `<启动器>.pfs`）和它的编号分卷，再挂启动器自己的 `<启动器>.pfs.NNN`。
  游戏目录里的散文件也能读到，和 Windows 上一样。
- 不挂 Enigma Virtual Box（EVB）映射进来的文件。这两款的官方 exe 都不用 EVB，目录里的 EVB exe
  都来自补丁。
- App 名取原版的窗口标题，图标取原版启动器。bundle id 和数据目录带 `-ja` 后缀。

### `--cn`

- 只写 `--cn` 时，先找文件名含 cn 或 chs 的 exe（有几个就取最大的），按这个启动器看到的文件挂载：
  它自己的 `<启动器>.pfs.NNN`，EVB 打进 exe 的文件，以及它引用的 EVB 外部包。
- 没有这样的 exe，就挂文件名含 cn 或 chs、文件头是 pf2、pf6 或 pf8 的 `.pfs` 和 `.pfs.NNN`。
  文件头不对的跳过。都没有就报错退出，并列出找过的名字。
- 第一款的目录里还留着 2022 年的旧补丁：`hamidashi_cn.dll` 和 `root_cn.pfs`。`root_cn.pfs` 其实是
  EVB 包，要配那个 dll 才能用，文件头不是 PFS，所以就算没有 `hamidashi_CHS.exe`，自动查找也不会挂它。
- `--cn PATH` 给的是 exe 时，按启动器处理。给的是 EVB 外部包时，解出后挂载；外部包按文件头识别，
  不看扩展名，第一款的 `root_cn.pfs` 就是一个外部包。给的是 pfs 或目录时，直接挂上。
- 补丁不是 exe 时，挂在基础包上面，替代原版启动器自己的包。
- bundle id 和数据目录不带后缀，和之前装的补丁版一致。重新打包不动已有存档。
- App 名取补丁里的窗口标题。补丁没改标题时（例如目录补丁），名字会和原版相同，脚本报错退出。
  这时用 `--name` 另起一个名字。

### EVB 和打包检查

- 补丁用到 EVB 时，需要 [evbunpack](https://github.com/mos9527/evbunpack)（Apache-2.0）来解出虚拟文件。
  第一款的 `--cn` 就需要它。只有打包时用到，app 运行时不需要：

  ```bash
  python3 -m venv ~/code/.venvs/evb && ~/code/.venvs/evb/bin/pip install evbunpack
  ```

- 脚本只解一次，放到 `~/Library/Application Support/Madosoft Artemis/<数据目录>/evb/`，第一款约
  215 MB，不写进游戏目录。补丁文件换了，或者换了补丁，会删掉重解。
- app 运行时读这个目录。要删掉重解的目录正被运行中的 app 读着，或者要替换的 app 正在运行时，
  脚本会停下，提示先退出游戏。是否在运行，按正在运行的 app 可执行文件路径判断。
- 启动器引用的 EVB 包不存在，或者文件头不是 EVB 时，脚本报出包的路径并退出。
- 签名前，脚本用 `otool -L` 检查复制进 app 的播放器。只要链接了系统库以外的库，例如
  `/opt/homebrew` 里的，就报错停止。

## 运行时行为

- 双击 app 直接进游戏。`Contents/Resources/game.conf` 里写着游戏和补丁的路径。
- 存档放在 `~/Library/Application Support/Madosoft Artemis/<数据目录>/save/`，游戏目录不写任何东西。
  从终端直接运行 `openartemis` 且没设 `OA_SAVE_ROOT` 时，存档放在
  `~/Library/Application Support/Madosoft Artemis/<游戏目录名>/save/`。
- 游戏所在的盘没接上时，会弹「重试 / 退出」对话框。
- 点窗口的关闭按钮或按 ⌘Q，由游戏自己弹出退出确认，原版是「ゲームを終了しますか？」。
  选「是」，游戏先保存系统数据（已读记录、设置等）再退出；选「否」继续游戏。
- 游戏里的确认框还开着、或者正在放影片时，退出确认要等确认框答完、影片放完或被跳过才出现；
  其间多按几次也只问一次。
- 注销和关机时同样会先弹这个确认。选「否」，app 继续运行，这次注销或关机也就取消了。
  真实的注销和关机还没测过。
- 已读记录由游戏脚本自己维护，存在全局存档 `saveg.dat` 里，不属于某个存档槽，重开后仍有效。
  F3（跳过已读）只跳读过的正文，停在第一句未读；设置里允许跳过未读文本的开关打开后，才连未读一起跳。
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

把 Windows 版的存档文件**复制**（不是移动）到 app 存档目录下的对应子目录。引擎会读入官方的
BOWX/BOWG 格式，`saveg.dat` 里的已读记录也一起读入。

子目录名由游戏的 `system.ini` 决定：

| 版本 | Windows 上的存档位置 | app 里的子目录 |
|---|---|---|
| 第一款原版 | `%APPDATA%\まどそふと\ハミダシクリエイティブ` | `hamidashi-ja/save/まどそふと/ハミダシクリエイティブ` |
| 第一款 `--cn` | 游戏目录里的 `savedata_cn` | `hamidashi/save/まどそふと/ハミダシクリエイティブ` |
| 第二款原版 | `%APPDATA%\まどそふと\ハミダシクリエイティブ凸` | `hamidashitotsu-ja/save/まどそふと/ハミダシクリエイティブ凸` |
| 第二款 `--cn` | 游戏目录里的 `savedata_chs` | `hamidashitotsu/save/savedata_chs` |

例如第二款原版：

```bash
S=~/Library/Application\ Support/Madosoft\ Artemis
mkdir -p "$S/hamidashitotsu-ja/save/まどそふと/ハミダシクリエイティブ凸"
cp -p <Windows 存档目录>/*.dat "$S/hamidashitotsu-ja/save/まどそふと/ハミダシクリエイティブ凸/"
```

引擎第一次保存时会改写成自己的格式，原来的官方文件留作同目录下的 `<文件名>.official.bak`。
用游戏目录里「全CG存档」的 `saveg.dat` 副本验证过补丁版的导入：导入后 F3 在第一款共通线开头
连续跳了两分多钟，在第二款妃愛篇从开头一直跳到 OP 影片。原版的存档导入没有单独测过。

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

例子（第二款原版；补丁版再加 `--overlay`，顺序同 `game.conf`）：

```bash
OA_AUDIO_MIXDIAG=1 OA_SAVE_ROOT=/tmp/save OA_DRIVE_SCRIPT=play.drv \
  build/macos-arm64/src/app/openartemis_drive "$G/ハミダシクリエイティブ凸/hamidashitotsu.pfs"
```

要按某个 app 的 `game.conf` 跑脚本，可以复制一份 app，把 `openartemis_drive` 放进副本的
`Contents/MacOS/openartemis`，再从终端运行它。

`OA_AUDIO_MIXDIAG=1` 会把每次 SE/语音开始播放（`se play ... :vo/...`）和每秒的 BGM/SE
电平写进日志，用来在没有耳机的环境下证明声音在出。

## 测试范围（macOS 27.0.1，Apple Silicon）

测试宿主注入鼠标和按键，存帧核对画面。

### 原版和打包（2026-11-09）

两款原版都测了：

- 从终端运行打好的 app 里的 `openartemis`（`OA_SAVE_ROOT` 指向 /tmp），看到品牌 logo、日文标题画面，
  日志里有标题 BGM；
- 把测试宿主放进同一个 app 的副本跑脚本：新游戏，日文正文和名字无乱码；
- Ctrl 快进到第一个选项，点选项后剧情继续；快存、快读回到同一句；
- 存到 1 号档后退出，重开后从标题读档，读回同一句；
- 退出确认是日文「ゲームを終了しますか？」；
- 日志里语音（`:vo/...`）和剧情 BGM 都在出。

打包脚本：

- `--cn` 自动查找时，两款生成的 `game.conf`、bundle id、数据目录和之前装的补丁版完全相同。
  已用 `--cn --install` 重装两个补丁版，签名仍是 Developer ID；
- `--cn PATH` 测了五种：启动器 exe（`hamidashi_CHS.exe`）、EVB 外部包（`HappyBirthday.MrXiao`、
  `root_cn.pfs`）、打包了文件的 EVB exe（`hamidashi.exe`）、pfs（`hamidashitotsu_chs.pfs.025`）、目录。
  `hamidashi_CHS.exe`、`HappyBirthday.MrXiao` 和 pfs 的结果与自动查找一致；
- 自动查找的回退：/tmp 下的假游戏目录里只有文件头是 EVB 的 `*_cn.pfs` 时，`--cn` 报错；
  换成文件头是 pf8 的 `*_cn.pfs`，就挂上了。这个包不带标题，所以加了 `--name`；
- 运行中检查：/tmp 下的 app 副本运行时，重解它读着的 EVB 目录、替换它，都会停下并提示先退出游戏；
- 出错的情况：/tmp 下没有补丁的假游戏目录用 `--cn` 时报错，并列出找过的名字；没有原版启动器、
  补丁类型不认识、路径不存在、补丁版名字和原版相同，都报错退出。

### 补丁版（第二轮）

以下在两款 `--cn` 版本上测过：

- 启动、品牌 logo 影片、标题画面；
- OP 影片：第二款在妃愛篇开头，带中文字幕；第一款在共通线第二、三个选项之间，歌词字幕是英文；
- 新游戏，几十句正文，正文、名字和选项无乱码、无缺字；
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

- **第一款原版没有 v1.1 修正**：官方 v1.1 更新是 `hamidashi.pfs.010`，来自官方的 v1.1 更新包。
  这个游戏目录里，它只在 `原版备份/` 下，所以原版 app 是 v1.0 加官方裸パッチ（`root.pfs.020`）。
  把 `hamidashi.pfs.010` 放到 `hamidashi.exe` 旁边，原版打包时会当作启动器自己的包挂上。
  用测试宿主单独挂它试过，能进游戏、正文正常；放进游戏目录后的打包没有测。
- **第一款的 `hamidashi.exe` 是补丁**：它是 2021 年「白井木学园汉化组」Chs1.1 补丁的 EVB exe，
  里面打包了中文的 `hamidashi.pfs.099` 和 OP 影片。未改动的原版 exe 在 `原版备份/hamidashi.exe`，
  不用 EVB。原版 app 只用 `hamidashi.exe` 的图标，图标和原版相同。`--cn hamidashi.exe` 可以打出这个旧补丁版。
- **第一款的旧补丁**：`hamidashi_cn.dll` 和 `root_cn.pfs` 来自 2022 年的旧补丁，`root_cn.pfs` 实际是 EVB
  外部包。`--cn root_cn.pfs` 能打出 app，只核对了挂载列表，没有进游戏验证，也不知道是否离不开那个 dll。
- **原版没测的部分**：OP 影片、回看、设置、自动模式、F3、关闭请求的各种时机，目前只在补丁版上测过。
- **真实全屏动画**：测试时屏幕锁着，macOS 不执行全屏切换。已验证的是设置按钮发出请求、游戏读回状态；⌃⌘F 和测试宿主的 `fullscreen` 命令调用同一个宿主全屏函数。窗口实际进出全屏需要在解锁的屏幕上确认。
- **⌘ 快捷键**：⌘ 键对应的 F 键动作已用测试宿主验证（F3、F4、F5 等）；⌘ 组合键转成 F 键这一步只做了代码检查，没有用真实键盘按过。
- **关闭确认框的外观**：测试宿主替用户回答对话框；app 里弹的是系统消息框（「是 / 否」），还没在解锁的屏幕上看过。
- **按住 Ctrl 时点选项**：一直按住 Ctrl 时点击选项不生效，松开 Ctrl 再点即可。
- **快速连点后存档**：有一次在快速连点后立刻存档，存到了上一句；之后没有复现。
- **签名与公证**：app 用 Developer ID 签名，但没有公证。本机构建的 app 不带隔离标记，可以直接打开；拷到别的 Mac 需要右键打开。
- **外接盘权限**：app 第一次读外接盘时，macOS 会弹一次访问权限框，需要手动允许。

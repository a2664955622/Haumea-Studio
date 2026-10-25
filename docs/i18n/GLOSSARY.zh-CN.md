# GLOSSARY.zh-CN.md — 中文（简体）术语表

本文件记录 Haumea Studio 简体中文（zh-CN）语言文件的翻译约定，供维护者审阅与后续维护参考。与 `docs/i18n/GLOSSARY.pt-BR.md` 同一思路：保证界面一致、可预测、对 SA-MP 中文玩家自然。

## 翻译原则

1. 面向 SA-MP / open.mp 中文玩家；界面文案简洁直接，按钮用 2–4 字动词。
2. 完整句子意译，但严格保留全部技术事实：数字、文件名、函数名、上限值、占位符。
3. 语言名不翻译：`Language-Name: 简体中文`。

## 保留英文的术语（不翻译）

- **平台/游戏术语**：SA-MP, open.mp, GTA, San Andreas, Pawn, streamer, gamemode,
  gang zone, pickup, checkpoint, race checkpoint, NPC, textdraw, 3D text label,
  map icon, workspace, prefab, LOD, IPL, IDE, DFF, TXD, COL, radar art,
  virtual world, gizmo, HUD, FOV, FPS, native。
- **文件名与路径**：config.json, artconfig.txt, NODES<id>.DAT, `.pwn`, `.inc`, `.rec`,
  `.ttf`, models/, scriptfiles/, npcmodes/recordings, modloader, artwork,
  Windows-1252, UTF-8。
- **Pawn 标识符**：CreateObject, RemoveBuildingForPlayer, AddStaticVehicle(Ex),
  SetPlayerVirtualWorld, CreatePlayerGangZone, AddSimpleModel, OnGameModeInit,
  OnNPCFinishMovePath, NPC_Create 等一切函数/宏名。
- **快捷键与鼠标**：Ctrl, Shift, Alt, Tab, Enter, Esc, Del, F1–F12, LMB, RMB, MMB。
- **产品/字体/专有名**：Haumea, Haumea Studio, Namaka, Discord, zenity, kdialog,
  Pricedown, Bank Gothic, Beckett, Inter, Liberation；载具名（Freight, Streak, Tram…）
  与帮派名（Ballas, Grove, Vagos…）；客户端版本号 0.3.7 / 0.3.7-R2 / 0.3.DL。
- **命令**：/fontsize, /pagesize, /hudscalefix。

## 核心术语对照（zh-CN 采用译名）

| English | 简体中文 |
| --- | --- |
| Object / Map Objects | 物体 / 地图物体 |
| Object Info | 物体信息 |
| Object Browser | 物体浏览器 |
| world object / custom model | 世界物体 / 自定义模型 |
| Tools / Preferences / Settings | 工具 / 首选项 / 设置 |
| Characters / Paths / Markers | 角色 / 路径 / 标记 |
| Text & audio / Model files | 文本与音频 / 模型文件 |
| Theme / Dark / Light (theme) | 主题 / 深色 / 浅色 |
| Performance / VSync / Distance fog | 性能 / 垂直同步 / 距离雾 |
| View / Log / Window / Panel | 视图 / 日志 / 窗口 / 面板 |
| Save / Save As / Open / Close | 保存 / 另存为 / 打开 / 关闭 |
| Delete / Cancel / Apply / Reset | 删除 / 取消 / 应用 / 重置 |
| Undo / Redo / Copy / Paste | 撤销 / 重做 / 复制 / 粘贴 |
| Duplicate / Select / Deselect | 复制副本 / 选择 / 取消选择 |
| Refresh / Find / Replace / Filter | 刷新 / 查找 / 替换 / 筛选 |
| Position / Rotation / Size / Scale | 位置 / 旋转 / 尺寸 / 缩放 |
| Camera / colour / Alpha | 相机 / 颜色 / 透明度 |
| Font / Font size | 字体 / 字号 |
| Texture / Material / Model / Skin | 贴图 / 材质 / 模型 / 皮肤 |
| Vehicle / Actor / Player | 载具 / 演员 / 玩家 |
| Server / Client / handle / slot | 服务器 / 客户端 / 句柄 / 槽位 |
| instance / group / grid / snap | 实例 / 组 / 网格 / 吸附 |
| Area / dynamic area / zone | 区域 / 动态区域 / 区域 |
| Map / world / interior | 地图 / 世界 / 内部 |
| limit（the limit of %d） | 上限（%d 的上限） |
| Import / Export / Preview | 导入 / 导出 / 预览 |
| Favourites / tag / preset | 收藏夹 / 标签 / 预设 |
| Drop to ground / ground | 落到地面 / 地面 |
| draw distance / stream distance | 绘制距离 / 流送距离 |
| recording / node | 录制 / 节点 |
| shortcut / command | 快捷键 / 命令 |
| health / armour / weapon | 生命值 / 护甲 / 武器 |
| Map check / Shown as | 地图检查 / 显示方式 |

## 风格细则

- **占位符**：`%d`、`%s`、`%.1f` 等与 msgid 顺序完全一致；不翻译占位符内部。
- **SA-MP 标记**：`~r~ ~g~ ~b~ ~y~ ~p~ ~w~ ~l~ ~h~ ~n~`、`{RRGGBB}`、`\n` 原样保留，
  换行位置与英文一致。
- **空格**：中英文/数字之间加一个半角空格（“%d 个物体”）；占位符与路径内部无空格。
- **标点**：行文用全角（，。：；！？、）；代码、路径、按键名用半角。
- **复数**：英文 "(s)" 标记在中文省略（"%d object(s)" → “%d 个物体”）。
- **单位**：m、m²、px、s、ms、km/h、FPS 原样保留。
- **游戏名词**：武器等用中文社区通用译名（Fist 拳头、Katana 武士刀、
  Rocket launcher 火箭筒、Desert Eagle 沙漠之鹰…）；无把握时保留英文。

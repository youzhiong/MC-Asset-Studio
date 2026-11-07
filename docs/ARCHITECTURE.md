# MC Asset Studio — 架构

## 目标

Windows 原生桌面程序，C++20 业务核心 + Qt Widgets 前端，面向 Minecraft Java Edition 资源包，优先材质工作流。

当前阶段已实现 **新建/打开项目、游戏目录/多实例扫描、模组检测与分类、参考资源回退、参考材质显示/导入、材质包编辑（pack.mcmeta / 模组支持 / 封面）、ZIP 与文件夹导入、高层 ZIP 标签、全量材质分类、材质墙、集合模式、方块/物品关联、mcmeta 动画、像素编辑与动画编辑、导出/覆盖原 ZIP**；3D 预览尚未实现。

## 分层

```text
MCStudio.exe (WIN32, x64)
  └─ mcstudio_ui         MainWindow / 文档与卡片墙 / 集合 / 检查器 / 首选项
       └─ mcstudio_core  环境扫描 / 模组解析 / 资源栈 / 项目 / ZIP / 导出
```

核心以 QtCore 为主，像素编辑使用 QtGui 的 `QImage`；UI 依赖 QtWidgets。`Qt6::CorePrivate` 的 ZIP API 被限制在 `ZipArchive` / `ExportService`。

### `mcstudio_core`

**项目与 ZIP**
- `AppPaths`：`data/`、`data/game/`、`%LOCALAPPDATA%` 回退
- `Project`：项目元数据、source/pack 索引、`origin` / `supportedNamespaces` / `contentFingerprint`、`reference.json` 加载/保存、`writeOutputFile` / `removeOutputFile`（带路径校验的 `pack/` 原子写入）
- `ProjectService`：新建空项目、文件夹导入复制、项目列表、pack.mcmeta 生成
- `ZipArchive` / `ZipImporter` / `ExportService`：ZIP 指纹、pack 根识别、解包、导出、原子覆盖

**游戏环境**
- `InstanceInfo`：`ModFileInfo` / `ResourcePackFileInfo` / `DirFingerprint` / `InstanceInfo` / `GameRootInfo` / `GameEnvironmentSnapshot`
- `GameEnvironment`：扫描 `.minecraft/versions`、读取版本 JSON、加载器推断、继承链与客户端 jar、根/实例 mods 与 resourcepacks 合并、`data/game/` 缓存
- `GameScanTask`：后台扫描，互斥状态 + UI `poll()` 桥接
- `GameSettings`：`settings.ini` 的 `[games]` / `[reference]`
- `PackFormat`：pack_format ↔ Java 版本表、版本比较

**模组与命名空间**
- `ModInfo` / `ModScanner`：只读 jar 中央目录与元数据条目；Forge TOML、Fabric/Quilt JSON、mcmod.info、MANIFEST；单 jar 多模组；按 path+size+mtime 缓存
- `ModCatalog`：namespace → 模组；`modId` 匹配 → jar 内 `assets/<ns>/` 反查 → 跨实例元数据补全 → 未找到；兼容资源占比判定
- `ModCatalogTask`：后台构建 catalog

**参考资源**
- `AssetProvider`：`ProjectAssetProvider` / `JarAssetProvider` / `AssetObjectsProvider`
- `AssetStack`：`pack/ → source/ → 参考 mod jar → 客户端 jar → assets/indexes + objects` 分层只读查找
- `ReferenceEnvironment`：参考实例匹配（项目覆盖 / 默认 / pack_format / supported_formats / 同小版本）与资源栈构建
- `TextureAssociation`：blockstates → models（逐级 parent）→ textures；lang 分层合并；只输出资源包真实存在的材质；记录 `modelSource`
- `AssociationTask`：后台模型关联构建，带进度/取消
- `AssociationCache`：`data/projects/<id>/association.json`，按 zip SHA-1（无 ZIP 时用内容指纹）+ 参考实例 + 回退开关失效

**材质分类**
- `AnimationMeta`：mcmeta 动画
- `TextureCategory`：全量材质目录分类
- `AssetNamespace`：`assets/<ns>/…` 解析

**像素编辑**
- `PixelEdit`：不依赖 Widgets 的像素操作；像素点/线/矩形/椭圆、连续或全图油漆桶、颜色容差、镜像点/矩形
- `TextureDocument`：单张材质的编辑状态；静态图或垂直帧条、物理帧与播放序列、逐帧时长、`frametime` / `interpolate`、80 步/128 MB 撤销历史、`pack/` 原子保存、保留 `.mcmeta` 未知字段

### `mcstudio_ui`

- `MainWindow`：ZIP 标签、侧边栏（模组下拉、参考实例、类型树）、新建/打开/最近项目、导入导出、首选项、诊断
- `DocumentArea`：固定标签（项目概览 / 材质墙）与可关闭文档标签；欢迎页仅无项目时显示
- `TextureBoardPage` / `TextureBoardView`：卡片墙、模组/分类/路径搜索过滤、动画、自由布局、缩放、参考材质层（蓝框/橙框）
- `CollectionWindow` / `CollectionCanvas`：类型 / 方块物品 / 模组 / 模组 + 方块物品分组；1:1 像素画布（16×=1 方块）；类型/模组多选组合筛选；参考材质蓝/橙边框
- `InspectorPanel`：预览、尺寸、来源、参考来源、关联组、缺失材质数
- `ProjectOverviewWidget`：来源/格式/文件数、`pack.mcmeta` 填表编辑（保留未知字段）、材质包编辑入口、导出
- `NewProjectDialog` / `OpenProjectDialog`：新建空包、项目列表/浏览/最近
- `PackEditorPage`：模组范围（当前实例/全部）、搜索、勾选创建 `pack/assets/<ns>/`、封面导入与保存
- `Theme`：10 套配色（曜石黑/纯白/海洋/森林/暖阳/紫罗兰/蓝白/薄荷/樱花/薰衣草）+ 跟随系统 + Windows 11 原生主题、语义颜色表、`theme.qss` 颜色替换、QPalette/字体、Qt 控件样式、运行时 `StyleChange` 重绘；主题/样式写入 `settings.ini` 的 `[appearance]`
- **Win11 原生主题**：`theme=win11` 时强制 `QStyleFactory::create("windows11")`，使用 `win11Palette()` 的 Windows 11 浅/深色板，只应用 `win11Stylesheet()`（容器/字体），不加载自定义控件 QSS，让 `qmodernwindowsstyle` 自行绘制按钮、下拉、复选、标签页等。
- `TextureEditorPage` / `PixelCanvas` / `PixelIcons`：材质编辑标签；工具栏、RGBA 调色板、选区、缩放/平移、撤销/重做、底部动画时间轴
- `CoverEditorDialog`：正方形裁剪、拖动/缩放/90° 旋转、64/128/256/512 快捷尺寸与 4096 上限
- `GameDirectoryPage`：多根目录、实例表、扫描进度/取消、参考策略
- `PreferencesDialog`：外观主题、游戏目录、资源与回退、缓存管理
- `ResourcePackImportDialog`：从实例 resourcepacks 导入
- `IconFactory` / 文本与图片文档页

## 像素编辑与动画

```text
材质墙双击
  → 项目内路径：TextureEditorPage(edit:<path>)
  → 参考路径：确认复制 PNG + .mcmeta 到 pack/ → 再打开
  → 编辑（PixelCanvas 直接修改 TextureDocument 的当前物理帧）
  → Ctrl+S：PNG + .mcmeta 原子写入 pack/ → 通知 MainWindow 刷新卡片/检查器
```

- 画布按整数倍缩放，最近邻采样；棋盘格、像素网格、对称线、选区虚线、洋葱皮都画在屏幕空间，不写回图像。
- 每次笔触/形状/填充/选区移动/动画操作在 `TextureDocument` 中形成一个撤销快照；保存后 `savedIndex` 更新，撤销回保存点会清除 dirty。
- 动画帧模型：`frames` 保存物理正方形帧，`playback` 保存播放顺序，`times` 保存逐帧时长；保存时重建垂直帧条。
- `.mcmeta` 的 `animation` 块保留未知字段；逐帧时长或非顺序播放写入 `frames` 数组；`interpolate` 只在开启时写入。
- 删除动画保留第一帧并裁掉其余帧，移除 `animation`；若源包曾有 `.mcmeta`，写入 `{}` 覆盖，避免源包动画重新生效。
- 参考材质只读：必须显式复制到 `pack/` 后才进入编辑器；参考 jar 永远不会被回写。
- 未保存保护：编辑器标签通过 `DocumentArea::beforeClose` 保存/放弃/取消；切换项目、关闭项目、退出时由 `MainWindow::confirmCloseTextureEditors` 统一确认。
- 会话恢复只重新打开普通文档（`documentPaths` 跳过 `edit:` 前缀），编辑器内容不持久化，避免恢复出过期的未保存状态。
- 右键“查看大图”仍打开原来的 `ImageDocumentWidget`；双击行为改为编辑器。

## 主题与外观

- `Theme::Colors` 是语义颜色表（window / base / panel / text / accent / danger / reference / pack / checker 等），自定义画布直接读取。
- `Theme::availableThemes()` 提供 10 套 `Info`：曜石黑、纯白、海洋、森林、暖阳、紫罗兰、蓝白、薄荷、樱花、薰衣草；深色主题按强调色混合背景，浅色主题使用固定浅色骨架 + 强调色。
- `theme=auto` 时 `Theme::resolveThemeId()` 用 `QStyleHints::colorScheme()` 选择曜石黑/纯白；`installSystemThemeWatcher()` 监听 `colorSchemeChanged`，`auto` 和 `win11` 都会重新应用。
- `theme=win11` 时 `QStyleFactory::create("windows11")` 获取 `qmodernwindowsstyle`；`win11Palette(dark)` 提供 Windows 11 浅/深色语义色，`win11Stylesheet()` 只设置容器/字体，不覆盖原生控件。
- `Theme::availableStyles()` 枚举 `QStyleFactory::keys()`，优先 Fusion、Windows 11（WinUI 风格）、Windows Vista/7、Windows 经典；非 Win11 主题的 `Theme::apply()` 先设置控件样式，再设置 QPalette/字体/令牌化 QSS。
- `Theme::apply()` 设置 Fusion、QPalette、字体，并把 `theme.qss` 中 43 个基准十六进制色替换为语义颜色；因此 QSS 不需要复制多份。
- 硬编码内联样式改为 `#PrimaryButton` / `#GhostButton` / `#DangerButton` / `#Badge` / `#VersionBadge` / `#IconPreview` / `#CoverPreview` / `#EditorToolColumn` / `#EditorSidePanel` / `#EditorTimeline` 等对象名，统一走 QSS。
- 自绘控件在 `changeEvent(StyleChange/PaletteChange)` 中重绘；`TextureBoardView` / `CollectionCanvas` 自定义绘制读取 `Theme::colors()`；`CollectionWindow` 在主题变化时重建分组。
- 首选项“外观”页只预览，点“应用/确定”时 `PreferencesDialog::save()` 写 `settings.ini`（`theme` / `style`）并调用 `Theme::apply()`；启动时 `main.cpp` 先读取 `GameSettings::theme` / `widgetStyle` 再建主窗口。

## 游戏环境扫描

```text
<root>/versions/<id>/
  <id>.json           clientVersion / assetIndex / libraries / inheritsFrom / jar
  <id>.jar            客户端 jar（或继承父版本）
  mods/*.jar          隔离实例 mods
  resourcepacks/*.zip 隔离实例资源包
<root>/mods/          共享 mods（与实例合并）
<root>/resourcepacks/ 共享资源包
```

- 加载器优先从 `libraries` 推断：`fmlloader:<mc>-<loader>`、`forge:<mc>-<loader>`、`neoforge`、`fabric-loader`、`quilt-loader`；`mainClass` 兜底。
- 客户端 jar 解析顺序：实例自身 jar → JSON `jar` 字段 → `inheritsFrom` 父版本 jar。
- 缓存失效：根路径、版本 JSON size/mtime、实例目录、mods/resourcepacks 目录指纹。
- 扫描在后台线程，UI 通过 120ms `poll()` 读取进度；支持取消。

## 模组与 namespace

- 元数据优先级：`neoforge.mods.toml` → `mods.toml` → `fabric.mod.json` → `quilt.mod.json` → `mcmod.info` → `MANIFEST.MF`；Fabric/Quilt 实例优先读对应格式。
- 最小 TOML reader 支持 `[[mods]]`、`[[dependencies.<id>]]`、多行三引号字符串与转义。
- 每个 jar 记录 `namespaces` 与 `namespaceCounts`；只有“主导命名空间 / 达到占比阈值 / 无元数据”的 jar 才可认领 namespace，避免兼容资源误判。
- 跨实例只补全模组元数据（`source=cross`），参考 assets 永远只来自当前选定实例。
- 内置 namespace：`minecraft`、`forge`、`neoforge`、`fabric`、`quilt`、`c`、`common`。
- mod 元数据缓存位于 `data/game/mods/`，键为 jar 路径 + size + mtime + 加载器。

## 参考资源与模型关联

```text
pack/ 覆盖层
  → source/
    → 参考实例 mod jar（仅 local 映射）
      → 客户端 jar
        → assets/indexes + assets/objects
```

- `AssetStack` 只读；`locate()` 返回来源层，用于诊断与 `modelSource`。
- `TextureAssociation` 对资源包出现的每个 namespace 取 blockstates/models 的并集；每个文件按栈解析，parent 链逐层回退。
- 纹理只在资源包文件索引中存在时进入关联结果；参考纹理由材质墙的可选参考层单独展示。
- lang 按 locale 合并所有层，资源包键优先；`item.*` 缺失时回退 `block.*`。
- 关联结果携带：
  - `modelSource = pack / reference / mixed`
  - `textureSource = pack`
  - `missingModels / missingTextures`
- 结果写入 `association.json`（项目目录，不进入 ZIP）；首次构建在后台线程，状态栏显示进度。
- 参考实例匹配优先级：项目 `reference.json` → 首选项默认 → pack_format 精确 → supported_formats → 同小版本；不匹配允许强制并显示黄色警告。

### 参考材质显示与导入

- `TextureBoardPage` 持有 `AssetStack` 副本和 `showReference` 状态；开启后枚举 `assets/**.png`，跳过 `Project::fileIndex()` 中已有的路径。
- 每个参考条目记录 `referenceOnly=true` 和来源层 label；材质墙用蓝框（包内）/橙框（参考）区分，检查器显示参考来源。
- 右键或检查器“导入到资源包”把该路径的 PNG 与 `.mcmeta` 写入 `pack/`；按命名空间批量导入走同一写入函数。
- 导入完成重新 `Project::scan()`、刷新分类计数和参考列表；参考材质不会自动进入导出，必须显式复制。
- 参考列表按需加载，使用进度窗口；常见完整实例会加载 5k–10k 张纹理。

### 集合模式

- 画布单位 = 材质像素：16× 材质占 1 方块，32× 占 2 方块；不再按目标分辨率归一化。
- 非 framed 分组使用 skyline 自动排版：按面积从大到小，放到当前最低可用位置；小图填充大图旁边的空隙，保留 4 单位间距。
- 每张图周围留 1 单位 padding，边框画在像素之外（0.35 单位），不遮挡材质。
- 蓝色边框 = 包内，橙色 = 参考未导入，选中绿色。
- 顶部“类型”“模组”为可打勾多选菜单；与参考开关、分组方式组合过滤。
- 方块/物品关联模式下，参考材质会追加到“参考材质 · 模组”分组。


## 缓存设计

```text
data/
  settings.ini
  projects/<projectId>/
    project.json
    reference.json
    association.json
    source/  pack/
  cache/
    sources/<zipSha1>.json
    sessions/<projectId>.json
    layouts/<projectId>.json
  game/
    roots/<rootHash>.json
    instances/<instanceKey>.json
    mods/<jarHash>.json
    scans/<rootHash>.log
```

- 2A 项目缓存不变：`source.json` 记录 ZIP 指纹与解包清单，索引在内存重建。
- 新建/文件夹项目没有 ZIP 指纹，`Project::scan()` 生成 `contentFingerprint`（路径 + 来源 + size + mtime）。
- 环境缓存按目录/文件指纹失效；模组缓存按 jar size+mtime；关联缓存按 zipSHA1 或 contentFingerprint + 参考实例 + 回退开关。
- 所有缓存都位于 `data/`，不写入资源包 ZIP；打包排除 `data/`。
- 缓存清理：首选项可清 `data/game` 或项目 `association.json`。

## 项目创建与打开

```text
新建项目
  → 生成 project id
  → data/projects/<id>/source/pack.mcmeta（+ 可选 pack.png）
  → project.json(origin=created)
  → openProject()

打开项目
  → 项目列表（data/projects/*/project.json）或浏览文件夹
  → 含 project.json：直接加载
  → 含 pack.mcmeta：ProjectService::importFolder 复制到新项目（origin=folder）
  → 最近项目写入 settings.ini project/recent

打开后
  → 隐藏“资源概览”，固定显示“项目概览”和“材质墙”
  → 启动参考匹配 / mod catalog / 模型关联
```

- `ProjectOverviewWidget` 的 `pack.mcmeta` 表单保留未知 `/features` `/filter` `/overlays` / `language` 字段。
- `PackEditorPage` 的模组支持集合写入 `project.json supportedNamespaces`；勾选创建空 `pack/assets/<ns>/`，取消时只删除空目录，不删文件；保存后主窗口重建 catalog/AssetStack，开启参考材质时自动加载新增命名空间。
- 封面编辑结果写入 `pack/pack.png`；新建/文件夹项目禁止覆盖原 ZIP。
- 导出时显式保留没有文件的空目录（例如刚勾选、尚未放入材质的 `assets/<ns>/`），普通 ZIP 的文件条目不变。

## 导入与导出

```text
选择 ZIP / 从实例 resourcepacks 选择 / 文件夹导入 / 新建空包
  → （ZIP）SHA-1 指纹 /（文件夹）复制目录
  → 识别 pack.mcmeta / 剥离包装目录
  → 写入 data/projects/<id>/source
  → 写 project.json（origin=zip/folder/created）
  → 打开材质墙 / 项目概览 / pack.mcmeta / pack.png
  → 后台：环境匹配 → mod catalog → AssetStack → 模型关联 → association.json
```

导出：

```text
source/ + pack/（pack 覆盖）
  → QZipWriter 写入 QBuffer
  → QSaveFile 原子提交
```

- 默认导出新 ZIP；“覆盖原 ZIP”使用同一路径原子替换。
- 新建 / 文件夹项目没有原始 ZIP，只允许“导出为新 ZIP”。
- 参考资源层、`reference.json` / `association.json` 不参与导出。

## 后台任务与进度

- `GameScanTask` / `ModCatalogTask` / `AssociationTask` 使用 `QThreadPool` + 互斥状态 + UI `poll()` 定时器。
- worker 不持有 QWidget；关闭项目/对话框时设置取消标志，结果丢弃。
- 状态栏右侧 `QProgressBar` 显示扫描/解析进度；完成或失败后隐藏。
- 关联解析期间检查器显示“解析中…”，完成后自动刷新；集合窗口显示 pending 状态并在结果到达时重建。

## 安全与性能

- 拒绝绝对路径、`..`、Windows ADS 冒号路径；ZIP 导入防 zip slip。
- 像素编辑/动画只写 `pack/`；`Project::writeOutputFile` 校验相对路径并用 `QSaveFile` 原子提交，`source/` 与参考 jar 不回写。
- 只解包用户选择编辑的资源包；客户端 jar / mod jar 只读中央目录与按需条目。
- 大 jar 不整包解压；模组元数据按 jar 缓存。
- 模型关联结果缓存，避免每次选中材质重复解析。
- “所有扫描到的实例”模组表按需读取 jar 元数据，600+ 个 jar 时显示进度窗口；结果按 `data/game/mods` 缓存。
- 已知性能点：`QZipReader::fileData` 线性查找条目，超大包首次关联仍约 20–25s（后台 + 进度）；后续可换自研中央目录读取或增量构建。

## 目标版本

- 首版：Java 1.20.1，`pack_format` 15
- 支持版本表覆盖 1.6.1 → 1.21.4；未知 pack_format 允许手动选择参考实例
- 版本差异集中在 `PackFormat` 与关联/语言回退逻辑

## 非目标（当前阶段）

- 3D 预览、原版对比
- 像素编辑器高级能力：图层、任意角度旋转、调色板导入
- 声音、数据包编辑
- 基岩版
- 多人协作、云端项目

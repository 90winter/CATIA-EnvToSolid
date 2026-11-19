# CATIA-EnvToSolid — CATIA V5R26 (B26) 包络体 / 实体转无参实体工具

基于 **CATIA V5-6R2016 (B26) 原生 CAA C++** 与 **Win32 图形界面**开发的批处理转换工具，用于将 `.CATPart` 中的带参实体、包络体（Volume）与封闭曲面（Closed Surface）批量转换为干净的独立无参实体（Datum Solid），同时保留对象名称及实体/面级图形属性。

---

## 核心功能与转换规则

程序会遍历输入 `.CATPart` 的结构树，**仅保留同时处于显示（`Show`，含完整父级链）且激活（`Active`）状态**的以下对象：

1. **实体几何体（Solid Body）**：消除草图、拉伸、倒圆角等建模历史，生成独立无参 `Solid`（Datum Solid）。
2. **包络体 / 体积特征（Volume）**：将几何图形集或混合体内的 `Volume` 转换为独立 Body 下的无参 `Solid`。
3. **封闭曲面（Closed Surface）**：识别闭合二维壳体（`CATShell::IsClosed()`），闭合为实体后生成独立无参 `Solid`。

其余对象（`NoShow` 隐藏对象、位于隐藏几何图形集内的对象、未激活特征、开放曲面、草图、点线面等辅助元素）全部舍弃，输出全新的干净 `.CATPart`：
- 每个转换对象对应一个独立几何体（Body），保留原名称；
- 保留 Body 级与面级（B-Rep Face）颜色、透明度、边线及图层属性。

---

## 目录结构

```text
c:\包络体转实体\
├── 包络体转实体工具.exe       # Win32 图形界面宿主程序（支持拖拽、批量队列与进度展示）
├── run_converter.bat          # 根目录命令行快捷入口
├── build.bat                  # CAA Worker 编译脚本（自动 subst 虚拟盘符规避中文路径）
├── build_ui.bat               # Win32 GUI 编译脚本（VS2012 x64）
├── Part1.CATPart              # 示例测试模型
├── dist\
│   ├── CAAEnvToSolid.exe      # 原生 B26 CAA 批处理 Worker
│   ├── netapi32.dll           # DSLS 许可运行库依赖
│   ├── run_converter.bat      # 批处理启动包装脚本
│   ├── run_converter.ps1      # 自动定位 CATSTART.exe 与 CATEnv 并通过 --argfile 传参
│   └── README.md              # 分发目录简明说明
├── src_ws\                    # CAA 工作区（CAAEnvToSolid.edu / CAAEnvToSolid.m）
└── src_ui\                    # Win32 GUI 源码（EnvToSolidUI.cpp / Runner.cpp / Runner.h）
```

---

## 使用方法

### 方式一：图形界面（推荐）
直接双击运行根目录下的 `包络体转实体工具.exe`：
1. 点击 **添加文件...** / **添加文件夹...** / **添加文件夹(递归)...** 或直接将 `.CATPart` / 文件夹拖入窗口；
2. 可选设置统一的**输出目录**（默认在各原文件同目录生成 `<原文件名>_solid.CATPart`；递归扫描搭配统一输出目录时会保留相对子目录结构）；
3. 点击 **开始转换**，支持随时取消或超时中止。

### 方式二：命令行批处理
通过 `run_converter.bat`（或 `dist\run_converter.bat`）调用：

```bat
:: 1. 单文件转换（省略输出路径时默认生成 <原文件名>_solid.CATPart）
run_converter.bat "C:\路径\Part1.CATPart" "C:\路径\Part1_solid.CATPart"

:: 2. 目录批量转换（支持 --recursive 递归子目录）
run_converter.bat "C:\输入目录" "C:\输出目录" --recursive

:: 3. 检查模型结构、可见性、体积与面级颜色属性
run_converter.bat --inspect "C:\路径\Part1.CATPart"

:: 4. 输出 UTF-8 格式 JSON 转换回执
run_converter.bat "C:\路径\Part1.CATPart" "C:\路径\Part1_solid.CATPart" --report "C:\路径\report.json"
```

> **环境配置**：若 CATIA 未安装在默认路径，可在 `dist\config.ini` 中手动指定：
> ```ini
> CATSTART_EXE=C:\Program Files\Dassault Systemes\B26\win_b64\code\bin\CATSTART.exe
> ENV_NAME=CATIA.V5-6R2016.B26
> ENV_DIR=C:\ProgramData\DassaultSystemes\CATEnv
> ```

---

## 编译构建（开发环境）

- **环境要求**：Windows 10/11 x64、Visual Studio 2012 (`v110` x64)、CATIA V5-6R2016 (B26) 及配套 CAA RADE (`tck_init` / `mkmk`)。
- **编译 CAA Worker**：
  ```bat
  build.bat
  ```
  脚本会自动映射临时虚拟盘符（规避 `mkmk` 对中文工作区路径的限制）、初始化 `V5_6R2016_B26` TCK 环境、执行 `mkGetPreq` + `mkmk -a -u` + `mkCreateRuntimeView`，并将产物与 `netapi32.dll` 复制到 `dist\`。
- **编译 Win32 GUI**：
  ```bat
  build_ui.bat
  ```
  生成根目录下的 `包络体转实体工具.exe`。

---

## 关键工程特性

- **进程隔离与可靠取消**：GUI 通过 `CATSTART.exe` + UTF-8 `--argfile` 启动独立 CAA Worker，并利用 `CREATE_SUSPENDED` + Windows Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) 管理子进程树，确保取消或超时不会残留后台进程。
- **多级几何转换兜底**：`SolidBody`、`Volume` 与 `ClosedSurface` 优先走 `CATCloneManager` / `CATCGMCreateCloseOperator` + `CATIDatumFactory::InstanciateDatum` + `CATIPrtProceduralView::InsertInProceduralView`，异常分支自动回退 `CATMmrInterPartCopy(AsResult, ALL_GP)` 或临时 Part `CreateCloseSurface`。
- **发布门控与回执校验**：整件最终 `Update()` 失败时拦截保存（退出码 `13`）；GUI 端强制校验 JSON 回执完整性及输出文件实际落盘。
- **面级颜色高效迁移**：对无显式面级覆盖的模型走 Fast-Path 跳过几何匹配；当特征更新导致拓扑指针变化时自动清空过期 `CATCloneManager` 映射并启用质心/包围盒/面积匹配回退。

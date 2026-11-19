# 包络体转实体项目审核建议

**审核备注：gpt sol6.2**  
**审核日期：2026-11-19**

本次复核了 Win32 GUI、B26 CAA Worker 和 [agron 审核报告](项目审核与优化建议_agron.md)。建议继续沿用 GUI 与 Worker 的分进程架构，优先防止输出覆盖、拦截更新失败的模型，并确保取消后 Worker 退出。agron 报告之外，本次新增两项发现：整件更新失败被忽略，以及回执缺失时界面仍可能显示成功。

## 优先修复

| 优先级 | 发现 | 审核建议 |
| --- | --- | --- |
| P0 | **统一输出目录有覆盖风险。** GUI 只用文件名生成目标路径。递归加入两个子目录中的同名 CATPart 时，两者指向同一输出。保存回退还显式允许覆盖现有文件。 | 启动前生成整批输入到输出的映射。递归导入保留相对目录，检查重复目标、已有文件及目标是否指向任务中的任一源文件。默认报告冲突并列出对应路径。依据 [Runner.cpp，第 257 行](src_ui/Runner.cpp#L257) 和 [PartConverter.cpp，第 1405 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/PartConverter.cpp#L1405)。 |
| P0 | **整件模型更新失败后仍会尝试保存。** 最终的 SafeSpecUpdate 返回值被丢弃。只要保存成功且对象级失败数为零，文件就会被标记为成功。 | 把整件更新设为发布条件。失败时返回更新诊断并保留已有输出。对进入回退分支的样件，重开生成文件核对 Body 数、Datum Solid 类型、正体积和边界尺寸。依据 [PartConverter.cpp，第 2187 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/PartConverter.cpp#L2187)。 |
| P1 | **取消和超时只终止 CATSTART 进程句柄。** 源码没有等待 Worker 退出，随后便清理参数文件和回执。Worker 是否实际残留需在 B26 上验证。 | 挂起创建 CATSTART，成功加入 Windows Job Object 后再恢复运行。取消或超时后等待 Job 内进程退出，再清理临时文件。分别验证启动中、转换中和写盘时取消。依据 [Runner.cpp，第 419 行](src_ui/Runner.cpp#L419)。 |
| P1 | **回执缺失时界面仍可能显示成功。** Worker 忽略 JSON 写入结果。回执为空且启动器退出码为 0 时，GUI 直接显示成功。 | GUI 发起的任务应要求有效且与本次输入、输出一致的回执，并确认本次输出已生成。回执缺失、损坏或写入失败时显示具体原因。保留部分成功状态，并明确说明是否生成了可用文件。依据 [main.cpp，第 450 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/main.cpp#L450) 和 [Runner.cpp，第 530 行](src_ui/Runner.cpp#L530)。 |

## 对 agron 其余建议的取舍

- **面颜色优先处理匹配歧义。** 当前代码为每个候选面预计算面积、重心和包围盒，然后两两比较。优先使用克隆映射或拓扑日志，再对仍未映射且需要转移属性的面做几何匹配。记录有歧义及未匹配的面。agron 示例中的对角线乘以 2% 会让 1 米大的面获得 20 mm 阈值，当前按最低分选面的逻辑可能把颜色贴到邻近面。性能收益以实测为准。依据 [VisPropHelper.cpp，第 414 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/VisPropHelper.cpp#L414)。
- **颜色默认值和图层继承先明确验收要求。** 本机 R26 CAADoc 与公开头文件均将 S_AUTOMATIC 定义为成功读到标准值。若目标是保留实际显示外观，先用 B26 样件验证 Volume、曲面和 Solid 的默认样式及父几何集继承行为。将默认值写成显式覆盖会改变输出模型后续的样式行为。依据 [VisPropHelper.cpp，第 152 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/VisPropHelper.cpp#L152)。
- **稳定性修复后评估会话复用。** GUI 当前逐件启动 Worker，CLI 已支持单会话目录批处理。先用同一批样件对比启动耗时、转换耗时和进程内存，再决定是否增加显式任务列表。复用会话时应支持逐件回执和失败后续跑。agron 报告中的提速百分比缺少项目实测依据。依据 [EnvToSolidUI.cpp，第 460 行](src_ui/EnvToSolidUI.cpp#L460) 和 [main.cpp，第 386 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/main.cpp#L386)。
- **同名 Body、特殊路径和构建清理后排。** 同名 Body 可保留源层级信息，并在回执记录最终名称。源文件当前使用只读方式打开。增加 ASCII 临时副本回退前，先复现具体读失败并验证外部引用的解析。ProductStructure 依赖和 /EHsc 调整应独立构建并验证运行。依据 [PartConverter.cpp，第 1851 行](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/src/PartConverter.cpp#L1851) 和 [Imakefile.mk](src_ws/CAAEnvToSolid.edu/CAAEnvToSolid.m/Imakefile.mk)。

## 验证范围

本次为静态审核，核对了本机 B26 CAADoc 和相关公开头文件。源码与样件未改动，未重新编译或运行 CATIA。Worker 残留、样式继承及性能收益仍需实机验证。

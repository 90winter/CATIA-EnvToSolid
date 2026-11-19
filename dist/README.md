# CAAEnvToSolid — CATIA V5R26 (B26) 包络体/实体转无参实体批处理工具

## 功能说明
将 `.CATPart` 中**处于显示（Show）且激活（Active）状态**的：
1. **实体几何体（Solid Body）**：将带建模过程（草图、拉伸、倒圆角等）的实体消除参数，生成独立无参实体（Datum Solid）；
2. **包络体 / 体积特征（Volume）**：将几何图形集或混合体中的 Volume 转换为独立无参实体；
3. **封闭曲面（Closed Surface）**：将封闭壳体曲面封闭并转换为独立无参实体。

其余对象（隐藏 `NoShow`、位于隐藏图形集内的对象、未激活特征、开放曲面、草图、点线面等辅助元素）全部舍弃；输出全新的干净 `.CATPart`，每个转换对象对应一个独立几何体（Body）并保留原名称、实体/面级颜色及透明度。

## 使用方法

### 1. 单文件转换
```bat
run_converter.bat "C:\路径\Part1.CATPart" "C:\路径\Part1_solid.CATPart"
```
若省略输出路径，默认在同目录生成 `<原文件名>_solid.CATPart`。

### 2. 目录批量转换
```bat
run_converter.bat "C:\输入目录" "C:\输出目录"
```
支持递归子目录：
```bat
run_converter.bat "C:\输入目录" "C:\输出目录" --recursive
```

### 3. 检查模型结构与颜色/体积属性
```bat
run_converter.bat --inspect "C:\路径\Part1.CATPart"
```

### 4. 可选参数
- `--report <json路径>`：输出 UTF-8 格式 JSON 转换回执。
- `config.ini`：若 CATIA 安装在非默认位置，可在 `dist\config.ini` 中手动配置 `CATSTART_EXE`、`ENV_NAME`、`ENV_DIR`。

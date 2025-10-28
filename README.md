# wcocr（wechat-ocr 改进版）

> 本项目基于 [swigger/wechat-ocr](https://github.com/swigger/wechat-ocr) 修改而来，仅供学习与研究使用，请勿用于商业用途。

## 简介
原项目以 DLL 方式调用，本版本修改为 **可执行文件（exe）**，方便命令行直接使用。  
保留原识别功能与模型加载逻辑。

## 使用方法
1. 克隆或下载本项目。  
2. 在编译设置中将输出类型从 **DLL** 改为 **EXE**。  
3. 编译后生成 `wcocr.exe`。

命令行调用示例：  
`wcocr.exe --img "C:\path\to\image.jpg"`

可选参数：  
`--img "图片路径"`

输出示例：  Left, Top, Right, Bottom 为int类型坐标
`{"text":"示例文字","bbox":[Left, Top, Right, Top, Right, Bottom, Left, Bottom]}`

## 注意
- 仅供学习研究，请勿商用。  
- 请保留原作者版权与项目链接。  
- 模型文件需放在程序可读取的位置。

## 致谢
原作者：[swigger/wechat-ocr](https://github.com/swigger/wechat-ocr)

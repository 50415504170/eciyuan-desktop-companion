# 构建工具与运行库

成品使用 MinGW-w64 / GCC 编译，并静态链接编译器提供的标准运行库。构建依赖和许可参考：

- MinGW-w64：https://www.mingw-w64.org/license/
- GCC Runtime Library Exception：https://www.gnu.org/licenses/gcc-exception-3.1.html
- GCC GPLv3：https://www.gnu.org/licenses/gpl-3.0.html

zlib 代理只转发函数名，不包含 zlib 实现。原版 `zlib1.dll` 必须来自使用者已有的鹅次元安装，不随本项目发布。

Windows 系统 API 与 PowerShell 使用系统自带组件；不随发布包复制。

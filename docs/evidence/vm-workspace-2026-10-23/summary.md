# VM独立工作区配置

用户要求VM新建独立工作区，生成文件不要堆在源码下。规范为$HOME/locker-workspace，src/build/runtime/logs/artifacts同级；各卡build/logs按卡号/批次隔离，service与simulator运行数据独立。

本轮只新增创建工具/配置与修改执行文本：prepare_vm_workspace.py仅允许Linux运行，检查工作区不在旧源码内、子目录不越界/不为symlink、不覆盖同名文件，使用mkdir保留已有内容，不复制/移动/删除源码。正式项目文档和必要归档证据仍保留Windows约定目录，VM大量原始构建/测试输出留在src之外。

实际连接：读取本机SSH config提供的192.0.2.11，用户确认VM已开机且IP相同；两次BatchMode/StrictHostKeyChecking SSH均连接22端口超时，未进入认证。Windows VMnet8本机地址192.168.30.1。未改变网络/防火墙/SSH配置，未读取凭据，未在guest创建任何目录；目录状态仍PENDING_VM_CONNECTION，不宣称成功。

已同步VM工作流、S1-01最终复验指令、阶段1命令、全45卡/180段、总手册/源指纹manifest、配置和执行资料包。原S1-01实现/测试/历史报告与状态未修改。

本地校验仅为Python语法、路径边界与文档/契约检查，不是Linux运行或VM mkdir测试。实际创建可在VM终端用文档提供mkdir命令完成；确认目录后再同步当前源快照并使用外部build/logs路径复验。

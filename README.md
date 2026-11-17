# kernel_xiaomi_odin_mew

小米 MIX 4（`odin` / SM8350）的 LineageOS 24.0（Android 15）自用内核，基于 [Inkypen79/kernel_xiaomi_odin](https://github.com/Inkypen79/kernel_xiaomi_odin)（Linux 5.4.302）构建。

> **纯个人自用，刷机风险自负，不提供任何担保或维护支持。**

## 集成特性（均内建）

* **[SukiSU-Nova](https://github.com/SukiSU-Nova/SukiSU-Nova)**：内核级 Root（`builtin-new`）
* **[SUSFS](https://gitlab.com/simonpunk/susfs4ksu)**：路径、挂载、kstat 等隐藏（v2.4.0）
* **[ReKernel-X](https://github.com/myflavor/ReKernel-X)**：冻结进程事件上报（v2.0-odin）
* **[NoMount](https://github.com/maxsteeel/nomount)**：VFS 层路径重定向（v2.3.0 / 协议 v23）
* **[ipset](https://ipset.netfilter.org/)**：内核态 IP 集合

## 刷入

在 Recovery 或内核管理器中直接刷入 GitHub Actions 生成的 `odin-ksu-<sha>.zip`（刷入前请务必备份原 `boot` 分区）。

## 许可证

遵循 Linux 内核 `GPL-2.0` 及各集成组件的原始开源许可证，详见 [COPYING](COPYING)。

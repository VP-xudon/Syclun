# Developer Certificate of Origin (DCO)

> **中文说明（非条款，仅帮助理解）**
>
> 本仓库接受外部贡献时采用 **DCO（开发者原创证书）** 而非 CLA。你只需在每个提交
> 信息末尾加一行 `Signed-off-by`，即表示你声明：这段代码确实是你写的（或你有权以
> 本项目许可证提交它），并且你同意它被公开、被永久记录。
>
> 签名方式：
>
> ```bash
> git commit -s                       # 签署当前提交（最常用）
> git commit --amend -s               # 补签最后一个提交
> git rebase --signoff origin/main    # 补签本分支上的全部提交
> ```
>
> 生成的提交信息形如：
>
> ```
> [interpreter][fix] keep constructor arguments in derived ClsProto
>
> ClsProto's copy constructor inherited Object's no-op `::`, so
> `-(std::Number(5) n)` silently produced 0.
>
> Signed-off-by: Your Name <you@example.com>
> ```
>
> `Signed-off-by` 里的姓名与邮箱**必须是真实信息**，且与 git 的 `user.name` /
> `user.email` 一致：
>
> ```bash
> git config user.name  "Your Name"
> git config user.email "you@example.com"
> ```
>
> 完整流程见 [`CONTRIBUTING.md`](./CONTRIBUTING.md) §10。
> 下列英文原文为**唯一正式条款**，中文说明仅供参考。

---

Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
Everyone is permitted to copy and distribute verbatim copies of this license
document, but changing it is not allowed.

Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I have the
    right to submit it under the open source license indicated in the file; or

(b) The contribution is based upon previous work that, to the best of my
    knowledge, is covered under an appropriate open source license and I have
    the right under that license to submit that work with modifications,
    whether created in whole or in part by me, under the same open source
    license (unless I am permitted to submit under a different license), as
    indicated in the file; or

(c) The contribution was provided directly to me by some other person who
    certified (a), (b) or (c) and I have not modified it.

(d) I understand and agree that this project and the contribution are public
    and that a record of the contribution (including all personal information
    I submit with it, including my sign-off) is maintained indefinitely and
    may be redistributed consistent with this project or the open source
    license(s) involved.

---

Official text / 官方原文：<https://developercertificate.org/>
The English text above is verbatim; the Chinese notes at the top are an
explanation only and have no legal effect.
以上英文为逐字原文；文首的中文说明仅为解释，不具法律效力。

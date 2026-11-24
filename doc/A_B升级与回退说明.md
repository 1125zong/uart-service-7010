# FMQL7020 A/B 固件升级与回退说明

本文整理当前工程中与固件升级、A/B 启动槽、U-Boot 回退逻辑相关的环境变量和代码职责。

---

## 1. 分区与启动槽映射

当前板卡使用 eMMC A/B 双槽方案：

| 启动槽 | kernel 分区 | kernel 文件 | rootfs 分区 | U-Boot 启动命令 | bootargs |
|---|---|---|---|---|---|
| A | `mmc 1:1` | `image_emmc.ub` | `/dev/mmcblk0p2` | `boot_a` | `bootargs_a` |
| B | `mmc 1:3` | `image_emmc.ub` | `/dev/mmcblk0p4` | `boot_b` | `bootargs_b` |

对应 U-Boot 命令：

```text
boot_a=echo "[UBOOT] Booting A: mmc 1:1 + rootfs_a"; mmc dev 1 && mmc rescan && fatload mmc 1:1 0x2000000 image_emmc.ub && setenv bootargs ${bootargs_a} && bootm 0x2000000

boot_b=echo "[UBOOT] Booting B: mmc 1:3 + rootfs_b"; mmc dev 1 && mmc rescan && fatload mmc 1:3 0x2000000 image_emmc.ub && setenv bootargs ${bootargs_b} && bootm 0x2000000
```

> 注意：U-Boot 加载的内核文件名是 `image_emmc.ub`。升级代码必须使用相同文件名，否则内核升级不会生效。

---

## 2. 当前环境变量快照

与升级相关的当前值：

```text
boot_slot=a
boot_attempts=3
boot_slot_prev=a
boot_ok=1
```

当前实际生效的核心状态变量是：

```text
boot_slot
boot_attempts
```

`boot_slot_prev` 和 `boot_ok` 是旧版回退逻辑遗留变量，当前简化版 U-Boot 逻辑不依赖它们。

---

## 3. 当前 U-Boot 启动流程

当前 `bootcmd`：

```text
bootcmd=run guard_log; run guard_check; run boot_select
```

执行顺序：

```text
bootcmd
  -> guard_log
  -> guard_check
       -> boot_attempts > 0 : guard_dec
       -> boot_attempts = 0 : guard_switch
                              -> guard_toggle
                              -> guard_reset
  -> boot_select
       -> boot_a 或 boot_b
```

### 3.1 `guard_log`

打印当前状态：

```text
guard_log=echo "[UBOOT] slot=${boot_slot} ok=${boot_ok} attempts=${boot_attempts}"
```

当前简化逻辑已经不使用 `boot_ok`，也可以改成：

```sh
setenv guard_log 'echo "[UBOOT] slot=${boot_slot} attempts=${boot_attempts}"'
```

### 3.2 `guard_check`

主判断逻辑：

```text
guard_check=if test ${boot_attempts} -gt 0; then run guard_dec; else run guard_switch; fi
```

含义：

- `boot_attempts > 0`：执行 `guard_dec`；
- `boot_attempts = 0`：执行 `guard_switch`，切换槽位。

### 3.3 `guard_dec`

消耗一次启动机会：

```text
guard_dec=setexpr boot_attempts ${boot_attempts} - 1; saveenv; echo "[UBOOT] trial boot, attempts left=${boot_attempts}"
```

每次进入 U-Boot 并执行 `bootcmd` 时，只要 `boot_attempts > 0`，就会减 1 并保存。

### 3.4 `guard_switch`

次数耗尽后执行切槽：

```text
guard_switch=echo "[UBOOT] attempts exhausted, switch to another slot"; run guard_toggle; run guard_reset
```

### 3.5 `guard_toggle`

在 A/B 槽之间切换：

```text
guard_toggle=if test ${boot_slot} = b; then setenv boot_slot a; else setenv boot_slot b; fi
```

### 3.6 `guard_reset`

重置启动次数并保存：

```text
guard_reset=setenv boot_attempts 3; saveenv; echo "[UBOOT] next slot=${boot_slot} attempts=${boot_attempts}"
```

### 3.7 `boot_select`

根据 `boot_slot` 选择启动槽：

```text
boot_select=echo "[UBOOT] boot_slot=${boot_slot}"; if test ${boot_slot} = b; then run boot_b; else run boot_a; fi
```

---

## 4. 回退规则

当前设计不再使用 `boot_ok`，只使用 `boot_attempts` 作为启动续约计数。

### 4.1 升级完成时

Linux 升级程序写：

```text
boot_attempts=3
boot_slot=目标槽
```

例如从 A 升级 B：

```text
boot_slot=b
boot_attempts=3
```

### 4.2 每次启动进入 U-Boot

U-Boot 先减一次：

```text
3 -> 2
2 -> 1
1 -> 0
```

然后继续从当前 `boot_slot` 启动。

### 4.3 应用启动成功

应用启动成功后写回：

```text
boot_attempts=3
```

这相当于“续约”。

代码位置：

```157:162:APP/core/app_init.c
    /* 业务启动成功，刷新启动计数，避免 U-Boot 连续失败切槽 */
    if (set_uboot_env("boot_attempts", "3") != 0)
    {
        printf("Warning: failed to refresh boot_attempts\n");
    }
    sync();
```

也就是说：

```text
启动成功 -> boot_attempts 恢复为 3
启动失败 -> 应用没写回 3，boot_attempts 继续递减
```

### 4.4 连续失败 3 次

假设：

```text
boot_slot=b
boot_attempts=3
```

连续失败过程：

```text
第 1 次进入 U-Boot：3 -> 2，从 B 启动，应用没刷新
第 2 次进入 U-Boot：2 -> 1，从 B 启动，应用没刷新
第 3 次进入 U-Boot：1 -> 0，从 B 启动，应用没刷新
第 4 次进入 U-Boot：看到 0，切到 A，boot_attempts 重置为 3，并立即从 A 启动
```

预期日志：

```text
[UBOOT] attempts exhausted, switch to another slot
[UBOOT] next slot=a attempts=3
[UBOOT] boot_slot=a
[UBOOT] Booting A: mmc 1:1 + rootfs_a
```

---

## 5. Linux 侧代码职责

### 5.1 升级完成时初始化回退状态

文件：`APP/core/app_update.c`

当前逻辑：

```text
boot_slot_prev = current
boot_attempts  = 3
boot_slot      = target
```

关键代码：

```1480:1497:APP/core/app_update.c
    if (set_uboot_env("boot_slot_prev", current_slot) != 0)
    {
        fprintf(stderr, "Failed to set boot_slot_prev\n");
        return ERROR;
    }

    if (set_uboot_env("boot_attempts", "3") != 0)
    {
        fprintf(stderr, "Failed to set boot_attempts\n");
        return ERROR;
    }

    /* 最后切换 boot_slot，保证试运行标志先于新槽生效 */
    if (set_uboot_env("boot_slot", target_slot) != 0)
    {
        fprintf(stderr, "Failed to set boot_slot\n");
        return ERROR;
    }
```

`boot_slot_prev` 当前 U-Boot 简化逻辑不读取，保留不影响使用。

### 5.2 应用成功启动后续约

文件：`APP/core/app_init.c`

应用任务创建成功、LED 任务启动后，写：

```text
boot_attempts=3
```

这表示当前槽启动成功。

### 5.3 环境变量写入实现

文件：`APP/core/app_update_env.c`

`set_uboot_env()` 底层执行 `fw_setenv`：

```345:364:APP/core/app_update_env.c
int set_uboot_env(const char *key, const char *value)
{
    char command[512] = {0};

    if (key == NULL || value == NULL || key[0] == '\0')
    {
        fprintf(stderr, "set_uboot_env: invalid argument\n");
        return -1;
    }

    snprintf(command, sizeof(command), "fw_setenv %s %s", key, value);
    printf("set_uboot_env cmd: %s\n", command);

    if (system(command) != 0)
    {
        fprintf(stderr, "set_uboot_env: failed to set %s=%s\n", key, value);
        return -1;
    }

    return 0;
}
```

因此 Linux 侧必须保证：

- `fw_setenv` 存在；
- `/etc/fw_env.config` 正确；
- 应用有权限写 U-Boot 环境变量。

---

## 6. U-Boot 环境变量整理

### 6.1 核心状态变量

| 变量 | 当前值 | 当前是否使用 | 说明 |
|---|---|---|---|
| `boot_slot` | `a` | 是 | 下次启动槽 |
| `boot_attempts` | `3` | 是 | 启动续约计数 |
| `boot_slot_prev` | `a` | 否 | 旧版回退变量 |
| `boot_ok` | `1` | 否 | 旧版成功标志 |

### 6.2 回退脚本变量

| 变量 | 当前是否使用 | 说明 |
|---|---|---|
| `bootcmd` | 是 | 总入口 |
| `guard_log` | 是 | 打印状态 |
| `guard_check` | 是 | 判断次数 |
| `guard_dec` | 是 | 次数减 1 |
| `guard_switch` | 是 | 次数耗尽后切槽 |
| `guard_toggle` | 是 | A/B 互切 |
| `guard_reset` | 是 | 重置次数 |
| `boot_select` | 是 | 选择 A/B 启动 |

### 6.3 槽位启动变量

| 变量 | 说明 |
|---|---|
| `boot_a` | 从 A 槽加载 kernel 并使用 `bootargs_a` |
| `boot_b` | 从 B 槽加载 kernel 并使用 `bootargs_b` |
| `bootargs_a` | A 区 rootfs 参数 |
| `bootargs_b` | B 区 rootfs 参数 |
| `bootargs` | 默认 bootargs，但会被 `boot_a/boot_b` 覆盖 |

### 6.4 遗留变量

| 变量 | 说明 |
|---|---|
| `guard_retry` | 旧版 `boot_ok` 逻辑使用，当前不用 |
| `guard_rollback` | 旧版回退逻辑使用，当前不用 |
| `tmp=2` | `setexpr` 测试残留 |

可清理：

```sh
setenv guard_retry
setenv guard_rollback
setenv tmp
saveenv
```

---

## 7. 推荐的 U-Boot 配置命令

如果环境丢失，可按下面重建。

### 7.1 状态变量

```sh
setenv boot_slot a
setenv boot_attempts 3
```

### 7.2 回退脚本

```sh
setenv guard_log 'echo "[UBOOT] slot=${boot_slot} attempts=${boot_attempts}"'
```

```sh
setenv guard_dec 'setexpr boot_attempts ${boot_attempts} - 1; saveenv; echo "[UBOOT] trial boot, attempts left=${boot_attempts}"'
```

```sh
setenv guard_toggle 'if test ${boot_slot} = b; then setenv boot_slot a; else setenv boot_slot b; fi'
```

```sh
setenv guard_reset 'setenv boot_attempts 3; saveenv; echo "[UBOOT] next slot=${boot_slot} attempts=${boot_attempts}"'
```

```sh
setenv guard_switch 'echo "[UBOOT] attempts exhausted, switch to another slot"; run guard_toggle; run guard_reset'
```

```sh
setenv guard_check 'if test ${boot_attempts} -gt 0; then run guard_dec; else run guard_switch; fi'
```

```sh
setenv boot_select 'echo "[UBOOT] boot_slot=${boot_slot}"; if test ${boot_slot} = b; then run boot_b; else run boot_a; fi'
```

```sh
setenv bootcmd 'run guard_log; run guard_check; run boot_select'
```

保存：

```sh
saveenv
```

---

## 8. 建议增加的保护

### 8.1 kernel panic 自动重启

当前 `bootargs_a`、`bootargs_b` 没有 `panic=5`。建议添加：

```sh
setenv bootargs_a 'console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw panic=5'
setenv bootargs_b 'console=ttyPS0,115200 earlycon root=/dev/mmcblk0p4 rootfstype=ext4 rootwait rw panic=5'
saveenv
```

这样 rootfs 挂载失败、kernel panic 时能自动重启，U-Boot 才能继续消耗 `boot_attempts`。

### 8.2 U-Boot 启动失败自动复位

建议把 `bootcmd` 改成：

```sh
setenv bootcmd 'run guard_log; run guard_check; run boot_select || reset'
saveenv
```

如果 `fatload` 或 `bootm` 失败，U-Boot 自动复位并重新进入回退判断。

---

## 9. 如何模拟启动失败

### 方法 1：启动过程中提前复位

适合验证连续失败回退：

```text
第 1 次：boot_attempts 3 -> 2，提前复位
第 2 次：boot_attempts 2 -> 1，提前复位
第 3 次：boot_attempts 1 -> 0，提前复位
第 4 次：进入 U-Boot 后切槽
```

### 方法 2：让应用无法续约

临时改名 `fw_setenv`：

```sh
P=$(which fw_setenv)
mv "$P" "$P.bak"
```

重启 3 次后恢复：

```sh
mv "$P.bak" "$P"
```

应用无法写回 `boot_attempts=3`，会打印：

```text
Warning: failed to refresh boot_attempts
```

### 方法 3：直接测试切槽

```sh
setenv boot_attempts 0
saveenv
reset
```

下一次进入 U-Boot 会直接切槽。

---

## 10. 验证命令

### U-Boot 下

```sh
printenv boot_slot boot_attempts
printenv bootcmd guard_check guard_dec guard_switch guard_toggle guard_reset
```

### Linux 下

```sh
fw_printenv boot_slot boot_attempts
```

应用正常启动后应看到：

```text
boot_attempts=3
```

### 正常情况下的 U-Boot 日志

```text
[UBOOT] slot=b attempts=3
[UBOOT] trial boot, attempts left=2
[UBOOT] boot_slot=b
[UBOOT] Booting B: mmc 1:3 + rootfs_b
```

应用成功续约后，下一次仍是：

```text
[UBOOT] slot=b attempts=3
```

不会累积到切槽。

---

## 11. 注意事项

1. **应用必须重新编译烧录**  
   因为 `boot_attempts=3` 的续约代码在 `APP/core/app_init.c` 中，旧程序不会续约。

2. **U-Boot 只能用 `setexpr` 的版本**  
   当前板卡已验证支持：
   ```sh
   setexpr tmp 3 - 1
   echo ${tmp}
   ```
   输出应为 `2`。

3. **`boot_ok` 当前不用**  
   虽然环境变量里还保留 `boot_ok=1`，但 U-Boot 回退逻辑已经不读取它。

4. **内核文件名必须一致**  
   U-Boot 加载 `image_emmc.ub`，升级代码也必须以这个文件名写入。

5. **`uEnv.txt` 可能覆盖环境变量**  
   当前环境中存在：
   ```text
   envboot=... env import -t ${load_addr} $filesize ...
   loadbootenv=load mmc 0:1 ${load_addr} ${bootenv}
   ```
   如果 `uEnv.txt` 覆盖了 `bootcmd` 或 `guard_*` 变量，会导致回退逻辑失效。

6. **PL 看门狗不能覆盖早期启动失败**  
   当前应用里的 PL 看门狗在应用启动后才初始化。  
   它主要保护应用运行阶段，不能保护 U-Boot、内核早期、rootfs 挂载失败。

7. **如果需要覆盖内核硬死机，需要 PS 硬件看门狗**  
   或至少配置 `panic=5` 和 `bootcmd ... || reset`。

---

## 12. 当前结论

当前简化方案：

```text
boot_attempts 作为启动续约计数
应用成功启动后写回 3
U-Boot 每次启动先减 1
减到 0 后切槽并重置为 3
```

该方案可以覆盖：

```text
应用没跑起来
应用启动后没写回 boot_attempts
连续多次启动失败
kernel/rootfs 失败后能自动重启的情况
```

不能完全覆盖：

```text
内核早期卡死且不复位
U-Boot 阶段卡死
没有 panic、没有 watchdog、没有 reset 的死机
```

如需覆盖这些场景，需要补充 PS 硬件看门狗或更完整的启动阶段保护。

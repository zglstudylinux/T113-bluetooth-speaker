# RTL8723DS 蓝牙"好的"固件集（实测可用版）

本目录存放本项目实测验证通过的 RTL8723DS 蓝牙模块全套固件/工具（从 Tina SDK
`T113-Tina5.0-V1.2` 中提取，md5 与板上运行的文件逐一核对一致）。
SDK 里同名文件存在多个版本（尤其 `rtk_hciattach` 有新旧两版，行为差异很大），
此目录固化"验证过的组合"，供恢复/换板/重烧后对照使用。

## 文件清单与板上位置

| 文件 | 大小 | md5 | 板上位置 | 来源 |
|---|---|---|---|---|
| `rtl8723d_fw` | 58800 B | `74c10313…` | `/lib/firmware/rtlbt/` | SDK `platform/allwinner/wireless/firmware/rtl8723ds/` |
| `rtl8723d_config` | 41 B | `c5eddd7e…` | `/lib/firmware/rtlbt/` | 同上 |
| `rtl8723dsh4_fw` | 59600 B | `cf394f69…` | `/lib/firmware/rtlbt/` | 同上（H4 变体，rtk_hciattach 按需选用） |
| `rtl8723dsh4_config` | 106 B | `62c05e28…` | `/lib/firmware/rtlbt/` | 同上 |
| `rtk_hciattach` | 100096 B | `ed6f599f…` | `/usr/sbin/` | SDK `platform/thirdparty/wireless/rtk_hciattach/prebuilt/` |

配套的 btmanager 库（`libbtmg.so` 285460B + `bt_test` 158268B，4.0.3 good 版）
在 `third_party/bt/`，`scripts/deploy.sh` 会自动推送到板子。

## 为什么固件要单独存一份

1. **SDK 内同名多版本**：`rtk_hciattach` 至少有 plow 2017 旧版（63352B，ACL MTU
   820，inquiry scan 不正常、手机搜不到）和更新版（**100096B，本目录这份**，含
   `rtk_parse_config_file`，ACL MTU **1021**，一切正常）。不固化 md5 很容易拿错。
2. **BT 固件本体**（`rtl8723d_fw` 58800B + `rtl8723d_config` 41B）SDK 版一直是好的，
   没换过——曾有"固件版本 trade-off"的误判，实际差异全在 rtk_hciattach 版本
   （详见 SDK 仓库 `docs/rtl8723ds-bluetooth-porting.md` 坑 21）。
3. **重烧固件后恢复**：板的 rootfs 重烧后这些文件按上表推回即可：
   ```bash
   adb push firmware/rtl8723ds/rtl8723d_fw      /lib/firmware/rtlbt/
   adb push firmware/rtl8723ds/rtl8723d_config   /lib/firmware/rtlbt/
   adb push firmware/rtl8723ds/rtl8723dsh4_fw    /lib/firmware/rtlbt/
   adb push firmware/rtl8723ds/rtl8723dsh4_config /lib/firmware/rtlbt/
   adb push firmware/rtl8723ds/rtk_hciattach     /usr/sbin/
   adb shell chmod +x /usr/sbin/rtk_hciattach
   adb shell /etc/bluetooth/bt_init.sh start   # 或由 btmanager 自动拉起
   ```

## 换固件/config 后的硬性要求（坑 18）

RTL8723DS 模块常供电、无复位/使能 GPIO，**软 `reboot` 不清模块 RAM 里的固件**。
换任何固件/config 后必须**拔 5V 电源彻底断电 15 秒**再上电，否则
`rtk_hciattach` 报 `H5 sync timed out`。

## 验证命令（板上）

```bash
adb shell hciconfig hci0          # 应 UP RUNNING PSCAN ISCAN，ACL MTU 1021:8
adb shell "hciconfig hci0 name"   # 应 ZGL_BT_SPEAKER（app 启动后）
adb shell "ps | grep hciattach"   # rtk_hciattach -n -s 115200 /dev/ttyS1 rtk_h5
adb shell "md5sum /usr/sbin/rtk_hciattach /lib/firmware/rtlbt/rtl8723d_fw"
```

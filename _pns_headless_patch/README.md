# Headless PNS 推挤布线 API — 补丁草案

目标：让外部 python 通过 KiCad 内置 Push-and-Shove 引擎（PNS::ROUTER /
LINE_PLACER / SHOVE）在指定层从 A 布到 B，自动推走挡路走线，无需 GUI。

本目录**不改动任何现有源文件**；除两个 `[需应用]` 挂接 diff 外全部为新增文件。

## 文件清单

| 文件 | 说明 |
|---|---|
| `pns_headless_api.h` | PNS_BRIDGE 单例 + PNS_HEADLESS_IFACE（安装到 `pcbnew/router/`） |
| `pns_headless_api.cpp` | 实现（同上） |
| `swig_pns_headless_draft.i` | SWIG 暴露层草案，**仅适用还有 pcbnew/swig 的 KiCad 9.x 树** `[需应用]` |
| `pybind11_pns_headless_draft.cpp` | master 树（SWIG 已删）的 pybind11 原型 `[需应用, experimental]` |
| `CMAKE_HOOKS.md` | CMake 挂接 diff + Windows/MSVC 编译前置条件 |

## 关键事实修正（对本任务侦察结论的更新）

1. **本树已删除 SWIG**：`pcbnew/swig/` 目录在 master 上不存在（python 通道改为
   外部 kicad-python + IPC API/protobuf）。因此 SWIG 片段按 KiCad 9.x 树撰写，
   master 另附 pybind11 原型。
2. **PNS_KICAD_IFACE_BASE 已高度 headless 化**（Display*/Hide* 虚空实现），
   但派生类 PNS_KICAD_IFACE 把它们全部重新实现为 view/tool 版本（崩溃点见下）。
3. **写回 BOARD 不能走 BOARD_COMMIT**：`BOARD_COMMIT::Push` 第一行就解引用
   `m_toolMgr->GetView()`（board_commit.cpp:220），headless 无 TOOL_MANAGER。
   改为 iface.AddItem 内直接 `BOARD::Add()`（board.cpp:1636 自动
   connectivity->Add，撤销栈由调用方后续按需处理）。

## 设计决策

1. **写回路径走 iface 而非手工 GetUpdatedItems 转换**：`ROUTER::CommitRouting()`
   → `m_iface->AddItem/UpdateItem/RemoveItem`（pns_router.cpp:914-964）。复用
   `PNS_KICAD_IFACE::createBoardItem()`（pns_kicad_iface.cpp:2817-2954，含 via
   层对/secondary drill/tenting 全字段逻辑），bridge 只负责"直接上板"而非重建。
2. **每 waypoint 段独立 Start→Move→FixRoute(forceFinish)→Commit 周期**：
   绕开 chained-placement/mouse-trail 状态机（pns_line_placer.cpp:1669-1701），
   语义与 `ROUTER::ContinueFromEnd`（pns_router.cpp:658-694）同构。失败 leg 用
   `StopRouting()` 整体丢弃，已提交 leg 保留（报错给出位置）。
3. **强制推挤 = 独立 ROUTING_SETTINGS + RM_Shove**，不读用户配置文件
   （NESTED_SETTINGS null parent 时 LoadFromFile 安全返回，nested_settings.cpp:45）；
   `m_shoveVias=true / m_fixAllSegments=true` 默认即开。
4. **起点 item 查找**：StartRouting 的 aItem 决定 net（LINE_PLACER::Start
   pns_line_placer.cpp:1331，null→orphaned net），故 bridge 先在 PNS world 里
   按网查最近 item（pad>via>线端点，半径可配，`ROUTER::QueryHoverItems` slop 版
   pns_router.cpp:201-219），找不到即报错。
5. **提交前验证 `Placer()->CurrentEnd()==target`**（对齐 GUI 判据
   pns_router.cpp:646），防半截线；`SetAllowPartialRoute(true)` 可放行。
6. **换层过孔**：`routeLeg(..., aViaAtEnd=true)` 用 GUI "V 键"语义
   （`ToggleViaPlacement()` + 重 Move，via-only 提交有专门路径
   pns_line_placer.cpp:1564-1581）；单层 Route() 签名暂不触发（aAllowVia 保留）。

## 风险清单（行号为本树 ac8691a6）

### A. 无 VIEW/无 TOOL 会崩或卡死的点（补丁已 override 规避）

| # | 位置 | 问题 |
|---|---|---|
| A1 | pns_kicad_iface.cpp:2547-2548 | `DisplayItem` 解引用 `m_tool->GetManager()->GetSettings()`；clearance≥0 恒成立→必崩 |
| A2 | pns_kicad_iface.cpp:2612（调用链 pns_line_placer.cpp:1497→:1922） | `DisplayRatline` 解引用 `m_view->GetPainter()`，**每次 Move 都触发**→必崩 |
| A3 | pns_kicad_iface.cpp:2659-2663（调用点 pns_router.cpp:826-827） | `HideItem` 解引用 `m_view` |
| A4 | pns_kicad_iface.cpp:2510-2511（调用点 pns_router.cpp:843,:1039） | `EraseView` 解引用 `m_view`（m_hiddenItems 非空时） |
| A5 | pns_kicad_iface.cpp:2973,:3026 | `Commit` 解引用 `m_tool` 并 new BOARD_COMMIT(m_tool) |
| A6 | pns_kicad_iface.cpp:3030-3032 | `GetUnits` 解引用 `m_tool`（syncZone 的 UNITS_PROVIDER 使用） |
| A7 | pns_kicad_iface.cpp:1963-1967 | `syncZone` keepout 三角化失效时 **KIDIALOG::ShowModal 模态弹窗**（headless 卡死；仅畸形 rule area 触发） |
| A8 | pns_kicad_iface.cpp:2207 | `IsAnyLayerVisible` null view 时返回 false（语义陷阱；仅 GUI 工具消费：pns_tool_base.cpp:153,:452） |
| A9 | board_commit.cpp:220 | `BOARD_COMMIT::Push` 解引用 `m_toolMgr->GetView()` → 禁走 commit 路径 |

### B. PNS 状态机陷阱

| # | 位置 | 问题 |
|---|---|---|
| B1 | pns_router.cpp:79 | `m_settings=nullptr`：必须 `LoadSettings()` 后才可碰 `Settings()`（首个消费者 isStartingPointRoutable :332） |
| B2 | pns_router.cpp:977-983 | `FixRoute` 的 `aForceCommit` **只作用于拖拽路径**；布线提交靠显式 `CommitRouting()` |
| B3 | pns_line_placer.cpp:1535-1547,:1551-1565 | 碰撞未清 / 空线且无 via → FixRoute 返回 false，必须 StopRouting 丢弃 |
| B4 | pns_line_placer.cpp:1331 | aStartItem=null → orphaned net；bridge 必须先找到同网 item |
| B5 | pns_line_placer.cpp:1292-1323 | `SetLayer` 在 chainedPlacement 时拒绝换层 → headless 用"每 leg 重启+via at end"模型 |
| B6 | pns_router.cpp:66 vs pns_item.cpp:172、pns_diff_pair.cpp:1234 | ROUTER ctor 抢占全局单例 `theRouter`；PNS 内部 `GetInstance()->Settings()` 无 settings 保护 → bridge 与 GUI ROUTER 并存时 theRouter 指向最后构造者 |
| B7 | pns_router.cpp:130-133 | `QueryHoverItems` 在 routing 中返回 placer 节点（含 LINE_T 头线）→ 端点查找须过滤 LINE_T |
| B8 | pns_router.cpp:330-472 | 起点合法性检查：NPTH pad、文本、rule-area 拒绝并写 FailureReason（:444）；`AllowDRCViolations=true` 可跳过（:332） |
| B9 | pns_kicad_iface.cpp:1178,:1216 + router_tool.cpp:1752 | `m_UseConnectedTrackWidth`/`m_TempOverrideTrackWidth` 影响线宽继承；headless 默认关闭无碍，但 bds 被外部改动后需 ResyncWorld |

### C. 编译依赖链（头文件）

pns_headless_api.cpp → pns_kicad_iface.h → pns_router.h → pns_node.h /
pns_routing_settings.h / pns_sizes_settings.h；另有 board.h、
board_design_settings.h、connectivity_data.h、geometry_utils.h(kimath)、
base_units.h。全部位于 pnsrouter 目标现有 include 目录内
（pcbnew/router/CMakeLists.txt:3-24），无需新增 include path。

## python 用例（KiCad 9.x + SWIG 路径）

```python
import pcbnew
board = pcbnew.LoadBoard("board.kicad_pcb")

br = pcbnew.PNS_BRIDGE_PY.Instance()
br.Attach( board )

br.SetTrackWidthMM( 0.2 )          # 0 = 跟随 netclass/默认规则
ok = br.RouteMM( "EN6_MCU",                       # net 名
                 (5.78, 18.1), (11.2, 26.95),     # from/to, mm
                 pcbnew.F_Cu, True,               # layer, allow_via
                 [] )                             # waypoints: [x1,y1,x2,y2,...]
if not ok:
    print( br.LastError() )

pcbnew.SaveBoard( board, "board_routed.kicad_pcb" )
br.Detach()
```

推挤行为：RM_Shove 下挡路走线/过孔被推开并一并提交（与 GUI 推挤一致）；
`SetAllowDrcViolations(True)` 可放开起点 DRC 限制（自动化排线兜底）。

## 已知边界 / 后续工作

- 单层 Route() 签名不触发过孔；多层 leg 序列 API（waypoint 带层）在
  routeLeg(aViaAtEnd) 已有机制，待暴露成公共重载。
- 撤销栈：headless 直改 BOARD 不产生 undo 记录（save 后即成事实）。
- 多个 Route() 之间若外部改板（挪器件/覆铜重灌），需先 `ResyncWorld()`。

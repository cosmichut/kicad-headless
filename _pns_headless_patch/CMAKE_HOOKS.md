# CMake 挂接说明（[需应用]）

本补丁核心文件 `pns_headless_api.{h,cpp}` 设计为放进 `pcbnew/router/`，与 PNS
其余源码同编译单元。以下 diff 均为草稿，需手动应用（本补丁目录不直接改现有文件）。

## 1. 把 bridge 编进 pnsrouter 静态库（必做）

文件：`pcbnew/router/CMakeLists.txt`

```diff
 set( PCBNEW_PNS_SRCS
     time_limit.cpp
     pns_kicad_iface.cpp
+    pns_headless_api.cpp
     pns_algo_base.cpp
```

依据：`pnsrouter` STATIC 库由 `${PCBNEW_PNS_SRCS}` 构建
（pcbnew/router/CMakeLists.txt:96 `add_library( pnsrouter STATIC ${PCBNEW_PNS_SRCS} )`），
而 `pnsrouter` 已被 `pcbnew_kiface` 链接（pcbnew/CMakeLists.txt:814），
且 QA 库 `pnsrouter_qa` **不需要**加（它编 PNS 源的独立副本，避免符号冲突）。

## 2. KiCad 9.x 树（还有 SWIG 的分支）

- 按上节加入 pns_headless_api.cpp；
- 把 `swig_pns_headless_draft.i` 拷为 `pcbnew/swig/pns_headless.i`；
- 在 `pcbnew/swig/pcbnew.i` 的 board 相关 include 之后加一行：
  `%include "pns_headless.i"`
- 依赖：SWIG ≥ 4.0、Python 头文件（KiCad 9 的 pcbnew/CMakeLists.txt 里
  `add_library(pcbnew_python SHARED ...)` 一类目标会自动带上 pnsrouter）。

## 3. master 树（SWIG 已删除）

master 的正式 python 通道是 IPC API（`pcbnew/api/api_handler_pcb.cpp`，protobuf）。
两个可选路径：

### 3a. pybind11 独立模块（快速原型，experimental）

新建 `pcbnew/router/CMakeLists.txt` 追加段（示意）：

```cmake
if( KICAD_BUILD_PNS_PYBIND )   # 自定义开关
    find_package( pybind11 CONFIG REQUIRED )
    pybind11_add_module( pns_headless ${CMAKE_CURRENT_SOURCE_DIR}/pybind11_pns_headless_draft.cpp )
    target_include_directories( pns_headless PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} )
    target_link_libraries( pns_headless PRIVATE
        pnsrouter connectivity pcbcommon common gal core kicommon kiplatform
        ${wxWidgets_LIBRARIES} )
endif()
```

风险：与 `pcbnew_kiface` 的符号重复（Windows 上 DLL 边界严格，MSVC 需要一致的
`/MD` 运行库与 wx 编译选项）。原型可行，长期应走 3b。

### 3b. IPC API（upstream 方向）

在 `api/protos/pcb.proto` 暴露 `RouteRequest/RouteResponse`，在
`pcbnew/api/api_handler_pcb.cpp` 增加命令处理器，内部调
`PNS_BRIDGE::Instance()`。改动面大（protobuf schema + handler），本补丁只做
C++ 侧准备，schema 草案未包含。

## 4. 编译前置条件（Windows / MSVC）

来自本树 CMakeLists.txt / vcpkg.json（master 状态）：

| 依赖 | 版本/说明 |
|---|---|
| CMake | ≥ 3.28.1（CMakeLists.txt:22） |
| MSVC | VS 2022（17.x），x64，C++20 |
| wxWidgets | 3.2+（本树 vcpkg 锁 wxwidgets-33，即 3.3 分支）；`gl aui adv html core net base propgrid xml stc richtext webview` 组件（CMakeLists.txt:963） |
| Python | 解释器 ≥ 3.6（构建脚本用，CMakeLists.txt:952）；SWIG 路径还需 Python 开发头文件 |
| Protobuf | ≥ 3.15（CMakeLists.txt:886，IPC API 用） |
| OpenCascade | vcpkg（3D 可选功能仍被链接） |
| ngspice | vcpkg（`KICAD_SPICE_QA` 关闭可去） |
| 其余 vcpkg | gettext, sqlite3, libgit2, glm, curl, harfbuzz/freetype/pixman/cairo（经 wx/其他链） |
| Boost / ZLIB | 头文件与库（pcbnew 链接 `Boost::headers ZLIB::ZLIB`，pcbnew/CMakeLists.txt:778-784） |

vcpkg 清单模式（`vcpkg.json` + `vcpkg-configuration.json` 已在树根）下
`cmake --preset` + vcpkg 工具链可一次拉齐。

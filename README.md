# Tcmalloc

这是一个使用 CMake 管理的 C++ 项目，目录结构如下：

```text
Tcmalloc/
├── CMakeLists.txt
├── include/
├── src/
├── tests/
└── benchmark/
```

## 环境要求

- CMake 3.20 或更高版本
- 支持 C++17 的编译器，例如 GCC、Clang
- Git，用于 CMake `FetchContent` 拉取依赖

项目会在 CMake 配置阶段自动拉取以下依赖：

- glog
- GoogleTest

## 编译项目

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

如果需要重新生成构建目录，可以先删除旧的 `build/`：

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

## 运行单元测试

默认开启测试构建。`tests/` 目录下的每个 `.cc` 文件都会生成一个同名测试可执行文件，并注册到 CTest。

编译后执行：

```bash
ctest --test-dir build --output-on-failure
```

也可以直接运行某个测试程序，例如当前的 `tests/tests_main.cc` 会生成：

```bash
./build/tests_main
```

如果只想编译项目，不构建测试：

```bash
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
```

## 运行 Benchmark

`benchmark/` 目录下的每个 `.cc` 文件都会生成一个同名可执行文件。

例如添加：

```text
benchmark/bench_mark.cc
```

编译后运行：

```bash
./build/bench_mark
```

## 添加源码

- 头文件放到 `include/`，建议使用 `.hpp`
- 源文件放到 `src/`，使用 `.cc`
- 单元测试放到 `tests/`，使用 `.cc`
- 性能测试放到 `benchmark/`，使用 `.cc`

CMake 使用 `CONFIGURE_DEPENDS` 自动收集源码。新增文件后，如果构建系统没有自动识别，可以重新执行：

```bash
cmake -S . -B build
```

# Tcmalloc

## 环境要求

- CMake 3.20 或更高版本
- 支持 C++17 的编译器，例如 GCC、Clang
- Git，用于 CMake `FetchContent` 拉取依赖

项目会在 CMake 配置阶段自动拉取以下依赖：

- glog
- GoogleTest

## 运行测试

配置并编译：

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

运行全部测试：

```bash
ctest --test-dir build --output-on-failure
```

也可以直接运行测试可执行文件：

```bash
./build/tests_main
```

## 运行 Benchmark

编译后运行全部 benchmark 场景：

```bash
./build/bench_mark
```

只测试 `malloc/free`：

```bash
./build/bench_mark --allocator=malloc --iterations=20000
```

只测试 `tcmalloc/tcfree`：

```bash
./build/bench_mark --allocator=tcmalloc --iterations=20000
```

输出格式为 CSV：

```text
scenario,allocator,operations,elapsed_ms,ops_per_sec,rss_before,rss_middle,rss_after
```

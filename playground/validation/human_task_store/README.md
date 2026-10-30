# 人物任务与全局行为存储回归

构建后传入 `playground/sandbox/domino` 的临时副本绝对路径；测试会保存并修改该副本，不要传入仓库样例目录。

```bash
./scripts/build.sh playground-build playground/validation/human_task_store/main.c debug
test_dir=$(mktemp -d /tmp/domino-human-store.XXXXXX)
cp -a playground/sandbox/domino "$test_dir/world"
./build-debug/bin/domino-playground "$test_dir/world"
rm -rf -- "$test_dir"
```

覆盖行为数组扩容、ID 回绕避让、交换删除与人物清单顺序、255 项行为 ID 保存重载、执行版本重建、睡眠进度续跑，以及两条生命周期和销毁后资源复位。测试只在无业务线程的 Init 阶段注入 32 位毫秒测试时钟；最后一轮启动线程后不直接读取业务容器。

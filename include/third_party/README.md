# 第三方库

## pytorch

安装：
```bash
cd ./include/third_party
wget https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.8.0%2Bcpu.zip
unzip libtorch-shared-with-deps-2.8.0+cpu.zip
rm libtorch-shared-with-deps-2.8.0+cpu.zip
```

cmake 命令：
```bash
cmake .. -DCMAKE_PREFIX_PATH="/home/amonologue/Projects/WebServer/include/third_party/libtorch"
cmake --build .
```

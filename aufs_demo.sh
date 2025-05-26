#!/bin/bash
# OverlayFS 演示脚本：多层挂载、读写和写时复制（建议在 WSL2 Linux 主目录下运行）

set -e

# 统一在 ~/overlay-test 目录下操作，确保在 ext4 文件系统
BASE=~/overlay-test
mkdir -p $BASE/overlay-lower1 $BASE/overlay-lower2 $BASE/overlay-upper $BASE/overlay-work $BASE/overlay-mnt

echo "lower1-file" > $BASE/overlay-lower1/layer1.txt
echo "lower2-file" > $BASE/overlay-lower2/layer2.txt
echo "upper-file" > $BASE/overlay-upper/upper.txt

# 挂载 OverlayFS
mount -t overlay overlay -o lowerdir=$BASE/overlay-lower1:$BASE/overlay-lower2,upperdir=$BASE/overlay-upper,workdir=$BASE/overlay-work $BASE/overlay-mnt

echo "--- 挂载后目录内容 ---"
ls $BASE/overlay-mnt
cat $BASE/overlay-mnt/layer1.txt || true
cat $BASE/overlay-mnt/layer2.txt || true
cat $BASE/overlay-mnt/upper.txt || true

echo "--- 在挂载点写入新文件 ---"
echo "hello overlay" > $BASE/overlay-mnt/newfile.txt
ls $BASE/overlay-mnt

# 修改只读层文件，触发写时复制
cp $BASE/overlay-mnt/layer1.txt $BASE/overlay-mnt/layer1.txt.bak
echo "change" > $BASE/overlay-mnt/layer1.txt
cat $BASE/overlay-mnt/layer1.txt

# 查看 upper 层内容，验证写时复制
ls $BASE/overlay-upper
cat $BASE/overlay-upper/layer1.txt

# 查看 mount层 内容
ls $BASE/overlay-mnt

# 卸载
umount $BASE/overlay-mnt

# 清理
rm -rf $BASE/overlay-lower1 $BASE/overlay-lower2 $BASE/overlay-upper $BASE/overlay-work $BASE/overlay-mnt

echo "OverlayFS demo 完成。"
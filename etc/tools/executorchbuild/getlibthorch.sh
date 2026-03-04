################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

#cd /work/deps
#mkdir -p libtorch
#cd libtorch

rm -rf ./libtorch

# Example for CPU-only Linux x86_64, adjust URL per your torch version/needs
curl -L -o libtorch.tar.gz \
    "https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.3.0%2Bcpu.zip" # URL example; pick matching version/arch

# If it's a .zip:
#apt-get update && apt-get install -y unzip
unzip libtorch.tar.gz
# This usually creates /work/deps/libtorch/libtorch; you might want to mv it up:
#mv libtorch/* .
#rmdir libtorch

rm -rf /work/deps/libtorch
mkdir /work/deps/libtorch
mkdir /work/deps/libtorch/include
mkdir /work/deps/libtorch/lib
cp -r libtorch/include/* /work/deps/libtorch/include
cp -r libtorch/lib/* /work/deps/libtorch/lib

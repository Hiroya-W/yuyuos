# YuyuOS

ゼロからの OS 自作入門で作成している自作 OS

## Original Code

YuyuOS は MikanOS をベースとして開発されています。

https://github.com/uchan-nos/mikanos

## Build && Run

環境変数をセットする。

```sh
source ~/osbook/devenv/buildenv.sh
source ~/edk2/edksetup.sh
```

カーネルをビルドする

```
cd ~/workspace/yuyuos/kernel
make
```

ブートローダをビルドする

```
cd ~/edk2
build
```

QEMUで実行する

```
cd ~/edk2
$HOME/osbook/devenv/run_qemu.sh Build/YuyuLoaderX64/DEBUG_CLANG38/X64/Loader.efi $HOME/workspaces/yuyuos/kernel/kernel.elf 
```
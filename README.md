# File Manager

Lingmo File Manager, simple to use, beautiful, and retain the classic PC interactive design.

## Dependencies

File Manager is built on [lingmo-toolkit](https://github.com/lingmoos/lingmo-toolkit) and
the files library of [lingmo-framework](https://github.com/lingmoos/lingmo-framework);
install them first. The mount service needs FUSE, libssh, libsmbclient, libmtp and
libudev, and zlib, bzip2, liblzma and LZFSE for disk images; archives need libarchive
and libzip. GoogleTest for the tests.

Debian/Ubuntu:

```shell
sudo apt install build-essential cmake pkg-config libicu-dev libarchive-dev libzip-dev \
    libfuse3-dev libssh-dev libsmbclient-dev libmtp-dev libudev-dev zlib1g-dev libbz2-dev \
    liblzma-dev liblzfse-dev libgtest-dev
```

## Build

```shell
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

## Tests

```shell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

BSD-3-Clause; see [LICENSE](LICENSE).

# Text Editor

A plain-text editor for Cutefish, with tabs, session restore, encoding
detection and syntax highlighting for common languages.

## Dependencies

Qt 6 (Core, Gui, Quick, Quick Controls, DBus, Concurrent, Linguist tools),
FishUI and the Cutefish framework (`cutefish-framework`).

```shell
sudo apt install cmake qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools
```

## Build

```shell
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ..
make
```

## Install

```shell
sudo make install
```

## Tests

```shell
cmake -DBUILD_TESTING=ON ..
make
ctest
```

## License

This project has been licensed by GPLv3.

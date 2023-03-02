## vtabledump - Efficiently dump C++ virtual function tables from ELF objects

## Building
1. Install a C++17 compiler and the [CMake](https://cmake.org/)
2. Open the project folder from a terminal
3. Run `cmake -DCMAKE_BUILD_TYPE=Release -B build` to generate build files
4. Run `cmake --build build` to compile

## Usage
```
USAGE: vtabledump <file> [--mangled] [--json] [--filter=<regex>]
```

Example: Dumping the class `CTFPlayer` from the Team Fortress 2 server library: ([Command output](https://gist.github.com/hkva/aa9ffc72cf5e4d313c2cb05db8a59e36#file-example1-txt))
```
vtabledump.exe server_srv.so --filter="CTFPlayer"
```

Example: Dumping the same class, but as JSON and with mangled names: ([Command output](https://gist.github.com/hkva/aa9ffc72cf5e4d313c2cb05db8a59e36#file-example2-json))
```
vtabledump.exe server_srv.so --filter="CTFPlayer" --mangled --json
```

## Contributing

All contributions are welcome. If you find a bug or want to request a feature, please [create an issue](https://github.com/hkva/vtabledump/issues/new).

## License
[MIT](/LICENSE)

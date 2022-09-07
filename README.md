Tested with Arch Linux, GCC 12.2.0

Requires a reasonably recent Boost library, I have 1.79.0

```
cd ./build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4
./demo
```

Then run

```
curl http://localhost:7070
```

Should return the status code of `http://google.com` - which is always 301
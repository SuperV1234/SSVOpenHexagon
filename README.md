[Open Hexagon](http://www.facebook.com/OpenHexagon) - [by Vittorio Romeo](http://vittorioromeo.info) 
===================================================================================================
###[Official README](http://vittorioromeo.info/Downloads/OpenHexagon/README.html)  


## How to build on Linux

Tested on `Linux Mint 15 x64` and `Linux Mint 15 x86`.  
Tested compilers: **g++ 4.7.2**, **g++ 4.8.0**, **g++ 4.8.1**, **clang++ 3.2**, [**clang++ 3.4**](http://llvm.org/apt/).

1. Clone this repository
```bash
git clone git://github.com/SuperV1234/SSVOpenHexagon.git
cd SSVOpenHexagon
```

2. Pull everything recursively (submodules!)
```bash
./init-repository.sh`
```

3. You should now have all submodules updated and pulled

4. If your distribution packages SFML 2 you can install it through your package manager otherwise build and install it [manually](http://sfmlcoder.wordpress.com/2011/08/16/building-sfml-2-0-with-make-for-gcc/)

5. Open Hexagon requires `liblua5.1-dev` and `sparsehash` libraries to compile
```bash
sudo apt-get install liblua5.1-dev
```
If your distribution doesn't package sparsehash 2 you can get latest version [from here](https://code.google.com/p/sparsehash/downloads/list)

6. Build dependencies and Open Hexagon
```bash
cd SSVOpenHexagon
./build-repository.sh
````

8. Open Hexagon should now be installed on your system - download the assets from a released binary version [from my website](http://vittorioromeo.info) and put them in the installed game folder

# Requirements:
1. Api key from openrouter.ai
2. emscripten installed:
```code 
sudo apt install git python3 cmake build-essential\
cd ~/Downloads
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```
# Build 
## Build desktop
```code 
rm -rf build_desktop
mkdir build_desktop
cd build_desktop
cmake ..
make
./SchoolBot
```
## Build web
```code 
rm -rf build_web
mkdir build_web
cd build_web
emcmake cmake ..
make
```

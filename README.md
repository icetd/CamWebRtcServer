## build
```
./scripts/build_x264.sh
git submodule update --init
mkdir build && cd build
cmake ..
make
```
### test env
```
gcc (Ubuntu 9.4.0-1ubuntu1~20.04.2) 9.4.0
g++ (Ubuntu 9.4.0-1ubuntu1~20.04.2) 9.4.0
```

Make sure your build tools version is not very old

## config
config with file configs/config.ini
```
[log]
level = NOTICE

[video]
width = 1920
height = 1080
fps = 30
;[CRF | ABR]
method = CRF      
;if ABR bitrate is efficient
bitrate = 1440    
;if CRF rf_constant is efficient [0 - 51] degree of loss; default is 23; rf_constant < 10  frame size is bigger than live555 max frame size);
rf_constant = 23  
;IDR-frames 
min_ikeyint = 30
max_ikeyiny = 60
;[YUY2 | MJPEG]
format = MJPEG
device = /dev/video0

[Server]
port = 8000
```
## function
- support mjpeg and yuy2 format usb camera.
- support h264 codec.
- support webrtc h264 stream

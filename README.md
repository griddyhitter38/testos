# testos


I don't actually know would it work besides linux/unix typed os but i'll try running qemu thru the windows

to compile use this:
    make

to run qemu use this commands:

qemu-system-x86_64 -m 512 \   
    -device qemu-xhci \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
    -drive format=raw,file=build/testos.img
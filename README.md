# ShatFS
A Plan9 style file system for the Raspberry Pi Sense Hat

## What it does

All testing was done using 9Front.  Be sure that the I²C drivers are in the kernel and available in the namespace. 

  bind -b '#J' /dev

Shatfs looks for the standard addresses of the devices on the Sense hat in /dev/i2c1/

By default, shatfs with post to /srv/shatfs and mount to /mnt/shat.

In /mnt/shat are the files;
+ tempp and press are tempurature in C and pressure in hPa from the LSP25H
+ temph and humid are temperature in C and humidity in %RH from the HTS221
+ accel, gyro, and mag are the 9 axis of the LSM9DS1
+ led lets you write to the 8x8 LED grid

## ToDo

Make the accel, gyro, and mag files do continous output.  Right now they give 1 set of X Y Z values every time they are read.

Write something to convert plan9 image format to what the LED grid expects.  As of now, it will write all 0 to the grid to clear it on start up.  The input is otherwise untested.



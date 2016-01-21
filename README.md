# vidbrot
Video Mandlebrot Demo for Linux

This project is a simple demo of grabbing YUV frames from a video-for-linux device, converting to RGB using GLSL, and then iteratively remapping the image into the complex plane via the Mandelbrot equation. GL with GLSL support and openglut (or glut, with a change to the #include) are required. The glut menu allows you to tweak iterations and other rendering parameters.

I was prompted to write this because there were no simple examples for getting video data into the GL pipeline under Linux, feel free to rip apart whatever you need for your own projects.

![screenshot](https://cloud.githubusercontent.com/assets/1423804/12474986/4e31fe2e-bfd4-11e5-91e3-26a6c9e17c3f.jpg)

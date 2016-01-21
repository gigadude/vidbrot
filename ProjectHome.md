This project is a simple demo of grabbing YUV frames from a video-for-linux device, converting to RGB using GLSL, and then iteratively remapping the image into the complex plane via the Mandelbrot equation. GL with GLSL support and openglut (or glut, with a change to the #include) are required. The glut menu allows you to tweak iterations and other rendering parameters.

I was prompted to write this because there were no simple examples for getting video data into the GL pipeline under Linux, feel free to rip apart whatever you need for your own projects.

![http://vidbrot.googlecode.com/files/screenshot.jpg](http://vidbrot.googlecode.com/files/screenshot.jpg)
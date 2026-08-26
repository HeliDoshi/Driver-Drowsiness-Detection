# Driver-Drowsiness-Detection
Reliable driver drowsiness detection using face / eye analysis. This repository contains code and models to detect driver drowsiness from webcam, video files, or recorded images using a combination of classical (eye-aspect-ratio) and deep-learning-based approaches. The project aims to provide a real-time alerting prototype and training/evaluation scripts to reproduce results.

## Features

Real-time webcam demo with audible/visual alerts
Support for video-file and image inference
Two detection approaches:
Lightweight EAR (Eye Aspect Ratio) method for CPU-friendly operation
CNN-based classifier (optionally with temporal smoothing / LSTM) for higher accuracy
Training and evaluation scripts
Model checkpoint saving and inference utilities

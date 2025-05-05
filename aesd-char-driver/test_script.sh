#!/bin/bash

# Unload the previous module
echo "Unloading the module..."
sudo ./aesdchar_unload || echo "No module loaded"

# Clean the build directory
echo "Cleaning up..."
make clean

# Build the module
echo "Building the module..."
make || { echo "Build failed"; exit 1; }

# Load the new module
echo "Loading the module..."
sudo ./aesdchar_load || { echo "Module load failed"; exit 1; }

# Check if the device is created
echo "Verifying /dev/aesdchar..."
ls /dev/ | grep aesd || { echo "Device not created"; exit 1; }

# Test writing to the device
echo "Testing write operation..."
echo "Hello, AESD!" | sudo tee /dev/aesdchar

# Test reading from the device (if implemented)
echo "Testing read operation..."
sudo cat /dev/aesdchar || echo "Read failed or not implemented yet"

# Check kernel logs for driver output
echo "Checking kernel logs..."
dmesg | tail > dmesg_output.log
cat dmesg_output.log


echo "Running drivertest now"
sudo /home/sijeo/Coursera_Embedded_Linux/assignments-3-and-later-sijeo-philip/assignment-autotest/test/assignment8/drivertest.sh

echo "Running sockettest now"
sudo /home/sijeo/Coursera_Embedded_Linux/assignments-3-and-later-sijeo-philip/assignment-autotest/test/assignment5/sockettest.sh



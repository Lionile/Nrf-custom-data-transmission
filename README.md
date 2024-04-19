# Nrf Custom Data Transmission

---

## Needed project references

### PC Code - Visual Studio 2022 (NuGet)

SixLabors.ImageSharp -Version 3.1.3
System.IO.Ports -Version 8.0.0

---

## Uploading the code

### Arduino IDE

To upload the Arduino code, if you don't know how to use PlatformIO you can copy the code from the cpp file to an Arduino project. Since it is just normal c++ code, there shouldn't be any errors when compiling it with the Arduino IDE.

### PlatformIO

If you want to use PlatformIO (in Visual Studio Code), after cloning the repo, you have to go to PlatformIO Home->Projects and Add Existing. Then you add each file separately (receiver and transmitter) by going into the folder which contains a platformio.ini file. They might not appear right away after adding them, so try restarting VS Code if they don't.
The platformio.ini file contains the setup for the project. You can find the needed platform and board identifiers on PlatformIO [docs](https://docs.platformio.org/en/latest/boards/index.html).
If you need to change the serial monitor baud rate, you need to change it in both the Serial.begin statement and change the monitor_speed atribute in the platform.ini file.
Finally, before uploading you need to set the upload_port to whatever port your board is on, and click the Upload button at the bottom of the screen.
